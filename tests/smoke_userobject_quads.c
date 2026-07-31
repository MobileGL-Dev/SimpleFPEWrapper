// SimpleFPEWrapper - tests/smoke_userobject_quads.c
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// Sodium draws terrain with its OWN program and its OWN VAO of generic
// attributes, then issues glDrawArrays(GL_QUADS, first, count). That is legal
// GL 2.1, but mode 7 does not exist in GLES: passing it through raw is
// GL_INVALID_ENUM and the whole draw is dropped. RenderDoc capture
// sfpew-1.16-sodium-issue.rdc has 503 such draws in EID 130..2311.
//
// Unlike the fixed-function paths this draw cannot be moved to fpe_vao - the
// app's attributes live in the app's VAO and must not be touched. Only the
// element binding is borrowed, and since that is VAO state it has to be
// restored. Both halves are checked here:
//   A. a GL_QUADS draw through a user program's own VAO actually renders
//   B. the app's GL_ELEMENT_ARRAY_BUFFER binding survives that draw
//   C. the non-zero `first` form (Sodium draws sub-ranges) lands correctly
//
// Skips (77) when the machine has no EGL device.

#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

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
#define GL_QUADS 0x0007
#define GL_FLOAT 0x1406
#define GL_RGBA 0x1908
#define GL_UNSIGNED_BYTE 0x1401
#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_ELEMENT_ARRAY_BUFFER_BINDING 0x8895
#define GL_STATIC_DRAW 0x88E4
#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_FALSE 0
#define GL_NO_ERROR 0

