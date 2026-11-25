/* Tessellation drawn with glDrawArraysIndirect against the same draw made with glDrawArrays.
 * The vertex and control shaders hold N floats live (the register pressure of
 * tools/metal-probes/indirect-threads.m); the evaluation shader passes a per-patch value to the
 * fragment shader flat. Prints how many of the patches differ between the two draws.
 * usage: gl-tess-indirect [N live floats, default 64] [patches], or tests/mac/gl-tess-indirect.sh */
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/glcorearb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define F(ret, name, args) static ret (*name) args;
#define FUNCS(X) \
    X(GLuint, glCreateShader, (GLenum)) X(void, glShaderSource, (GLuint, GLsizei, const char **, const GLint *)) \
    X(void, glCompileShader, (GLuint)) X(void, glGetShaderiv, (GLuint, GLenum, GLint *)) \
    X(void, glGetShaderInfoLog, (GLuint, GLsizei, GLsizei *, char *)) X(GLuint, glCreateProgram, (void)) \
    X(void, glAttachShader, (GLuint, GLuint)) X(void, glLinkProgram, (GLuint)) X(void, glUseProgram, (GLuint)) \
    X(void, glGetProgramiv, (GLuint, GLenum, GLint *)) X(void, glGetProgramInfoLog, (GLuint, GLsizei, GLsizei *, char *)) \
    X(void, glGenTextures, (GLsizei, GLuint *)) X(void, glBindTexture, (GLenum, GLuint)) \
    X(void, glTexImage2D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *)) \
    X(void, glGenFramebuffers, (GLsizei, GLuint *)) X(void, glBindFramebuffer, (GLenum, GLuint)) \
    X(void, glFramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint)) \
    X(void, glGenVertexArrays, (GLsizei, GLuint *)) X(void, glBindVertexArray, (GLuint)) \
    X(void, glGenBuffers, (GLsizei, GLuint *)) X(void, glBindBuffer, (GLenum, GLuint)) \
    X(void, glBufferData, (GLenum, GLsizeiptr, const void *, GLenum)) \
    X(void, glViewport, (GLint, GLint, GLsizei, GLsizei)) X(void, glClearColor, (GLfloat, GLfloat, GLfloat, GLfloat)) \
    X(void, glClear, (GLbitfield)) X(void, glDrawArrays, (GLenum, GLint, GLsizei)) \
    X(void, glDrawArraysIndirect, (GLenum, const void *)) X(void, glPatchParameteri, (GLenum, GLint)) \
    X(void, glFinish, (void)) X(void, glReadPixels, (GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *)) \
    X(GLenum, glGetError, (void)) X(const GLubyte *, glGetString, (GLenum))
FUNCS(F)

#define GRID 32
#define CELL 4
#define W (GRID * CELL)

static GLuint compile(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[4096]; glGetShaderInfoLog(s, sizeof(log), NULL, log); printf("compile: %s\n", log); exit(1); }
    return s;
}

/* v[] from the id, four rounds mixing every value with its neighbours, then the sum */
#define HEAVY \
    "float heavy(uint id) {\n" \
    "  float v[N];\n" \
    "  for (int j = 0; j < N; j++) v[j] = float((id * 7u + uint(j) * 37u) %% 101u) / 101.0;\n" \
    "  float s = 0.0;\n" \
    "  for (int r = 0; r < 4; r++) for (int j = 0; j < N; j++) { s += v[j] * v[(j + r + 1) %% N]; v[j] = s * 0.5 - v[j]; }\n" \
    "  for (int j = 0; j < N; j++) s += v[j];\n" \
    "  return s; }\n"

int main(int argc, char **argv)
{
    int live = argc > 1 ? atoi(argv[1]) : 64, patches = argc > 2 ? atoi(argv[2]) : GRID * GRID;
    if (patches > GRID * GRID) patches = GRID * GRID;
    EGLDisplay dpy = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, NULL, NULL);
    EGLint major, minor, count;
    EGLConfig config;
    if (!eglInitialize(dpy, &major, &minor)) { printf("eglInitialize failed\n"); return 1; }
    eglBindAPI(EGL_OPENGL_API);
    eglChooseConfig(dpy, (EGLint[]){EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_NONE}, &config, 1, &count);
    EGLContext ctx = eglCreateContext(dpy, config, EGL_NO_CONTEXT,
        (EGLint[]){EGL_CONTEXT_MAJOR_VERSION, 4, EGL_CONTEXT_MINOR_VERSION, 3,
                   EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT, EGL_NONE});
    if (!ctx || !eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)) { printf("no 4.3 context\n"); return 1; }
