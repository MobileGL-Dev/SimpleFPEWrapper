// SimpleFPEWrapper - tools/glbench.c
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// Fixed-function CPU-overhead benchmark for ANY library that exports the
// GL 1.x entry points: SimpleFPEWrapper, gl4es, a system libGL, Mesa, a
// vendor driver. Nothing here is specific to this project - the workload
// resolves every entry point with dlsym against the library under test and
// never links a system libGL, so the same binary measures all of them and
// the numbers are directly comparable.
//
// Deliberately standalone rather than a ctest: it takes a library path, and
// comparing two implementations means running it twice. tools/glbench-compare.sh
// drives that and prints the ratio table.
//
//   CMPBENCH_LIB=<path to .so>   library under test (required)
//   CMPBENCH_CTX=egl|glx         how to obtain the backing context
//   CMPBENCH_SCALE=<float>       iteration scale (default 1.0)
//   CMPBENCH_ONLY=<substring>    run only matching phases
//   CMPBENCH_VIEWPORT=<n>        square viewport; 1 (default) removes
//                                fragment cost so only CPU overhead shows
//   CMPBENCH_TSV=1               emit "phase<TAB>value<TAB>unit" for scripts
//
// Phases fall in two groups. The first nine isolate one mechanism each
// (immediate, dlist, clientarrays, drawelements, tinybatch, progtoggle,
// matrixops, getter, texswitch). The mc* ones reproduce the GL 1.x call
// sequences Minecraft issues, so a library can be judged on the shape of
// work a real app produces rather than on microbenchmarks alone; see
// plans/12-fpe-draw-cost.md for what each reconstructs.
//
// Every phase uses only GL 1.x calls that any of these libraries implements,
// so a phase that prints nothing means an entry point was missing, not that
// the library was excused from the work.

#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef unsigned int GLenum, GLuint, GLbitfield;
typedef unsigned char GLubyte, GLboolean;
typedef float GLfloat;
typedef double GLdouble;
typedef int GLint, GLsizei;

#define GL_QUADS 0x0007
#define GL_TRIANGLES 0x0004
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_DEPTH_BUFFER_BIT 0x00000100
#define GL_RGBA 0x1908
#define GL_UNSIGNED_BYTE 0x1401
#define GL_UNSIGNED_SHORT 0x1403
#define GL_FLOAT 0x1406
#define GL_MODELVIEW 0x1700
#define GL_PROJECTION 0x1701
#define GL_MODELVIEW_MATRIX 0x0BA6
#define GL_TEXTURE_2D 0x0DE1
#define GL_VERTEX_ARRAY 0x8074
#define GL_COLOR_ARRAY 0x8076
#define GL_TEXTURE_COORD_ARRAY 0x8078
#define GL_LIGHTING 0x0B50
#define GL_FOG 0x0B60
#define GL_ALPHA_TEST 0x0BC0
#define GL_COMPILE 0x1300
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_NEAREST 0x2600

static void* lib;
static void* backend_gles;
static void* (*lib_get_proc)(const char*);

// The resolution order a real app ends up with: the wrapper's own exported
// symbol, then its eglGetProcAddress (which is how a wrapper hands over
// entry points it only forwards), then the backend GLES library. gl4es
// exports everything directly and never needs the later steps.
static void* sym(const char* n) {
    void* p = dlsym(lib, n);
    if (p) return p;
    if (lib_get_proc) {
        p = lib_get_proc(n);
        if (p) return p;
    }
    if (backend_gles) {
        p = dlsym(backend_gles, n);
        if (p) return p;
    }
    return NULL;
}
#define LOAD(v, n)                                                                                 \
    do {                                                                                           \
        *(void**)(&v) = sym(n);                                                                     \
        if (!v) {                                                                                   \
            fprintf(stderr, "MISSING %s\n", n);                                                     \
            missing++;                                                                              \
        }                                                                                           \
    } while (0)

static int missing = 0;
static const char* only = NULL;
static int phase_on(const char* name) { return only == NULL || strstr(name, only) != NULL; }

