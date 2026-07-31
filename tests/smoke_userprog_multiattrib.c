// SimpleFPEWrapper - tests/smoke_userprog_multiattrib.c
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// The generic-attribute-0 aliasing rule (GL 2.1: attribute 0 aliases gl_Vertex)
// lets a shader that declares its own single position input be fed by
// glVertexPointer - that is the MC 1.16 PostPass/blit shape and
// smoke_userobject_blit covers it.
//
// But a shader with SEVERAL attributes pairs with its own VAO/VBO and must NOT
// be hijacked into consuming the fixed-function arrays. OptiFine's 1.12 world
// shader is that shape (position + color + texcoord + ...). When it was still
// current in the program shadow and the game returned to the main menu, every
// FFP draw routed into sfpewUserProgramFixedFunctionDrawArrays, which wired
// position only: no color, no texcoords, everything black. RenderDoc capture
// 1.12-Optifine/1-black-screen.rdc shows the signature - 26 draws, all with a
// single glVertexAttribPointer(index=0) and every texture bound to 0.
//
// This test pins the discrimination: a multi-attribute program must render
// through its OWN vertex state, unaffected by any enabled client arrays.
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
#define GL_FALSE 0
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
static void (*fGenVertexArrays)(GLsizei, GLuint*);
static void (*fBindVertexArray)(GLuint);
static void (*fVertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*);
static void (*fEnableVertexAttribArray)(GLuint);
static GLint (*fGetAttribLocation)(GLuint, const GLchar*);
static void (*fEnableClientState)(GLenum);
static void (*fDisableClientState)(GLenum);
static void (*fVertexPointer)(GLint, GLenum, GLsizei, const void*);
static void (*fColorPointer)(GLint, GLenum, GLsizei, const void*);

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
    R(fUseProgram,"glUseProgram") R(fCreateShader,"glCreateShader")
    R(fShaderSource,"glShaderSource") R(fCompileShader,"glCompileShader")
    R(fGetShaderiv,"glGetShaderiv") R(fCreateProgram,"glCreateProgram")
    R(fAttachShader,"glAttachShader") R(fLinkProgram,"glLinkProgram")
    R(fGetProgramiv,"glGetProgramiv") R(fGetProgramInfoLog,"glGetProgramInfoLog")
    R(fGenBuffers,"glGenBuffers") R(fBindBuffer,"glBindBuffer")
    R(fBufferData,"glBufferData") R(fGenVertexArrays,"glGenVertexArrays")
    R(fBindVertexArray,"glBindVertexArray")
    R(fVertexAttribPointer,"glVertexAttribPointer")
    R(fEnableVertexAttribArray,"glEnableVertexAttribArray")
    R(fGetAttribLocation,"glGetAttribLocation")
    R(fEnableClientState,"glEnableClientState")
    R(fDisableClientState,"glDisableClientState")
    R(fVertexPointer,"glVertexPointer") R(fColorPointer,"glColorPointer")
#undef R

    // Multi-attribute program, OptiFine-world-shader shaped: position at
    // location 0 plus a color input. It owns its vertex state entirely.
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

    // Full-screen green quad in the program's own VAO, interleaved x,y,r,g,b.
    static const GLfloat verts[] = {
        -1,-1, 0,1,0,   1,-1, 0,1,0,   1,1, 0,1,0,
        -1,-1, 0,1,0,   1, 1, 0,1,0,  -1,1, 0,1,0,
    };
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

    // Fixed-function client arrays are ALSO enabled and point at unrelated
    // geometry. This is the trap: the wrapper must not treat them as this
    // program's vertex source. Color is deliberately red so a hijack is
    // visible as red-or-black instead of green.
    static const GLfloat ffPos[] = { -0.2f,-0.2f, 0.2f,-0.2f, 0.0f,0.2f };
    static const GLfloat ffCol[] = { 1,0,0,1,  1,0,0,1,  1,0,0,1 };
    fEnableClientState(GL_VERTEX_ARRAY);
    fEnableClientState(GL_COLOR_ARRAY);
    fVertexPointer(2, GL_FLOAT, 0, ffPos);
    fColorPointer(4, GL_FLOAT, 0, ffCol);

    fUseProgram(prog);
    fClearColor(0.0f, 0.0f, 1.0f, 1.0f);
    fClear(GL_COLOR_BUFFER_BIT);
    fDrawArrays(GL_TRIANGLES, 0, 6);
    fFinish();

    // The program's own green quad must cover the framebuffer. A hijack wires
    // only position from ffPos and drops the color input, which reads as
    // black (or leaves the clear color where the small triangle misses).
    check(32, 32, 0, 1, 0, "multi-attribute program draws from its own VAO (center)");
    check(6, 6, 0, 1, 0, "multi-attribute program draws from its own VAO (corner)");

    const GLenum err = fGetError();
    if (err != GL_NO_ERROR) {
        fprintf(stderr, "FAIL: glGetError 0x%x\n", err); ++failures; }

    fDisableClientState(GL_VERTEX_ARRAY);
    fDisableClientState(GL_COLOR_ARRAY);
    fUseProgram(0);
    fBindVertexArray(0);

    if (failures != 0) {
        fprintf(stderr, "FAIL: %d check(s) failed\n", failures); return 1; }
    printf("OK: multi-attribute user program is not hijacked by client arrays\n");
    return 0;
}
