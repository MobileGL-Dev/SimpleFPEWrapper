// SimpleFPEWrapper - tests/smoke_orthpos.c
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// Ported from piglit's tests/spec/gl-1.0/orthpos.c (MIT-style license; see
// piglit's COPYING), covering its immediate-mode cases: points, vertical
// lines, horizontal lines and 1x1 quads.
//
// Under an orthographic projection an application can address individual
// pixels, and OpenGL 1.x apps rely on that heavily for 2D drawing. The test
// tiles a square region one primitive per pixel, alternating two colors,
// with blending on. Reading it back then distinguishes three distinct
// failure modes at once:
//
//   - a GAP (background pixel inside the region) means a primitive was
//     rasterized to the wrong pixel or dropped;
//   - an OVERLAP (a pixel blended twice, so brighter than a single 0.5-alpha
//     draw) means two primitives claimed the same pixel;
//   - a BAD EDGE (an unfilled pixel on the region border, or a filled pixel
//     just outside it) means the transform chain has an off-by-half bias.
//
// For this wrapper that exercises the whole fixed-function transform chain
// it synthesizes: glOrtho building the projection matrix, the generated
// vertex shader applying it, and the viewport transform - any half-pixel
// error anywhere in it shows up here and nowhere else in the suite.
//
// Line cases follow OpenGL's diamond-exit rule the same way upstream does:
// the terminal vertex is specified one pixel past the last pixel that should
// be lit, so the lines are treated as half-open.

#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <EGL/egl.h>

typedef unsigned int GLenum, GLbitfield;
typedef unsigned char GLubyte;
typedef float GLfloat;
typedef int GLint, GLsizei;

#define WIN 64
#define DRAW 32 // the tiled region is [1, DRAW] on both axes
#define GL_POINTS 0x0000
#define GL_LINES 0x0001
#define GL_QUADS 0x0007
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_RGBA 0x1908
#define GL_UNSIGNED_BYTE 0x1401
#define GL_PROJECTION 0x1701
#define GL_MODELVIEW 0x1700
#define GL_BLEND 0x0BE2
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_NO_ERROR 0

