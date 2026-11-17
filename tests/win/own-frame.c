/* A window that draws its own frame, as many desktop apps do (ribbon toolbars, apps with their
 * own title bar), to see how Wine's Mac driver shows it and how Mac window tools (Rectangle, the
 * green button's tiles) treat it (docs/findings.md #63). Mac-side, read the window and resize it
 * with tests/mac/ax-window.swift, and drag its edge with tests/mac/drag-pid.swift; Windows-side,
 * hold logs every move and size.
 *
 * Usage: own-frame.exe [standard|popup|nccalc|border] [check|hold|hold-maximized] [default-minmax]
 *                      [region|shape] [hit-client] [no-sizing] [layered] [unaware]
 *                      [monitor-minmax] [strip]
 *   standard        WS_OVERLAPPEDWINDOW: Wine gives it a Mac title bar (the control)
 *   popup           no caption, a sizing border, its own title strip (WS_POPUP | WS_THICKFRAME)
 *   nccalc          caption styles, but the app takes the whole window as client area
 *                   (WM_NCCALCSIZE) and draws its own frame, as ribbon libraries do
 *   border          caption styles; the app takes all but the sizing border as client area
 *   check           maximize, compare the window's rect on the Windows side with the work area,
 *                   print PASS or FAIL and exit with 0 or 1 (the default). It counts the sizing
 *                   border that Windows hangs past the work area's edges, so for what the Mac
 *                   shows, use hold and ax-window
 *   hold            stay open and log every move and size: try a window tool on it, or run
 *                   tests/mac/ax-window from a Mac terminal
 *   hold-maximized  the same, starting maximized
 *   default-minmax  leave WM_GETMINMAXINFO to Windows. Without it, the own-frame kinds answer as
 *                   apps that draw their own frame usually do: the work area, relative to the
 *                   monitor. Windows maximizes a captionless popup over the whole monitor, so
 *                   popup with default-minmax is expected to cover the menu bar and the Dock.
 *                   nccalc default-minmax is the frame of Enterprise Architect, for one: its
 *                   sizing border hangs past the work area when it is maximized
 *   region          give the window a region while it isn't maximized: one rectangle a pixel
 *                   larger than the window, set again on every size change, and none while it is
 *                   maximized, as some ribbon libraries do. It changes nothing on screen, but Wine
 *                   counted any region as a shape and wouldn't let macOS resize a shaped window.
 *   shape           a region that does change the outline (an ellipse): the window is shaped, and
 *                   window tools are expected not to resize it
 *   hit-client      answer WM_NCHITTEST with HTCLIENT everywhere, keeping WS_THICKFRAME (for the
 *                   shadow and snapping), as CEF, Qt and launchers do. Windows resizes such a
 *                   window from no edge, so a drag at its edge is expected to resize nothing
 *   no-sizing       popup only: no WS_THICKFRAME, so nothing may resize the window
 *   monitor-minmax  answer WM_GETMINMAXINFO with the whole monitor, as games maximizing for borderless
 *                   fullscreen do: the window is expected to cover the menu bar and the Dock
 *   strip           standard only: take WS_CAPTION and WS_THICKFRAME away after creating the window,
 *                   keeping WS_MAXIMIZEBOX, as games do for borderless fullscreen. Windows maximizes
 *                   it over the whole monitor
 *   layered         a layered window (WS_EX_LAYERED, opaque): Wine draws no frame for it either
 *   unaware         stay DPI unaware: Windows scales the window, and its region, from 96 DPI,
 *                   which in Retina mode is half the Mac window's pixels
 *
 * Build: toolchains/llvm-mingw/bin/x86_64-w64-mingw32-clang -Os -o own-frame.exe tests/win/own-frame.c -luser32 -lgdi32 */
#define _WIN32_WINNT 0x0A00
#define WIN32_LEAN_AND_MEAN
#include <stdio.h>
#include <string.h>
#include <windows.h>
#include <windowsx.h>

enum frame { STANDARD, POPUP, NCCALC, BORDER };

static enum frame frame = POPUP;
static BOOL hold, start_maximized, default_minmax, region, shape, hit_client, no_sizing, layered, unaware, monitor_minmax, strip;
static int step, result = 2;
static char last[512];

static const char *frame_name( void )
{
    return frame == STANDARD ? "standard" : frame == POPUP ? "popup" : frame == NCCALC ? "nccalc" : "border";
}

