// SimpleFPEWrapper - tests/smoke_vbo_surface.c
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// plans/12: the buffer / vertex-attribute surface goes through the wrapper.
//
// glBufferData, glVertexAttribPointer, glEnableVertexAttribArray and friends
// read and write through the bound GL_ARRAY_BUFFER and VAO. They used to
// resolve to the backend's own pointers, leaving the wrapper shadowing the
// array-buffer binding while blind to writes through it. This drives them the
// way an LWJGL frontend does and interleaves fixed-function immediate draws,
// which is the case that breaks if the wrapper ever leaves its own ring buffer
// or VAO bound across an entry point:
//   A: VBO + attributes + user program renders (proves plain pass-through)
//   B: an immediate-mode fixed-function draw between two VBO draws
//   C: the VBO draw again, unchanged - its buffer and attribute state must
//      have survived the fixed-function draw in between
//   D: the ARB spellings (LWJGL2 prefers them) reach the same wrappers
//   E: glGetBufferParameteriv / glMapBufferRange / glUnmapBuffer round-trip

#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

#include <EGL/egl.h>

typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef unsigned char GLubyte;
typedef unsigned char GLboolean;
typedef float GLfloat;
typedef int GLint, GLsizei;
typedef char GLchar;
typedef unsigned int GLbitfield;
typedef long GLsizeiptr, GLintptr;

#define GL_ARRAY_BUFFER 0x8892
#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE0 0x84C0
#define GL_STATIC_DRAW 0x88E4
#define GL_BUFFER_SIZE 0x8764
#define GL_MAP_READ_BIT 0x0001
#define GL_TRIANGLES 0x0004
#define GL_QUADS 0x0007
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_FLOAT 0x1406
#define GL_FALSE 0
#define GL_RGBA 0x1908
#define GL_UNSIGNED_BYTE 0x1401
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_NO_ERROR 0

static void* (*resolve)(const char*);
#define R(dst, name)                                                                               \
    do {                                                                                           \
        *(void**)(&dst) = resolve(name);                                                           \
        if (!dst) {                                                                                \
            fprintf(stderr, "FAIL: cannot resolve %s\n", name);                                    \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static GLuint (*fCreateShader)(GLenum);
static void (*fShaderSource)(GLuint, GLsizei, const GLchar* const*, const GLint*);
static void (*fCompileShader)(GLuint);
static void (*fGetShaderiv)(GLuint, GLenum, GLint*);
static void (*fGetShaderInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*);
static GLuint (*fCreateProgram)(void);
static void (*fAttachShader)(GLuint, GLuint);
static void (*fLinkProgram)(GLuint);
static void (*fGetProgramiv)(GLuint, GLenum, GLint*);
static void (*fUseProgram)(GLuint);
static GLint (*fGetAttribLocation)(GLuint, const GLchar*);

static void (*fGenBuffers)(GLsizei, GLuint*);
static void (*fBindBuffer)(GLenum, GLuint);
static void (*fBufferData)(GLenum, GLsizeiptr, const void*, GLenum);
static void (*fBufferSubData)(GLenum, GLintptr, GLsizeiptr, const void*);
static void (*fGetBufferParameteriv)(GLenum, GLenum, GLint*);
static void* (*fMapBufferRange)(GLenum, GLintptr, GLsizeiptr, GLbitfield);
static GLboolean (*fUnmapBuffer)(GLenum);
static void (*fVertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*);
static void (*fEnableVertexAttribArray)(GLuint);
static void (*fDisableVertexAttribArray)(GLuint);

static void (*fClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
static void (*fClear)(GLbitfield);
static void (*fDrawArrays)(GLenum, GLint, GLsizei);
static void (*fBegin)(GLenum);
static void (*fEnd)(void);
static void (*fColor4f)(GLfloat, GLfloat, GLfloat, GLfloat);
static void (*fVertex2f)(GLfloat, GLfloat);
static void (*fReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*);
static GLenum (*fGetError)(void);
static void (*fBindTexture)(GLenum, GLuint);
static void (*fActiveTexture)(GLenum);

// Draws whatever is in the bound array buffer, tinted by a uniform-free
// constant so the colour identifies which path produced the pixel.
static const char* kVS = "attribute vec2 aPos;\n"
                         "attribute vec3 aColor;\n"
                         "varying vec3 vColor;\n"
                         "void main() {\n"
                         "    vColor = aColor;\n"
                         "    gl_Position = vec4(aPos, 0.0, 1.0);\n"
                         "}\n";

static const char* kFS = "varying vec3 vColor;\n"
                         "void main() { gl_FragColor = vec4(vColor, 1.0); }\n";

static GLuint buildProgram(void) {
    GLuint vs = fCreateShader(GL_VERTEX_SHADER);
    fShaderSource(vs, 1, &kVS, NULL);
    fCompileShader(vs);
    GLint ok = 0;
    fGetShaderiv(vs, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024] = {0};
        fGetShaderInfoLog(vs, sizeof log, NULL, log);
        fprintf(stderr, "FAIL: vertex shader: %s\n", log);
        return 0;
    }
    GLuint fs = fCreateShader(GL_FRAGMENT_SHADER);
    fShaderSource(fs, 1, &kFS, NULL);
    fCompileShader(fs);
    fGetShaderiv(fs, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024] = {0};
        fGetShaderInfoLog(fs, sizeof log, NULL, log);
        fprintf(stderr, "FAIL: fragment shader: %s\n", log);
        return 0;
    }
    GLuint prog = fCreateProgram();
    fAttachShader(prog, vs);
    fAttachShader(prog, fs);
    fLinkProgram(prog);
    fGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        fprintf(stderr, "FAIL: link\n");
        return 0;
    }
    return prog;
}

