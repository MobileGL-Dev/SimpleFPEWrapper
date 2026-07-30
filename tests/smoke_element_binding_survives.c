// SimpleFPEWrapper - tests/smoke_element_binding_survives.c
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header
//
// An app that keeps its indices in a GL_ELEMENT_ARRAY_BUFFER on VAO 0 must find
// that binding intact after a fixed-function draw runs in between. The wrapper
// binds its own element buffer for GL_QUADS index synthesis, and the draw guard
// restores the app's binding afterwards.
//
// sfpewFlushDeferredDrawState() now elides that restore when its shadow proves
// VAO 0 already holds the value - which is the normal case, because the wrapper
// binds its element buffer while its OWN VAO is current and an element binding
// is VAO state. Measured 100% redundant (20000/20000 restores in bench.mcgui,
// 140000/140000 in bench.mcentity), so the restore was pure per-draw cost.
//
// Nothing in the suite covered the case the elision could break: making the
// elision unconditional passed all 680 tests. This test pins the contract, so a
// future path that does disturb VAO 0's element binding - or a shadow that
// stops being maintained - fails here instead of silently drawing garbage.
//
// Sequence, all on VAO 0:
//   1. app uploads indices to its own element buffer, draws from it  -> green
//   2. a fixed-function immediate GL_QUADS draw runs in between (this is what
//      makes the wrapper bind its own element buffer and hold the app's state)
//   3. app repeats its indexed draw WITHOUT re-binding anything      -> green
//
// Step 3 is the assertion: if the element binding did not survive, the draw
// reads indices from whatever the wrapper left bound - its own quad-index
// buffer, whose contents are unrelated - and the result is not the green quad.
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
typedef unsigned short GLushort;

#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_TRIANGLES 0x0004
#define GL_QUADS 0x0007
#define GL_FLOAT 0x1406
#define GL_RGBA 0x1908
#define GL_UNSIGNED_BYTE 0x1401
#define GL_UNSIGNED_SHORT 0x1403
#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_STATIC_DRAW 0x88E4
#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_VERTEX_ARRAY 0x8074
#define GL_COLOR_ARRAY 0x8076
#define GL_ELEMENT_ARRAY_BUFFER_BINDING 0x8895
#define GL_FALSE 0
#define GL_NO_ERROR 0

