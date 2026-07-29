// SimpleFPEWrapper - tests/smoke_ffp_vbo_arrays.c
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// Fixed-function vertex arrays sourced from the app's own VBO: glVertexPointer
// and glColorPointer given BUFFER OFFSETS while GL_ARRAY_BUFFER is bound,
// rather than client-memory pointers. That is how Minecraft 1.16 + OptiFine
// feeds its world draws, and nothing else in the suite covered it - every
// other fixed-function test passes client pointers, which takes the opposite
// branch (upload into the wrapper's own ring buffer).
//
// It is also the shape where sfpewBackendBindAttributeBuffer() elides its
// glBindBuffer: the attribute source IS the buffer the app already has bound,
// and the draw guard leaves the backend on the app's binding, so the bind is a
// provable no-op. If that elision were wrong the draw would source attributes
// from whatever buffer happened to be bound - the wrapper's ring, or nothing -
// and the geometry would be garbage or missing.
//
// Two draws back to back: the second is the one whose bind gets skipped once
// the first has established the binding.
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
#define GL_COLOR_ARRAY 0x8076
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
static void (*fGenBuffers)(GLsizei, GLuint*);
static void (*fBindBuffer)(GLenum, GLuint);
static void (*fBufferData)(GLenum, GLsizeiptr, const void*, GLenum);
static void (*fEnableClientState)(GLenum);
static void (*fDisableClientState)(GLenum);
static void (*fVertexPointer)(GLint, GLenum, GLsizei, const void*);
static void (*fColorPointer)(GLint, GLenum, GLsizei, const void*);

static int failures;

static void expect(int x, int y, int r, int g, int b, const char* what) {
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
    R(fClearColor,"glClearColor") R(fClear,"glClear") R(fDrawArrays,"glDrawArrays")
    R(fReadPixels,"glReadPixels") R(fGetError,"glGetError") R(fFinish,"glFinish")
    R(fUseProgram,"glUseProgram") R(fCreateShader,"glCreateShader")
    R(fShaderSource,"glShaderSource") R(fCompileShader,"glCompileShader")
    R(fGetShaderiv,"glGetShaderiv") R(fCreateProgram,"glCreateProgram")
    R(fAttachShader,"glAttachShader") R(fLinkProgram,"glLinkProgram")
    R(fGetProgramiv,"glGetProgramiv") R(fGetProgramInfoLog,"glGetProgramInfoLog")
    R(fGenBuffers,"glGenBuffers") R(fBindBuffer,"glBindBuffer")
    R(fBufferData,"glBufferData")
    R(fEnableClientState,"glEnableClientState")
    R(fDisableClientState,"glDisableClientState")
    R(fVertexPointer,"glVertexPointer") R(fColorPointer,"glColorPointer")
#undef R

    static const char* vsSrc =
        "#version 300 es\n"
        "in vec4 fpe_Vertex;\n"
        "in vec4 fpe_Color;\n"
        "out vec4 vCol;\n"
        "void main() { vCol = fpe_Color; gl_Position = fpe_Vertex; }\n";
    static const char* fsSrc =
        "#version 300 es\n"
        "precision mediump float;\n"
        "in vec4 vCol;\n"
        "out vec4 o;\n"
        "void main() { o = vCol; }\n";

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

    // Interleaved x,y,r,g,b,a - two full-screen triangles, all green. Held in
    // the app's OWN buffer; the fixed-function pointers below are offsets into
    // it, so starting_pointer stays under the stride and the wrapper sources
    // attributes straight from this buffer instead of uploading a copy.
    static const GLfloat verts[] = {
        -1,-1, 0,1,0,1,   1,-1, 0,1,0,1,   1,1, 0,1,0,1,
        -1,-1, 0,1,0,1,   1, 1, 0,1,0,1,  -1,1, 0,1,0,1,
    };
    const GLsizei stride = 6 * (GLsizei)sizeof(GLfloat);

    GLuint vbo = 0;
    fGenBuffers(1, &vbo);
    fBindBuffer(GL_ARRAY_BUFFER, vbo);
    fBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof verts, verts, GL_STATIC_DRAW);

    fVertexPointer(2, GL_FLOAT, stride, (const void*)0);
    fColorPointer(4, GL_FLOAT, stride, (const void*)(2 * sizeof(GLfloat)));
    fEnableClientState(GL_VERTEX_ARRAY);
    fEnableClientState(GL_COLOR_ARRAY);
    fUseProgram(prog);

    fClearColor(0.0f, 0.0f, 1.0f, 1.0f);
    fClear(GL_COLOR_BUFFER_BIT);
    fDrawArrays(GL_TRIANGLES, 0, 6);
    fFinish();
    expect(32, 32, 0, 1, 0, "draw 1: VBO-backed FFP arrays -> green (centre)");
    expect(6, 6, 0, 1, 0, "draw 1: VBO-backed FFP arrays -> green (corner)");

    // Draw 2 is the one whose attribute-buffer bind is elided: the app's
    // binding is unchanged and the guard still holds it.
    fClear(GL_COLOR_BUFFER_BIT);
    fDrawArrays(GL_TRIANGLES, 0, 6);
    fFinish();
    expect(32, 32, 0, 1, 0, "draw 2: repeated VBO-backed draw -> green (bind elided)");
    expect(6, 6, 0, 1, 0, "draw 2: repeated VBO-backed draw -> green (corner)");

    const GLenum err = fGetError();
    if (err != GL_NO_ERROR) {
        fprintf(stderr, "FAIL: glGetError 0x%x\n", err); ++failures; }

    fDisableClientState(GL_VERTEX_ARRAY);
    fDisableClientState(GL_COLOR_ARRAY);
    fUseProgram(0);
    fBindBuffer(GL_ARRAY_BUFFER, 0);

    if (failures != 0) {
        fprintf(stderr, "FAIL: %d check(s) failed\n", failures); return 1; }
    printf("OK: fixed-function arrays source correctly from the app's own VBO\n");
    return 0;
}
