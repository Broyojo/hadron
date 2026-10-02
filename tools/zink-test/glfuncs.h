/* The OpenGL functions the tests use, loaded through eglGetProcAddress. */
/* An EGL-only Mesa has no libGL: every OpenGL function comes from eglGetProcAddress, as it does
 * for Wine. */
#define GL_FUNCS(X) \
    X(const GLubyte *, GetString, (GLenum)) X(GLenum, GetError, (void)) \
    X(GLuint, CreateShader, (GLenum)) X(void, ShaderSource, (GLuint, GLsizei, const char **, const GLint *)) \
    X(void, CompileShader, (GLuint)) X(void, GetShaderiv, (GLuint, GLenum, GLint *)) \
    X(void, GetShaderInfoLog, (GLuint, GLsizei, GLsizei *, char *)) X(GLuint, CreateProgram, (void)) \
    X(void, AttachShader, (GLuint, GLuint)) X(void, LinkProgram, (GLuint)) X(void, UseProgram, (GLuint)) \
    X(void, GenTextures, (GLsizei, GLuint *)) X(void, BindTexture, (GLenum, GLuint)) \
    X(void, TexImage2D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *)) \
    X(void, GenFramebuffers, (GLsizei, GLuint *)) X(void, BindFramebuffer, (GLenum, GLuint)) \
    X(void, FramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint)) X(GLenum, CheckFramebufferStatus, (GLenum)) \
    X(void, GenVertexArrays, (GLsizei, GLuint *)) X(void, BindVertexArray, (GLuint)) \
    X(void, GenBuffers, (GLsizei, GLuint *)) X(void, BindBuffer, (GLenum, GLuint)) \
    X(void, BufferData, (GLenum, GLsizeiptr, const void *, GLenum)) X(GLint, GetAttribLocation, (GLuint, const char *)) \
    X(void, VertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void *)) \
    X(void, EnableVertexAttribArray, (GLuint)) X(void, Viewport, (GLint, GLint, GLsizei, GLsizei)) \
    X(void, ClearColor, (GLfloat, GLfloat, GLfloat, GLfloat)) X(void, Clear, (GLbitfield)) \
    X(void, DrawArrays, (GLenum, GLint, GLsizei)) X(void, GetIntegerv, (GLenum, GLint *)) \
    X(void, DrawElements, (GLenum, GLsizei, GLenum, const void *)) X(void, BufferSubData, (GLenum, GLintptr, GLsizeiptr, const void *)) \
    X(void, Finish, (void)) X(void, Flush, (void)) X(void, PixelStorei, (GLenum, GLint)) \
    X(const GLubyte *, GetStringi, (GLenum, GLuint)) \
    X(void, ReadPixels, (GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *))
#define X(ret, name, args) static ret (*p_gl##name) args;
GL_FUNCS(X)
#undef X
#define glGetString p_glGetString
#define glGetError p_glGetError
#define glCreateShader p_glCreateShader
#define glShaderSource p_glShaderSource
#define glCompileShader p_glCompileShader
#define glGetShaderiv p_glGetShaderiv
#define glGetShaderInfoLog p_glGetShaderInfoLog
#define glCreateProgram p_glCreateProgram
#define glAttachShader p_glAttachShader
#define glLinkProgram p_glLinkProgram
#define glUseProgram p_glUseProgram
#define glGenTextures p_glGenTextures
#define glBindTexture p_glBindTexture
#define glTexImage2D p_glTexImage2D
#define glGenFramebuffers p_glGenFramebuffers
#define glBindFramebuffer p_glBindFramebuffer
#define glFramebufferTexture2D p_glFramebufferTexture2D
#define glCheckFramebufferStatus p_glCheckFramebufferStatus
#define glGenVertexArrays p_glGenVertexArrays
#define glBindVertexArray p_glBindVertexArray
#define glGenBuffers p_glGenBuffers
#define glBindBuffer p_glBindBuffer
#define glBufferData p_glBufferData
#define glGetAttribLocation p_glGetAttribLocation
#define glVertexAttribPointer p_glVertexAttribPointer
#define glEnableVertexAttribArray p_glEnableVertexAttribArray
#define glViewport p_glViewport
#define glClearColor p_glClearColor
#define glClear p_glClear
#define glDrawArrays p_glDrawArrays
#define glReadPixels p_glReadPixels
#define glGetIntegerv p_glGetIntegerv
#define glDrawElements p_glDrawElements
#define glBufferSubData p_glBufferSubData
#define glFinish p_glFinish
#define glFlush p_glFlush
#define glPixelStorei p_glPixelStorei
#define glGetStringi p_glGetStringi

static void load_gl_funcs(void)
{
#define X(ret, name, args) p_gl##name = (ret (*) args)eglGetProcAddress("gl" #name);
    GL_FUNCS(X)
#undef X
}
