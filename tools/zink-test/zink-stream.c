/* Vertex data that changes every frame, the way a sprite batch does it: each frame a new set of
 * quads (interleaved position, colour bytes, texture coordinates; 16-bit indices) replaces the
 * vertex buffer's contents with glBufferData and is drawn once. Every frame is read back and
 * each quad's centre and a background pixel are checked. Offscreen, on the surfaceless platform.
 * With "client" as the fourth argument the quads are drawn the way single sprites are: one draw
 * call each, from vertex arrays in the program's own memory instead of a buffer object.
 * With "subdata" the quads go through one small buffer in batches of 16: glBufferSubData over the
 * same range, then a draw, for each batch, as a texture atlas does it without orphaning.
 * With "streak" each frame also draws a ribbon between two halves of the quads, the way a motion
 * trail is drawn: a triangle strip whose length changes every frame, from three separate arrays
 * in the program's memory (two-float positions, two-float texture coordinates, colour bytes).
 * usage: zink-stream [frames] [quads per frame] [flush: none|flush|finish] [buffer|client|subdata|streak] */
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "glfuncs.h"

#define W 512
#define H 512

struct vertex { float x, y, z; unsigned char r, g, b, a; float u, v; };

static GLuint compile(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    return s;
}

int main(int argc, char **argv)
{
    unsigned frames = argc > 1 ? atoi(argv[1]) : 600, quads = argc > 2 ? atoi(argv[2]) : 64;
    const char *flush = argc > 3 ? argv[3] : "none";
    int client = argc > 4 && !strcmp(argv[4], "client");
    int streak = argc > 4 && !strcmp(argv[4], "streak");
    int subdata = streak || (argc > 4 && !strcmp(argv[4], "subdata"));
    EGLDisplay dpy = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, NULL, NULL);
    EGLint major, minor, count;
    EGLConfig config;

    load_gl_funcs();
    if (!eglInitialize(dpy, &major, &minor)) { printf("eglInitialize failed\n"); return 1; }
    eglBindAPI(EGL_OPENGL_API);
    eglChooseConfig(dpy, (EGLint[]){EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_NONE}, &config, 1, &count);
    EGLContext ctx = eglCreateContext(dpy, config, EGL_NO_CONTEXT, (EGLint[]){EGL_NONE});
    if (!eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)) { printf("eglMakeCurrent failed\n"); return 1; }

    GLuint fbo, tex, vbo, ibo, prog;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);

    prog = glCreateProgram();
    glAttachShader(prog, compile(GL_VERTEX_SHADER,
        "#version 120\nattribute vec4 a_position; attribute vec4 a_color; attribute vec2 a_uv;\n"
        "varying vec4 v_color; void main() { gl_Position = a_position; v_color = a_color + vec4(a_uv, 0.0, 0.0) * 0.0; }"));
    glAttachShader(prog, compile(GL_FRAGMENT_SHADER,
        "#version 120\nvarying vec4 v_color; void main() { gl_FragColor = v_color; }"));
    glLinkProgram(prog);
    glUseProgram(prog);

    struct vertex *verts = malloc(quads * 4 * sizeof(*verts));
    unsigned short *indices = malloc(quads * 6 * sizeof(*indices));
    unsigned char *pixels = malloc(W * H * 4);
    for (unsigned q = 0; q < quads; q++)
    {
        unsigned short *i = indices + q * 6;
        i[0] = q * 4; i[1] = q * 4 + 1; i[2] = q * 4 + 2; i[3] = q * 4 + 2; i[4] = q * 4 + 1; i[5] = q * 4 + 3;
    }
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glGenBuffers(1, &ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, quads * 6 * sizeof(*indices), indices, GL_STATIC_DRAW);
    GLint pos = glGetAttribLocation(prog, "a_position"), col = glGetAttribLocation(prog, "a_color"), uv = glGetAttribLocation(prog, "a_uv");
    glEnableVertexAttribArray(pos);
    glVertexAttribPointer(pos, 3, GL_FLOAT, GL_FALSE, sizeof(struct vertex), (void *)0);
    glEnableVertexAttribArray(col);
    glVertexAttribPointer(col, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(struct vertex), (void *)12);
    if (uv >= 0) { glEnableVertexAttribArray(uv); glVertexAttribPointer(uv, 2, GL_FLOAT, GL_FALSE, sizeof(struct vertex), (void *)16); }
    glViewport(0, 0, W, H);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);

    /* quads sit on a grid of cells; which cell a quad takes moves every frame */
    unsigned grid = 1;
    while (grid * grid < quads * 2) grid++;
    unsigned cell = W / grid, bad_frames = 0, first_bad = 0, bad_quads = 0, bad_background = 0, bad_ribbon = 0;
    printf("%s, %u frames of %u quads, flush: %s, %s\n", glGetString(GL_RENDERER), frames, quads, flush,
           client ? "client arrays, one draw per quad" : streak ? "glBufferSubData batches around a client-array ribbon" :
           subdata ? "glBufferSubData over one range, a draw per 16 quads" : "buffer object, one draw");
    if (subdata) glBufferData(GL_ARRAY_BUFFER, 16 * 4 * sizeof(*verts), NULL, GL_DYNAMIC_DRAW);
    if (client)
    {
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glVertexAttribPointer(pos, 3, GL_FLOAT, GL_FALSE, sizeof(struct vertex), &verts->x);
        glVertexAttribPointer(col, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(struct vertex), &verts->r);
        if (uv >= 0) glVertexAttribPointer(uv, 2, GL_FLOAT, GL_FALSE, sizeof(struct vertex), &verts->u);
    }

    for (unsigned f = 0; f < frames; f++)
    {
        for (unsigned q = 0; q < quads; q++)
        {
            unsigned slot = (q * 2 + f) % (grid * grid), cx = slot % grid, cy = slot / grid;
            float x0 = -1.0f + 2.0f * (cx * cell + 2) / W, x1 = -1.0f + 2.0f * ((cx + 1) * cell - 2) / W;
            float y0 = -1.0f + 1.6f * (cy * cell + 2) / H, y1 = -1.0f + 1.6f * ((cy + 1) * cell - 2) / H;
            unsigned char r = 64 + (q * 37 + f) % 192, g = 64 + (q * 91) % 192, b = 255;
            struct vertex *v = verts + q * 4;
            v[0] = (struct vertex){x0, y0, 0, r, g, b, 255, 0, 0};
            v[1] = (struct vertex){x1, y0, 0, r, g, b, 255, 1, 0};
            v[2] = (struct vertex){x0, y1, 0, r, g, b, 255, 0, 1};
            v[3] = (struct vertex){x1, y1, 0, r, g, b, 255, 1, 1};
        }
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        if (client)
        {
            for (unsigned q = 0; q < quads; q++) glDrawArrays(GL_TRIANGLE_STRIP, q * 4, 4);
        }
        else if (subdata)
        {
            for (unsigned q = 0; q < quads; q += 16)
            {
                unsigned n = quads - q < 16 ? quads - q : 16;
                glBufferSubData(GL_ARRAY_BUFFER, 0, n * 4 * sizeof(*verts), verts + q * 4);
                glDrawElements(GL_TRIANGLES, n * 6, GL_UNSIGNED_SHORT, 0);
                if (streak && q <= quads / 2 && q + 16 > quads / 2)
                {
                    /* the ribbon: x from -0.9 to 0.9, y from 0.80 to 0.95, in a number of segments that changes */
                    static float ribbon_pos[2 * 2 * 130], ribbon_uv[2 * 2 * 130];
                    static unsigned char ribbon_col[4 * 2 * 130];
                    unsigned segments = 1 + (f * 7) % 128;
                    for (unsigned i = 0; i <= segments; i++)
                    {
                        float x = -0.9f + 1.8f * i / segments;
                        ribbon_pos[i * 4] = x; ribbon_pos[i * 4 + 1] = 0.80f; ribbon_pos[i * 4 + 2] = x; ribbon_pos[i * 4 + 3] = 0.95f;
                        ribbon_uv[i * 4] = ribbon_uv[i * 4 + 2] = (float)i / segments; ribbon_uv[i * 4 + 1] = 0; ribbon_uv[i * 4 + 3] = 1;
                        for (unsigned v = 0; v < 2; v++)
                        {
                            unsigned char *c = ribbon_col + (i * 2 + v) * 4;
                            c[0] = 255; c[1] = 200; c[2] = 0; c[3] = 255;
                        }
                    }
                    glBindBuffer(GL_ARRAY_BUFFER, 0);
                    glVertexAttribPointer(pos, 2, GL_FLOAT, GL_FALSE, 0, ribbon_pos);
                    glVertexAttribPointer(col, 4, GL_UNSIGNED_BYTE, GL_TRUE, 0, ribbon_col);
                    if (uv >= 0) glVertexAttribPointer(uv, 2, GL_FLOAT, GL_FALSE, 0, ribbon_uv);
                    glDrawArrays(GL_TRIANGLE_STRIP, 0, (segments + 1) * 2);
                    glBindBuffer(GL_ARRAY_BUFFER, vbo);
                    glVertexAttribPointer(pos, 3, GL_FLOAT, GL_FALSE, sizeof(struct vertex), (void *)0);
                    glVertexAttribPointer(col, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(struct vertex), (void *)12);
                    if (uv >= 0) glVertexAttribPointer(uv, 2, GL_FLOAT, GL_FALSE, sizeof(struct vertex), (void *)16);
                }
            }
        }
        else
        {
            glBufferData(GL_ARRAY_BUFFER, quads * 4 * sizeof(*verts), verts, GL_DYNAMIC_DRAW);
            glDrawElements(GL_TRIANGLES, quads * 6, GL_UNSIGNED_SHORT, 0);
        }
        if (!strcmp(flush, "flush")) glFlush();
        if (!strcmp(flush, "finish")) glFinish();
        if (!strcmp(flush, "none") && f % 8 != 7) continue;   /* read back only some frames, so frames queue up */
        glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

        unsigned wrong = 0, wrong_bg = 0, wrong_ribbon = 0;
        for (unsigned q = 0; q < quads; q++)
        {
            unsigned slot = (q * 2 + f) % (grid * grid), cx = slot % grid, cy = slot / grid;
            const unsigned char *p = pixels + ((unsigned)((cy * cell + cell / 2) * 0.8f) * W + cx * cell + cell / 2) * 4;
            unsigned char r = 64 + (q * 37 + f) % 192, g = 64 + (q * 91) % 192;
            if (abs(p[0] - r) > 1 || abs(p[1] - g) > 1 || p[2] != 255) wrong++;
            /* the cell after it holds no quad */
            unsigned empty = (slot + 1) % (grid * grid), ex = empty % grid, ey = empty / grid;
            p = pixels + ((unsigned)((ey * cell + cell / 2) * 0.8f) * W + ex * cell + cell / 2) * 4;
            if (p[0] || p[1] || p[2]) wrong_bg++;
        }
        if (streak)
        {
            /* inside the ribbon at five places, and above and below it */
            for (unsigned i = 0; i < 5; i++)
            {
                unsigned x = W / 2 + (int)(0.8f * W / 2 * ((int)i - 2) / 2.0f);
                const unsigned char *in = pixels + ((unsigned)((0.875f + 1) * H / 2) * W + x) * 4;
                const unsigned char *above = pixels + ((unsigned)((0.975f + 1) * H / 2) * W + x) * 4;
                const unsigned char *below = pixels + ((unsigned)((0.70f + 1) * H / 2) * W + x) * 4;
                if (in[0] != 255 || abs(in[1] - 200) > 1 || in[2] != 0)
                {
                    wrong_ribbon++;
                    if (getenv("VERBOSE") && bad_frames < 6)
                        printf("  frame %u (%u segments): ribbon sample %u at x=%u is %u %u %u %u\n", f, 1 + (f * 7) % 128, i, x, in[0], in[1], in[2], in[3]);
                }
                if (above[0] || above[1] || above[2] || below[0] || below[1] || below[2]) wrong_bg++;
            }
            wrong += wrong_ribbon;
        }
        if (wrong || wrong_bg) { if (!bad_frames) first_bad = f; bad_frames++; bad_quads += wrong; bad_background += wrong_bg; bad_ribbon += wrong_ribbon; }
    }
    printf("%u bad frames (first at %u), %u wrong quads or ribbon samples (%u ribbon), %u places that should be empty and are not, GL error 0x%x\n",
           bad_frames, first_bad, bad_quads, bad_ribbon, bad_background, glGetError());
    return bad_frames != 0;
}
