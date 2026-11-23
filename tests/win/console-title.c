/* A console program started the way Steam starts a game: it sets its console's title and reads its
 * screen buffer, as .NET's Console.Title and Console.Clear do at the start of Absolum (#184). On
 * Windows, a console program that a windowed program (Steam) starts gets a console of its own, and
 * both calls work. Started through hadron-steam.exe without one, SetConsoleTitleW failed with
 * error 12 and the game died. Its own output goes to that console, so the results go to the file
 * named on the command line, then PASS or FAIL in its last line.
 * Usage: console-title.exe <result file>
 * Build: toolchains/llvm-mingw/bin/x86_64-w64-mingw32-clang -o console-title.exe tests/win/console-title.c */
#include <stdio.h>
#include <windows.h>

int main( int argc, char **argv )
{
    CONSOLE_SCREEN_BUFFER_INFO info;
    BOOL title, buffer;
    DWORD title_error, buffer_error;
    FILE *out;

    if (argc < 2) return 2;
    title = SetConsoleTitleW( L"console-title" );
    title_error = title ? 0 : GetLastError();
    buffer = GetConsoleScreenBufferInfo( GetStdHandle( STD_OUTPUT_HANDLE ), &info );
    buffer_error = buffer ? 0 : GetLastError();
    if (!(out = fopen( argv[1], "w" ))) return 2;
    fprintf( out, "console window %p\n", GetConsoleWindow() );
    fprintf( out, "SetConsoleTitleW: %s (error %lu)\n", title ? "ok" : "failed", title_error );
    fprintf( out, "GetConsoleScreenBufferInfo: %s (error %lu)\n", buffer ? "ok" : "failed", buffer_error );
    fprintf( out, "%s\n", title && buffer ? "PASS" : "FAIL" );
    fclose( out );
    return title && buffer ? 0 : 1;
}
