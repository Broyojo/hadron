/* Standard handles that are Unix pipes or sockets must work as the anonymous pipes they are to a
 * Windows program. scripts/play points a game's stdout and stderr at a FIFO (HADRON_LOG), Wine
 * reports them as pipes (GetFileType), and Node and Electron (Riot Client) open a pipe stdio
 * stream with SetNamedPipeHandleState(PIPE_READMODE_BYTE | PIPE_WAIT), which failed with error 6,
 * ERROR_INVALID_HANDLE, so the program stopped with "open EBADF".
 *
 * Each standard handle that is a pipe is checked against what a pipe from CreatePipe answers:
 * GetNamedPipeHandleState and NtQueryInformationFile(FilePipeInformation) give byte mode and
 * blocking, setting byte mode and blocking succeeds, message mode fails with
 * ERROR_INVALID_PARAMETER as on a byte pipe, and the handle carries the access CreatePipe gives
 * the same end. Exit 0 when all hold, 2 when no standard handle is a pipe (nothing tested).
 * Build: x86_64-w64-mingw32-clang -O1 stdio-pipe.c -o stdio-pipe.exe
 * Usage: stdio-pipe.exe [report file] 2>&1 | cat        (stdout and stderr a pipe)
 *        stdio-pipe.exe report.txt < fifo > fifo2 2>&1   (FIFOs, as scripts/play's log)
 * The report goes to the file, or to stdout without one. */
#include <stdio.h>
#include <windows.h>
#include <winternl.h>

typedef struct
{
    ULONG ReadMode;
    ULONG CompletionMode;
} PIPE_INFO;

#define FILE_PIPE_INFORMATION_CLASS 23  /* FilePipeInformation */

static NTSTATUS (WINAPI *pNtQueryInformationFile)( HANDLE, IO_STATUS_BLOCK *, void *, ULONG, int );
static NTSTATUS (WINAPI *pNtQueryObject)( HANDLE, int, void *, ULONG, ULONG * );

static FILE *out;
static int failures;

static void check( int ok, const char *what, const char *name, unsigned long got )
{
    fprintf( out, "%s %s %s (%#lx)\n", ok ? "ok  " : "FAIL", name, what, got );
    if (!ok) failures++;
}

static ACCESS_MASK granted_access( HANDLE handle )
{
    PUBLIC_OBJECT_BASIC_INFORMATION info;

    if (pNtQueryObject( handle, 0 /* ObjectBasicInformation */, &info, sizeof(info), NULL )) return 0;
    return info.GrantedAccess;
}

/* read_end: the handle reads (a CreatePipe read end) rather than writes */
static void test_pipe( const char *name, HANDLE handle, BOOL read_end )
{
    ACCESS_MASK access = granted_access( handle );
    ACCESS_MASK attributes = read_end ? FILE_WRITE_ATTRIBUTES : FILE_READ_ATTRIBUTES;
    DWORD state = 0xdead, mode, type = GetFileType( handle );
    IO_STATUS_BLOCK io;
    PIPE_INFO info = {0xdead, 0xdead};
    NTSTATUS status;
    BOOL ret;

    fprintf( out, "%s: handle %p, GetFileType %lu, access %#lx\n", name, handle, type, access );
    check( type == FILE_TYPE_PIPE, "is a pipe", name, type );
    /* CreatePipe gives the read end FILE_WRITE_ATTRIBUTES and the write end FILE_READ_ATTRIBUTES
     * beside the generic rights, so either end can set and query its mode. */
    check( (access & attributes) == attributes, "has the attribute access a CreatePipe end has", name, access );

    status = pNtQueryInformationFile( handle, &io, &info, sizeof(info), FILE_PIPE_INFORMATION_CLASS );
    check( !status && info.ReadMode == 0 && info.CompletionMode == 0,
           "FilePipeInformation is byte mode, blocking", name, status ? status : info.ReadMode | info.CompletionMode << 4 );

    SetLastError( 0xdeadbeef );
    ret = GetNamedPipeHandleStateA( handle, &state, NULL, NULL, NULL, NULL, 0 );
    check( ret && state == (PIPE_READMODE_BYTE | PIPE_WAIT), "GetNamedPipeHandleState is byte mode, blocking",
           name, ret ? state : GetLastError() );

    mode = PIPE_READMODE_BYTE | PIPE_WAIT;
    SetLastError( 0xdeadbeef );
    ret = SetNamedPipeHandleState( handle, &mode, NULL, NULL );
    check( ret, "SetNamedPipeHandleState(PIPE_READMODE_BYTE | PIPE_WAIT) succeeds", name, ret ? 0 : GetLastError() );

    mode = PIPE_READMODE_MESSAGE | PIPE_WAIT;
    SetLastError( 0xdeadbeef );
    ret = SetNamedPipeHandleState( handle, &mode, NULL, NULL );
    check( !ret && GetLastError() == ERROR_INVALID_PARAMETER,
           "SetNamedPipeHandleState(PIPE_READMODE_MESSAGE) fails with ERROR_INVALID_PARAMETER", name,
           ret ? 0 : GetLastError() );
}

int main( int argc, char **argv )
{
    static const struct { DWORD std; const char *name; BOOL read_end; } handles[] =
    {
        { STD_INPUT_HANDLE,  "stdin",  TRUE },
        { STD_OUTPUT_HANDLE, "stdout", FALSE },
        { STD_ERROR_HANDLE,  "stderr", FALSE },
    };
    HMODULE ntdll = GetModuleHandleA( "ntdll.dll" );
    HANDLE read_pipe, write_pipe;
    int i, tested = 0;

    pNtQueryInformationFile = (void *)GetProcAddress( ntdll, "NtQueryInformationFile" );
    pNtQueryObject = (void *)GetProcAddress( ntdll, "NtQueryObject" );

    /* The report goes to the file named by the argument, or to stdout. */
    out = argc > 1 ? fopen( argv[1], "w" ) : stdout;
    if (!out) return 1;
    setvbuf( out, NULL, _IONBF, 0 );

    /* The reference: a pipe from CreatePipe. */
    if (!CreatePipe( &read_pipe, &write_pipe, NULL, 0 ))
    {
        fprintf( out, "FAIL CreatePipe: %lu\n", GetLastError() );
        return 1;
    }
    test_pipe( "CreatePipe read end", read_pipe, TRUE );
    test_pipe( "CreatePipe write end", write_pipe, FALSE );
    CloseHandle( read_pipe );
    CloseHandle( write_pipe );

    for (i = 0; i < 3; i++)
    {
        HANDLE handle = GetStdHandle( handles[i].std );

        if (GetFileType( handle ) != FILE_TYPE_PIPE)
        {
            fprintf( out, "%s: not a pipe (GetFileType %lu), skipped\n", handles[i].name, GetFileType( handle ) );
            continue;
        }
        test_pipe( handles[i].name, handle, handles[i].read_end );
        tested++;
    }
    fprintf( out, "%d failures, %d standard handles tested\n", failures, tested );
    if (failures) return 1;
    return tested ? 0 : 2;
}
