// SimpleFPEWrapper - tests/smoke_long_primitives.c
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// Ported from piglit's tests/general/longprim.c (MIT-style license; see
// piglit's COPYING): one glBegin/glEnd pair holding an enormous number of
// vertices, for every primitive mode.
//
// This is the only test in the suite that pushes the immediate-mode
// collector far past its comfortable sizes, so it is the one that covers:
//
//   - fixed_function_draw_state_t::vb growing across many reallocations
//     while advance() keeps packing into it;
//   - the merge path correctly DECLINING these runs (they are far past
//     kImmediateMergeVertexLimit) and falling through to a direct draw;
//   - a single upload larger than the streaming ring's default capacity,
//     which forces sfpewUploadToRing to finish outstanding work, replace the
//     buffer object and retry at a bigger power-of-two size - the path that
//     also has to invalidate every cached attribute binding, since the
//     buffer NAME changes underneath them.
//
// Upstream goes to a million vertices; this port stops at 200k per run,
// which still crosses the ring-growth threshold for the widest vertex
// layouts while keeping the test a few seconds rather than a minute. Each
// run is checked for GL errors and the result is probed for actual
// geometry - a silently dropped draw would otherwise look like a pass.

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <EGL/egl.h>

typedef unsigned int GLenum, GLbitfield;
typedef unsigned char GLubyte;
typedef float GLfloat;
typedef int GLint, GLsizei;

#define WIN 64
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
#define GL_PROJECTION 0x1701
#define GL_MODELVIEW 0x1700
#define GL_NO_ERROR 0

static void* (*resolve)(const char*);
static void (*fVertex2f)(GLfloat, GLfloat);
static void (*fBegin)(GLenum);
static void (*fEnd)(void);
static void (*fColor3f)(GLfloat, GLfloat, GLfloat);
static void (*fNormal3f)(GLfloat, GLfloat, GLfloat);
static void (*fTexCoord2f)(GLfloat, GLfloat);
static void (*fClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
static void (*fClear)(GLbitfield);
static void (*fOrtho)(double, double, double, double, double, double);
static void (*fMatrixMode)(GLenum);
static void (*fLoadIdentity)(void);
static void (*fFinish)(void);
static void (*fReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*);
static GLenum (*fGetError)(void);

#define R(dst, name)                                                                             \
    do {                                                                                         \
        *(void**)&dst = resolve(name);                                                           \
        if (!dst) { fprintf(stderr, "FAIL: cannot resolve %s\n", name); return 1; }               \
    } while (0)

static int fails;
static unsigned rng_state = 12345u;
static float rnd(void) { // deterministic; the geometry only needs to be spread out
    rng_state = rng_state * 1103515245u + 12345u;
    return (float)((rng_state >> 16) & 0x7fff) / 32767.0f;
}

static const char* prim_name(GLenum p) {
    switch (p) {
    case GL_POINTS: return "POINTS";
    case GL_LINES: return "LINES";
    case GL_LINE_LOOP: return "LINE_LOOP";
    case GL_LINE_STRIP: return "LINE_STRIP";
    case GL_TRIANGLES: return "TRIANGLES";
    case GL_TRIANGLE_STRIP: return "TRIANGLE_STRIP";
    case GL_TRIANGLE_FAN: return "TRIANGLE_FAN";
    case GL_QUADS: return "QUADS";
    case GL_QUAD_STRIP: return "QUAD_STRIP";
    case GL_POLYGON: return "POLYGON";
    default: return "?";
    }
}

static int any_lit(void) {
    static GLubyte px[WIN * WIN * 4];
    fReadPixels(0, 0, WIN, WIN, GL_RGBA, GL_UNSIGNED_BYTE, px);
    for (int i = 0; i < WIN * WIN; ++i)
        if (px[i * 4] > 20 || px[i * 4 + 1] > 20 || px[i * 4 + 2] > 20) return 1;
    return 0;
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
    R(fColor3f, "glColor3f"); R(fNormal3f, "glNormal3f"); R(fTexCoord2f, "glTexCoord2f");
    R(fClearColor, "glClearColor"); R(fClear, "glClear"); R(fOrtho, "glOrtho");
    R(fMatrixMode, "glMatrixMode"); R(fLoadIdentity, "glLoadIdentity"); R(fFinish, "glFinish");
    R(fReadPixels, "glReadPixels"); R(fGetError, "glGetError");

    fMatrixMode(GL_PROJECTION);
    fLoadIdentity();
    fOrtho(-1, 1, -1, 1, -1, 1);
    fMatrixMode(GL_MODELVIEW);
    fLoadIdentity();
    fClearColor(0, 0, 0, 0);
    fColor3f(1, 1, 1);
    (void)fGetError();

    static const GLenum prims[] = {GL_POINTS,         GL_LINES,      GL_LINE_LOOP,
                                   GL_LINE_STRIP,     GL_TRIANGLES,  GL_TRIANGLE_STRIP,
                                   GL_TRIANGLE_FAN,   GL_QUADS,      GL_QUAD_STRIP,
                                   GL_POLYGON};
    static const int lengths[] = {1000, 20000, 200000};

    for (int li = 0; li < 3; ++li) {
        const int len = lengths[li];
        for (int pi = 0; pi < 10; ++pi) {
            const GLenum prim = prims[pi];
            fClear(GL_COLOR_BUFFER_BIT);
            // A wide per-vertex layout (position + color + normal +
            // texcoord) so the collected stream is big enough to force the
            // ring to grow at the largest length.
            fBegin(prim);
            for (int i = 0; i < len; ++i) {
                fColor3f(1.0f, 1.0f, 1.0f);
                fNormal3f(0.0f, 0.0f, 1.0f);
                fTexCoord2f(rnd(), rnd());
                fVertex2f(rnd() * 2.0f - 1.0f, rnd() * 2.0f - 1.0f);
            }
            fEnd();
            fFinish();

            const GLenum err = fGetError();
            if (err != GL_NO_ERROR) {
                printf("FAIL %-16s x%-7d err=0x%04x\n", prim_name(prim), len, err);
                ++fails;
                continue;
            }
            if (!any_lit()) {
                printf("FAIL %-16s x%-7d drew nothing\n", prim_name(prim), len);
                ++fails;
                continue;
            }
            printf("OK   %-16s x%-7d\n", prim_name(prim), len);
        }
    }

    if (fails) {
        fprintf(stderr, "FAIL: %d long-primitive case(s) failed\n", fails);
        return 1;
    }
    printf("PASS: long single-Begin/End runs survive at every primitive mode\n");
    return 0;
}