// One result line. Human form by default; CMPBENCH_TSV=1 switches to
// "phase<TAB>value<TAB>unit" so a driver script can diff two runs without
// parsing column widths.
static int tsv_mode = 0;
static void report(const char* phase, double value, const char* unit) {
    if (tsv_mode) printf("%s\t%.4f\t%s\n", phase, value, unit);
    else printf("%-14s %10.2f %s\n", phase, value, unit);
    fflush(stdout);
}

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1.0e6;
}

/* GL entry points */
static void (*fClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
static void (*fClear)(GLbitfield);
static void (*fFinish)(void);
static void (*fViewport)(GLint, GLint, GLsizei, GLsizei);
static void (*fBegin)(GLenum);
static void (*fEnd)(void);
static void (*fVertex3f)(GLfloat, GLfloat, GLfloat);
static void (*fColor4f)(GLfloat, GLfloat, GLfloat, GLfloat);
static void (*fTexCoord2f)(GLfloat, GLfloat);
static void (*fPushMatrix)(void);
static void (*fPopMatrix)(void);
static void (*fTranslatef)(GLfloat, GLfloat, GLfloat);
static void (*fRotatef)(GLfloat, GLfloat, GLfloat, GLfloat);
static void (*fMatrixMode)(GLenum);
static void (*fLoadIdentity)(void);
static void (*fGetFloatv)(GLenum, GLfloat*);
static void (*fEnable)(GLenum);
static void (*fDisable)(GLenum);
static void (*fEnableClientState)(GLenum);
static void (*fDisableClientState)(GLenum);
static void (*fVertexPointer)(GLint, GLenum, GLsizei, const void*);
static void (*fColorPointer)(GLint, GLenum, GLsizei, const void*);
static void (*fTexCoordPointer)(GLint, GLenum, GLsizei, const void*);
static void (*fDrawArrays)(GLenum, GLint, GLsizei);
static void (*fDrawElements)(GLenum, GLsizei, GLenum, const void*);
static GLuint (*fGenLists)(GLsizei);
static void (*fNewList)(GLuint, GLenum);
static void (*fEndList)(void);
static void (*fCallList)(GLuint);
static void (*fGenTextures)(GLsizei, GLuint*);
static void (*fBindTexture)(GLenum, GLuint);
static void (*fTexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum,
                           const void*);
static void (*fTexSubImage2D)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum,
                              const void*);
static void (*fTexParameteri)(GLenum, GLenum, GLint);
static void (*fReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*);
static const GLubyte* (*fGetString)(GLenum);

static void load_gl(void) {
    LOAD(fClearColor, "glClearColor");
    LOAD(fClear, "glClear");
    LOAD(fFinish, "glFinish");
    LOAD(fViewport, "glViewport");
    LOAD(fBegin, "glBegin");
    LOAD(fEnd, "glEnd");
    LOAD(fVertex3f, "glVertex3f");
    LOAD(fColor4f, "glColor4f");
    LOAD(fTexCoord2f, "glTexCoord2f");
    LOAD(fPushMatrix, "glPushMatrix");
    LOAD(fPopMatrix, "glPopMatrix");
    LOAD(fTranslatef, "glTranslatef");
    LOAD(fRotatef, "glRotatef");
    LOAD(fMatrixMode, "glMatrixMode");
    LOAD(fLoadIdentity, "glLoadIdentity");
    LOAD(fGetFloatv, "glGetFloatv");
    LOAD(fEnable, "glEnable");
    LOAD(fDisable, "glDisable");
    LOAD(fEnableClientState, "glEnableClientState");
    LOAD(fDisableClientState, "glDisableClientState");
    LOAD(fVertexPointer, "glVertexPointer");
    LOAD(fColorPointer, "glColorPointer");
    LOAD(fTexCoordPointer, "glTexCoordPointer");
    LOAD(fDrawArrays, "glDrawArrays");
    LOAD(fDrawElements, "glDrawElements");
    LOAD(fGenLists, "glGenLists");
    LOAD(fNewList, "glNewList");
    LOAD(fEndList, "glEndList");
    LOAD(fCallList, "glCallList");
    LOAD(fGenTextures, "glGenTextures");
    LOAD(fBindTexture, "glBindTexture");
    LOAD(fTexImage2D, "glTexImage2D");
    LOAD(fTexSubImage2D, "glTexSubImage2D");
    LOAD(fTexParameteri, "glTexParameteri");
    LOAD(fReadPixels, "glReadPixels");
    LOAD(fGetString, "glGetString");
}

