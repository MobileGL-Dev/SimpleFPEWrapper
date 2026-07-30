// SimpleFPEWrapper - tests/smoke_dlist_beginend.c
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// Ported from piglit's tests/spec/gl-1.0/dlist-beginend.c (MIT-style
// license; see piglit's COPYING). Display lists and Begin/End compose in
// ways the wrapper's compiled-run machinery has to get right: a list holding
// only loose vertex commands is legal and must feed whatever Begin/End block
// is open at glCallList time, and the block may even be split across three
// separate lists (Begin in one, vertices in another, End in a third).
//
// This is the awkward case for the glEndList run compiler
// (sfpewCompileImmediateRuns): a list whose vertices have NO Begin/End
// around them cannot be baked into a compiled draw, because the primitive
// mode is not known until replay. It must stay as replayable per-vertex
// commands that push into the caller's open block.
//
// The upstream subtests asserting GL_INVALID_OPERATION for glRectf /
// glDrawArrays inside Begin/End are deliberately NOT ported: this wrapper
// does not enforce the "illegal inside Begin/End" rule for passthrough
// entry points, a documented architectural decision (see the file header of
// tests/smoke_beginend_coverage.c). What IS ported and enforced is that the
// legal geometry still lands on screen in each arrangement.

#include <dlfcn.h>
#include <stdio.h>
#include <EGL/egl.h>

typedef unsigned int GLenum, GLuint, GLbitfield;
typedef unsigned char GLubyte;
typedef float GLfloat;
typedef int GLint, GLsizei;

#define WIN 32
#define GL_QUADS 0x0007
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_RGBA 0x1908
#define GL_UNSIGNED_BYTE 0x1401
#define GL_COMPILE 0x1300
#define GL_COMPILE_AND_EXECUTE 0x1301
#define GL_NO_ERROR 0
#define GL_INVALID_ENUM 0x0500

