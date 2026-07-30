// SimpleFPEWrapper - tests/smoke_buffer_name_reuse.c
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header
//
// A buffer name the wrapper once owned must stop being treated as
// wrapper-internal the moment it is deleted.
//
// The self-adoption guards (internal_buffers) teach the array-buffer shadow
// to report a wrapper-owned binding as zero. Names, however, are recycled:
// when the wrapper deletes one of its buffers (an immediate-ring replacement,
// a captured static VBO dying with its display list), the GL name goes back
// to the pool and the app's next glGenBuffers can receive it. A stale
// internal_buffers entry then makes the shadow zero out the APP'S OWN VBO,
// and every fixed-function draw sourcing from it silently switches its
// attribute source to the wrapper's ring - garbage vertices.
//
// This is not hypothetical: it shipped in b3dd356 and was reported within
// hours as random vertex corruption in MC 1.12 with "Use VBOs" on (chunk VBO
// names recycle constantly there), captured in
// RDC/Minecraft/1.12-Optifine/vertex-bug.rdc.
//
// Driver name-recycling policy cannot be forced from a test (NVIDIA hands
// out fresh names), so the recycled-name collision is staged directly through
// test hooks instead of hoping for it:
//   1. an app VBO draws correctly (baseline)
//   2. sfpewMarkBufferInternalForTest() poisons its name, and after driving
//      the shadow past its heal interval the SAME draw must go wrong - this
//      is the bug mechanism made visible, and it doubles as the test's own
//      sensitivity proof (no hand-broken build needed)
//   3. deleting the buffer through the wrapper must shed the internal
//      identity (sfpewBufferIsInternalForTest() -> false) - the actual fix
//   4. a fresh buffer reusing that name draws correctly again
//
// Skips (77) when the machine has no EGL device.

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>

#include <EGL/egl.h>

typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef unsigned char GLubyte;
typedef float GLfloat;
typedef int GLint, GLsizei;
typedef unsigned int GLbitfield;
typedef long GLsizeiptr;

#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_TRIANGLES 0x0004
#define GL_QUADS 0x0007
#define GL_FLOAT 0x1406
#define GL_RGBA 0x1908
#define GL_UNSIGNED_BYTE 0x1401
#define GL_ARRAY_BUFFER 0x8892
#define GL_STATIC_DRAW 0x88E4
#define GL_VERTEX_ARRAY 0x8074
#define GL_COLOR_ARRAY 0x8076
#define GL_NO_ERROR 0

