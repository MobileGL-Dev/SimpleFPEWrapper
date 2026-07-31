// SimpleFPEWrapper - tests/smoke_userobject_blit.c
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// Minecraft 1.16 fabulous mode: PostPass binds its OWN shader program that
// declares "in vec4 Position" (not gl_Vertex, not fpe_Vertex), then feeds the
// blit quad through glVertexPointer + glDrawArrays(GL_QUADS). On desktop GL the
// legacy aliasing rule routes generic attribute 0 to gl_Vertex, so attribute 0
// (Position) receives the vertex data automatically. The wrapper's
// sfpewUserProgramAttribLocations probed only fpe_Vertex, found nothing, and
// returned false - the draw fell through to raw glDrawArrays(GL_QUADS) on the
// GLES backend with nothing bound to Position, collapsing all four vertices to
// (0,0,0,1). RenderDoc capture, 1.16 fabulous, EID 1844.
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

#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_QUADS 0x0007
#define GL_TRIANGLES 0x0004
#define GL_FLOAT 0x1406
#define GL_RGBA 0x1908
#define GL_UNSIGNED_BYTE 0x1401
#define GL_VERTEX_ARRAY 0x8074
#define GL_LINK_STATUS 0x8B82
#define GL_INFO_LOG_LENGTH 0x8B84
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_COMPILE_STATUS 0x8B81
#define GL_NO_ERROR 0

static void (*fClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
static void (*fClear)(GLbitfield);
static void (*fEnableClientState)(GLenum);
static void (*fDisableClientState)(GLenum);
static void (*fVertexPointer)(GLint, GLenum, GLsizei, const void*);
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
static void (*fDeleteShader)(GLuint);
static void (*fDeleteProgram)(GLuint);

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
    R(fClearColor,"glClearColor") R(fClear,"glClear")
    R(fEnableClientState,"glEnableClientState") R(fDisableClientState,"glDisableClientState")
    R(fVertexPointer,"glVertexPointer") R(fDrawArrays,"glDrawArrays")
    R(fReadPixels,"glReadPixels") R(fGetError,"glGetError") R(fFinish,"glFinish")
    R(fUseProgram,"glUseProgram") R(fCreateShader,"glCreateShader")
    R(fShaderSource,"glShaderSource") R(fCompileShader,"glCompileShader")
    R(fGetShaderiv,"glGetShaderiv") R(fCreateProgram,"glCreateProgram")
    R(fAttachShader,"glAttachShader") R(fLinkProgram,"glLinkProgram")
    R(fGetProgramiv,"glGetProgramiv") R(fGetProgramInfoLog,"glGetProgramInfoLog")
    R(fDeleteShader,"glDeleteShader") R(fDeleteProgram,"glDeleteProgram")

    // A shader that uses "Position" at location 0 (like MC 1.16 PostPass/blit).
    // The name is NOT fpe_Vertex, so the wrapper must fall back to the GL
    // attribute-0 / gl_Vertex aliasing rule.
    static const char* vsSrc =
        "#version 300 es\n"
        "in vec4 Position;\n"
        "void main() { gl_Position = Position; }\n";
    static const char* fsSrc =
        "#version 300 es\n"
        "precision mediump float;\n"
        "out vec4 o;\n"
        "void main() { o = vec4(1.0, 0.0, 0.0, 1.0); }\n";

    GLuint vs = fCreateShader(GL_VERTEX_SHADER);
    GLuint fs = fCreateShader(GL_FRAGMENT_SHADER);
    fShaderSource(vs, 1, &vsSrc, NULL);
    fShaderSource(fs, 1, &fsSrc, NULL);
    fCompileShader(vs); fCompileShader(fs);
    GLint ok = 0;
    fGetShaderiv(vs, GL_COMPILE_STATUS, &ok);
    if (!ok) { fprintf(stderr, "FAIL: VS compile failed\n"); return 1; }
    fGetShaderiv(fs, GL_COMPILE_STATUS, &ok);
    if (!ok) { fprintf(stderr, "FAIL: FS compile failed\n"); return 1; }
    GLuint prog = fCreateProgram();
    fAttachShader(prog, vs); fAttachShader(prog, fs);
    fLinkProgram(prog);
    fGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512]; fGetProgramInfoLog(prog, sizeof(log), NULL, log);
        fprintf(stderr, "FAIL: link: %s\n", log); return 1; }
    fDeleteShader(vs); fDeleteShader(fs);

    // Feed the blit quad through the fixed-function vertex array path, exactly
    // as MC 1.16's BufferUploader.end does.
    static const GLfloat quad[] = {
        -1.0f, -1.0f, 0.0f,
         1.0f, -1.0f, 0.0f,
         1.0f,  1.0f, 0.0f,
        -1.0f,  1.0f, 0.0f,
    };
    fClearColor(0.0f, 0.0f, 1.0f, 1.0f);
    fClear(GL_COLOR_BUFFER_BIT);
    fUseProgram(prog);
    fEnableClientState(GL_VERTEX_ARRAY);
    fVertexPointer(3, GL_FLOAT, 0, quad);
    fDrawArrays(GL_QUADS, 0, 4);
    fFinish();

    // Without the aliasing fallback this was raw glDrawArrays(GL_QUADS) on the
    // ES backend with nothing bound to Position: all four vertices read
    // (0,0,0,1) and the quad collapsed to one point (center pixel stayed blue).
    check(32, 32, 1, 0, 0, "user program with 'in vec4 Position' receives vertex array data");

    const GLenum err = fGetError();
    if (err != GL_NO_ERROR) {
        fprintf(stderr, "FAIL: glGetError 0x%x\n", err); ++failures; }

    fUseProgram(0);
    fDisableClientState(GL_VERTEX_ARRAY);
    fDeleteProgram(prog);

    if (failures != 0) {
        fprintf(stderr, "FAIL: %d check(s) failed\n", failures); return 1; }
    printf("OK: generic-attribute-0 aliasing feeds 'Position' from glVertexPointer\n");
    return 0;
}
