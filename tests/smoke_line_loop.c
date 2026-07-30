// SimpleFPEWrapper - tests/smoke_line_loop.c
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// Ported from piglit's tests/spec/gl-1.0/long-line-loop.c (MIT-style
// license; see piglit's COPYING), whose point is that a GL_LINE_LOOP's
// CLOSING segment - the one from the last vertex back to the first - must be
// drawn, and must stay drawn as the vertex count grows.
//
// Upstream checks this by drawing a many-segment circle as a LINE_LOOP and
// again as a LINE_STRIP with the first vertex repeated, then requiring the
// two renderings to be pixel-identical. This port instead traces a rectangle
// outline (with collinear intermediate vertices to reach any desired vertex
// count) and probes the midpoint of each of the four edges, which is
// resolution-independent and does not depend on line-rasterization tie-break
// rules that differ between drivers.
//
// The vertex counts matter to THIS wrapper specifically. Small runs are
// merged and rewritten into independent GL_LINES by appendMergedRun, which
// has to synthesize the closing segment itself (`emit(count - 1); emit(0)`);
// runs past the merge-size limit skip the expander and reach the backend as
// a real GL_LINE_LOOP. Both paths are covered here, plus the client-array
// path that upstream uses. A closing segment dropped in the expander would
// otherwise only show up as a subtly open outline in an app.

#include <dlfcn.h>
#include <stdio.h>
#include <EGL/egl.h>

typedef unsigned int GLenum, GLbitfield;
typedef unsigned char GLubyte;
typedef float GLfloat;
typedef int GLint, GLsizei;

#define WIN 64
#define GL_LINE_LOOP 0x0002
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_RGBA 0x1908
#define GL_UNSIGNED_BYTE 0x1401
#define GL_FLOAT 0x1406
#define GL_PROJECTION 0x1701
#define GL_MODELVIEW 0x1700
#define GL_VERTEX_ARRAY 0x8074
#define GL_NO_ERROR 0

// The outline the test traces, in window coordinates.
#define X0 8
#define Y0 8
#define X1 56
#define Y1 56

static void* (*resolve)(const char*);
static void (*fVertex2f)(GLfloat, GLfloat);
static void (*fBegin)(GLenum);
static void (*fEnd)(void);
static void (*fColor3f)(GLfloat, GLfloat, GLfloat);
static void (*fClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
static void (*fClear)(GLbitfield);
static void (*fOrtho)(double, double, double, double, double, double);
static void (*fMatrixMode)(GLenum);
static void (*fLoadIdentity)(void);
static void (*fFinish)(void);
static void (*fReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*);
static void (*fVertexPointer)(GLint, GLenum, GLsizei, const void*);
static void (*fEnableClientState)(GLenum);
static void (*fDisableClientState)(GLenum);
static void (*fDrawArrays)(GLenum, GLint, GLsizei);
static GLenum (*fGetError)(void);

#define R(dst, name)                                                                             \
    do {                                                                                         \
        *(void**)&dst = resolve(name);                                                           \
        if (!dst) { fprintf(stderr, "FAIL: cannot resolve %s\n", name); return 1; }               \
    } while (0)

static int fails;

// Walk the rectangle perimeter counter-clockwise, subdividing each side so
// the loop has `perSide * 4` vertices in total. The extra vertices are
// collinear, so the drawn shape is the same rectangle at every count.
static void build_outline(int perSide, GLfloat* out) {
    int n = 0;
    for (int i = 0; i < perSide; ++i) { // bottom, left to right
        const float t = (float)i / (float)perSide;
        out[n++] = X0 + (X1 - X0) * t; out[n++] = Y0 + 0.5f;
    }
    for (int i = 0; i < perSide; ++i) { // right, bottom to top
        const float t = (float)i / (float)perSide;
        out[n++] = X1 - 0.5f; out[n++] = Y0 + (Y1 - Y0) * t;
    }
    for (int i = 0; i < perSide; ++i) { // top, right to left
        const float t = (float)i / (float)perSide;
        out[n++] = X1 - (X1 - X0) * t; out[n++] = Y1 - 0.5f;
    }
    for (int i = 0; i < perSide; ++i) { // left, top to bottom
        const float t = (float)i / (float)perSide;
        out[n++] = X0 + 0.5f; out[n++] = Y1 - (Y1 - Y0) * t;
    }
}

static int lit(int x, int y) {
    GLubyte p[4] = {0, 0, 0, 0};
    fReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p);
    return p[1] > 100;
}