static void* (*resolve)(const char*);
static void (*fVertex2f)(GLfloat, GLfloat);
static void (*fBegin)(GLenum);
static void (*fEnd)(void);
static void (*fColor4f)(GLfloat, GLfloat, GLfloat, GLfloat);
static void (*fClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
static void (*fClear)(GLbitfield);
static void (*fFinish)(void);
static void (*fReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*);
static GLuint (*fGenLists)(GLsizei);
static void (*fNewList)(GLuint, GLenum);
static void (*fEndList)(void);
static void (*fCallList)(GLuint);
static void (*fDeleteLists)(GLuint, GLsizei);
static GLenum (*fGetError)(void);

#define R(dst, name)                                                                             \
    do {                                                                                         \
        *(void**)&dst = resolve(name);                                                           \
        if (!dst) { fprintf(stderr, "FAIL: cannot resolve %s\n", name); return 1; }               \
    } while (0)

static int fails;

// The full-viewport quad these tests draw is green; anything else is a fail.
static void expect_green(const char* what) {
    GLubyte p[4] = {0, 0, 0, 0};
    fReadPixels(WIN / 2, WIN / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p);
    const int ok = p[1] > 150 && p[0] < 100;
    printf("%s %-46s rgba=(%3u,%3u,%3u,%3u)\n", ok ? "OK  " : "FAIL", what, p[0], p[1], p[2], p[3]);
    if (!ok) ++fails;
}

static void expect_black(const char* what) {
    GLubyte p[4] = {0, 0, 0, 0};
    fReadPixels(WIN / 2, WIN / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p);
    const int ok = p[0] < 60 && p[1] < 60 && p[2] < 60;
    printf("%s %-46s rgba=(%3u,%3u,%3u,%3u)\n", ok ? "OK  " : "FAIL", what, p[0], p[1], p[2], p[3]);
    if (!ok) ++fails;
}

// The four corners of a viewport-filling quad, in a list with NO Begin/End.
static void record_bare_vertices(GLuint list) {
    fNewList(list, GL_COMPILE);
    fColor4f(0, 1, 0, 1);
    fVertex2f(-1, -1);
    fVertex2f(1, -1);
    fVertex2f(1, 1);
    fVertex2f(-1, 1);
    fEndList();
}

int main(void) {
    void* h = dlopen(WRAPPER_LIB_PATH, RTLD_NOW | RTLD_LOCAL);
    if (!h) { fprintf(stderr, "FAIL: dlopen: %s\n", dlerror()); return 1; }
    resolve = (void* (*)(const char*))dlsym(h, "eglGetProcAddress");
    if (!resolve) { fprintf(stderr, "FAIL: no eglGetProcAddress\n"); return 1; }

    EGLDisplay d = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (d == EGL_NO_DISPLAY || !eglInitialize(d, 0, 0)) { printf("SKIP\n"); return 77; }
    const EGLint cf[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE,
                         EGL_OPENGL_ES3_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
                         EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
    EGLConfig c; EGLint nc = 0;
    if (!eglChooseConfig(d, cf, &c, 1, &nc) || !nc) { printf("SKIP\n"); return 77; }
    const EGLint pb[] = {EGL_WIDTH, WIN, EGL_HEIGHT, WIN, EGL_NONE};
    EGLSurface s = eglCreatePbufferSurface(d, c, pb);
    eglBindAPI(EGL_OPENGL_ES_API);
    const EGLint ca[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    EGLContext x = eglCreateContext(d, c, EGL_NO_CONTEXT, ca);
    if (!eglMakeCurrent(d, s, s, x)) { printf("SKIP\n"); return 77; }

    R(fVertex2f, "glVertex2f"); R(fBegin, "glBegin"); R(fEnd, "glEnd");
    R(fColor4f, "glColor4f"); R(fClearColor, "glClearColor"); R(fClear, "glClear");
    R(fFinish, "glFinish"); R(fReadPixels, "glReadPixels"); R(fGenLists, "glGenLists");
    R(fNewList, "glNewList"); R(fEndList, "glEndList"); R(fCallList, "glCallList");
    R(fDeleteLists, "glDeleteLists"); R(fGetError, "glGetError");

    fClearColor(0, 0, 0, 0);
    (void)fGetError();

    // --- 1. glCallList of a bare-vertex list inside Begin/End -----------
    {
        const GLuint list = fGenLists(1);
        record_bare_vertices(list);
        fClear(GL_COLOR_BUFFER_BIT);
        fBegin(GL_QUADS);
        fCallList(list);
        fEnd();
        fFinish();
        const GLenum err = fGetError();
        if (err != GL_NO_ERROR) {
            printf("FAIL %-46s err=0x%04x\n", "callList(vertices) in Begin/End: no error", err);
            ++fails;
        } else {
            printf("OK   %-46s\n", "callList(vertices) in Begin/End: no error");
        }
        expect_green("callList(vertices) in Begin/End: drew quad");
        fDeleteLists(list, 1);
    }

    // --- 2. the same, nested inside a COMPILE_AND_EXECUTE list ----------
    // The outer list records Begin, the CallList and End; both the immediate
    // execution and the later replay must produce the quad.
    {
        const GLuint inner = fGenLists(1);
        const GLuint outer = fGenLists(1);
        record_bare_vertices(inner);

        fClear(GL_COLOR_BUFFER_BIT);
        fNewList(outer, GL_COMPILE_AND_EXECUTE);
        fBegin(GL_QUADS);
        fCallList(inner);
        fEnd();
        fEndList();
        fFinish();
        expect_green("nested list, immediate execution");

        fClear(GL_COLOR_BUFFER_BIT);
        fCallList(outer);
        fFinish();
        expect_green("nested list, replayed");

        fDeleteLists(inner, 1);
        fDeleteLists(outer, 1);
    }

    // --- 3. Begin, vertices and End in THREE separate lists -------------
    // Upstream expects the glCallList(begin) to raise INVALID_OPERATION
    // (Begin inside Begin/End); this wrapper does raise it, because the
    // nested-Begin check is one of the rules it DOES enforce. Either way the
    // geometry must still be drawn by the outer block.
    {
        const GLuint begin = fGenLists(1), vertex = fGenLists(1), end = fGenLists(1);
        fNewList(begin, GL_COMPILE);
        fBegin(GL_QUADS);
        fEndList();
        record_bare_vertices(vertex);
        fNewList(end, GL_COMPILE);
        fEnd();
        fEndList();
        (void)fGetError();

        fClear(GL_COLOR_BUFFER_BIT);
        fBegin(GL_QUADS);
        fCallList(begin); // nested Begin: error expected, block stays open
        fCallList(vertex);
        fCallList(end);
        fFinish();
        (void)fGetError();
        expect_green("split Begin/vertices/End lists drew quad");

        fDeleteLists(begin, 1);
        fDeleteLists(vertex, 1);
        fDeleteLists(end, 1);
    }

    // --- 4. an invalid glBegin mode inside a list ------------------------
    // glBegin(10000) must raise GL_INVALID_ENUM at record time and must NOT
    // be compiled into the list, so replaying draws nothing.
    {
        const GLuint list = fGenLists(1);
        fClear(GL_COLOR_BUFFER_BIT);
        fNewList(list, GL_COMPILE_AND_EXECUTE);
        fBegin(10000);
        fColor4f(0, 1, 0, 1);
        fVertex2f(-1, -1);
        fVertex2f(1, -1);
        fVertex2f(1, 1);
        fVertex2f(-1, 1);
        fEnd();
        fEndList();
        const GLenum err = fGetError();
        if (err != GL_INVALID_ENUM) {
            printf("FAIL %-46s err=0x%04x want=0x0500\n", "glBegin(bad mode) in list errors", err);
            ++fails;
        } else {
            printf("OK   %-46s\n", "glBegin(bad mode) in list errors");
        }
        fFinish();
        expect_black("glBegin(bad mode) in list drew nothing");

        fClear(GL_COLOR_BUFFER_BIT);
        fCallList(list);
        fFinish();
        (void)fGetError();
        expect_black("replaying that list drew nothing");
        fDeleteLists(list, 1);
    }

    if (fails) {
        fprintf(stderr, "FAIL: %d display-list/Begin-End case(s) mismatched\n", fails);
        return 1;
    }
    printf("PASS: display lists compose with Begin/End correctly\n");
    return 0;
}
