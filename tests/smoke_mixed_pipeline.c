// SimpleFPEWrapper - tests/smoke_mixed_pipeline.c
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// plans/09 S9 mixed pipeline (the OptiFine shader-pack shape, captured in
// sfpew-bad-shaderpack.rdc): a USER program bound while geometry arrives
// through FIXED-FUNCTION channels. The fragment shader swaps R<->B, so a
// red input rendering BLUE proves the user shader really ran - the old
// bug substituted the wrapper's internal FPE program (identity colors) or
// dropped the draw entirely (GL_QUADS passthrough, unwired attributes).
//   A: glVertexPointer/glColorPointer (independent client arrays) +
//      glDrawArrays(GL_QUADS)
//   B: glBegin/glEnd immediate quad
//   C: glDrawElements(GL_QUADS, client ushort indices)
//   D: glBindAttribLocation pinning a custom attribute (mc_Entity-style)
//      must survive translation (no hardcoded layout(location) override).

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

#define GL_QUADS 0x0007
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_VERTEX_ARRAY 0x8074
#define GL_COLOR_ARRAY 0x8076
#define GL_FLOAT 0x1406
#define GL_UNSIGNED_SHORT 0x1403
#define GL_RGBA 0x1908
#define GL_UNSIGNED_BYTE 0x1401
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30

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
static void (*fBindAttribLocation)(GLuint, GLuint, const GLchar*);
static void (*fVertexAttrib4f)(GLuint, GLfloat, GLfloat, GLfloat, GLfloat);
static void (*fClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
static void (*fClear)(GLbitfield);
static void (*fEnableClientState)(GLenum);
static void (*fDisableClientState)(GLenum);
static void (*fVertexPointer)(GLint, GLenum, GLsizei, const void*);
static void (*fColorPointer)(GLint, GLenum, GLsizei, const void*);
static void (*fDrawArrays)(GLenum, GLint, GLsizei);
static void (*fDrawElements)(GLenum, GLsizei, GLenum, const void*);
static void (*fBegin)(GLenum);
static void (*fEnd)(void);
static void (*fColor4f)(GLfloat, GLfloat, GLfloat, GLfloat);
static void (*fVertex2f)(GLfloat, GLfloat);
static void (*fReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*);
static GLenum (*fGetError)(void);

static GLuint compileStage(GLenum stage, const char* src, const char* tag) {
    GLuint shader = fCreateShader(stage);
    fShaderSource(shader, 1, &src, NULL);
    fCompileShader(shader);
    GLint ok = 0;
    fGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096] = {0};
        fGetShaderInfoLog(shader, sizeof(log), NULL, log);
        fprintf(stderr, "FAIL: %s compile:\n%s\n", tag, log);
        return 0;
    }
    return shader;
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
    R(fBindAttribLocation, "glBindAttribLocation");
    R(fVertexAttrib4f, "glVertexAttrib4f");
    R(fClearColor, "glClearColor");
    R(fClear, "glClear");
    R(fEnableClientState, "glEnableClientState");
    R(fDisableClientState, "glDisableClientState");
    R(fVertexPointer, "glVertexPointer");
    R(fColorPointer, "glColorPointer");
    R(fDrawArrays, "glDrawArrays");
    R(fDrawElements, "glDrawElements");
    R(fBegin, "glBegin");
    R(fEnd, "glEnd");
    R(fColor4f, "glColor4f");
    R(fVertex2f, "glVertex2f");
    R(fReadPixels, "glReadPixels");
    R(fGetError, "glGetError");

    // R<->B swapping shader pair: proves WHICH program produced the pixel.
    static const char* kVS = "#version 120\n"
                             "varying vec4 col;\n"
                             "void main() { gl_Position = gl_Vertex; col = gl_Color; }\n";
    static const char* kFS = "#version 120\n"
                             "varying vec4 col;\n"
                             "void main() { gl_FragColor = vec4(col.b, col.g, col.r, 1.0); }\n";
    const GLuint vs = compileStage(GL_VERTEX_SHADER, kVS, "vs");
    const GLuint fs = compileStage(GL_FRAGMENT_SHADER, kFS, "fs");
    if (!vs || !fs) return 1;
    GLuint prog = fCreateProgram();
    fAttachShader(prog, vs);
    fAttachShader(prog, fs);
    fLinkProgram(prog);
    GLint linked = 0;
    fGetProgramiv(prog, GL_LINK_STATUS, &linked);
    if (!linked) {
        fprintf(stderr, "FAIL: link\n");
        return 1;
    }
    fUseProgram(prog);

    // --- A: user program + independent client arrays + GL_QUADS ----------
    static const GLfloat quad_pos[8] = {-1, -1, 1, -1, 1, 1, -1, 1};
    static const GLfloat quad_red[16] = {1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1};
    fClearColor(0, 0, 0, 1);
    fClear(GL_COLOR_BUFFER_BIT);
    fEnableClientState(GL_VERTEX_ARRAY);
    fEnableClientState(GL_COLOR_ARRAY);
    fVertexPointer(2, GL_FLOAT, 0, quad_pos);
    fColorPointer(4, GL_FLOAT, 0, quad_red);
    fDrawArrays(GL_QUADS, 0, 4);
    if (!checkCenter("A: client arrays + QUADS through user shader (red->blue)", 0, 0, 255))
        return 1;

    // --- C: same arrays via glDrawElements(GL_QUADS) ----------------------
    static const unsigned short quad_idx[4] = {0, 1, 2, 3};
    fClear(GL_COLOR_BUFFER_BIT);
    fDrawElements(GL_QUADS, 4, GL_UNSIGNED_SHORT, quad_idx);
    if (!checkCenter("C: client-index QUADS through user shader (red->blue)", 0, 0, 255)) return 1;
    fDisableClientState(GL_VERTEX_ARRAY);
    fDisableClientState(GL_COLOR_ARRAY);

    // --- B: user program + immediate mode --------------------------------
    fClear(GL_COLOR_BUFFER_BIT);
    fColor4f(0.0f, 1.0f, 0.0f, 1.0f); // green: swap keeps green (control for
    fBegin(GL_QUADS);                 // "did the draw happen at all")
    fVertex2f(-1, -1);
    fVertex2f(1, -1);
    fVertex2f(1, 1);
    fVertex2f(-1, 1);
    fEnd();
    if (!checkCenter("B1: immediate QUADS through user shader (green)", 0, 255, 0)) return 1;
    fClear(GL_COLOR_BUFFER_BIT);
    fColor4f(1.0f, 0.0f, 0.0f, 1.0f); // red -> must come out BLUE
    fBegin(GL_QUADS);
    fVertex2f(-1, -1);
    fVertex2f(1, -1);
    fVertex2f(1, 1);
    fVertex2f(-1, 1);
    fEnd();
    if (!checkCenter("B2: immediate QUADS through user shader (red->blue)", 0, 0, 255)) return 1;

    // --- D: glBindAttribLocation pinning must survive translation --------
    static const char* kVSAttr =
        "#version 120\n"
        "attribute vec4 mc_Entity;\n"
        "varying vec4 col;\n"
        "void main() { gl_Position = gl_Vertex; col = mc_Entity; }\n";
    static const char* kFSAttr = "#version 120\n"
                                 "varying vec4 col;\n"
                                 "void main() { gl_FragColor = col; }\n";
    const GLuint vs2 = compileStage(GL_VERTEX_SHADER, kVSAttr, "vs-attr");
    const GLuint fs2 = compileStage(GL_FRAGMENT_SHADER, kFSAttr, "fs-attr");
    if (!vs2 || !fs2) return 1;
    GLuint prog2 = fCreateProgram();
    fAttachShader(prog2, vs2);
    fAttachShader(prog2, fs2);
    fBindAttribLocation(prog2, 11, "mc_Entity"); // the OptiFine pattern
    fLinkProgram(prog2);
    fGetProgramiv(prog2, GL_LINK_STATUS, &linked);
    if (!linked) {
        fprintf(stderr, "FAIL: attr-pin program link\n");
        return 1;
    }
    fUseProgram(prog2);
    fVertexAttrib4f(11, 0.0f, 1.0f, 1.0f, 1.0f); // cyan via the PINNED slot
    fClear(GL_COLOR_BUFFER_BIT);
    fEnableClientState(GL_VERTEX_ARRAY);
    fVertexPointer(2, GL_FLOAT, 0, quad_pos);
    fDrawArrays(GL_QUADS, 0, 4);
    if (!checkCenter("D: glBindAttribLocation(11, mc_Entity) honored (cyan)", 0, 255, 255))
        return 1;

    // --- E: alpha test applies to USER programs (cutout foliage) ---------
    // GL 2.1 runs the alpha test after the fragment shader, so it must cull
    // fragments of a user program too; GLES has no alpha test at all, hence
    // the emulation. This is what legacy Minecraft's grass/leaves rely on:
    // without it the transparent parts of the texture paint over the scene.
    static const char* kFSAlpha = "#version 120\n"
                                  "varying vec4 col;\n"
                                  "void main() { gl_FragColor = vec4(1.0, 0.0, 0.0, col.a); }\n";
    const GLuint fs3 = compileStage(GL_FRAGMENT_SHADER, kFSAlpha, "fs-alpha");
    if (!fs3) return 1;
    GLuint prog3 = fCreateProgram();
    fAttachShader(prog3, vs);
    fAttachShader(prog3, fs3);
    fLinkProgram(prog3);
    fGetProgramiv(prog3, GL_LINK_STATUS, &linked);
    if (!linked) {
        fprintf(stderr, "FAIL: alpha-test program link\n");
        return 1;
    }
    fUseProgram(prog3);
    fEnableClientState(GL_VERTEX_ARRAY);
    fEnableClientState(GL_COLOR_ARRAY);
    fVertexPointer(2, GL_FLOAT, 0, quad_pos);

    void (*fAlphaFunc)(GLenum, GLfloat);
    void (*fEnable)(GLenum);
    void (*fDisable)(GLenum);
    R(fAlphaFunc, "glAlphaFunc");
    R(fEnable, "glEnable");
    R(fDisable, "glDisable");

    // alpha 0.25 vs ref 0.5 with GL_GREATER: every fragment must be culled,
    // leaving the green clear color untouched.
    static const GLfloat quad_a25[16] = {1, 1, 1, 0.25f, 1, 1, 1, 0.25f,
                                         1, 1, 1, 0.25f, 1, 1, 1, 0.25f};
    static const GLfloat quad_a75[16] = {1, 1, 1, 0.75f, 1, 1, 1, 0.75f,
                                         1, 1, 1, 0.75f, 1, 1, 1, 0.75f};
    fColorPointer(4, GL_FLOAT, 0, quad_a25);
    fEnable(0x0BC0 /* GL_ALPHA_TEST */);
    fAlphaFunc(0x0204 /* GL_GREATER */, 0.5f);
    fClearColor(0.0f, 1.0f, 0.0f, 1.0f);
    fClear(GL_COLOR_BUFFER_BIT);
    fDrawArrays(GL_QUADS, 0, 4);
    if (!checkCenter("E1: alpha 0.25 GREATER 0.5 -> discarded (green survives)", 0, 255, 0))
        return 1;

    // Same state, alpha 0.75: the fragment passes and paints red.
    fColorPointer(4, GL_FLOAT, 0, quad_a75);
    fClear(GL_COLOR_BUFFER_BIT);
    fDrawArrays(GL_QUADS, 0, 4);
    if (!checkCenter("E2: alpha 0.75 GREATER 0.5 -> kept (red)", 255, 0, 0)) return 1;

    // Disabling the test must let the low-alpha fragment through again.
    fDisable(0x0BC0);
    fColorPointer(4, GL_FLOAT, 0, quad_a25);
    fClear(GL_COLOR_BUFFER_BIT);
    fDrawArrays(GL_QUADS, 0, 4);
    if (!checkCenter("E3: alpha test disabled -> kept (red)", 255, 0, 0)) return 1;

    // GL_LESS is the mirror image: 0.25 passes, 0.75 does not.
    fEnable(0x0BC0);
    fAlphaFunc(0x0201 /* GL_LESS */, 0.5f);
    fClear(GL_COLOR_BUFFER_BIT);
    fDrawArrays(GL_QUADS, 0, 4);
    if (!checkCenter("E4: alpha 0.25 LESS 0.5 -> kept (red)", 255, 0, 0)) return 1;
    fColorPointer(4, GL_FLOAT, 0, quad_a75);
    fClear(GL_COLOR_BUFFER_BIT);
    fDrawArrays(GL_QUADS, 0, 4);
    if (!checkCenter("E5: alpha 0.75 LESS 0.5 -> discarded (green survives)", 0, 255, 0)) return 1;
    fDisable(0x0BC0);

    // --- F: glDrawRangeElements / glMultiDraw* honor the same plumbing ---
    // These GL 1.2/1.4 core entry points used to pass straight through, so
    // GL_QUADS died on GLES and the emulated alpha test never applied.
    void (*fDrawRangeElements)(GLenum, GLuint, GLuint, GLsizei, GLenum, const void*);
    void (*fMultiDrawArrays)(GLenum, const GLint*, const GLsizei*, GLsizei);
    void (*fMultiDrawElements)(GLenum, const GLsizei*, GLenum, const void* const*, GLsizei);
    R(fDrawRangeElements, "glDrawRangeElements");
    R(fMultiDrawArrays, "glMultiDrawArrays");
    R(fMultiDrawElements, "glMultiDrawElements");

    fUseProgram(prog); // back to the R<->B swapping program
    fColorPointer(4, GL_FLOAT, 0, quad_red);
    fClear(GL_COLOR_BUFFER_BIT);
    fDrawRangeElements(GL_QUADS, 0, 3, 4, GL_UNSIGNED_SHORT, quad_idx);
    if (!checkCenter("F1: glDrawRangeElements(QUADS) through user shader (red->blue)", 0, 0, 255))
        return 1;

    static const GLint md_first[1] = {0};
    static const GLsizei md_count[1] = {4};
    fClear(GL_COLOR_BUFFER_BIT);
    fMultiDrawArrays(GL_QUADS, md_first, md_count, 1);
    if (!checkCenter("F2: glMultiDrawArrays(QUADS) through user shader (red->blue)", 0, 0, 255))
        return 1;

    const void* md_indices[1] = {quad_idx};
    fClear(GL_COLOR_BUFFER_BIT);
    fMultiDrawElements(GL_QUADS, md_count, GL_UNSIGNED_SHORT, md_indices, 1);
    if (!checkCenter("F3: glMultiDrawElements(QUADS) through user shader (red->blue)", 0, 0, 255))
        return 1;

    // The emulated alpha test must reach these paths too (cutout foliage
    // drawn via glDrawRangeElements is exactly the OptiFine terrain shape).
    fUseProgram(prog3);
    fColorPointer(4, GL_FLOAT, 0, quad_a25);
    fEnable(0x0BC0);
    fAlphaFunc(0x0204 /* GL_GREATER */, 0.5f);
    fClear(GL_COLOR_BUFFER_BIT);
    fDrawRangeElements(GL_QUADS, 0, 3, 4, GL_UNSIGNED_SHORT, quad_idx);
    if (!checkCenter("F4: alpha test applies to glDrawRangeElements (green survives)", 0, 255, 0))
        return 1;
    fDisable(0x0BC0);

    fDisableClientState(GL_VERTEX_ARRAY);
    fDisableClientState(GL_COLOR_ARRAY);

    if (fGetError() != 0) {
        fprintf(stderr, "FAIL: GL error at end\n");
        return 1;
    }
    printf("OK: mixed fixed-function/user-program pipeline verified\n");
    return 0;
}