static int scaled( HWND hwnd, int value )
{
    return MulDiv( value, GetDpiForWindow( hwnd ), 96 );
}

static int caption( HWND hwnd )
{
    return frame == STANDARD ? 0 : scaled( hwnd, 32 );
}

static void monitor_rects( HWND hwnd, RECT *work, RECT *monitor )
{
    MONITORINFO info = {.cbSize = sizeof(info)};

    GetMonitorInfoW( MonitorFromWindow( hwnd, MONITOR_DEFAULTTONEAREST ), &info );
    *work = info.rcWork;
    *monitor = info.rcMonitor;
}

/* What the user sees of the window. The own-frame kinds take the whole window as client area, so
 * all of it. The standard frame loses its sizing border, which Windows hangs off the work area
 * when it maximizes; its caption stays. */
static RECT visible_rect( HWND hwnd )
{
    RECT win, border = {0};
    DWORD style = GetWindowLongW( hwnd, GWL_STYLE ) & ~WS_CAPTION;

    GetWindowRect( hwnd, &win );
    if (frame != STANDARD) return win;
    AdjustWindowRectExForDpi( &border, style, FALSE, 0, GetDpiForWindow( hwnd ) );
    win.left -= border.left;
    win.top -= border.top;
    win.right -= border.right;
    win.bottom -= border.bottom;
    return win;
}

static void describe( HWND hwnd, const char *event )
{
    RECT vis = visible_rect( hwnd ), work, mon;

    monitor_rects( hwnd, &work, &mon );
    snprintf( last, sizeof(last),
              "%s: visible %ld,%ld %ldx%ld; work area %ld,%ld %ldx%ld; monitor %ld,%ld %ldx%ld; %u dpi%s",
              event, vis.left, vis.top, vis.right - vis.left, vis.bottom - vis.top, work.left, work.top,
              work.right - work.left, work.bottom - work.top, mon.left, mon.top, mon.right - mon.left,
              mon.bottom - mon.top, GetDpiForWindow( hwnd ), IsZoomed( hwnd ) ? "; maximized" : "" );
    if (hold)
    {
        printf( "%s\n", last );
        fflush( stdout );
    }
    InvalidateRect( hwnd, NULL, TRUE );
}

static int verdict( HWND hwnd )
{
    RECT vis = visible_rect( hwnd ), work, mon;
    long left, top, right, bottom;

    monitor_rects( hwnd, &work, &mon );
    describe( hwnd, "maximized" );
    printf( "%s %s\n", frame_name(), last );
    if (!IsZoomed( hwnd ))
    {
        printf( "FAIL: the window did not maximize\n" );
        return 1;
    }
    left = vis.left - work.left;
    top = vis.top - work.top;
    right = vis.right - work.right;
    bottom = vis.bottom - work.bottom;
    if (!left && !top && !right && !bottom)
    {
        printf( "PASS: it fills the work area exactly\n" );
        return 0;
    }
    printf( "FAIL: its edges are off the work area's by left %+ld, top %+ld, right %+ld, bottom %+ld px\n",
            left, top, right, bottom );
    return 1;
}

static LRESULT hit_test( HWND hwnd, LPARAM lp )
{
    POINT pt = {GET_X_LPARAM( lp ), GET_Y_LPARAM( lp )};
    RECT win;
    int b = scaled( hwnd, 6 );
    BOOL l, r, t, d;

    GetWindowRect( hwnd, &win );
    if (!IsZoomed( hwnd ))
    {
        l = pt.x < win.left + b;
        r = pt.x >= win.right - b;
        t = pt.y < win.top + b;
        d = pt.y >= win.bottom - b;
        if (t && l) return HTTOPLEFT;
        if (t && r) return HTTOPRIGHT;
        if (d && l) return HTBOTTOMLEFT;
        if (d && r) return HTBOTTOMRIGHT;
        if (l) return HTLEFT;
        if (r) return HTRIGHT;
        if (t) return HTTOP;
        if (d) return HTBOTTOM;
    }
    return pt.y < win.top + caption( hwnd ) ? HTCAPTION : HTCLIENT;
}

