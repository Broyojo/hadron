/* Set a cursor while the pointer is over the program's window, and see what size the Mac shows it
 * at. The window is 32x32 and nearly transparent, put under the pointer where it is, and closes
 * after a second, so the test needs nobody at the screen. Read the Mac side with
 * tests/mac/nscursor-log.m, which logs every NSCursor the process makes, or with WINEDEBUG=+cursor,
 * which logs how many points per pixel winemac shows a cursor at. (macOS shows only the active
 * app's cursor, and a program started from a script can't make itself active, so the cursor on
 * screen can't be read from outside.) Windows shows every cursor at its size in pixels; in Retina mode winemac shows it at
 * 96 / DPI points per pixel, the DPI of the window it is set for (#98, #67).
 *
 * Usage: cursor-size.exe [bitmap|wait|wait-unaware|file|ani|cache]
 *   bitmap        (the default) a game's own cursor: 32x32 pixels from CreateIconIndirect, hot spot
 *                 5,9, in a program that isn't DPI aware
 *   wait          the system's hourglass (IDC_WAIT) in a per-monitor DPI-aware program: Windows
 *                 loads it in the size for the DPI, 64x64 pixels at 192 DPI
 *   wait-unaware  the same in a program that isn't DPI aware: 32x32 pixels
 *   file          a cursor file with a 32x32 and a 64x64 image (LoadCursorFromFile), per-monitor
 *                 aware: Windows picks the one for the DPI
 *   ani           an animated cursor file of two frames, each with a 32x32 and a 64x64 image
 *   cache         the hourglass of a per-monitor aware program, set first over a window that
 *                 isn't DPI aware, then over one that is: one HCURSOR over windows of two DPIs
 *
 * Build: toolchains/llvm-mingw/bin/x86_64-w64-mingw32-clang -Os -o cursor-size.exe tests/win/cursor-size.c -luser32 -lgdi32 */
#define _WIN32_WINNT 0x0A00
#include <stdio.h>
#include <string.h>
#include <windows.h>

static HCURSOR cursor;

static LRESULT CALLBACK wndproc( HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam )
{
    if (msg == WM_SETCURSOR) { SetCursor( cursor ); return TRUE; }
    if (msg == WM_TIMER) { DestroyWindow( hwnd ); return 0; }
    if (msg == WM_DESTROY) { PostQuitMessage( 0 ); return 0; }
    return DefWindowProcA( hwnd, msg, wparam, lparam );
}

static void describe( const char *what, HCURSOR c )
{
    ICONINFO info;
    BITMAP bm;

    if (!c || !GetIconInfo( c, &info ))
    {
        printf( "%s: no cursor (error %lu)\n", what, GetLastError() );
        return;
    }
    GetObjectA( info.hbmColor ? info.hbmColor : info.hbmMask, sizeof(bm), &bm );
    printf( "%s: %ldx%ld pixels, hot spot %lu,%lu, %u DPI\n", what, bm.bmWidth,
            info.hbmColor ? bm.bmHeight : bm.bmHeight / 2, info.xHotspot, info.yHotspot, GetDpiForSystem() );
    fflush( stdout );
}

/* One cursor image as a cursor file holds it: a header, 32-bit pixels, a 1-bit mask. */
static DWORD image_size( int size )
{
    return sizeof(BITMAPINFOHEADER) + size * size * 4 + size * ((size + 31) / 32 * 4);
}

static BYTE *put_image( BYTE *p, int size, DWORD colour )
{
    BITMAPINFOHEADER bih = {.biSize = sizeof(bih), .biWidth = size, .biHeight = size * 2, .biPlanes = 1,
                            .biBitCount = 32};
    int i;

    memcpy( p, &bih, sizeof(bih) );
    p += sizeof(bih);
    for (i = 0; i < size * size; i++, p += 4)
    {
        DWORD pixel = (i % size == i / size || i % size < 2) ? colour : 0;
        memcpy( p, &pixel, 4 );
    }
    memset( p, 0, size * ((size + 31) / 32 * 4) );
    return p + size * ((size + 31) / 32 * 4);
}

/* A cursor file with a 32x32 and a 64x64 image, hot spots 5,9 and 10,18. */
static DWORD put_cursor_file( BYTE *p, DWORD colour )
{
    static const int sizes[] = {32, 64};
    BYTE *start = p, *image;
    DWORD offset = 6 + 16 * 2;
    int i;

    *(WORD *)p = 0; *(WORD *)(p + 2) = 2; *(WORD *)(p + 4) = 2;
    p += 6;
    for (i = 0; i < 2; i++, p += 16)
    {
        p[0] = sizes[i]; p[1] = sizes[i]; p[2] = 0; p[3] = 0;
        *(WORD *)(p + 4) = 5 * sizes[i] / 32;
        *(WORD *)(p + 6) = 9 * sizes[i] / 32;
        *(DWORD *)(p + 8) = image_size( sizes[i] );
        *(DWORD *)(p + 12) = offset;
        offset += image_size( sizes[i] );
    }
    image = p;
    for (i = 0; i < 2; i++) image = put_image( image, sizes[i], colour );
    return image - start;
}

