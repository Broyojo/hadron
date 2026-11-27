/* Under lldb, a signal a thread stopped with is lost when lldb steps another thread over a
 * breakpoint at the same time (findings #80). One thread signals itself in a loop
 * (pthread_kill(pthread_self(), SIGUSR2)) and checks that its handler ran before pthread_kill
 * returned; the main thread calls tick() every millisecond, which tests/mac/lldb-signal-step.sh
 * puts a breakpoint on. Prints how many self-signals were sent and how many were lost.
 * usage: lldb-signal-step [seconds, default 5] (alone, or under lldb through the script) */
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static _Thread_local volatile int waiting;
static atomic_ulong sent, lost;
static volatile int stop;

__attribute__((noinline)) void tick( void )
{
    __asm__ volatile( "" );
}

static void usr2_handler( int sig, siginfo_t *si, void *ctx )
{
    waiting = 0;
}

static void *self_signaller( void *arg )
{
    while (!stop)
    {
        waiting = 1;
        atomic_fetch_add( &sent, 1 );
        pthread_kill( pthread_self(), SIGUSR2 );
        if (waiting) atomic_fetch_add( &lost, 1 );
        waiting = 0;
    }
    return NULL;
}

int main( int argc, char **argv )
{
    int secs = argc > 1 ? atoi( argv[1] ) : 5;
    struct sigaction sa;
    pthread_t thread;
    time_t end;

    memset( &sa, 0, sizeof(sa) );
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sa.sa_sigaction = usr2_handler;
    sigaction( SIGUSR2, &sa, NULL );
    pthread_create( &thread, NULL, self_signaller, NULL );
    for (end = time( NULL ) + secs; time( NULL ) < end;)
    {
        tick();
        usleep( 1000 );
    }
    stop = 1;
    pthread_join( thread, NULL );
    printf( "%lu self-signals, %lu lost\n", sent, lost );
    return lost ? 2 : 0;
}
