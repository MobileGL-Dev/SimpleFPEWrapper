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
//   bench.immediate    - glBegin/glEnd vertex throughput (ring VBO path)
//   bench.dlist        - display-list replay of the same load (batching)
//   bench.progcache    - per-draw cost, constant vs. alternating FPE keys
//   bench.translate    - GLSL 1.10 -> ESSL translation latency
//   bench.tinybatch    - one quad per glBegin/glEnd (GUI pattern)
//   bench.clientarrays - interleaved client arrays, GL_QUADS (chunk path)
//   bench.gatherarrays - independent client allocations (CPU interleave)
//   bench.drawelements - client-memory ushort indices
//   bench.matrixops    - push/translate/rotate/pop matrix stack cost
//   bench.texupload    - 16x16 glTexSubImage2D (lightmap) + 256x256 full
//   bench.texswitch    - draws alternating between two bound textures
//   bench.readpixels   - 256x256 RGBA readback
//   bench.getter       - glGetFloatv(GL_MODELVIEW_MATRIX) shadow reads
//   bench.progcompile  - cold program-cache misses (16 enable combos)
//
// SFPEW_BENCH_SCALE scales every iteration count (default 1.0; use
// small values on slow/software devices). Exit 0 always, 77 = no device.

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <EGL/egl.h>

typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef float GLfloat;
typedef int GLint, GLsizei;
typedef unsigned int GLbitfield;

#define GL_TRIANGLES 0x0004
#define GL_QUADS 0x0007
#define GL_COMPILE 0x1300
#define GL_LIGHTING 0x0B50
#define GL_FOG 0x0B60
#define GL_ALPHA_TEST 0x0BC0
#define GL_TEXTURE_2D 0x0DE1
#define GL_MODELVIEW_MATRIX 0x0BA6
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_FLOAT 0x1406
#define GL_UNSIGNED_SHORT 0x1403
#define GL_UNSIGNED_BYTE 0x1401
#define GL_RGBA 0x1908
#define GL_VERTEX_ARRAY 0x8074
#define GL_COLOR_ARRAY 0x8076
#define GL_TEXTURE_COORD_ARRAY 0x8078
#define GL_NEAREST 0x2600

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
static void (*fVertexPointer)(GLint, GLenum, GLsizei, const void*);
static void (*fColorPointer)(GLint, GLenum, GLsizei, const void*);
static void (*fTexCoordPointer)(GLint, GLenum, GLsizei, const void*);
static void (*fEnableClientState)(GLenum);
static void (*fDisableClientState)(GLenum);
static void (*fDrawArrays)(GLenum, GLint, GLsizei);
static void (*fDrawElements)(GLenum, GLsizei, GLenum, const void*);
static void (*fPushMatrix)(void);
static void (*fPopMatrix)(void);
static void (*fTranslatef)(GLfloat, GLfloat, GLfloat);
static void (*fRotatef)(GLfloat, GLfloat, GLfloat, GLfloat);
static void (*fGenTextures)(GLsizei, GLuint*);
static void (*fBindTexture)(GLenum, GLuint);
static void (*fTexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum,
                           const void*);
static void (*fTexSubImage2D)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum,
                              const void*);
static void (*fTexParameteri)(GLenum, GLenum, GLint);
static void (*fReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*);
static void (*fGetFloatv)(GLenum, GLfloat*);
typedef int (*translate_fn)(unsigned int, const char*, char*, int);

