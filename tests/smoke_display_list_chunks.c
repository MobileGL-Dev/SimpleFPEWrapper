// SimpleFPEWrapper - tests/smoke_display_list_chunks.c
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// Minecraft's terrain is display lists compiled from ONE shared client
// buffer: the tessellator fills its buffer with a chunk, points the vertex
// arrays at it, calls glDrawArrays inside glNewList, and then reuses the very
// same buffer for the next chunk. GL says a vertex-array draw dereferences
// its data when the command is COMPILED, so each list must keep its own
// snapshot - a wrapper that retained the pointer would replay every chunk
// with the last chunk's contents, and one that silently dropped a draw it
// could not snapshot would lose the chunk entirely.
//
// Each list here draws one quad in its own screen column with its own colour,
// so the readback names exactly which lists survived compilation and replay.
// The lists are then called three ways, because the wrapper has a separate
// path for each: one glCallLists batch (which it tries to merge into a single
// multi-draw), individual glCallList calls, and a batch after half of the
// lists have been re-recorded in place - the rebuild MC does whenever a chunk
// changes, which frees and reallocates inside the wrapper's vertex arena.
//
// The batched replay also has to work on a backend without
// glMultiDrawArrays: MobileGlues resolves the pointer but implements nothing
// behind it up to V1.3.5, so a group drawn that way vanishes without an
// error. SFPEW_NO_MULTIDRAWARRAYS forces the path taken there - identity
// indices through the indexed multi-draw - and this runs the same checks
// under it, with GL_TRIANGLES so the group cannot take the quad index path
// instead.
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

#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_QUADS 0x0007
#define GL_TRIANGLES 0x0004
#define GL_FLOAT 0x1406
#define GL_RGBA 0x1908
#define GL_UNSIGNED_BYTE 0x1401
#define GL_UNSIGNED_INT 0x1405
#define GL_VERTEX_ARRAY 0x8074
#define GL_COLOR_ARRAY 0x8076
#define GL_TEXTURE_COORD_ARRAY 0x8078
#define GL_COMPILE 0x1300
#define GL_NO_ERROR 0

// The wrapper's captured-list vertex arena is 64 MiB; CHUNKS * QUAD_BYTES
// stays far below that, so this exercises the arena path rather than the
// per-list fallback. The count is still high enough that the batch path has
// to stitch many lists together.
#define CHUNKS 64
// A quad needs four vertices, the same band as triangles needs six; the
// buffer is sized for the larger so one shape serves both modes.
#define MAX_VERTS_PER_CHUNK 6
#define STRIDE 32 // MC's tessellator stride: xyz, uv, colour, padding

