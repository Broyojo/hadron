/* Two threads of a traced process that take a signal at the same time can swap signals: one thread
 * gets the other's signal and its own is lost (findings #80). This is a debugger of its own, without
 * lldb: the child calls PT_TRACE_ME and PT_SIGEXC, so each signal it takes stops the thread with an
 * EXC_SOFTWARE/EXC_SOFT_SIGNAL exception, and the parent answers each one as debugserver does, with
 * PT_THUPDATE(thread, signal) and a reply to the exception message.
 *
 * In the child, one thread signals itself in a loop (pthread_kill(pthread_self(), SIGUSR2)) and
 * checks that its handler ran before pthread_kill returned, as Wine's syscall return does on arm64;
 * another sends SIGUSR1 to four sleeping threads, as wineserver does to suspend threads. The child
 * counts the self-signals whose handler didn't run on the thread ("lost") and SIGUSR2 handlers run
 * on a thread that didn't send one ("strays"; that thread's SIGUSR1 is lost instead); the parent
 * counts PT_THUPDATE calls that failed. debugserver answers a batch of exceptions after suspending
 * the task; this answers them one at a time, which makes no difference to the kernel's state.
 *
 * usage: traced-signal-swap [seconds, default 5] [notrace]
 * build: clang -O2 -o traced-signal-swap tests/mac/traced-signal-swap.c */
#include <errno.h>
#include <mach/mach.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <unistd.h>

static _Thread_local volatile int waiting;
static atomic_ulong sent, lost, strays, usr1_sent, usr1_seen;
static volatile int stop;
static pthread_t sleepers[4];

static void usr2_handler( int sig, siginfo_t *si, void *ctx )
{
    if (!waiting) atomic_fetch_add( &strays, 1 );
    waiting = 0;
}

static void usr1_handler( int sig, siginfo_t *si, void *ctx )
{
    atomic_fetch_add( &usr1_seen, 1 );
}

static void *self_signaller( void *arg )
{
    while (!stop)
    {
        waiting = 1;
        atomic_fetch_add( &sent, 1 );
        pthread_kill( pthread_self(), SIGUSR2 );
        if (waiting) atomic_fetch_add( &lost, 1 );  /* returned without the handler having run here */
        waiting = 0;
    }
    return NULL;
}

static void *sleeper( void *arg )
{
    waiting = 0;  /* first use of the thread-local outside the handler */
    while (!stop) usleep( 1000 );
    return NULL;
}

static void *usr1_sender( void *arg )
{
    unsigned int i = 0;
    while (!stop)
    {
        if (!pthread_kill( sleepers[i++ % 4], SIGUSR1 )) atomic_fetch_add( &usr1_sent, 1 );
        usleep( 50 );
    }
    return NULL;
}

static int child( int secs, int traced )
{
    struct sigaction sa;
    pthread_t self_thread, sender;
    int i;

    if (traced && (ptrace( PT_TRACE_ME, 0, 0, 0 ) || ptrace( PT_SIGEXC, 0, 0, 0 )))
    {
        perror( "ptrace" );
        return 1;
    }
    memset( &sa, 0, sizeof(sa) );
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sigemptyset( &sa.sa_mask );
    sigaddset( &sa.sa_mask, SIGUSR1 );
    sigaddset( &sa.sa_mask, SIGUSR2 );
    sa.sa_sigaction = usr2_handler;
    sigaction( SIGUSR2, &sa, NULL );
    sa.sa_sigaction = usr1_handler;
    sigaction( SIGUSR1, &sa, NULL );

    for (i = 0; i < 4; i++) pthread_create( &sleepers[i], NULL, sleeper, NULL );
    pthread_create( &self_thread, NULL, self_signaller, NULL );
    pthread_create( &sender, NULL, usr1_sender, NULL );
    sleep( secs );
    stop = 1;
    pthread_join( self_thread, NULL );
    pthread_join( sender, NULL );
    for (i = 0; i < 4; i++) pthread_join( sleepers[i], NULL );
    /* a SIGUSR1 sent to a thread that already has one pending merges with it, so fewer handled than sent
     * is only a loss when traced shows it and not traced doesn't */
    printf( "child (%s): %lu self-signals, %lu lost, %lu SIGUSR2 strays, %lu SIGUSR1 sent, %lu handled\n",
            traced ? "traced" : "not traced", sent, lost, strays, usr1_sent, usr1_seen );
    return lost || strays ? 2 : 0;
}

