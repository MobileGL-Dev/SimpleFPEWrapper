// SimpleFPEWrapper - tests/smoke_texgen_toggle.c
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// Toggling GL_TEXTURE_GEN_S/T selects a different generated program, and the
// program-key cache's fast path has to notice the toggle in BOTH directions.
// It did not: its enable comparison was field-by-field and texture_gen_enable
// was not among the fields, so switching texgen OFF between two otherwise
// identical draws HIT the cache and kept drawing with the texgen program.
// Minecraft's enchantment glint is sphere-map texgen that comes and goes with
// the camera, which turned the miss into "lighting is broken from some view
// angles" under shader packs.
//
// The texture has a red left half and a blue right half. The vertex texcoords
// address the red half; the object-linear texgen planes address the blue
// half. Which colour appears IS which program ran, so the off -> on -> off
// sequence checks the cache notices both edges.
//
// Skips (77) when the machine has no EGL device.

#include <dlfcn.h>
#include <stdio.h>

#include <EGL/egl.h>

typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef unsigned char GLubyte;
typedef float GLfloat;
typedef double GLdouble;
typedef int GLint, GLsizei;
typedef unsigned int GLbitfield;

#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_TRIANGLES 0x0004
#define GL_FLOAT 0x1406
#define GL_RGBA 0x1908
#define GL_UNSIGNED_BYTE 0x1401
#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_NEAREST 0x2600
#define GL_VERTEX_ARRAY 0x8074
#define GL_TEXTURE_COORD_ARRAY 0x8078
#define GL_TEXTURE_GEN_S 0x0C60
#define GL_TEXTURE_GEN_T 0x0C61
#define GL_TEXTURE_GEN_MODE 0x2500
#define GL_OBJECT_PLANE 0x2501
#define GL_OBJECT_LINEAR 0x2401
#define GL_S 0x2000
#define GL_T 0x2001
#define GL_NO_ERROR 0

static void (*fClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
static void (*fClear)(GLbitfield);
static void (*fDrawArrays)(GLenum, GLint, GLsizei);
static void (*fReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*);
static GLenum (*fGetError)(void);
static void (*fFinish)(void);
static void (*fEnable)(GLenum);
static void (*fDisable)(GLenum);
static void (*fEnableClientState)(GLenum);
static void (*fDisableClientState)(GLenum);
static void (*fVertexPointer)(GLint, GLenum, GLsizei, const void*);
static void (*fTexCoordPointer)(GLint, GLenum, GLsizei, const void*);
static void (*fGenTextures)(GLsizei, GLuint*);
static void (*fBindTexture)(GLenum, GLuint);
static void (*fTexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum,
                           const void*);
static void (*fTexParameteri)(GLenum, GLenum, GLint);
static void (*fTexGeni)(GLenum, GLenum, GLint);
static void (*fTexGenfv)(GLenum, GLenum, const GLfloat*);
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
    R(fEnable,"glEnable") R(fDisable,"glDisable")
    R(fEnableClientState,"glEnableClientState") R(fDisableClientState,"glDisableClientState")
    R(fVertexPointer,"glVertexPointer") R(fTexCoordPointer,"glTexCoordPointer")
    R(fGenTextures,"glGenTextures") R(fBindTexture,"glBindTexture")
    R(fTexImage2D,"glTexImage2D") R(fTexParameteri,"glTexParameteri")
    R(fTexGeni,"glTexGeni") R(fTexGenfv,"glTexGenfv") R(fColor4f,"glColor4f")
#undef R

    // 2x1: texel 0 red, texel 1 blue.
    static const GLubyte texels[] = { 255,0,0,255,  0,0,255,255 };
    static const GLfloat pos[] = { -1,-1,  1,-1,  1,1,   -1,-1,  1,1,  -1,1 };
    // Vertex texcoords address the RED texel (s in [0, 0.5)).
    static const GLfloat uv[]  = { 0.25f,0.5f, 0.25f,0.5f, 0.25f,0.5f,
                                   0.25f,0.5f, 0.25f,0.5f, 0.25f,0.5f };
    // Object-linear planes address the BLUE texel: s = 0*x+0*y+0*z+0.75.
    static const GLfloat planeS[4] = {0.0f, 0.0f, 0.0f, 0.75f};
    static const GLfloat planeT[4] = {0.0f, 0.0f, 0.0f, 0.5f};

    GLuint tex = 0;
    fGenTextures(1, &tex);
    fBindTexture(GL_TEXTURE_2D, tex);
    fTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, texels);
    fTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    fTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    fEnable(GL_TEXTURE_2D);

    fTexGeni(GL_S, GL_TEXTURE_GEN_MODE, GL_OBJECT_LINEAR);
    fTexGeni(GL_T, GL_TEXTURE_GEN_MODE, GL_OBJECT_LINEAR);
    fTexGenfv(GL_S, GL_OBJECT_PLANE, planeS);
    fTexGenfv(GL_T, GL_OBJECT_PLANE, planeT);

    fVertexPointer(2, GL_FLOAT, 0, pos);
    fTexCoordPointer(2, GL_FLOAT, 0, uv);
    fEnableClientState(GL_VERTEX_ARRAY);
    fEnableClientState(GL_TEXTURE_COORD_ARRAY);
    fColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    fClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    // The toggle sequence. Every draw is identical except for the texgen
    // enable, so a cache that overlooks that enable serves the wrong program
    // on one of the edges.
    drawAndExpect(1, 0, 0, "texgen off: vertex texcoords sample red");

    fEnable(GL_TEXTURE_GEN_S);
    fEnable(GL_TEXTURE_GEN_T);
    drawAndExpect(0, 0, 1, "texgen on: generated coordinates sample blue");

    fDisable(GL_TEXTURE_GEN_S);
    fDisable(GL_TEXTURE_GEN_T);
    drawAndExpect(1, 0, 0, "texgen off again: back to vertex texcoords");

    fEnable(GL_TEXTURE_GEN_S);
    fEnable(GL_TEXTURE_GEN_T);
    drawAndExpect(0, 0, 1, "texgen on again: the on-program is not stale either");

    const GLenum err = fGetError();
    if (err != GL_NO_ERROR) {
        fprintf(stderr, "FAIL: glGetError 0x%x\n", err); ++failures; }

    fDisable(GL_TEXTURE_GEN_S);
    fDisable(GL_TEXTURE_GEN_T);
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
    printf("OK: the program cache follows texgen through both edges\n");
    return 0;
}
