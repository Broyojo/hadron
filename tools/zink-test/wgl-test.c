/* OpenGL through Wine: a window, a WGL context, a triangle on a background that changes colour,
 * for a few seconds. Prints the renderer and what was read back from the back buffer.
 *
 *   x86_64-w64-mingw32-clang -O1 -o wgl-test.exe wgl-test.c -lopengl32 -lgdi32 -luser32
 *   HADRON_OPENGL=zink scripts/play <appid> wgl-test.exe [seconds]
 *
 * With HADRON_OPENGL=zink the renderer is "zink Vulkan ... (MESA_KOSMICKRISP)"; without it,
 * Apple's. */
#include <windows.h>
#include <GL/gl.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    double seconds = argc > 1 ? atof(argv[1]) : 4;
    WNDCLASSA wc = { .lpfnWndProc = DefWindowProcA, .hInstance = GetModuleHandleA(NULL), .lpszClassName = "wgltest",
                     .style = CS_OWNDC };
    RegisterClassA(&wc);
    HWND hwnd = CreateWindowA("wgltest", "OpenGL through Wine", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 120, 120, 480, 360,
                              0, 0, wc.hInstance, 0);
    HDC dc = GetDC(hwnd);
    PIXELFORMATDESCRIPTOR pfd = { .nSize = sizeof(pfd), .nVersion = 1,
        .dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER, .iPixelType = PFD_TYPE_RGBA,
        .cColorBits = 32, .cDepthBits = 24 };
    int format = ChoosePixelFormat(dc, &pfd);
    if (!format || !SetPixelFormat(dc, format, &pfd)) { printf("no pixel format (error %lu)\n", GetLastError()); return 1; }
    HGLRC rc = wglCreateContext(dc);
    if (!rc || !wglMakeCurrent(dc, rc)) { printf("no context (error %lu)\n", GetLastError()); return 1; }
    printf("vendor   %s\nrenderer %s\nversion  %s\n", glGetString(GL_VENDOR), glGetString(GL_RENDERER), glGetString(GL_VERSION));
    fflush(stdout);

    DWORD end = GetTickCount() + (DWORD)(seconds * 1000);
    unsigned frames = 0, good = 0, failed = 0;
    RECT rect;
    while (GetTickCount() < end)
    {
        MSG msg;
        while (PeekMessageA(&msg, 0, 0, 0, PM_REMOVE)) DispatchMessageA(&msg);
        GetClientRect(hwnd, &rect);
        float t = (frames % 120) / 120.0f;
        glViewport(0, 0, rect.right, rect.bottom);
        glClearColor(0, t, 1 - t, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glColor3f(1.0f, 0.5f, 0.25f);
        glBegin(GL_TRIANGLES);
        glVertex2f(-0.8f, -0.8f); glVertex2f(0.8f, -0.8f); glVertex2f(0.0f, 0.8f);
        glEnd();
        unsigned char px[4] = {0};
        glReadPixels(rect.right / 2, rect.bottom / 3, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        if (px[0] == 255 && px[1] >= 127 && px[1] <= 128 && px[2] >= 63 && px[2] <= 64) good++;
        if (!SwapBuffers(dc)) failed++;
        frames++;
    }
    printf("%u frames in %.0f s (%.0f per second), triangle pixel right in %u, %u swaps failed, GL error 0x%x, client %ldx%ld\n",
           frames, seconds, frames / seconds, good, failed, glGetError(), rect.right, rect.bottom);
    fflush(stdout);
    return 0;
}
