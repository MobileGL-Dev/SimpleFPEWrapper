/* SimpleFPEWrapper - tools/mctrace/egl_es_shim.c
 * Copyright (c) 2026 MobileGL-Dev
 * SPDX-License-Identifier: LGPL-3.0-only
 *
 * A libEGL.so.1 for apitrace's eglretrace that replays a desktop GL trace
 * the way the wrapper is deployed: on a GLES context, through the wrapper.
 *
 * eglretrace asks EGL for a desktop compatibility context for a GLX trace,
 * and on a desktop driver gets one: the trace would run natively, or the
 * wrapper would run over desktop GL, never the GLES backend it ships on.
 * eglretrace resolves every entry point with dlsym(RTLD_NEXT) before falling
 * back to the libEGL.so.1 it dlopen()s, so the first global definition of a
 * name wins. Preload this library ahead of the wrapper
 *
 *     LD_PRELOAD="libEGL.so.1 libSimpleFPEWrapper.so"
 *
 * (and put its directory on LD_LIBRARY_PATH, which serves the dlopen too):
 * EGL names then resolve here and GL names fall through to the wrapper. Every
 * EGL call is forwarded to the system libEGL, loaded privately so it never
 * answers a global lookup itself, except that
 *
 *   - binding the desktop GL API binds GLES instead, configs are chosen for
 *     GLES 3, and a desktop context request becomes a GLES 3.2 context;
 *   - context creation, make-current, swaps and eglGetProcAddress go through
 *     the wrapper, exactly as a launcher hands it a context and resolves its
 *     GL entry points.
 *
 * The wrapper loads the system EGL as "libEGL.so", a name this file does not
 * answer to, so its backend is the real driver.
 *
 * Android cannot preload either library (its vendor EGL layer resolves EGL
 * entry points globally and would find them while the wrapper is loading
 * the system libEGL). There this file is installed as libEGL.so on
 * LD_LIBRARY_PATH, which makes it the libEGL eglretrace links;
 * SFPEW_SHIM_WRAPPER_LOCAL keeps the wrapper private, and SFPEW_EGL hands the
 * wrapper the system libEGL by path, since its name is now this file's. The
 * Android eglretrace takes GL entry points from its libEGL's
 * eglGetProcAddress, so they reach the wrapper the same way.
 *
 * The wrapper need not be SFPEW: gl4es built with NOX11 and NOEGL - the
 * build that takes a context made outside it, as on a launcher - runs the
 * same way, its GL resolver standing in for eglGetProcAddress and its swap
 * hooks called around every swap, so both are measured on equal terms.
 *
 * Without SFPEW_SHIM_WRAPPER nothing is rewritten: every call goes straight
 * to the system libEGL, which is the native baseline. Either way, with
 * SFPEW_SHIM_FRAME_TIMES set, every buffer swap is timestamped and the times
 * are written there at exit, one line per swap: monotonic and process CPU
 * time in nanoseconds - the same clocks at the same point for every target.
 *
 * Build: cc -shared -fPIC -O2 -Wl,-Bsymbolic -o libEGL.so.1 egl_es_shim.c -ldl
 *        (-Bsymbolic: the preloaded wrapper exports some of the same names, and
 *        must not capture this file's references to its own definitions)
 * Env:   SFPEW_SHIM_WRAPPER     path of the wrapper library (SFPEW or gl4es); unset
 *                               replays natively
 *        SFPEW_SHIM_REAL_EGL    system libEGL (default /usr/lib64/libEGL.so)
 *        SFPEW_SHIM_FRAME_TIMES file for the swap timestamps
 *        SFPEW_SHIM_SNAPSHOT    file for a PPM of the image one swap presents,
 *        SFPEW_SHIM_SNAPSHOT_FRAME  the swap's number, from 1
 *        SFPEW_SHIM_WRAPPER_LOCAL load the wrapper RTLD_LOCAL (Android)
 *        SFPEW_SHIM_DEBUG       log EGL resolution and context creation
 */
#define _GNU_SOURCE
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

/* With SFPEW_SHIM_DEBUG, stdout is line-buffered for the whole process, so
 * what a wrapper logs there survives a replay that has to be killed. */
__attribute__((constructor)) static void debug_output(void) {
    if (getenv("SFPEW_SHIM_DEBUG") != NULL) setvbuf(stdout, NULL, _IOLBF, 0);
}