static void paint( HWND hwnd )
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint( hwnd, &ps );
    RECT rc, strip, body;
    HBRUSH brush;
    HFONT font, old;
    char title[128];

    GetClientRect( hwnd, &rc );
    strip = body = rc;
    strip.bottom = body.top = caption( hwnd );
    FillRect( dc, &body, GetSysColorBrush( COLOR_WINDOW ) );
    font = CreateFontW( -scaled( hwnd, 13 ), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                        CLEARTYPE_QUALITY, 0, L"Tahoma" );
    old = SelectObject( dc, font );
    SetBkMode( dc, TRANSPARENT );
    if (strip.bottom > strip.top)
    {
        brush = CreateSolidBrush( RGB( 0x3d, 0x6f, 0xa8 ) );
        FillRect( dc, &strip, brush );
        DeleteObject( brush );
        snprintf( title, sizeof(title), "own-frame (%s): drag here; double-click to maximize", frame_name() );
        strip.left += scaled( hwnd, 10 );
        SetTextColor( dc, RGB( 0xff, 0xff, 0xff ) );
        DrawTextA( dc, title, -1, &strip, DT_SINGLELINE | DT_VCENTER );
    }
    InflateRect( &body, -scaled( hwnd, 10 ), -scaled( hwnd, 10 ) );
    SetTextColor( dc, GetSysColor( COLOR_WINDOWTEXT ) );
    DrawTextA( dc, last, -1, &body, DT_WORDBREAK );
    SelectObject( dc, old );
    DeleteObject( font );
    EndPaint( hwnd, &ps );
}

