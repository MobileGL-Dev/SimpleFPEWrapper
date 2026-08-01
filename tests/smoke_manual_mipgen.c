// SimpleFPEWrapper - tests/smoke_manual_mipgen.c
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// glGenerateMipmap is not a passthrough: after the backend call the wrapper
// rebuilds the 2D chain level by level with blits, because the mobile driver
// was caught leaving the HIGH levels of an NPOT render-target chain STALE -
// level 4 tracked the freshly rendered base while level 8 still held an image
// from seconds earlier. A shader pack's auto-exposure averaged the scene from
// level 6 of that chain, so the leftovers crushed the exposure and the screen
// went dead dark from some view angles.
//
// The repair must not corrupt what a healthy driver produces, so on a desktop
// GL stack this checks, for an NPOT texture sized to hit odd level dimensions:
//  - every level of the chain reads back the base's solid colour;
//  - a SECOND upload + mipgen replaces every level (no stale content - on the
//    device this is the part the driver got wrong);
//  - framebuffer bindings and the scissor switch survive the wrapped call.
//
// Skips (77) when the machine has no EGL device.

#include <dlfcn.h>
#include <stdio.h>

#include <EGL/egl.h>

typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef unsigned char GLubyte;
typedef unsigned char GLboolean;
typedef float GLfloat;
typedef int GLint, GLsizei;
typedef unsigned int GLbitfield;

#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_LINEAR 0x2601
#define GL_LINEAR_MIPMAP_LINEAR 0x2703
#define GL_RGBA 0x1908
#define GL_UNSIGNED_BYTE 0x1401
#define GL_READ_FRAMEBUFFER 0x8CA8
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#define GL_READ_FRAMEBUFFER_BINDING 0x8CAA
#define GL_DRAW_FRAMEBUFFER_BINDING 0x8CA6
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_SCISSOR_TEST 0x0C11
#define GL_NO_ERROR 0

#define TEX_W 300
#define TEX_H 135

static void (*fGenTextures)(GLsizei, GLuint*);
static void (*fBindTexture)(GLenum, GLuint);
static void (*fTexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum,
                           const void*);
static void (*fTexParameteri)(GLenum, GLenum, GLint);
static void (*fGenerateMipmap)(GLenum);
static void (*fGenFramebuffers)(GLsizei, GLuint*);
static void (*fBindFramebuffer)(GLenum, GLuint);
static void (*fFramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint);
static void (*fReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*);
static void (*fGetIntegerv)(GLenum, GLint*);
static void (*fEnable)(GLenum);
static GLboolean (*fIsEnabled)(GLenum);
static GLenum (*fGetError)(void);
static void (*fFinish)(void);

static int failures;

static GLubyte level0[TEX_W * TEX_H * 4];

static void fillLevel0(GLubyte r, GLubyte g, GLubyte b) {
    for (int i = 0; i < TEX_W * TEX_H; ++i) {
        level0[i * 4 + 0] = r;
        level0[i * 4 + 1] = g;
        level0[i * 4 + 2] = b;
        level0[i * 4 + 3] = 255;
    }
}

// Solid input, so every level of a correct chain reads the same colour to
// within blit rounding.
static void expectChain(GLuint fbo, GLuint tex, GLubyte r, GLubyte g, GLubyte b,
                        const char* what) {
    fBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
    int w = TEX_W, h = TEX_H;
    for (int level = 1; w > 1 || h > 1; ++level) {
        w = w > 1 ? w >> 1 : 1;
        h = h > 1 ? h >> 1 : 1;
        fFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex,
                              level);
        GLubyte p[4] = {0, 0, 0, 0};
        fReadPixels(w / 2, h / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p);
        const int ok = p[0] > r - 12 && p[0] < r + 12 && p[1] > g - 12 && p[1] < g + 12 &&
                       p[2] > b - 12 && p[2] < b + 12;
        if (!ok) {
            fprintf(stderr, "FAIL: %s: level %d = (%u,%u,%u), expected ~(%u,%u,%u)\n", what,
                    level, p[0], p[1], p[2], r, g, b);
            ++failures;
        }
    }
    fFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
    fBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    printf("OK: %s\n", what);
}

