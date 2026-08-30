// gl_loader.cpp
#include "gl_loader.h"

PFNGLCLEARCOLOR     pfn_glClearColor     = nullptr;
PFNGLCLEAR          pfn_glClear          = nullptr;
PFNGLVIEWPORT       pfn_glViewport       = nullptr;
PFNGLGENTEXTURES    pfn_glGenTextures    = nullptr;
PFNGLBINDTEXTURE    pfn_glBindTexture    = nullptr;
PFNGLTEXPARAMETERI  pfn_glTexParameteri  = nullptr;
PFNGLPIXELSTOREI    pfn_glPixelStorei    = nullptr;
PFNGLTEXIMAGE2D     pfn_glTexImage2D     = nullptr;
PFNGLDELETETEXTURES pfn_glDeleteTextures = nullptr;

bool gl_loader_init() {
    // SDL_GL_GetProcAddress on Windows falls back to opengl32.dll for GL 1.1
    // entry points (glClear/glViewport/glGenTextures/etc.), so GL 1.x symbols
    // resolve even though wglGetProcAddress would return null for them.
    pfn_glClearColor     = (PFNGLCLEARCOLOR)    SDL_GL_GetProcAddress("glClearColor");
    pfn_glClear          = (PFNGLCLEAR)         SDL_GL_GetProcAddress("glClear");
    pfn_glViewport       = (PFNGLVIEWPORT)      SDL_GL_GetProcAddress("glViewport");
    pfn_glGenTextures    = (PFNGLGENTEXTURES)   SDL_GL_GetProcAddress("glGenTextures");
    pfn_glBindTexture    = (PFNGLBINDTEXTURE)   SDL_GL_GetProcAddress("glBindTexture");
    pfn_glTexParameteri  = (PFNGLTEXPARAMETERI) SDL_GL_GetProcAddress("glTexParameteri");
    pfn_glPixelStorei    = (PFNGLPIXELSTOREI)   SDL_GL_GetProcAddress("glPixelStorei");
    pfn_glTexImage2D     = (PFNGLTEXIMAGE2D)    SDL_GL_GetProcAddress("glTexImage2D");
    pfn_glDeleteTextures = (PFNGLDELETETEXTURES)SDL_GL_GetProcAddress("glDeleteTextures");

    return pfn_glClearColor && pfn_glClear && pfn_glViewport &&
           pfn_glGenTextures && pfn_glBindTexture && pfn_glTexParameteri &&
           pfn_glPixelStorei && pfn_glTexImage2D && pfn_glDeleteTextures;
}
