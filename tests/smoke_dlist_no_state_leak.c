// SimpleFPEWrapper - tests/smoke_dlist_no_state_leak.c
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header
//
// Replaying a display list must not disturb the app's GL_ARRAY_BUFFER binding.
//
// The upload of that resident data originally went through GL_ARRAY_BUFFER. It
// runs as an argument to drawImmediateVertices, so it executes BEFORE that
// call's draw-state guard captures the app's bindings - nothing was there to
// undo it. On its own the wrapper recovers, but the array-buffer shadow
// re-reads GL_ARRAY_BUFFER_BINDING every 256 queries to self-heal, and a read
// landing right after the upload would record THE WRAPPER'S PRIVATE BUFFER AS
// THE APP'S. Every later app draw would then source attributes from the
// display-list buffer - garbage texture coordinates. The upload now goes
// through GL_COPY_WRITE_BUFFER, which is not part of vertex array state.
//
// HONEST SCOPE: this test does NOT catch that bug. Verified by reintroducing
// it - all checks still pass, because the upload happens once per run and the
// heal only fires every 256 queries, so the poisoned window is a single call
// that the heal almost never lands in. The bug was found by reading, not by
// this test, and it is kept because the invariant it states is worth guarding
// even though it cannot police the rare interleaving:
//
//   1. GL_ARRAY_BUFFER_BINDING is unchanged across a replay.
//   2. An app draw AFTER a replay still reads its own vertex data.
//   3. The replayed list still renders, so a fix cannot pass by breaking it.
//
// Catching the rare case deterministically would need the heal counter driven
// to a known phase, which no public entry point exposes.
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
typedef unsigned char GLboolean;
typedef long GLsizeiptr;

#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_TRIANGLES 0x0004
#define GL_QUADS 0x0007
#define GL_FLOAT 0x1406
#define GL_RGBA 0x1908
#define GL_UNSIGNED_BYTE 0x1401
#define GL_ARRAY_BUFFER 0x8892
#define GL_ARRAY_BUFFER_BINDING 0x8894
#define GL_STATIC_DRAW 0x88E4
#define GL_COMPILE 0x1300
#define GL_VERTEX_ARRAY 0x8074
#define GL_COLOR_ARRAY 0x8076
#define GL_NO_ERROR 0

