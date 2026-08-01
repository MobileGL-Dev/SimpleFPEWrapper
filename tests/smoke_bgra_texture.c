// SimpleFPEWrapper - tests/smoke_bgra_texture.c
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// GL_BGRA texture data, through both routes the wrapper has for it on a GLES
// backend:
//
//   plain pointer  the bytes pass through the wrapper, which reorders them
//                  and stores ordinary RGBA texels
//   pixel buffer   the bytes never pass through the wrapper at all, so the
//                  texels are stored in the application's order and the
//                  sampler swizzle reorders them at read time
//
// Both must sample as the colour the application described, and a texture
// must be able to move between the two: an upload of the other kind replaces
// the level, so the swizzle from a previous upload has to go with it. That
// last part is what a per-texture mode gets wrong when it is only ever set
// and never cleared, which is why the ordinary upload here comes last.
//
// Probe colours keep R != B - a channel swap has to be visible - and the
// quad is drawn with the texture modulating a white vertex colour.
//
// Skips (77) when the machine has no EGL device.

#include <dlfcn.h>
#include <stdio.h>

#include <EGL/egl.h>

typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef unsigned char GLubyte;
typedef float GLfloat;
typedef int GLint, GLsizei;
typedef unsigned int GLbitfield;
typedef long GLsizeiptr;

#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_TRIANGLES 0x0004
#define GL_FLOAT 0x1406
#define GL_RGBA 0x1908
#define GL_BGRA 0x80E1
#define GL_UNSIGNED_BYTE 0x1401
#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_NEAREST 0x2600
#define GL_VERTEX_ARRAY 0x8074
#define GL_TEXTURE_COORD_ARRAY 0x8078
#define GL_PIXEL_UNPACK_BUFFER 0x88EC
#define GL_STATIC_DRAW 0x88E4
#define GL_NO_ERROR 0

static void (*fClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
static void (*fClear)(GLbitfield);
static void (*fDrawArrays)(GLenum, GLint, GLsizei);
static void (*fReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*);
static GLenum (*fGetError)(void);
static void (*fFinish)(void);
static void (*fEnable)(GLenum);
static void (*fEnableClientState)(GLenum);
static void (*fDisableClientState)(GLenum);
static void (*fVertexPointer)(GLint, GLenum, GLsizei, const void*);
static void (*fTexCoordPointer)(GLint, GLenum, GLsizei, const void*);
static void (*fGenTextures)(GLsizei, GLuint*);
static void (*fBindTexture)(GLenum, GLuint);
static void (*fTexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum,
                           const void*);
static void (*fTexParameteri)(GLenum, GLenum, GLint);
static void (*fGenBuffers)(GLsizei, GLuint*);
static void (*fBindBuffer)(GLenum, GLuint);
static void (*fBufferData)(GLenum, GLsizeiptr, const void*, GLenum);
static void (*fColor4f)(GLfloat, GLfloat, GLfloat, GLfloat);

static int failures;

static void drawAndExpect(int r, int g, int b, const char* what) {
    fClear(GL_COLOR_BUFFER_BIT);
    fDrawArrays(GL_TRIANGLES, 0, 6);
    fFinish();
    GLubyte p[4] = {0, 0, 0, 0};
    fReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p);
    const int ok = ((p[0] > 200) == (r > 0)) && ((p[1] > 200) == (g > 0)) &&
                   ((p[2] > 200) == (b > 0));
    if (!ok) {
        fprintf(stderr, "FAIL: %s: pixel = (%u,%u,%u), expected (%d,%d,%d)\n", what, p[0], p[1],
                p[2], r, g, b);
        ++failures;
    } else {
        printf("OK: %s\n", what);
    }
}

int main(void) {
    void* handle = dlopen(WRAPPER_LIB_PATH, RTLD_NOW | RTLD_LOCAL);
    if (!handle) { fprintf(stderr, "FAIL: dlopen: %s\n", dlerror()); return 1; }
    typedef void* (*resolver_t)(const char*);
    resolver_t resolve = (resolver_t)dlsym(handle, "eglGetProcAddress");
    if (!resolve) return 1;

    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display == EGL_NO_DISPLAY || !eglInitialize(display, NULL, NULL)) {
        printf("SKIP: no EGL display\n"); return 77; }
    static const EGLint cfg[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE,
        EGL_OPENGL_ES3_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8, EGL_NONE};
    EGLConfig config; EGLint n = 0;
    if (!eglChooseConfig(display, cfg, &config, 1, &n) || n == 0) {
        printf("SKIP: no ES3 config\n"); return 77; }
    static const EGLint pb[] = {EGL_WIDTH, 64, EGL_HEIGHT, 64, EGL_NONE};
    EGLSurface surface = eglCreatePbufferSurface(display, config, pb);
    eglBindAPI(EGL_OPENGL_ES_API);
    static const EGLint ca[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, ca);
    if (surface == EGL_NO_SURFACE || context == EGL_NO_CONTEXT ||
        !eglMakeCurrent(display, surface, surface, context)) {
        printf("SKIP: could not make ES3 context current\n"); return 77; }

