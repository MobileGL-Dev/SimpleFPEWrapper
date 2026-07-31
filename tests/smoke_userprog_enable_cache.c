// SimpleFPEWrapper - tests/smoke_userprog_enable_cache.c
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// sfpewSendUserProgramAttributes() skips glEnableVertexAttribArray when
// fpe_user_vao_enabled already has the bit: enable state is per-VAO and
// persists, and that VAO is wrapper-owned so the mask is authoritative.
// RenderDoc 1.16-Optifine/1-frame19661.rdc measured 365 of 373 enables on it
// as redundant - 9.9% of the whole frame's call count.
//
// The failure mode that skip could introduce is an attribute that the mask
// claims is enabled while the VAO has it disabled: geometry silently loses an
// input. The mask is only cleared by a draw whose layout DROPS that slot, so
// the case to pin is shrink-then-regrow:
//
//   draw 1  fpe_Vertex + fpe_Color   -> both enabled, mask = {0,2}
//   draw 2  fpe_Vertex only          -> color disabled, mask = {0}
//   draw 3  fpe_Vertex + fpe_Color   -> color MUST be enabled again
//
// If the skip consulted a stale mask, draw 3 would read color from the
// constant current-value instead of the array and come out wrong.
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

#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_TRIANGLES 0x0004
#define GL_FLOAT 0x1406
#define GL_RGBA 0x1908
#define GL_UNSIGNED_BYTE 0x1401
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
static void (*fEnableClientState)(GLenum);
static void (*fDisableClientState)(GLenum);
static void (*fVertexPointer)(GLint, GLenum, GLsizei, const void*);
static void (*fColorPointer)(GLint, GLenum, GLsizei, const void*);
static void (*fColor4f)(GLfloat, GLfloat, GLfloat, GLfloat);

static int failures;

// Full-coverage draws, so one interior pixel characterises the whole draw.
static void expect(int r, int g, int b, const char* what) {
    GLubyte p[4] = {0, 0, 0, 0};
    fReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p);
    const int ok = ((p[0] > 200) == (r > 0)) && ((p[1] > 200) == (g > 0)) &&
                   ((p[2] > 200) == (b > 0));
    if (!ok) {
        fprintf(stderr, "FAIL: %s: pixel = (%u,%u,%u), expected (%d,%d,%d)\n", what, p[0], p[1],
                p[2], r, g, b);
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
    R(fEnableClientState,"glEnableClientState")
    R(fDisableClientState,"glDisableClientState")
    R(fVertexPointer,"glVertexPointer") R(fColorPointer,"glColorPointer")
    R(fColor4f,"glColor4f")
#undef R

    // fpe_-named inputs are what sfpewUserProgramAttribLocations resolves, so
    // this program is fed by the fixed-function arrays through fpe_user_vao -
    // the VAO whose enable mask this test is about.
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

    // Two full-screen triangles; the color array is green everywhere.
    static const GLfloat pos[] = {
        -1,-1,  1,-1,  1,1,   -1,-1,  1,1,  -1,1,
    };
    static const GLfloat green[] = {
        0,1,0,1,  0,1,0,1,  0,1,0,1,  0,1,0,1,  0,1,0,1,  0,1,0,1,
    };

    fUseProgram(prog);
    fVertexPointer(2, GL_FLOAT, 0, pos);
    fColorPointer(4, GL_FLOAT, 0, green);

    // Draw 1: both arrays on. Establishes mask = {fpe_Vertex, fpe_Color}.
    fEnableClientState(GL_VERTEX_ARRAY);
    fEnableClientState(GL_COLOR_ARRAY);
    fClearColor(0.0f, 0.0f, 1.0f, 1.0f);
    fClear(GL_COLOR_BUFFER_BIT);
    fDrawArrays(GL_TRIANGLES, 0, 6);
    fFinish();
    expect(0, 1, 0, "draw 1: vertex+color arrays -> green");

    // Draw 2: same layout repeated. This is the draw whose enables the mask
    // lets us skip; it must still render identically.
    fClear(GL_COLOR_BUFFER_BIT);
    fDrawArrays(GL_TRIANGLES, 0, 6);
    fFinish();
    expect(0, 1, 0, "draw 2: repeated layout still green (enable skipped)");

    // Draw 3: layout SHRINKS - color array off, constant red current value.
    // The wrapper must disable that location and feed the constant.
    fDisableClientState(GL_COLOR_ARRAY);
    fColor4f(1.0f, 0.0f, 0.0f, 1.0f);
    fClear(GL_COLOR_BUFFER_BIT);
    fDrawArrays(GL_TRIANGLES, 0, 6);
    fFinish();
    expect(1, 0, 0, "draw 3: color array off -> constant red");

    // Draw 4: layout REGROWS. The color location was disabled by draw 3, so
    // its mask bit is clear and the enable must be re-issued. A stale mask
    // would leave it disabled and keep feeding the red constant.
    fEnableClientState(GL_COLOR_ARRAY);
    fClear(GL_COLOR_BUFFER_BIT);
    fDrawArrays(GL_TRIANGLES, 0, 6);
    fFinish();
    expect(0, 1, 0, "draw 4: color array back on -> green again (enable re-issued)");

    const GLenum err = fGetError();
    if (err != GL_NO_ERROR) {
        fprintf(stderr, "FAIL: glGetError 0x%x\n", err); ++failures; }

    fDisableClientState(GL_VERTEX_ARRAY);
    fDisableClientState(GL_COLOR_ARRAY);
    fUseProgram(0);

    if (failures != 0) {
        fprintf(stderr, "FAIL: %d check(s) failed\n", failures); return 1; }
    printf("OK: fpe_user_vao enable mask survives shrink and regrow\n");
    return 0;
}