static void* real_handle(void) {
    static void* handle;
    if (handle == NULL) {
        const char* path = getenv("SFPEW_SHIM_REAL_EGL");
        handle = dlopen(path != NULL ? path : "/usr/lib64/libEGL.so", RTLD_NOW | RTLD_LOCAL);
        if (handle == NULL) fprintf(stderr, "egl_es_shim: no system libEGL: %s\n", dlerror());
    }
    return handle;
}

static void* wrapper_handle(void) {
    static void* handle;
    if (handle == NULL) {
        const char* path = getenv("SFPEW_SHIM_WRAPPER");
        if (path == NULL) {
            fprintf(stderr, "egl_es_shim: set SFPEW_SHIM_WRAPPER\n");
            abort();
        }
        /* SFPEW_SHIM_WRAPPER_LOCAL keeps the wrapper's exports out of global
         * lookups: on Android a vendor EGL layer resolves EGL entry points
         * globally and would otherwise land in the wrapper's own, in the
         * middle of the wrapper loading that very libEGL. GL names then
         * reach the wrapper only through eglGetProcAddress below. */
        const int scope = getenv("SFPEW_SHIM_WRAPPER_LOCAL") != NULL ? RTLD_LOCAL : RTLD_GLOBAL;
        handle = dlopen(path, RTLD_NOW | scope);
        if (handle == NULL) fprintf(stderr, "egl_es_shim: no wrapper: %s\n", dlerror());
    }
    return handle;
}

/* glvnd exports only core EGL by name; extensions come from its resolver. */
static int wrapping(void) {
    const char* path = getenv("SFPEW_SHIM_WRAPPER");
    return path != NULL && *path != '\0';
}

/* ---- swap timestamps ---------------------------------------------------- */

/* Wall time says how long a frame took; process CPU time says how much work
 * it cost the CPU, which a GPU stall or a driver's power state does not
 * move - the wrapper's own overhead shows there undiluted. */
static unsigned long long (*frame_times)[2];
static size_t frame_count, frame_capacity;

static unsigned long long nanoseconds(clockid_t clock) {
    struct timespec now;
    clock_gettime(clock, &now);
    return (unsigned long long)now.tv_sec * 1000000000ull + (unsigned long long)now.tv_nsec;
}

static void write_frame_times(void) {
    const char* path = getenv("SFPEW_SHIM_FRAME_TIMES");
    FILE* out = path != NULL ? fopen(path, "w") : NULL;
    if (out == NULL) return;
    for (size_t i = 0; i < frame_count; ++i) fprintf(out, "%llu %llu\n", frame_times[i][0], frame_times[i][1]);
    fclose(out);
}

static void note_swap(void) {
    if (getenv("SFPEW_SHIM_FRAME_TIMES") == NULL) return;
    if (frame_count == frame_capacity) {
        if (frame_capacity == 0) atexit(write_frame_times);
        frame_capacity = frame_capacity ? frame_capacity * 2 : 4096;
        frame_times = realloc(frame_times, frame_capacity * sizeof *frame_times);
    }
    frame_times[frame_count][0] = nanoseconds(CLOCK_MONOTONIC);
    frame_times[frame_count][1] = nanoseconds(CLOCK_PROCESS_CPUTIME_ID);
    ++frame_count;
}

static void* real(const char* name) {
    void* fn = dlsym(real_handle(), name);
    if (fn == NULL) {
        void* (*resolve)(const char*) = (void* (*)(const char*))dlsym(real_handle(), "eglGetProcAddress");
        if (resolve != NULL) fn = resolve(name);
    }
    return fn;
}
/* The wrapper's own definition of an entry point, or the system's where it
 * leaves that to the application: gl4es built without EGL creates no
 * contexts and swaps no buffers. Neither wrapper links libEGL, so a lookup
 * in its handle finds only what it defines. */
static void* wrapped(const char* name) {
    void* fn = dlsym(wrapper_handle(), name);
    return fn != NULL ? fn : real(name);
}

/* Its GL resolver: SFPEW's is eglGetProcAddress, gl4es's gl4es_GetProcAddress
 * (and glXGetProcAddress where it has GLX). */
