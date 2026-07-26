// SimpleFPEWrapper - tests/bench_fpe.c
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// plans/11 Q5 microbenchmarks. Numbers-only: the run never fails on
// timing (CI machines differ wildly); regressions are judged by
// comparing the printed table between commits on the same machine.
//
//   bench.immediate  - glBegin/glEnd vertex throughput (ring VBO path)
//   bench.dlist      - display-list replay of the same load (batching)
//   bench.progcache  - per-draw cost when the FPE state toggles between
//                      two program-cache keys vs. a constant state
//   bench.translate  - GLSL 1.10 -> ESSL translation latency
//
// SFPEW_BENCH_SCALE scales every iteration count (default 1.0; use
// small values on slow/software devices). Exit 0 always, 77 = no device.

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <EGL/egl.h>

typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef float GLfloat;
typedef int GLint, GLsizei;
typedef unsigned int GLbitfield;

#define GL_QUADS 0x0007
#define GL_COMPILE 0x1300
#define GL_LIGHTING 0x0B50
#define GL_COLOR_BUFFER_BIT 0x00004000

static void (*fClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
static void (*fClear)(GLbitfield);
static void (*fBegin)(GLenum);
static void (*fEnd)(void);
static void (*fColor3f)(GLfloat, GLfloat, GLfloat);
static void (*fTexCoord2f)(GLfloat, GLfloat);
static void (*fVertex2f)(GLfloat, GLfloat);
static GLuint (*fGenLists)(GLsizei);
static void (*fNewList)(GLuint, GLenum);
static void (*fEndList)(void);
static void (*fCallList)(GLuint);
static void (*fEnable)(GLenum);
static void (*fDisable)(GLenum);
static void (*fFinish)(void);
static GLenum (*fGetError)(void);
typedef int (*translate_fn)(unsigned int, const char*, char*, int);

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1.0e6;
}

// One 4-vertex colored+textured quad in a small grid cell, immediate mode.
static void emit_quad(int i) {
    const float cell = 0.01f * (float)(i % 100) - 0.5f;
    fColor3f(0.25f, 0.5f, 0.75f);
    fTexCoord2f(0.0f, 0.0f);
    fVertex2f(cell, cell);
    fTexCoord2f(1.0f, 0.0f);
    fVertex2f(cell + 0.01f, cell);
    fTexCoord2f(1.0f, 1.0f);
    fVertex2f(cell + 0.01f, cell + 0.01f);
    fTexCoord2f(0.0f, 1.0f);
    fVertex2f(cell, cell + 0.01f);
}

static void draw_quads(int count) {
    fBegin(GL_QUADS);
    for (int i = 0; i < count; ++i) emit_quad(i);
    fEnd();
}

int main(void) {
    void* handle = dlopen(WRAPPER_LIB_PATH, RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        fprintf(stderr, "FAIL: dlopen: %s\n", dlerror());
        return 1;
    }
    typedef void* (*resolver_t)(const char*);
    resolver_t resolve = (resolver_t)dlsym(handle, "eglGetProcAddress");
    translate_fn translate = (translate_fn)dlsym(handle, "sfpewTranslateGlslForTest");
    if (!resolve || !translate) return 1;

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
    static const EGLint pbuffer_attribs[] = {EGL_WIDTH, 256, EGL_HEIGHT, 256, EGL_NONE};
    EGLSurface surface = eglCreatePbufferSurface(display, config, pbuffer_attribs);
    eglBindAPI(EGL_OPENGL_ES_API);
    static const EGLint ctx_attribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, ctx_attribs);
    if (surface == EGL_NO_SURFACE || context == EGL_NO_CONTEXT ||
        !eglMakeCurrent(display, surface, surface, context)) {
        printf("SKIP: could not make an ES3 pbuffer context current\n");
        return 77;
    }