#define R(fn, s) fn = (typeof(fn))resolve(s); if (!fn) { fprintf(stderr, "FAIL: missing %s\n", s); return 1; }
    R(fClearColor,"glClearColor") R(fClear,"glClear") R(fDrawArrays,"glDrawArrays")
    R(fReadPixels,"glReadPixels") R(fGetError,"glGetError") R(fFinish,"glFinish")
    R(fEnable,"glEnable") R(fEnableClientState,"glEnableClientState")
    R(fDisableClientState,"glDisableClientState") R(fVertexPointer,"glVertexPointer")
    R(fTexCoordPointer,"glTexCoordPointer") R(fGenTextures,"glGenTextures")
    R(fBindTexture,"glBindTexture") R(fTexImage2D,"glTexImage2D")
    R(fTexParameteri,"glTexParameteri") R(fGenBuffers,"glGenBuffers")
    R(fBindBuffer,"glBindBuffer") R(fBufferData,"glBufferData") R(fColor4f,"glColor4f")
#undef R

    // 2x2 texels. Read as B,G,R,A this is red; read as R,G,B,A it is blue.
    static const GLubyte texels[] = {
        0, 0, 255, 255,  0, 0, 255, 255,
        0, 0, 255, 255,  0, 0, 255, 255,
    };
    static const GLfloat pos[] = { -1,-1,  1,-1,  1,1,   -1,-1,  1,1,  -1,1 };
    static const GLfloat uv[]  = {  0, 0,  1, 0,  1, 1,    0, 0,  1, 1,   0, 1 };

    GLuint tex = 0;
    fGenTextures(1, &tex);
    fBindTexture(GL_TEXTURE_2D, tex);
    fTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    fTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    fEnable(GL_TEXTURE_2D);

    fVertexPointer(2, GL_FLOAT, 0, pos);
    fTexCoordPointer(2, GL_FLOAT, 0, uv);
    fEnableClientState(GL_VERTEX_ARRAY);
    fEnableClientState(GL_TEXTURE_COORD_ARRAY);
    fColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    fClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    // Route 1: the wrapper sees the bytes and reorders them.
    fTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_BGRA, GL_UNSIGNED_BYTE, texels);
    drawAndExpect(1, 0, 0, "GL_BGRA texture from client memory samples as described");

    // Route 2: the same bytes through a pixel buffer object, which the
    // wrapper cannot reach into.
    GLuint pbo = 0;
    fGenBuffers(1, &pbo);
    fBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbo);
    fBufferData(GL_PIXEL_UNPACK_BUFFER, (GLsizeiptr)sizeof(texels), texels, GL_STATIC_DRAW);
    fTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_BGRA, GL_UNSIGNED_BYTE, (const void*)0);
    fBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    drawAndExpect(1, 0, 0, "GL_BGRA texture from a pixel buffer samples as described");

    // The same bytes uploaded as ordinary RGBA must come out the other way
    // round - and this is the case a per-texture mode gets wrong if it is
    // never cleared, because the level before it was the swizzled one.
    fTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, texels);
    drawAndExpect(0, 0, 1, "an ordinary upload afterwards is not read swizzled");

    // ...and back again, so the mode is not one-way.
    fTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_BGRA, GL_UNSIGNED_BYTE, texels);
    drawAndExpect(1, 0, 0, "switching back to GL_BGRA takes effect");

    const GLenum err = fGetError();
    if (err != GL_NO_ERROR) {
        fprintf(stderr, "FAIL: glGetError 0x%x\n", err); ++failures; }

    fDisableClientState(GL_TEXTURE_COORD_ARRAY);
    fDisableClientState(GL_VERTEX_ARRAY);
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(display, context);
    eglDestroySurface(display, surface);
    eglTerminate(display);

    if (failures) {
        fprintf(stderr, "FAIL: %d check(s) failed\n", failures);
        return 1;
    }
    printf("OK: GL_BGRA textures sample as described through both routes\n");
    return 0;
}