static int checkCenter(const char* tag, int r, int g, int b) {
    GLubyte px[4] = {0, 0, 0, 0};
    fReadPixels(128, 128, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    const int tol = 8;
    if (px[0] < r - tol || px[0] > r + tol || px[1] < g - tol || px[1] > g + tol ||
        px[2] < b - tol || px[2] > b + tol) {
        fprintf(stderr, "FAIL[%s]: center pixel (%u,%u,%u), expected (%d,%d,%d)\n", tag, px[0],
                px[1], px[2], r, g, b);
        return 0;
    }
    printf("OK: %s -> (%u,%u,%u)\n", tag, px[0], px[1], px[2]);
    return 1;
}

// A green full-screen triangle pair, interleaved position+colour.
static const GLfloat kQuad[] = {
    -1.0f, -1.0f, 0.0f, 1.0f, 0.0f, 1.0f,  -1.0f, 0.0f, 1.0f, 0.0f,
    1.0f,  1.0f,  0.0f, 1.0f, 0.0f, -1.0f, -1.0f, 0.0f, 1.0f, 0.0f,
    1.0f,  1.0f,  0.0f, 1.0f, 0.0f, -1.0f, 1.0f,  0.0f, 1.0f, 0.0f,
};

// A fixed-function immediate run long enough that the small-run merger declines
// it, so it draws at glEnd and leaves the wrapper's program/VAO/buffers live.
// A 4-vertex run would instead sit pending until the next barrier, and that
// barrier restores right after flushing - which is exactly the window the C2/C3
// checks below need to exist in order to police it.
static void fpeDrawUnmerged(void) {
    fBegin(GL_QUADS);
    fColor4f(1.0f, 0.0f, 0.0f, 1.0f);
    for (int i = 0; i < 20; ++i) { // 80 vertices, past the 64-vertex merge limit
        const GLfloat y0 = -0.5f + (GLfloat)i * 0.05f;
        const GLfloat y1 = y0 + 0.05f;
        fVertex2f(-0.5f, y0);
        fVertex2f(0.5f, y0);
        fVertex2f(0.5f, y1);
        fVertex2f(-0.5f, y1);
    }
    fEnd();
}

static int drawVbo(GLuint prog, GLuint vbo, GLint locPos, GLint locColor, const char* tag, int r,
                   int g, int b) {
    fClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    fClear(GL_COLOR_BUFFER_BIT);
    fUseProgram(prog);
    fBindBuffer(GL_ARRAY_BUFFER, vbo);
    fVertexAttribPointer((GLuint)locPos, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), (void*)0);
    fVertexAttribPointer((GLuint)locColor, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat),
                         (void*)(2 * sizeof(GLfloat)));
    fEnableVertexAttribArray((GLuint)locPos);
    fEnableVertexAttribArray((GLuint)locColor);
    fDrawArrays(GL_TRIANGLES, 0, 6);
    return checkCenter(tag, r, g, b);
}

