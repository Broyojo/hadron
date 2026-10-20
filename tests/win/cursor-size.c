/* Set a game's own cursor, as a game does: a 32x32 colour cursor with its hot spot at (5,9), set
 * while the pointer is over the program's window. The window is 32x32 and nearly transparent, put
 * under the pointer where it is, and closes after a second, so the test needs nobody at the screen.
 * What the Mac shows is read on the Mac side: tests/mac/nscursor-log.m logs every NSCursor the
 * process makes. A cursor should cover as many points as it has pixels, in Retina mode too (#98).
 * Usage: cursor-size.exe */
#include <stdio.h>
#include <windows.h>

static HCURSOR cursor;

static LRESULT CALLBACK wndproc( HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam )
{
    if (msg == WM_SETCURSOR) { SetCursor( cursor ); return TRUE; }
    if (msg == WM_TIMER) { DestroyWindow( hwnd ); return 0; }
    if (msg == WM_DESTROY) { PostQuitMessage( 0 ); return 0; }
    return DefWindowProcA( hwnd, msg, wparam, lparam );
}

int main( void )
{
    BITMAPV5HEADER bi = {.bV5Size = sizeof(bi), .bV5Width = 32, .bV5Height = -32, .bV5Planes = 1,
                         .bV5BitCount = 32, .bV5Compression = BI_BITFIELDS, .bV5RedMask = 0xff0000,
                         .bV5GreenMask = 0xff00, .bV5BlueMask = 0xff, .bV5AlphaMask = 0xff000000};
    WNDCLASSA wc = {.lpfnWndProc = wndproc, .hInstance = GetModuleHandleA( NULL ), .lpszClassName = "cursor-size"};
    ICONINFO info = {.fIcon = FALSE, .xHotspot = 5, .yHotspot = 9};
    DWORD *bits;
    HWND hwnd;
    POINT pt;
    MSG msg;
    int i;

    info.hbmColor = CreateDIBSection( NULL, (BITMAPINFO *)&bi, DIB_RGB_COLORS, (void **)&bits, NULL, 0 );
    for (i = 0; i < 32 * 32; i++) bits[i] = (i % 32 == i / 32 || i % 32 < 2) ? 0xffff0000 : 0;
    info.hbmMask = CreateBitmap( 32, 32, 1, 1, NULL );
    cursor = CreateIconIndirect( &info );
    printf( "cursor %p, SM_CXCURSOR %d\n", cursor, GetSystemMetrics( SM_CXCURSOR ) );

    /* Wine finds the window under the pointer from the position it last heard of: tell it the
     * current one, which leaves the pointer where it is. */
    GetCursorPos( &pt );
    SetCursorPos( pt.x, pt.y );
    RegisterClassA( &wc );
    hwnd = CreateWindowExA( WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_LAYERED | WS_EX_TOOLWINDOW, "cursor-size", "",
                            WS_POPUP, pt.x - 16, pt.y - 16, 32, 32, NULL, NULL, wc.hInstance, NULL );
    SetLayeredWindowAttributes( hwnd, 0, 1, LWA_ALPHA );
    ShowWindow( hwnd, SW_SHOWNOACTIVATE );
    SetTimer( hwnd, 1, 1000, NULL );
    SetCursorPos( pt.x, pt.y );
    while (GetMessageA( &msg, NULL, 0, 0 )) DispatchMessageA( &msg );
    printf( "done\n" );
    return 0;
}