// A single check at the end can only say "something raised an error", which is
// useless for locating it. Drain and report per phase instead, naming the phase
// that raised it.
static GLenum (*g_bench_get_error)(void);
static int g_bench_error_count;
static void bench_check_error(const char* phase) {
    GLenum err;
    while (g_bench_get_error != NULL && (err = g_bench_get_error()) != 0) {
        fprintf(stderr, "FAIL: GL error 0x%x after phase '%s'\n", err, phase);
        ++g_bench_error_count;
    }
}

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
    // Route eglMakeCurrent through the wrapper the way a launcher does: that
    // is what lets the wrapper track the current context exactly instead of
    // asking libEGL on every state access. SFPEW_BENCH_DIRECT_EGL=1 calls
    // libEGL directly so the two paths can be compared.
    EGLBoolean (*wrapperMakeCurrent)(EGLDisplay, EGLSurface, EGLSurface, EGLContext) = NULL;
    if (getenv("SFPEW_BENCH_DIRECT_EGL") == NULL)
        *(void**)(&wrapperMakeCurrent) = resolve("eglMakeCurrent");
    const int made_current =
        wrapperMakeCurrent != NULL
            ? wrapperMakeCurrent(display, surface, surface, context)
            : eglMakeCurrent(display, surface, surface, context);
    if (surface == EGL_NO_SURFACE || context == EGL_NO_CONTEXT || !made_current) {
        printf("SKIP: could not make an ES3 pbuffer context current\n");
        return 77;
    }
    printf("bench.egl: current-context tracking = %s\n",
           wrapperMakeCurrent != NULL ? "wrapper (exact)" : "polled via libEGL");

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
    g_bench_get_error = fGetError;
    R(fVertexPointer, "glVertexPointer");
    R(fColorPointer, "glColorPointer");
    R(fTexCoordPointer, "glTexCoordPointer");
    R(fEnableClientState, "glEnableClientState");
    R(fDisableClientState, "glDisableClientState");
    R(fDrawArrays, "glDrawArrays");
    R(fDrawElements, "glDrawElements");
    R(fPushMatrix, "glPushMatrix");
    R(fPopMatrix, "glPopMatrix");
    R(fTranslatef, "glTranslatef");
    R(fRotatef, "glRotatef");
    R(fGenTextures, "glGenTextures");
    R(fBindTexture, "glBindTexture");
    R(fTexImage2D, "glTexImage2D");
    R(fTexSubImage2D, "glTexSubImage2D");
    R(fTexParameteri, "glTexParameteri");
    R(fReadPixels, "glReadPixels");
    R(fGetFloatv, "glGetFloatv");

    double scale = 1.0;
    const char* scale_env = getenv("SFPEW_BENCH_SCALE");
    if (scale_env != NULL) scale = atof(scale_env);
    if (scale <= 0.0) scale = 1.0;

    // SFPEW_BENCH_ONLY=<substring> runs just the phases whose name matches,
    // so a profiler sees one workload instead of all fourteen interleaved.
    const char* only = getenv("SFPEW_BENCH_ONLY");