int main(void) {
    void* handle = dlopen(WRAPPER_LIB_PATH, RTLD_NOW | RTLD_LOCAL);
    if (!handle) { fprintf(stderr, "FAIL: dlopen: %s\n", dlerror()); return 1; }
    typedef void* (*resolver_t)(const char*);
    resolver_t resolve = (resolver_t)dlsym(handle, "eglGetProcAddress");
    if (!resolve) return 1;

    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display == EGL_NO_DISPLAY || !eglInitialize(display, NULL, NULL)) {
        printf("SKIP: no EGL display\n"); return 77; }
    static const EGLint cfg[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE,
        EGL_OPENGL_ES3_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8, EGL_NONE};
    EGLConfig config; EGLint n = 0;
    if (!eglChooseConfig(display, cfg, &config, 1, &n) || n == 0) {
        printf("SKIP: no ES3 config\n"); return 77; }
    static const EGLint pb[] = {EGL_WIDTH, 64, EGL_HEIGHT, 64, EGL_NONE};
    EGLSurface surface = eglCreatePbufferSurface(display, config, pb);
    eglBindAPI(EGL_OPENGL_ES_API);
    static const EGLint ca[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, ca);
    if (surface == EGL_NO_SURFACE || context == EGL_NO_CONTEXT ||
        !eglMakeCurrent(display, surface, surface, context)) {
        printf("SKIP: could not make ES3 context current\n"); return 77; }

#define R(fn, s) fn = (typeof(fn))resolve(s); if (!fn) { fprintf(stderr, "FAIL: missing %s\n", s); return 1; }
    R(fGenTextures,"glGenTextures") R(fBindTexture,"glBindTexture")
    R(fTexImage2D,"glTexImage2D") R(fTexParameteri,"glTexParameteri")
    R(fGenerateMipmap,"glGenerateMipmap")
    R(fGenFramebuffers,"glGenFramebuffers") R(fBindFramebuffer,"glBindFramebuffer")
    R(fFramebufferTexture2D,"glFramebufferTexture2D")
    R(fReadPixels,"glReadPixels") R(fGetIntegerv,"glGetIntegerv")
    R(fEnable,"glEnable") R(fIsEnabled,"glIsEnabled")
    R(fGetError,"glGetError") R(fFinish,"glFinish")
#undef R

    GLuint tex = 0;
    fGenTextures(1, &tex);
    fBindTexture(GL_TEXTURE_2D, tex);
    fTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    fTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // Allocate every level up front, filled with magenta garbage. This is the
    // stale-driver simulation: with SFPEW_MANUAL_MIPGEN=only the wrapper
    // skips the backend mipgen entirely, so ONLY the wrapper's blit chain can
    // replace this garbage - any level it misses fails the colour checks.
    fillLevel0(255, 0, 255);
    {
        int w = TEX_W, h = TEX_H;
        for (int level = 1; w > 1 || h > 1; ++level) {
            w = w > 1 ? w >> 1 : 1;
            h = h > 1 ? h >> 1 : 1;
            fTexImage2D(GL_TEXTURE_2D, level, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                        level0);
        }
    }

    GLuint fbo = 0;
    fGenFramebuffers(1, &fbo);

    // The wrapped call must put the framebuffer bindings and the scissor
    // switch back exactly as they were.
    fEnable(GL_SCISSOR_TEST);

    fillLevel0(200, 60, 20);
    fTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, TEX_W, TEX_H, 0, GL_RGBA, GL_UNSIGNED_BYTE, level0);

    // llvmpipe rebuilds the texture's storage at the next validation point
    // after the level-by-level definitions above, repopulating from the
    // uploads - which would silently discard whatever the repair blits wrote
    // in between. Force that validation FIRST with a top-level read, the way
    // the storage is already settled in the real per-frame use (the levels
    // exist long before each regeneration).
    {
        fBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
        fFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 8);
        GLubyte warm[4];
        fReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, warm);
        fFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
        fBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    }

    fGenerateMipmap(GL_TEXTURE_2D);
    fFinish();
    expectChain(fbo, tex, 200, 60, 20, "first chain carries the base colour to the top");

    // Re-upload and regenerate: every level must FOLLOW. Stale high levels
    // keeping the first colour is exactly the device failure.
    fillLevel0(20, 180, 220);
    fTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, TEX_W, TEX_H, 0, GL_RGBA, GL_UNSIGNED_BYTE, level0);
    fGenerateMipmap(GL_TEXTURE_2D);
    fFinish();
    expectChain(fbo, tex, 20, 180, 220, "regenerated chain follows the new base");

    if (!fIsEnabled(GL_SCISSOR_TEST)) {
        fprintf(stderr, "FAIL: scissor enable did not survive glGenerateMipmap\n");
        ++failures;
    }
    GLint read_fb = -1, draw_fb = -1;
    fGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read_fb);
    fGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw_fb);
    if (read_fb != 0 || draw_fb != 0) {
        fprintf(stderr, "FAIL: framebuffer bindings leaked: read=%d draw=%d\n", read_fb,
                draw_fb);
        ++failures;
    }

    const GLenum err = fGetError();
    if (err != GL_NO_ERROR) {
        fprintf(stderr, "FAIL: glGetError 0x%x\n", err); ++failures; }

    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(display, context);
    eglDestroySurface(display, surface);
    eglTerminate(display);

    if (failures) {
        fprintf(stderr, "FAIL: %d check(s) failed\n", failures);
        return 1;
    }
    printf("OK: the rebuilt mip chain is complete and fresh\n");
    return 0;
}
