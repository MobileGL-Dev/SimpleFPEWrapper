// SimpleFPEWrapper - tests/smoke_gl3_core.c
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// A GL 3.3 core frontend, doing exactly what a GLAD/GLEW/LWJGL3 engine
// does: discover the version, then render through a VAO with explicit
// layout(location = N) attributes and its own fragment output. Two
// wrapper-side failures used to make this impossible - an "OpenGL ES"
// version string that desktop loaders cannot parse, and a compat prelude
// whose auto-mapped attributes and fpe_FragColor collided with the
// shader's own declarations.
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <EGL/egl.h>

typedef unsigned int GLenum, GLuint, GLbitfield;
typedef unsigned char GLubyte, GLboolean;
typedef float GLfloat;
typedef int GLint, GLsizei;
typedef char GLchar;
typedef long GLsizeiptr;

#define WIN 64
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_RGBA 0x1908
#define GL_UNSIGNED_BYTE 0x1401
#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_ARRAY_BUFFER 0x8892
#define GL_STATIC_DRAW 0x88E4
#define GL_FLOAT 0x1406
#define GL_TRIANGLES 0x0004
#define GL_MAJOR_VERSION 0x821B
#define GL_MINOR_VERSION 0x821C
#define GL_VERSION 0x1F02
#define GL_SHADING_LANGUAGE_VERSION 0x8B8C

static void* (*resolve)(const char*);
static int failures = 0;

