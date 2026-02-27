/*
 * r_gl.h -- OpenGL function declarations for the FAKK2 renderer
 *
 * Loads GL functions via SDL2's GL proc address mechanism.
 * This replaces the original's LoadLibraryA("opengl32.dll") approach.
 *
 * We target OpenGL 2.1 compatibility profile as the minimum, which
 * provides fixed-function pipeline (matching original FAKK2) plus
 * shaders and FBOs for modern enhancements.
 */

#ifndef R_GL_H
#define R_GL_H

#ifdef USE_SDL2
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
#else
#include <GL/gl.h>
#endif

/* =========================================================================
 * Extension functions loaded at runtime
 *
 * These are functions beyond GL 1.1 that we need to load dynamically
 * via SDL_GL_GetProcAddress. On Windows, only GL 1.1 functions are
 * directly available from opengl32.dll.
 * ========================================================================= */

/* Multitexture (GL 1.3) */
typedef void (APIENTRY *PFNGLACTIVETEXTUREPROC)(GLenum texture);
typedef void (APIENTRY *PFNGLCLIENTACTIVETEXTUREPROC)(GLenum texture);

/* VBO (GL 1.5) */
typedef void (APIENTRY *PFNGLGENBUFFERSPROC)(GLsizei n, GLuint *buffers);
typedef void (APIENTRY *PFNGLDELETEBUFFERSPROC)(GLsizei n, const GLuint *buffers);
typedef void (APIENTRY *PFNGLBINDBUFFERPROC)(GLenum target, GLuint buffer);
typedef void (APIENTRY *PFNGLBUFFERDATAPROC)(GLenum target, GLsizeiptr size, const void *data, GLenum usage);

/* Shaders (GL 2.0) */
typedef GLuint (APIENTRY *PFNGLCREATESHADERPROC)(GLenum type);
typedef void (APIENTRY *PFNGLDELETESHADERPROC)(GLuint shader);
typedef void (APIENTRY *PFNGLSHADERSOURCEPROC)(GLuint shader, GLsizei count, const GLchar **string, const GLint *length);
typedef void (APIENTRY *PFNGLCOMPILESHADERPROC)(GLuint shader);
typedef GLuint (APIENTRY *PFNGLCREATEPROGRAMPROC)(void);
typedef void (APIENTRY *PFNGLDELETEPROGRAMPROC)(GLuint program);
typedef void (APIENTRY *PFNGLATTACHSHADERPROC)(GLuint program, GLuint shader);
typedef void (APIENTRY *PFNGLLINKPROGRAMPROC)(GLuint program);
typedef void (APIENTRY *PFNGLUSEPROGRAMPROC)(GLuint program);
typedef GLint (APIENTRY *PFNGLGETUNIFORMLOCATIONPROC)(GLuint program, const GLchar *name);
typedef void (APIENTRY *PFNGLUNIFORM1IPROC)(GLint location, GLint v0);
typedef void (APIENTRY *PFNGLUNIFORM1FPROC)(GLint location, GLfloat v0);
typedef void (APIENTRY *PFNGLUNIFORM3FPROC)(GLint location, GLfloat v0, GLfloat v1, GLfloat v2);
typedef void (APIENTRY *PFNGLUNIFORM4FPROC)(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3);
typedef void (APIENTRY *PFNGLUNIFORMMATRIX4FVPROC)(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);
typedef void (APIENTRY *PFNGLGETSHADERIVPROC)(GLuint shader, GLenum pname, GLint *params);
typedef void (APIENTRY *PFNGLGETSHADERINFOLOGPROC)(GLuint shader, GLsizei maxLength, GLsizei *length, GLchar *infoLog);
typedef void (APIENTRY *PFNGLGETPROGRAMIVPROC)(GLuint program, GLenum pname, GLint *params);
typedef void (APIENTRY *PFNGLGETPROGRAMINFOLOGPROC)(GLuint program, GLsizei maxLength, GLsizei *length, GLchar *infoLog);

/* VAO (GL 3.0) */
typedef void (APIENTRY *PFNGLGENVERTEXARRAYSPROC)(GLsizei n, GLuint *arrays);
typedef void (APIENTRY *PFNGLDELETEVERTEXARRAYSPROC)(GLsizei n, const GLuint *arrays);
typedef void (APIENTRY *PFNGLBINDVERTEXARRAYPROC)(GLuint array);
typedef void (APIENTRY *PFNGLENABLEVERTEXATTRIBARRAYPROC)(GLuint index);
typedef void (APIENTRY *PFNGLVERTEXATTRIBPOINTERPROC)(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *pointer);

/* =========================================================================
 * Global function pointers
 * ========================================================================= */

extern PFNGLACTIVETEXTUREPROC           qglActiveTexture;
extern PFNGLCLIENTACTIVETEXTUREPROC     qglClientActiveTexture;
extern PFNGLGENBUFFERSPROC              qglGenBuffers;
extern PFNGLDELETEBUFFERSPROC           qglDeleteBuffers;
extern PFNGLBINDBUFFERPROC              qglBindBuffer;
extern PFNGLBUFFERDATAPROC              qglBufferData;
extern PFNGLCREATESHADERPROC            qglCreateShader;
extern PFNGLDELETESHADERPROC            qglDeleteShader;
extern PFNGLSHADERSOURCEPROC            qglShaderSource;
extern PFNGLCOMPILESHADERPROC           qglCompileShader;
extern PFNGLCREATEPROGRAMPROC           qglCreateProgram;
extern PFNGLDELETEPROGRAMPROC           qglDeleteProgram;
extern PFNGLATTACHSHADERPROC            qglAttachShader;
extern PFNGLLINKPROGRAMPROC             qglLinkProgram;
extern PFNGLUSEPROGRAMPROC              qglUseProgram;
extern PFNGLGETUNIFORMLOCATIONPROC      qglGetUniformLocation;
extern PFNGLUNIFORM1IPROC              qglUniform1i;
extern PFNGLUNIFORM1FPROC              qglUniform1f;
extern PFNGLUNIFORM3FPROC              qglUniform3f;
extern PFNGLUNIFORM4FPROC              qglUniform4f;
extern PFNGLUNIFORMMATRIX4FVPROC       qglUniformMatrix4fv;
extern PFNGLGETSHADERIVPROC            qglGetShaderiv;
extern PFNGLGETSHADERINFOLOGPROC       qglGetShaderInfoLog;
extern PFNGLGETPROGRAMIVPROC           qglGetProgramiv;
extern PFNGLGETPROGRAMINFOLOGPROC      qglGetProgramInfoLog;
extern PFNGLGENVERTEXARRAYSPROC        qglGenVertexArrays;
extern PFNGLDELETEVERTEXARRAYSPROC     qglDeleteVertexArrays;
extern PFNGLBINDVERTEXARRAYPROC        qglBindVertexArray;
extern PFNGLENABLEVERTEXATTRIBARRAYPROC qglEnableVertexAttribArray;
extern PFNGLVERTEXATTRIBPOINTERPROC    qglVertexAttribPointer;

/* =========================================================================
 * Loader
 * ========================================================================= */

qboolean R_LoadGLFunctions(void);

#endif /* R_GL_H */
