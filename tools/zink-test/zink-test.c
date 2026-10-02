/* Zink on KosmicKrisp without a window: creates the newest OpenGL context EGL will give on the
 * surfaceless platform, prints what it is, and draws a triangle into a framebuffer object to
 * check that rendering and reading back work.
 *
 * Build and run: see run.sh. */
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <stdio.h>
#include <string.h>

#include "glfuncs.h"

/* The extensions each OpenGL version adds to the one before, as Mesa requires them. */
static const struct { const char *version, *extensions; } required[] = {
    {"4.0", "ARB_draw_buffers_blend ARB_draw_indirect ARB_gpu_shader5 ARB_gpu_shader_fp64 ARB_sample_shading "
            "ARB_tessellation_shader ARB_texture_buffer_object_rgb32 ARB_texture_cube_map_array ARB_texture_gather "
            "ARB_texture_query_lod ARB_transform_feedback2 ARB_transform_feedback3"},
    {"4.1", "ARB_ES2_compatibility ARB_vertex_attrib_64bit ARB_viewport_array"},
    {"4.2", "ARB_base_instance ARB_conservative_depth ARB_internalformat_query ARB_shader_atomic_counters "
            "ARB_shader_image_load_store ARB_shading_language_420pack ARB_shading_language_packing "
            "ARB_texture_compression_bptc ARB_transform_feedback_instanced"},
    {"4.3", "ARB_ES3_compatibility ARB_arrays_of_arrays ARB_compute_shader ARB_copy_image ARB_explicit_uniform_location "
            "ARB_fragment_layer_viewport ARB_framebuffer_no_attachments ARB_internalformat_query2 "
            "ARB_robust_buffer_access_behavior ARB_shader_image_size ARB_shader_storage_buffer_object "
            "ARB_stencil_texturing ARB_texture_buffer_range ARB_texture_query_levels ARB_texture_view "
            "ARB_vertex_attrib_binding"},
    {"4.4", "ARB_buffer_storage ARB_clear_texture ARB_enhanced_layouts ARB_query_buffer_object "
            "ARB_texture_mirror_clamp_to_edge ARB_texture_stencil8 ARB_vertex_type_10f_11f_11f_rev ARB_multi_bind"},
    {"4.5", "ARB_ES3_1_compatibility ARB_clip_control ARB_conditional_render_inverted ARB_cull_distance "
            "ARB_derivative_control ARB_shader_texture_image_samples ARB_direct_state_access "
            "ARB_get_texture_sub_image KHR_robustness ARB_texture_barrier"},
    {"4.6", "ARB_gl_spirv ARB_spirv_extensions ARB_indirect_parameters ARB_pipeline_statistics_query "
            "ARB_polygon_offset_clamp ARB_shader_atomic_counter_ops ARB_shader_draw_parameters "
            "ARB_shader_group_vote ARB_texture_filter_anisotropic ARB_transform_feedback_overflow_query"},
};

static int has_extension(const char *name, size_t len)
{
    GLint count = 0;

    glGetIntegerv(GL_NUM_EXTENSIONS, &count);
    for (GLint i = 0; i < count; i++)
    {
        const char *ext = (const char *)glGetStringi(GL_EXTENSIONS, i);
        if (!strncmp(ext, "GL_", 3) && strlen(ext + 3) == len && !strncmp(ext + 3, name, len)) return 1;
    }
    return 0;
}

static void print_missing(void)
{
    for (unsigned v = 0; v < sizeof(required) / sizeof(required[0]); v++)
    {
        int missing = 0;

        printf("OpenGL %s is missing:", required[v].version);
        for (const char *p = required[v].extensions; *p;)
        {
            size_t len = strcspn(p, " ");
            if (!has_extension(p, len)) { printf(" %.*s", (int)len, p); missing++; }
            p += len;
            while (*p == ' ') p++;
        }
        printf(missing ? "\n" : " nothing\n");
    }
}

