// SimpleFPEWrapper - tests/smoke_list_current_attr.c
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// Display-list current-attribute semantics against the glEndList run
// compiler: (1) a compiled Begin/End run that never sets color must
// inherit the CURRENT color at glCallList time (GL 2.1: current state
// applies), and (2) a glColor executed FROM a list must update the
// current color so a later immediate draw uses it. Both regressed once
// during the run-compiler bring-up; this locks them in. Skips (77)
// without an EGL device.

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
#define GL_COMPILE 0x1300
#define GL_RGBA 0x1908
#define GL_UNSIGNED_BYTE 0x1401

static int check_pixel(const GLubyte* pixel, int r, int g, int b, const char* what) {
    const int ok = (r ? pixel[0] >= 200 : pixel[0] <= 50) &&
                   (g ? pixel[1] >= 200 : pixel[1] <= 50) &&
                   (b ? pixel[2] >= 200 : pixel[2] <= 50);
    if (!ok) {
        fprintf(stderr, "FAIL: %s: pixel (%u,%u,%u), expected (%s,%s,%s)\n", what, pixel[0],
                pixel[1], pixel[2], r ? "hi" : "lo", g ? "hi" : "lo", b ? "hi" : "lo");
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

    void (*fClearColor)(GLfloat, GLfloat, GLfloat, GLfloat) =
        (void (*)(GLfloat, GLfloat, GLfloat, GLfloat))resolve("glClearColor");
    void (*fClear)(GLbitfield) = (void (*)(GLbitfield))resolve("glClear");
    void (*fBegin)(GLenum) = (void (*)(GLenum))resolve("glBegin");
    void (*fEnd)(void) = (void (*)(void))resolve("glEnd");
    void (*fColor3f)(GLfloat, GLfloat, GLfloat) =
        (void (*)(GLfloat, GLfloat, GLfloat))resolve("glColor3f");
    void (*fVertex2f)(GLfloat, GLfloat) = (void (*)(GLfloat, GLfloat))resolve("glVertex2f");
    GLuint (*fGenLists)(GLsizei) = (GLuint(*)(GLsizei))resolve("glGenLists");
    void (*fNewList)(GLuint, GLenum) = (void (*)(GLuint, GLenum))resolve("glNewList");
    void (*fEndList)(void) = (void (*)(void))resolve("glEndList");
    void (*fCallList)(GLuint) = (void (*)(GLuint))resolve("glCallList");
    void (*fReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*) =
        (void (*)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*))resolve("glReadPixels");
    GLenum (*fGetError)(void) = (GLenum(*)(void))resolve("glGetError");
    if (!fClearColor || !fClear || !fBegin || !fEnd || !fColor3f || !fVertex2f || !fGenLists ||
        !fNewList || !fEndList || !fCallList || !fReadPixels || !fGetError) {
        fprintf(stderr, "FAIL: resolver missing entry points\n");
        return 1;
    }

    GLubyte pixel[4];

    // --- Phase 1: colorless compiled run inherits the call-time color.
    const GLuint colorless = fGenLists(1);
    fNewList(colorless, GL_COMPILE);
    fBegin(GL_QUADS);
    fVertex2f(-1.0f, -1.0f);
    fVertex2f(1.0f, -1.0f);
    fVertex2f(1.0f, 1.0f);
    fVertex2f(-1.0f, 1.0f);
    fEnd();
    fEndList();

    fClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    fClear(GL_COLOR_BUFFER_BIT);
    fColor3f(1.0f, 0.0f, 0.0f); // current color at call time: red
    fCallList(colorless);
    fReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    if (!check_pixel(pixel, 1, 0, 0, "colorless list with red current color")) return 1;

    fColor3f(0.0f, 0.0f, 1.0f); // and again with blue: the list must follow
    fCallList(colorless);
    fReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    if (!check_pixel(pixel, 0, 0, 1, "colorless list with blue current color")) return 1;
    printf("OK: compiled colorless run inherits call-time current color\n");

    // --- Phase 2: in-list glColor updates the current color for later
    // immediate draws (list-executed setters are sticky per GL 2.1).
    const GLuint greening = fGenLists(1);
    fNewList(greening, GL_COMPILE);
    fBegin(GL_TRIANGLES);
    fColor3f(0.0f, 1.0f, 0.0f);
    fVertex2f(-1.0f, -1.0f);
    fVertex2f(1.0f, -1.0f);
    fVertex2f(0.0f, 1.0f);
    fEnd();
    fEndList();

    fColor3f(1.0f, 0.0f, 0.0f); // red before the call
    fCallList(greening);        // list paints green and leaves green current

    fClear(GL_COLOR_BUFFER_BIT);
    fBegin(GL_QUADS); // colorless immediate quad: must come out green
    fVertex2f(-1.0f, -1.0f);
    fVertex2f(1.0f, -1.0f);
    fVertex2f(1.0f, 1.0f);
    fVertex2f(-1.0f, 1.0f);
    fEnd();
    fReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    if (!check_pixel(pixel, 0, 1, 0, "immediate quad after green-setting list")) return 1;
    printf("OK: list-executed glColor stays current after glCallList\n");

    const GLenum error = fGetError();
    if (error != 0) {
        fprintf(stderr, "FAIL: glGetError() = 0x%x\n", error);
        return 1;
    }
    printf("OK: display-list current-attribute semantics hold\n");
    return 0;
}