int main(void) {
    void* h = dlopen(WRAPPER_LIB_PATH, RTLD_NOW | RTLD_LOCAL);
    if (!h) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 1; }
    *(void**)(&resolve) = dlsym(h, "eglGetProcAddress");

    EGLDisplay d = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (d == EGL_NO_DISPLAY || !eglInitialize(d, NULL, NULL)) { printf("SKIP: no EGL display\n"); return 77; }
    const EGLint ca[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE,
                         EGL_OPENGL_ES3_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
                         EGL_ALPHA_SIZE, 8, EGL_NONE};
    EGLConfig c; EGLint n = 0;
    if (!eglChooseConfig(d, ca, &c, 1, &n) || n == 0) { printf("SKIP: no ES3 config\n"); return 77; }
    const EGLint pa[] = {EGL_WIDTH, WIN, EGL_HEIGHT, WIN, EGL_NONE};
    EGLSurface s = eglCreatePbufferSurface(d, c, pa);
    eglBindAPI(EGL_OPENGL_ES_API);
    const EGLint xa[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    EGLContext x = eglCreateContext(d, c, EGL_NO_CONTEXT, xa);
    if (!eglMakeCurrent(d, s, s, x)) { printf("SKIP: no current context\n"); return 77; }

#define R(v, nm) *(void**)(&v) = resolve(nm); if (!v) { fprintf(stderr, "*** MISSING %s\n", nm); ++failures; }
    const GLubyte* (*fGetString)(GLenum); R(fGetString, "glGetString")
    void (*fGetIntegerv)(GLenum, GLint*); R(fGetIntegerv, "glGetIntegerv")
    void (*fGenVertexArrays)(GLsizei, GLuint*); R(fGenVertexArrays, "glGenVertexArrays")
    void (*fBindVertexArray)(GLuint); R(fBindVertexArray, "glBindVertexArray")
    void (*fGenBuffers)(GLsizei, GLuint*); R(fGenBuffers, "glGenBuffers")
    void (*fBindBuffer)(GLenum, GLuint); R(fBindBuffer, "glBindBuffer")
    void (*fBufferData)(GLenum, GLsizeiptr, const void*, GLenum); R(fBufferData, "glBufferData")
    GLuint (*fCreateShader)(GLenum); R(fCreateShader, "glCreateShader")
    void (*fShaderSource)(GLuint, GLsizei, const GLchar* const*, const GLint*); R(fShaderSource, "glShaderSource")
    void (*fCompileShader)(GLuint); R(fCompileShader, "glCompileShader")
    void (*fGetShaderiv)(GLuint, GLenum, GLint*); R(fGetShaderiv, "glGetShaderiv")
    void (*fGetShaderInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*); R(fGetShaderInfoLog, "glGetShaderInfoLog")
    GLuint (*fCreateProgram)(void); R(fCreateProgram, "glCreateProgram")
    void (*fAttachShader)(GLuint, GLuint); R(fAttachShader, "glAttachShader")
    void (*fLinkProgram)(GLuint); R(fLinkProgram, "glLinkProgram")
    void (*fGetProgramiv)(GLuint, GLenum, GLint*); R(fGetProgramiv, "glGetProgramiv")
    void (*fGetProgramInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*); R(fGetProgramInfoLog, "glGetProgramInfoLog")
    void (*fUseProgram)(GLuint); R(fUseProgram, "glUseProgram")
    void (*fVertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*); R(fVertexAttribPointer, "glVertexAttribPointer")
    void (*fEnableVertexAttribArray)(GLuint); R(fEnableVertexAttribArray, "glEnableVertexAttribArray")
    void (*fClearColor)(GLfloat, GLfloat, GLfloat, GLfloat); R(fClearColor, "glClearColor")
    void (*fClear)(GLbitfield); R(fClear, "glClear")
    void (*fDrawArrays)(GLenum, GLint, GLsizei); R(fDrawArrays, "glDrawArrays")
    void (*fReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*); R(fReadPixels, "glReadPixels")
    GLenum (*fGetError)(void); R(fGetError, "glGetError")
    if (failures) return 1;

    // --- Phase 1: version discovery, the way GLAD/GLEW do it ---
    GLint major = -1, minor = -1;
    fGetIntegerv(GL_MAJOR_VERSION, &major);
    fGetIntegerv(GL_MINOR_VERSION, &minor);
    const char* ver = (const char*)fGetString(GL_VERSION);
    const char* glsl = (const char*)fGetString(GL_SHADING_LANGUAGE_VERSION);
    printf("GL_MAJOR/MINOR_VERSION = %d.%d\n", major, minor);
    printf("GL_VERSION             = %s\n", ver ? ver : "(null)");
    printf("GL_SHADING_LANGUAGE_VERSION = %s\n", glsl ? glsl : "(null)");
    // A desktop loader parses the leading "<major>.<minor>" of GL_VERSION.
    if (!ver || ver[0] < '0' || ver[0] > '9') {
        printf("*** GL_VERSION does not start with a digit: a desktop GL loader "
               "(GLAD/GLEW/LWJGL3) cannot parse it and aborts init\n");
        ++failures;
    }
    if (major < 3) { printf("*** GL_MAJOR_VERSION < 3\n"); ++failures; }
    if (!glsl || glsl[0] < '0' || glsl[0] > '9') {
        printf("*** GL_SHADING_LANGUAGE_VERSION does not start with a digit\n");
        ++failures;
    }
    // The reported string and the integer queries must tell one story.
    if (ver && ver[0] >= '0' && ver[0] <= '9' && (ver[0] - '0') != major) {
        printf("*** GL_VERSION says %c.x but GL_MAJOR_VERSION says %d\n", ver[0], major);
        ++failures;
    }
    // The backend's identity must stay visible (additive contract).
    if (ver && !strstr(ver, "OpenGL ES") && major >= 3 && minor >= 0) {
        printf("note: desktop backend reported verbatim\n");
    }

    // --- Phase 2: the core-profile render path ---
    GLuint vao = 0, vbo = 0;
    fGenVertexArrays(1, &vao);
    fBindVertexArray(vao);
    fGenBuffers(1, &vbo);
    fBindBuffer(GL_ARRAY_BUFFER, vbo);
    // interleaved position(2) + color(3), fed via explicit locations
    static const GLfloat verts[] = {
        -1, -1, 1, 0, 0,
         3, -1, 1, 0, 0,
        -1,  3, 1, 0, 0,
    };
    fBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);

    static const char* vs_src =
        "#version 330 core\n"
        "layout(location = 0) in vec2 aPos;\n"
        "layout(location = 1) in vec3 aColor;\n"
        "out vec3 vColor;\n"
        "void main() { gl_Position = vec4(aPos, 0.0, 1.0); vColor = aColor; }\n";
    static const char* fs_src =
        "#version 330 core\n"
        "in vec3 vColor;\n"
        "out vec4 FragColor;\n"
        "void main() { FragColor = vec4(vColor, 1.0); }\n";

    GLuint vs = fCreateShader(GL_VERTEX_SHADER);
    fShaderSource(vs, 1, &vs_src, NULL);
    fCompileShader(vs);
    GLint ok = 0;
    fGetShaderiv(vs, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096] = {0};
        fGetShaderInfoLog(vs, sizeof(log), NULL, log);
        printf("*** VS compile failed:\n%s\n", log);
        ++failures;
    }
    GLuint fs = fCreateShader(GL_FRAGMENT_SHADER);
    fShaderSource(fs, 1, &fs_src, NULL);
    fCompileShader(fs);
    fGetShaderiv(fs, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096] = {0};
        fGetShaderInfoLog(fs, sizeof(log), NULL, log);
        printf("*** FS compile failed:\n%s\n", log);
        ++failures;
    }
    GLuint prog = fCreateProgram();
    fAttachShader(prog, vs);
    fAttachShader(prog, fs);
    fLinkProgram(prog);
    fGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096] = {0};
        fGetProgramInfoLog(prog, sizeof(log), NULL, log);
        printf("*** link failed:\n%s\n", log);
        ++failures;
    }
    fUseProgram(prog);

    // The app addresses the locations it declared in the shader; it never
    // calls glBindAttribLocation nor glGetAttribLocation.
    fVertexAttribPointer(0, 2, GL_FLOAT, 0, 5 * sizeof(GLfloat), (const void*)0);
    fEnableVertexAttribArray(0);
    fVertexAttribPointer(1, 3, GL_FLOAT, 0, 5 * sizeof(GLfloat),
                         (const void*)(2 * sizeof(GLfloat)));
    fEnableVertexAttribArray(1);

    fClearColor(0, 0, 1, 1);
    fClear(GL_COLOR_BUFFER_BIT);
    fDrawArrays(GL_TRIANGLES, 0, 3);

    GLubyte px[4] = {0, 0, 0, 0};
    fReadPixels(WIN / 2, WIN / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    printf("center pixel = (%u,%u,%u,%u), expected red (255,0,0)\n", px[0], px[1], px[2], px[3]);
    if (px[0] < 200 || px[2] > 50) {
        printf("*** the GL3 core draw did not land: explicit layout(location) "
               "inputs are not reaching the declared slots\n");
        ++failures;
    }
    const GLenum err = fGetError();
    if (err) { printf("*** GL error 0x%x\n", err); ++failures; }
    printf("\nfailures: %d\n", failures);
    return failures != 0;
}
