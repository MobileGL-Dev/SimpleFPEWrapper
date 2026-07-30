// SimpleFPEWrapper - tests/smoke_degenerate_prims.c
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// Ported from piglit's tests/general/degenerate-prims.c (MIT-style license;
// see piglit's COPYING). Every primitive mode drawn with FEWER vertices than
// one complete primitive group must draw nothing at all - GL drops the
// incomplete trailing group.
//
// This matters more here than on a native driver. The wrapper rewrites the
// legacy modes GL has but GLES does not: GL_QUADS becomes indexed triangles
// via (count / 4) * 6 indices, GL_QUAD_STRIP becomes GL_TRIANGLE_STRIP and
// GL_POLYGON becomes GL_TRIANGLE_FAN. A rewrite that rounded the wrong way,
// or that forwarded a 3-vertex QUAD_STRIP straight through as a triangle
// strip (which WOULD draw a triangle), turns "nothing" into visible
// geometry. Upstream notes GL_QUADS/GL_QUAD_STRIP with 3 verts as a repeat
// offender in Mesa for exactly this reason.
//
// Upstream drives this through client arrays; this port runs every case
// twice, once through glDrawArrays (the commit_fpe_state_on_draw rewrite)
// and once through glBegin/glEnd (the drawImmediateVertices rewrite, plus
// its small-run merging), because the two paths convert independently.

#include <dlfcn.h>
#include <stdio.h>
#include <EGL/egl.h>

typedef unsigned int GLenum, GLbitfield;
typedef unsigned char GLubyte;
typedef float GLfloat;
typedef int GLint, GLsizei;

#define WIN 32
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
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_RGBA 0x1908
#define GL_UNSIGNED_BYTE 0x1401
#define GL_FLOAT 0x1406
#define GL_VERTEX_ARRAY 0x8074
#define GL_PROJECTION 0x1701
#define GL_MODELVIEW 0x1700
#define GL_NO_ERROR 0

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

// The whole framebuffer must be black: nothing was drawable.
static void expect_blank(const char* what) {
    static GLubyte px[WIN * WIN * 4];
    fReadPixels(0, 0, WIN, WIN, GL_RGBA, GL_UNSIGNED_BYTE, px);
    int lit = 0, first = -1;
    for (int i = 0; i < WIN * WIN; ++i) {
        if (px[i * 4] > 20 || px[i * 4 + 1] > 20 || px[i * 4 + 2] > 20) {
            if (first < 0) first = i;
            ++lit;
        }
    }
    if (lit == 0) {
        printf("OK   %-38s nothing drawn\n", what);
        return;
    }
    printf("FAIL %-38s %d lit pixel(s), first at (%d,%d)\n", what, lit, first % WIN, first / WIN);
    ++fails;
}

static const char* prim_name(GLenum p) {
    switch (p) {
    case GL_POINTS: return "GL_POINTS";
    case GL_LINES: return "GL_LINES";
    case GL_LINE_STRIP: return "GL_LINE_STRIP";
    case GL_LINE_LOOP: return "GL_LINE_LOOP";
    case GL_TRIANGLES: return "GL_TRIANGLES";
    case GL_TRIANGLE_STRIP: return "GL_TRIANGLE_STRIP";
    case GL_TRIANGLE_FAN: return "GL_TRIANGLE_FAN";
    case GL_QUADS: return "GL_QUADS";
    case GL_QUAD_STRIP: return "GL_QUAD_STRIP";
    case GL_POLYGON: return "GL_POLYGON";
    default: return "?";
    }
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
    fOrtho(-1, 1, -1, 1, -1, 1);
    fMatrixMode(GL_MODELVIEW);
    fLoadIdentity();
    fClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    fColor3f(1, 1, 1);

    // Upstream's vertex sets: each covers the whole viewport, so ANY
    // primitive that does get assembled is impossible to miss.
    static const GLfloat verts2[2][2] = {{-1, -1}, {1, 1}};
    static const GLfloat verts3[3][2] = {{-1, -1}, {1, -1}, {0, 1}};
    static const GLfloat verts4[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};

    const struct {
        GLenum prim;
        int count;         // fewer vertices than one complete group
        const GLfloat* v;
    } cases[] = {
        {GL_POINTS, 0, &verts2[0][0]},
        {GL_LINES, 1, &verts2[0][0]},
        {GL_LINE_STRIP, 1, &verts2[0][0]},
        {GL_LINE_LOOP, 1, &verts2[0][0]},
        {GL_TRIANGLES, 2, &verts3[0][0]},
        {GL_TRIANGLE_STRIP, 2, &verts3[0][0]},
        {GL_TRIANGLE_FAN, 2, &verts3[0][0]},
        {GL_QUADS, 3, &verts4[0][0]},
        {GL_QUAD_STRIP, 3, &verts4[0][0]},
        {GL_POLYGON, 2, &verts4[0][0]},
    };
    const int n = (int)(sizeof(cases) / sizeof(cases[0]));

    // --- client arrays (upstream's path) --------------------------------
    fEnableClientState(GL_VERTEX_ARRAY);
    for (int i = 0; i < n; ++i) {
        fClear(GL_COLOR_BUFFER_BIT);
        fVertexPointer(2, GL_FLOAT, 0, cases[i].v);
        fDrawArrays(cases[i].prim, 0, cases[i].count);
        fFinish();
        char what[96];
        snprintf(what, sizeof what, "glDrawArrays %s x%d", prim_name(cases[i].prim), cases[i].count);
        expect_blank(what);
    }
    fDisableClientState(GL_VERTEX_ARRAY);

    // --- immediate mode (this wrapper's independent rewrite) ------------
    for (int i = 0; i < n; ++i) {
        fClear(GL_COLOR_BUFFER_BIT);
        fBegin(cases[i].prim);
        for (int v = 0; v < cases[i].count; ++v)
            fVertex2f(cases[i].v[v * 2], cases[i].v[v * 2 + 1]);
        fEnd();
        fFinish();
        char what[96];
        snprintf(what, sizeof what, "glBegin %s x%d", prim_name(cases[i].prim), cases[i].count);
        expect_blank(what);
    }

    const GLenum err = fGetError();
    if (err != GL_NO_ERROR) {
        fprintf(stderr, "FAIL: degenerate primitives latched GL error 0x%04x\n", err);
        return 1;
    }
    if (fails) {
        fprintf(stderr, "FAIL: %d degenerate primitive(s) drew something\n", fails);
        return 1;
    }
    printf("PASS: incomplete primitive groups draw nothing on both draw paths\n");
    return 0;
}