static GLuint compile(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type);
    GLint ok;
    char log[1024];

    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { glGetShaderInfoLog(s, sizeof(log), NULL, log); printf("shader: %s\n", log); }
    return s;
}

int main(void)
{
    static const int versions[][2] = {{4, 6}, {4, 5}, {4, 4}, {4, 3}, {4, 2}, {4, 1}, {4, 0}, {3, 3}, {3, 2}};
    EGLDisplay dpy = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, NULL, NULL);
    EGLint major, minor, count;
    EGLConfig config;
    EGLContext ctx = EGL_NO_CONTEXT;

    load_gl_funcs();

    if (!eglInitialize(dpy, &major, &minor)) { printf("eglInitialize failed: 0x%x\n", eglGetError()); return 1; }
    printf("EGL %d.%d, %s\n", major, minor, eglQueryString(dpy, EGL_VENDOR));
    eglBindAPI(EGL_OPENGL_API);
    eglChooseConfig(dpy, (EGLint[]){EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_NONE}, &config, 1, &count);
    if (!count) { printf("no config\n"); return 1; }

    for (unsigned i = 0; i < sizeof(versions) / sizeof(versions[0]) && ctx == EGL_NO_CONTEXT; i++)
        ctx = eglCreateContext(dpy, config, EGL_NO_CONTEXT, (EGLint[]){
            EGL_CONTEXT_MAJOR_VERSION, versions[i][0], EGL_CONTEXT_MINOR_VERSION, versions[i][1],
            EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT, EGL_NONE});
    if (ctx == EGL_NO_CONTEXT) { printf("no core context: 0x%x\n", eglGetError()); return 1; }
    if (!eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)) { printf("eglMakeCurrent failed: 0x%x\n", eglGetError()); return 1; }

    printf("vendor   %s\nrenderer %s\nversion  %s\nGLSL     %s\n", glGetString(GL_VENDOR), glGetString(GL_RENDERER),
           glGetString(GL_VERSION), glGetString(GL_SHADING_LANGUAGE_VERSION));

    print_missing();

    EGLContext compat = eglCreateContext(dpy, config, EGL_NO_CONTEXT, (EGLint[]){EGL_NONE});
    if (compat != EGL_NO_CONTEXT && eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, compat))
    {
        printf("compatibility profile: %s\n", glGetString(GL_VERSION));
        eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx);
    }
    else printf("compatibility profile: none (0x%x)\n", eglGetError());

    GLuint fbo, tex, vao, vbo, prog;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 256, 256, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) { printf("framebuffer incomplete\n"); return 1; }

    prog = glCreateProgram();
    glAttachShader(prog, compile(GL_VERTEX_SHADER, "#version 150\nin vec2 p; void main() { gl_Position = vec4(p, 0.0, 1.0); }"));
    glAttachShader(prog, compile(GL_FRAGMENT_SHADER, "#version 150\nout vec4 c; void main() { c = vec4(1.0, 0.5, 0.25, 1.0); }"));
    glLinkProgram(prog);
    glUseProgram(prog);
    static const float tri[] = {-1, -1, 1, -1, 0, 1};
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(tri), tri, GL_STATIC_DRAW);
    glVertexAttribPointer(glGetAttribLocation(prog, "p"), 2, GL_FLOAT, GL_FALSE, 0, 0);
    glEnableVertexAttribArray(glGetAttribLocation(prog, "p"));

    glViewport(0, 0, 256, 256);
    glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    unsigned char centre[4], corner[4];
    glReadPixels(128, 100, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, centre);
    glReadPixels(2, 250, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, corner);
    printf("triangle pixel %u %u %u %u (expect 255 128 64 255), background pixel %u %u %u %u (expect 0 0 255 255), GL error 0x%x\n",
           centre[0], centre[1], centre[2], centre[3], corner[0], corner[1], corner[2], corner[3], glGetError());
    return 0;
}
