// SimpleFPEWrapper - tests/smoke_mixed_translation_link.c
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// Program-level native pass-through:
//
//   all-native program -> magenta
//     Both shaders are #version 300 es and directly acceptable to a GLES
//     backend. The fragment deliberately redefines the prelude's
//     `fpe_FogParameters` STRUCT, so translating it would fail; it must be
//     left as the original native source.
//
//   mixed program -> green
//     A desktop #version 120 vertex shader is paired with an ESSL 300 es
//     fragment. The program is not entirely pass-through, so the native
//     fragment is re-translated to the backend target at link time and the
//     program links from one uniform version.
//
// Probe colors keep R == B: llvmpipe swaps R/B in fragment output on
// surfaceless BGRA pbuffer configs (a driver bug, reproduced without the
// wrapper), and symmetric colors are immune without losing discrimination.
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
typedef unsigned char GLboolean;
typedef char GLchar;

#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_TRIANGLES 0x0004
#define GL_FLOAT 0x1406
#define GL_RGBA 0x1908
#define GL_UNSIGNED_BYTE 0x1401
#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_VERTEX_ARRAY 0x8074
#define GL_FOG_COLOR 0x0B66
#define GL_NO_ERROR 0

static void (*fClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
static void (*fClear)(GLbitfield);
static void (*fDrawArrays)(GLenum, GLint, GLsizei);
static void (*fReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*);
static GLenum (*fGetError)(void);
static void (*fFinish)(void);
static void (*fUseProgram)(GLuint);
static GLuint (*fCreateShader)(GLenum);
static void (*fShaderSource)(GLuint, GLsizei, const GLchar* const*, const GLint*);
static void (*fCompileShader)(GLuint);
static void (*fGetShaderiv)(GLuint, GLenum, GLint*);
static GLuint (*fCreateProgram)(void);
static void (*fAttachShader)(GLuint, GLuint);
static void (*fLinkProgram)(GLuint);
static void (*fGetProgramiv)(GLuint, GLenum, GLint*);
static void (*fGetProgramInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*);
static void (*fEnableClientState)(GLenum);
static void (*fDisableClientState)(GLenum);
static void (*fVertexPointer)(GLint, GLenum, GLsizei, const void*);
static void (*fFogfv)(GLenum, const GLfloat*);

static int failures;

static void expect(int r, int g, int b, const char* what) {
    GLubyte p[4] = {0, 0, 0, 0};
    fReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p);
    const int ok = ((p[0] > 200) == (r > 0)) && ((p[1] > 200) == (g > 0)) &&
                   ((p[2] > 200) == (b > 0));
    if (!ok) {
        fprintf(stderr, "FAIL: %s: pixel = (%u,%u,%u), expected (%d,%d,%d)\n", what, p[0], p[1],
                p[2], r, g, b);
        ++failures;
    } else {
        printf("OK: %s\n", what);
    }
}

static GLuint compileShader(GLenum stage, const char* src, const char* what) {
    GLuint sh = fCreateShader(stage);
    fShaderSource(sh, 1, &src, NULL);
    fCompileShader(sh);
    GLint ok = 0;
    fGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        fprintf(stderr, "FAIL: %s compile\n", what);
        ++failures;
    }
    return sh;
}

static int linkOk(GLuint prog, const char* what) {
    fLinkProgram(prog);
    GLint ok = 0;
    fGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512] = {0};
        fGetProgramInfoLog(prog, sizeof log, NULL, log);
        fprintf(stderr, "FAIL: %s link: %s\n", what, log);
        ++failures;
        return 0;
    }
    return 1;
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
    R(fClearColor,"glClearColor") R(fClear,"glClear") R(fDrawArrays,"glDrawArrays")
    R(fReadPixels,"glReadPixels") R(fGetError,"glGetError") R(fFinish,"glFinish")
    R(fUseProgram,"glUseProgram") R(fCreateShader,"glCreateShader")
    R(fShaderSource,"glShaderSource") R(fCompileShader,"glCompileShader")
    R(fGetShaderiv,"glGetShaderiv") R(fCreateProgram,"glCreateProgram")
    R(fAttachShader,"glAttachShader") R(fLinkProgram,"glLinkProgram")
    R(fGetProgramiv,"glGetProgramiv") R(fGetProgramInfoLog,"glGetProgramInfoLog")
    R(fEnableClientState,"glEnableClientState")
    R(fDisableClientState,"glDisableClientState")
    R(fVertexPointer,"glVertexPointer") R(fFogfv,"glFogfv")
