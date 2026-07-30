// SimpleFPEWrapper - tests/smoke_draw_batch.c
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// Ported from piglit's tests/general/draw-batch.c (MIT-style license; see
// piglit's COPYING). It draws the SAME four coloured triangles four times
// over, once through each of the draw paths an OpenGL 1.x application has -
// glDrawElements, glDrawArrays, glBegin/glEnd and glCallList - putting each
// pass on its own row via glTranslatef, then probes all sixteen cells. Every
// row has to come out identical.
//
// That makes it a cross-check no single-path test can be: the four paths are
// separate code in this wrapper (drawElementsNow, drawArraysNow,
// drawImmediateVertices, and the display-list replay commands), they convert
// legacy primitives independently, and since the deferred draw-state work
// they also each decide for themselves when to hand the app's
// program/VAO/buffer bindings back. A path that leaks state into the next
// one, or that applies the modelview at the wrong time, shows up here as one
// row disagreeing with the other three.
//
// Interleaved between the passes are the state changes upstream uses to
// provoke exactly that: a modelview translate, a client-array
// enable/disable, and colour changes.
//
// NOT ported: upstream also enables GL_COLOR_SUM and feeds a secondary
// colour through glSecondaryColorPointer/glSecondaryColor3fv, expecting it
// summed into the fragment colour. This wrapper tracks secondary colour but
// never sums it (no GL_COLOR_SUM support in the generated shaders at all),
// so that part is omitted rather than xfailed - dropping it keeps the
// four-path equivalence check, which is the part that exercises this
// wrapper, fully enforced.

#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <EGL/egl.h>

typedef unsigned int GLenum, GLuint, GLbitfield;
typedef unsigned char GLubyte;
typedef unsigned short GLushort;
typedef float GLfloat;
typedef int GLint, GLsizei;

#define WIN 128
#define GL_TRIANGLES 0x0004
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_RGBA 0x1908
#define GL_UNSIGNED_BYTE 0x1401
#define GL_UNSIGNED_SHORT 0x1403
#define GL_FLOAT 0x1406
#define GL_PROJECTION 0x1701
#define GL_MODELVIEW 0x1700
#define GL_VERTEX_ARRAY 0x8074
#define GL_COLOR_ARRAY 0x8076
#define GL_COMPILE 0x1300
#define GL_NO_ERROR 0