typedef __eglMustCastToProperFunctionPointerType (*resolver)(const char*);
static resolver wrapper_resolver(void) {
    static const char* const names[] = {"eglGetProcAddress", "gl4es_GetProcAddress"};
    for (size_t i = 0; i < sizeof names / sizeof *names; ++i) {
        resolver fn = (resolver)dlsym(wrapper_handle(), names[i]);
        if (fn != NULL) return fn;
    }
    return NULL;
}

/* Plain forwarding to the system libEGL. */
#define FORWARD(ret, name, params, args)                                                           \
    ret name params {                                                                              \
        static ret(*fn) params;                                                                    \
        if (fn == NULL) fn = (ret(*) params)real(#name);                                           \
        return fn args;                                                                            \
    }
/* Forwarding to the wrapper's own definition, or the system's when
 * replaying natively. */
#define WRAPPED(ret, name, params, args)                                                           \
    ret name params {                                                                              \
        static ret(*fn) params;                                                                    \
        if (fn == NULL) fn = (ret(*) params)(wrapping() ? wrapped(#name) : real(#name));           \
        return fn args;                                                                            \
    }
/* ---- snapshots ---------------------------------------------------------- */

/* The image a swap presents, read back from the driver just before it, with
 * the read state put back afterwards: the same readback whatever drew the
 * image - wrapper, other wrapper or none - unlike apitrace's own snapshots,
 * which go through the GL being measured and need what it implements. */
static void snapshot(EGLDisplay d, EGLSurface s, const char* path) {
    EGLint width = 0, height = 0;
    EGLBoolean (*query)(EGLDisplay, EGLSurface, EGLint, EGLint*) =
        (EGLBoolean(*)(EGLDisplay, EGLSurface, EGLint, EGLint*))real("eglQuerySurface");
    void (*get)(GLenum, GLint*) = (void (*)(GLenum, GLint*))real("glGetIntegerv");
    void (*bind_framebuffer)(GLenum, GLuint) = (void (*)(GLenum, GLuint))real("glBindFramebuffer");
    void (*bind_buffer)(GLenum, GLuint) = (void (*)(GLenum, GLuint))real("glBindBuffer");
    void (*store)(GLenum, GLint) = (void (*)(GLenum, GLint))real("glPixelStorei");
    void (*read)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*) =
        (void (*)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*))real("glReadPixels");
    if (!query(d, s, EGL_WIDTH, &width) || !query(d, s, EGL_HEIGHT, &height) || width <= 0 || height <= 0) return;

    static const GLenum pack[] = {GL_PACK_ALIGNMENT, GL_PACK_ROW_LENGTH, GL_PACK_SKIP_ROWS, GL_PACK_SKIP_PIXELS};
    GLint framebuffer = 0, buffer = 0, saved[4];
    get(GL_READ_FRAMEBUFFER_BINDING, &framebuffer);
    get(GL_PIXEL_PACK_BUFFER_BINDING, &buffer);
    for (int i = 0; i < 4; ++i) get(pack[i], &saved[i]);
    bind_framebuffer(GL_READ_FRAMEBUFFER, 0);
    bind_buffer(GL_PIXEL_PACK_BUFFER, 0);
    for (int i = 0; i < 4; ++i) store(pack[i], i == 0 ? 1 : 0);
    unsigned char* rgba = malloc((size_t)width * height * 4);
    read(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    for (int i = 0; i < 4; ++i) store(pack[i], saved[i]);
    bind_buffer(GL_PIXEL_PACK_BUFFER, buffer);
    bind_framebuffer(GL_READ_FRAMEBUFFER, framebuffer);

    /* A binary PPM, top row first. */
    FILE* out = fopen(path, "wb");
    if (out != NULL) {
        fprintf(out, "P6\n%d %d\n255\n", width, height);
        for (int y = height - 1; y >= 0; --y)
            for (int x = 0; x < width; ++x) fwrite(rgba + ((size_t)y * width + x) * 4, 1, 3, out);
        fclose(out);
    }
    free(rgba);
}

/* ---- swapchain backpressure ---------------------------------------------- */

/* A window surface holds a few buffers, and a swap with all of them still
 * in flight blocks: the CPU never gets more than a frame or two ahead of the
 * GPU. A pbuffer swap blocks on nothing, so a replay would queue frame after
 * frame on the GPU - and with them every copy the driver keeps of a texture
 * that queued frames still read while the next frame rewrites it, until a
 * phone runs out of memory. Every swap therefore fences its frame and waits
 * for the one FRAMES_IN_FLIGHT swaps back, as a double-buffered surface
 * would, without being paced by a display. */
#define FRAMES_IN_FLIGHT 2

static void throttle(EGLDisplay d) {
    static EGLSyncKHR (*create)(EGLDisplay, EGLenum, const EGLint*);
    static EGLint (*wait)(EGLDisplay, EGLSyncKHR, EGLint, EGLTimeKHR);
    static EGLBoolean (*destroy)(EGLDisplay, EGLSyncKHR);
    static EGLSyncKHR fences[FRAMES_IN_FLIGHT];
    static size_t next;
    if (create == NULL) {
        create = (EGLSyncKHR(*)(EGLDisplay, EGLenum, const EGLint*))real("eglCreateSyncKHR");
        wait = (EGLint(*)(EGLDisplay, EGLSyncKHR, EGLint, EGLTimeKHR))real("eglClientWaitSyncKHR");
        destroy = (EGLBoolean(*)(EGLDisplay, EGLSyncKHR))real("eglDestroySyncKHR");
        if (create == NULL || wait == NULL || destroy == NULL) return;
    }
    if (fences[next] != EGL_NO_SYNC_KHR) {
        wait(d, fences[next], EGL_SYNC_FLUSH_COMMANDS_BIT_KHR, EGL_FOREVER_KHR);
        destroy(d, fences[next]);
    }
    fences[next] = create(d, EGL_SYNC_FENCE_KHR, NULL);
    next = (next + 1) % FRAMES_IN_FLIGHT;
}

/* With SFPEW_SHIM_SNAPSHOT=<file> and SFPEW_SHIM_SNAPSHOT_FRAME=<n>, the
 * image of the n-th swap (from 1) is written to <file>. */
static void maybe_snapshot(EGLDisplay d, EGLSurface s) {
    static size_t swaps;
    const char* path = getenv("SFPEW_SHIM_SNAPSHOT");
    const char* frame = getenv("SFPEW_SHIM_SNAPSHOT_FRAME");
    if (++swaps, path != NULL && frame != NULL && strtoull(frame, NULL, 10) == swaps) snapshot(d, s, path);
}

/* The same, for a swap: timestamped once it has been issued, and bracketed
 * by the hooks a wrapper that does not own the surface asks the application
 * to call around it (gl4es: flush what it batched, blit its own FBO). */
static void (*pre_swap)(void), (*post_swap)(void);
#define SWAP(name, params, args)                                                                   \
    EGLBoolean name params {                                                                       \
        static EGLBoolean(*fn) params;                                                             \
        if (fn == NULL) {                                                                          \
            fn = (EGLBoolean(*) params)(wrapping() ? wrapped(#name) : real(#name));                \
            if (wrapping()) {                                                                      \
                pre_swap = (void (*)(void))dlsym(wrapper_handle(), "gl4es_pre_swap");              \
                post_swap = (void (*)(void))dlsym(wrapper_handle(), "gl4es_post_swap");            \
            }                                                                                      \
        }                                                                                          \
        if (pre_swap != NULL) pre_swap();                                                          \
        maybe_snapshot(d, s);                                                                      \
        EGLBoolean swapped = fn args;                                                              \
        throttle(d);                                                                               \
        note_swap();                                                                               \
        if (post_swap != NULL) post_swap();                                                        \
        return swapped;                                                                            \
    }

FORWARD(EGLDisplay, eglGetDisplay, (EGLNativeDisplayType d), (d))
FORWARD(EGLDisplay, eglGetPlatformDisplay, (EGLenum p, void* d, const EGLAttrib* a), (p, d, a))
FORWARD(EGLDisplay, eglGetPlatformDisplayEXT, (EGLenum p, void* d, const EGLint* a), (p, d, a))
FORWARD(EGLBoolean, eglInitialize, (EGLDisplay d, EGLint* ma, EGLint* mi), (d, ma, mi))
FORWARD(EGLBoolean, eglTerminate, (EGLDisplay d), (d))
FORWARD(EGLint, eglGetError, (void), ())
FORWARD(const char*, eglQueryString, (EGLDisplay d, EGLint n), (d, n))
FORWARD(EGLBoolean, eglGetConfigs, (EGLDisplay d, EGLConfig* c, EGLint s, EGLint* n), (d, c, s, n))
FORWARD(EGLBoolean, eglGetConfigAttrib, (EGLDisplay d, EGLConfig c, EGLint a, EGLint* v), (d, c, a, v))
FORWARD(EGLSurface, eglCreateWindowSurface, (EGLDisplay d, EGLConfig c, EGLNativeWindowType w, const EGLint* a),
        (d, c, w, a))
FORWARD(EGLSurface, eglCreatePbufferSurface, (EGLDisplay d, EGLConfig c, const EGLint* a), (d, c, a))
FORWARD(EGLBoolean, eglDestroySurface, (EGLDisplay d, EGLSurface s), (d, s))
FORWARD(EGLBoolean, eglQuerySurface, (EGLDisplay d, EGLSurface s, EGLint a, EGLint* v), (d, s, a, v))
FORWARD(EGLBoolean, eglQueryContext, (EGLDisplay d, EGLContext c, EGLint a, EGLint* v), (d, c, a, v))
FORWARD(EGLContext, eglGetCurrentContext, (void), ())
FORWARD(EGLDisplay, eglGetCurrentDisplay, (void), ())
FORWARD(EGLSurface, eglGetCurrentSurface, (EGLint w), (w))
FORWARD(EGLBoolean, eglSwapInterval, (EGLDisplay d, EGLint i), (d, 0 * i))
FORWARD(EGLBoolean, eglReleaseThread, (void), ())
FORWARD(EGLBoolean, eglWaitClient, (void), ())
FORWARD(EGLBoolean, eglWaitGL, (void), ())
FORWARD(EGLBoolean, eglWaitNative, (EGLint e), (e))
FORWARD(EGLImageKHR, eglCreateImageKHR, (EGLDisplay d, EGLContext c, EGLenum t, EGLClientBuffer b, const EGLint* a),
        (d, c, t, b, a))
FORWARD(EGLBoolean, eglDestroyImageKHR, (EGLDisplay d, EGLImageKHR i), (d, i))
FORWARD(EGLBoolean, eglSetDamageRegionKHR, (EGLDisplay d, EGLSurface s, EGLint* r, EGLint n), (d, s, r, n))

/* A benchmark is not paced by a display: whatever surface becomes current
 * swaps without waiting for one. */
EGLBoolean eglMakeCurrent(EGLDisplay d, EGLSurface w, EGLSurface r, EGLContext c) {
    static EGLBoolean (*fn)(EGLDisplay, EGLSurface, EGLSurface, EGLContext);
    static EGLBoolean (*interval)(EGLDisplay, EGLint);
    if (fn == NULL) {
        fn = (EGLBoolean(*)(EGLDisplay, EGLSurface, EGLSurface, EGLContext))(wrapping() ? wrapped("eglMakeCurrent")
                                                                                         : real("eglMakeCurrent"));
        interval = (EGLBoolean(*)(EGLDisplay, EGLint))real("eglSwapInterval");
    }
    EGLBoolean made = fn(d, w, r, c);
    if (made && w != EGL_NO_SURFACE && interval != NULL) interval(d, 0);
    return made;
}
WRAPPED(EGLBoolean, eglDestroyContext, (EGLDisplay d, EGLContext c), (d, c))
SWAP(eglSwapBuffers, (EGLDisplay d, EGLSurface s), (d, s))
SWAP(eglSwapBuffersWithDamageEXT, (EGLDisplay d, EGLSurface s, EGLint* r, EGLint n), (d, s, r, n))
SWAP(eglSwapBuffersWithDamageKHR, (EGLDisplay d, EGLSurface s, EGLint* r, EGLint n), (d, s, r, n))

/* GL names resolve through the wrapper, as a launcher's would. EGL names
 * resolve here: this file's own definition where it has one, so an entry
 * point fetched by name is intercepted the same as one linked by name, and
 * the system's otherwise (extensions included). */
__eglMustCastToProperFunctionPointerType eglGetProcAddress(const char* name) {
    if (name[0] == 'e' && name[1] == 'g' && name[2] == 'l') {
        static void* self;
        if (self == NULL) {
            Dl_info info;
            dladdr((void*)eglGetProcAddress, &info);
            self = dlopen(info.dli_fname, RTLD_NOW | RTLD_NOLOAD);
        }
        void* own = self != NULL ? dlsym(self, name) : NULL;
        static resolver system;
        if (system == NULL) system = (resolver)real("eglGetProcAddress");
        __eglMustCastToProperFunctionPointerType found =
            own != NULL ? (__eglMustCastToProperFunctionPointerType)own : system(name);
        if (getenv("SFPEW_SHIM_DEBUG")) fprintf(stderr, "egl_es_shim: %s -> %p%s\n", name, (void*)found, own ? " (shim)" : "");
        return found;
    }
    static resolver gl;
    if (gl == NULL) gl = wrapping() ? wrapper_resolver() : (resolver)real("eglGetProcAddress");
    return gl(name);
}

/* The API the application believes it bound. EGL's default is GLES. */
static __thread EGLenum app_api = EGL_OPENGL_ES_API;

EGLBoolean eglBindAPI(EGLenum api) {
    static EGLBoolean (*fn)(EGLenum);
    if (fn == NULL) fn = (EGLBoolean(*)(EGLenum))real("eglBindAPI");
    app_api = api;
    return fn(wrapping() && api == EGL_OPENGL_API ? EGL_OPENGL_ES_API : api);
}

EGLenum eglQueryAPI(void) {
    static EGLenum (*fn)(void);
    if (!wrapping()) {
        if (fn == NULL) fn = (EGLenum(*)(void))real("eglQueryAPI");
        return fn();
    }
    return app_api;
}

EGLBoolean eglChooseConfig(EGLDisplay d, const EGLint* attribs, EGLConfig* configs, EGLint size, EGLint* count) {
    static EGLBoolean (*fn)(EGLDisplay, const EGLint*, EGLConfig*, EGLint, EGLint*);
    if (fn == NULL) fn = (EGLBoolean(*)(EGLDisplay, const EGLint*, EGLConfig*, EGLint, EGLint*))real("eglChooseConfig");
    if (!wrapping()) return fn(d, attribs, configs, size, count);
    /* The same request, renderable as GLES 3 instead of desktop GL. */
    EGLint copy[128];
    int n = 0, renderable = 0;
    for (; attribs != NULL && attribs[n] != EGL_NONE && n < 124; n += 2) {
        copy[n] = attribs[n];
        copy[n + 1] = attribs[n + 1];
        if (attribs[n] == EGL_RENDERABLE_TYPE) {
            copy[n + 1] = EGL_OPENGL_ES3_BIT;
            renderable = 1;
        }
    }
    if (!renderable) {
        copy[n++] = EGL_RENDERABLE_TYPE;
        copy[n++] = EGL_OPENGL_ES3_BIT;
    }
    copy[n] = EGL_NONE;
    return fn(d, copy, configs, size, count);
}

EGLContext eglCreateContext(EGLDisplay d, EGLConfig c, EGLContext share, const EGLint* attribs) {
    static EGLContext (*fn)(EGLDisplay, EGLConfig, EGLContext, const EGLint*);
    if (fn == NULL)
        fn = (EGLContext(*)(EGLDisplay, EGLConfig, EGLContext, const EGLint*))(wrapping() ? wrapped("eglCreateContext")
                                                                                        : real("eglCreateContext"));
    if (!wrapping()) return fn(d, c, share, attribs);
    /* A desktop request carries desktop version and profile attributes that
     * mean nothing to a GLES context; ask for the newest GLES instead, which
     * is what a launcher hands the wrapper. */
    static const EGLint es32[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 2, EGL_NONE};
    EGLContext context = fn(d, c, share, app_api == EGL_OPENGL_API ? es32 : attribs);
    if (getenv("SFPEW_SHIM_DEBUG")) {
        EGLint renderable = 0;
        EGLBoolean (*attrib)(EGLDisplay, EGLConfig, EGLint, EGLint*) =
            (EGLBoolean(*)(EGLDisplay, EGLConfig, EGLint, EGLint*))real("eglGetConfigAttrib");
        attrib(d, c, EGL_RENDERABLE_TYPE, &renderable);
        EGLint (*error)(void) = (EGLint(*)(void))real("eglGetError");
        fprintf(stderr, "egl_es_shim: eglCreateContext(api %#x, renderable %#x) -> %p, error %#x\n", app_api,
                renderable, context, context == EGL_NO_CONTEXT ? error() : 0x3000);
    }
    return context;
}