#define R(dst, name)                                                                               \
    do {                                                                                           \
        *(void**)(&dst) = resolve(name);                                                           \
        if (!dst) {                                                                                \
            fprintf(stderr, "FAIL: cannot resolve %s\n", name);                                    \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
    R(fClearColor, "glClearColor");
    R(fClear, "glClear");
    R(fBegin, "glBegin");
    R(fEnd, "glEnd");
    R(fColor3f, "glColor3f");
    R(fTexCoord2f, "glTexCoord2f");
    R(fVertex2f, "glVertex2f");
    R(fGenLists, "glGenLists");
    R(fNewList, "glNewList");
    R(fEndList, "glEndList");
    R(fCallList, "glCallList");
    R(fEnable, "glEnable");
    R(fDisable, "glDisable");
    R(fFinish, "glFinish");
    R(fGetError, "glGetError");

    double scale = 1.0;
    const char* scale_env = getenv("SFPEW_BENCH_SCALE");
    if (scale_env != NULL) scale = atof(scale_env);
    if (scale <= 0.0) scale = 1.0;

    fClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    fClear(GL_COLOR_BUFFER_BIT);

    // --- bench.immediate ------------------------------------------------
    const int quads_per_frame = 1000;
    const int frames = (int)(400 * scale) > 0 ? (int)(400 * scale) : 1;
    draw_quads(quads_per_frame); // warmup: first draw compiles the program
    fFinish();
    double t0 = now_ms();
    for (int frm = 0; frm < frames; ++frm) draw_quads(quads_per_frame);
    fFinish();
    double immediate_ms = now_ms() - t0;
    const double verts = (double)frames * quads_per_frame * 4;
    printf("bench.immediate: %7.2f Mvert/s  (%6.1f ns/vert, %d frames x %d quads)\n",
           verts / immediate_ms / 1000.0, immediate_ms * 1.0e6 / verts, frames, quads_per_frame);

    // --- bench.dlist ------------------------------------------------------
    const GLuint list = fGenLists(1);
    fNewList(list, GL_COMPILE);
    draw_quads(quads_per_frame);
    fEndList();
    fCallList(list); // warmup replay
    fFinish();
    t0 = now_ms();
    for (int frm = 0; frm < frames; ++frm) fCallList(list);
    fFinish();
    double dlist_ms = now_ms() - t0;
    printf("bench.dlist:     %7.2f Mvert/s  (%6.1f ns/vert, replay speedup %.2fx)\n",
           verts / dlist_ms / 1000.0, dlist_ms * 1.0e6 / verts,
           dlist_ms > 0.0 ? immediate_ms / dlist_ms : 0.0);

    // --- bench.progcache --------------------------------------------------
    // Constant state: every draw resolves the SAME program-cache key.
    const int draws = (int)(10000 * scale) > 0 ? (int)(10000 * scale) : 1;
    draw_quads(1);
    fFinish();
    t0 = now_ms();
    for (int i = 0; i < draws; ++i) draw_quads(1);
    fFinish();
    const double steady_ms = now_ms() - t0;
    // Toggling state: alternating keys, still 100% cache hits after the
    // first two draws - measures hash+lookup+uniform-refresh overhead.
    fEnable(GL_LIGHTING);
    draw_quads(1);
    fDisable(GL_LIGHTING);
    fFinish();
    t0 = now_ms();
    for (int i = 0; i < draws; ++i) {
        if (i & 1) fEnable(GL_LIGHTING);
        else fDisable(GL_LIGHTING);
        draw_quads(1);
    }
    fFinish();
    const double toggle_ms = now_ms() - t0;
    fDisable(GL_LIGHTING);
    printf("bench.progcache: %7.2f us/draw steady, %7.2f us/draw toggling (%d draws)\n",
           steady_ms * 1000.0 / draws, toggle_ms * 1000.0 / draws, draws);

    // --- bench.translate ---------------------------------------------------
    static const char* kVS =
        "#version 110\n"
        "varying vec2 uv;\n"
        "void main() {\n"
        "    uv = (gl_TextureMatrix[0] * gl_MultiTexCoord0).xy;\n"
        "    gl_FrontColor = gl_Color * gl_LightSource[0].diffuse;\n"
        "    gl_Position = ftransform();\n"
        "}\n";
    static const char* kFS =
        "#version 110\n"
        "uniform sampler2D tex;\n"
        "varying vec2 uv;\n"
        "void main() {\n"
        "    vec4 c = texture2D(tex, uv) * gl_Color;\n"
        "    float fog = clamp((gl_Fog.end - gl_FogFragCoord) * gl_Fog.scale, 0.0, 1.0);\n"
        "    gl_FragColor = mix(gl_Fog.color, c, fog);\n"
        "}\n";
    static char out_buf[1 << 16];
    const int pairs = (int)(20 * scale) > 0 ? (int)(20 * scale) : 1;
    if (translate(0x8B31, kVS, out_buf, sizeof(out_buf)) != 0 ||
        translate(0x8B30, kFS, out_buf, sizeof(out_buf)) != 0) { // warmup + sanity
        fprintf(stderr, "FAIL: translation benchmark shader does not translate\n");
        return 1;
    }
    t0 = now_ms();
    for (int i = 0; i < pairs; ++i) {
        translate(0x8B31, kVS, out_buf, sizeof(out_buf));
        translate(0x8B30, kFS, out_buf, sizeof(out_buf));
    }
    const double translate_ms = now_ms() - t0;
    printf("bench.translate: %7.2f ms/shader (GLSL 1.10 pair x %d)\n",
           translate_ms / (pairs * 2), pairs);

    if (fGetError() != 0) {
        fprintf(stderr, "FAIL: GL error raised during benchmarks\n");
        return 1;
    }
    return 0;
}