static HCURSOR load_file( const char *name, BOOL animated )
{
    static BYTE data[65536];
    BYTE *p = data;
    DWORD written;
    HANDLE file;
    int i;

    if (!animated) p += put_cursor_file( p, 0xffff0000 );
    else
    {
        BYTE *list, *riff_size;

        memcpy( p, "RIFF", 4 ); riff_size = p + 4; memcpy( p + 8, "ACON", 4 ); p += 12;
        memcpy( p, "anih", 4 ); *(DWORD *)(p + 4) = 36; p += 8;
        memset( p, 0, 36 );
        ((DWORD *)p)[0] = 36;  /* cbSize */
        ((DWORD *)p)[1] = 2;   /* frames */
        ((DWORD *)p)[2] = 2;   /* steps */
        ((DWORD *)p)[7] = 30;  /* jiffies per frame */
        ((DWORD *)p)[8] = 1;   /* AF_ICON: each frame is a cursor file */
        p += 36;
        memcpy( p, "LIST", 4 ); list = p + 4; memcpy( p + 8, "fram", 4 ); p += 12;
        for (i = 0; i < 2; i++)
        {
            DWORD size;

            memcpy( p, "icon", 4 );
            size = put_cursor_file( p + 8, i ? 0xff0000ff : 0xffff0000 );
            *(DWORD *)(p + 4) = size;
            p += 8 + size + (size & 1);
        }
        *(DWORD *)list = p - list - 4;
        *(DWORD *)riff_size = p - data - 8;
    }
    file = CreateFileA( name, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL );
    if (file == INVALID_HANDLE_VALUE) return NULL;
    WriteFile( file, data, p - data, &written, NULL );
    CloseHandle( file );
    return LoadCursorFromFileA( name );
}

/* Show a window under the pointer for a second, its cursor the one set, on a thread of the given
 * DPI awareness. */
static void show_window( DPI_AWARENESS_CONTEXT awareness, const char *what )
{
    DPI_AWARENESS_CONTEXT old = SetThreadDpiAwarenessContext( awareness );
    HWND hwnd;
    POINT pt;
    MSG msg;

    /* Wine finds the window under the pointer from the position it last heard of: tell it the
     * current one, which leaves the pointer where it is. */
    GetCursorPos( &pt );
    SetCursorPos( pt.x, pt.y );
    hwnd = CreateWindowExA( WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_LAYERED | WS_EX_TOOLWINDOW, "cursor-size", "",
                            WS_POPUP, pt.x - 16, pt.y - 16, 32, 32, NULL, NULL, GetModuleHandleA( NULL ), NULL );
    SetLayeredWindowAttributes( hwnd, 0, 1, LWA_ALPHA );
    printf( "over a window of %u DPI%s%s\n", GetDpiForWindow( hwnd ), what ? ", " : "", what ? what : "" );
    fflush( stdout );
    ShowWindow( hwnd, SW_SHOWNOACTIVATE );
    SetTimer( hwnd, 1, 1000, NULL );
    SetCursorPos( pt.x, pt.y );
    while (GetMessageA( &msg, NULL, 0, 0 )) DispatchMessageA( &msg );
    SetThreadDpiAwarenessContext( old );
}

int main( int argc, char **argv )
{
    BITMAPV5HEADER bi = {.bV5Size = sizeof(bi), .bV5Width = 32, .bV5Height = -32, .bV5Planes = 1,
                         .bV5BitCount = 32, .bV5Compression = BI_BITFIELDS, .bV5RedMask = 0xff0000,
                         .bV5GreenMask = 0xff00, .bV5BlueMask = 0xff, .bV5AlphaMask = 0xff000000};
    WNDCLASSA wc = {.lpfnWndProc = wndproc, .hInstance = GetModuleHandleA( NULL ), .lpszClassName = "cursor-size"};
    const char *test = argc > 1 ? argv[1] : "bitmap";
    DPI_AWARENESS_CONTEXT aware = DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2;

    RegisterClassA( &wc );
    if (!strcmp( test, "bitmap" ))
    {
        ICONINFO info = {.fIcon = FALSE, .xHotspot = 5, .yHotspot = 9};
        DWORD *bits;
        int i;

        info.hbmColor = CreateDIBSection( NULL, (BITMAPINFO *)&bi, DIB_RGB_COLORS, (void **)&bits, NULL, 0 );
        for (i = 0; i < 32 * 32; i++) bits[i] = (i % 32 == i / 32 || i % 32 < 2) ? 0xffff0000 : 0;
        info.hbmMask = CreateBitmap( 32, 32, 1, 1, NULL );
        cursor = CreateIconIndirect( &info );
        describe( "bitmap", cursor );
        show_window( DPI_AWARENESS_CONTEXT_UNAWARE, NULL );
    }
    else if (!strcmp( test, "wait" ) || !strcmp( test, "wait-unaware" ))
    {
        if (!strcmp( test, "wait-unaware" )) aware = DPI_AWARENESS_CONTEXT_UNAWARE;
        SetProcessDpiAwarenessContext( aware );
        cursor = LoadCursorA( NULL, (const char *)IDC_WAIT );
        describe( "hourglass", cursor );
        show_window( aware, NULL );
    }
    else if (!strcmp( test, "file" ) || !strcmp( test, "ani" ))
    {
        SetProcessDpiAwarenessContext( aware );
        cursor = load_file( !strcmp( test, "ani" ) ? "cursor-size.ani" : "cursor-size.cur", !strcmp( test, "ani" ) );
        describe( test, cursor );
        show_window( aware, NULL );
    }
    else if (!strcmp( test, "cache" ))
    {
        SetProcessDpiAwarenessContext( aware );
        cursor = LoadCursorA( NULL, (const char *)IDC_WAIT );
        describe( "hourglass", cursor );
        show_window( DPI_AWARENESS_CONTEXT_UNAWARE, "first" );
        show_window( aware, "then" );
    }
    else
    {
        fprintf( stderr, "usage: cursor-size.exe [bitmap|wait|wait-unaware|file|ani|cache]\n" );
        return 2;
    }
    printf( "done\n" );
    return 0;
}
