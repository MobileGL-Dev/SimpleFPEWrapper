// SimpleFPEWrapper - tests/smoke_edgeflag.c
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// Ported from piglit's tests/spec/gl-1.0/edgeflag.c, edgeflag-quads.c and
// edgeflag-const.c (MIT-style license; see piglit's COPYING), merged into
// one file because all three fail on the same single missing feature.
//
// In GL_LINE polygon mode only edges whose leading vertex had
// glEdgeFlag(GL_TRUE) are drawn. The three cases are: a GL_POLYGON with its
// verticals flagged off, two GL_QUADS in one Begin/End with the same
// pattern (upstream's note: some hardware cannot do per-vertex edge flags on
// quad lists, so they must be split before submission), and a constant
// glEdgeFlag set OUTSIDE Begin/End applying to a whole primitive.
//
// How the wrapper honours these: advance() collects per-vertex edge flags
// into a lazily-populated array parallel to the interleaved vertex stream
// (empty = "all boundary", the GL default, so runs that never clear the
// flag pay one compare per vertex and no allocation), and the shared
// wireframe expansion (sfpewBuildWireframeIndices) drops every edge whose
// LEADING vertex has the flag cleared - GL's rule: the flag current when a
// vertex is specified controls the edge that begins at it. A primitive
// whose edges are ALL suppressed draws nothing, which the callers
// distinguish from an index-upload failure (that falls back to filled).
//
// This test began life as the port's xfail (WILL_FAIL) documenting the gap;
// the GL_LINE emulation and the edge-flag plumbing landed in that order,
// un-xfailing it exactly as the original file header demanded.

#include <dlfcn.h>
#include <stdio.h>
#include <EGL/egl.h>

typedef unsigned int GLenum, GLbitfield;
typedef unsigned char GLubyte, GLboolean;
typedef float GLfloat;
typedef int GLint, GLsizei;

#define WIN 32
#define GL_QUADS 0x0007
#define GL_POLYGON 0x0009
#define GL_LINE 0x1B01
#define GL_FRONT_AND_BACK 0x0408
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_RGBA 0x1908
#define GL_UNSIGNED_BYTE 0x1401
#define GL_PROJECTION 0x1701
#define GL_MODELVIEW 0x1700
#define GL_TRUE 1
#define GL_FALSE 0

static void* (*resolve)(const char*);
static void (*fVertex2f)(GLfloat, GLfloat);
static void (*fBegin)(GLenum);
static void (*fEnd)(void);
static void (*fEdgeFlag)(GLboolean);
static void (*fColor4f)(GLfloat, GLfloat, GLfloat, GLfloat);
static void (*fClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
static void (*fClear)(GLbitfield);
static void (*fPolygonMode)(GLenum, GLenum);
static void (*fOrtho)(double, double, double, double, double, double);
static void (*fMatrixMode)(GLenum);
static void (*fLoadIdentity)(void);
static void (*fFinish)(void);
static void (*fFlush)(void);
static void (*fReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*);

#define R(dst, name)                                                                             \
    do {                                                                                         \
        *(void**)&dst = resolve(name);                                                           \
        if (!dst) { fprintf(stderr, "FAIL: cannot resolve %s\n", name); return 1; }               \
    } while (0)

static int fails;
static void probe(int x, int y, int want_lit, const char* what) {
    GLubyte p[4] = {0, 0, 0, 0};
    fReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p);
    const int lit = p[1] > 150; // green channel
    printf("%s %-34s (%2d,%2d) lit=%d want=%d\n", lit == want_lit ? "OK  " : "FAIL", what, x, y,
           lit, want_lit);
    if (lit != want_lit) fails++;
}