static int run(void) {
    GLuint prog = buildProgram();
    if (prog == 0) return 1;
    const GLint locPos = fGetAttribLocation(prog, "aPos");
    const GLint locColor = fGetAttribLocation(prog, "aColor");
    if (locPos < 0 || locColor < 0) {
        fprintf(stderr, "FAIL: attribute locations (%d, %d)\n", locPos, locColor);
        return 1;
    }

    GLuint vbo = 0;
    fGenBuffers(1, &vbo);
    if (vbo == 0) {
        fprintf(stderr, "FAIL: glGenBuffers gave 0\n");
        return 1;
    }
    fBindBuffer(GL_ARRAY_BUFFER, vbo);
    fBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof kQuad, kQuad, GL_STATIC_DRAW);

    // E: the buffer really holds what glBufferData was given, read back
    // through the wrapper's own query and mapping entry points.
    GLint size = 0;
    fGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &size);
    if (size != (GLint)sizeof kQuad) {
        fprintf(stderr, "FAIL: GL_BUFFER_SIZE %d, expected %zu\n", size, sizeof kQuad);
        return 1;
    }
    const GLfloat* mapped =
        (const GLfloat*)fMapBufferRange(GL_ARRAY_BUFFER, 0, (GLsizeiptr)sizeof kQuad,
                                        GL_MAP_READ_BIT);
    if (mapped == NULL) {
        fprintf(stderr, "FAIL: glMapBufferRange returned NULL\n");
        return 1;
    }
    if (memcmp(mapped, kQuad, sizeof kQuad) != 0) {
        fprintf(stderr, "FAIL: mapped contents differ from glBufferData input\n");
        fUnmapBuffer(GL_ARRAY_BUFFER);
        return 1;
    }
    if (!fUnmapBuffer(GL_ARRAY_BUFFER)) {
        fprintf(stderr, "FAIL: glUnmapBuffer\n");
        return 1;
    }
    printf("OK: glBufferData/glGetBufferParameteriv/glMapBufferRange round-trip\n");

    // A: the VBO path renders at all.
    if (!drawVbo(prog, vbo, locPos, locColor, "A: VBO + attributes through user program", 0, 255, 0))
        return 1;

    // B: a fixed-function immediate draw in between. This is the wrapper
    // binding its own program, VAO and ring buffer; by the time glEnd returns
    // the app's must be back, or C below renders wrong.
    fUseProgram(0);
    fClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    fClear(GL_COLOR_BUFFER_BIT);
    fBegin(GL_QUADS);
    fColor4f(1.0f, 0.0f, 0.0f, 1.0f);
    fVertex2f(-1.0f, -1.0f);
    fVertex2f(1.0f, -1.0f);
    fVertex2f(1.0f, 1.0f);
    fVertex2f(-1.0f, 1.0f);
    fEnd();
    if (!checkCenter("B: fixed-function immediate quad between VBO draws", 255, 0, 0)) return 1;

    // C: the same VBO draw as A, with no re-upload. Its buffer contents and
    // attribute pointers have to have survived B untouched.
    if (!drawVbo(prog, vbo, locPos, locColor, "C: VBO draw unchanged after the FPE draw", 0, 255, 0))
        return 1;

    // C2: the one that actually polices the draw guard. Everything is set up
    // BEFORE the fixed-function draw and deliberately NOT re-established
    // after it, so the draw below runs on whatever program, array buffer and
    // VAO-0 attribute state the wrapper left behind. C above cannot catch a
    // leak because drawVbo re-binds all three itself.
    fUseProgram(prog);
    fBindBuffer(GL_ARRAY_BUFFER, vbo);
    fVertexAttribPointer((GLuint)locPos, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), (void*)0);
    fVertexAttribPointer((GLuint)locColor, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat),
                         (void*)(2 * sizeof(GLfloat)));
    fEnableVertexAttribArray((GLuint)locPos);
    fEnableVertexAttribArray((GLuint)locColor);

    fpeDrawUnmerged(); // wrapper binds its own program / VAO / ring buffer here

    fClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    fClear(GL_COLOR_BUFFER_BIT);
    fDrawArrays(GL_TRIANGLES, 0, 6); // no re-bind: relies on the state above
    if (!checkCenter("C2: pre-FPE program/buffer/attribute state survived glEnd", 0, 255, 0))
        return 1;

    // C3: same as C2, but a texture bind sits between the fixed-function draw
    // and the app's draw. glBindTexture deliberately does NOT hand the app's
    // program/VAO/buffers back (it cannot observe them, and restoring there
    // cost a full save/restore cycle per texture switch - plans/12), so the
    // wrapper's state stays live across it. glDrawArrays' own barrier is then
    // the only thing standing between that and the app drawing with the
    // wrapper's program.
    fUseProgram(prog);
    fBindBuffer(GL_ARRAY_BUFFER, vbo);
    fVertexAttribPointer((GLuint)locPos, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), (void*)0);
    fVertexAttribPointer((GLuint)locColor, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat),
                         (void*)(2 * sizeof(GLfloat)));
    fEnableVertexAttribArray((GLuint)locPos);
    fEnableVertexAttribArray((GLuint)locColor);

    // The clear goes BEFORE the fixed-function draw on purpose: glClearColor
    // and glClear both fire the full barrier, so placing them after the texture
    // bind would restore the app's state and this check could never fail.
    // glDrawArrays' own barrier is then the only restore left in the sequence.
    fClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    fClear(GL_COLOR_BUFFER_BIT);

    fpeDrawUnmerged();
    fBindTexture(GL_TEXTURE_2D, 0); // the narrowed barrier: deliberately no restore
    fActiveTexture(GL_TEXTURE0);

    fDrawArrays(GL_TRIANGLES, 0, 6); // still no re-bind
    if (!checkCenter("C3: app state survived an FPE draw followed by a texture bind", 0, 255, 0))
        return 1;

    // D: glBufferSubData through the wrapper repaints the quad blue, proving
    // writes land in the app's buffer and not somewhere the wrapper owns.
    GLfloat blue[sizeof kQuad / sizeof kQuad[0]];
    memcpy(blue, kQuad, sizeof kQuad);
    for (size_t v = 0; v < 6; ++v) {
        blue[v * 5 + 2] = 0.0f;
        blue[v * 5 + 3] = 0.0f;
        blue[v * 5 + 4] = 1.0f;
    }
    fBindBuffer(GL_ARRAY_BUFFER, vbo);
    fBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)sizeof blue, blue);
    if (!drawVbo(prog, vbo, locPos, locColor, "D: glBufferSubData recoloured the app's buffer", 0, 0,
                 255))
        return 1;

    fDisableVertexAttribArray((GLuint)locPos);
    fDisableVertexAttribArray((GLuint)locColor);
    fBindBuffer(GL_ARRAY_BUFFER, 0);

    const GLenum err = fGetError();
    if (err != GL_NO_ERROR) {
        fprintf(stderr, "FAIL: glGetError 0x%x\n", err);
        return 1;
    }
    printf("OK: buffer/vertex-attribute surface routes through the wrapper\n");
    return 0;
}

