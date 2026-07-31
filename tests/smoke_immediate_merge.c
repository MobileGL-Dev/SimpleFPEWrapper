// SimpleFPEWrapper - tests/smoke_immediate_merge.c
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// Consecutive small glBegin/glEnd runs are merged into a single draw call
// (plans/12). That rewrites strips, fans and quads into independent
// primitives and delays the draw until some other entry point flushes it, so
// this test pins down what must stay observable:
//   A. every mergeable primitive still covers the pixels it covered alone,
//   B. per-run colors survive the merge (run N does not take run N+1's color),
//   C. a matrix change between two runs is NOT absorbed by the merge,
//   D. a lone run still lands (nothing is left pending at glReadPixels).
// Skips (77) when the machine has no EGL device.

#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

#include <EGL/egl.h>

typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef unsigned char GLubyte;
typedef float GLfloat;
typedef int GLint, GLsizei;
typedef unsigned int GLbitfield;

#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_POINTS 0x0000
#define GL_LINES 0x0001
#define GL_LINE_LOOP 0x0002
#define GL_LINE_STRIP 0x0003
#define GL_TRIANGLES 0x0004
#define GL_TRIANGLE_STRIP 0x0005
#define GL_TRIANGLE_FAN 0x0006
#define GL_QUADS 0x0007
#define GL_QUAD_STRIP 0x0008
#define GL_POLYGON 0x0009
#define GL_RGBA 0x1908
#define GL_UNSIGNED_BYTE 0x1401
#define GL_MODELVIEW 0x1700

#define FB 64

static void (*fClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
static void (*fClear)(GLbitfield);
static void (*fBegin)(GLenum);
static void (*fEnd)(void);
static void (*fColor3f)(GLfloat, GLfloat, GLfloat);
static void (*fVertex2f)(GLfloat, GLfloat);
static void (*fReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*);
static GLenum (*fGetError)(void);
static void (*fFinish)(void);
static void (*fMatrixMode)(GLenum);
static void (*fLoadIdentity)(void);
static void (*fTranslatef)(GLfloat, GLfloat, GLfloat);

static int failures;

// One 0.8x0.8 NDC box centered at (cx, 0), expressed in whichever primitive is
// asked for. Every form covers the box's center pixel, so a correct expansion
// is observable no matter which rewrite path the merger took.
static void box(GLenum primitive, GLfloat cx, GLfloat r, GLfloat g, GLfloat b) {
    const GLfloat l = cx - 0.4f, rt = cx + 0.4f, bt = -0.4f, tp = 0.4f;
    fBegin(primitive);
    fColor3f(r, g, b);
    switch (primitive) {
    case GL_TRIANGLES:
        // Two independent triangles covering the box.
        fVertex2f(l, bt);
        fVertex2f(rt, bt);
        fVertex2f(rt, tp);
        fVertex2f(rt, tp);
        fVertex2f(l, tp);
        fVertex2f(l, bt);
        break;
    case GL_TRIANGLE_STRIP:
        // Strip order: bl, br, tl, tr.
        fVertex2f(l, bt);
        fVertex2f(rt, bt);
        fVertex2f(l, tp);
        fVertex2f(rt, tp);
        break;
    case GL_QUAD_STRIP:
        // Quad-strip order: bl, tl, br, tr forms one quad.
        fVertex2f(l, bt);
        fVertex2f(l, tp);
        fVertex2f(rt, bt);
        fVertex2f(rt, tp);
        break;
    default:
        // QUADS, POLYGON and TRIANGLE_FAN all take the perimeter in order.
        fVertex2f(l, bt);
        fVertex2f(rt, bt);
        fVertex2f(rt, tp);
        fVertex2f(l, tp);
        break;
    }
    fEnd();
}

// A run long enough that the merger declines it (> the vertex limit), so it
// draws directly. Covers the whole framebuffer.
static void big_quad(GLfloat r, GLfloat g, GLfloat b) {
    fBegin(GL_QUADS);
    fColor3f(r, g, b);
    for (int i = 0; i < 40; ++i) {
        // 40 stacked full-width strips: 160 vertices, past the merge limit.
        const GLfloat y0 = -1.0f + (GLfloat)i * 0.05f;
        const GLfloat y1 = y0 + 0.05f;
        fVertex2f(-1.0f, y0);
        fVertex2f(1.0f, y0);
        fVertex2f(1.0f, y1);
        fVertex2f(-1.0f, y1);
    }
    fEnd();
}

// Reads one pixel and checks it against an expected RGB triple (tolerant: the
// point of the test is which primitive covered the pixel, not exact shading).
static int pixel_is(int x, int y, int r, int g, int b, const char* what) {
    GLubyte p[4] = {0, 0, 0, 0};
    fReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p);
    const int ok = (r < 0 || (p[0] > 200) == (r > 0)) && (g < 0 || (p[1] > 200) == (g > 0)) &&
                   (b < 0 || (p[2] > 200) == (b > 0));
    if (!ok) {
        fprintf(stderr, "FAIL: %s: pixel(%d,%d) = (%u,%u,%u), expected (%d,%d,%d)\n", what, x, y,
                p[0], p[1], p[2], r, g, b);
        ++failures;
    }
    return ok;
}

