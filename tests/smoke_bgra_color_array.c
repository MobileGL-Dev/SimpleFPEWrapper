// SimpleFPEWrapper - tests/smoke_bgra_color_array.c
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// glColorPointer(GL_BGRA, ...) is GL 3.2 / ARB_vertex_array_bgra: four
// normalized unsigned bytes whose ORDER is blue, green, red, alpha. It is a
// component order, not a component count - and 0x80E1 read as a count is what
// the wrapper used to store, which put a nonsense stride on every following
// calculation.
//
// GLES has no such format, so the wrapper hands the driver a plain
// four-component array and undoes the order in the generated shader. A
// desktop GL backend takes GL_BGRA itself. Either way the drawn colour must
// be the one the application described, which is what this checks: the same
// bytes are drawn twice, once declared BGRA and once RGBA, and the two must
// come out as each other's channel swap.
//
// The probe colours keep R == B out of the picture on purpose - a swap has to
// be visible - so this test would fail on a driver that swaps R and B behind
// our back. That is the point.
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

#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_TRIANGLES 0x0004
#define GL_FLOAT 0x1406
#define GL_RGBA 0x1908
#define GL_BGRA 0x80E1
#define GL_UNSIGNED_BYTE 0x1401
#define GL_VERTEX_ARRAY 0x8074
#define GL_COLOR_ARRAY 0x8076
#define GL_NO_ERROR 0

static void (*fClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
static void (*fClear)(GLbitfield);
static void (*fDrawArrays)(GLenum, GLint, GLsizei);
static void (*fReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*);
static GLenum (*fGetError)(void);
static void (*fFinish)(void);
static void (*fEnableClientState)(GLenum);
static void (*fDisableClientState)(GLenum);
static void (*fVertexPointer)(GLint, GLenum, GLsizei, const void*);
static void (*fColorPointer)(GLint, GLenum, GLsizei, const void*);

static int failures;

static void expect(int r, int g, int b, const char* what) {
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
    R(fEnableClientState,"glEnableClientState") R(fDisableClientState,"glDisableClientState")
    R(fVertexPointer,"glVertexPointer") R(fColorPointer,"glColorPointer")
#undef R

    static const GLfloat pos[] = { -1,-1,  1,-1,  1,1,   -1,-1,  1,1,  -1,1 };
    // One byte quadruple per vertex. Read as B,G,R,A this is red; read as
    // R,G,B,A it is blue. Alpha stays opaque either way.
    static const GLubyte packed[] = {
        0, 0, 255, 255,  0, 0, 255, 255,  0, 0, 255, 255,
        0, 0, 255, 255,  0, 0, 255, 255,  0, 0, 255, 255,
    };

    fVertexPointer(2, GL_FLOAT, 0, pos);
    fEnableClientState(GL_VERTEX_ARRAY);
    fEnableClientState(GL_COLOR_ARRAY);
    fClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    fColorPointer(GL_BGRA, GL_UNSIGNED_BYTE, 0, packed);
    fClear(GL_COLOR_BUFFER_BIT);
    fDrawArrays(GL_TRIANGLES, 0, 6);
    fFinish();
    expect(1, 0, 0, "GL_BGRA colour array: B,G,R,A order reaches the shader");

    // The same bytes declared the ordinary way must come out the other way
    // round, which is what makes the check above about the ORDER and not
    // about the wrapper drawing some fixed colour.
    fColorPointer(4, GL_UNSIGNED_BYTE, 0, packed);
    fClear(GL_COLOR_BUFFER_BIT);
    fDrawArrays(GL_TRIANGLES, 0, 6);
    fFinish();
    expect(0, 0, 1, "the same bytes as RGBA draw the swapped colour");

    // Back to BGRA: the two must not share a generated program, and the
    // switch back has to take effect.
    fColorPointer(GL_BGRA, GL_UNSIGNED_BYTE, 0, packed);
    fClear(GL_COLOR_BUFFER_BIT);
    fDrawArrays(GL_TRIANGLES, 0, 6);
    fFinish();
    expect(1, 0, 0, "switching back to GL_BGRA takes effect");

    const GLenum err = fGetError();
    if (err != GL_NO_ERROR) {
        fprintf(stderr, "FAIL: glGetError 0x%x\n", err); ++failures; }

    fDisableClientState(GL_COLOR_ARRAY);
    fDisableClientState(GL_VERTEX_ARRAY);
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(display, context);
    eglDestroySurface(display, surface);
    eglTerminate(display);

    if (failures) {
        fprintf(stderr, "FAIL: %d check(s) failed\n", failures);
        return 1;
    }
    printf("OK: GL_BGRA colour arrays keep their component order\n");
    return 0;
}
