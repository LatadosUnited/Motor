#pragma once

// Carregador de OpenGL próprio (substitui glad/glew).
// Header privado: só os .cpp da engine incluem. Os headers públicos não expõem tipos GL.
//
// Para usar uma função nova: adicione-a em MOTOR_GL_FUNCTIONS com o tipo PFN... do glcorearb.h.

#include <GL/glcorearb.h>

#define MOTOR_GL_FUNCTIONS(X)                                         \
    /* Estado / frame */                                              \
    X(PFNGLGETSTRINGPROC, glGetString)                                \
    X(PFNGLGETINTEGERVPROC, glGetIntegerv)                            \
    X(PFNGLENABLEPROC, glEnable)                                      \
    X(PFNGLDISABLEPROC, glDisable)                                    \
    X(PFNGLVIEWPORTPROC, glViewport)                                  \
    X(PFNGLCLEARCOLORPROC, glClearColor)                              \
    X(PFNGLCLEARPROC, glClear)                                        \
    X(PFNGLCULLFACEPROC, glCullFace)                                  \
    X(PFNGLDEPTHFUNCPROC, glDepthFunc)                                \
    X(PFNGLDEPTHMASKPROC, glDepthMask)                                \
    X(PFNGLPOLYGONMODEPROC, glPolygonMode)                            \
    X(PFNGLPOLYGONOFFSETPROC, glPolygonOffset)                        \
    X(PFNGLDRAWELEMENTSPROC, glDrawElements)                          \
    X(PFNGLBLENDFUNCSEPARATEPROC, glBlendFuncSeparate)                \
    /* Debug */                                                       \
    X(PFNGLDEBUGMESSAGECALLBACKPROC, glDebugMessageCallback)          \
    X(PFNGLDEBUGMESSAGECONTROLPROC, glDebugMessageControl)            \
    /* Shaders */                                                     \
    X(PFNGLCREATESHADERPROC, glCreateShader)                          \
    X(PFNGLSHADERSOURCEPROC, glShaderSource)                          \
    X(PFNGLCOMPILESHADERPROC, glCompileShader)                        \
    X(PFNGLGETSHADERIVPROC, glGetShaderiv)                            \
    X(PFNGLGETSHADERINFOLOGPROC, glGetShaderInfoLog)                  \
    X(PFNGLDELETESHADERPROC, glDeleteShader)                          \
    X(PFNGLCREATEPROGRAMPROC, glCreateProgram)                        \
    X(PFNGLATTACHSHADERPROC, glAttachShader)                          \
    X(PFNGLDETACHSHADERPROC, glDetachShader)                          \
    X(PFNGLLINKPROGRAMPROC, glLinkProgram)                            \
    X(PFNGLGETPROGRAMIVPROC, glGetProgramiv)                          \
    X(PFNGLGETPROGRAMINFOLOGPROC, glGetProgramInfoLog)                \
    X(PFNGLDELETEPROGRAMPROC, glDeleteProgram)                        \
    X(PFNGLUSEPROGRAMPROC, glUseProgram)                              \
    X(PFNGLGETUNIFORMLOCATIONPROC, glGetUniformLocation)              \
    X(PFNGLPROGRAMUNIFORM1IPROC, glProgramUniform1i)                  \
    X(PFNGLPROGRAMUNIFORM1FPROC, glProgramUniform1f)                  \
    X(PFNGLPROGRAMUNIFORM2FVPROC, glProgramUniform2fv)                \
    X(PFNGLPROGRAMUNIFORM3FVPROC, glProgramUniform3fv)                \
    X(PFNGLPROGRAMUNIFORM4FVPROC, glProgramUniform4fv)                \
    X(PFNGLPROGRAMUNIFORMMATRIX3FVPROC, glProgramUniformMatrix3fv)    \
    X(PFNGLPROGRAMUNIFORMMATRIX4FVPROC, glProgramUniformMatrix4fv)    \
    /* Buffers (DSA) */                                               \
    X(PFNGLCREATEBUFFERSPROC, glCreateBuffers)                        \
    X(PFNGLNAMEDBUFFERSTORAGEPROC, glNamedBufferStorage)              \
    X(PFNGLNAMEDBUFFERSUBDATAPROC, glNamedBufferSubData)              \
    X(PFNGLDELETEBUFFERSPROC, glDeleteBuffers)                        \
    X(PFNGLBINDBUFFERBASEPROC, glBindBufferBase)                      \
    /* Vertex arrays (DSA) */                                         \
    X(PFNGLCREATEVERTEXARRAYSPROC, glCreateVertexArrays)              \
    X(PFNGLDELETEVERTEXARRAYSPROC, glDeleteVertexArrays)              \
    X(PFNGLBINDVERTEXARRAYPROC, glBindVertexArray)                    \
    X(PFNGLVERTEXARRAYVERTEXBUFFERPROC, glVertexArrayVertexBuffer)    \
    X(PFNGLVERTEXARRAYELEMENTBUFFERPROC, glVertexArrayElementBuffer)  \
    X(PFNGLENABLEVERTEXARRAYATTRIBPROC, glEnableVertexArrayAttrib)    \
    X(PFNGLVERTEXARRAYATTRIBFORMATPROC, glVertexArrayAttribFormat)    \
    X(PFNGLVERTEXARRAYATTRIBBINDINGPROC, glVertexArrayAttribBinding)  \
    /* Texturas (DSA) */                                              \
    X(PFNGLCREATETEXTURESPROC, glCreateTextures)                      \
    X(PFNGLTEXTURESTORAGE2DPROC, glTextureStorage2D)                  \
    X(PFNGLTEXTURESUBIMAGE2DPROC, glTextureSubImage2D)                \
    X(PFNGLTEXTUREPARAMETERIPROC, glTextureParameteri)                \
    X(PFNGLTEXTUREPARAMETERFPROC, glTextureParameterf)                \
    X(PFNGLGENERATETEXTUREMIPMAPPROC, glGenerateTextureMipmap)        \
    X(PFNGLBINDTEXTUREUNITPROC, glBindTextureUnit)                    \
    X(PFNGLDELETETEXTURESPROC, glDeleteTextures)                      \
    X(PFNGLGETTEXTUREIMAGEPROC, glGetTextureImage)                    \
    /* Framebuffers (DSA) */                                          \
    X(PFNGLCREATEFRAMEBUFFERSPROC, glCreateFramebuffers)              \
    X(PFNGLDELETEFRAMEBUFFERSPROC, glDeleteFramebuffers)              \
    X(PFNGLBINDFRAMEBUFFERPROC, glBindFramebuffer)                    \
    X(PFNGLNAMEDFRAMEBUFFERTEXTUREPROC, glNamedFramebufferTexture)    \
    X(PFNGLNAMEDFRAMEBUFFERRENDERBUFFERPROC, glNamedFramebufferRenderbuffer) \
    X(PFNGLCHECKNAMEDFRAMEBUFFERSTATUSPROC, glCheckNamedFramebufferStatus) \
    X(PFNGLBLITNAMEDFRAMEBUFFERPROC, glBlitNamedFramebuffer)          \
    X(PFNGLCREATERENDERBUFFERSPROC, glCreateRenderbuffers)            \
    X(PFNGLDELETERENDERBUFFERSPROC, glDeleteRenderbuffers)            \
    X(PFNGLNAMEDRENDERBUFFERSTORAGEMULTISAMPLEPROC, glNamedRenderbufferStorageMultisample) \
    /* Fase 7: sombras, céu, HDR e pós-processamento */               \
    X(PFNGLDRAWARRAYSPROC, glDrawArrays)                              \
    X(PFNGLBLENDFUNCPROC, glBlendFunc)                                \
    X(PFNGLGENTEXTURESPROC, glGenTextures)                            \
    X(PFNGLTEXTUREVIEWPROC, glTextureView)                            \
    X(PFNGLTEXTURESTORAGE3DPROC, glTextureStorage3D)                  \
    X(PFNGLTEXTUREPARAMETERFVPROC, glTextureParameterfv)              \
    X(PFNGLNAMEDFRAMEBUFFERTEXTURELAYERPROC, glNamedFramebufferTextureLayer) \
    X(PFNGLNAMEDFRAMEBUFFERDRAWBUFFERPROC, glNamedFramebufferDrawBuffer) \
    X(PFNGLNAMEDFRAMEBUFFERREADBUFFERPROC, glNamedFramebufferReadBuffer) \
    X(PFNGLCLEARNAMEDFRAMEBUFFERFVPROC, glClearNamedFramebufferfv)    \
    X(PFNGLCOLORMASKPROC, glColorMask)                                \
    /* Fase 22: timer queries das zonas de GPU do profiler (TracyOpenGL.hpp) */ \
    X(PFNGLGENQUERIESPROC, glGenQueries)                              \
    X(PFNGLDELETEQUERIESPROC, glDeleteQueries)                        \
    X(PFNGLGETQUERYIVPROC, glGetQueryiv)                              \
    X(PFNGLGETQUERYOBJECTIVPROC, glGetQueryObjectiv)                  \
    X(PFNGLGETQUERYOBJECTUI64VPROC, glGetQueryObjectui64v)            \
    X(PFNGLGETINTEGER64VPROC, glGetInteger64v)                        \
    X(PFNGLQUERYCOUNTERPROC, glQueryCounter)                          \
    X(PFNGLGETERRORPROC, glGetError)                                  \
    X(PFNGLGETSTRINGIPROC, glGetStringi)                              \
    /* Draw com o intervalo de vértices: o driver da Intel não varre os índices na CPU (Mesh::Draw) */ \
    X(PFNGLDRAWRANGEELEMENTSPROC, glDrawRangeElements)

// Os ponteiros vivem num namespace próprio para não colidir com os símbolos
// exportados pela opengl32.dll; o using-directive abaixo permite chamá-los como glXxx().
namespace motor::glfn {
#define MOTOR_GL_DECLARE(type, name) extern type name;
MOTOR_GL_FUNCTIONS(MOTOR_GL_DECLARE)
#undef MOTOR_GL_DECLARE
} // namespace motor::glfn

using namespace motor::glfn;

namespace motor::gl {
using ProcLoader = void* (*)(const char* name);

// Carrega todas as funções. Requer um contexto OpenGL 4.5 ativo.
bool Load(ProcLoader loader);
} // namespace motor::gl
