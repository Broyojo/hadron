/* OpenGL window opacity test: a pixel format with alpha, and a frame whose left half has
 * alpha 0 and whose right half has alpha 1. A window on Windows is opaque either way, so the
 * whole window is green; where the alpha shows through, the left half is black.
 * Usage: glalpha.exe [seconds]   (default 5) */
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include <GL/gl.h>

static LRESULT CALLBACK wndproc( HWND hwnd, UINT msg, WPARAM wp, LPARAM lp )
{
    if (msg == WM_DESTROY) PostQuitMessage( 0 );
    return DefWindowProcA( hwnd, msg, wp, lp );
}

int main( int argc, char **argv )
{
    double seconds = argc > 1 ? atof( argv[1] ) : 5.0;
    WNDCLASSA wc = { CS_OWNDC, wndproc, 0, 0, GetModuleHandleA( NULL ), NULL, LoadCursorA( NULL, (LPCSTR)IDC_ARROW ), NULL, NULL, "glalpha" };
    PIXELFORMATDESCRIPTOR pfd = { sizeof(pfd), 1, PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER, PFD_TYPE_RGBA, 32 };
    LARGE_INTEGER freq, start, t;
    unsigned char left[4], right[4];
    int format;
    HGLRC ctx;
    HWND hwnd;
    HDC dc;
    MSG msg;

    setvbuf( stdout, NULL, _IONBF, 0 );
    RegisterClassA( &wc );
    hwnd = CreateWindowA( "glalpha", "Hadron OpenGL alpha test", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                          100, 100, 800, 600, NULL, NULL, wc.hInstance, NULL );
    if (!hwnd) { printf( "CreateWindow failed: %lu\n", GetLastError() ); return 1; }
    dc = GetDC( hwnd );
    pfd.cAlphaBits = 8;
    if (!(format = ChoosePixelFormat( dc, &pfd )) || !SetPixelFormat( dc, format, &pfd ))
    { printf( "no pixel format: %lu\n", GetLastError() ); return 1; }
    DescribePixelFormat( dc, format, sizeof(pfd), &pfd );
    if (!(ctx = wglCreateContext( dc )) || !wglMakeCurrent( dc, ctx ))
    { printf( "no context: %lu\n", GetLastError() ); return 1; }
    printf( "renderer: %s, format %d with %d alpha bits\n", glGetString( GL_RENDERER ), format, pfd.cAlphaBits );

    QueryPerformanceFrequency( &freq );
    QueryPerformanceCounter( &start );
    for (;;)
    {
        RECT rc;

        while (PeekMessageA( &msg, NULL, 0, 0, PM_REMOVE ))
        {
            if (msg.message == WM_QUIT) return 0;
            DispatchMessageA( &msg );
        }
        QueryPerformanceCounter( &t );
        if ((double)(t.QuadPart - start.QuadPart) / freq.QuadPart >= seconds) break;

        GetClientRect( hwnd, &rc );
        glViewport( 0, 0, rc.right, rc.bottom );
        glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
        glClear( GL_COLOR_BUFFER_BIT );
        glLoadIdentity();
        glBegin( GL_QUADS );
        glColor4f( 0.0f, 1.0f, 0.0f, 0.0f );
        glVertex2f( -1.0f, -1.0f ); glVertex2f( 0.0f, -1.0f ); glVertex2f( 0.0f, 1.0f ); glVertex2f( -1.0f, 1.0f );
        glColor4f( 0.0f, 1.0f, 0.0f, 1.0f );
        glVertex2f( 0.0f, -1.0f ); glVertex2f( 1.0f, -1.0f ); glVertex2f( 1.0f, 1.0f ); glVertex2f( 0.0f, 1.0f );
        glEnd();
        glReadPixels( rc.right / 4, rc.bottom / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, left );
        glReadPixels( rc.right * 3 / 4, rc.bottom / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, right );
        SwapBuffers( dc );
    }
    printf( "drawn: left %u %u %u %u, right %u %u %u %u\n", left[0], left[1], left[2], left[3],
            right[0], right[1], right[2], right[3] );
    return 0;
}
