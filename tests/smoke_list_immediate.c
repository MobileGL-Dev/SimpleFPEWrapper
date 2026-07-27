// SimpleFPEWrapper - tests/smoke_list_immediate.c
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// A glBegin/glEnd block inside a display list is compiled into a single
// command carrying the accumulated vertex block (plans/12-fpe-draw-cost.md)
// instead of one command per glVertex/glColor/glTexCoord. That rewrite must
// be invisible: replaying the list has to put the same pixels on screen as
// running the same calls immediately, GL_COMPILE must still not draw while
// recording, and GL_COMPILE_AND_EXECUTE must both draw and record.

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <EGL/egl.h>

typedef unsigned int GLenum, GLuint, GLbitfield;
typedef unsigned char GLubyte;
typedef float GLfloat;
typedef int GLint, GLsizei;

#define GL_QUADS 0x0007
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_RGBA 0x1908
#define GL_UNSIGNED_BYTE 0x1401
#define GL_COMPILE 0x1300
#define GL_COMPILE_AND_EXECUTE 0x1301
#define GL_NO_ERROR 0

static void* gl;
static void* sym(const char* n) { return dlsym(gl, n); }

static void (*fClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
static void (*fClear)(GLbitfield);
static void (*fFinish)(void);
static void (*fViewport)(GLint, GLint, GLsizei, GLsizei);
static void (*fBegin)(GLenum);
static void (*fEnd)(void);
static void (*fVertex3f)(GLfloat, GLfloat, GLfloat);
static void (*fColor4f)(GLfloat, GLfloat, GLfloat, GLfloat);
static GLuint (*fGenLists)(GLsizei);
static void (*fNewList)(GLuint, GLenum);
static void (*fEndList)(void);
static void (*fCallList)(GLuint);
static void (*fReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*);
static GLenum (*fGetError)(void);

static int failures = 0;

// A two-colour shape: enough vertices and attributes that a mistake in the
// interleaving or the vertex count shows up as wrong pixels.
static void draw_shape(void) {
    fBegin(GL_QUADS);
    fColor4f(1.0f, 0.0f, 0.0f, 1.0f);
    fVertex3f(-0.9f, -0.9f, 0.0f);
    fVertex3f(-0.1f, -0.9f, 0.0f);
    fVertex3f(-0.1f, 0.9f, 0.0f);
    fVertex3f(-0.9f, 0.9f, 0.0f);
    fColor4f(0.0f, 0.0f, 1.0f, 1.0f);
    fVertex3f(0.1f, -0.9f, 0.0f);
    fVertex3f(0.9f, -0.9f, 0.0f);
    fVertex3f(0.9f, 0.9f, 0.0f);
    fVertex3f(0.1f, 0.9f, 0.0f);
    fEnd();
}

#define W 64
#define H 64
static GLubyte immediate_pixels[W * H * 4];
static GLubyte replay_pixels[W * H * 4];

static void capture(GLubyte* out) {
    fClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    fClear(GL_COLOR_BUFFER_BIT);
    fFinish();
    fReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, out);
}

// A pixel is "lit" if it is not the clear colour; comparing lit coverage and
// exact bytes catches both a missing draw and a corrupted one.
static int lit_count(const GLubyte* p) {
    int n = 0;
    for (int i = 0; i < W * H; ++i)
        if (p[i * 4] != 0 || p[i * 4 + 1] != 0 || p[i * 4 + 2] != 0) ++n;
    return n;
}

