// SimpleFPEWrapper - tests/smoke_fog.c
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// Numeric fog verification: every fog factor is computed on the CPU from
// the GL 2.1 formulas and compared against what the wrapper renders, so a
// wrong distance, a flipped mix or an ignored state shows up as a number
// rather than as "the fog looks odd". Covers GL_LINEAR / GL_EXP / GL_EXP2
// and both GL_FOG_COORD_SRC sources (GL 1.4 core).
#include <dlfcn.h>
#include <math.h>
#include <stdio.h>
#include <EGL/egl.h>

typedef unsigned int GLenum, GLuint, GLbitfield;
typedef unsigned char GLubyte;
#include <stdlib.h>
typedef float GLfloat;
typedef int GLint, GLsizei;

#define WIN 64
#define GL_QUADS 0x0007
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_RGBA 0x1908
#define GL_UNSIGNED_BYTE 0x1401
#define GL_FOG 0x0B60
#define GL_FOG_MODE 0x0B65
#define GL_FOG_DENSITY 0x0B62
#define GL_FOG_START 0x0B63
#define GL_FOG_END 0x0B64
#define GL_FOG_COLOR 0x0B66
#define GL_FOG_COORD_SRC 0x8450
#define GL_FOG_COORD 0x8451
#define GL_FRAGMENT_DEPTH 0x8452
#define GL_LINEAR 0x2601
#define GL_EXP 0x0800
#define GL_EXP2 0x0801
#define GL_PROJECTION 0x1701
#define GL_MODELVIEW 0x1700