/* ---------------- context creation: EGL or GLX, via the library ------- */
typedef void* EGLDisplay;
typedef void* EGLSurface;
typedef void* EGLContext;
typedef void* EGLConfig;
typedef int EGLint;
typedef unsigned int EGLBoolean;

static int make_context_egl(void) {
    void* egl = dlopen("libEGL.so.1", RTLD_NOW | RTLD_GLOBAL);
    if (!egl) { fprintf(stderr, "no libEGL: %s\n", dlerror()); return 0; }
    EGLDisplay (*getDisplay)(void*) = dlsym(egl, "eglGetDisplay");
    EGLBoolean (*initialize)(EGLDisplay, EGLint*, EGLint*) = dlsym(egl, "eglInitialize");
    EGLBoolean (*chooseConfig)(EGLDisplay, const EGLint*, EGLConfig*, EGLint, EGLint*) =
        dlsym(egl, "eglChooseConfig");
    EGLSurface (*createPbuffer)(EGLDisplay, EGLConfig, const EGLint*) =
        dlsym(egl, "eglCreatePbufferSurface");
    EGLBoolean (*bindAPI)(EGLint) = dlsym(egl, "eglBindAPI");
    EGLContext (*createContext)(EGLDisplay, EGLConfig, EGLContext, const EGLint*) =
        dlsym(egl, "eglCreateContext");
    EGLBoolean (*makeCurrentReal)(EGLDisplay, EGLSurface, EGLSurface, EGLContext) =
        dlsym(egl, "eglMakeCurrent");
    if (!getDisplay || !initialize || !chooseConfig || !createPbuffer || !createContext ||
        !makeCurrentReal)
        return 0;

    EGLDisplay d = getDisplay((void*)0);
    if (!initialize(d, NULL, NULL)) return 0;
    const EGLint ca[] = {0x3033 /*SURFACE_TYPE*/, 0x0001 /*PBUFFER*/,
                         0x3040 /*RENDERABLE_TYPE*/, 0x0040 /*ES3*/,
                         0x3024, 8, 0x3023, 8, 0x3022, 8, 0x3021, 8, 0x3038};
    EGLConfig c;
    EGLint n = 0;
    if (!chooseConfig(d, ca, &c, 1, &n) || n == 0) return 0;
    const EGLint pa[] = {0x3057 /*WIDTH*/, 256, 0x3056 /*HEIGHT*/, 256, 0x3038};
    EGLSurface s = createPbuffer(d, c, pa);
    if (bindAPI) bindAPI(0x30A0 /*OPENGL_ES_API*/);
    const EGLint xa[] = {0x3098 /*CONTEXT_CLIENT_VERSION*/, 3, 0x3038};
    EGLContext x = createContext(d, c, NULL, xa);
    if (!s || !x) return 0;
    // Prefer the library's own eglMakeCurrent when it exports one: that is
    // how a launcher wires EGL through the wrapper.
    EGLBoolean (*makeCurrent)(EGLDisplay, EGLSurface, EGLSurface, EGLContext) =
        (EGLBoolean(*)(EGLDisplay, EGLSurface, EGLSurface, EGLContext))dlsym(lib,
                                                                            "eglMakeCurrent");
    const char* routed = "library";
    if (!makeCurrent) { makeCurrent = makeCurrentReal; routed = "libEGL directly"; }
    if (!makeCurrent(d, s, s, x)) return 0;
    if (!tsv_mode) printf("# context: EGL pbuffer 256x256, eglMakeCurrent via %s\n", routed);
    return 1;
}

