// SimpleFPEWrapper - tests/smoke_polygon_mode.c
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// glPolygonMode(GL_LINE) and glPolygonMode(GL_POINT) emulation, on BOTH draw
// paths and for every primitive glPolygonMode applies to.
//
// GLES has no polygon mode, so the wrapper emulates it: GL_LINE expands each
// filled primitive into the GL_LINES pairs that outline it, GL_POINT draws
// its vertices as GL_POINTS. The client-array path had this from the start;
// the immediate-mode path ignored polygon mode entirely and always drew
// filled until it was added alongside this test, so a glBegin/glEnd
// wireframe silently came out solid.
//
// What each case checks, on a rectangle drawn near the edges of the
// viewport: under GL_LINE the border pixels are lit and the INTERIOR is not
// (that is the whole point - a filled draw lights both); under GL_POINT the
// corners are lit and the edges between them are not.
//
// The quad cases matter beyond their own correctness. Merging small
// immediate runs rewrites quads and fans into independent triangles, which
// is invisible when filled but would draw each quad's DIAGONAL once
// outlined. Drawing several quads in one Begin/End, plus consecutive
// single-quad runs that would otherwise merge, pins that the outline stays
// the application's own.
//
// Per-face polygon modes (front != back) are deliberately not covered:
// splitting a draw by facing needs CPU-side facing tests, so the wrapper
// documents that as a gap and rasterizes filled.

#include <dlfcn.h>
#include <stdio.h>
#include <EGL/egl.h>

typedef unsigned int GLenum, GLbitfield;
typedef unsigned char GLubyte;
typedef float GLfloat;
typedef int GLint, GLsizei;

#define WIN 32
#define GL_TRIANGLES 0x0004
#define GL_TRIANGLE_STRIP 0x0005
#define GL_TRIANGLE_FAN 0x0006
#define GL_QUADS 0x0007
#define GL_QUAD_STRIP 0x0008
#define GL_POLYGON 0x0009
#define GL_FILL 0x1B02
#define GL_LINE 0x1B01
#define GL_POINT 0x1B00
#define GL_FRONT_AND_BACK 0x0408
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_RGBA 0x1908
#define GL_UNSIGNED_BYTE 0x1401
#define GL_FLOAT 0x1406
#define GL_PROJECTION 0x1701
#define GL_MODELVIEW 0x1700
#define GL_VERTEX_ARRAY 0x8074
#define GL_NO_ERROR 0

// The rectangle every case outlines, in window coordinates.
#define X0 6
#define Y0 6
#define X1 26
#define Y1 26

static void* (*resolve)(const char*);
static void (*fVertex2f)(GLfloat, GLfloat);
static void (*fBegin)(GLenum);
static void (*fEnd)(void);
static void (*fColor3f)(GLfloat, GLfloat, GLfloat);
static void (*fClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
static void (*fClear)(GLbitfield);
static void (*fPolygonMode)(GLenum, GLenum);
static void (*fOrtho)(double, double, double, double, double, double);
static void (*fMatrixMode)(GLenum);
static void (*fLoadIdentity)(void);
static void (*fTranslatef)(GLfloat, GLfloat, GLfloat);
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

static int lit(int x, int y) {
    GLubyte p[4] = {0, 0, 0, 0};
    fReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p);
    return p[1] > 100;
}

// GL_LINE: all four borders drawn, interior hollow.
//
// The interior probe sits deliberately OFF both corner-to-corner diagonals.
// The triangle modes decompose the rectangle into two triangles and each
// triangle is its own primitive, so their shared edge - a diagonal across
// the rectangle - is a real part of the wireframe and is drawn. Probing the
// center would therefore be testing the diagonal, not the fill.
static void check_outline(const char* what) {
    const int mx = (X0 + X1) / 2, my = (Y0 + Y1) / 2;
    const int ix = X0 + 4, iy = Y0 + 6; // off y == x and off x + y == X1 + Y0
    const struct { int x, y, want; const char* where; } probes[] = {
        {mx, Y0, 1, "bottom border"},   {mx, Y1, 1, "top border"},
        {X0, my, 1, "left border"},     {X1, my, 1, "right border"},
        {ix, iy, 0, "interior hollow"},
    };
    for (int i = 0; i < 5; ++i) {
        if (lit(probes[i].x, probes[i].y) == probes[i].want) continue;
        printf("FAIL %-38s %s wrong at (%d,%d)\n", what, probes[i].where, probes[i].x,
               probes[i].y);
        ++fails;
        return;
    }
    printf("OK   %-38s outlined, interior hollow\n", what);
}

