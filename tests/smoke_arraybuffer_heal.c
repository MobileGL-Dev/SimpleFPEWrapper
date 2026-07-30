// SimpleFPEWrapper - tests/smoke_arraybuffer_heal.c
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header
//
// The wrapper shadows GL_ARRAY_BUFFER_BINDING so fixed-function draws can decide
// where to source attributes without a synchronous query per draw. An app that
// binds GL_ARRAY_BUFFER without going through the wrapper - JNI dispatching
// straight to the driver, or another wrapper layered underneath - desynchronizes
// that shadow.
//
// The program and VAO shadows already re-read the truth every 256 queries and
// self-heal. The array-buffer shadow did not: once seeded it answered from
// itself for the rest of the process, so a single bypassed bind stayed wrong
// forever and every later fixed-function draw sourced attributes from the wrong
// buffer.
//
// This test resolves glBindBuffer from the BACKEND (libGLESv2) rather than the
// wrapper and binds through it, so the wrapper never sees the call.
//
// SCOPE, stated precisely because it is narrower than the name suggests: this
// verifies that a bypassed bind does not corrupt the wrapper's own
// fixed-function draws. It does NOT isolate the 256-query heal - it passes with
// the heal disabled, because the explicit wrapper-side glBindBuffer after the
// bypass reseeds the shadow directly. Deterministically observing the heal needs
// the app to draw from the bypassed binding with no intervening wrapper bind,
// which means a user-program passthrough draw whose glVertexAttribPointer was
// baked before the bypass - a harness this file does not build. The heal itself
// is justified by parity with the program and VAO shadows (which already
// reconcile every 256 queries) and by the attribute-buffer bind elision now
// depending on this shadow being right; see plans/12.
//
// Two buffers hold different geometry, so "which buffer is bound" is directly
// visible as pixels:
//   green buffer: a full-screen quad
//   red buffer:   a small off-centre triangle that misses the sample point
//
// Skips (77) when the machine has no EGL device, or when the backend's
// glBindBuffer cannot be resolved separately from the wrapper's.

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
#define GL_UNSIGNED_BYTE 0x1401
#define GL_ARRAY_BUFFER 0x8892
#define GL_STATIC_DRAW 0x88E4
#define GL_VERTEX_ARRAY 0x8074
#define GL_COLOR_ARRAY 0x8076
#define GL_MODELVIEW_MATRIX 0x0BA6
#define GL_NO_ERROR 0