static void (*fClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
static void (*fClear)(GLbitfield);
static void (*fDrawArrays)(GLenum, GLint, GLsizei);
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
static void (*fDeleteShader)(GLuint);
static void (*fDeleteProgram)(GLuint);
static void (*fGenBuffers)(GLsizei, GLuint*);
static void (*fBindBuffer)(GLenum, GLuint);
static void (*fBufferData)(GLenum, GLsizeiptr, const void*, GLenum);
static void (*fGenVertexArrays)(GLsizei, GLuint*);
static void (*fBindVertexArray)(GLuint);
static void (*fVertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*);
static void (*fEnableVertexAttribArray)(GLuint);
static GLint (*fGetAttribLocation)(GLuint, const GLchar*);
static void (*fDrawElements)(GLenum, GLsizei, GLenum, const void*);

static int failures;

static void check(int x, int y, int r, int g, int b, const char* what) {
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
    R(fGetIntegerv,"glGetIntegerv") R(fUseProgram,"glUseProgram")
    R(fCreateShader,"glCreateShader") R(fShaderSource,"glShaderSource")
    R(fCompileShader,"glCompileShader") R(fGetShaderiv,"glGetShaderiv")
    R(fCreateProgram,"glCreateProgram") R(fAttachShader,"glAttachShader")
    R(fLinkProgram,"glLinkProgram") R(fGetProgramiv,"glGetProgramiv")
    R(fGetProgramInfoLog,"glGetProgramInfoLog") R(fDeleteShader,"glDeleteShader")
    R(fDeleteProgram,"glDeleteProgram") R(fGenBuffers,"glGenBuffers")
    R(fBindBuffer,"glBindBuffer") R(fBufferData,"glBufferData")
    R(fGenVertexArrays,"glGenVertexArrays") R(fBindVertexArray,"glBindVertexArray")
    R(fVertexAttribPointer,"glVertexAttribPointer")
    R(fEnableVertexAttribArray,"glEnableVertexAttribArray")
    R(fGetAttribLocation,"glGetAttribLocation")
    R(fDrawElements,"glDrawElements")
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
    fDeleteShader(vs); fDeleteShader(fs);

    // 8 vertices = 2 quads. Quad 0 (verts 0..3) covers the left half in red,
    // quad 1 (verts 4..7) the right half in green. Interleaved x,y,r,g,b.
    static const GLfloat verts[] = {
        -1.0f, -1.0f, 1,0,0,   0.0f, -1.0f, 1,0,0,   0.0f, 1.0f, 1,0,0,  -1.0f, 1.0f, 1,0,0,
         0.0f, -1.0f, 0,1,0,   1.0f, -1.0f, 0,1,0,   1.0f, 1.0f, 0,1,0,   0.0f, 1.0f, 0,1,0,
    };

    GLuint vao = 0, vbo = 0, appIbo = 0;
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

    // The app parks its own element buffer in this VAO. The wrapper borrows the
    // element binding to convert GL_QUADS, so this must come back unchanged.
    static const GLubyte dummy[] = {0, 1, 2};
    fGenBuffers(1, &appIbo);
    fBindBuffer(GL_ELEMENT_ARRAY_BUFFER, appIbo);
    fBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)sizeof dummy, dummy, GL_STATIC_DRAW);

    fUseProgram(prog);

    // A: both quads. Raw GL_QUADS to a GLES backend is GL_INVALID_ENUM and the
    // draw vanishes, leaving the clear color.
    fClearColor(0.0f, 0.0f, 1.0f, 1.0f);
    fClear(GL_COLOR_BUFFER_BIT);
    fDrawArrays(GL_QUADS, 0, 8);
    fFinish();
    check(16, 32, 1, 0, 0, "A: user-VAO GL_QUADS draw, left quad red");
    check(48, 32, 0, 1, 0, "A: user-VAO GL_QUADS draw, right quad green");

    // B: the app's element binding must be exactly what it set.
    GLint boundElem = -1;
    fGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &boundElem);
    if (boundElem != (GLint)appIbo) {
        fprintf(stderr, "FAIL: B: element binding is %d, expected %u\n", boundElem, appIbo);
        ++failures;
    } else {
        printf("OK: B: app's element-array binding survived the quad conversion\n");
    }

    // C: non-zero first, the form Sodium uses for sub-ranges. Only the second
    // quad is drawn, so the left half must stay at the clear color.
    fClearColor(0.0f, 0.0f, 1.0f, 1.0f);
    fClear(GL_COLOR_BUFFER_BIT);
    fDrawArrays(GL_QUADS, 4, 4);
    fFinish();
    check(48, 32, 0, 1, 0, "C: first=4 sub-range draws the second quad");
    check(16, 32, 0, 0, 1, "C: first=4 leaves the first quad undrawn");

    // D: the indexed form. Same own-VAO situation, but GL_QUADS arrives through
    // glDrawElements, so the wrapper has to read the app's indices back and
    // expand them rather than just swapping the mode.
    static const GLubyte quadIdx[] = {4, 5, 6, 7};   // the right-hand quad only
    GLuint idxBuf = 0;
    fGenBuffers(1, &idxBuf);
    fBindBuffer(GL_ELEMENT_ARRAY_BUFFER, idxBuf);
    fBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)sizeof quadIdx, quadIdx, GL_STATIC_DRAW);
    fClearColor(0.0f, 0.0f, 1.0f, 1.0f);
    fClear(GL_COLOR_BUFFER_BIT);
    fDrawElements(GL_QUADS, 4, GL_UNSIGNED_BYTE, (void*)0);
    fFinish();
    check(48, 32, 0, 1, 0, "D: indexed GL_QUADS draws the referenced quad");
    check(16, 32, 0, 0, 1, "D: indexed GL_QUADS leaves the other quad undrawn");

    fGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &boundElem);
    if (boundElem != (GLint)idxBuf) {
        fprintf(stderr, "FAIL: D: element binding is %d, expected %u\n", boundElem, idxBuf);
        ++failures;
    } else {
        printf("OK: D: app's element binding survived the indexed conversion\n");
    }

    const GLenum err = fGetError();
    if (err != GL_NO_ERROR) {
        fprintf(stderr, "FAIL: glGetError 0x%x\n", err); ++failures; }

    fUseProgram(0);
    fBindVertexArray(0);
    fDeleteProgram(prog);

    if (failures != 0) {
        fprintf(stderr, "FAIL: %d check(s) failed\n", failures); return 1; }
    printf("OK: GL_QUADS through a user program's own VAO converts correctly\n");
    return 0;
}
