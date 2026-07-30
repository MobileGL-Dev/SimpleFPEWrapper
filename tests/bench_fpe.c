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
// Minecraft-shaped composites. The phases above isolate one mechanism each;
// these reproduce the GL 1.x call sequence the game actually issues, so a
// change that trades one mechanism against another still shows up. The call
// sequences are reconstructed from the 1.12/1.16 RenderDoc captures in
// plans/12 plus the known vanilla vertex formats - they are representative,
// not captured verbatim.
//
//   bench.mcchunk      - terrain chunk: DefaultVertexFormats.BLOCK, the
//                        28-byte interleaved position/color/uv/lightmap
//                        record, from a VBO, two texture units, GL_QUADS,
//                        one matrix push/translate/pop per chunk
//   bench.mcchunkmulti - the same but many small chunks per frame, which is
//                        where per-draw fixed cost dominates over vertex count
//   bench.mcgui        - GUI/HUD: one immediate-mode quad per widget with a
//                        texture bind and alpha-test toggle around it
//   bench.mcfont       - text: per-glyph colour changes inside one Begin/End,
//                        the shape the glyph batcher was written for
//   bench.mcentity     - entity models: display-list replay with a matrix
//                        push/rotate/pop per box
//
// Which Minecraft configuration each phase stands for:
//   - the five mc* phases draw with program 0, i.e. vanilla with no shader
//     pack, where the wrapper generates the whole pipeline itself.
//   - bench.userprog / bench.userprogelements are the OptiFine/Sodium case,
//     where the app binds its own program and the wrapper feeds the legacy
//     arrays and uniform block into it.
// They exercise disjoint code, so a change usually moves one group and not
// the other; check both before calling something a win.
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
#define GL_ARRAY_BUFFER 0x8892
#define GL_STATIC_DRAW 0x88E4
#define GL_UNSIGNED_SHORT 0x1403
#define GL_UNSIGNED_BYTE 0x1401
#define GL_RGBA 0x1908
#define GL_VERTEX_ARRAY 0x8074
#define GL_COLOR_ARRAY 0x8076
#define GL_TEXTURE_COORD_ARRAY 0x8078
#define GL_NEAREST 0x2600
#define GL_TEXTURE0 0x84C0
#define GL_TEXTURE1 0x84C1
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_BLEND 0x0BE2

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
static void (*fColor4f)(GLfloat, GLfloat, GLfloat, GLfloat);
static void (*fVertex3f)(GLfloat, GLfloat, GLfloat);
static void (*fActiveTexture)(GLenum);
static void (*fClientActiveTexture)(GLenum);
static void (*fDepthMask)(unsigned char);
static void (*fBlendFunc)(GLenum, GLenum);
static void (*fGenBuffers)(GLsizei, GLuint*);
static void (*fBindBuffer)(GLenum, GLuint);
static void (*fBufferData)(GLenum, long, const void*, GLenum);
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
    R(fColor4f, "glColor4f");
    R(fVertex3f, "glVertex3f");
    R(fActiveTexture, "glActiveTexture");
    R(fClientActiveTexture, "glClientActiveTexture");
    R(fDepthMask, "glDepthMask");
    R(fBlendFunc, "glBlendFunc");
    R(fGenBuffers, "glGenBuffers");
    R(fBindBuffer, "glBindBuffer");
    R(fBufferData, "glBufferData");

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

    // --- bench.userprog -----------------------------------------------------
    // Fixed-function client arrays drawn while a USER PROGRAM is current: the
    // Minecraft 1.16 + shaders shape, where the wrapper feeds the app's shader
    // from the legacy arrays (sfpewSendUserProgramAttributes) and pushes the
    // fixed-function uniform block into it (sfpewFeedUserProgramUniforms).
    //
    // Every other phase here draws with program 0, so none of them touch that
    // code at all. It was measured only through RenderDoc call counts until
    // this phase existed.
    {
        void (*fCreateShaderP)(void);
        unsigned int (*fCreateShaderF)(unsigned int) = NULL;
        void (*fShaderSourceF)(unsigned int, int, const char* const*, const int*) = NULL;
        void (*fCompileShaderF)(unsigned int) = NULL;
        unsigned int (*fCreateProgramF)(void) = NULL;
        void (*fAttachShaderF)(unsigned int, unsigned int) = NULL;
        void (*fLinkProgramF)(unsigned int) = NULL;
        void (*fGetProgramivF)(unsigned int, unsigned int, int*) = NULL;
        void (*fUseProgramF)(unsigned int) = NULL;
        (void)fCreateShaderP;
        *(void**)(&fCreateShaderF) = resolve("glCreateShader");
        *(void**)(&fShaderSourceF) = resolve("glShaderSource");
        *(void**)(&fCompileShaderF) = resolve("glCompileShader");
        *(void**)(&fCreateProgramF) = resolve("glCreateProgram");
        *(void**)(&fAttachShaderF) = resolve("glAttachShader");
        *(void**)(&fLinkProgramF) = resolve("glLinkProgram");
        *(void**)(&fGetProgramivF) = resolve("glGetProgramiv");
        *(void**)(&fUseProgramF) = resolve("glUseProgram");

        if (fCreateShaderF && fShaderSourceF && fCompileShaderF && fCreateProgramF &&
            fAttachShaderF && fLinkProgramF && fGetProgramivF && fUseProgramF) {
            // Declares the fpe_* inputs and a slice of the fixed-function
            // uniform block, so the wrapper resolves and feeds both.
            static const char* upVS =
                "#version 300 es\n"
                "in vec4 fpe_Vertex;\n"
                "in vec4 fpe_Color;\n"
                "in vec4 fpe_MultiTexCoord0;\n"
                "uniform mat4 fpe_ModelViewProjectionMatrix;\n"
                "out vec4 vCol;\n"
                "void main() {\n"
                "  vCol = fpe_Color + fpe_MultiTexCoord0 * 0.0;\n"
                "  gl_Position = fpe_ModelViewProjectionMatrix * fpe_Vertex;\n"
                "}\n";
            static const char* upFS =
                "#version 300 es\n"
                "precision mediump float;\n"
                "struct FpeFog { vec4 color; float density; };\n"
                "uniform FpeFog fpe_Fog;\n"
                "uniform int fpe_AlphaTestFunc;\n"
                "uniform float fpe_AlphaTestRef;\n"
                "in vec4 vCol;\n"
                "out vec4 o;\n"
                "void main() {\n"
                "  if (fpe_AlphaTestFunc != 0 && vCol.a < fpe_AlphaTestRef) discard;\n"
                "  o = vec4(vCol.rgb + fpe_Fog.color.rgb * 0.0, 1.0);\n"
                "}\n";
            const unsigned int upvs = fCreateShaderF(0x8B31);
            const unsigned int upfs = fCreateShaderF(0x8B30);
            fShaderSourceF(upvs, 1, &upVS, NULL);
            fShaderSourceF(upfs, 1, &upFS, NULL);
            fCompileShaderF(upvs);
            fCompileShaderF(upfs);
            const unsigned int upprog = fCreateProgramF();
            fAttachShaderF(upprog, upvs);
            fAttachShaderF(upprog, upfs);
            fLinkProgramF(upprog);
            int uplinked = 0;
            fGetProgramivF(upprog, 0x8B82 /* GL_LINK_STATUS */, &uplinked);
            // Buffer-backed pointers, not client memory. The capture this
            // phase models (Minecraft 1.16) keeps chunk geometry in its own
            // VBO, so the wrapper sources attributes straight from it. Client
            // pointers would instead upload 144KB per draw and make the phase
            // memory-bound, drowning out the per-draw call overhead that the
            // user-program path actually costs.
            unsigned int upvbo = 0;
            fGenBuffers(1, &upvbo);
            fBindBuffer(GL_ARRAY_BUFFER, upvbo);
            fBufferData(GL_ARRAY_BUFFER, (long)sizeof interleaved, interleaved, GL_STATIC_DRAW);
            if (uplinked && upvbo != 0) {
                fEnableClientState(GL_VERTEX_ARRAY);
                fEnableClientState(GL_COLOR_ARRAY);
                fEnableClientState(GL_TEXTURE_COORD_ARRAY);
                fVertexPointer(3, GL_FLOAT, 36, (const void*)0);
                fColorPointer(4, GL_FLOAT, 36, (const void*)(3 * sizeof(float)));
                fTexCoordPointer(2, GL_FLOAT, 36, (const void*)(7 * sizeof(float)));
                fUseProgramF(upprog);

                fDrawArrays(GL_TRIANGLES, 0, CA_VERTS); // warmup + resolve
                fFinish();
                t0 = now_ms();
                if (PHASE("userprog")) {
                    for (int frm = 0; frm < ca_frames; ++frm)
                        fDrawArrays(GL_TRIANGLES, 0, CA_VERTS);
                }
                fFinish();
                const double up_ms = now_ms() - t0;
                printf("bench.userprog:  %7.2f Mvert/s  (%6.1f ns/vert, FFP arrays -> user "
                       "program)\n",
                       ca_verts / up_ms / 1000.0, up_ms * 1.0e6 / ca_verts);
                bench_check_error("userprog");

                // Indexed variant: adds the element-ring upload and the
                // largest-index scan that userProgramDrawElements performs.
                fDrawElements(GL_TRIANGLES, CA_VERTS / 4 * 6, GL_UNSIGNED_SHORT, indices);
                fFinish();
                t0 = now_ms();
                if (PHASE("userprogelements")) {
                    for (int frm = 0; frm < ca_frames; ++frm)
                        fDrawElements(GL_TRIANGLES, CA_VERTS / 4 * 6, GL_UNSIGNED_SHORT, indices);
                }
                fFinish();
                const double upe_ms = now_ms() - t0;
                printf("bench.userprogelements: %7.2f Midx/s (%6.1f ns/idx, indexed FFP -> user "
                       "program)\n",
                       de_verts / upe_ms / 1000.0, upe_ms * 1.0e6 / de_verts);
                bench_check_error("userprogelements");

                fUseProgramF(0);
                fDisableClientState(GL_VERTEX_ARRAY);
                fDisableClientState(GL_COLOR_ARRAY);
                fDisableClientState(GL_TEXTURE_COORD_ARRAY);
                fBindBuffer(GL_ARRAY_BUFFER, 0);
            } else {
                printf("bench.userprog:  SKIP (user program did not link)\n");
            }
        } else {
            printf("bench.userprog:  SKIP (shader entry points unavailable)\n");
        }
    }

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

    // --- Minecraft-shaped composites ----------------------------------------
    //
    // Vanilla's terrain vertex is DefaultVertexFormats.BLOCK: 3 floats
    // position, 4 unsigned bytes colour, 2 floats UV, 2 shorts lightmap =
    // 28 bytes. The lightmap rides texture unit 1, which is why each chunk
    // costs two glClientActiveTexture switches on top of the pointer set.
    // Colour and lightmap are declared as GL_FLOAT here so one array serves
    // every phase; the stride is what the per-draw cost turns on, and keeping
    // a single format keeps these phases comparable to clientarrays above.
    enum { MC_STRIDE = 28, MC_CHUNK_VERTS = 400, MC_CHUNKS = 16 };
    static float mc_chunk[MC_CHUNK_VERTS * MC_CHUNKS * 7];
    for (int v = 0; v < MC_CHUNK_VERTS * MC_CHUNKS; ++v) {
        const int corner = v % 4;
        const float cell = 0.01f * (float)((v / 4) % 100) - 0.5f;
        float* d = &mc_chunk[v * 7];
        d[0] = cell + ((corner == 1 || corner == 2) ? 0.01f : 0.0f);
        d[1] = cell + ((corner >= 2) ? 0.01f : 0.0f);
        d[2] = 0.0f;
        d[3] = 0.8f;                              // colour (packed byte in vanilla)
        d[4] = (float)(corner & 1);               // u
        d[5] = (float)(corner >> 1);              // v
        d[6] = 0.5f;                              // lightmap s
    }
    unsigned int mc_vbo = 0;
    fGenBuffers(1, &mc_vbo);
    fBindBuffer(GL_ARRAY_BUFFER, mc_vbo);
    fBufferData(GL_ARRAY_BUFFER, (long)sizeof mc_chunk, mc_chunk, GL_STATIC_DRAW);

    // Sets up the two-unit chunk format exactly once, the way RenderGlobal
    // does before walking its chunk list.
    const int mc_frames = (int)(120 * scale) > 0 ? (int)(120 * scale) : 1;
    fBindTexture(GL_TEXTURE_2D, textures[0]);
    fEnableClientState(GL_VERTEX_ARRAY);
    fEnableClientState(GL_COLOR_ARRAY);
    fEnableClientState(GL_TEXTURE_COORD_ARRAY);
    fVertexPointer(3, GL_FLOAT, MC_STRIDE, (const void*)0);
    fColorPointer(3, GL_FLOAT, MC_STRIDE, (const void*)(3 * sizeof(float)));
    fTexCoordPointer(2, GL_FLOAT, MC_STRIDE, (const void*)(4 * sizeof(float)));
    fEnable(GL_TEXTURE_2D);

    // bench.mcchunk - one large chunk draw per frame. Vertex-throughput bound.
    const double mc_big_verts = (double)mc_frames * (MC_CHUNK_VERTS * MC_CHUNKS);
    fDrawArrays(GL_QUADS, 0, MC_CHUNK_VERTS * MC_CHUNKS);
    fFinish();
    t0 = now_ms();
    if (PHASE("mcchunk")) {
        for (int frm = 0; frm < mc_frames; ++frm) {
            fPushMatrix();
            fTranslatef(0.001f * (float)frm, 0.0f, 0.0f);
            fDrawArrays(GL_QUADS, 0, MC_CHUNK_VERTS * MC_CHUNKS);
            fPopMatrix();
        }
    }
    fFinish();
    const double mcc_ms = now_ms() - t0;
    printf("bench.mcchunk:     %7.2f Mvert/s  (%6.1f ns/vert, BLOCK fmt %d B, 1 draw/frame)\n",
           mc_big_verts / mcc_ms / 1000.0, mcc_ms * 1.0e6 / mc_big_verts, MC_STRIDE);
    bench_check_error("mcchunk");

    // bench.mcchunkmulti - the same vertices split across 16 chunk draws, each
    // with its own matrix and lightmap-unit switch. This is the real frame
    // shape: a render distance of 8 submits hundreds of these, so per-draw
    // fixed cost, not vertex rate, sets the frame time.
    t0 = now_ms();
    if (PHASE("mcchunkmulti")) {
        for (int frm = 0; frm < mc_frames; ++frm) {
            for (int ch = 0; ch < MC_CHUNKS; ++ch) {
                fPushMatrix();
                fTranslatef(0.001f * (float)ch, 0.0f, 0.0f);
                fClientActiveTexture(GL_TEXTURE1);
                fClientActiveTexture(GL_TEXTURE0);
                fDrawArrays(GL_QUADS, ch * MC_CHUNK_VERTS, MC_CHUNK_VERTS);
                fPopMatrix();
            }
        }
    }
    fFinish();
    const double mcm_ms = now_ms() - t0;
    const double mcm_draws = (double)mc_frames * MC_CHUNKS;
    printf("bench.mcchunkmulti: %6.2f Mvert/s  (%6.2f us/chunk draw, %d chunks/frame)\n",
           mc_big_verts / mcm_ms / 1000.0, mcm_ms * 1000.0 / mcm_draws, MC_CHUNKS);
    bench_check_error("mcchunkmulti");

    fDisableClientState(GL_VERTEX_ARRAY);
    fDisableClientState(GL_COLOR_ARRAY);
    fDisableClientState(GL_TEXTURE_COORD_ARRAY);
    fBindBuffer(GL_ARRAY_BUFFER, 0);

    // bench.mcgui - Gui/GuiIngame: every widget is its own immediate-mode quad
    // bracketed by a texture bind and the alpha-test/blend toggles the vanilla
    // helpers push and pop. One quad per Begin/End with state changes between,
    // which is what defeats naive batching.
    const int gui_widgets = (int)(4000 * scale) > 0 ? (int)(4000 * scale) : 1;
    fFinish();
    t0 = now_ms();
    if (PHASE("mcgui")) {
        for (int w = 0; w < gui_widgets; ++w) {
            fBindTexture(GL_TEXTURE_2D, textures[w & 1]);
            fEnable(GL_BLEND);
            fBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            fDisable(GL_ALPHA_TEST);
            draw_quads(1);
            fEnable(GL_ALPHA_TEST);
            fDisable(GL_BLEND);
        }
    }
    fFinish();
    const double gui_ms = now_ms() - t0;
    printf("bench.mcgui:       %7.2f us/widget (1 quad + bind + blend/alpha toggle, %d widgets)\n",
           gui_ms * 1000.0 / (double)gui_widgets, gui_widgets);
    bench_check_error("mcgui");

    // bench.mcfont - FontRenderer: one quad per glyph, colour set per glyph,
    // all inside a single Begin/End for the string. The per-vertex colour is
    // why the glyph path cannot simply hoist state out of the batch.
    const int font_strings = (int)(2000 * scale) > 0 ? (int)(2000 * scale) : 1;
    enum { FONT_GLYPHS = 24 };
    fBindTexture(GL_TEXTURE_2D, textures[0]);
    fFinish();
    t0 = now_ms();
    if (PHASE("mcfont")) {
        for (int s = 0; s < font_strings; ++s) {
            fBegin(GL_QUADS);
            for (int g = 0; g < FONT_GLYPHS; ++g) {
                const float x = -0.5f + 0.01f * (float)g;
                fColor4f(1.0f, 1.0f, 1.0f, 0.25f + 0.03f * (float)(g & 7));
                fTexCoord2f(0.0f, 0.0f); fVertex3f(x, 0.0f, 0.0f);
                fTexCoord2f(1.0f, 0.0f); fVertex3f(x + 0.008f, 0.0f, 0.0f);
                fTexCoord2f(1.0f, 1.0f); fVertex3f(x + 0.008f, 0.01f, 0.0f);
                fTexCoord2f(0.0f, 1.0f); fVertex3f(x, 0.01f, 0.0f);
            }
            fEnd();
        }
    }
    fFinish();
    const double font_ms = now_ms() - t0;
    printf("bench.mcfont:      %7.2f us/string (%d glyphs, per-glyph colour, one Begin/End)\n",
           font_ms * 1000.0 / (double)font_strings, FONT_GLYPHS);
    bench_check_error("mcfont");

    // bench.mcentity - ModelRenderer: each box is a compiled display list
    // replayed under its own push/rotate/pop. Mobs and armour layers make this
    // a few hundred list replays a frame.
    enum { ENT_BOXES = 12 };
    GLuint ent_lists[ENT_BOXES];
    for (int b = 0; b < ENT_BOXES; ++b) {
        ent_lists[b] = fGenLists(1);
        fNewList(ent_lists[b], GL_COMPILE);
        draw_quads(6); // a box: 6 faces
        fEndList();
    }
    const int ent_models = (int)(2000 * scale) > 0 ? (int)(2000 * scale) : 1;
    fFinish();
    t0 = now_ms();
    if (PHASE("mcentity")) {
        for (int m = 0; m < ent_models; ++m) {
            for (int b = 0; b < ENT_BOXES; ++b) {
                fPushMatrix();
                fTranslatef(0.0f, 0.01f * (float)b, 0.0f);
                fRotatef(1.0f * (float)b, 0.0f, 1.0f, 0.0f);
                fCallList(ent_lists[b]);
                fPopMatrix();
            }
        }
    }
    fFinish();
    const double ent_ms = now_ms() - t0;
    printf("bench.mcentity:    %7.2f us/model  (%d boxes, list replay + matrix each)\n",
           ent_ms * 1000.0 / (double)ent_models, ENT_BOXES);
    bench_check_error("mcentity");

    fDisable(GL_TEXTURE_2D);

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