#define L(ret, name, args) name = (ret (*) args)eglGetProcAddress(#name);
    FUNCS(L)

    char def[64];
    snprintf(def, sizeof(def), "#version 430\n#define N %d\n", live);
    char vs[4096], tcs[4096], tes[2048];
    snprintf(vs, sizeof(vs), "%s" HEAVY
        "out float v_val;\n"
        "void main() { uint p = uint(gl_VertexID) / 3u, c = uint(gl_VertexID) %% 3u;\n"
        "  vec2 o = vec2(float(p %% %du), float(p / %du)) * (2.0 / %d.0) - 1.0;\n"
        "  vec2 d = vec2(c == 1u ? 1.0 : 0.0, c == 2u ? 1.0 : 0.0) * (2.0 / %d.0);\n"
        "  gl_Position = vec4(o + d * 0.9, 0.0, 1.0); v_val = heavy(uint(gl_VertexID)); }\n",
        def, GRID, GRID, GRID, GRID);
    snprintf(tcs, sizeof(tcs), "%s" HEAVY
        "layout(vertices = 3) out; in float v_val[]; out float c_val[];\n"
        "void main() { gl_out[gl_InvocationID].gl_Position = gl_in[gl_InvocationID].gl_Position;\n"
        "  c_val[gl_InvocationID] = v_val[gl_InvocationID] + heavy(uint(gl_PrimitiveID) * 3u + uint(gl_InvocationID) + 5000u);\n"
        "  gl_TessLevelOuter[0] = 1.0; gl_TessLevelOuter[1] = 1.0; gl_TessLevelOuter[2] = 1.0; gl_TessLevelInner[0] = 1.0; }\n",
        def);
    snprintf(tes, sizeof(tes), "#version 430\n"
        "layout(triangles) in; in float c_val[]; flat out float e_val;\n"
        "void main() { gl_Position = gl_TessCoord.x * gl_in[0].gl_Position + gl_TessCoord.y * gl_in[1].gl_Position\n"
        "  + gl_TessCoord.z * gl_in[2].gl_Position; e_val = c_val[0] + 2.0 * c_val[1] + 4.0 * c_val[2]; }\n");
    const char *fs = "#version 430\nflat in float e_val; out vec4 o; void main() { o = vec4(e_val, 1.0, 0.0, 1.0); }\n";

    GLuint prog = glCreateProgram();
    glAttachShader(prog, compile(GL_VERTEX_SHADER, vs));
    glAttachShader(prog, compile(GL_TESS_CONTROL_SHADER, tcs));
    glAttachShader(prog, compile(GL_TESS_EVALUATION_SHADER, tes));
    glAttachShader(prog, compile(GL_FRAGMENT_SHADER, fs));
    glLinkProgram(prog);
    GLint ok;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) { char log[4096]; glGetProgramInfoLog(prog, sizeof(log), NULL, log); printf("link: %s\n", log); return 1; }
    glUseProgram(prog);

    GLuint tex[2], fbo[2], vao, ind;
    glGenTextures(2, tex);
    glGenFramebuffers(2, fbo);
    for (int i = 0; i < 2; i++) {
        glBindTexture(GL_TEXTURE_2D, tex[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, W, W, 0, GL_RGBA, GL_FLOAT, NULL);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex[i], 0);
    }
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    GLuint cmd[4] = {patches * 3, 1, 0, 0};
    glGenBuffers(1, &ind);
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, ind);
    glBufferData(GL_DRAW_INDIRECT_BUFFER, sizeof(cmd), cmd, GL_STATIC_DRAW);
    glPatchParameteri(GL_PATCH_VERTICES, 3);
    glViewport(0, 0, W, W);

    float *px[2];
    for (int i = 0; i < 2; i++) {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo[i]);
        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT);
        if (i == 0) glDrawArrays(GL_PATCHES, 0, patches * 3);
        else glDrawArraysIndirect(GL_PATCHES, 0);
        px[i] = malloc(W * W * 16);
        glReadPixels(0, 0, W, W, GL_RGBA, GL_FLOAT, px[i]);
    }
    int differ = 0, drawn = 0;
    for (int p = 0; p < patches; p++) {
        int x = (p % GRID) * CELL + 1, y = (p / GRID) * CELL + 1;
        float *a = px[0] + (y * W + x) * 4, *b = px[1] + (y * W + x) * 4;
        if (a[1] == 1.0f) drawn++;
        if (memcmp(a, b, 16)) {
            if (differ < 4) printf("patch %d: direct %g %g, indirect %g %g\n", p, a[0], a[1], b[0], b[1]);
            differ++;
        }
    }
    printf("%s, %d live floats: %d patches, %d drawn directly, %d differ in the indirect draw, GL error 0x%x\n",
           glGetString(GL_RENDERER), live, patches, drawn, differ, glGetError());
    return differ != 0;
}
