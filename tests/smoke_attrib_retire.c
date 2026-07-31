// SimpleFPEWrapper - tests/smoke_attrib_retire.c
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// A draw whose enabled client-array set is SMALLER than the previous draw's
// must not leave the retired attribute enabled. cidx() only assigns a physical
// attribute index to a slot that is enabled or carries a constant value, so
// when the set shrinks the indices above the new high water mark belong to no
// slot and used to keep the previous layout's format. That stale index then
// reads at its old relative offset against the new, smaller stride - past the
// end of every vertex.
//
// Found in a RenderDoc capture of Minecraft 1.16 fabulous: attr 2 stayed
// enabled at relativeoffset 12 against a 12-byte position-only stride, so every
// texcoord fetched the following vertex's position.
//
// Skips (77) when the machine has no EGL device.

#include <dlfcn.h>
#include <stdio.h>

#include <EGL/egl.h>

typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef unsigned char GLubyte;
typedef float GLfloat;
typedef int GLint, GLsizei;
typedef unsigned int GLbitfield;

#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_TRIANGLES 0x0004
#define GL_QUADS 0x0007
#define GL_FLOAT 0x1406
#define GL_RGBA 0x1908
#define GL_UNSIGNED_BYTE 0x1401
#define GL_VERTEX_ARRAY 0x8074
#define GL_COLOR_ARRAY 0x8076
#define GL_TEXTURE_COORD_ARRAY 0x8078
#define GL_NO_ERROR 0

static void (*fClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
static void (*fClear)(GLbitfield);
static void (*fEnableClientState)(GLenum);
static void (*fDisableClientState)(GLenum);
static void (*fVertexPointer)(GLint, GLenum, GLsizei, const void*);
static void (*fColorPointer)(GLint, GLenum, GLsizei, const void*);
static void (*fTexCoordPointer)(GLint, GLenum, GLsizei, const void*);
static void (*fDrawArrays)(GLenum, GLint, GLsizei);
static void (*fReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*);
static GLenum (*fGetError)(void);
static void (*fColor4f)(GLfloat, GLfloat, GLfloat, GLfloat);
static void (*fFinish)(void);

static int failures;

static void check(int x, int y, int r, int g, int b, const char* what) {
    GLubyte p[4] = {0, 0, 0, 0};
    fReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p);
    const int ok = ((p[0] > 200) == (r > 0)) && ((p[1] > 200) == (g > 0)) &&
                   ((p[2] > 200) == (b > 0));
    if (!ok) {
        fprintf(stderr, "FAIL: %s: pixel(%d,%d) = (%u,%u,%u), expected (%d,%d,%d)\n", what, x, y,
                p[0], p[1], p[2], r, g, b);
        ++failures;
    } else {
        printf("OK: %s\n", what);
    }
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
    static const EGLint pbuffer_attribs[] = {EGL_WIDTH, 64, EGL_HEIGHT, 64, EGL_NONE};
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
    fEnableClientState = (void (*)(GLenum))resolve("glEnableClientState");
    fDisableClientState = (void (*)(GLenum))resolve("glDisableClientState");
    fVertexPointer = (void (*)(GLint, GLenum, GLsizei, const void*))resolve("glVertexPointer");
    fColorPointer = (void (*)(GLint, GLenum, GLsizei, const void*))resolve("glColorPointer");
    fTexCoordPointer = (void (*)(GLint, GLenum, GLsizei, const void*))resolve("glTexCoordPointer");
    fDrawArrays = (void (*)(GLenum, GLint, GLsizei))resolve("glDrawArrays");
    fReadPixels = (void (*)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum,
                            void*))resolve("glReadPixels");
    fGetError = (GLenum(*)(void))resolve("glGetError");
    fColor4f = (void (*)(GLfloat, GLfloat, GLfloat, GLfloat))resolve("glColor4f");
    fFinish = (void (*)(void))resolve("glFinish");
    if (!fClearColor || !fClear || !fEnableClientState || !fDisableClientState || !fVertexPointer ||
        !fColorPointer || !fTexCoordPointer || !fDrawArrays || !fReadPixels || !fGetError ||
        !fColor4f || !fFinish) {
        fprintf(stderr, "FAIL: resolver missing entry points\n");
        return 1;
    }

    // Draw A: position + color + texcoord, interleaved, stride 36 bytes.
    // Claims physical attribute indices 0, 1 and 2.
    static const GLfloat wide[] = {
        -1, -1, 0, 1, 0, 0, 1, 0, 0,
         1, -1, 0, 1, 0, 0, 1, 1, 0,
         1,  1, 0, 1, 0, 0, 1, 1, 1,
        -1,  1, 0, 1, 0, 0, 1, 0, 1,
    };
    fClearColor(0.0f, 0.0f, 1.0f, 1.0f);
    fClear(GL_COLOR_BUFFER_BIT);
    fEnableClientState(GL_VERTEX_ARRAY);
    fEnableClientState(GL_COLOR_ARRAY);
    fEnableClientState(GL_TEXTURE_COORD_ARRAY);
    fVertexPointer(3, GL_FLOAT, 36, wide);
    fColorPointer(4, GL_FLOAT, 36, wide + 3);
    fTexCoordPointer(2, GL_FLOAT, 36, wide + 7);
    fDrawArrays(GL_QUADS, 0, 4);
    fFinish();
    check(32, 32, 1, 0, 0, "A: position+color+texcoord draw renders red");

    // Draw B: the set SHRINKS to position only, tightly packed (stride 12).
    // Physical index 2 is now claimed by no slot. Left enabled it keeps
    // relativeoffset 12 from draw A and reads past every 12-byte vertex, which
    // corrupts the shader's texcoord input; the wrapper must retire it.
    // The color comes from the sticky current value so the pixel test is
    // independent of the color array.
    static const GLfloat narrow[] = {
        -1, -1, 0,
         1, -1, 0,
         1,  1, 0,
        -1,  1, 0,
    };
    fClearColor(0.0f, 0.0f, 1.0f, 1.0f);
    fClear(GL_COLOR_BUFFER_BIT);
    fDisableClientState(GL_COLOR_ARRAY);
    fDisableClientState(GL_TEXTURE_COORD_ARRAY);
    fColor4f(0.0f, 1.0f, 0.0f, 1.0f);
    fVertexPointer(3, GL_FLOAT, 0, narrow);
    fDrawArrays(GL_QUADS, 0, 4);
    fFinish();
    check(32, 32, 0, 1, 0, "B: shrunk-set draw renders green (retired attr not read)");

    const GLenum err = fGetError();
    if (err != GL_NO_ERROR) {
        fprintf(stderr, "FAIL: glGetError 0x%x\n", err);
        ++failures;
    }

    fDisableClientState(GL_VERTEX_ARRAY);

    if (failures != 0) {
        fprintf(stderr, "FAIL: %d attribute-retire check(s) failed\n", failures);
        return 1;
    }
    printf("OK: shrinking the enabled array set retires stale attribute indices\n");
    return 0;
}
