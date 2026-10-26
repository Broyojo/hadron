/* A game that starts its real executable and exits, as DOOM (2016) starts DOOMx64vk.exe from
 * DOOMx64.exe (issue #128). Run through hadron-steam.exe: "steam-relaunch.exe" starts itself
 * with "child" and exits at once; the child, like steam_api's SteamAPI_Init, reads the pid
 * registered under HKCU\Software\Valve\Steam\ActiveProcess and checks that the process is
 * still running, first at once and then after a second (with a window up, which starts Wine's
 * desktop if nothing has), and prints PASS or FAIL. hadron-steam.exe has to stay until the
 * child has exited and then exit itself, so the caller checks that it returns.
 *
 * Build: aarch64-w64-mingw32-clang -municode -o steam-relaunch.exe steam-relaunch.c -luser32
 * Run:   wine hadron-steam.exe steam-relaunch.exe */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <wchar.h>

static BOOL steam_running( DWORD *pid )
{
    DWORD size = sizeof(*pid);
    HANDLE process;
    BOOL running;

    *pid = 0;
    if (RegGetValueW( HKEY_CURRENT_USER, L"Software\\Valve\\Steam\\ActiveProcess", L"pid",
                      RRF_RT_REG_DWORD, NULL, pid, &size ) || !*pid) return FALSE;
    if (!(process = OpenProcess( SYNCHRONIZE, FALSE, *pid ))) return FALSE;
    running = WaitForSingleObject( process, 0 ) == WAIT_TIMEOUT;
    CloseHandle( process );
    return running;
}

int wmain( int argc, WCHAR **argv )
{
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    WCHAR cmdline[MAX_PATH + 16], path[MAX_PATH];
    BOOL at_start, later;
    DWORD pid;
    HWND window;

    if (argc < 2 || wcscmp( argv[1], L"child" ))
    {
        GetModuleFileNameW( NULL, path, MAX_PATH );
        swprintf( cmdline, sizeof(cmdline) / sizeof(cmdline[0]), L"\"%ls\" child", path );
        if (!CreateProcessW( NULL, cmdline, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi ))
        {
            printf( "FAIL: could not start the child: %lu\n", GetLastError() );
            return 1;
        }
        CloseHandle( pi.hThread );
        CloseHandle( pi.hProcess );
        return 0;
    }

    Sleep( 200 );  /* the parent has exited */
    at_start = steam_running( &pid );
    window = CreateWindowW( L"STATIC", L"steam-relaunch", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100,
                            NULL, NULL, NULL, NULL );
    Sleep( 1000 );
    later = steam_running( &pid );
    if (window) DestroyWindow( window );
    printf( "%s: Steam process %lu %s at the child's start, %s a second later\n",
            at_start && later ? "PASS" : "FAIL", pid, at_start ? "running" : "not running",
            later ? "running" : "not running" );
    fflush( stdout );
    return at_start && later ? 0 : 1;
}