static int make_context_glx(void) {
    void* xlib = dlopen("libX11.so.6", RTLD_NOW | RTLD_GLOBAL);
    if (!xlib) { fprintf(stderr, "no libX11\n"); return 0; }
    void* (*XOpenDisplay)(const char*) = dlsym(xlib, "XOpenDisplay");
    unsigned long (*XDefaultRootWindow)(void*) = dlsym(xlib, "XDefaultRootWindow");
    if (!XOpenDisplay) return 0;
    void* dpy = XOpenDisplay(NULL);
    if (!dpy) { fprintf(stderr, "cannot open X display\n"); return 0; }

    // GLX comes from the library under test (gl4es exports glX*).
    void* (*ChooseVisual)(void*, int, int*) = sym("glXChooseVisual");
    void* (*CreateContext)(void*, void*, void*, int) = sym("glXCreateContext");
    int (*MakeCurrent)(void*, unsigned long, void*) = sym("glXMakeCurrent");
    void* (*CreatePbuffer)(void*, void*, const int*) = sym("glXCreatePbuffer");
    if (!ChooseVisual || !CreateContext || !MakeCurrent) {
        fprintf(stderr, "library exports no usable GLX\n");
        return 0;
    }
    int attribs[] = {4 /*GLX_RGBA*/, 8 /*RED_SIZE*/, 8, 9 /*GREEN*/, 8, 10 /*BLUE*/, 8,
                     12 /*DEPTH*/, 16, 0 /*None*/};
    void* vi = ChooseVisual(dpy, 0, attribs);
    if (!vi) { fprintf(stderr, "glXChooseVisual failed\n"); return 0; }
    void* ctx = CreateContext(dpy, vi, NULL, 1);
    if (!ctx) { fprintf(stderr, "glXCreateContext failed\n"); return 0; }
    const unsigned long root = XDefaultRootWindow ? XDefaultRootWindow(dpy) : 0;
    if (!MakeCurrent(dpy, root, ctx)) {
        fprintf(stderr, "glXMakeCurrent failed\n");
        return 0;
    }
    if (!tsv_mode) printf("# context: GLX on root window, via the library's own GLX\n");
    return 1;
}

/* ------------------------------- workloads ---------------------------- */
static void draw_quads_immediate(int quads) {
    fBegin(GL_QUADS);
    for (int q = 0; q < quads; ++q) {
        const GLfloat x = (GLfloat)(q % 32) / 32.0f - 0.5f;
        const GLfloat y = (GLfloat)((q / 32) % 32) / 32.0f - 0.5f;
        for (int v = 0; v < 4; ++v) {
            fColor4f(0.5f, 0.6f, 0.7f, 1.0f);
            fTexCoord2f((GLfloat)(v & 1), (GLfloat)((v >> 1) & 1));
            fVertex3f(x + (v == 1 || v == 2 ? 0.02f : 0.0f),
                      y + (v >= 2 ? 0.02f : 0.0f), 0.0f);
        }
    }
    fEnd();
}

#define CA_QUADS 1000
#define CA_VERTS (CA_QUADS * 4)
static GLfloat interleaved[CA_VERTS * 9];
static unsigned short indices[CA_QUADS * 6];