// GL_POINT: the corner vertices are drawn, the spans between them are not.
static void check_points(const char* what) {
    const int mx = (X0 + X1) / 2, my = (Y0 + Y1) / 2;
    if (!lit(X0, Y0)) {
        printf("FAIL %-38s corner vertex missing at (%d,%d)\n", what, X0, Y0);
        ++fails;
        return;
    }
    if (lit(mx, Y0) || lit(mx, my)) {
        printf("FAIL %-38s edge or interior lit, expected points only\n", what);
        ++fails;
        return;
    }
    printf("OK   %-38s corner points only\n", what);
}

// Vertex lists tracing the same rectangle for each primitive mode. Counts
// are chosen so every mode covers the identical area.
static const GLfloat quad[] = {X0, Y0, X1, Y0, X1, Y1, X0, Y1};
static const GLfloat tris[] = {X0, Y0, X1, Y0, X1, Y1, X1, Y1, X0, Y1, X0, Y0};
static const GLfloat strip[] = {X0, Y0, X1, Y0, X0, Y1, X1, Y1};

static void draw_immediate(GLenum prim, const GLfloat* v, int n) {
    fBegin(prim);
    for (int i = 0; i < n; ++i) fVertex2f(v[i * 2], v[i * 2 + 1]);
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
    R(fColor3f, "glColor3f"); R(fClearColor, "glClearColor"); R(fClear, "glClear");
    R(fPolygonMode, "glPolygonMode"); R(fOrtho, "glOrtho"); R(fMatrixMode, "glMatrixMode");
    R(fLoadIdentity, "glLoadIdentity"); R(fTranslatef, "glTranslatef");
    R(fFinish, "glFinish"); R(fReadPixels, "glReadPixels");
    R(fVertexPointer, "glVertexPointer"); R(fEnableClientState, "glEnableClientState");
    R(fDisableClientState, "glDisableClientState"); R(fDrawArrays, "glDrawArrays");
    R(fGetError, "glGetError");

    fMatrixMode(GL_PROJECTION);
    fLoadIdentity();
    fOrtho(0, WIN, 0, WIN, -1, 1);
    fMatrixMode(GL_MODELVIEW);
    fLoadIdentity();
    // Integer window coordinates land exactly on pixel CORNERS, where which
    // pixel a line or point claims is a tie. The usual pixel-perfect-2D
    // nudge puts them unambiguously inside the intended pixel, so a border
    // at X0 really is column X0 (same bias tests/smoke_orthpos.c uses).
    fTranslatef(0.375f, 0.375f, 0.0f);
    fClearColor(0, 0, 0, 0);
    fColor3f(0, 1, 0);
    (void)fGetError();

    const struct { GLenum prim; const GLfloat* v; int n; const char* name; } cases[] = {
        {GL_QUADS, quad, 4, "QUADS"},
        {GL_POLYGON, quad, 4, "POLYGON"},
        {GL_TRIANGLE_FAN, quad, 4, "TRIANGLE_FAN"},
        {GL_TRIANGLES, tris, 6, "TRIANGLES"},
        {GL_TRIANGLE_STRIP, strip, 4, "TRIANGLE_STRIP"},
        {GL_QUAD_STRIP, strip, 4, "QUAD_STRIP"},
    };
    const int ncases = (int)(sizeof(cases) / sizeof(cases[0]));

    // --- GL_LINE ---------------------------------------------------------
    fPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    for (int i = 0; i < ncases; ++i) {
        char what[80];

        fClear(GL_COLOR_BUFFER_BIT);
        draw_immediate(cases[i].prim, cases[i].v, cases[i].n);
        fFinish();
        snprintf(what, sizeof what, "LINE  glBegin %s", cases[i].name);
        check_outline(what);

        fClear(GL_COLOR_BUFFER_BIT);
        fEnableClientState(GL_VERTEX_ARRAY);
        fVertexPointer(2, GL_FLOAT, 0, cases[i].v);
        fDrawArrays(cases[i].prim, 0, cases[i].n);
        fDisableClientState(GL_VERTEX_ARRAY);
        fFinish();
        snprintf(what, sizeof what, "LINE  glDrawArrays %s", cases[i].name);
        check_outline(what);
    }

    // Two quads in ONE Begin/End: the outline must be per-quad, with no
    // diagonal from a triangle rewrite.
    {
        fClear(GL_COLOR_BUFFER_BIT);
        fBegin(GL_QUADS);
        for (int i = 0; i < 4; ++i) fVertex2f(quad[i * 2], quad[i * 2 + 1]);
        // A second, degenerate-but-offscreen quad so the run holds two.
        for (int i = 0; i < 4; ++i) fVertex2f(quad[i * 2] + WIN, quad[i * 2 + 1]);
        fEnd();
        fFinish();
        check_outline("LINE  two QUADS in one Begin/End");
    }

    // Consecutive single-quad runs: these are exactly what the run merger
    // would fuse into triangles if it were still merging under GL_LINE.
    {
        fClear(GL_COLOR_BUFFER_BIT);
        draw_immediate(GL_QUADS, quad, 4);
        draw_immediate(GL_QUADS, quad, 4);
        fFinish();
        check_outline("LINE  two consecutive QUADS runs");
    }

    // --- GL_POINT --------------------------------------------------------
    fPolygonMode(GL_FRONT_AND_BACK, GL_POINT);
    for (int i = 0; i < ncases; ++i) {
        char what[80];

        fClear(GL_COLOR_BUFFER_BIT);
        draw_immediate(cases[i].prim, cases[i].v, cases[i].n);
        fFinish();
        snprintf(what, sizeof what, "POINT glBegin %s", cases[i].name);
        check_points(what);

        fClear(GL_COLOR_BUFFER_BIT);
        fEnableClientState(GL_VERTEX_ARRAY);
        fVertexPointer(2, GL_FLOAT, 0, cases[i].v);
        fDrawArrays(cases[i].prim, 0, cases[i].n);
        fDisableClientState(GL_VERTEX_ARRAY);
        fFinish();
        snprintf(what, sizeof what, "POINT glDrawArrays %s", cases[i].name);
        check_points(what);
    }

    // --- back to GL_FILL --------------------------------------------------
    // Restoring the mode has to take effect immediately, including for a run
    // that would have been held back from merging a moment earlier.
    fPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    fClear(GL_COLOR_BUFFER_BIT);
    draw_immediate(GL_QUADS, quad, 4);
    fFinish();
    {
        const int mx = (X0 + X1) / 2, my = (Y0 + Y1) / 2;
        if (!lit(mx, my)) {
            printf("FAIL %-38s interior not filled after restoring GL_FILL\n", "FILL  glBegin QUADS");
            ++fails;
        } else {
            printf("OK   %-38s interior filled again\n", "FILL  glBegin QUADS");
        }
    }

    const GLenum err = fGetError();
    if (err != GL_NO_ERROR) {
        fprintf(stderr, "FAIL: latched GL error 0x%04x\n", err);
        return 1;
    }
    if (fails) {
        fprintf(stderr, "FAIL: %d polygon-mode case(s) mismatched\n", fails);
        return 1;
    }
    printf("PASS: GL_LINE and GL_POINT polygon modes work on both draw paths\n");
    return 0;
}