static void (*fClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
static void (*fClear)(GLbitfield);
static void (*fDrawArrays)(GLenum, GLint, GLsizei);
static void (*fReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*);
static GLenum (*fGetError)(void);
static void (*fFinish)(void);
static void (*fGenBuffers)(GLsizei, GLuint*);
static void (*fDeleteBuffers)(GLsizei, const GLuint*);
static void (*fBindBuffer)(GLenum, GLuint);
static void (*fBufferData)(GLenum, GLsizeiptr, const void*, GLenum);
static void (*fEnableClientState)(GLenum);
static void (*fDisableClientState)(GLenum);
static void (*fVertexPointer)(GLint, GLenum, GLsizei, const void*);
static void (*fColorPointer)(GLint, GLenum, GLsizei, const void*);
static void (*fBegin)(GLenum);
static void (*fEnd)(void);
static void (*fColor4f)(GLfloat, GLfloat, GLfloat, GLfloat);
static void (*fVertex2f)(GLfloat, GLfloat);

static int failures;

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
    R(fGenBuffers,"glGenBuffers") R(fDeleteBuffers,"glDeleteBuffers")
    R(fBindBuffer,"glBindBuffer") R(fBufferData,"glBufferData")
    R(fEnableClientState,"glEnableClientState")
    R(fDisableClientState,"glDisableClientState")
    R(fVertexPointer,"glVertexPointer") R(fColorPointer,"glColorPointer")
    R(fBegin,"glBegin") R(fEnd,"glEnd") R(fColor4f,"glColor4f") R(fVertex2f,"glVertex2f")
#undef R

    fClearColor(0.0f, 0.0f, 1.0f, 1.0f);

    typedef void (*mark_fn)(GLuint);
    typedef int (*isint_fn)(GLuint);
    typedef GLuint (*bind_fn)(void);
    mark_fn markInternal = (mark_fn)dlsym(handle, "sfpewMarkBufferInternalForTest");
    isint_fn isInternal = (isint_fn)dlsym(handle, "sfpewBufferIsInternalForTest");
    bind_fn logicalBinding = (bind_fn)dlsym(handle, "sfpewLogicalArrayBufferBindingForTest");
    if (!markInternal || !isInternal || !logicalBinding) {
        fprintf(stderr, "FAIL: test hooks not exported\n");
        return 1;
    }

    static const GLfloat greenQuad[] = {
        -1,-1, 0,1,0,1,   1,-1, 0,1,0,1,   1,1, 0,1,0,1,
        -1,-1, 0,1,0,1,   1, 1, 0,1,0,1,  -1,1, 0,1,0,1,
    };
    const GLsizei stride = 6 * (GLsizei)sizeof(GLfloat);
    fEnableClientState(GL_VERTEX_ARRAY);
    fEnableClientState(GL_COLOR_ARRAY);

    // Draws the currently configured VBO-backed layout after driving the
    // array-buffer shadow well past its ~256-query heal interval (each
    // gl*Pointer records the binding, i.e. queries the shadow).
#define DRAW_AFTER_HEAL()                                                                          \
    do {                                                                                           \
        for (int spin = 0; spin < 600; ++spin)                                                     \
            fVertexPointer(2, GL_FLOAT, stride, (const void*)0);                                   \
        fColorPointer(4, GL_FLOAT, stride, (const void*)(2 * sizeof(GLfloat)));                    \
        fClear(GL_COLOR_BUFFER_BIT);                                                               \
        fDrawArrays(GL_TRIANGLES, 0, 6);                                                           \
        fFinish();                                                                                 \
    } while (0)

    GLubyte p[4];
#define CENTRE_IS_GREEN() (fReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p), \
                           (p[1] > 200 && p[0] < 100 && p[2] < 100))

    GLuint vbo = 0;
    fGenBuffers(1, &vbo);
    fBindBuffer(GL_ARRAY_BUFFER, vbo);
    fBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof greenQuad, greenQuad, GL_STATIC_DRAW);

    // 1. Baseline: the app's VBO draws its own vertices.
    DRAW_AFTER_HEAL();
    if (!CENTRE_IS_GREEN()) {
        fprintf(stderr, "FAIL: baseline VBO draw wrong: (%u,%u,%u)\n", p[0], p[1], p[2]);
        ++failures;
    } else {
        printf("OK: baseline VBO-backed draw is green\n");
    }

    // 2. Poison the name the way a stale registry entry would, and prove the
    //    mechanism at the shadow layer: after the heal interval the logical
    //    binding collapses to zero even though the app's VBO is bound. This
    //    is the test's built-in sensitivity check. (Drawing in this state
    //    dereferences buffer offsets as client pointers and crashes - the
    //    real-world severity - so the assertion stops at the shadow.)
    markInternal(vbo);
    fBindBuffer(GL_ARRAY_BUFFER, vbo); // reseed the shadow, then let heal poison it
    for (int spin = 0; spin < 600; ++spin)
        fVertexPointer(2, GL_FLOAT, stride, (const void*)0);
    if (logicalBinding() != 0) {
        fprintf(stderr, "FAIL: poisoned name survived the heal (logical=%u) - the test "
                        "cannot observe the mechanism it exists to guard\n", logicalBinding());
        ++failures;
    } else {
        printf("OK: a stale internal mark zeroes the app's binding (mechanism proven)\n");
    }

    // 3. The fix: deleting the buffer through the wrapper sheds the internal
    //    identity, because the name is about to be recycled.
    fDeleteBuffers(1, &vbo);
    if (isInternal(vbo)) {
        fprintf(stderr, "FAIL: deleted name %u still marked wrapper-internal\n", vbo);
        ++failures;
    } else {
        printf("OK: deletion sheds the wrapper-internal identity\n");
    }

    // 4. A recycled/fresh name must draw correctly again.
    GLuint again = 0;
    fGenBuffers(1, &again);
    fBindBuffer(GL_ARRAY_BUFFER, again);
    fBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof greenQuad, greenQuad, GL_STATIC_DRAW);
    DRAW_AFTER_HEAL();
    if (!CENTRE_IS_GREEN()) {
        fprintf(stderr, "FAIL: post-delete VBO draw wrong: (%u,%u,%u)\n", p[0], p[1], p[2]);
        ++failures;
    } else {
        printf("OK: a fresh buffer (name %u) draws correctly after the cycle\n", again);
    }
    fDeleteBuffers(1, &again);

    const GLenum err = fGetError();
    if (err != GL_NO_ERROR) {
        fprintf(stderr, "FAIL: glGetError 0x%x\n", err); ++failures; }

    fDisableClientState(GL_VERTEX_ARRAY);
    fDisableClientState(GL_COLOR_ARRAY);
    fBindBuffer(GL_ARRAY_BUFFER, 0);

    if (failures != 0) {
        fprintf(stderr, "FAIL: %d check(s) failed\n", failures); return 1; }
    printf("OK: recycled buffer names shed their wrapper-internal identity\n");
    return 0;
}
