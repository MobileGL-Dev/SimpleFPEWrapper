// SimpleFPEWrapper - tests/smoke_multidraw_native.c
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// glMultiDraw* forwards to the backend's native multi-draw when no sub-draw
// needs individual work, and otherwise loops over the single-draw path. Both
// halves have to stay correct, so both are exercised here:
//
//   A. native path - user program, own VAO, core mode. Each sub-draw range must
//      land in its own place, which is what catches a forward that drops or
//      mixes up first[]/count[].
//   B. loop path via mode - GL_QUADS cannot forward (it becomes an indexed draw
//      whose index count differs per sub-draw), so it must still render.
//   C. loop path via state - fixed-function arrays with a core mode must still
//      go per-draw, because commit_fpe_state_on_draw uploads only the range
//      each sub-draw covers.
//   D. indexed forwarding - glMultiDrawElements over the same shapes.
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
typedef long GLsizeiptr;

#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_TRIANGLES 0x0004
#define GL_QUADS 0x0007
#define GL_FLOAT 0x1406
#define GL_RGBA 0x1908
#define GL_UNSIGNED_BYTE 0x1401
#define GL_ARRAY_BUFFER 0x8892
#define GL_STATIC_DRAW 0x88E4
#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_VERTEX_ARRAY 0x8074
#define GL_FALSE 0
#define GL_NO_ERROR 0