static void* (*resolve)(const char*);
static void (*fVertex2fv)(const GLfloat*);
static void (*fBegin)(GLenum);
static void (*fEnd)(void);
static void (*fColor3fv)(const GLfloat*);
static void (*fClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
static void (*fClear)(GLbitfield);
static void (*fOrtho)(double, double, double, double, double, double);
static void (*fMatrixMode)(GLenum);
static void (*fLoadIdentity)(void);
static void (*fTranslatef)(GLfloat, GLfloat, GLfloat);
static void (*fFinish)(void);
static void (*fReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*);
static void (*fVertexPointer)(GLint, GLenum, GLsizei, const void*);
static void (*fColorPointer)(GLint, GLenum, GLsizei, const void*);
static void (*fEnableClientState)(GLenum);
static void (*fDisableClientState)(GLenum);
static void (*fDrawArrays)(GLenum, GLint, GLsizei);
static void (*fDrawElements)(GLenum, GLsizei, GLenum, const void*);
static GLuint (*fGenLists)(GLsizei);
static void (*fNewList)(GLuint, GLenum);
static void (*fEndList)(void);
static void (*fCallList)(GLuint);
static GLenum (*fGetError)(void);

#define R(dst, name)                                                                             \
    do {                                                                                         \
        *(void**)&dst = resolve(name);                                                           \
        if (!dst) { fprintf(stderr, "FAIL: cannot resolve %s\n", name); return 1; }               \
    } while (0)

static int fails;

// Four right triangles, one per column, interleaved as {x, y, r, g, b}
// exactly like upstream's array layout.
static GLfloat array[] = {
    10, 10, 1, 0, 0,   27, 10, 1, 0, 0,   10, 30, 1, 0, 0,
    30, 10, 0, 1, 0,   47, 10, 0, 1, 0,   30, 30, 0, 1, 0,
    50, 10, 0, 0, 1,   67, 10, 0, 0, 1,   50, 30, 0, 0, 1,
    70, 10, 1, 0, 1,   87, 10, 1, 0, 1,   70, 30, 1, 0, 1,
};
static const GLushort indices[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};

// Expected colour of each column, as 0/1 per channel.
static const int expect[4][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {1, 0, 1}};

static void probe_row(int rowIndex, int y, const char* what) {
    for (int col = 0; col < 4; ++col) {
        const int px = 15 + col * 20;
        GLubyte p[4] = {0, 0, 0, 0};
        fReadPixels(px, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p);
        const int gotR = p[0] > 150, gotG = p[1] > 150, gotB = p[2] > 150;
        const int ok = gotR == expect[col][0] && gotG == expect[col][1] && gotB == expect[col][2];
        if (!ok) {
            printf("FAIL %-14s row %d col %d at (%d,%d): rgb=(%3u,%3u,%3u) want=(%d,%d,%d)\n", what,
                   rowIndex, col, px, y, p[0], p[1], p[2], expect[col][0] * 255,
                   expect[col][1] * 255, expect[col][2] * 255);
            ++fails;
            return;
        }
    }
    printf("OK   %-14s row %d: all 4 triangles correct\n", what, rowIndex);
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

    R(fVertex2fv, "glVertex2fv"); R(fBegin, "glBegin"); R(fEnd, "glEnd");
    R(fColor3fv, "glColor3fv"); R(fClearColor, "glClearColor"); R(fClear, "glClear");
    R(fOrtho, "glOrtho"); R(fMatrixMode, "glMatrixMode"); R(fLoadIdentity, "glLoadIdentity");
    R(fTranslatef, "glTranslatef"); R(fFinish, "glFinish"); R(fReadPixels, "glReadPixels");
    R(fVertexPointer, "glVertexPointer"); R(fColorPointer, "glColorPointer");
    R(fEnableClientState, "glEnableClientState");
    R(fDisableClientState, "glDisableClientState");
    R(fDrawArrays, "glDrawArrays"); R(fDrawElements, "glDrawElements");
    R(fGenLists, "glGenLists"); R(fNewList, "glNewList"); R(fEndList, "glEndList");
    R(fCallList, "glCallList"); R(fGetError, "glGetError");

    fMatrixMode(GL_PROJECTION);
    fLoadIdentity();
    fOrtho(0, WIN, 0, WIN, -1, 1);
    fMatrixMode(GL_MODELVIEW);
    fLoadIdentity();
    fClearColor(0, 0, 0, 0);
    (void)fGetError();

    // State change: declare the client arrays, then clear. The arrays must
    // survive the clear (upstream's comment: "The vertex array state should
    // be preserved after glClear").
    fVertexPointer(2, GL_FLOAT, 20, array);
    fColorPointer(3, GL_FLOAT, 20, array + 2);
    fEnableClientState(GL_VERTEX_ARRAY);
    fEnableClientState(GL_COLOR_ARRAY);
    fClear(GL_COLOR_BUFFER_BIT);

    // Row 0: glDrawElements, one call per triangle.
    for (int i = 0; i < 4; ++i)
        fDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, indices + i * 3);

    // State change: modelview. Row 1: glDrawArrays.
    fTranslatef(0, 30, 0);
    for (int i = 0; i < 4; ++i) fDrawArrays(GL_TRIANGLES, i * 3, 3);

    // State change: drop the client arrays entirely, so the immediate-mode
    // rows cannot accidentally source from them.
    fDisableClientState(GL_VERTEX_ARRAY);
    fDisableClientState(GL_COLOR_ARRAY);

    // Row 2: glBegin/glEnd.
    fTranslatef(0, 30, 0);
    for (int i = 0; i < 4; ++i) {
        fBegin(GL_TRIANGLES);
        for (int j = 0; j < 3; ++j) {
            fColor3fv(array + i * 15 + j * 5 + 2);
            fVertex2fv(array + i * 15 + j * 5);
        }
        fEnd();
    }

    // Row 3: the same Begin/End runs, compiled into display lists.
    fTranslatef(0, 30, 0);
    const GLuint base = fGenLists(4);
    for (int i = 0; i < 4; ++i) {
        fNewList(base + i, GL_COMPILE);
        fBegin(GL_TRIANGLES);
        for (int j = 0; j < 3; ++j) {
            fColor3fv(array + i * 15 + j * 5 + 2);
            fVertex2fv(array + i * 15 + j * 5);
        }
        fEnd();
        fEndList();
    }
    for (int i = 0; i < 4; ++i) fCallList(base + i);
    fFinish();

    probe_row(0, 15, "DrawElements");
    probe_row(1, 45, "DrawArrays");
    probe_row(2, 75, "Begin/End");
    probe_row(3, 105, "CallList");

    const GLenum err = fGetError();
    if (err != GL_NO_ERROR) {
        fprintf(stderr, "FAIL: latched GL error 0x%04x\n", err);
        return 1;
    }
    if (fails) {
        fprintf(stderr, "FAIL: %d draw path(s) disagreed\n", fails);
        return 1;
    }
    printf("PASS: all four draw paths agree across interleaved state changes\n");
    return 0;
}