/* exception_raise request and reply (EXCEPTION_DEFAULT, 32-bit codes) */
struct exc_request
{
    mach_msg_header_t head;
    mach_msg_body_t body;
    mach_msg_port_descriptor_t thread;
    mach_msg_port_descriptor_t task;
    NDR_record_t ndr;
    exception_type_t exception;
    mach_msg_type_number_t code_count;
    integer_t code[2];
    mach_msg_trailer_t trailer;
};

struct exc_reply
{
    mach_msg_header_t head;
    NDR_record_t ndr;
    kern_return_t ret;
};

static mach_port_t exc_port;
static pid_t child_pid;
static atomic_ulong stops, updates_failed;

static void *exception_server( void *arg )
{
    for (;;)
    {
        struct exc_request req;
        struct exc_reply reply;

        memset( &req, 0, sizeof(req) );
        if (mach_msg( &req.head, MACH_RCV_MSG, 0, sizeof(req), exc_port, MACH_MSG_TIMEOUT_NONE,
                      MACH_PORT_NULL ) != MACH_MSG_SUCCESS)
            break;
        if (req.exception == EXC_SOFTWARE && req.code[0] == EXC_SOFT_SIGNAL)
        {
            atomic_fetch_add( &stops, 1 );
            /* deliver the signal the thread stopped with, as a debugger passing it on does */
            if (ptrace( PT_THUPDATE, child_pid, (caddr_t)(uintptr_t)req.thread.name, req.code[1] ))
            {
                if (atomic_fetch_add( &updates_failed, 1 ) < 5)
                    fprintf( stderr, "PT_THUPDATE(thread %#x, signal %d): %s\n", req.thread.name, req.code[1],
                             strerror( errno ) );
            }
        }
        mach_port_deallocate( mach_task_self(), req.thread.name );
        mach_port_deallocate( mach_task_self(), req.task.name );

        memset( &reply, 0, sizeof(reply) );
        reply.head.msgh_bits = MACH_MSGH_BITS( MACH_MSGH_BITS_REMOTE( req.head.msgh_bits ), 0 );
        reply.head.msgh_remote_port = req.head.msgh_remote_port;
        reply.head.msgh_size = sizeof(reply);
        reply.head.msgh_id = req.head.msgh_id + 100;
        reply.ndr = NDR_record;
        reply.ret = KERN_SUCCESS;
        mach_msg( &reply.head, MACH_SEND_MSG, sizeof(reply), 0, MACH_PORT_NULL, MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL );
    }
    return NULL;
}

int main( int argc, char **argv )
{
    int secs = argc > 1 ? atoi( argv[1] ) : 5;
    int traced = !(argc > 2 && !strcmp( argv[2], "notrace" ));
    pthread_t server;
    int status;

    /* task exception ports are inherited across fork: the child's EXC_SOFT_SIGNAL exceptions come here */
    mach_port_allocate( mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &exc_port );
    mach_port_insert_right( mach_task_self(), exc_port, exc_port, MACH_MSG_TYPE_MAKE_SEND );
    task_set_exception_ports( mach_task_self(), EXC_MASK_SOFTWARE, exc_port, EXCEPTION_DEFAULT, THREAD_STATE_NONE );

    fflush( stdout );
    if (!(child_pid = fork())) return child( secs, traced );
    task_set_exception_ports( mach_task_self(), EXC_MASK_SOFTWARE, MACH_PORT_NULL, EXCEPTION_DEFAULT, THREAD_STATE_NONE );
    pthread_create( &server, NULL, exception_server, NULL );
    waitpid( child_pid, &status, 0 );
    printf( "parent: %lu signal stops, %lu PT_THUPDATE failed\n", stops, updates_failed );
    return WIFEXITED( status ) ? WEXITSTATUS( status ) : 3;
}