static void (*fClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
static void (*fClear)(GLbitfield);
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
static void (*fGenBuffers)(GLsizei, GLuint*);
static void (*fBindBuffer)(GLenum, GLuint);
static void (*fBufferData)(GLenum, GLsizeiptr, const void*, GLenum);
static void (*fGenVertexArrays)(GLsizei, GLuint*);
static void (*fBindVertexArray)(GLuint);
static void (*fVertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*);
static void (*fEnableVertexAttribArray)(GLuint);
static GLint (*fGetAttribLocation)(GLuint, const GLchar*);
static void (*fMultiDrawArrays)(GLenum, const GLint*, const GLsizei*, GLsizei);
static void (*fMultiDrawElements)(GLenum, const GLsizei*, GLenum, const void* const*, GLsizei);
static void (*fEnableClientState)(GLenum);
static void (*fDisableClientState)(GLenum);
static void (*fVertexPointer)(GLint, GLenum, GLsizei, const void*);
static void (*fColor4f)(GLfloat, GLfloat, GLfloat, GLfloat);

static int failures;

static void checkPixel(int x, int y, int r, int g, int b, const char* what) {
    GLubyte p[4] = {0, 0, 0, 0};
    fReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p);
    const int ok = ((p[0] > 200) == (r > 0)) && ((p[1] > 200) == (g > 0)) &&
                   ((p[2] > 200) == (b > 0));
    if (!ok) {
        fprintf(stderr, "FAIL: %s: pixel(%d,%d) = (%u,%u,%u), expected (%d,%d,%d)\n", what, x, y,
                p[0], p[1], p[2], r, g, b);
        ++failures;
    } else {
        printf("OK: %s\n", what);
    }
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
    R(fClearColor,"glClearColor") R(fClear,"glClear") R(fReadPixels,"glReadPixels")
    R(fGetError,"glGetError") R(fFinish,"glFinish") R(fUseProgram,"glUseProgram")
    R(fCreateShader,"glCreateShader") R(fShaderSource,"glShaderSource")
    R(fCompileShader,"glCompileShader") R(fGetShaderiv,"glGetShaderiv")
    R(fCreateProgram,"glCreateProgram") R(fAttachShader,"glAttachShader")
    R(fLinkProgram,"glLinkProgram") R(fGetProgramiv,"glGetProgramiv")
    R(fGetProgramInfoLog,"glGetProgramInfoLog") R(fGenBuffers,"glGenBuffers")
    R(fBindBuffer,"glBindBuffer") R(fBufferData,"glBufferData")
    R(fGenVertexArrays,"glGenVertexArrays") R(fBindVertexArray,"glBindVertexArray")
    R(fVertexAttribPointer,"glVertexAttribPointer")
    R(fEnableVertexAttribArray,"glEnableVertexAttribArray")
    R(fGetAttribLocation,"glGetAttribLocation")
    R(fMultiDrawArrays,"glMultiDrawArrays") R(fMultiDrawElements,"glMultiDrawElements")
    R(fEnableClientState,"glEnableClientState") R(fDisableClientState,"glDisableClientState")
    R(fVertexPointer,"glVertexPointer") R(fColor4f,"glColor4f")
#undef R

    static const char* vsSrc =
        "#version 300 es\n"
        "in vec2 aPos;\n"
        "in vec3 aCol;\n"
        "out vec3 vCol;\n"
        "void main() { vCol = aCol; gl_Position = vec4(aPos, 0.0, 1.0); }\n";
    static const char* fsSrc =
        "#version 300 es\n"
        "precision mediump float;\n"
        "in vec3 vCol;\n"
        "out vec4 o;\n"
        "void main() { o = vec4(vCol, 1.0); }\n";

    GLuint vs = fCreateShader(GL_VERTEX_SHADER), fs = fCreateShader(GL_FRAGMENT_SHADER);
    fShaderSource(vs, 1, &vsSrc, NULL); fShaderSource(fs, 1, &fsSrc, NULL);
    fCompileShader(vs); fCompileShader(fs);
    GLint ok = 0;
    fGetShaderiv(vs, GL_COMPILE_STATUS, &ok);
    if (!ok) { fprintf(stderr, "FAIL: VS compile\n"); return 1; }
    fGetShaderiv(fs, GL_COMPILE_STATUS, &ok);
    if (!ok) { fprintf(stderr, "FAIL: FS compile\n"); return 1; }
    GLuint prog = fCreateProgram();
    fAttachShader(prog, vs); fAttachShader(prog, fs); fLinkProgram(prog);
    fGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) { char log[512]; fGetProgramInfoLog(prog, sizeof log, NULL, log);
        fprintf(stderr, "FAIL: link: %s\n", log); return 1; }

    // Three vertical bands as TRIANGLES, 6 verts each: red at x in [-1,-1/3),
    // green at [-1/3,1/3), blue at [1/3,1]. Sub-draw i is verts 6i..6i+5, so a
    // forward that ignores first[]/count[] cannot produce the right picture.
    GLfloat verts[3 * 6 * 5];
    {
        const GLfloat xs[3][2] = {{-1.0f, -1.0f / 3.0f}, {-1.0f / 3.0f, 1.0f / 3.0f},
                                  {1.0f / 3.0f, 1.0f}};
        const GLfloat cols[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
        int w = 0;
        for (int band = 0; band < 3; ++band) {
            const GLfloat x0 = xs[band][0], x1 = xs[band][1];
            const GLfloat quad[6][2] = {{x0, -1}, {x1, -1}, {x1, 1}, {x0, -1}, {x1, 1}, {x0, 1}};
            for (int v = 0; v < 6; ++v) {
                verts[w++] = quad[v][0];
                verts[w++] = quad[v][1];
                verts[w++] = cols[band][0];
                verts[w++] = cols[band][1];
                verts[w++] = cols[band][2];
            }
        }
    }

    GLuint vao = 0, vbo = 0;
    fGenVertexArrays(1, &vao);
    fBindVertexArray(vao);
    fGenBuffers(1, &vbo);
    fBindBuffer(GL_ARRAY_BUFFER, vbo);
    fBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof verts, verts, GL_STATIC_DRAW);
    const GLint locPos = fGetAttribLocation(prog, "aPos");
    const GLint locCol = fGetAttribLocation(prog, "aCol");
    if (locPos < 0 || locCol < 0) { fprintf(stderr, "FAIL: attrib locations\n"); return 1; }
    fVertexAttribPointer((GLuint)locPos, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), (void*)0);
    fVertexAttribPointer((GLuint)locCol, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat),
                         (void*)(2 * sizeof(GLfloat)));
    fEnableVertexAttribArray((GLuint)locPos);
    fEnableVertexAttribArray((GLuint)locCol);
    fUseProgram(prog);

    // A: all three bands, one call. This is the native forward.
    static const GLint firstsAll[3] = {0, 6, 12};
    static const GLsizei countsAll[3] = {6, 6, 6};
    fClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    fClear(GL_COLOR_BUFFER_BIT);
    fMultiDrawArrays(GL_TRIANGLES, firstsAll, countsAll, 3);
    fFinish();
    checkPixel(10, 32, 1, 0, 0, "A: native multidraw, band 0 red");
    checkPixel(32, 32, 0, 1, 0, "A: native multidraw, band 1 green");
    checkPixel(54, 32, 0, 0, 1, "A: native multidraw, band 2 blue");

    // A2: a subset, to pin that first[]/count[] are honoured rather than the
    // whole buffer being drawn. Only band 2 is named; bands 0 and 1 stay clear.
    static const GLint firstsOne[1] = {12};
    static const GLsizei countsOne[1] = {6};
    fClear(GL_COLOR_BUFFER_BIT);
    fMultiDrawArrays(GL_TRIANGLES, firstsOne, countsOne, 1);
    fFinish();
    checkPixel(54, 32, 0, 0, 1, "A2: subset draws the named range");
    checkPixel(10, 32, 0, 0, 0, "A2: subset leaves band 0 clear");
    checkPixel(32, 32, 0, 0, 0, "A2: subset leaves band 1 clear");

    // B: GL_QUADS cannot forward. Same geometry re-expressed as 4-vertex quads
    // so the loop path is the only way it can render.
    GLfloat quadVerts[3 * 4 * 5];
    {
        const GLfloat xs[3][2] = {{-1.0f, -1.0f / 3.0f}, {-1.0f / 3.0f, 1.0f / 3.0f},
                                  {1.0f / 3.0f, 1.0f}};
        const GLfloat cols[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
        int w = 0;
        for (int band = 0; band < 3; ++band) {
            const GLfloat x0 = xs[band][0], x1 = xs[band][1];
            const GLfloat quad[4][2] = {{x0, -1}, {x1, -1}, {x1, 1}, {x0, 1}};
            for (int v = 0; v < 4; ++v) {
                quadVerts[w++] = quad[v][0];
                quadVerts[w++] = quad[v][1];
                quadVerts[w++] = cols[band][0];
                quadVerts[w++] = cols[band][1];
                quadVerts[w++] = cols[band][2];
            }
        }
    }
    fBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof quadVerts, quadVerts, GL_STATIC_DRAW);
    static const GLint quadFirsts[3] = {0, 4, 8};
    static const GLsizei quadCounts[3] = {4, 4, 4};
    fClear(GL_COLOR_BUFFER_BIT);
    fMultiDrawArrays(GL_QUADS, quadFirsts, quadCounts, 3);
    fFinish();
    checkPixel(10, 32, 1, 0, 0, "B: GL_QUADS multidraw falls back to the loop, band 0");
    checkPixel(32, 32, 0, 1, 0, "B: GL_QUADS multidraw falls back to the loop, band 1");
    checkPixel(54, 32, 0, 0, 1, "B: GL_QUADS multidraw falls back to the loop, band 2");

    // D: indexed forwarding over the TRIANGLES layout.
    fBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof verts, verts, GL_STATIC_DRAW);
    static const GLubyte idx0[6] = {0, 1, 2, 3, 4, 5};
    static const GLubyte idx2[6] = {12, 13, 14, 15, 16, 17};
    const void* idxPtrs[2] = {idx0, idx2};
    static const GLsizei idxCounts[2] = {6, 6};
    fClear(GL_COLOR_BUFFER_BIT);
    fMultiDrawElements(GL_TRIANGLES, idxCounts, GL_UNSIGNED_BYTE, idxPtrs, 2);
    fFinish();
    checkPixel(10, 32, 1, 0, 0, "D: indexed multidraw, band 0 red");
    checkPixel(54, 32, 0, 0, 1, "D: indexed multidraw, band 2 blue");
    checkPixel(32, 32, 0, 0, 0, "D: indexed multidraw skips the unreferenced band");

    fUseProgram(0);
    fBindVertexArray(0);

    // C: fixed-function arrays with a core mode must NOT forward - each sub-draw
    // needs its own state commit and upload. Client memory, no user program.
    static const GLfloat ffVerts[] = {
        -1, -1,  -1.0f / 3.0f, -1,  -1.0f / 3.0f, 1,
        -1, -1,  -1.0f / 3.0f,  1,  -1,           1,
         1.0f / 3.0f, -1,  1, -1,  1, 1,
         1.0f / 3.0f, -1,  1,  1,  1.0f / 3.0f, 1,
    };
    static const GLint ffFirsts[2] = {0, 6};
    static const GLsizei ffCounts[2] = {6, 6};
    fClearColor(0.0f, 0.0f, 1.0f, 1.0f);
    fClear(GL_COLOR_BUFFER_BIT);
    fColor4f(0.0f, 1.0f, 0.0f, 1.0f);
    fEnableClientState(GL_VERTEX_ARRAY);
    fVertexPointer(2, GL_FLOAT, 0, ffVerts);
    fMultiDrawArrays(GL_TRIANGLES, ffFirsts, ffCounts, 2);
    fFinish();
    checkPixel(10, 32, 0, 1, 0, "C: fixed-function multidraw, sub-draw 0 green");
    checkPixel(54, 32, 0, 1, 0, "C: fixed-function multidraw, sub-draw 1 green");
    checkPixel(32, 32, 0, 0, 1, "C: fixed-function multidraw leaves the gap clear");
    fDisableClientState(GL_VERTEX_ARRAY);

    const GLenum err = fGetError();
    if (err != GL_NO_ERROR) {
        fprintf(stderr, "FAIL: glGetError 0x%x\n", err); ++failures; }

    if (failures != 0) {
        fprintf(stderr, "FAIL: %d check(s) failed\n", failures); return 1; }
    printf("OK: glMultiDraw* forwards natively where valid and loops where not\n");
    return 0;
}