// All four edges must be lit. The LEFT edge is the closing segment for the
// vertex order built above, so it is the one that goes missing if the loop
// is not closed.
static void check_outline(const char* what) {
    const int mx = (X0 + X1) / 2, my = (Y0 + Y1) / 2;
    const struct { int x, y; const char* edge; } probes[] = {
        {mx, Y0, "bottom"}, {X1 - 1, my, "right"}, {mx, Y1 - 1, "top"}, {X0, my, "left (closing)"},
    };
    for (int i = 0; i < 4; ++i) {
        if (lit(probes[i].x, probes[i].y)) continue;
        printf("FAIL %-40s %s edge missing at (%d,%d)\n", what, probes[i].edge, probes[i].x,
               probes[i].y);
        ++fails;
        return;
    }
    // The interior must stay empty: a loop drawn as a filled primitive, or a
    // stray closing triangle, would light it up.
    if (lit(mx, my)) {
        printf("FAIL %-40s interior filled at (%d,%d)\n", what, mx, my);
        ++fails;
        return;
    }
    printf("OK   %-40s all 4 edges drawn, interior empty\n", what);
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
    R(fColor3f, "glColor3f"); R(fClearColor, "glClearColor"); R(fClear, "glClear");
    R(fOrtho, "glOrtho"); R(fMatrixMode, "glMatrixMode"); R(fLoadIdentity, "glLoadIdentity");
    R(fFinish, "glFinish"); R(fReadPixels, "glReadPixels");
    R(fVertexPointer, "glVertexPointer"); R(fEnableClientState, "glEnableClientState");
    R(fDisableClientState, "glDisableClientState"); R(fDrawArrays, "glDrawArrays");
    R(fGetError, "glGetError");

    fMatrixMode(GL_PROJECTION);
    fLoadIdentity();
    fOrtho(0, WIN, 0, WIN, -1, 1);
    fMatrixMode(GL_MODELVIEW);
    fLoadIdentity();
    fClearColor(0, 0, 0, 0);
    fColor3f(0, 1, 0);
    (void)fGetError();

    // 4 verts merges and expands; 256 verts (64 per side) is past the
    // merge-size limit and reaches the backend as a real GL_LINE_LOOP.
    static GLfloat verts[4 * 64 * 2];
    const int perSideCases[] = {1, 4, 16, 64};

    for (int k = 0; k < 4; ++k) {
        const int perSide = perSideCases[k];
        const int count = perSide * 4;
        build_outline(perSide, verts);

        char what[80];

        fClear(GL_COLOR_BUFFER_BIT);
        fBegin(GL_LINE_LOOP);
        for (int i = 0; i < count; ++i) fVertex2f(verts[i * 2], verts[i * 2 + 1]);
        fEnd();
        fFinish();
        snprintf(what, sizeof what, "glBegin LINE_LOOP x%d", count);
        check_outline(what);

        fClear(GL_COLOR_BUFFER_BIT);
        fEnableClientState(GL_VERTEX_ARRAY);
        fVertexPointer(2, GL_FLOAT, 0, verts);
        fDrawArrays(GL_LINE_LOOP, 0, count);
        fDisableClientState(GL_VERTEX_ARRAY);
        fFinish();
        snprintf(what, sizeof what, "glDrawArrays LINE_LOOP x%d", count);
        check_outline(what);
    }

    const GLenum err = fGetError();
    if (err != GL_NO_ERROR) {
        fprintf(stderr, "FAIL: latched GL error 0x%04x\n", err);
        return 1;
    }
    if (fails) {
        fprintf(stderr, "FAIL: %d line-loop case(s) mismatched\n", fails);
        return 1;
    }
    printf("PASS: GL_LINE_LOOP closes on both draw paths at every size\n");
    return 0;
}
