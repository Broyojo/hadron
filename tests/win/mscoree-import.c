/* A native program that imports mscoree.dll, the .NET runtime's loader, as Fable Anniversary's
 * executable does (issue #57): without Wine Mono, or with mscoree switched off, it does not load
 * at all. Prints the runtime version mscoree reports.
 * Usage: mscoree-import.exe */
#include <stdio.h>
#include <windows.h>

HRESULT WINAPI GetCORVersion( LPWSTR buffer, DWORD size, DWORD *len );

int main( void )
{
    WCHAR version[64];
    DWORD len = 0;
    HRESULT hr = GetCORVersion( version, sizeof(version) / sizeof(version[0]), &len );

    if (FAILED(hr)) { printf( "GetCORVersion failed: %#lx\n", hr ); return 1; }
    printf( "mscoree loaded; runtime %ls\n", version );
    return 0;
}
