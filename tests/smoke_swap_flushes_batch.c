// SimpleFPEWrapper - tests/smoke_swap_flushes_batch.c
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header
//
// A frame must not end with geometry still buffered inside the wrapper.
//
// Small immediate-mode runs are accumulated so consecutive ones merge into one
// draw, and the batch is drained by sfpewEntryBarrier() from the next entry
// point that could observe it. Buffer swap was not one of those entry points -
// only eglMakeCurrent was wrapped - so a frame whose last drawing was
// immediate-mode left its batch pending ACROSS the swap. The next frame's first
// entry point then drained it into the new back buffer, where that frame's
// glClear erased it. Geometry drawn late in a frame vanished and the depth it
// wrote landed in the wrong frame: heavy flickering, the previous frame looking
// as though it was never cleared, wrong depth, and components rendering wrong.
//
// Two things are checked:
//
//   1. eglGetProcAddress("eglSwapBuffers") returns the WRAPPER's entry, not
//      libEGL's. If the routing regresses, the drain cannot happen at all.
//   2. Immediate geometry is pending before the swap and NOT pending after it.
//
// Check 2 asserts on sfpewImmediateBatchPendingForTest() rather than on pixels
// deliberately: a pbuffer performs no real buffer swap, so the frame-ordering
// error is invisible in the framebuffer here - with and without the fix the
// pixels come out identical. The batch predicate is the only thing that
// distinguishes them without a windowed, double-buffered surface.
//
// Skips (77) when the machine has no EGL device.

#include <dlfcn.h>
#include <stdio.h>

#include <EGL/egl.h>

typedef unsigned int GLenum;
typedef unsigned int GLbitfield;
typedef float GLfloat;

#define GL_QUADS 0x0007
#define GL_TRIANGLES 0x0004
#define GL_COLOR_BUFFER_BIT 0x00004000

static int failures;

int main(void) {
    void* handle = dlopen(WRAPPER_LIB_PATH, RTLD_NOW | RTLD_LOCAL);
    if (!handle) { fprintf(stderr, "FAIL: dlopen: %s\n", dlerror()); return 1; }
    typedef void* (*resolver_t)(const char*);
    resolver_t resolve = (resolver_t)dlsym(handle, "eglGetProcAddress");
    if (!resolve) { fprintf(stderr, "FAIL: no eglGetProcAddress\n"); return 1; }

    // The wrapper's own view of whether immediate geometry is still buffered.
    typedef int (*pending_fn)(void);
    pending_fn pending = (pending_fn)dlsym(handle, "sfpewImmediateBatchPendingForTest");
    if (!pending) {
        fprintf(stderr, "FAIL: sfpewImmediateBatchPendingForTest not exported\n");
        return 1;
    }

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
    if (surface == EGL_NO_SURFACE || context == EGL_NO_CONTEXT) {
        printf("SKIP: no ES3 surface/context\n"); return 77; }

    // Route EGL through the wrapper the way a launcher does.
    typedef EGLBoolean (*mc_fn)(EGLDisplay, EGLSurface, EGLSurface, EGLContext);
    typedef EGLBoolean (*swap_fn)(EGLDisplay, EGLSurface);
    mc_fn wMakeCurrent = (mc_fn)resolve("eglMakeCurrent");
    swap_fn wSwap = (swap_fn)resolve("eglSwapBuffers");
    if (!wMakeCurrent) { fprintf(stderr, "FAIL: eglMakeCurrent not routed\n"); return 1; }
    if (!wSwap) {
        fprintf(stderr, "FAIL: eglGetProcAddress did not return the wrapper's "
                        "eglSwapBuffers - the pending batch cannot be drained at swap\n");
        return 1;
    }
    printf("OK: eglSwapBuffers routes through the wrapper\n");

    if (!wMakeCurrent(display, surface, surface, context)) {
        printf("SKIP: could not make ES3 context current\n"); return 77; }

    void (*fBegin)(GLenum) = (void (*)(GLenum))resolve("glBegin");
    void (*fEnd)(void) = (void (*)(void))resolve("glEnd");
    void (*fVertex2f)(GLfloat, GLfloat) = (void (*)(GLfloat, GLfloat))resolve("glVertex2f");
    void (*fColor4f)(GLfloat, GLfloat, GLfloat, GLfloat) =
        (void (*)(GLfloat, GLfloat, GLfloat, GLfloat))resolve("glColor4f");
    void (*fClearColor)(GLfloat, GLfloat, GLfloat, GLfloat) =
        (void (*)(GLfloat, GLfloat, GLfloat, GLfloat))resolve("glClearColor");
    void (*fClear)(GLbitfield) = (void (*)(GLbitfield))resolve("glClear");
    if (!fBegin || !fEnd || !fVertex2f || !fColor4f || !fClearColor || !fClear) {
        fprintf(stderr, "FAIL: missing immediate-mode entry points\n"); return 1; }

    fClearColor(0.0f, 0.0f, 1.0f, 1.0f);
    fClear(GL_COLOR_BUFFER_BIT);

    // End the frame with a small immediate-mode quad - the shape that goes into
    // the merge batch and is left pending for a later entry point to drain.
    fBegin(GL_QUADS);
    fColor4f(1.0f, 0.0f, 0.0f, 1.0f);
    fVertex2f(-0.5f, -0.5f); fVertex2f(0.5f, -0.5f);
    fVertex2f(0.5f, 0.5f);   fVertex2f(-0.5f, 0.5f);
    fEnd();

    if (!pending()) {
        // Not a failure of the fix: if the batcher ever stops holding this
        // shape there is nothing for the swap to drain. Say so rather than
        // reporting a pass that proved nothing.
        printf("SKIP: this shape is no longer batched, so the swap has nothing "
               "to drain - the test can no longer observe the behavior\n");
        return 77;
    }
    printf("OK: geometry is buffered in the wrapper at end of frame\n");

    wSwap(display, surface);

    if (pending()) {
        fprintf(stderr, "FAIL: geometry still buffered AFTER eglSwapBuffers - it will be "
                        "drawn into the next frame and erased by its clear\n");
        ++failures;
    } else {
        printf("OK: the swap drained it, so the frame is complete when presented\n");
    }

    // A second frame, to confirm the drain did not break ordinary drawing.
    fClear(GL_COLOR_BUFFER_BIT);
    fBegin(GL_QUADS);
    fColor4f(0.0f, 1.0f, 0.0f, 1.0f);
    fVertex2f(-0.5f, -0.5f); fVertex2f(0.5f, -0.5f);
    fVertex2f(0.5f, 0.5f);   fVertex2f(-0.5f, 0.5f);
    fEnd();
    wSwap(display, surface);
    if (pending()) {
        fprintf(stderr, "FAIL: second frame left geometry buffered after the swap\n");
        ++failures;
    } else {
        printf("OK: a second frame drains as well\n");
    }

    if (failures != 0) {
        fprintf(stderr, "FAIL: %d check(s) failed\n", failures); return 1; }
    printf("OK: buffer swap leaves no geometry buffered in the wrapper\n");
    return 0;
}
