// SimpleFPEWrapper - tests/smoke_render.c
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// First end-to-end FPE render check (plans/11 Q2): every GL call goes
// through the wrapper's resolver; an immediate-mode red quad must come
// back red through glReadPixels on a real GLES3 device. Skips (77) when
// the machine has no EGL device.

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
#define GL_QUADS 0x0007
#define GL_RGBA 0x1908
#define GL_UNSIGNED_BYTE 0x1401

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
                                            EGL_OPENGL_ES3_BIT, EGL_NONE};
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
    void (*fColor3f)(GLfloat, GLfloat, GLfloat) = (void (*)(GLfloat, GLfloat, GLfloat))resolve("glColor3f");
    void (*fVertex2f)(GLfloat, GLfloat) = (void (*)(GLfloat, GLfloat))resolve("glVertex2f");
    void (*fReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*) =
        (void (*)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*))resolve("glReadPixels");
    GLenum (*fGetError)(void) = (GLenum(*)(void))resolve("glGetError");
    if (!fClearColor || !fClear || !fBegin || !fEnd || !fColor3f || !fVertex2f || !fReadPixels ||
        !fGetError) {
        fprintf(stderr, "FAIL: resolver missing entry points\n");
        return 1;
    }

    fClearColor(0.0f, 0.0f, 1.0f, 1.0f);
    fClear(GL_COLOR_BUFFER_BIT);

    // Identity matrices: NDC coordinates directly. Full-screen red quad.
    fBegin(GL_QUADS);
    fColor3f(1.0f, 0.0f, 0.0f);
    fVertex2f(-1.0f, -1.0f);
    fVertex2f(1.0f, -1.0f);
    fVertex2f(1.0f, 1.0f);
    fVertex2f(-1.0f, 1.0f);
    fEnd();

    GLubyte pixel[4] = {0, 0, 0, 0};
    fReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);

    const GLenum error = fGetError();
    if (error != 0) {
        fprintf(stderr, "FAIL: glGetError() = 0x%x after the draw\n", error);
        return 1;
    }
    if (pixel[0] < 200 || pixel[1] > 50 || pixel[2] > 50) {
        fprintf(stderr, "FAIL: center pixel is (%u,%u,%u,%u), expected red\n", pixel[0], pixel[1],
                pixel[2], pixel[3]);
        return 1;
    }
    printf("OK: phase 1 immediate-mode quad is red\n");

    // --- Phase 2: vertex lighting (plans/04). A directional white light
    // shining down -z onto a +z-facing red quad: diffuse keeps red bright.
    void (*fEnable)(GLenum) = (void (*)(GLenum))resolve("glEnable");
    void (*fDisable)(GLenum) = (void (*)(GLenum))resolve("glDisable");
    void (*fLightfv)(GLenum, GLenum, const GLfloat*) =
        (void (*)(GLenum, GLenum, const GLfloat*))resolve("glLightfv");
    void (*fNormal3f)(GLfloat, GLfloat, GLfloat) =
        (void (*)(GLfloat, GLfloat, GLfloat))resolve("glNormal3f");
    if (!fEnable || !fDisable || !fLightfv || !fNormal3f) return 1;

    fClearColor(0.0f, 0.0f, 1.0f, 1.0f);
    fClear(GL_COLOR_BUFFER_BIT);
    fEnable(0x0B50 /* GL_LIGHTING */);
    fEnable(0x4000); /* GL_LIGHT0 */
    static const GLfloat white[4] = {1, 1, 1, 1};
    static const GLfloat dir[4] = {0, 0, 1, 0}; // directional, towards viewer
    fLightfv(0x4000, 0x1201 /* GL_DIFFUSE */, white);
    fLightfv(0x4000, 0x1203 /* GL_POSITION */, dir);
    fEnable(0x0B57 /* GL_COLOR_MATERIAL */);

    fBegin(GL_QUADS);
    fColor3f(1.0f, 0.0f, 0.0f);
    fNormal3f(0.0f, 0.0f, 1.0f);
    fVertex2f(-1.0f, -1.0f);
    fVertex2f(1.0f, -1.0f);
    fVertex2f(1.0f, 1.0f);
    fVertex2f(-1.0f, 1.0f);
    fEnd();

    fReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    if (fGetError() != 0) {
        fprintf(stderr, "FAIL: GL error after the lit draw\n");
        return 1;
    }
    if (pixel[0] < 150 || pixel[1] > 80 || pixel[2] > 80) {
        fprintf(stderr, "FAIL: lit pixel is (%u,%u,%u), expected bright red\n", pixel[0], pixel[1],
                pixel[2]);
        return 1;
    }
    fDisable(0x0B50);
    fDisable(0x0B57);
    printf("OK: phase 2 directional lighting keeps the quad red\n");

    // --- Phase 3: GL_MODULATE texturing (plans/05). A solid green texture
    // times a white quad must come back green.
    void (*fGenTextures)(GLsizei, GLuint*) = (void (*)(GLsizei, GLuint*))resolve("glGenTextures");
    void (*fBindTexture)(GLenum, GLuint) = (void (*)(GLenum, GLuint))resolve("glBindTexture");
    void (*fTexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*) =
        (void (*)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum,
                  const void*))resolve("glTexImage2D");
    void (*fTexParameteri)(GLenum, GLenum, GLint) =
        (void (*)(GLenum, GLenum, GLint))resolve("glTexParameteri");
    void (*fTexCoord2f)(GLfloat, GLfloat) = (void (*)(GLfloat, GLfloat))resolve("glTexCoord2f");
    if (!fGenTextures || !fBindTexture || !fTexImage2D || !fTexParameteri || !fTexCoord2f) return 1;

    GLuint texture = 0;
    fGenTextures(1, &texture);
    fBindTexture(0x0DE1 /* GL_TEXTURE_2D */, texture);
    static const GLubyte green_texel[4] = {0, 255, 0, 255};
    fTexImage2D(0x0DE1, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, green_texel);
    fTexParameteri(0x0DE1, 0x2801 /* GL_TEXTURE_MIN_FILTER */, 0x2600 /* GL_NEAREST */);
    fTexParameteri(0x0DE1, 0x2800 /* GL_TEXTURE_MAG_FILTER */, 0x2600);
    fEnable(0x0DE1); // GL_TEXTURE_2D

    fClearColor(0.0f, 0.0f, 1.0f, 1.0f);
    fClear(GL_COLOR_BUFFER_BIT);
    fBegin(GL_QUADS);
    fColor3f(1.0f, 1.0f, 1.0f);
    fTexCoord2f(0.0f, 0.0f);
    fVertex2f(-1.0f, -1.0f);
    fTexCoord2f(1.0f, 0.0f);
    fVertex2f(1.0f, -1.0f);
    fTexCoord2f(1.0f, 1.0f);
    fVertex2f(1.0f, 1.0f);
    fTexCoord2f(0.0f, 1.0f);
    fVertex2f(-1.0f, 1.0f);
    fEnd();

    fReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    if (fGetError() != 0) {
        fprintf(stderr, "FAIL: GL error after the textured draw\n");
        return 1;
    }
    if (pixel[1] < 200 || pixel[0] > 50 || pixel[2] > 50) {
        fprintf(stderr, "FAIL: textured pixel is (%u,%u,%u), expected green\n", pixel[0], pixel[1],
                pixel[2]);
        return 1;
    }
    printf("OK: phase 3 GL_MODULATE texturing renders green\n");
    printf("OK: all FPE render phases passed on the real GLES3 device\n");
    return 0;
}
