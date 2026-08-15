// ===========================================================================
//  Minimal GL surface. No GL headers -- values are from the GL spec and are
//  stable across every implementation.
// ===========================================================================
typedef unsigned int  GLenum;
typedef unsigned int  GLuint;
typedef unsigned int  GLbitfield;
typedef int           GLint;
typedef int           GLsizei;
typedef float         GLfloat;
typedef double        GLclampd;
typedef unsigned char GLboolean;
typedef void          GLvoid;

#define GL_NO_ERROR                    0
#define GL_NONE                        0
#define GL_ZERO                        0
#define GL_FALSE                       0
#define GL_TRUE                        1
#define GL_DEPTH_BUFFER_BIT            0x00000100
#define GL_COLOR_BUFFER_BIT            0x00004000
#define GL_TEXTURE_2D                  0x0DE1
#define GL_TEXTURE_2D_ARRAY            0x8C1A
#define GL_LESS                        0x0201
#define GL_UNSIGNED_INT                0x1405
#define GL_UNSIGNED_BYTE               0x1401
#define GL_TEXTURE_BORDER_COLOR        0x1004
#define GL_DEPTH_COMPONENT             0x1902
#define GL_LEQUAL                      0x0203
#define GL_VIEWPORT                    0x0BA2
#define GL_DEPTH_TEST                  0x0B71
#define GL_BLEND                       0x0BE2
#define GL_BLEND_SRC_RGB                0x80C9
#define GL_BLEND_DST_RGB                0x80C8
#define GL_BLEND_SRC_ALPHA              0x80CB
#define GL_BLEND_DST_ALPHA              0x80CA
#define GL_BLEND_EQUATION_RGB           0x8009
#define GL_BLEND_EQUATION_ALPHA         0x883D
#define GL_FUNC_ADD                     0x8006
#define GL_SRC_ALPHA                    0x0302
#define GL_ONE                          1
#define GL_ONE_MINUS_SRC_ALPHA          0x0303
#define GL_DEPTH_WRITEMASK             0x0B72
#define GL_COLOR_WRITEMASK             0x0C23
#define GL_SCISSOR_TEST                0x0C11
#define GL_NEAREST                     0x2600
#define GL_LINEAR                      0x2601
#define GL_COMPARE_REF_TO_TEXTURE      0x884E
#define GL_TEXTURE_MAG_FILTER          0x2800
#define GL_TEXTURE_MIN_FILTER          0x2801
#define GL_TEXTURE_WRAP_S              0x2802
#define GL_TEXTURE_WRAP_T              0x2803
#define GL_TEXTURE_BINDING_2D          0x8069
#define GL_TEXTURE_BINDING_2D_ARRAY    0x8C1D
#define GL_CLAMP_TO_BORDER             0x812D
#define GL_DEPTH_COMPONENT24           0x81A6
#define GL_SAMPLES_PASSED              0x8914
#define GL_TEXTURE0                    0x84C0
#define GL_ACTIVE_TEXTURE              0x84E0
#define GL_MAX_TEXTURE_IMAGE_UNITS     0x8872
#define GL_CURRENT_PROGRAM             0x8B8D
#define GL_QUERY_RESULT                0x8866
#define GL_TEXTURE_COMPARE_MODE        0x884C
#define GL_TEXTURE_COMPARE_FUNC        0x884D
#define GL_R32UI                        0x8236
#define GL_RED_INTEGER                  0x8D94
#define GL_CLAMP_TO_EDGE                0x812F
#define GL_FRAMEBUFFER                 0x8D40
#define GL_FRAMEBUFFER_BINDING         0x8CA6   // == GL_DRAW_FRAMEBUFFER_BINDING
#define GL_FRAMEBUFFER_COMPLETE        0x8CD5
#define GL_DEPTH_ATTACHMENT            0x8D00
#define GL_READ_FRAMEBUFFER            0x8CA8
#define GL_DRAW_FRAMEBUFFER            0x8CA9
#define GL_READ_FRAMEBUFFER_BINDING    0x8CAA
#define GL_DRAW_FRAMEBUFFER_BINDING    0x8CA6
#define GL_SAMPLES                     0x80A9
#define GL_VERSION                     0x1F02
#define GL_RENDERER                    0x1F01
#define GL_VENDOR                      0x1F00
#define GL_COLOR_ATTACHMENT0            0x8CE0
#define GL_RGBA                         0x1908
#define GL_RGBA8                        0x8058
#define GL_R8                           0x8229
#define GL_R32F                         0x822E
#define GL_RED                          0x1903
#define GL_FLOAT                        0x1406
#define GL_VERTEX_SHADER                0x8B31
#define GL_FRAGMENT_SHADER              0x8B30
#define GL_COMPILE_STATUS               0x8B81
#define GL_LINK_STATUS                  0x8B82
// GL_NVX_gpu_memory_info.  NVIDIA exposes these through glGetIntegerv; Mesa,
// AMD and Intel normally do not.  The values are KiB and describe the whole
// GL driver's memory pool, not this process alone.
#define GL_GPU_MEMORY_INFO_DEDICATED_VIDMEM_NVX 0x9047
#define GL_GPU_MEMORY_INFO_CURRENT_AVAILABLE_VIDMEM_NVX 0x9049