// One axis-aligned rectangle with the horizontal edges flagged TRUE
// (boundary, must draw) and the verticals FALSE (must be suppressed).
static void flagged_rect(GLenum prim, float x0, float y0, float x1, float y1) {
    fBegin(prim);
    fEdgeFlag(GL_TRUE);
    fVertex2f(x0, y0);
    fEdgeFlag(GL_FALSE);
    fVertex2f(x1, y0);
    fEdgeFlag(GL_TRUE);
    fVertex2f(x1, y1);
    fEdgeFlag(GL_FALSE);
    fVertex2f(x0, y1);
    fEnd();
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
                         EGL_BLUE_SIZE, 8, EGL_NONE};
    EGLConfig c; EGLint nc = 0;
    if (!eglChooseConfig(d, cf, &c, 1, &nc) || !nc) { printf("SKIP\n"); return 77; }
    const EGLint pb[] = {EGL_WIDTH, WIN, EGL_HEIGHT, WIN, EGL_NONE};
    EGLSurface s = eglCreatePbufferSurface(d, c, pb);
    eglBindAPI(EGL_OPENGL_ES_API);
    const EGLint ca[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    EGLContext x = eglCreateContext(d, c, EGL_NO_CONTEXT, ca);
    if (!eglMakeCurrent(d, s, s, x)) { printf("SKIP\n"); return 77; }

    R(fVertex2f, "glVertex2f"); R(fBegin, "glBegin"); R(fEnd, "glEnd");
    R(fEdgeFlag, "glEdgeFlag"); R(fColor4f, "glColor4f"); R(fClearColor, "glClearColor");
    R(fClear, "glClear"); R(fPolygonMode, "glPolygonMode"); R(fOrtho, "glOrtho");
    R(fMatrixMode, "glMatrixMode"); R(fLoadIdentity, "glLoadIdentity"); R(fFinish, "glFinish");
    R(fFlush, "glFlush"); R(fReadPixels, "glReadPixels");

    fMatrixMode(GL_PROJECTION);
    fLoadIdentity();
    fOrtho(0, WIN, 0, WIN, -1, 1);
    fMatrixMode(GL_MODELVIEW);
    fLoadIdentity();
    fPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    fClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    fColor4f(0, 1, 0, 0);

    // --- edgeflag.c: one GL_POLYGON, verticals suppressed ---------------
    fClear(GL_COLOR_BUFFER_BIT);
    flagged_rect(GL_POLYGON, 1.5f, 1.5f, 5.5f, 5.5f);
    fFinish();
    probe(3, 1, 1, "polygon: bottom edge drawn");
    probe(3, 5, 1, "polygon: top edge drawn");
    probe(1, 3, 0, "polygon: left edge suppressed");
    probe(5, 3, 0, "polygon: right edge suppressed");

    // --- edgeflag-quads.c: two GL_QUADS in ONE Begin/End ----------------
    fClear(GL_COLOR_BUFFER_BIT);
    fBegin(GL_QUADS);
    fEdgeFlag(GL_TRUE);  fVertex2f(1.5f, 1.5f);
    fEdgeFlag(GL_FALSE); fVertex2f(5.5f, 1.5f);
    fEdgeFlag(GL_TRUE);  fVertex2f(5.5f, 5.5f);
    fEdgeFlag(GL_FALSE); fVertex2f(1.5f, 5.5f);
    fEdgeFlag(GL_TRUE);  fVertex2f(11.5f, 1.5f);
    fEdgeFlag(GL_FALSE); fVertex2f(15.5f, 1.5f);
    fEdgeFlag(GL_TRUE);  fVertex2f(15.5f, 5.5f);
    fEdgeFlag(GL_FALSE); fVertex2f(11.5f, 5.5f);
    fEnd();
    fFinish();
    probe(3, 1, 1, "quads[0]: bottom edge drawn");
    probe(3, 5, 1, "quads[0]: top edge drawn");
    probe(1, 3, 0, "quads[0]: left edge suppressed");
    probe(5, 3, 0, "quads[0]: right edge suppressed");
    probe(13, 1, 1, "quads[1]: bottom edge drawn");
    probe(13, 5, 1, "quads[1]: top edge drawn");
    probe(11, 3, 0, "quads[1]: left edge suppressed");
    probe(15, 3, 0, "quads[1]: right edge suppressed");

    // --- edgeflag-const.c: flag set OUTSIDE Begin/End -------------------
    // TRUE before the first primitive: every edge draws. FALSE before the
    // second: none does. The glFlush between them is upstream's way of
    // stopping the two runs from merging into one per-vertex-attributed
    // batch - which is exactly what this wrapper's run merging would do.
    fClear(GL_COLOR_BUFFER_BIT);
    fEdgeFlag(GL_TRUE);
    fBegin(GL_POLYGON);
    fVertex2f(1.5f, 1.5f); fVertex2f(5.5f, 1.5f);
    fVertex2f(5.5f, 5.5f); fVertex2f(1.5f, 5.5f);
    fEnd();
    fFlush();
    fEdgeFlag(GL_FALSE);
    fBegin(GL_POLYGON);
    fVertex2f(11.5f, 1.5f); fVertex2f(15.5f, 1.5f);
    fVertex2f(15.5f, 5.5f); fVertex2f(11.5f, 5.5f);
    fEnd();
    fFinish();
    probe(3, 1, 1, "const TRUE: bottom edge drawn");
    probe(3, 5, 1, "const TRUE: top edge drawn");
    probe(1, 3, 1, "const TRUE: left edge drawn");
    probe(5, 3, 1, "const TRUE: right edge drawn");
    probe(13, 1, 0, "const FALSE: bottom suppressed");
    probe(13, 5, 0, "const FALSE: top suppressed");
    probe(11, 3, 0, "const FALSE: left suppressed");
    probe(15, 3, 0, "const FALSE: right suppressed");

    if (fails) {
        fprintf(stderr, "FAIL: %d probe(s) mismatched\n", fails);
        return 1;
    }
    printf("PASS: glEdgeFlag drives GL_LINE polygon-mode edge suppression\n");
    return 0;
}