int main(void) {
    void* handle = dlopen(WRAPPER_LIB_PATH, RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        fprintf(stderr, "FAIL: dlopen: %s\n", dlerror());
        return 1;
    }
    *(void**)(&resolve) = dlsym(handle, "eglGetProcAddress");
    if (!resolve) return 1;

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
    static const EGLint pb[] = {EGL_WIDTH, 256, EGL_HEIGHT, 256, EGL_NONE};
    EGLSurface surface = eglCreatePbufferSurface(display, config, pb);
    eglBindAPI(EGL_OPENGL_ES_API);
    static const EGLint ctx_attribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, ctx_attribs);
    if (surface == EGL_NO_SURFACE || context == EGL_NO_CONTEXT ||
        !eglMakeCurrent(display, surface, surface, context)) {
        printf("SKIP: no current ES3 context\n");
        return 77;
    }

    R(fCreateShader, "glCreateShader");
    R(fShaderSource, "glShaderSource");
    R(fCompileShader, "glCompileShader");
    R(fGetShaderiv, "glGetShaderiv");
    R(fGetShaderInfoLog, "glGetShaderInfoLog");
    R(fCreateProgram, "glCreateProgram");
    R(fAttachShader, "glAttachShader");
    R(fLinkProgram, "glLinkProgram");
    R(fGetProgramiv, "glGetProgramiv");
    R(fUseProgram, "glUseProgram");
    R(fGetAttribLocation, "glGetAttribLocation");
    R(fGenBuffers, "glGenBuffers");
    R(fBindBuffer, "glBindBuffer");
    R(fBufferData, "glBufferData");
    R(fBufferSubData, "glBufferSubData");
    R(fGetBufferParameteriv, "glGetBufferParameteriv");
    R(fMapBufferRange, "glMapBufferRange");
    R(fUnmapBuffer, "glUnmapBuffer");
    R(fVertexAttribPointer, "glVertexAttribPointer");
    R(fEnableVertexAttribArray, "glEnableVertexAttribArray");
    R(fDisableVertexAttribArray, "glDisableVertexAttribArray");
    R(fClearColor, "glClearColor");
    R(fClear, "glClear");
    R(fDrawArrays, "glDrawArrays");
    R(fBegin, "glBegin");
    R(fEnd, "glEnd");
    R(fColor4f, "glColor4f");
    R(fVertex2f, "glVertex2f");
    R(fReadPixels, "glReadPixels");
    R(fGetError, "glGetError");
    R(fBindTexture, "glBindTexture");
    R(fActiveTexture, "glActiveTexture");
    return run();
}