namespace gl {
    void     (*GetIntegerv)(GLenum, GLint*)                                = nullptr;
    void     (*GetBooleanv)(GLenum, GLboolean*)                            = nullptr;
    void     (*GetFloatv)(GLenum, GLfloat*)                                = nullptr;
    GLboolean(*IsEnabled)(GLenum)                                          = nullptr;
    GLenum   (*GetError)()                                                 = nullptr;
    const unsigned char* (*GetString)(GLenum)                              = nullptr;
    void     (*Enable)(GLenum)                                             = nullptr;
    void     (*PolygonOffset)(GLfloat, GLfloat)                            = nullptr;
    void     (*CullFace)(GLenum)                                           = nullptr;
    void     (*Disable)(GLenum)                                            = nullptr;
    void     (*BlendFuncSeparate)(GLenum, GLenum, GLenum, GLenum)          = nullptr;
    void     (*BlendEquationSeparate)(GLenum, GLenum)                      = nullptr;
    void     (*Viewport)(GLint, GLint, GLsizei, GLsizei)                   = nullptr;
    void     (*Clear)(GLbitfield)                                          = nullptr;
    void     (*ClearColor)(GLfloat, GLfloat, GLfloat, GLfloat)             = nullptr;
    void     (*ClearDepth)(GLclampd)                                       = nullptr;
    void     (*ColorMask)(GLboolean, GLboolean, GLboolean, GLboolean)      = nullptr;
    void     (*DepthMask)(GLboolean)                                       = nullptr;
    void     (*Finish)()                                                   = nullptr;
    void     (*GenTextures)(GLsizei, GLuint*)                              = nullptr;
    void     (*DeleteTextures)(GLsizei, const GLuint*)                     = nullptr;
    void     (*BindTexture)(GLenum, GLuint)                                = nullptr;
    void     (*TexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint,
                           GLenum, GLenum, const GLvoid*)                  = nullptr;
    void     (*TexImage3D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLsizei,
                           GLint, GLenum, GLenum, const GLvoid*)            = nullptr;
    void     (*TexSubImage2D)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei,
                              GLenum, GLenum, const GLvoid*)                = nullptr;
    void     (*CopyTexSubImage2D)(GLenum, GLint, GLint, GLint, GLint, GLint,
                                  GLsizei, GLsizei)                          = nullptr;
    void     (*CopyTexSubImage3D)(GLenum, GLint, GLint, GLint, GLint, GLint,
                                  GLint, GLsizei, GLsizei)                   = nullptr;
    void     (*TexParameteri)(GLenum, GLenum, GLint)                       = nullptr;
    void     (*TexParameterfv)(GLenum, GLenum, const GLfloat*)             = nullptr;
    void     (*GetTexImage)(GLenum, GLint, GLenum, GLenum, GLvoid*)        = nullptr;
    void     (*DrawBuffer)(GLenum)                                         = nullptr;
    void     (*ReadBuffer)(GLenum)                                         = nullptr;
    void     (*GenFramebuffers)(GLsizei, GLuint*)                          = nullptr;
    void     (*DeleteFramebuffers)(GLsizei, const GLuint*)                 = nullptr;
    void     (*BindFramebuffer)(GLenum, GLuint)                            = nullptr;
    void     (*FramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint)= nullptr;
    void     (*FramebufferTextureLayer)(GLenum, GLenum, GLuint, GLint, GLint)= nullptr;
    GLenum   (*CheckFramebufferStatus)(GLenum)                             = nullptr;
    // Occlusion queries -- the probe's cheap "did anything draw" instrument.
    void     (*GenQueries)(GLsizei, GLuint*)                               = nullptr;
    void     (*DeleteQueries)(GLsizei, const GLuint*)                      = nullptr;
    void     (*BeginQuery)(GLenum, GLuint)                                 = nullptr;
    void     (*EndQuery)(GLenum)                                           = nullptr;
    void     (*GetQueryObjectuiv)(GLuint, GLenum, GLuint*)                 = nullptr;
    void     (*ActiveTexture)(GLenum)                                      = nullptr;
    GLint    (*GetUniformLocation)(GLuint, const char*)                    = nullptr;
    void     (*Uniform1i)(GLint, GLint)                                    = nullptr;
    void     (*Uniform1f)(GLint, GLfloat)                                  = nullptr;
    void     (*Uniform2f)(GLint, GLfloat, GLfloat)                         = nullptr;
    void     (*Uniform4fv)(GLint, GLsizei, const GLfloat*)                   = nullptr;
    void     (*Uniform1fv)(GLint, GLsizei, const GLfloat*)                   = nullptr;
    void     (*Uniform4f)(GLint, GLfloat, GLfloat, GLfloat, GLfloat)       = nullptr;
    void     (*UniformMatrix4fv)(GLint, GLsizei, GLboolean, const GLfloat*)= nullptr;
    void     (*GetUniformiv)(GLuint, GLint, GLint*)                        = nullptr;
    void     (*GetUniformfv)(GLuint, GLint, GLfloat*)                       = nullptr;
    GLboolean(*IsProgram)(GLuint)                                           = nullptr;
    void     (*BlitFramebuffer)(GLint,GLint,GLint,GLint,GLint,GLint,GLint,GLint,
                                GLbitfield,GLenum)                          = nullptr;
    void     (*GetTexParameteriv)(GLenum, GLenum, GLint*)                  = nullptr;
    void     (*GetTexLevelParameteriv)(GLenum, GLint, GLenum, GLint*)      = nullptr;
}

