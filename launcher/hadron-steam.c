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
}

static void clear_steam_process(void)
{
    HKEY key;

    if (RegOpenKeyExW( HKEY_CURRENT_USER, L"Software\\Valve\\Steam\\ActiveProcess", 0, KEY_ALL_ACCESS, &key )) return;
    set_dword( key, L"pid", 0 );
    RegCloseKey( key );
}

int wmain( int argc, WCHAR **argv )
{
    const WCHAR *steam_dir = L"C:\\Program Files (x86)\\Steam";
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    WCHAR *cmdline, *game_dir, *p;
    DWORD exit_code = 1;

    if (argc < 2)
    {
        fwprintf( stderr, L"usage: hadron-steam.exe <game.exe> [args...]\n" );
        return 1;
    }

    /* everything after our own name is the game's command line, passed through verbatim */
    cmdline = GetCommandLineW();
    if (*cmdline == '"') cmdline = wcschr( cmdline + 1, '"' ) + 1;
    else while (*cmdline && *cmdline != ' ') cmdline++;
    while (*cmdline == ' ') cmdline++;

    /* run the game from its own directory, as Steam does */
    game_dir = _wcsdup( argv[1] );
    if ((p = wcsrchr( game_dir, '\\' ))) *p = 0;
    else game_dir = NULL;

    register_steam_process( steam_dir );

    if (!CreateProcessW( NULL, cmdline, NULL, NULL, FALSE, 0, NULL, game_dir, &si, &pi ))
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