int main(void) {
    void* handle = dlopen(WRAPPER_LIB_PATH, RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        fprintf(stderr, "FAIL: dlopen: %s\n", dlerror());
        return 1;
    }
    typedef void* (*resolver_t)(const char*);
    resolver_t resolve = (resolver_t)dlsym(handle, "eglGetProcAddress");
    if (!resolve) return 1;

    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display == EGL_NO_DISPLAY || !eglInitialize(display, NULL, NULL)) {
        printf("SKIP: no EGL display\n");
        return 77;
    }
    static const EGLint config_attribs[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE,
                                            EGL_OPENGL_ES3_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
                                            EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
    EGLConfig config;
    EGLint num_config = 0;
    if (!eglChooseConfig(display, config_attribs, &config, 1, &num_config) || num_config == 0) {
        printf("SKIP: no ES3 pbuffer config\n");
        return 77;
    }
    static const EGLint pbuffer_attribs[] = {EGL_WIDTH, FB, EGL_HEIGHT, FB, EGL_NONE};
    EGLSurface surface = eglCreatePbufferSurface(display, config, pbuffer_attribs);
    eglBindAPI(EGL_OPENGL_ES_API);
    static const EGLint ctx_attribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, ctx_attribs);
    if (surface == EGL_NO_SURFACE || context == EGL_NO_CONTEXT ||
        !eglMakeCurrent(display, surface, surface, context)) {
        printf("SKIP: could not make an ES3 pbuffer context current\n");
        return 77;
    }

    fClearColor = (void (*)(GLfloat, GLfloat, GLfloat, GLfloat))resolve("glClearColor");
    fClear = (void (*)(GLbitfield))resolve("glClear");
    fBegin = (void (*)(GLenum))resolve("glBegin");
    fEnd = (void (*)(void))resolve("glEnd");
    fColor3f = (void (*)(GLfloat, GLfloat, GLfloat))resolve("glColor3f");
    fVertex2f = (void (*)(GLfloat, GLfloat))resolve("glVertex2f");
    fReadPixels = (void (*)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*))resolve("glReadPixels");
    fGetError = (GLenum(*)(void))resolve("glGetError");
    fFinish = (void (*)(void))resolve("glFinish");
    fMatrixMode = (void (*)(GLenum))resolve("glMatrixMode");
    fLoadIdentity = (void (*)(void))resolve("glLoadIdentity");
    fTranslatef = (void (*)(GLfloat, GLfloat, GLfloat))resolve("glTranslatef");
    if (!fClearColor || !fClear || !fBegin || !fEnd || !fColor3f || !fVertex2f || !fReadPixels ||
        !fGetError || !fFinish || !fMatrixMode || !fLoadIdentity || !fTranslatef) {
        fprintf(stderr, "FAIL: resolver missing entry points\n");
        return 1;
    }

    fMatrixMode(GL_MODELVIEW);
    fLoadIdentity();

    // --- A: two GL_QUADS runs, disjoint on screen, different colors. Merged
    // into one draw, each run must keep its own color and its own pixels.
    fClearColor(0.0f, 0.0f, 1.0f, 1.0f);
    fClear(GL_COLOR_BUFFER_BIT);
    box(GL_QUADS, -0.5f, 1.0f, 0.0f, 0.0f);
    box(GL_QUADS, 0.5f, 0.0f, 1.0f, 0.0f);
    fFinish();
    pixel_is(16, 32, 1, 0, 0, "A: left quad of a merged pair");
    pixel_is(48, 32, 0, 1, 0, "A: right quad of a merged pair");
    pixel_is(32, 4, 0, 0, 1, "A: gap between the merged quads stays cleared");

    // --- B: the same for every other mergeable primitive. Strips, fans and
    // quad strips lose their implicit connectivity when rewritten, so a wrong
    // expansion shows up as a missing or misplaced region.
    static const GLenum mergeable[] = {GL_TRIANGLE_STRIP, GL_TRIANGLE_FAN, GL_QUAD_STRIP,
                                       GL_POLYGON, GL_TRIANGLES};
    static const char* names[] = {"TRIANGLE_STRIP", "TRIANGLE_FAN", "QUAD_STRIP", "POLYGON",
                                  "TRIANGLES"};
    for (unsigned i = 0; i < sizeof(mergeable) / sizeof(mergeable[0]); ++i) {
        char label[64];
        fClearColor(0.0f, 0.0f, 1.0f, 1.0f);
        fClear(GL_COLOR_BUFFER_BIT);
        box(mergeable[i], -0.5f, 1.0f, 0.0f, 0.0f);
        box(mergeable[i], 0.5f, 0.0f, 1.0f, 0.0f);
        fFinish();
        snprintf(label, sizeof(label), "B: %s left run", names[i]);
        pixel_is(16, 32, 1, 0, 0, label);
        snprintf(label, sizeof(label), "B: %s right run", names[i]);
        pixel_is(48, 32, 0, 1, 0, label);
    }

    // --- C: a matrix change between two runs must NOT be absorbed. Both runs
    // use the same local coordinates; only the second is translated. Merging
    // them would draw both at the translated place and leave the first's
    // pixels cleared.
    fClearColor(0.0f, 0.0f, 1.0f, 1.0f);
    fClear(GL_COLOR_BUFFER_BIT);
    fLoadIdentity();
    box(GL_QUADS, -0.5f, 1.0f, 0.0f, 0.0f);
    fTranslatef(1.0f, 0.0f, 0.0f);
    box(GL_QUADS, -0.5f, 0.0f, 1.0f, 0.0f);
    fFinish();
    pixel_is(16, 32, 1, 0, 0, "C: run before the translate stays put");
    pixel_is(48, 32, 0, 1, 0, "C: run after the translate is translated");
    fLoadIdentity();

    // --- D: a lone run must still reach the framebuffer - glReadPixels has to
    // flush a batch that is holding exactly one pending run.
    fClearColor(0.0f, 0.0f, 1.0f, 1.0f);
    fClear(GL_COLOR_BUFFER_BIT);
    box(GL_QUADS, 0.0f, 1.0f, 0.0f, 0.0f);
    pixel_is(32, 32, 1, 0, 0, "D: lone pending run is flushed by glReadPixels");

    // --- E: draw order across the merge boundary. A merged small run followed
    // by an overlapping large (non-mergeable) run must land in that order, so
    // the large one wins.
    fClearColor(0.0f, 0.0f, 1.0f, 1.0f);
    fClear(GL_COLOR_BUFFER_BIT);
    box(GL_QUADS, 0.0f, 1.0f, 0.0f, 0.0f);
    big_quad(0.0f, 1.0f, 0.0f);
    fFinish();
    pixel_is(32, 32, 0, 1, 0, "E: later large run overwrites the merged small run");

    if (fGetError() != 0) {
        fprintf(stderr, "FAIL: GL error 0x%x during the merge checks\n", fGetError());
        return 1;
    }

    if (failures != 0) {
        fprintf(stderr, "FAIL: %d immediate-merge check(s) failed\n", failures);
        return 1;
    }
    printf("OK: immediate-run merging preserves geometry, color and ordering\n");
    return 0;
}