static void (*fClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
static void (*fClear)(GLbitfield);
static void (*fDrawElements)(GLenum, GLsizei, GLenum, const void*);
static void (*fReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*);
static GLenum (*fGetError)(void);
static void (*fFinish)(void);
static void (*fGetIntegerv)(GLenum, GLint*);
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
static void (*fVertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*);
static void (*fEnableVertexAttribArray)(GLuint);
static GLint (*fGetAttribLocation)(GLuint, const GLchar*);
// Fixed-function immediate mode, for the interleaved draw.
static void (*fBegin)(GLenum);
static void (*fEnd)(void);
static void (*fColor4f)(GLfloat, GLfloat, GLfloat, GLfloat);
static void (*fVertex2f)(GLfloat, GLfloat);

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
    R(fClearColor,"glClearColor") R(fClear,"glClear") R(fDrawElements,"glDrawElements")
    R(fReadPixels,"glReadPixels") R(fGetError,"glGetError") R(fFinish,"glFinish")
    R(fGetIntegerv,"glGetIntegerv")
    R(fUseProgram,"glUseProgram") R(fCreateShader,"glCreateShader")
    R(fShaderSource,"glShaderSource") R(fCompileShader,"glCompileShader")
    R(fGetShaderiv,"glGetShaderiv") R(fCreateProgram,"glCreateProgram")
    R(fAttachShader,"glAttachShader") R(fLinkProgram,"glLinkProgram")
    R(fGetProgramiv,"glGetProgramiv") R(fGetProgramInfoLog,"glGetProgramInfoLog")
    R(fGenBuffers,"glGenBuffers") R(fBindBuffer,"glBindBuffer")
    R(fBufferData,"glBufferData")
    R(fVertexAttribPointer,"glVertexAttribPointer")
    R(fEnableVertexAttribArray,"glEnableVertexAttribArray")
    R(fGetAttribLocation,"glGetAttribLocation")
    R(fBegin,"glBegin") R(fEnd,"glEnd") R(fColor4f,"glColor4f") R(fVertex2f,"glVertex2f")
#undef R

    // A plain program with its own attributes, so the indexed draws below are
    // pure passthrough and depend only on the element binding being intact.
    static const char* vsSrc =
        "#version 300 es\n"
        "in vec2 aPos;\n"
        "void main() { gl_Position = vec4(aPos, 0.0, 1.0); }\n";
    static const char* fsSrc =
        "#version 300 es\n"
        "precision mediump float;\n"
        "out vec4 o;\n"
        "void main() { o = vec4(0.0, 1.0, 0.0, 1.0); }\n";

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

    // Four corners; the app's indices select two triangles covering the screen.
    static const GLfloat verts[] = { -1,-1,  1,-1,  1,1,  -1,1 };
    static const GLushort idx[] = { 0, 1, 2, 0, 2, 3 };

    GLuint vbo = 0, ibo = 0;
    fGenBuffers(1, &vbo);
    fBindBuffer(GL_ARRAY_BUFFER, vbo);
    fBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof verts, verts, GL_STATIC_DRAW);
    fGenBuffers(1, &ibo);
    fBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
    fBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)sizeof idx, idx, GL_STATIC_DRAW);

    const GLint locPos = fGetAttribLocation(prog, "aPos");
    if (locPos < 0) { fprintf(stderr, "FAIL: attrib location\n"); return 1; }
    fUseProgram(prog);
    fVertexAttribPointer((GLuint)locPos, 2, GL_FLOAT, GL_FALSE, 0, (void*)0);
    fEnableVertexAttribArray((GLuint)locPos);

    fClearColor(0.0f, 0.0f, 1.0f, 1.0f);

    // 1. Baseline: the app's indexed draw works.
    fClear(GL_COLOR_BUFFER_BIT);
    fDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, (void*)0);
    fFinish();
    expect(32, 32, 0, 1, 0, "step 1: app indexed draw from its own element buffer");

    // 2. A fixed-function GL_QUADS draw in between. This is what makes the
    //    wrapper bind its own element buffer (quad index synthesis) and take
    //    the deferred save of the app's state.
    fUseProgram(0);
    fBegin(GL_QUADS);
    fColor4f(1.0f, 0.0f, 0.0f, 1.0f);
    fVertex2f(-0.2f, -0.2f); fVertex2f(0.2f, -0.2f);
    fVertex2f(0.2f, 0.2f);   fVertex2f(-0.2f, 0.2f);
    fEnd();
    fFinish();

    // The app's element binding must still be reported as its own buffer. This
    // catches a lost binding directly, not just through the rendering.
    GLint bound = 0;
    fGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &bound);
    if (bound != (GLint)ibo) {
        fprintf(stderr, "FAIL: element binding is %d after the FPE draw, expected %u\n",
                bound, ibo);
        ++failures;
    } else {
        printf("OK: step 2: element binding still the app's buffer after an FPE draw\n");
    }

    // 3. The app repeats its indexed draw WITHOUT re-binding anything.
    fUseProgram(prog);
    fClear(GL_COLOR_BUFFER_BIT);
    fDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, (void*)0);
    fFinish();
    expect(32, 32, 0, 1, 0, "step 3: indexed draw still correct with no re-bind");
    expect(4, 4, 0, 1, 0, "step 3: covers the corner too (indices intact)");

    const GLenum err = fGetError();
    if (err != GL_NO_ERROR) {
        fprintf(stderr, "FAIL: glGetError 0x%x\n", err); ++failures; }

    fUseProgram(0);
    fBindBuffer(GL_ARRAY_BUFFER, 0);
    fBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    if (failures != 0) {
        fprintf(stderr, "FAIL: %d check(s) failed\n", failures); return 1; }
    printf("OK: the app's VAO-0 element binding survives a fixed-function draw\n");
    return 0;
}