#define GLBIND(name) \
    do { *(void**)(&gl::name) = dlsym(RTLD_DEFAULT, "gl" #name); \
         if (!gl::name) { fprintf(stderr, "[shadowmap] missing GL entry: gl%s\n", #name); ok = false; } } while (0)

static bool bind_gl() {
    bool ok = true;
    GLBIND(GetIntegerv);  GLBIND(GetBooleanv);   GLBIND(IsEnabled);  GLBIND(GetFloatv);
    GLBIND(IsProgram); GLBIND(BlitFramebuffer);
    GLBIND(GetError);     GLBIND(GetString);
    GLBIND(Enable);       GLBIND(Disable);       GLBIND(PolygonOffset); GLBIND(CullFace);       GLBIND(BlendFuncSeparate); GLBIND(BlendEquationSeparate); GLBIND(Viewport);
    GLBIND(Clear);        GLBIND(ClearColor);    GLBIND(ClearDepth); GLBIND(ColorMask); GLBIND(DepthMask);
    GLBIND(Finish);
    GLBIND(GenTextures);  GLBIND(DeleteTextures);GLBIND(BindTexture);
    GLBIND(TexImage2D);   GLBIND(TexImage3D);    GLBIND(TexSubImage2D); GLBIND(CopyTexSubImage2D); GLBIND(CopyTexSubImage3D); GLBIND(TexParameteri); GLBIND(TexParameterfv);
    GLBIND(GetTexImage);  GLBIND(DrawBuffer);    GLBIND(ReadBuffer);
    GLBIND(GenFramebuffers); GLBIND(DeleteFramebuffers);
    GLBIND(BindFramebuffer); GLBIND(FramebufferTexture2D); GLBIND(FramebufferTextureLayer);
    GLBIND(CheckFramebufferStatus);
    GLBIND(GenQueries);   GLBIND(DeleteQueries); GLBIND(BeginQuery);
    GLBIND(EndQuery);     GLBIND(GetQueryObjectuiv);
    GLBIND(ActiveTexture);GLBIND(GetUniformLocation);
    GLBIND(Uniform1i);    GLBIND(Uniform1f); GLBIND(Uniform2f); GLBIND(Uniform4f); GLBIND(Uniform4fv); GLBIND(Uniform1fv); GLBIND(UniformMatrix4fv); GLBIND(GetUniformiv); GLBIND(GetUniformfv);
    GLBIND(GetTexParameteriv); GLBIND(GetTexLevelParameteriv);
    // WHICH GPU IS ACTUALLY RENDERING. The only other place this was printed is
    // create_shadow_target(), which the accepted CSM path never calls -- so
    // neither platform's log contained it, and a Windows-vs-Linux comparison
    // where every timing differs by a uniform 3.4-4.8x cannot be interpreted
    // without it. Hybrid-graphics machines routinely hand an OpenGL app the
    // integrated GPU on one OS and the discrete one on the other.
    if (ok && gl::GetString) {
        const unsigned char* ven = gl::GetString(GL_VENDOR);
        const unsigned char* ren = gl::GetString(GL_RENDERER);
        const unsigned char* ver = gl::GetString(GL_VERSION);
        fprintf(stderr, "[shadowmap][gpu] vendor=%s | renderer=%s | version=%s\n",
                ven ? (const char*)ven : "?", ren ? (const char*)ren : "?",
                ver ? (const char*)ver : "?");
    }
    return ok;
}
#undef GLBIND

