// SimpleFPEWrapper - tests/smoke_rastercolor.c
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// Ported from piglit's tests/spec/gl-1.0/rastercolor.c (MIT-style license;
// see piglit's COPYING). glRasterPos SNAPSHOTS the current colour along with
// the transformed position: a later glColor changes what primitives draw
// with, but every glBitmap issued from that raster position keeps the colour
// that was current when glRasterPos ran.
//
// The sequence is: raster position set while green is current, then blue
// becomes current, then a bitmap (must be green), then an immediate-mode
// quad (must be blue), then a second bitmap advanced by the first one's
// xmove (must still be green). That last one also pins the raster-position
// ADVANCE: glBitmap moves the raster position by its xmove/ymove, and the
// snapshot has to survive the move.

#include <dlfcn.h>
#include <stdio.h>
#include <EGL/egl.h>

typedef unsigned int GLenum, GLbitfield;
typedef unsigned char GLubyte;
typedef float GLfloat;
typedef int GLint, GLsizei;

#define WIN 64
#define GL_QUADS 0x0007
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_RGBA 0x1908
#define GL_UNSIGNED_BYTE 0x1401
#define GL_PROJECTION 0x1701
#define GL_MODELVIEW 0x1700
#define GL_UNPACK_ALIGNMENT 0x0CF5
#define GL_NO_ERROR 0

static void* (*resolve)(const char*);
static void (*fVertex2f)(GLfloat, GLfloat);
static void (*fBegin)(GLenum);
static void (*fEnd)(void);
static void (*fColor3fv)(const GLfloat*);
static void (*fRasterPos2i)(GLint, GLint);
static void (*fBitmap)(GLsizei, GLsizei, GLfloat, GLfloat, GLfloat, GLfloat, const GLubyte*);
static void (*fClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
static void (*fClear)(GLbitfield);
static void (*fOrtho)(double, double, double, double, double, double);
static void (*fMatrixMode)(GLenum);
static void (*fLoadIdentity)(void);
static void (*fViewport)(GLint, GLint, GLsizei, GLsizei);
static void (*fPixelStorei)(GLenum, GLint);
static void (*fFinish)(void);
static void (*fReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*);
static GLenum (*fGetError)(void);

#define R(dst, name)                                                                             \
    do {                                                                                         \
        *(void**)&dst = resolve(name);                                                           \
        if (!dst) { fprintf(stderr, "FAIL: cannot resolve %s\n", name); return 1; }               \
    } while (0)

static int fails;
static void probe(int x, int y, int wantR, int wantG, int wantB, const char* what) {
    GLubyte p[4] = {0, 0, 0, 0};
    fReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p);
    const int gotR = p[0] > 150, gotG = p[1] > 150, gotB = p[2] > 150;
    const int ok = gotR == wantR && gotG == wantG && gotB == wantB;
    printf("%s %-34s (%2d,%2d) rgb=(%3u,%3u,%3u)\n", ok ? "OK  " : "FAIL", what, x, y, p[0], p[1],
           p[2]);
    if (!ok) ++fails;
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
    R(fColor3fv, "glColor3fv"); R(fRasterPos2i, "glRasterPos2i"); R(fBitmap, "glBitmap");
    R(fClearColor, "glClearColor"); R(fClear, "glClear"); R(fOrtho, "glOrtho");
    R(fMatrixMode, "glMatrixMode"); R(fLoadIdentity, "glLoadIdentity"); R(fViewport, "glViewport");
    R(fPixelStorei, "glPixelStorei"); R(fFinish, "glFinish"); R(fReadPixels, "glReadPixels");
    R(fGetError, "glGetError");

    static const GLfloat green[3] = {0, 1, 0};
    static const GLfloat blue[3] = {0, 0, 1};
    static const GLubyte bitmap[8] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

    fPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    fClearColor(0, 0, 0, 0);
    fClear(GL_COLOR_BUFFER_BIT);

    fViewport(0, 0, WIN, WIN);
    fMatrixMode(GL_PROJECTION);
    fLoadIdentity();
    fOrtho(0, WIN, 0, WIN, -1, 1);
    fMatrixMode(GL_MODELVIEW);
    fLoadIdentity();
    (void)fGetError();

    // Raster colour becomes green; the raster position snapshots it.
    fColor3fv(green);
    fRasterPos2i(8, 8);

    // Primitive colour becomes blue - must NOT affect the snapshot.
    fColor3fv(blue);

    // Bitmap 1 at the raster position, advancing x by 32 for bitmap 2.
    fBitmap(8, 8, 0, 0, 32, 0, bitmap);

    // A blue quad between the two bitmaps.
    fBegin(GL_QUADS);
    fVertex2f(24, 8);
    fVertex2f(32, 8);
    fVertex2f(32, 16);
    fVertex2f(24, 16);
    fEnd();

    // Bitmap 2 at the advanced raster position, no further advance.
    fBitmap(8, 8, 0, 0, 0, 0, bitmap);
    fFinish();

    probe(12, 12, 0, 1, 0, "bitmap 1 keeps raster green");
    probe(28, 12, 0, 0, 1, "quad uses current blue");
    probe(44, 12, 0, 1, 0, "bitmap 2 still raster green");

    const GLenum err = fGetError();
    if (err != GL_NO_ERROR) {
        fprintf(stderr, "FAIL: latched GL error 0x%04x\n", err);
        return 1;
    }
    if (fails) {
        fprintf(stderr, "FAIL: %d probe(s) mismatched\n", fails);
        return 1;
    }
    printf("PASS: glRasterPos snapshots the current colour for glBitmap\n");
    return 0;
}