#undef R

    // Native ESSL 300 es (self-declared fpe_Vertex yields to the prelude).
    static const char* vsNativeSrc =
        "#version 300 es\n"
        "in vec4 fpe_Vertex;\n"
        "void main() { gl_Position = fpe_Vertex; }\n";
    // Redefines the prelude's STRUCT type, so translation fails; the
    // backend-legal original must be passed through instead.
    static const char* fsPassSrc =
        "#version 300 es\n"
        "precision mediump float;\n"
        "struct fpe_FogParameters { vec4 color; };\n"
        "uniform fpe_FogParameters fpe_Fog;\n"
        "out vec4 o;\n"
        "void main() { o = vec4(fpe_Fog.color.rgb, 1.0); }\n";
    // Desktop GLSL that cannot pass through to a GLES backend.
    static const char* vsDesktopSrc =
        "#version 120\n"
        "void main() { gl_Position = ftransform(); }\n";
    // Native ESSL 300 es that also translates cleanly.
    static const char* fsGreenSrc =
        "#version 300 es\n"
        "precision mediump float;\n"
        "out vec4 o;\n"
        "void main() { o = vec4(0.0, 1.0, 0.0, 1.0); }\n";

    GLuint vsNative = compileShader(GL_VERTEX_SHADER, vsNativeSrc, "VS (native)");
    GLuint fsPass = compileShader(GL_FRAGMENT_SHADER, fsPassSrc, "FS (native pass-through)");
    GLuint vsDesktop = compileShader(GL_VERTEX_SHADER, vsDesktopSrc, "VS (desktop 120)");
    GLuint fsGreen = compileShader(GL_FRAGMENT_SHADER, fsGreenSrc, "FS (native+translatable)");
    if (failures) return 1;

    GLuint progNative = fCreateProgram();
    fAttachShader(progNative, vsNative); fAttachShader(progNative, fsPass);
    GLuint progMixed = fCreateProgram();
    fAttachShader(progMixed, vsDesktop); fAttachShader(progMixed, fsGreen);

    static const GLfloat pos[] = { -1,-1,  1,-1,  1,1,   -1,-1,  1,1,  -1,1 };
    fVertexPointer(2, GL_FLOAT, 0, pos);
    fEnableClientState(GL_VERTEX_ARRAY);
    fClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    static const GLfloat magenta[] = {1.0f, 0.0f, 1.0f, 1.0f};
    fFogfv(GL_FOG_COLOR, magenta);

    for (int round = 1; round <= 2; ++round) {
        char what[128];
        snprintf(what, sizeof what, "round %d: all-native program links from original sources",
                 round);
        if (linkOk(progNative, what)) {
            fUseProgram(progNative);
            fClear(GL_COLOR_BUFFER_BIT);
            fDrawArrays(GL_TRIANGLES, 0, 6);
            fFinish();
            snprintf(what, sizeof what, "round %d: all-native program draws the fed fog color",
                     round);
            expect(1, 0, 1, what);
        }
        snprintf(what, sizeof what, "round %d: mixed program links after re-translating all",
                 round);
        if (linkOk(progMixed, what)) {
            fUseProgram(progMixed);
            fClear(GL_COLOR_BUFFER_BIT);
            fDrawArrays(GL_TRIANGLES, 0, 6);
            fFinish();
            snprintf(what, sizeof what, "round %d: mixed program draws green", round);
            expect(0, 1, 0, what);
        }
    }

    const GLenum err = fGetError();
    if (err != GL_NO_ERROR) {
        fprintf(stderr, "FAIL: glGetError 0x%x\n", err); ++failures; }

    fDisableClientState(GL_VERTEX_ARRAY);
    fUseProgram(0);
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(display, context);
    eglDestroySurface(display, surface);
    eglTerminate(display);

    if (failures) {
        fprintf(stderr, "FAIL: %d check(s) failed\n", failures);
        return 1;
    }
    printf("OK: native and re-translated programs link consistently\n");
    return 0;
}
