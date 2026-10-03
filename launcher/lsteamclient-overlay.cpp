/* What Steam's in-game overlay would show, opened in the Mac Steam client instead.
 *
 * A game asks the overlay for a web page or a store page through ISteamFriends. Steam's Mac
 * overlay is never loaded into a Wine process, so those calls did nothing (Teardown's "manage
 * workshop mods" button). scripts/build-lsteamclient.sh adds a call to these functions in front
 * of each such call in lsteamclient's unix side. */
#include <spawn.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>

extern char **environ;

static void open_in_steam( const char *link )
{
    char *argv[] = { (char *)"open", (char *)link, NULL };
    pid_t pid;

    if (posix_spawn( &pid, "/usr/bin/open", NULL, NULL, argv, environ )) return;
    waitpid( pid, NULL, 0 );
}

void hadron_overlay_open_url( const char *url )
{
    char link[4096];

    if (!url || (strncmp( url, "http://", 7 ) && strncmp( url, "https://", 8 ))) return;
    if (snprintf( link, sizeof(link), "steam://openurl/%s", url ) >= (int)sizeof(link)) return;
    open_in_steam( link );
}

void hadron_overlay_open_store( unsigned int app )
{
    char link[64];

    snprintf( link, sizeof(link), "steam://store/%u", app );
    open_in_steam( link );
}
