/* The retry loop of Wine's syscall_dispatcher_return_slowpath() on arm64 (Wine patch 0057), taken
 * out of Wine: a thread marks itself as waiting and signals itself with SIGUSR2 until the handler
 * has run. On macOS another thread's sigprocmask() sets the mask of every thread, so SIGUSR2 can be
 * blocked again after the loop unblocked it (findings #80). Here the main thread does that, with
 * sigprocmask(SIG_BLOCK), after the loop's first unblock and before its first pthread_kill, and
 * leaves it blocked. With the unblock made once before the loop ("once") the signal stays pending
 * and the loop spins until the test gives up; with the unblock made before each pthread_kill
 * ("each", as 0057 does) the next try delivers it.
 * usage: slowpath-reblock [once|each] [rounds, default 100]; exit status 2 if a round spun. */
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int unblock_each;
static volatile sig_atomic_t delivered;
static atomic_int stage;  /* 1: the loop has unblocked SIGUSR2, 2: the main thread has blocked it again */

static void usr2_handler( int sig, siginfo_t *si, void *ctx )
{
    delivered = 1;  /* Wine's handler switches to the frame's context instead, so the loop never sees it */
}

static double now( void )
{
    struct timespec ts;
    clock_gettime( CLOCK_MONOTONIC, &ts );
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

static void *slowpath( void *arg )
{
    unsigned long *tries = arg;
    sigset_t mask;
    double start;
    unsigned long i;

    sigemptyset( &mask );
    sigaddset( &mask, SIGUSR2 );
    pthread_sigmask( SIG_BLOCK, &mask, NULL );  /* the thread arrives with SIGUSR2 blocked */
    delivered = 0;
    if (!unblock_each) pthread_sigmask( SIG_UNBLOCK, &mask, NULL );
    start = now();
    for (i = 0; !delivered; i++)
    {
        if (unblock_each) pthread_sigmask( SIG_UNBLOCK, &mask, NULL );
        if (i == 0)
        {
            /* another thread's sigprocmask() lands between the unblock and the signal */
            atomic_store( &stage, 1 );
            while (atomic_load( &stage ) != 2) sched_yield();
        }
        pthread_kill( pthread_self(), SIGUSR2 );
        if (delivered) break;
        if (now() - start > 0.2) break;  /* Wine's loop would spin for ever */
        sched_yield();
    }
    *tries = delivered ? i + 1 : 0;
    return NULL;
}

int main( int argc, char **argv )
{
    int rounds = argc > 2 ? atoi( argv[2] ) : 100, round, spun = 0;
    unsigned long tries, max_tries = 0;
    struct sigaction sa;
    sigset_t mask;
    pthread_t thread;

    unblock_each = !(argc > 1 && !strcmp( argv[1], "once" ));
    memset( &sa, 0, sizeof(sa) );
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sa.sa_sigaction = usr2_handler;
    sigaction( SIGUSR2, &sa, NULL );
    sigemptyset( &mask );
    sigaddset( &mask, SIGUSR2 );

    for (round = 0; round < rounds; round++)
    {
        atomic_store( &stage, 0 );
        pthread_create( &thread, NULL, slowpath, &tries );
        while (atomic_load( &stage ) != 1) sched_yield();
        sigprocmask( SIG_BLOCK, &mask, NULL );  /* every thread's mask, on macOS */
        atomic_store( &stage, 2 );
        pthread_join( thread, NULL );
        sigprocmask( SIG_UNBLOCK, &mask, NULL );
        if (!tries) spun++;
        else if (tries > max_tries) max_tries = tries;
    }
    printf( "unblock %s: %d rounds, %d spun with SIGUSR2 pending, the others delivered within %lu tries\n",
            unblock_each ? "before each try" : "once before the loop", rounds, spun, max_tries );
    return spun ? 2 : 0;
}