#define PHASE(name) (only == NULL || strstr(name, only) != NULL)

    fClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    fClear(GL_COLOR_BUFFER_BIT);

    // --- bench.immediate ------------------------------------------------
    const int quads_per_frame = 1000;
    const int frames = (int)(400 * scale) > 0 ? (int)(400 * scale) : 1;
    draw_quads(quads_per_frame); // warmup: first draw compiles the program
    fFinish();
    double t0 = now_ms();
    if (PHASE("immediate")) {
        for (int frm = 0; frm < frames; ++frm) draw_quads(quads_per_frame);
    }
    fFinish();
    double immediate_ms = now_ms() - t0;
    const double verts = (double)frames * quads_per_frame * 4;
    printf("bench.immediate: %7.2f Mvert/s  (%6.1f ns/vert, %d frames x %d quads)\n",
           verts / immediate_ms / 1000.0, immediate_ms * 1.0e6 / verts, frames, quads_per_frame);
    bench_check_error("immediate");

    // --- bench.dlist ------------------------------------------------------
    const GLuint list = fGenLists(1);
    fNewList(list, GL_COMPILE);
    draw_quads(quads_per_frame);
    fEndList();
    fCallList(list); // warmup replay
    fFinish();
    t0 = now_ms();
    if (PHASE("dlist")) {
        for (int frm = 0; frm < frames; ++frm) fCallList(list);
    }
    fFinish();
    double dlist_ms = now_ms() - t0;
    printf("bench.dlist:     %7.2f Mvert/s  (%6.1f ns/vert, replay speedup %.2fx)\n",
           verts / dlist_ms / 1000.0, dlist_ms * 1.0e6 / verts,
           dlist_ms > 0.0 ? immediate_ms / dlist_ms : 0.0);
    bench_check_error("dlist");

    // --- bench.progcache --------------------------------------------------
    // Constant state: every draw resolves the SAME program-cache key.
    const int draws = (int)(10000 * scale) > 0 ? (int)(10000 * scale) : 1;
    draw_quads(1);
    fFinish();
    t0 = now_ms();
    if (PHASE("progcache")) {
        for (int i = 0; i < draws; ++i) draw_quads(1);
    }
    fFinish();
    const double steady_ms = now_ms() - t0;
    // Toggling state: alternating keys, still 100% cache hits after the
    // first two draws - measures hash+lookup+uniform-refresh overhead.
    fEnable(GL_LIGHTING);
    draw_quads(1);
    fDisable(GL_LIGHTING);
    fFinish();
    t0 = now_ms();
    if (PHASE("progcache")) {
        for (int i = 0; i < draws; ++i) {
            if (i & 1) fEnable(GL_LIGHTING);
            else fDisable(GL_LIGHTING);
            draw_quads(1);
        }
    }
    fFinish();
    const double toggle_ms = now_ms() - t0;
    fDisable(GL_LIGHTING);
    printf("bench.progcache: %7.2f us/draw steady, %7.2f us/draw toggling (%d draws)\n",
           steady_ms * 1000.0 / draws, toggle_ms * 1000.0 / draws, draws);
    bench_check_error("progcache");

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
    double translate_ms = 0.0;
    if (PHASE("translate")) {
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
        translate_ms = now_ms() - t0;
    }
    printf("bench.translate: %7.2f ms/shader (GLSL 1.10 pair x %d)\n",
           translate_ms / (pairs * 2), pairs);
    bench_check_error("translate");

    // --- bench.tinybatch ---------------------------------------------------
    // GUI drawing pattern: one quad per Begin/End, thousands per frame.
    const int batches = (int)(20000 * scale) > 0 ? (int)(20000 * scale) : 1;
    draw_quads(1);
    fFinish();
    t0 = now_ms();
    if (PHASE("tinybatch")) {
        for (int i = 0; i < batches; ++i) draw_quads(1);
    }
    fFinish();
    const double tiny_ms = now_ms() - t0;
    printf("bench.tinybatch: %7.2f us/batch (1 quad per Begin/End, %d batches)\n",
           tiny_ms * 1000.0 / batches, batches);
    bench_check_error("tinybatch");

    // --- bench.clientarrays / bench.gatherarrays ---------------------------
    // The Minecraft chunk path: client-memory arrays drawn as GL_QUADS
    // (index synthesis). Interleaved single allocation first, then the
    // same data as three independent allocations (CPU gather path).
    enum { CA_VERTS = 4000 };
    static float interleaved[CA_VERTS * 9];
    static float pos[CA_VERTS * 3], col[CA_VERTS * 4], tex[CA_VERTS * 2];
    for (int v = 0; v < CA_VERTS; ++v) {
        const int q = v / 4, corner = v % 4;
        const float cell = 0.01f * (float)(q % 100) - 0.5f;
        const float px = cell + ((corner == 1 || corner == 2) ? 0.01f : 0.0f);
        const float py = cell + ((corner >= 2) ? 0.01f : 0.0f);
        float* dst = &interleaved[v * 9];
        dst[0] = px; dst[1] = py; dst[2] = 0.0f;
        dst[3] = 0.3f; dst[4] = 0.6f; dst[5] = 0.9f; dst[6] = 1.0f;
        dst[7] = (float)(corner & 1); dst[8] = (float)(corner >> 1);
        pos[v * 3] = px; pos[v * 3 + 1] = py; pos[v * 3 + 2] = 0.0f;
        col[v * 4] = 0.3f; col[v * 4 + 1] = 0.6f; col[v * 4 + 2] = 0.9f; col[v * 4 + 3] = 1.0f;
        tex[v * 2] = (float)(corner & 1); tex[v * 2 + 1] = (float)(corner >> 1);
    }
    fEnableClientState(GL_VERTEX_ARRAY);
    fEnableClientState(GL_COLOR_ARRAY);
    fEnableClientState(GL_TEXTURE_COORD_ARRAY);
    const int ca_frames = (int)(300 * scale) > 0 ? (int)(300 * scale) : 1;
    const double ca_verts = (double)ca_frames * CA_VERTS;

    fVertexPointer(3, GL_FLOAT, 36, interleaved);
    fColorPointer(4, GL_FLOAT, 36, interleaved + 3);
    fTexCoordPointer(2, GL_FLOAT, 36, interleaved + 7);
    fDrawArrays(GL_QUADS, 0, CA_VERTS);
    fFinish();
    t0 = now_ms();
    if (PHASE("clientarrays")) {
        for (int frm = 0; frm < ca_frames; ++frm) fDrawArrays(GL_QUADS, 0, CA_VERTS);
    }
    fFinish();
    const double ca_ms = now_ms() - t0;
    printf("bench.clientarrays: %7.2f Mvert/s  (%6.1f ns/vert, interleaved QUADS)\n",
           ca_verts / ca_ms / 1000.0, ca_ms * 1.0e6 / ca_verts);
    bench_check_error("clientarrays");

    fVertexPointer(3, GL_FLOAT, 0, pos);
    fColorPointer(4, GL_FLOAT, 0, col);
    fTexCoordPointer(2, GL_FLOAT, 0, tex);
    fDrawArrays(GL_QUADS, 0, CA_VERTS);
    fFinish();
    t0 = now_ms();
    if (PHASE("gatherarrays")) {
        for (int frm = 0; frm < ca_frames; ++frm) fDrawArrays(GL_QUADS, 0, CA_VERTS);
    }
    fFinish();
    const double ga_ms = now_ms() - t0;
    printf("bench.gatherarrays: %7.2f Mvert/s  (%6.1f ns/vert, independent allocations)\n",
           ca_verts / ga_ms / 1000.0, ga_ms * 1.0e6 / ca_verts);
    bench_check_error("gatherarrays");

    // --- bench.drawelements -------------------------------------------------
    static unsigned short indices[CA_VERTS / 4 * 6];
    for (int q = 0; q < CA_VERTS / 4; ++q) {
        const unsigned short base = (unsigned short)(q * 4);
        unsigned short* dst = &indices[q * 6];
        dst[0] = base; dst[1] = base + 1; dst[2] = base + 2;
        dst[3] = base; dst[4] = base + 2; dst[5] = base + 3;
    }
    fVertexPointer(3, GL_FLOAT, 36, interleaved);
    fColorPointer(4, GL_FLOAT, 36, interleaved + 3);
    fTexCoordPointer(2, GL_FLOAT, 36, interleaved + 7);
    fDrawElements(GL_TRIANGLES, CA_VERTS / 4 * 6, GL_UNSIGNED_SHORT, indices);
    fFinish();
    t0 = now_ms();
    if (PHASE("drawelements")) {
        for (int frm = 0; frm < ca_frames; ++frm)
            fDrawElements(GL_TRIANGLES, CA_VERTS / 4 * 6, GL_UNSIGNED_SHORT, indices);
    }
    fFinish();
    const double de_ms = now_ms() - t0;
    const double de_verts = (double)ca_frames * (CA_VERTS / 4 * 6);
    printf("bench.drawelements: %7.2f Midx/s   (%6.1f ns/idx, client ushort indices)\n",
           de_verts / de_ms / 1000.0, de_ms * 1.0e6 / de_verts);
    bench_check_error("drawelements");
    fDisableClientState(GL_VERTEX_ARRAY);
    fDisableClientState(GL_COLOR_ARRAY);
    fDisableClientState(GL_TEXTURE_COORD_ARRAY);

    // --- bench.matrixops -----------------------------------------------------
    const int mat_ops = (int)(200000 * scale) > 0 ? (int)(200000 * scale) : 1;
    t0 = now_ms();
    if (PHASE("matrixops")) {
        for (int i = 0; i < mat_ops; ++i) {
            fPushMatrix();
            fTranslatef(0.1f, 0.2f, 0.3f);
            fRotatef(17.0f, 0.0f, 1.0f, 0.0f);
            fPopMatrix();
        }
    }
    const double mat_ms = now_ms() - t0;
    printf("bench.matrixops: %7.1f ns/group (push+translate+rotate+pop, x%d)\n",
           mat_ms * 1.0e6 / mat_ops, mat_ops);
    bench_check_error("matrixops");

    // --- bench.texupload -------------------------------------------------------
    static unsigned char texels[256 * 256 * 4];
    for (unsigned i = 0; i < sizeof(texels); ++i) texels[i] = (unsigned char)i;
    GLuint textures[2] = {0, 0};
    fGenTextures(2, textures);
    for (int t = 0; t < 2; ++t) {
        fBindTexture(GL_TEXTURE_2D, textures[t]);
        fTexParameteri(GL_TEXTURE_2D, 0x2801 /* MIN_FILTER */, GL_NEAREST);
        fTexParameteri(GL_TEXTURE_2D, 0x2800 /* MAG_FILTER */, GL_NEAREST);
        fTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 256, 0, GL_RGBA, GL_UNSIGNED_BYTE, texels);
    }
    const int subs = (int)(5000 * scale) > 0 ? (int)(5000 * scale) : 1;
    fFinish();
    t0 = now_ms();
    if (PHASE("texupload")) {
        for (int i = 0; i < subs; ++i)
            fTexSubImage2D(GL_TEXTURE_2D, 0, (i & 7) * 16, ((i >> 3) & 7) * 16, 16, 16, GL_RGBA,
                           GL_UNSIGNED_BYTE, texels);
    }
    fFinish();
    const double sub_ms = now_ms() - t0;
    const int fulls = (int)(200 * scale) > 0 ? (int)(200 * scale) : 1;
    t0 = now_ms();
    if (PHASE("texupload")) {
        for (int i = 0; i < fulls; ++i)
            fTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 256, 0, GL_RGBA, GL_UNSIGNED_BYTE, texels);
    }
    fFinish();
    const double full_ms = now_ms() - t0;
    printf("bench.texupload: %7.2f us/16x16 sub, %7.1f MB/s full 256x256 RGBA\n",
           sub_ms * 1000.0 / subs, (double)fulls * sizeof(texels) / (full_ms / 1000.0) / 1.0e6);
    bench_check_error("texupload");

    // --- bench.texswitch ---------------------------------------------------------
    fEnable(GL_TEXTURE_2D);
    draw_quads(1);
    fFinish();
    t0 = now_ms();
    if (PHASE("texswitch")) {
        for (int i = 0; i < draws; ++i) {
            fBindTexture(GL_TEXTURE_2D, textures[i & 1]);
            draw_quads(1);
        }
    }
    fFinish();
    const double switch_ms = now_ms() - t0;
    fDisable(GL_TEXTURE_2D);
    printf("bench.texswitch: %7.2f us/draw (alternating glBindTexture, %d draws)\n",
           switch_ms * 1000.0 / draws, draws);
    bench_check_error("texswitch");

    // --- bench.readpixels ----------------------------------------------------------
    static unsigned char readback[256 * 256 * 4];
    const int reads = (int)(200 * scale) > 0 ? (int)(200 * scale) : 1;
    fReadPixels(0, 0, 256, 256, GL_RGBA, GL_UNSIGNED_BYTE, readback);
    t0 = now_ms();
    if (PHASE("readpixels")) {
        for (int i = 0; i < reads; ++i)
            fReadPixels(0, 0, 256, 256, GL_RGBA, GL_UNSIGNED_BYTE, readback);
    }
    const double read_ms = now_ms() - t0;
    printf("bench.readpixels: %6.2f us/read, %7.1f MB/s (256x256 RGBA)\n",
           read_ms * 1000.0 / reads, (double)reads * sizeof(readback) / (read_ms / 1000.0) / 1.0e6);
    bench_check_error("readpixels");

    // --- bench.getter -----------------------------------------------------------------
    static GLfloat matrix_out[16];
    const int gets = (int)(500000 * scale) > 0 ? (int)(500000 * scale) : 1;
    t0 = now_ms();
    if (PHASE("getter")) {
        for (int i = 0; i < gets; ++i) fGetFloatv(GL_MODELVIEW_MATRIX, matrix_out);
    }
    const double get_ms = now_ms() - t0;
    printf("bench.getter:    %7.1f ns/glGetFloatv(GL_MODELVIEW_MATRIX) (x%d)\n",
           get_ms * 1.0e6 / gets, gets);
    bench_check_error("getter");

    // --- bench.progcompile --------------------------------------------------------------
    // First-touch program generation across 16 enable combinations. Runs
    // LAST: it deliberately floods the program cache with fresh keys.
    static const GLenum combo_bits[4] = {GL_FOG, GL_ALPHA_TEST, GL_LIGHTING, GL_TEXTURE_2D};
    fBindTexture(GL_TEXTURE_2D, textures[0]);
    fFinish();
    t0 = now_ms();
    if (PHASE("progcompile")) {
        for (int combo = 0; combo < 16; ++combo) {
            for (int bit = 0; bit < 4; ++bit) {
                if (combo & (1 << bit)) fEnable(combo_bits[bit]);
                else fDisable(combo_bits[bit]);
            }
            draw_quads(1);
        }
    }
    fFinish();
    const double compile_ms = now_ms() - t0;
    for (int bit = 0; bit < 4; ++bit) fDisable(combo_bits[bit]);
    printf("bench.progcompile: %5.2f ms/program (16 fog/alpha/light/tex combos, first touch)\n",
           compile_ms / 16.0);
    bench_check_error("progcompile");

    if (g_bench_error_count != 0 || fGetError() != 0) {
        fprintf(stderr, "FAIL: GL error raised during benchmarks\n");
        return 1;
    }
    return 0;
}
