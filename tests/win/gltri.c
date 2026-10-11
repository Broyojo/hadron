/* OpenGL smoke test: spinning triangle in a legacy context, reports FPS.
 * Usage: gltri.exe [seconds]   (default 5) */
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
    WNDCLASSA wc = { CS_OWNDC, wndproc, 0, 0, GetModuleHandleA( NULL ), NULL, LoadCursorA( NULL, (LPCSTR)IDC_ARROW ), NULL, NULL, "gltri" };
    PIXELFORMATDESCRIPTOR pfd = { sizeof(pfd), 1, PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER, PFD_TYPE_RGBA, 32 };
    LARGE_INTEGER freq, start, t;
    unsigned frames = 0;
    int format;
    HGLRC ctx;
    HWND hwnd;
    HDC dc;
    MSG msg;

    setvbuf( stdout, NULL, _IONBF, 0 );
    RegisterClassA( &wc );
    hwnd = CreateWindowA( "gltri", "Hadron OpenGL test", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                          100, 100, 800, 600, NULL, NULL, wc.hInstance, NULL );
    if (!hwnd) { printf( "CreateWindow failed: %lu\n", GetLastError() ); return 1; }
    dc = GetDC( hwnd );
    pfd.cDepthBits = 24;
    if (!(format = ChoosePixelFormat( dc, &pfd )) || !SetPixelFormat( dc, format, &pfd ))
    { printf( "no pixel format: %lu\n", GetLastError() ); return 1; }
    if (!(ctx = wglCreateContext( dc )) || !wglMakeCurrent( dc, ctx ))
    { printf( "no context: %lu\n", GetLastError() ); return 1; }
    printf( "renderer: %s, version %s\n", glGetString( GL_RENDERER ), glGetString( GL_VERSION ) );

    QueryPerformanceFrequency( &freq );
    QueryPerformanceCounter( &start );
    for (;;)
    {
        RECT rc;
        double elapsed;

        while (PeekMessageA( &msg, NULL, 0, 0, PM_REMOVE ))
        {
            if (msg.message == WM_QUIT) goto done;
            DispatchMessageA( &msg );
        }
        QueryPerformanceCounter( &t );
        elapsed = (double)(t.QuadPart - start.QuadPart) / freq.QuadPart;
        if (elapsed >= seconds) break;

        GetClientRect( hwnd, &rc );
        glViewport( 0, 0, rc.right, rc.bottom );
        glClearColor( 0.1f, 0.1f, 0.15f, 1.0f );
        glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
        glLoadIdentity();
        glRotatef( (float)(elapsed * 90.0), 0, 0, 1 );
        glBegin( GL_TRIANGLES );
        glColor3f( 1.0f, 0.2f, 0.2f ); glVertex2f( 0.0f, 0.7f );
        glColor3f( 0.2f, 1.0f, 0.2f ); glVertex2f( 0.6f, -0.5f );
        glColor3f( 0.2f, 0.4f, 1.0f ); glVertex2f( -0.6f, -0.5f );
        glEnd();
        SwapBuffers( dc );
        frames++;
    }
done:
    printf( "rendered %u frames in %.1fs: %.0f fps\n", frames, seconds, frames / seconds );
    return 0;
}
