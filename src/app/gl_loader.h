// gl_loader.h
//
// Minimal runtime GL loader for the handful of fixed-function GL calls the
// preview renderer needs (background clear, viewport, 2D texture upload).
//
// Rationale: the only GL we use is ~10 functions, all GL 1.x/2.x core. Rather
// than pulling in glad (whose upstream repo ships no prebuilt src/gl.c) we
// resolve these pointers at runtime via SDL_GL_GetProcAddress. Dear ImGui's
// OpenGL3 backend uses its OWN embedded loader (imgui_impl_opengl3_loader.h);
// this one is independent and the two never clash (different translation
// units, different symbol names).
//
#pragma once

#include <SDL3/SDL.h>

#ifndef APIENTRY
#define APIENTRY
#endif

// Minimal GL typedefs (avoids pulling in system GL headers).
typedef unsigned int  GLenum;
typedef int           GLint;
typedef int           GLsizei;
typedef unsigned int  GLbitfield;
typedef float         GLfloat;
typedef float         GLclampf;
typedef unsigned char GLboolean;
typedef unsigned int  GLuint;
typedef void          GLvoid;

#define GL_TEXTURE_2D          0x0DE1u
#define GL_TEXTURE_MIN_FILTER  0x2801u
#define GL_TEXTURE_MAG_FILTER  0x2800u
#define GL_TEXTURE_WRAP_S      0x2802u
#define GL_TEXTURE_WRAP_T      0x2803u
#define GL_NEAREST             0x2600u
#define GL_CLAMP_TO_EDGE       0x812Fu
#define GL_RGBA                0x1908u
#define GL_UNSIGNED_BYTE       0x1401u
#define GL_COLOR_BUFFER_BIT    0x00004000u
#define GL_UNPACK_ROW_LENGTH   0x0CF2u

typedef GLvoid (APIENTRY *PFNGLCLEARCOLOR)(GLclampf, GLclampf, GLclampf, GLclampf);
typedef GLvoid (APIENTRY *PFNGLCLEAR)(GLbitfield);
typedef GLvoid (APIENTRY *PFNGLVIEWPORT)(GLint, GLint, GLsizei, GLsizei);
typedef GLvoid (APIENTRY *PFNGLGENTEXTURES)(GLsizei, GLuint *);
typedef GLvoid (APIENTRY *PFNGLBINDTEXTURE)(GLenum, GLuint);
typedef GLvoid (APIENTRY *PFNGLTEXPARAMETERI)(GLenum, GLenum, GLint);
typedef GLvoid (APIENTRY *PFNGLPIXELSTOREI)(GLenum, GLint);
typedef GLvoid (APIENTRY *PFNGLTEXIMAGE2D)(GLenum, GLint, GLint, GLsizei, GLsizei,
                                           GLint, GLenum, GLenum, const GLvoid *);
typedef GLvoid (APIENTRY *PFNGLDELETETEXTURES)(GLsizei, const GLuint *);

extern PFNGLCLEARCOLOR     pfn_glClearColor;
extern PFNGLCLEAR          pfn_glClear;
extern PFNGLVIEWPORT       pfn_glViewport;
extern PFNGLGENTEXTURES    pfn_glGenTextures;
extern PFNGLBINDTEXTURE    pfn_glBindTexture;
extern PFNGLTEXPARAMETERI  pfn_glTexParameteri;
extern PFNGLPIXELSTOREI    pfn_glPixelStorei;
extern PFNGLTEXIMAGE2D     pfn_glTexImage2D;
extern PFNGLDELETETEXTURES pfn_glDeleteTextures;

// Resolve all pointers. Must be called after SDL_GL_MakeCurrent().
// Returns false if any required function is missing.
bool gl_loader_init();
