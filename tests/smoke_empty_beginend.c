// SimpleFPEWrapper - tests/smoke_empty_beginend.c
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// Ported from piglit's tests/spec/gl-1.0/empty-begin-end-clause.c (MIT-style
// license; see piglit's COPYING; fdo bug #23489): a Begin/End pair with zero
// vertices submitted must be a legal no-op, over every primitive mode, not
// just GL_LINES. Also covers what the beginend-coverage port's GL_NONE ==
// GL_POINTS sentinel fix was really guarding: GL_POINTS is mode 0, the same
// value the wrapper used to mean "no Begin/End open", so an empty
// glBegin(GL_POINTS)/glEnd() used to raise a spurious GL_INVALID_OPERATION
// and every vertex submitted inside one was silently dropped.

#include <dlfcn.h>
#include <stdio.h>
#include <EGL/egl.h>

typedef unsigned int GLenum, GLbitfield;
typedef int GLsizei;

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
#define GL_NO_ERROR 0
#define GL_COLOR_BUFFER_BIT 0x00004000

int main(void) {
    void* h = dlopen(WRAPPER_LIB_PATH, RTLD_NOW | RTLD_LOCAL);
    if (!h) { fprintf(stderr, "FAIL: dlopen: %s\n", dlerror()); return 1; }
    void* (*resolve)(const char*) = (void* (*)(const char*))dlsym(h, "eglGetProcAddress");
    if (!resolve) { fprintf(stderr, "FAIL: no eglGetProcAddress\n"); return 1; }

    EGLDisplay d = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (d == EGL_NO_DISPLAY || !eglInitialize(d, 0, 0)) { printf("SKIP\n"); return 77; }
    const EGLint cf[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE,
                         EGL_OPENGL_ES3_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
                         EGL_BLUE_SIZE, 8, EGL_NONE};
    EGLConfig c; EGLint nc = 0;
    if (!eglChooseConfig(d, cf, &c, 1, &nc) || !nc) { printf("SKIP\n"); return 77; }
    const EGLint pb[] = {EGL_WIDTH, 64, EGL_HEIGHT, 64, EGL_NONE};
    EGLSurface s = eglCreatePbufferSurface(d, c, pb);
    eglBindAPI(EGL_OPENGL_ES_API);
    const EGLint ca[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    EGLContext x = eglCreateContext(d, c, EGL_NO_CONTEXT, ca);
    if (!eglMakeCurrent(d, s, s, x)) { printf("SKIP\n"); return 77; }

    void (*begin)(GLenum) = (void (*)(GLenum))resolve("glBegin");
    void (*end)(void) = (void (*)(void))resolve("glEnd");
    void (*flush)(void) = (void (*)(void))resolve("glFlush");
    void (*clearColor)(float, float, float, float) =
        (void (*)(float, float, float, float))resolve("glClearColor");
    void (*clear)(GLbitfield) = (void (*)(GLbitfield))resolve("glClear");
    GLenum (*getErr)(void) = (GLenum (*)(void))resolve("glGetError");
    if (!begin || !end || !flush || !clearColor || !clear || !getErr) {
        fprintf(stderr, "FAIL: entry points missing\n");
        return 1;
    }

    const GLenum modes[] = {GL_POINTS,    GL_LINES,         GL_LINE_LOOP,  GL_LINE_STRIP,
                            GL_TRIANGLES, GL_TRIANGLE_STRIP, GL_TRIANGLE_FAN, GL_QUADS,
                            GL_QUAD_STRIP, GL_POLYGON};
    clearColor(0.0f, 0.0f, 0.0f, 0.0f);
    clear(GL_COLOR_BUFFER_BIT);
    (void)getErr();

    for (int m = 0; m < (int)(sizeof(modes) / sizeof(modes[0])); ++m) {
        for (int i = 0; i < 100; ++i) {
            begin(modes[m]);
            end();
        }
        const GLenum err = getErr();
        if (err != GL_NO_ERROR) {
            fprintf(stderr, "FAIL: empty Begin(0x%04x)/End() x100 latched 0x%04x\n", modes[m], err);
            return 1;
        }
    }
    flush();
    if (getErr() != GL_NO_ERROR) {
        fprintf(stderr, "FAIL: glFlush() after empty Begin/End runs latched an error\n");
        return 1;
    }

    printf("PASS: empty Begin/End is a no-op across every primitive mode\n");
    return 0;
}
