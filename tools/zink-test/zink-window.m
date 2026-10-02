/* Zink on KosmicKrisp in a window: an EGL window surface on a CAMetalLayer, which Mesa's
 * surfaceless platform accepts on macOS (Mesa 0035). Opens a window, draws a triangle on a
 * background that changes colour for a few seconds, and reports what was read back from the
 * back buffer before each swap. usage: zink-window [seconds] */
#import <Cocoa/Cocoa.h>
#import <QuartzCore/CAMetalLayer.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <stdio.h>
#include <string.h>
#include "glfuncs.h"

static GLuint compile(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    return s;
}

int main(int argc, char **argv)
{
    @autoreleasepool {
        double seconds = argc > 1 ? atof(argv[1]) : 4;
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        NSWindow *window = [[NSWindow alloc] initWithContentRect:NSMakeRect(200, 200, 480, 360)
                                                       styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskResizable
                                                         backing:NSBackingStoreBuffered defer:NO];
        window.title = @"Zink on KosmicKrisp";
        NSView *view = window.contentView;
        CAMetalLayer *layer = [CAMetalLayer layer];
        layer.contentsScale = window.backingScaleFactor;
        view.wantsLayer = YES;
        view.layer = layer;
        layer.frame = view.bounds;
        [window makeKeyAndOrderFront:nil];
        [NSApp activateIgnoringOtherApps:YES];

        EGLDisplay dpy = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, NULL, NULL);
        EGLint major, minor, count;
        EGLConfig config;
        load_gl_funcs();
        if (!eglInitialize(dpy, &major, &minor)) { printf("eglInitialize failed: 0x%x\n", eglGetError()); return 1; }
        eglBindAPI(EGL_OPENGL_API);
        eglChooseConfig(dpy, (EGLint[]){EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
                                        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_DEPTH_SIZE, 24, EGL_NONE},
                        &config, 1, &count);
        if (!count) { printf("no window config\n"); return 1; }
        EGLSurface surface = eglCreateWindowSurface(dpy, config, (__bridge EGLNativeWindowType)layer, NULL);
        if (surface == EGL_NO_SURFACE) { printf("eglCreateWindowSurface failed: 0x%x\n", eglGetError()); return 1; }
        EGLContext ctx = eglCreateContext(dpy, config, EGL_NO_CONTEXT, (EGLint[]){
            EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 3,
            EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT, EGL_NONE});
        if (!eglMakeCurrent(dpy, surface, surface, ctx)) { printf("eglMakeCurrent failed: 0x%x\n", eglGetError()); return 1; }

        EGLint w = 0, h = 0;
        eglQuerySurface(dpy, surface, EGL_WIDTH, &w);
        eglQuerySurface(dpy, surface, EGL_HEIGHT, &h);
        printf("%s, surface %dx%d\n", glGetString(GL_VERSION), w, h);

        GLuint prog = glCreateProgram(), vao, vbo;
        glAttachShader(prog, compile(GL_VERTEX_SHADER, "#version 150\nin vec2 p; void main() { gl_Position = vec4(p, 0.0, 1.0); }"));
        glAttachShader(prog, compile(GL_FRAGMENT_SHADER, "#version 150\nout vec4 c; void main() { c = vec4(1.0, 0.5, 0.25, 1.0); }"));
        glLinkProgram(prog);
        glUseProgram(prog);
        static const float tri[] = {-0.8f, -0.8f, 0.8f, -0.8f, 0, 0.8f};
        glGenVertexArrays(1, &vao);
        glBindVertexArray(vao);
        glGenBuffers(1, &vbo);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(tri), tri, GL_STATIC_DRAW);
        glVertexAttribPointer(glGetAttribLocation(prog, "p"), 2, GL_FLOAT, GL_FALSE, 0, 0);
        glEnableVertexAttribArray(glGetAttribLocation(prog, "p"));

        NSDate *end = [NSDate dateWithTimeIntervalSinceNow:seconds];
        unsigned frames = 0, good = 0, failed_swaps = 0;
        while ([end timeIntervalSinceNow] > 0) {
            NSEvent *event;
            while ((event = [NSApp nextEventMatchingMask:NSEventMaskAny untilDate:nil inMode:NSDefaultRunLoopMode dequeue:YES]))
                [NSApp sendEvent:event];
            eglQuerySurface(dpy, surface, EGL_WIDTH, &w);
            eglQuerySurface(dpy, surface, EGL_HEIGHT, &h);
            float t = (frames % 120) / 120.0f;
            glViewport(0, 0, w, h);
            glClearColor(0, t, 1 - t, 1);
            glClear(GL_COLOR_BUFFER_BIT);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            unsigned char px[4] = {0};
            glReadPixels(w / 2, h / 3, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
            if (px[0] == 255 && px[1] == 128 && px[2] == 64) good++;
            if (!eglSwapBuffers(dpy, surface)) failed_swaps++;
            frames++;
        }
        printf("%u frames in %.0f s (%.0f per second), triangle pixel right in %u, %u swaps failed, GL error 0x%x, final surface %dx%d\n",
               frames, seconds, frames / seconds, good, failed_swaps, glGetError(), w, h);
    }
    return 0;
}
