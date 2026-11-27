/* On macOS, sigprocmask() sets the signal mask of every thread in the process, not only the
 * caller's (XNU's sigprocmask calls set_procsigmask/block_procsigmask on each uthread; POSIX leaves
 * sigprocmask in a multithreaded process unspecified). steamclient.dylib's OpenSSL does it when it
 * loads: OPENSSL_cpuid_setup blocks all signals but SIGILL, SIGTRAP, SIGFPE, SIGBUS and SIGSEGV with
 * sigprocmask(SIG_SETMASK), probes instructions, and sets the old mask back (findings #80).
 *
 * One thread signals itself in a loop (pthread_kill(pthread_self(), SIGUSR2)) and checks that its
 * handler ran before pthread_kill returned, as Wine's syscall return does on arm64; another saves
 * and restores its mask around a short wait, as Wine's server_select does; the main thread does
 * what the OpenSSL probe does, with sigprocmask, or with pthread_sigmask for comparison. Prints the
 * self-signals that weren't delivered in time and whether the saving thread ends up with the probe's
 * mask.
 * usage: sigprocmask-all-threads [sigprocmask|pthread_sigmask] [rounds, default 2000] */
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static _Thread_local volatile int waiting;
static atomic_ulong sent, late, stuck;
static atomic_int stop;
static sigset_t saver_final;

static void usr2_handler( int sig, siginfo_t *si, void *ctx )
{
    waiting = 0;
}

static void *self_signaller( void *arg )
{
    while (!atomic_load( &stop ))
    {
        waiting = 1;
        atomic_fetch_add( &sent, 1 );
        pthread_kill( pthread_self(), SIGUSR2 );
        if (waiting)
        {
            atomic_fetch_add( &late, 1 );
            /* still pending once the mask is set back, but macOS delivers it only when the thread is
             * signalled again: setting another thread's mask doesn't make it check for signals */
            while (waiting && !atomic_load( &stop )) sched_yield();
            if (waiting) atomic_fetch_add( &stuck, 1 );
        }
        waiting = 0;
    }
    return NULL;
}

static void *saver( void *arg )
{
    sigset_t block, old;

    sigemptyset( &block );
    sigaddset( &block, SIGUSR1 );
    sigaddset( &block, SIGUSR2 );
    while (!atomic_load( &stop ))
    {
        pthread_sigmask( SIG_BLOCK, &block, &old );
        usleep( 10 );
        pthread_sigmask( SIG_SETMASK, &old, NULL );
    }
    pthread_sigmask( SIG_BLOCK, NULL, &saver_final );
    return NULL;
}

int main( int argc, char **argv )
{
    int use_sigprocmask = !(argc > 1 && !strcmp( argv[1], "pthread_sigmask" ));
    int rounds = argc > 2 ? atoi( argv[2] ) : 2000, i;
    struct sigaction sa;
    sigset_t probe, old;
    pthread_t threads[2];

    memset( &sa, 0, sizeof(sa) );
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sa.sa_sigaction = usr2_handler;
    sigaction( SIGUSR2, &sa, NULL );

    /* OpenSSL's armcap probe mask */
    sigfillset( &probe );
    sigdelset( &probe, SIGILL );
    sigdelset( &probe, SIGTRAP );
    sigdelset( &probe, SIGFPE );
    sigdelset( &probe, SIGBUS );
    sigdelset( &probe, SIGSEGV );

    pthread_create( &threads[0], NULL, self_signaller, NULL );
    pthread_create( &threads[1], NULL, saver, NULL );
    usleep( 10000 );
    for (i = 0; i < rounds; i++)
    {
        if (use_sigprocmask) sigprocmask( SIG_SETMASK, &probe, &old );
        else pthread_sigmask( SIG_SETMASK, &probe, &old );
        usleep( 50 );  /* the probes */
        if (use_sigprocmask) sigprocmask( SIG_SETMASK, &old, NULL );
        else pthread_sigmask( SIG_SETMASK, &old, NULL );
        usleep( 200 );
    }
    atomic_store( &stop, 1 );
    pthread_join( threads[0], NULL );
    pthread_join( threads[1], NULL );
    printf( "%s: %lu self-signals, %lu not delivered before pthread_kill returned (%lu of them not until the end); "
            "the saving thread ends with mask %#x%s\n", use_sigprocmask ? "sigprocmask" : "pthread_sigmask", sent, late, stuck,
            *(unsigned int *)&saver_final, sigismember( &saver_final, SIGUSR2 ) ? " (SIGUSR2 blocked)" : "" );
    return late || sigismember( &saver_final, SIGUSR2 ) ? 2 : 0;
}