static void (*fClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
static void (*fClear)(GLbitfield);
static void (*fDrawArrays)(GLenum, GLint, GLsizei);
static void (*fReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*);
static GLenum (*fGetError)(void);
static void (*fFinish)(void);
static void (*fGenBuffers)(GLsizei, GLuint*);
static void (*fBindBuffer)(GLenum, GLuint);
static void (*fBufferData)(GLenum, GLsizeiptr, const void*, GLenum);
static void (*fEnableClientState)(GLenum);
static void (*fDisableClientState)(GLenum);
static void (*fVertexPointer)(GLint, GLenum, GLsizei, const void*);
static void (*fColorPointer)(GLint, GLenum, GLsizei, const void*);
static void (*fGetFloatv)(GLenum, GLfloat*);

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
    R(fGenBuffers,"glGenBuffers") R(fBindBuffer,"glBindBuffer")
    R(fBufferData,"glBufferData")
    R(fEnableClientState,"glEnableClientState")
    R(fDisableClientState,"glDisableClientState")
    R(fVertexPointer,"glVertexPointer") R(fColorPointer,"glColorPointer")
    R(fGetFloatv,"glGetFloatv")
#undef R

    // The bypass: glBindBuffer straight from the backend, skipping the wrapper.
    void* backend = dlopen("libGLESv2.so.2", RTLD_NOW | RTLD_LOCAL);
    if (!backend) backend = dlopen("libGLESv2.so", RTLD_NOW | RTLD_LOCAL);
    void (*rawBindBuffer)(GLenum, GLuint) = NULL;
    if (backend) *(void**)(&rawBindBuffer) = dlsym(backend, "glBindBuffer");
    if (!rawBindBuffer) {
        printf("SKIP: cannot resolve the backend's glBindBuffer for the bypass\n");
        return 77;
    }

    // Interleaved x,y,r,g,b,a in both buffers.
    static const GLfloat greenQuad[] = {
        -1,-1, 0,1,0,1,   1,-1, 0,1,0,1,   1,1, 0,1,0,1,
        -1,-1, 0,1,0,1,   1, 1, 0,1,0,1,  -1,1, 0,1,0,1,
    };
    static const GLfloat redSliver[] = {
        -0.9f,-0.9f, 1,0,0,1,  -0.8f,-0.9f, 1,0,0,1,  -0.85f,-0.8f, 1,0,0,1,
        -0.9f,-0.9f, 1,0,0,1,  -0.8f,-0.9f, 1,0,0,1,  -0.85f,-0.8f, 1,0,0,1,
    };
    const GLsizei stride = 6 * (GLsizei)sizeof(GLfloat);

    GLuint bufGreen = 0, bufRed = 0;
    fGenBuffers(1, &bufGreen);
    fBindBuffer(GL_ARRAY_BUFFER, bufGreen);
    fBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof greenQuad, greenQuad, GL_STATIC_DRAW);
    fGenBuffers(1, &bufRed);
    fBindBuffer(GL_ARRAY_BUFFER, bufRed);
    fBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof redSliver, redSliver, GL_STATIC_DRAW);

    // Settle on the green buffer THROUGH the wrapper, so its shadow is seeded
    // and correct. Offsets, so the draw sources from the bound buffer.
    fBindBuffer(GL_ARRAY_BUFFER, bufGreen);
    fVertexPointer(2, GL_FLOAT, stride, (const void*)0);
    fColorPointer(4, GL_FLOAT, stride, (const void*)(2 * sizeof(GLfloat)));
    fEnableClientState(GL_VERTEX_ARRAY);
    fEnableClientState(GL_COLOR_ARRAY);
    fClearColor(0.0f, 0.0f, 1.0f, 1.0f);

    fClear(GL_COLOR_BUFFER_BIT);
    fDrawArrays(GL_TRIANGLES, 0, 6);
    fFinish();
    expect(0, 1, 0, "baseline: wrapper-bound buffer draws green");

    // Now bypass: rebind to the green buffer behind the wrapper's back. The
    // value matches what the shadow already holds, so this alone changes
    // nothing - it is the control for the step after it.
    rawBindBuffer(GL_ARRAY_BUFFER, bufGreen);
    fClear(GL_COLOR_BUFFER_BIT);
    fDrawArrays(GL_TRIANGLES, 0, 6);
    fFinish();
    expect(0, 1, 0, "control: a bypassed bind to the same buffer is harmless");

    // The real bypass: the backend now has bufRed bound while the wrapper's
    // shadow still says bufGreen. Drive well past the 256-query heal interval
    // with calls that consult the shadow, then confirm the wrapper has
    // reconciled and draws agree with the backend's actual binding again.
    rawBindBuffer(GL_ARRAY_BUFFER, bufRed);
    for (int i = 0; i < 600; ++i) {
        // glVertexPointer records the array-buffer binding per pointer, which
        // is exactly the path that reads the shadow.
        fVertexPointer(2, GL_FLOAT, stride, (const void*)0);
    }

    // Re-point through the wrapper so the layout is unambiguous, then draw.
    // Whatever the wrapper believes must now match the driver: binding bufRed
    // through the wrapper and drawing must produce the red sliver, which misses
    // the centre and leaves the clear colour there.
    fBindBuffer(GL_ARRAY_BUFFER, bufRed);
    fVertexPointer(2, GL_FLOAT, stride, (const void*)0);
    fColorPointer(4, GL_FLOAT, stride, (const void*)(2 * sizeof(GLfloat)));
    fClear(GL_COLOR_BUFFER_BIT);
    fDrawArrays(GL_TRIANGLES, 0, 6);
    fFinish();
    expect(0, 0, 1, "post-bypass: the red buffer's sliver misses the centre");

    // And back to green, to show the shadow tracks both directions.
    fBindBuffer(GL_ARRAY_BUFFER, bufGreen);
    fVertexPointer(2, GL_FLOAT, stride, (const void*)0);
    fColorPointer(4, GL_FLOAT, stride, (const void*)(2 * sizeof(GLfloat)));
    fClear(GL_COLOR_BUFFER_BIT);
    fDrawArrays(GL_TRIANGLES, 0, 6);
    fFinish();
    expect(0, 1, 0, "post-bypass: the green buffer draws green again");

    const GLenum err = fGetError();
    if (err != GL_NO_ERROR) {
        fprintf(stderr, "FAIL: glGetError 0x%x\n", err); ++failures; }

    fDisableClientState(GL_VERTEX_ARRAY);
    fDisableClientState(GL_COLOR_ARRAY);
    fBindBuffer(GL_ARRAY_BUFFER, 0);

    if (failures != 0) {
        fprintf(stderr, "FAIL: %d check(s) failed\n", failures); return 1; }
    printf("OK: the array-buffer shadow survives a bypassed bind\n");
    return 0;
}
