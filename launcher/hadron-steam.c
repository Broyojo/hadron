/* hadron-steam.exe: minimal stand-in for Steam's Windows process, like Proton's steam.exe.
 *
 * Games' steam_api checks HKCU\Software\Valve\Steam\ActiveProcess to decide whether Steam is
 * running and where steamclient.dll lives. Register this process there, launch the game and
 * wait for it; Wine redirects the game's steamclient.dll to lsteamclient, which talks to the
 * native Mac Steam client.
 *
 * Usage: hadron-steam.exe <game.exe> [args...]
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <wchar.h>

static void set_dword( HKEY key, const WCHAR *name, DWORD value )
{
    RegSetValueExW( key, name, 0, REG_DWORD, (const BYTE *)&value, sizeof(value) );
}

static void set_string( HKEY key, const WCHAR *name, const WCHAR *value )
{
    RegSetValueExW( key, name, 0, REG_SZ, (const BYTE *)value, (wcslen( value ) + 1) * sizeof(WCHAR) );
}

static void register_steam_process( const WCHAR *steam_dir )
{
    WCHAR path[MAX_PATH];
    HKEY key;

    if (RegCreateKeyExW( HKEY_CURRENT_USER, L"Software\\Valve\\Steam\\ActiveProcess", 0, NULL, 0,
                         KEY_ALL_ACCESS, NULL, &key, NULL )) return;
    set_dword( key, L"pid", GetCurrentProcessId() );
    set_dword( key, L"ActiveUser", 1 );
    swprintf( path, MAX_PATH, L"%ls\\steamclient.dll", steam_dir );
    set_string( key, L"SteamClientDll", path );
    swprintf( path, MAX_PATH, L"%ls\\steamclient64.dll", steam_dir );
    set_string( key, L"SteamClientDll64", path );
    RegCloseKey( key );

    if (RegCreateKeyExW( HKEY_CURRENT_USER, L"Software\\Valve\\Steam", 0, NULL, 0,
                         KEY_ALL_ACCESS, NULL, &key, NULL )) return;
    set_string( key, L"SteamPath", steam_dir );
    swprintf( path, MAX_PATH, L"%ls\\steam.exe", steam_dir );
    set_string( key, L"SteamExe", path );
    RegCloseKey( key );

    /* The steam:// protocol, as Steam's installer registers it: games open their store and
     * workshop pages with ShellExecute("steam://url/..."). Machine-wide, like the installer:
     * Wine's shell looks protocols up in HKLM's classes only. */
    if (RegCreateKeyExW( HKEY_LOCAL_MACHINE, L"Software\\Classes\\steam", 0, NULL, 0,
                         KEY_ALL_ACCESS, NULL, &key, NULL )) return;
    set_string( key, NULL, L"URL:steam protocol" );
    set_string( key, L"URL Protocol", L"" );
    RegCloseKey( key );
    if (RegCreateKeyExW( HKEY_LOCAL_MACHINE, L"Software\\Classes\\steam\\shell\\open\\command", 0, NULL, 0,
                         KEY_ALL_ACCESS, NULL, &key, NULL )) return;
    swprintf( path, MAX_PATH, L"\"%ls\\steam.exe\" -- \"%%1\"", steam_dir );
    set_string( key, NULL, path );
    RegCloseKey( key );
}

static void clear_steam_process(void)
{
    HKEY key;

    if (RegOpenKeyExW( HKEY_CURRENT_USER, L"Software\\Valve\\Steam\\ActiveProcess", 0, KEY_ALL_ACCESS, &key )) return;
    set_dword( key, L"pid", 0 );
    RegCloseKey( key );
}

/* Hand a steam:// URL to Mac Steam: winebrowser opens it with the host's URL handler. */
static int open_steam_url( const WCHAR *url )
{
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    WCHAR cmd[2048];

    swprintf( cmd, sizeof(cmd) / sizeof(cmd[0]), L"winebrowser \"%ls\"", url );
    if (!CreateProcessW( NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi )) return 1;
    WaitForSingleObject( pi.hProcess, INFINITE );
    CloseHandle( pi.hThread );
    CloseHandle( pi.hProcess );
    return 0;
}

/* Games run the SteamExe registered below (this program, staged as steam.exe) to talk to the
 * Steam client: steam:// links (also as "-- <link>", the registered protocol command),
 * "-applaunch <appid>", or nothing to bring Steam up. Forward
 * those to Mac Steam. Returns -1 when argv is a game to launch instead. */
static int forward_steam_request( int argc, WCHAR **argv )
{
    WCHAR url[64];

    if (argc < 2) return open_steam_url( L"steam://open/main" );
    if (!wcscmp( argv[1], L"--" ) && argc > 2) return open_steam_url( argv[2] );
    if (!wcsnicmp( argv[1], L"steam:", 6 )) return open_steam_url( argv[1] );
    if (!wcsicmp( argv[1], L"-applaunch" ) && argc > 2)
    {
        swprintf( url, sizeof(url) / sizeof(url[0]), L"steam://rungameid/%ls", argv[2] );
        return open_steam_url( url );
    }
    return -1;
}

int wmain( int argc, WCHAR **argv )
{
    int forwarded = forward_steam_request( argc, argv );

    const WCHAR *steam_dir = L"C:\\Program Files (x86)\\Steam";
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    WCHAR *cmdline;
    DWORD exit_code = 1;

    if (forwarded >= 0) return forwarded;

    /* everything after our own name is the game's command line, passed through verbatim */
    cmdline = GetCommandLineW();
    if (*cmdline == '"') cmdline = wcschr( cmdline + 1, '"' ) + 1;
    else while (*cmdline && *cmdline != ' ') cmdline++;
    while (*cmdline == ' ') cmdline++;

    register_steam_process( steam_dir );

    /* the game starts in the directory this was started in: Steam's working directory for the
     * launch, which is not always the executable's own */
    if (!CreateProcessW( NULL, cmdline, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi ))
    {
        fwprintf( stderr, L"hadron-steam: failed to start %ls: error %lu\n", argv[1], GetLastError() );
        clear_steam_process();
        return 1;
    }
    WaitForSingleObject( pi.hProcess, INFINITE );
    GetExitCodeProcess( pi.hProcess, &exit_code );
    clear_steam_process();
    return exit_code;
}