static void* (*resolve)(const char*);
static void (*fVertex2i)(GLint, GLint);
static void (*fBegin)(GLenum);
static void (*fEnd)(void);
static void (*fColor4f)(GLfloat, GLfloat, GLfloat, GLfloat);
static void (*fClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
static void (*fClear)(GLbitfield);
static void (*fOrtho)(double, double, double, double, double, double);
static void (*fMatrixMode)(GLenum);
static void (*fLoadIdentity)(void);
static void (*fTranslatef)(GLfloat, GLfloat, GLfloat);
static void (*fViewport)(GLint, GLint, GLsizei, GLsizei);
static void (*fEnable)(GLenum);
static void (*fDisable)(GLenum);
static void (*fBlendFunc)(GLenum, GLenum);
static void (*fFinish)(void);
static void (*fReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*);
static GLenum (*fGetError)(void);

#define R(dst, name)                                                                             \
    do {                                                                                         \
        *(void**)&dst = resolve(name);                                                           \
        if (!dst) { fprintf(stderr, "FAIL: cannot resolve %s\n", name); return 1; }               \
    } while (0)

static int fails;
static GLubyte img[WIN * WIN * 4];

// One 0.5-alpha draw over black lands near 127; two land near 191. Classify
// with a wide band so this stays about geometry, not blend precision.
enum pixel_kind { PX_BACKGROUND, PX_GREEN, PX_RED, PX_OVERDRAWN, PX_OTHER };

static enum pixel_kind classify(int x, int y) {
    const GLubyte* p = &img[(y * WIN + x) * 4];
    const int r = p[0], g = p[1], b = p[2];
    if (r < 40 && g < 40 && b < 40) return PX_BACKGROUND;
    if (b >= 40) return PX_OTHER;
    if (r >= 40 && g >= 40) return PX_OTHER;      // both channels: two colors mixed
    const int v = r >= 40 ? r : g;
    if (v > 160) return PX_OVERDRAWN;             // blended more than once
    if (v < 90) return PX_OTHER;                  // too dark for a full draw
    return r >= 40 ? PX_RED : PX_GREEN;
}

// Every pixel of [1, DRAW]^2 must be filled exactly once; everything outside
// must be untouched.
static void verify(const char* what) {
    fReadPixels(0, 0, WIN, WIN, GL_RGBA, GL_UNSIGNED_BYTE, img);
    int gaps = 0, overlaps = 0, other = 0, bad_outside = 0;
    int gx = -1, gy = -1, ox = -1, oy = -1, bx = -1, by = -1;

    for (int y = 0; y < WIN; ++y) {
        for (int x = 0; x < WIN; ++x) {
            const enum pixel_kind k = classify(x, y);
            const int inside = x >= 1 && x <= DRAW && y >= 1 && y <= DRAW;
            if (inside) {
                if (k == PX_BACKGROUND) {
                    if (gaps++ == 0) { gx = x; gy = y; }
                } else if (k == PX_OVERDRAWN) {
                    if (overlaps++ == 0) { ox = x; oy = y; }
                } else if (k == PX_OTHER) {
                    ++other;
                }
            } else if (k != PX_BACKGROUND) {
                if (bad_outside++ == 0) { bx = x; by = y; }
            }
        }
    }

    if (!gaps && !overlaps && !other && !bad_outside) {
        printf("OK   %-26s %dx%d region tiled exactly\n", what, DRAW, DRAW);
        return;
    }
    printf("FAIL %-26s", what);
    if (gaps) printf(" gaps=%d(first %d,%d)", gaps, gx, gy);
    if (overlaps) printf(" overlaps=%d(first %d,%d)", overlaps, ox, oy);
    if (other) printf(" unclassified=%d", other);
    if (bad_outside) printf(" outside=%d(first %d,%d)", bad_outside, bx, by);
    printf("\n");
    ++fails;
}

static void set_color(int alternate) {
    if (alternate) fColor4f(0.0f, 1.0f, 0.0f, 0.5f);
    else fColor4f(1.0f, 0.0f, 0.0f, 0.5f);
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

    R(fVertex2i, "glVertex2i"); R(fBegin, "glBegin"); R(fEnd, "glEnd");
    R(fColor4f, "glColor4f"); R(fClearColor, "glClearColor"); R(fClear, "glClear");
    R(fOrtho, "glOrtho"); R(fMatrixMode, "glMatrixMode"); R(fLoadIdentity, "glLoadIdentity");
    R(fTranslatef, "glTranslatef");
    R(fViewport, "glViewport"); R(fEnable, "glEnable"); R(fDisable, "glDisable");
    R(fBlendFunc, "glBlendFunc"); R(fFinish, "glFinish"); R(fReadPixels, "glReadPixels");
    R(fGetError, "glGetError");

    fViewport(0, 0, WIN, WIN);
    fMatrixMode(GL_PROJECTION);
    fLoadIdentity();
    fOrtho(0, WIN, 0, WIN, -1, 1);
    fMatrixMode(GL_MODELVIEW);
    fLoadIdentity();
    // The classic pixel-perfect-2D bias, straight from upstream: integer
    // vertex coordinates land exactly on pixel CORNERS, where which pixel a
    // point or line claims is a tie. Nudging by 0.375 puts them safely
    // inside one pixel without changing which one. Without it every case
    // here is ambiguous by construction and the results say nothing about
    // the transform chain.
    fTranslatef(0.375f, 0.375f, 0.0f);
    fClearColor(0, 0, 0, 0);
    fBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    fEnable(GL_BLEND);
    (void)fGetError();

    // --- unit points ----------------------------------------------------
    fClear(GL_COLOR_BUFFER_BIT);
    fBegin(GL_POINTS);
    for (int px = 1; px <= DRAW; ++px)
        for (int py = 1; py <= DRAW; ++py) {
            set_color((px ^ py) & 1);
            fVertex2i(px, py);
        }
    fEnd();
    fFinish();
    verify("immediate points");

    // --- vertical lines (half-open, terminal vertex one past the end) ---
    fClear(GL_COLOR_BUFFER_BIT);
    fBegin(GL_LINES);
    for (int px = 1; px <= DRAW; ++px) {
        set_color(px & 1);
        fVertex2i(px, 1);
        fVertex2i(px, DRAW + 1);
    }
    fEnd();
    fFinish();
    verify("immediate vlines");

    // --- horizontal lines ------------------------------------------------
    fClear(GL_COLOR_BUFFER_BIT);
    fBegin(GL_LINES);
    for (int py = 1; py <= DRAW; ++py) {
        set_color(py & 1);
        fVertex2i(1, py);
        fVertex2i(DRAW + 1, py);
    }
    fEnd();
    fFinish();
    verify("immediate hlines");

    // --- 1x1 quads --------------------------------------------------------
    fClear(GL_COLOR_BUFFER_BIT);
    fBegin(GL_QUADS);
    for (int px = 1; px <= DRAW; ++px)
        for (int py = 1; py <= DRAW; ++py) {
            set_color((px ^ py) & 1);
            fVertex2i(px, py);
            fVertex2i(px + 1, py);
            fVertex2i(px + 1, py + 1);
            fVertex2i(px, py + 1);
        }
    fEnd();
    fFinish();
    verify("immediate 1x1 quads");

    fDisable(GL_BLEND);
    const GLenum err = fGetError();
    if (err != GL_NO_ERROR) {
        fprintf(stderr, "FAIL: latched GL error 0x%04x\n", err);
        return 1;
    }
    if (fails) {
        fprintf(stderr, "FAIL: %d orthographic positioning case(s) mismatched\n", fails);
        return 1;
    }
    printf("PASS: immediate-mode primitives land on exact pixels under glOrtho\n");
    return 0;
}