static LRESULT CALLBACK window_proc( HWND hwnd, UINT msg, WPARAM wp, LPARAM lp )
{
    switch (msg)
    {
    case WM_NCCALCSIZE:
        if (frame == BORDER && wp)
        {
            RECT *r = &((NCCALCSIZE_PARAMS *)lp)->rgrc[0], border = {0};

            AdjustWindowRectExForDpi( &border, GetWindowLongW( hwnd, GWL_STYLE ) & ~WS_CAPTION, FALSE, 0,
                                      GetDpiForWindow( hwnd ) );
            r->left -= border.left;
            r->top -= border.top;
            r->right -= border.right;
            r->bottom -= border.bottom;
            return 0; /* all but the sizing border is client area */
        }
        if (frame != STANDARD && wp) return 0; /* the whole window is client area */
        break;
    case WM_NCHITTEST:
        if (frame != STANDARD) return hit_client ? HTCLIENT : hit_test( hwnd, lp );
        break;
    case WM_GETMINMAXINFO:
        if (monitor_minmax)
        {
            MINMAXINFO *info = (MINMAXINFO *)lp;
            RECT work, mon;

            monitor_rects( hwnd, &work, &mon );
            info->ptMaxPosition.x = info->ptMaxPosition.y = 0;
            info->ptMaxSize.x = mon.right - mon.left;
            info->ptMaxSize.y = mon.bottom - mon.top;
            return 0;
        }
        if (frame != STANDARD && frame != BORDER && !default_minmax)
        {
            MINMAXINFO *info = (MINMAXINFO *)lp;
            RECT work, mon;

            monitor_rects( hwnd, &work, &mon );
            info->ptMaxPosition.x = work.left - mon.left;
            info->ptMaxPosition.y = work.top - mon.top;
            info->ptMaxSize.x = work.right - work.left;
            info->ptMaxSize.y = work.bottom - work.top;
            return 0;
        }
        break;
    case WM_WINDOWPOSCHANGED:
        describe( hwnd, "moved or sized" );
        break;
    case WM_SIZE:
        if (region)
        {
            static int width, height;
            RECT rect;

            GetWindowRect( hwnd, &rect );
            if (IsZoomed( hwnd ))
            {
                if (width || height) SetWindowRgn( hwnd, NULL, TRUE );
                width = height = 0;
            }
            else if (rect.right - rect.left != width || rect.bottom - rect.top != height)
            {
                width = rect.right - rect.left;
                height = rect.bottom - rect.top;
                SetWindowRgn( hwnd, CreateRectRgn( 0, 0, width + 1, height + 1 ), TRUE );
            }
        }
        break;
    case WM_PAINT:
        paint( hwnd );
        return 0;
    case WM_TIMER:
        if (step++ == 0) ShowWindow( hwnd, SW_MAXIMIZE );
        else
        {
            KillTimer( hwnd, 1 );
            result = verdict( hwnd );
            if (!hold) DestroyWindow( hwnd );
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage( result );
        return 0;
    }
    return DefWindowProcW( hwnd, msg, wp, lp );
}

int main( int argc, char **argv )
{
    static const WCHAR class_name[] = L"own-frame";
    WNDCLASSW wc = {0};
    DWORD style;
    UINT dpi;
    HWND hwnd;
    MSG msg;
    int i;

    for (i = 1; i < argc; i++)
    {
        if (!strcmp( argv[i], "standard" )) frame = STANDARD;
        else if (!strcmp( argv[i], "popup" )) frame = POPUP;
        else if (!strcmp( argv[i], "nccalc" )) frame = NCCALC;
        else if (!strcmp( argv[i], "border" )) frame = BORDER;
        else if (!strcmp( argv[i], "hold" )) hold = TRUE;
        else if (!strcmp( argv[i], "hold-maximized" )) hold = start_maximized = TRUE;
        else if (!strcmp( argv[i], "check" )) hold = FALSE;
        else if (!strcmp( argv[i], "default-minmax" )) default_minmax = TRUE;
        else if (!strcmp( argv[i], "region" )) region = TRUE;
        else if (!strcmp( argv[i], "shape" )) shape = TRUE;
        else if (!strcmp( argv[i], "hit-client" )) hit_client = TRUE;
        else if (!strcmp( argv[i], "no-sizing" )) no_sizing = TRUE;
        else if (!strcmp( argv[i], "layered" )) layered = TRUE;
        else if (!strcmp( argv[i], "unaware" )) unaware = TRUE;
        else if (!strcmp( argv[i], "monitor-minmax" )) monitor_minmax = TRUE;
        else if (!strcmp( argv[i], "strip" )) strip = TRUE;
        else
        {
            fprintf( stderr, "usage: own-frame.exe [standard|popup|nccalc|border] [check|hold|hold-maximized] "
                             "[default-minmax] [region|shape] [hit-client] [no-sizing] [layered] [unaware] "
                             "[monitor-minmax] [strip]\n" );
            return 2;
        }
    }

    if (!unaware) SetProcessDpiAwarenessContext( DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 );
    dpi = GetDpiForSystem();
    wc.lpfnWndProc = window_proc;
    wc.hInstance = GetModuleHandleW( NULL );
    wc.hCursor = LoadCursorW( NULL, (LPCWSTR)IDC_ARROW );
    wc.lpszClassName = class_name;
    RegisterClassW( &wc );

    style = frame == POPUP ? WS_POPUP | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX
                           : WS_OVERLAPPEDWINDOW;
    if (no_sizing) style &= ~WS_THICKFRAME;
    hwnd = CreateWindowExW( layered ? WS_EX_LAYERED : 0, class_name, frame == STANDARD ? L"own-frame (standard)"
                                         : frame == POPUP    ? L"own-frame (popup)"
                                         : frame == NCCALC   ? L"own-frame (nccalc)"
                                                             : L"own-frame (border)",
                            style, MulDiv( 120, dpi, 96 ), MulDiv( 120, dpi, 96 ), MulDiv( 900, dpi, 96 ),
                            MulDiv( 600, dpi, 96 ), NULL, NULL, wc.hInstance, NULL );
    if (!hwnd)
    {
        fprintf( stderr, "CreateWindowEx failed: %lu\n", GetLastError() );
        return 2;
    }
    if (layered) SetLayeredWindowAttributes( hwnd, 0, 255, LWA_ALPHA );
    if (strip)
    {
        SetWindowLongW( hwnd, GWL_STYLE, GetWindowLongW( hwnd, GWL_STYLE ) & ~(WS_CAPTION | WS_THICKFRAME) );
        SetWindowPos( hwnd, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED );
    }
    if (shape)
    {
        RECT rect;

        GetWindowRect( hwnd, &rect );
        SetWindowRgn( hwnd, CreateEllipticRgn( 0, 0, rect.right - rect.left, rect.bottom - rect.top ), FALSE );
    }
    ShowWindow( hwnd, start_maximized ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL );
    UpdateWindow( hwnd );
    if (hold) printf( "own-frame (%s): close the window to end\n", frame_name() );
    else SetTimer( hwnd, 1, 700, NULL );

    while (GetMessageW( &msg, NULL, 0, 0 ) > 0)
    {
        TranslateMessage( &msg );
        DispatchMessageW( &msg );
    }
    return (int)msg.wParam;
}