int main(void) {
    const char* path = getenv("WRAPPER_LIB");
    gl = dlopen(path ? path : WRAPPER_LIB_PATH, RTLD_NOW | RTLD_LOCAL);
    if (gl == NULL) {
        fprintf(stderr, "SKIP: cannot load wrapper: %s\n", dlerror());
        return 77;
    }
    void* (*resolve)(const char*) = dlsym(gl, "eglGetProcAddress");
    if (resolve == NULL) {
        fprintf(stderr, "SKIP: no eglGetProcAddress\n");
        return 77;
    }

    EGLDisplay d = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (d == EGL_NO_DISPLAY || !eglInitialize(d, NULL, NULL)) {
        printf("SKIP: no EGL display\n");
        return 77;
    }
    const EGLint ca[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE,
                         EGL_OPENGL_ES3_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
                         EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
    EGLConfig c;
    EGLint n = 0;
    if (!eglChooseConfig(d, ca, &c, 1, &n) || n == 0) {
        printf("SKIP: no ES3 pbuffer config\n");
        return 77;
    }
    const EGLint pa[] = {EGL_WIDTH, W, EGL_HEIGHT, H, EGL_NONE};
    EGLSurface s = eglCreatePbufferSurface(d, c, pa);
    eglBindAPI(EGL_OPENGL_ES_API);
    const EGLint xa[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    EGLContext x = eglCreateContext(d, c, EGL_NO_CONTEXT, xa);
    if (s == EGL_NO_SURFACE || x == EGL_NO_CONTEXT || !eglMakeCurrent(d, s, s, x)) {
        printf("SKIP: no current context\n");
        return 77;
    }

#define R(dst, name)                                                                               \
    do {                                                                                           \
        *(void**)(&dst) = resolve(name);                                                           \
        if (!dst) {                                                                                \
            fprintf(stderr, "SKIP: %s missing\n", name);                                           \
            return 77;                                                                             \
        }                                                                                          \
    } while (0)
    R(fClearColor, "glClearColor");
    R(fClear, "glClear");
    R(fFinish, "glFinish");
    R(fViewport, "glViewport");
    R(fBegin, "glBegin");
    R(fEnd, "glEnd");
    R(fVertex3f, "glVertex3f");
    R(fColor4f, "glColor4f");
    R(fGenLists, "glGenLists");
    R(fNewList, "glNewList");
    R(fEndList, "glEndList");
    R(fCallList, "glCallList");
    R(fReadPixels, "glReadPixels");
    R(fGetError, "glGetError");
#undef R

    fViewport(0, 0, W, H);

    // Reference: the same calls, run immediately.
    capture(immediate_pixels);
    draw_shape();
    fFinish();
    fReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, immediate_pixels);
    const int immediate_lit = lit_count(immediate_pixels);
    if (immediate_lit == 0) {
        printf("*** immediate draw produced nothing; cannot compare\n");
        return 1;
    }

    // GL_COMPILE must record without drawing.
    GLuint list = fGenLists(1);
    capture(replay_pixels);
    fNewList(list, GL_COMPILE);
    draw_shape();
    fEndList();
    fFinish();
    fReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, replay_pixels);
    if (lit_count(replay_pixels) != 0) {
        printf("*** GL_COMPILE drew while recording (%d lit pixels)\n", lit_count(replay_pixels));
        ++failures;
    }

    // Replaying must match the immediate reference byte for byte.
    capture(replay_pixels);
    fCallList(list);
    fFinish();
    fReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, replay_pixels);
    const int replay_lit = lit_count(replay_pixels);
    if (replay_lit != immediate_lit) {
        printf("*** replay lit %d pixels, immediate lit %d\n", replay_lit, immediate_lit);
        ++failures;
    }
    if (memcmp(replay_pixels, immediate_pixels, sizeof(replay_pixels)) != 0) {
        printf("*** replayed pixels differ from the immediate reference\n");
        ++failures;
    }

    // Replaying twice must be idempotent: the compiled block keeps its own
    // copy of the vertices, so a second call cannot consume or move them.
    capture(replay_pixels);
    fCallList(list);
    fCallList(list);
    fFinish();
    fReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, replay_pixels);
    if (memcmp(replay_pixels, immediate_pixels, sizeof(replay_pixels)) != 0) {
        printf("*** replaying twice did not match a single draw\n");
        ++failures;
    }

    // GL_COMPILE_AND_EXECUTE must draw AND leave a replayable list.
    GLuint both = fGenLists(1);
    capture(replay_pixels);
    fNewList(both, GL_COMPILE_AND_EXECUTE);
    draw_shape();
    fEndList();
    fFinish();
    fReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, replay_pixels);
    if (memcmp(replay_pixels, immediate_pixels, sizeof(replay_pixels)) != 0) {
        printf("*** GL_COMPILE_AND_EXECUTE did not draw as it recorded\n");
        ++failures;
    }
    capture(replay_pixels);
    fCallList(both);
    fFinish();
    fReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, replay_pixels);
    if (memcmp(replay_pixels, immediate_pixels, sizeof(replay_pixels)) != 0) {
        printf("*** GL_COMPILE_AND_EXECUTE list does not replay correctly\n");
        ++failures;
    }

    if (fGetError() != GL_NO_ERROR) {
        printf("*** GL error raised during compiled-list rendering\n");
        ++failures;
    }

    printf("failures: %d\n", failures);
    if (failures == 0)
        printf("OK: compiled immediate blocks replay identically to immediate mode\n");
    return failures == 0 ? 0 : 1;
}