int main(void) {
    const char* path = getenv("CMPBENCH_LIB");
    if (!path) { fprintf(stderr, "set CMPBENCH_LIB\n"); return 2; }
    only = getenv("CMPBENCH_ONLY");
    tsv_mode = getenv("CMPBENCH_TSV") != NULL;
    double scale = 1.0;
    if (getenv("CMPBENCH_SCALE")) scale = atof(getenv("CMPBENCH_SCALE"));
    if (scale <= 0.0) scale = 1.0;

    lib = dlopen(path, RTLD_NOW | RTLD_GLOBAL);
    if (!lib) { fprintf(stderr, "dlopen %s: %s\n", path, dlerror()); return 2; }
    *(void**)(&lib_get_proc) = dlsym(lib, "eglGetProcAddress");
    backend_gles = dlopen("libGLESv2.so.2", RTLD_NOW | RTLD_GLOBAL);

    const char* ctx = getenv("CMPBENCH_CTX");
    const int ok = (ctx && strcmp(ctx, "glx") == 0) ? make_context_glx() : make_context_egl();
    if (!ok) { fprintf(stderr, "SKIP: no context\n"); return 77; }

    load_gl();
    if (missing) { fprintf(stderr, "SKIP: %d entry points missing\n", missing); return 77; }
    const GLubyte* ver = fGetString ? fGetString(0x1F02) : NULL;
    if (!tsv_mode) printf("# GL_VERSION: %s\n", ver ? (const char*)ver : "(null)");

    // A wrapper comparison is about CPU overhead per GL call, so the
    // default viewport is 1x1: rasterization then costs ~nothing and the
    // two libraries' different render targets stop mattering. Set
    // CMPBENCH_VIEWPORT=256 to include fragment work.
    int vp = 1;
    if (getenv("CMPBENCH_VIEWPORT")) vp = atoi(getenv("CMPBENCH_VIEWPORT"));
    if (vp <= 0) vp = 1;
    if (!tsv_mode) printf("# GL_RENDERER: %s\n", fGetString ? (const char*)fGetString(0x1F01) : "(null)");
    if (!tsv_mode) printf("# viewport: %dx%d\n", vp, vp);
    fViewport(0, 0, vp, vp);
    fClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    fClear(GL_COLOR_BUFFER_BIT);
    fMatrixMode(GL_PROJECTION);
    fLoadIdentity();
    fMatrixMode(GL_MODELVIEW);
    fLoadIdentity();

    double t0, ms;

    /* immediate mode */
    if (phase_on("immediate")) {
        const int frames = (int)(200 * scale) > 0 ? (int)(200 * scale) : 1;
        draw_quads_immediate(1000);
        fFinish();
        t0 = now_ms();
        for (int f = 0; f < frames; ++f) draw_quads_immediate(1000);
        fFinish();
        ms = now_ms() - t0;
        report("immediate", ms * 1.0e6 / ((double)frames * 4000.0), "ns/vert");
    }

    /* display list replay */
    if (phase_on("dlist")) {
        const int frames = (int)(200 * scale) > 0 ? (int)(200 * scale) : 1;
        GLuint list = fGenLists(1);
        fNewList(list, GL_COMPILE);
        draw_quads_immediate(1000);
        fEndList();
        fCallList(list);
        fFinish();
        t0 = now_ms();
        for (int f = 0; f < frames; ++f) fCallList(list);
        fFinish();
        ms = now_ms() - t0;
        report("dlist", ms * 1.0e6 / ((double)frames * 4000.0), "ns/vert");
    }

    /* client arrays */
    if (phase_on("clientarrays") || phase_on("drawelements")) {
        for (int v = 0; v < CA_VERTS; ++v) {
            GLfloat* p = interleaved + v * 9;
            p[0] = (GLfloat)(v % 64) / 64.0f - 0.5f;
            p[1] = (GLfloat)((v / 64) % 64) / 64.0f - 0.5f;
            p[2] = 0.0f;
            p[3] = 0.5f; p[4] = 0.6f; p[5] = 0.7f; p[6] = 1.0f;
            p[7] = (GLfloat)(v & 1); p[8] = (GLfloat)((v >> 1) & 1);
        }
        for (int q = 0; q < CA_QUADS; ++q) {
            const unsigned short b = (unsigned short)(q * 4);
            indices[q * 6 + 0] = b; indices[q * 6 + 1] = (unsigned short)(b + 1);
            indices[q * 6 + 2] = (unsigned short)(b + 2);
            indices[q * 6 + 3] = (unsigned short)(b + 2);
            indices[q * 6 + 4] = (unsigned short)(b + 3); indices[q * 6 + 5] = b;
        }
        fEnableClientState(GL_VERTEX_ARRAY);
        fEnableClientState(GL_COLOR_ARRAY);
        fEnableClientState(GL_TEXTURE_COORD_ARRAY);
        fVertexPointer(3, GL_FLOAT, 9 * sizeof(GLfloat), interleaved);
        fColorPointer(4, GL_FLOAT, 9 * sizeof(GLfloat), interleaved + 3);
        fTexCoordPointer(2, GL_FLOAT, 9 * sizeof(GLfloat), interleaved + 7);
        const int frames = (int)(200 * scale) > 0 ? (int)(200 * scale) : 1;

        if (phase_on("clientarrays")) {
            fDrawArrays(GL_QUADS, 0, CA_VERTS);
            fFinish();
            t0 = now_ms();
            for (int f = 0; f < frames; ++f) fDrawArrays(GL_QUADS, 0, CA_VERTS);
            fFinish();
            ms = now_ms() - t0;
            report("clientarrays", ms * 1.0e6 / ((double)frames * CA_VERTS), "ns/vert");
        }
        if (phase_on("drawelements")) {
            fDrawElements(GL_TRIANGLES, CA_QUADS * 6, GL_UNSIGNED_SHORT, indices);
            fFinish();
            t0 = now_ms();
            for (int f = 0; f < frames; ++f)
                fDrawElements(GL_TRIANGLES, CA_QUADS * 6, GL_UNSIGNED_SHORT, indices);
            fFinish();
            ms = now_ms() - t0;
            report("drawelements", ms * 1.0e6 / ((double)frames * CA_QUADS * 6), "ns/idx");
        }
        fDisableClientState(GL_VERTEX_ARRAY);
        fDisableClientState(GL_COLOR_ARRAY);
        fDisableClientState(GL_TEXTURE_COORD_ARRAY);
    }

    /* tiny batches: per-draw fixed cost */
    if (phase_on("tinybatch")) {
        const int batches = (int)(20000 * scale) > 0 ? (int)(20000 * scale) : 1;
        draw_quads_immediate(1);
        fFinish();
        t0 = now_ms();
        for (int i = 0; i < batches; ++i) draw_quads_immediate(1);
        fFinish();
        ms = now_ms() - t0;
        report("tinybatch", ms * 1000.0 / batches, "us/batch");
    }

    /* state toggling: shader/program cache pressure */
    if (phase_on("progtoggle")) {
        const int draws = (int)(10000 * scale) > 0 ? (int)(10000 * scale) : 1;
        t0 = now_ms();
        for (int i = 0; i < draws; ++i) {
            if (i & 1) fEnable(GL_LIGHTING); else fDisable(GL_LIGHTING);
            draw_quads_immediate(1);
        }
        fFinish();
        ms = now_ms() - t0;
        fDisable(GL_LIGHTING);
        report("progtoggle", ms * 1000.0 / draws, "us/draw");
    }

    /* pure CPU: matrix stack */
    if (phase_on("matrixops")) {
        const int ops = (int)(200000 * scale) > 0 ? (int)(200000 * scale) : 1;
        t0 = now_ms();
        for (int i = 0; i < ops; ++i) {
            fPushMatrix();
            fTranslatef(0.1f, 0.2f, 0.3f);
            fRotatef(17.0f, 0.0f, 1.0f, 0.0f);
            fPopMatrix();
        }
        ms = now_ms() - t0;
        report("matrixops", ms * 1.0e6 / ops, "ns/group");
    }

    /* pure CPU: getter */
    if (phase_on("getter")) {
        const int gets = (int)(500000 * scale) > 0 ? (int)(500000 * scale) : 1;
        GLfloat m[16];
        t0 = now_ms();
        for (int i = 0; i < gets; ++i) fGetFloatv(GL_MODELVIEW_MATRIX, m);
        ms = now_ms() - t0;
        report("getter", ms * 1.0e6 / gets, "ns/call");
    }

    /* texture switching */
    if (phase_on("texswitch")) {
        static GLubyte texels[64 * 64 * 4];
        for (int i = 0; i < 64 * 64 * 4; ++i) texels[i] = (GLubyte)(i * 7);
        GLuint tex[2];
        fGenTextures(2, tex);
        for (int i = 0; i < 2; ++i) {
            fBindTexture(GL_TEXTURE_2D, tex[i]);
            fTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 64, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, texels);
            fTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            fTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        }
        fEnable(GL_TEXTURE_2D);
        const int draws = (int)(10000 * scale) > 0 ? (int)(10000 * scale) : 1;
        t0 = now_ms();
        for (int i = 0; i < draws; ++i) {
            fBindTexture(GL_TEXTURE_2D, tex[i & 1]);
            draw_quads_immediate(1);
        }
        fFinish();
        ms = now_ms() - t0;
        fDisable(GL_TEXTURE_2D);
        report("texswitch", ms * 1000.0 / draws, "us/draw");
    }

    // --- Minecraft-shaped phases ------------------------------------------
    // Reconstructed GL 1.x call sequences from the 1.12/1.16 RenderDoc
    // captures plus the known vanilla vertex formats; see
    // plans/12-fpe-draw-cost.md. Only GL 1.x calls, so any library here runs
    // exactly the same workload.
    if (phase_on("mcchunk") || phase_on("mcchunkmulti")) {
        // DefaultVertexFormats.BLOCK: 3f position, colour, 2f uv, lightmap.
        // Declared all-float so one array serves every library; the 28-byte
        // stride is what per-draw cost turns on.
        enum { MC_STRIDE_F = 7, MC_CHUNK_V = 400, MC_CHUNKS = 16 };
        static GLfloat mc[MC_CHUNK_V * MC_CHUNKS * MC_STRIDE_F];
        for (int v = 0; v < MC_CHUNK_V * MC_CHUNKS; ++v) {
            const int corner = v % 4;
            const GLfloat cell = 0.01f * (GLfloat)((v / 4) % 100) - 0.5f;
            GLfloat* d = &mc[v * MC_STRIDE_F];
            d[0] = cell + ((corner == 1 || corner == 2) ? 0.01f : 0.0f);
            d[1] = cell + ((corner >= 2) ? 0.01f : 0.0f);
            d[2] = 0.0f;
            d[3] = 0.8f;
            d[4] = (GLfloat)(corner & 1);
            d[5] = (GLfloat)(corner >> 1);
            d[6] = 0.5f;
        }
        const GLsizei mcstride = MC_STRIDE_F * (GLsizei)sizeof(GLfloat);
        fEnableClientState(GL_VERTEX_ARRAY);
        fEnableClientState(GL_COLOR_ARRAY);
        fEnableClientState(GL_TEXTURE_COORD_ARRAY);
        fVertexPointer(3, GL_FLOAT, mcstride, mc);
        fColorPointer(3, GL_FLOAT, mcstride, mc + 3);
        fTexCoordPointer(2, GL_FLOAT, mcstride, mc + 4);
        const int mcframes = (int)(120 * scale) > 0 ? (int)(120 * scale) : 1;
        const double mcverts = (double)mcframes * (MC_CHUNK_V * MC_CHUNKS);

        if (phase_on("mcchunk") && !phase_on("mcchunkmulti")) {
            fDrawArrays(GL_QUADS, 0, MC_CHUNK_V * MC_CHUNKS);
            fFinish();
            t0 = now_ms();
            for (int f = 0; f < mcframes; ++f) {
                fPushMatrix();
                fTranslatef(0.001f * (GLfloat)f, 0.0f, 0.0f);
                fDrawArrays(GL_QUADS, 0, MC_CHUNK_V * MC_CHUNKS);
                fPopMatrix();
            }
            fFinish();
            ms = now_ms() - t0;
            report("mcchunk", ms * 1.0e6 / mcverts, "ns/vert");
        }
        if (phase_on("mcchunkmulti")) {
            // The real frame shape: many small chunk draws, each with its own
            // matrix, so per-draw fixed cost dominates over vertex rate.
            fDrawArrays(GL_QUADS, 0, MC_CHUNK_V);
            fFinish();
            t0 = now_ms();
            for (int f = 0; f < mcframes; ++f) {
                for (int c = 0; c < MC_CHUNKS; ++c) {
                    fPushMatrix();
                    fTranslatef(0.001f * (GLfloat)c, 0.0f, 0.0f);
                    fDrawArrays(GL_QUADS, c * MC_CHUNK_V, MC_CHUNK_V);
                    fPopMatrix();
                }
            }
            fFinish();
            ms = now_ms() - t0;
            report("mcchunkmulti", ms * 1000.0 / ((double)mcframes * MC_CHUNKS), "us/chunk");
        }
        fDisableClientState(GL_VERTEX_ARRAY);
        fDisableClientState(GL_COLOR_ARRAY);
        fDisableClientState(GL_TEXTURE_COORD_ARRAY);
    }

    if (phase_on("mcgui")) {
        // Gui/GuiIngame: one immediate quad per widget, bracketed by the
        // texture bind and alpha-test toggle the vanilla helpers push and pop.
        GLuint guitex[2] = {0, 0};
        fGenTextures(2, guitex);
        static unsigned char px[16 * 16 * 4];
        for (int i = 0; i < 16 * 16 * 4; ++i) px[i] = (unsigned char)(i * 7);
        for (int i = 0; i < 2; ++i) {
            fBindTexture(GL_TEXTURE_2D, guitex[i]);
            fTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 16, 16, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
            fTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            fTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        }
        fEnable(GL_TEXTURE_2D);
        const int widgets = (int)(4000 * scale) > 0 ? (int)(4000 * scale) : 1;
        fFinish();
        t0 = now_ms();
        for (int w = 0; w < widgets; ++w) {
            fBindTexture(GL_TEXTURE_2D, guitex[w & 1]);
            fDisable(GL_ALPHA_TEST);
            fBegin(GL_QUADS);
            fColor4f(1.0f, 1.0f, 1.0f, 1.0f);
            fTexCoord2f(0.0f, 0.0f); fVertex3f(-0.5f, -0.5f, 0.0f);
            fTexCoord2f(1.0f, 0.0f); fVertex3f(-0.4f, -0.5f, 0.0f);
            fTexCoord2f(1.0f, 1.0f); fVertex3f(-0.4f, -0.4f, 0.0f);
            fTexCoord2f(0.0f, 1.0f); fVertex3f(-0.5f, -0.4f, 0.0f);
            fEnd();
            fEnable(GL_ALPHA_TEST);
        }
        fFinish();
        ms = now_ms() - t0;
        fDisable(GL_ALPHA_TEST);
        fDisable(GL_TEXTURE_2D);
        report("mcgui", ms * 1000.0 / widgets, "us/widget");
    }

    if (phase_on("mcfont")) {
        // FontRenderer: one quad per glyph with a per-glyph colour, all inside
        // a single Begin/End for the string.
        const int strings = (int)(2000 * scale) > 0 ? (int)(2000 * scale) : 1;
        enum { GLYPHS = 24 };
        fFinish();
        t0 = now_ms();
        for (int st2 = 0; st2 < strings; ++st2) {
            fBegin(GL_QUADS);
            for (int g = 0; g < GLYPHS; ++g) {
                const GLfloat x = -0.5f + 0.01f * (GLfloat)g;
                fColor4f(1.0f, 1.0f, 1.0f, 0.25f + 0.03f * (GLfloat)(g & 7));
                fTexCoord2f(0.0f, 0.0f); fVertex3f(x, 0.0f, 0.0f);
                fTexCoord2f(1.0f, 0.0f); fVertex3f(x + 0.008f, 0.0f, 0.0f);
                fTexCoord2f(1.0f, 1.0f); fVertex3f(x + 0.008f, 0.01f, 0.0f);
                fTexCoord2f(0.0f, 1.0f); fVertex3f(x, 0.01f, 0.0f);
            }
            fEnd();
        }
        fFinish();
        ms = now_ms() - t0;
        report("mcfont", ms * 1000.0 / strings, "us/string");
    }

    if (phase_on("mcentity")) {
        // ModelRenderer: each box is a compiled display list replayed under
        // its own push/rotate/pop.
        enum { BOXES = 12 };
        GLuint lists[BOXES];
        for (int b = 0; b < BOXES; ++b) {
            lists[b] = fGenLists(1);
            fNewList(lists[b], GL_COMPILE);
            fBegin(GL_QUADS);
            for (int q = 0; q < 6; ++q) {
                const GLfloat c = 0.01f * (GLfloat)q - 0.2f;
                fColor4f(0.5f, 0.6f, 0.7f, 1.0f);
                fTexCoord2f(0.0f, 0.0f); fVertex3f(c, c, 0.0f);
                fTexCoord2f(1.0f, 0.0f); fVertex3f(c + 0.01f, c, 0.0f);
                fTexCoord2f(1.0f, 1.0f); fVertex3f(c + 0.01f, c + 0.01f, 0.0f);
                fTexCoord2f(0.0f, 1.0f); fVertex3f(c, c + 0.01f, 0.0f);
            }
            fEnd();
            fEndList();
        }
        const int models = (int)(2000 * scale) > 0 ? (int)(2000 * scale) : 1;
        fFinish();
        t0 = now_ms();
        for (int m = 0; m < models; ++m) {
            for (int b = 0; b < BOXES; ++b) {
                fPushMatrix();
                fTranslatef(0.0f, 0.01f * (GLfloat)b, 0.0f);
                fRotatef(1.0f * (GLfloat)b, 0.0f, 1.0f, 0.0f);
                fCallList(lists[b]);
                fPopMatrix();
            }
        }
        fFinish();
        ms = now_ms() - t0;
        report("mcentity", ms * 1000.0 / models, "us/model");
    }

    return 0;
}