static void* (*resolve)(const char*);
static void (*fClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
static void (*fClear)(GLbitfield);
static void (*fBegin)(GLenum);
static void (*fEnd)(void);
static void (*fColor3f)(GLfloat, GLfloat, GLfloat);
static void (*fVertex3f)(GLfloat, GLfloat, GLfloat);
static void (*fFogCoordf)(GLfloat);
static void (*fReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*);
static void (*fEnable)(GLenum);
static void (*fDisable)(GLenum);
static void (*fFogi)(GLenum, GLint);
static void (*fFogf)(GLenum, GLfloat);
static void (*fFogfv)(GLenum, const GLfloat*);
static void (*fMatrixMode)(GLenum);
static void (*fLoadIdentity)(void);
static void (*fOrtho)(double, double, double, double, double, double);
static GLenum (*fGetError)(void);

static int failures = 0;

// obj color red (1,0,0), fog color cyan (0,1,1): the red channel IS the
// fog factor and green/blue are (1 - factor), so one probe reads both.
static void probe(const char* tag, float expected_factor) {
    GLubyte px[4] = {0, 0, 0, 0};
    fReadPixels(WIN / 2, WIN / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    const float got = px[0] / 255.0f;
    const float diff = fabsf(got - expected_factor);
    printf("%-46s expected f=%.4f  got f=%.4f  %s\n", tag, expected_factor, got,
           diff <= 0.02f ? "OK" : "*** MISMATCH ***");
    if (diff > 0.02f) ++failures;
}

static void draw_at_depth(float eye_depth, float fog_coord, int use_coord) {
    fClear(GL_COLOR_BUFFER_BIT);
    fBegin(GL_QUADS);
    fColor3f(1.0f, 0.0f, 0.0f);
    if (use_coord) fFogCoordf(fog_coord);
    fVertex3f(-1.0f, -1.0f, -eye_depth);
    if (use_coord) fFogCoordf(fog_coord);
    fVertex3f(1.0f, -1.0f, -eye_depth);
    if (use_coord) fFogCoordf(fog_coord);
    fVertex3f(1.0f, 1.0f, -eye_depth);
    if (use_coord) fFogCoordf(fog_coord);
    fVertex3f(-1.0f, 1.0f, -eye_depth);
    fEnd();
}

int main(void) {
    void* h = dlopen(WRAPPER_LIB_PATH, RTLD_NOW | RTLD_LOCAL);
    if (!h) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 1; }
    *(void**)(&resolve) = dlsym(h, "eglGetProcAddress");

    EGLDisplay d = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (d == EGL_NO_DISPLAY || !eglInitialize(d, NULL, NULL)) { printf("SKIP: no EGL display\n"); return 77; }
    const EGLint ca[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE,
                         EGL_OPENGL_ES3_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
                         EGL_ALPHA_SIZE, 8, EGL_NONE};
    EGLConfig c; EGLint n = 0;
    if (!eglChooseConfig(d, ca, &c, 1, &n) || n == 0) { printf("SKIP\n"); return 77; }
    const EGLint pa[] = {EGL_WIDTH, WIN, EGL_HEIGHT, WIN, EGL_NONE};
    EGLSurface s = eglCreatePbufferSurface(d, c, pa);
    eglBindAPI(EGL_OPENGL_ES_API);
    const EGLint xa[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    EGLContext x = eglCreateContext(d, c, EGL_NO_CONTEXT, xa);
    if (!eglMakeCurrent(d, s, s, x)) { printf("SKIP\n"); return 77; }

#define R(v, nm) *(void**)(&v) = resolve(nm); if (!v) { fprintf(stderr, "miss %s\n", nm); return 1; }
    R(fClearColor, "glClearColor") R(fClear, "glClear") R(fBegin, "glBegin") R(fEnd, "glEnd")
    R(fColor3f, "glColor3f") R(fVertex3f, "glVertex3f") R(fReadPixels, "glReadPixels")
    R(fEnable, "glEnable") R(fDisable, "glDisable") R(fFogi, "glFogi") R(fFogf, "glFogf")
    R(fFogfv, "glFogfv") R(fMatrixMode, "glMatrixMode") R(fLoadIdentity, "glLoadIdentity")
    R(fOrtho, "glOrtho") R(fGetError, "glGetError")
    *(void**)(&fFogCoordf) = resolve("glFogCoordf");

    // Ortho so eye-space z is exactly what we pass as the vertex z.
    fMatrixMode(GL_PROJECTION);
    fLoadIdentity();
    fOrtho(-1, 1, -1, 1, -1000, 1000);
    fMatrixMode(GL_MODELVIEW);
    fLoadIdentity();

    static const GLfloat cyan[4] = {0.0f, 1.0f, 1.0f, 1.0f};
    fClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    fEnable(GL_FOG);
    fFogfv(GL_FOG_COLOR, cyan);

    // --- GL_LINEAR: f = (end - c) / (end - start) ---
    fFogi(GL_FOG_MODE, GL_LINEAR);
    fFogf(GL_FOG_START, 0.0f);
    fFogf(GL_FOG_END, 100.0f);
    draw_at_depth(50.0f, 0, 0);
    probe("LINEAR start=0 end=100 depth=50", 0.5f);
    draw_at_depth(25.0f, 0, 0);
    probe("LINEAR start=0 end=100 depth=25", 0.75f);
    fFogf(GL_FOG_START, 20.0f);
    fFogf(GL_FOG_END, 60.0f);
    draw_at_depth(40.0f, 0, 0);
    probe("LINEAR start=20 end=60 depth=40", 0.5f);

    // --- GL_EXP: f = exp(-density * c) ---
    fFogi(GL_FOG_MODE, GL_EXP);
    fFogf(GL_FOG_DENSITY, 0.02f);
    draw_at_depth(50.0f, 0, 0);
    probe("EXP density=0.02 depth=50", expf(-0.02f * 50.0f));
    fFogf(GL_FOG_DENSITY, 0.05f);
    draw_at_depth(20.0f, 0, 0);
    probe("EXP density=0.05 depth=20", expf(-0.05f * 20.0f));

    // --- GL_EXP2: f = exp(-(density * c)^2) ---
    fFogi(GL_FOG_MODE, GL_EXP2);
    fFogf(GL_FOG_DENSITY, 0.02f);
    draw_at_depth(50.0f, 0, 0);
    probe("EXP2 density=0.02 depth=50", expf(-(0.02f * 50.0f) * (0.02f * 50.0f)));

    // --- GL_FOG_COORD_SRC = GL_FOG_COORD: distance comes from glFogCoord ---
    if (fFogCoordf != NULL) {
        fFogi(GL_FOG_MODE, GL_LINEAR);
        fFogf(GL_FOG_START, 0.0f);
        fFogf(GL_FOG_END, 100.0f);
        fFogi(GL_FOG_COORD_SRC, GL_FOG_COORD);
        // eye depth 90 but fog coord 25: a correct implementation uses 25.
        draw_at_depth(90.0f, 25.0f, 1);
        probe("FOG_COORD src: coord=25 (eye depth 90)", 0.75f);
        fFogi(GL_FOG_COORD_SRC, GL_FRAGMENT_DEPTH);
        draw_at_depth(90.0f, 25.0f, 1);
        probe("FRAGMENT_DEPTH src back: depth=90", 0.10f);
    } else {
        fprintf(stderr, "FAIL: glFogCoordf does not resolve (GL 1.4 core)\n");
        ++failures;
    }

    printf("\nGL error: 0x%x\nfailures: %d\n", fGetError(), failures);
    return failures != 0;
}