static void (*fClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
static void (*fClear)(GLbitfield);
static void (*fDrawArrays)(GLenum, GLint, GLsizei);
static void (*fReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*);
static GLenum (*fGetError)(void);
static void (*fFinish)(void);
static void (*fGetIntegerv)(GLenum, GLint*);
static void (*fGenBuffers)(GLsizei, GLuint*);
static void (*fBindBuffer)(GLenum, GLuint);
static void (*fBufferData)(GLenum, GLsizeiptr, const void*, GLenum);
static void (*fEnableClientState)(GLenum);
static void (*fDisableClientState)(GLenum);
static void (*fVertexPointer)(GLint, GLenum, GLsizei, const void*);
static void (*fColorPointer)(GLint, GLenum, GLsizei, const void*);
static GLuint (*fGenLists)(GLsizei);
static void (*fNewList)(GLuint, GLenum);
static void (*fEndList)(void);
static void (*fCallList)(GLuint);
static void (*fBegin)(GLenum);
static void (*fEnd)(void);
static void (*fColor4f)(GLfloat, GLfloat, GLfloat, GLfloat);
static void (*fVertex2f)(GLfloat, GLfloat);

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
    R(fGetIntegerv,"glGetIntegerv")
    R(fGenBuffers,"glGenBuffers") R(fBindBuffer,"glBindBuffer") R(fBufferData,"glBufferData")
    R(fEnableClientState,"glEnableClientState")
    R(fDisableClientState,"glDisableClientState")
    R(fVertexPointer,"glVertexPointer") R(fColorPointer,"glColorPointer")
    R(fGenLists,"glGenLists") R(fNewList,"glNewList") R(fEndList,"glEndList")
    R(fCallList,"glCallList")
    R(fBegin,"glBegin") R(fEnd,"glEnd") R(fColor4f,"glColor4f") R(fVertex2f,"glVertex2f")
#undef R

    // The app's own buffer: a full-screen GREEN quad, interleaved x,y,r,g,b,a.
    static const GLfloat appVerts[] = {
        -1,-1, 0,1,0,1,   1,-1, 0,1,0,1,   1,1, 0,1,0,1,
        -1,-1, 0,1,0,1,   1, 1, 0,1,0,1,  -1,1, 0,1,0,1,
    };
    const GLsizei stride = 6 * (GLsizei)sizeof(GLfloat);
    GLuint appVbo = 0;
    fGenBuffers(1, &appVbo);
    fBindBuffer(GL_ARRAY_BUFFER, appVbo);
    fBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof appVerts, appVerts, GL_STATIC_DRAW);

    // A display list holding an immediate-mode run. Replaying it is what makes
    // the wrapper materialise its own resident vertex buffer. Deliberately a
    // small RED quad away from the sample point, so if its geometry ever bled
    // into the app's draw the center pixel would stop being green.
    const GLuint list = fGenLists(1);
    fNewList(list, GL_COMPILE);
    fBegin(GL_QUADS);
    fColor4f(1.0f, 0.0f, 0.0f, 1.0f);
    fVertex2f(-0.9f, -0.9f); fVertex2f(-0.8f, -0.9f);
    fVertex2f(-0.8f, -0.8f); fVertex2f(-0.9f, -0.8f);
    fEnd();
    fEndList();

    fBindBuffer(GL_ARRAY_BUFFER, appVbo);
    fVertexPointer(2, GL_FLOAT, stride, (const void*)0);
    fColorPointer(4, GL_FLOAT, stride, (const void*)(2 * sizeof(GLfloat)));
    fEnableClientState(GL_VERTEX_ARRAY);
    fEnableClientState(GL_COLOR_ARRAY);
    fClearColor(0.0f, 0.0f, 1.0f, 1.0f);

    // Baseline, before any replay.
    fClear(GL_COLOR_BUFFER_BIT);
    fDrawArrays(GL_TRIANGLES, 0, 6);
    fFinish();
    expect(0, 1, 0, "baseline: app draws green from its own buffer");

    // The shadow's self-heal fires every 256 queries, so a few iterations would
    // pass even with the binding poisoned. Drive well past that, replaying the
    // list before every app draw.
    int binding_mismatches = 0;
    for (int i = 0; i < 800; ++i) {
        fCallList(list);
        // Each glVertexPointer records the array-buffer binding, which is the
        // path that queries (and heals) the shadow.
        fVertexPointer(2, GL_FLOAT, stride, (const void*)0);
        fColorPointer(4, GL_FLOAT, stride, (const void*)(2 * sizeof(GLfloat)));
        GLint bound = -1;
        fGetIntegerv(GL_ARRAY_BUFFER_BINDING, &bound);
        if (bound != (GLint)appVbo) ++binding_mismatches;
    }
    if (binding_mismatches != 0) {
        fprintf(stderr, "FAIL: GL_ARRAY_BUFFER_BINDING left the app's buffer on %d of 800 "
                        "iterations\n", binding_mismatches);
        ++failures;
    } else {
        printf("OK: array-buffer binding survives 800 display-list replays\n");
    }

    // The property that actually broke: the app's draw must still read the
    // app's vertices, not the display list's.
    fClear(GL_COLOR_BUFFER_BIT);
    fDrawArrays(GL_TRIANGLES, 0, 6);
    fFinish();
    expect(0, 1, 0, "after 800 replays: app still draws from its own buffer");

    // And the list itself must still render - the fix must not have broken it.
    fDisableClientState(GL_VERTEX_ARRAY);
    fDisableClientState(GL_COLOR_ARRAY);
    fBindBuffer(GL_ARRAY_BUFFER, 0);
    fClear(GL_COLOR_BUFFER_BIT);
    fCallList(list);
    fFinish();
    {
        GLubyte p[4] = {0, 0, 0, 0};
        fReadPixels(3, 3, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p); // inside the red quad
        if (!(p[0] > 200 && p[1] < 200 && p[2] < 200)) {
            fprintf(stderr, "FAIL: replayed list did not render red: (%u,%u,%u)\n",
                    p[0], p[1], p[2]);
            ++failures;
        } else {
            printf("OK: the display list itself still renders correctly\n");
        }
    }

    const GLenum err = fGetError();
    if (err != GL_NO_ERROR) {
        fprintf(stderr, "FAIL: glGetError 0x%x\n", err); ++failures; }

    if (failures != 0) {
        fprintf(stderr, "FAIL: %d check(s) failed\n", failures); return 1; }
    printf("OK: display-list replay leaks no vertex-buffer state to the app\n");
    return 0;
}
