// SimpleFPEWrapper - tests/smoke_dlist_shademodel.c
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// Ported from piglit's tests/spec/gl-1.0/dlist-shademodel.c (MIT-style
// license; see piglit's COPYING). A display list sets GL_FLAT, draws a quad,
// redundantly sets GL_FLAT again, and draws a second quad; the list is then
// replayed while GL_SMOOTH is the live state. Both quads must come out flat
// shaded in their provoking vertex's color.
//
// Two things about this wrapper make it worth pinning:
//
//   - The shade model recorded in the list has to win over the live one, and
//     has to still apply after glEndList bakes the Begin/End runs into
//     compiled draws (sfpewCompileImmediateRuns). The redundant second
//     glShadeModel sits between two otherwise-mergeable runs, which is
//     exactly the arrangement upstream wrote it for.
//
//   - GL_QUADS has no GLES equivalent, so the wrapper rewrites each quad
//     into two triangles. Flat shading takes its color from the LAST vertex
//     of each primitive, so the rewrite has to preserve which vertex that
//     is: desktop GL_QUADS takes vertex 3 (the fourth), while the naive
//     triangle split (0,1,2)+(2,3,0) ends at vertex 2 and then vertex 0.
//     Getting that wrong shades both halves with the wrong color and no
//     other test in the suite would notice.

#include <dlfcn.h>
#include <stdio.h>
#include <EGL/egl.h>

typedef unsigned int GLenum, GLuint, GLbitfield;
typedef unsigned char GLubyte;
typedef float GLfloat;
typedef int GLint, GLsizei;

#define WIN 64
#define GL_QUADS 0x0007
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_RGBA 0x1908
#define GL_UNSIGNED_BYTE 0x1401
#define GL_COMPILE 0x1300
#define GL_FLAT 0x1D00
#define GL_SMOOTH 0x1D01
#define GL_NO_ERROR 0

static void* (*resolve)(const char*);
static void (*fVertex2f)(GLfloat, GLfloat);
static void (*fBegin)(GLenum);
static void (*fEnd)(void);
static void (*fColor3fv)(const GLfloat*);
static void (*fShadeModel)(GLenum);
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

static const GLfloat red[] = {1.0f, 0.0f, 0.0f};
static const GLfloat green[] = {0.0f, 1.0f, 0.0f};

static int fails;
static void probe_green(int x, int y, const char* what) {
    GLubyte p[4] = {0, 0, 0, 0};
    fReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p);
    const int ok = p[1] > 150 && p[0] < 100;
    printf("%s %-34s (%2d,%2d) rgb=(%3u,%3u,%3u)%s\n", ok ? "OK  " : "FAIL", what, x, y, p[0],
           p[1], p[2], ok ? "" : "  want flat green");
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
    R(fColor3fv, "glColor3fv"); R(fShadeModel, "glShadeModel");
    R(fClearColor, "glClearColor"); R(fClear, "glClear"); R(fFinish, "glFinish");
    R(fReadPixels, "glReadPixels"); R(fGenLists, "glGenLists"); R(fNewList, "glNewList");
    R(fEndList, "glEndList"); R(fCallList, "glCallList"); R(fDeleteLists, "glDeleteLists");
    R(fGetError, "glGetError");

    fClearColor(0, 0, 0, 0);
    (void)fGetError();

    const GLuint list = fGenLists(1);
    fNewList(list, GL_COMPILE);
    fShadeModel(GL_FLAT);

    // Left half. Vertices alternate red/green; the LAST one is green, so a
    // correctly flat-shaded quad is entirely green.
    fBegin(GL_QUADS);
    fColor3fv(red);   fVertex2f(-1, -1);
    fColor3fv(green); fVertex2f( 0, -1);
    fColor3fv(red);   fVertex2f( 0,  1);
    fColor3fv(green); fVertex2f(-1,  1);
    fEnd();

    // Redundant, and deliberately so: it sits between two runs the wrapper
    // would otherwise merge into one draw.
    fShadeModel(GL_FLAT);

    // Right half, same color pattern.
    fBegin(GL_QUADS);
    fColor3fv(red);   fVertex2f(0, -1);
    fColor3fv(green); fVertex2f(1, -1);
    fColor3fv(red);   fVertex2f(1,  1);
    fColor3fv(green); fVertex2f(0,  1);
    fEnd();
    fEndList();

    // The live state is SMOOTH; the list's own GL_FLAT must win.
    fShadeModel(GL_SMOOTH);
    fClear(GL_COLOR_BUFFER_BIT);
    fCallList(list);
    fFinish();

    probe_green(16, 16, "left quad flat-shaded green");
    probe_green(48, 16, "right quad flat-shaded green");
    probe_green(16, 48, "left quad, upper area");
    probe_green(48, 48, "right quad, upper area");

    fDeleteLists(list, 1);
    const GLenum err = fGetError();
    if (err != GL_NO_ERROR) {
        fprintf(stderr, "FAIL: latched GL error 0x%04x\n", err);
        return 1;
    }
    if (fails) {
        fprintf(stderr, "FAIL: %d probe(s) mismatched\n", fails);
        return 1;
    }
    printf("PASS: GL_FLAT recorded in a display list survives compilation\n");
    return 0;
}