static void (*fClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
static void (*fClear)(GLbitfield);
static void (*fReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*);
static GLenum (*fGetError)(void);
static void (*fFinish)(void);
static void (*fEnableClientState)(GLenum);
static void (*fDisableClientState)(GLenum);
static void (*fVertexPointer)(GLint, GLenum, GLsizei, const void*);
static void (*fColorPointer)(GLint, GLenum, GLsizei, const void*);
static void (*fTexCoordPointer)(GLint, GLenum, GLsizei, const void*);
static void (*fDrawArrays)(GLenum, GLint, GLsizei);
static GLuint (*fGenLists)(GLsizei);
static void (*fNewList)(GLuint, GLenum);
static void (*fEndList)(void);
static void (*fCallList)(GLuint);
static void (*fCallLists)(GLsizei, GLenum, const void*);

static int failures;

// A quad and two triangles cover the same band, so the readback checks are
// the same either way; the mode only decides which batch path the group
// takes.
static int quadMode = 1;
static int vertsPerChunk(void) { return quadMode ? 4 : 6; }

// One buffer for every chunk, exactly like the tessellator's.
static GLubyte sharedBuffer[MAX_VERTS_PER_CHUNK * STRIDE];

// Chunk i owns the horizontal band [i/CHUNKS, (i+1)/CHUNKS] in clip space and
// is tinted with a colour derived from i, so a readback in that band proves
// which chunk's data the list replayed.
static void fillSharedBuffer(int chunk) {
    const float left = -1.0f + 2.0f * (float)chunk / (float)CHUNKS;
    const float right = -1.0f + 2.0f * (float)(chunk + 1) / (float)CHUNKS;
    const GLubyte r = (GLubyte)(40 + 3 * chunk);
    const GLubyte g = (GLubyte)(255 - 3 * chunk);
    const float xs[4] = {left, right, right, left};
    const float ys[4] = {-1.0f, -1.0f, 1.0f, 1.0f};
    // Corner order: a quad walks its four in turn, two triangles repeat the
    // diagonal. Both sweep the same band.
    static const int quadCorners[4] = {0, 1, 2, 3};
    static const int triangleCorners[6] = {0, 1, 2, 0, 2, 3};
    const int* corners = quadMode ? quadCorners : triangleCorners;
    memset(sharedBuffer, 0, sizeof sharedBuffer);
    for (int v = 0; v < vertsPerChunk(); ++v) {
        GLubyte* vertex = sharedBuffer + v * STRIDE;
        float* position = (float*)vertex;
        position[0] = xs[corners[v]];
        position[1] = ys[corners[v]];
        position[2] = 0.0f;
        float* uv = (float*)(vertex + 12);
        uv[0] = 0.0f;
        uv[1] = 0.0f;
        vertex[20] = r;
        vertex[21] = g;
        vertex[22] = 64;
        vertex[23] = 255;
    }
}

// Compile chunk `chunk` into `list`, from the shared buffer, the way the
// tessellator does: point the arrays at the buffer that is about to be
// overwritten, draw, and close the list.
static void recordChunk(GLuint list, int chunk) {
    fillSharedBuffer(chunk);
    fNewList(list, GL_COMPILE);
    fVertexPointer(3, GL_FLOAT, STRIDE, sharedBuffer);
    fTexCoordPointer(2, GL_FLOAT, STRIDE, sharedBuffer + 12);
    fColorPointer(4, GL_UNSIGNED_BYTE, STRIDE, sharedBuffer + 20);
    fEnableClientState(GL_VERTEX_ARRAY);
    fEnableClientState(GL_TEXTURE_COORD_ARRAY);
    fEnableClientState(GL_COLOR_ARRAY);
    fDrawArrays(quadMode ? GL_QUADS : GL_TRIANGLES, 0, vertsPerChunk());
    fDisableClientState(GL_COLOR_ARRAY);
    fDisableClientState(GL_TEXTURE_COORD_ARRAY);
    fDisableClientState(GL_VERTEX_ARRAY);
    fEndList();
}

// Every chunk must have drawn its own band in its own colour.
static void checkAllChunks(const char* what) {
    GLubyte pixels[64 * 4];
    fReadPixels(0, 32, 64, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    int missing = 0, wrong = 0, firstBad = -1;
    for (int chunk = 0; chunk < CHUNKS; ++chunk) {
        // Band centre for a 64-wide readback with CHUNKS == 64: one pixel each.
        const int x = chunk;
        const GLubyte* pixel = pixels + x * 4;
        const GLubyte expectedR = (GLubyte)(40 + 3 * chunk);
        const GLubyte expectedG = (GLubyte)(255 - 3 * chunk);
        const int blank = pixel[0] == 0 && pixel[1] == 0 && pixel[2] == 0;
        const int near = pixel[0] + 6 >= expectedR && pixel[0] <= expectedR + 6 &&
                         pixel[1] + 6 >= expectedG && pixel[1] <= expectedG + 6;
        if (blank) {
            ++missing;
            if (firstBad < 0) firstBad = chunk;
        } else if (!near) {
            ++wrong;
            if (firstBad < 0) firstBad = chunk;
        }
    }
    if (missing || wrong) {
        const GLubyte* pixel = pixels + (firstBad < 0 ? 0 : firstBad) * 4;
        fprintf(stderr,
                "FAIL: %s: %d/%d chunks missing, %d wrong; first bad chunk %d = (%u,%u,%u), "
                "expected (%u,%u,64)\n",
                what, missing, CHUNKS, wrong, firstBad, pixel[0], pixel[1], pixel[2],
                (GLubyte)(40 + 3 * firstBad), (GLubyte)(255 - 3 * firstBad));
        ++failures;
    } else {
        printf("OK: %s\n", what);
    }
}

int main(int argc, char** argv) {
    quadMode = !(argc > 1 && strcmp(argv[1], "triangles") == 0);
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
    R(fClearColor,"glClearColor") R(fClear,"glClear") R(fReadPixels,"glReadPixels")
    R(fGetError,"glGetError") R(fFinish,"glFinish")
    R(fEnableClientState,"glEnableClientState") R(fDisableClientState,"glDisableClientState")
    R(fVertexPointer,"glVertexPointer") R(fColorPointer,"glColorPointer")
    R(fTexCoordPointer,"glTexCoordPointer") R(fDrawArrays,"glDrawArrays")
    R(fGenLists,"glGenLists") R(fNewList,"glNewList") R(fEndList,"glEndList")
    R(fCallList,"glCallList") R(fCallLists,"glCallLists")
#undef R

    const GLuint base = fGenLists(CHUNKS);
    if (base == 0) { fprintf(stderr, "FAIL: glGenLists(%d) returned 0\n", CHUNKS); return 1; }

    GLuint ids[CHUNKS];
    for (int chunk = 0; chunk < CHUNKS; ++chunk) {
        ids[chunk] = base + (GLuint)chunk;
        recordChunk(ids[chunk], chunk);
    }

    fClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    // One batch, the shape MC uses every frame.
    fClear(GL_COLOR_BUFFER_BIT);
    fCallLists(CHUNKS, GL_UNSIGNED_INT, ids);
    fFinish();
    checkAllChunks("glCallLists batch replays every chunk's own snapshot");

    // The same lists one at a time: a different wrapper path.
    fClear(GL_COLOR_BUFFER_BIT);
    for (int chunk = 0; chunk < CHUNKS; ++chunk) fCallList(ids[chunk]);
    fFinish();
    checkAllChunks("individual glCallList replays every chunk");

    // Rebuild half the lists in place - the chunk update MC does constantly -
    // and draw again. Re-recording frees the old snapshot inside the arena.
    for (int chunk = 0; chunk < CHUNKS; chunk += 2) recordChunk(ids[chunk], chunk);
    fClear(GL_COLOR_BUFFER_BIT);
    fCallLists(CHUNKS, GL_UNSIGNED_INT, ids);
    fFinish();
    checkAllChunks("re-recorded lists still replay their own snapshot");

    // And once more without touching anything, to catch a batch cache that
    // went stale behind the rebuild.
    fClear(GL_COLOR_BUFFER_BIT);
    fCallLists(CHUNKS, GL_UNSIGNED_INT, ids);
    fFinish();
    checkAllChunks("the batch after a rebuild is still correct");

    const GLenum err = fGetError();
    if (err != GL_NO_ERROR) {
        fprintf(stderr, "FAIL: glGetError 0x%x\n", err); ++failures; }

    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(display, context);
    eglDestroySurface(display, surface);
    eglTerminate(display);

    if (failures) {
        fprintf(stderr, "FAIL: %d check(s) failed\n", failures);
        return 1;
    }
    printf("OK: display-list chunks keep their own vertex snapshots (%s)\n",
           quadMode ? "quads" : "triangles");
    return 0;
}
