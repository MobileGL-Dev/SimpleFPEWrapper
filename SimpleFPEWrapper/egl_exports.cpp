// SimpleFPEWrapper - SimpleFPEWrapper/egl_exports.cpp
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// The rest of the EGL 1.5 core API, exported so the wrapper can stand in for
// a whole libEGL. A launcher loads one library for EGL and one for GL, and
// takes every EGL entry point by name from the first; pointing both at the
// wrapper lets SFPEW_EGL alone decide which backend sits underneath, with no
// second library path to keep in step with it.
//
// The commands the wrapper has its own reasons to see (context creation and
// destruction, make-current, swaps, eglGetProcAddress) are defined in
// lookup.cpp. Everything here goes straight to the backend's own entry point,
// resolved from the SFPEW_EGL library at first use like every other backend
// call; with no backend, each answers the way EGL does without a display.

#include "init.h"

#define SFPEW_FORWARD_EGL(RETURN, NAME, FAILED, PARAMS, ARGS)                                      \
    SFPEW_APIENTRY RETURN NAME PARAMS {                                                            \
        if (!sfpewEnsureBackend() || g_eglFuncs.NAME == nullptr) return FAILED;                    \
        return g_eglFuncs.NAME ARGS;                                                               \
    }

// EGL 1.0 - 1.4
SFPEW_FORWARD_EGL(EGLBoolean, eglChooseConfig, EGL_FALSE,
                  (EGLDisplay dpy, const EGLint* attrib_list, EGLConfig* configs, EGLint config_size,
                   EGLint* num_config),
                  (dpy, attrib_list, configs, config_size, num_config))
SFPEW_FORWARD_EGL(EGLBoolean, eglCopyBuffers, EGL_FALSE,
                  (EGLDisplay dpy, EGLSurface surface, EGLNativePixmapType target), (dpy, surface, target))
SFPEW_FORWARD_EGL(EGLSurface, eglCreatePbufferSurface, EGL_NO_SURFACE,
                  (EGLDisplay dpy, EGLConfig config, const EGLint* attrib_list), (dpy, config, attrib_list))
SFPEW_FORWARD_EGL(EGLSurface, eglCreatePixmapSurface, EGL_NO_SURFACE,
                  (EGLDisplay dpy, EGLConfig config, EGLNativePixmapType pixmap, const EGLint* attrib_list),
                  (dpy, config, pixmap, attrib_list))
SFPEW_FORWARD_EGL(EGLSurface, eglCreateWindowSurface, EGL_NO_SURFACE,
                  (EGLDisplay dpy, EGLConfig config, EGLNativeWindowType win, const EGLint* attrib_list),
                  (dpy, config, win, attrib_list))
SFPEW_FORWARD_EGL(EGLBoolean, eglDestroySurface, EGL_FALSE, (EGLDisplay dpy, EGLSurface surface),
                  (dpy, surface))
SFPEW_FORWARD_EGL(EGLBoolean, eglGetConfigAttrib, EGL_FALSE,
                  (EGLDisplay dpy, EGLConfig config, EGLint attribute, EGLint* value),
                  (dpy, config, attribute, value))
SFPEW_FORWARD_EGL(EGLBoolean, eglGetConfigs, EGL_FALSE,
                  (EGLDisplay dpy, EGLConfig* configs, EGLint config_size, EGLint* num_config),
                  (dpy, configs, config_size, num_config))
SFPEW_FORWARD_EGL(EGLDisplay, eglGetCurrentDisplay, EGL_NO_DISPLAY, (void), ())
SFPEW_FORWARD_EGL(EGLSurface, eglGetCurrentSurface, EGL_NO_SURFACE, (EGLint readdraw), (readdraw))
SFPEW_FORWARD_EGL(EGLDisplay, eglGetDisplay, EGL_NO_DISPLAY, (EGLNativeDisplayType display_id), (display_id))
SFPEW_FORWARD_EGL(EGLint, eglGetError, EGL_NOT_INITIALIZED, (void), ())
SFPEW_FORWARD_EGL(EGLBoolean, eglInitialize, EGL_FALSE, (EGLDisplay dpy, EGLint* major, EGLint* minor),
                  (dpy, major, minor))
SFPEW_FORWARD_EGL(EGLBoolean, eglQueryContext, EGL_FALSE,
                  (EGLDisplay dpy, EGLContext ctx, EGLint attribute, EGLint* value), (dpy, ctx, attribute, value))
SFPEW_FORWARD_EGL(const char*, eglQueryString, nullptr, (EGLDisplay dpy, EGLint name), (dpy, name))
SFPEW_FORWARD_EGL(EGLBoolean, eglQuerySurface, EGL_FALSE,
                  (EGLDisplay dpy, EGLSurface surface, EGLint attribute, EGLint* value),
                  (dpy, surface, attribute, value))
SFPEW_FORWARD_EGL(EGLBoolean, eglTerminate, EGL_FALSE, (EGLDisplay dpy), (dpy))
SFPEW_FORWARD_EGL(EGLBoolean, eglWaitGL, EGL_FALSE, (void), ())
SFPEW_FORWARD_EGL(EGLBoolean, eglWaitNative, EGL_FALSE, (EGLint engine), (engine))
SFPEW_FORWARD_EGL(EGLBoolean, eglBindTexImage, EGL_FALSE, (EGLDisplay dpy, EGLSurface surface, EGLint buffer),
                  (dpy, surface, buffer))
SFPEW_FORWARD_EGL(EGLBoolean, eglReleaseTexImage, EGL_FALSE, (EGLDisplay dpy, EGLSurface surface, EGLint buffer),
                  (dpy, surface, buffer))
SFPEW_FORWARD_EGL(EGLBoolean, eglSurfaceAttrib, EGL_FALSE,
                  (EGLDisplay dpy, EGLSurface surface, EGLint attribute, EGLint value),
                  (dpy, surface, attribute, value))
SFPEW_FORWARD_EGL(EGLBoolean, eglSwapInterval, EGL_FALSE, (EGLDisplay dpy, EGLint interval), (dpy, interval))
SFPEW_FORWARD_EGL(EGLBoolean, eglBindAPI, EGL_FALSE, (EGLenum api), (api))
SFPEW_FORWARD_EGL(EGLenum, eglQueryAPI, EGL_NONE, (void), ())
SFPEW_FORWARD_EGL(EGLSurface, eglCreatePbufferFromClientBuffer, EGL_NO_SURFACE,
                  (EGLDisplay dpy, EGLenum buftype, EGLClientBuffer buffer, EGLConfig config,
                   const EGLint* attrib_list),
                  (dpy, buftype, buffer, config, attrib_list))
SFPEW_FORWARD_EGL(EGLBoolean, eglReleaseThread, EGL_FALSE, (void), ())
SFPEW_FORWARD_EGL(EGLBoolean, eglWaitClient, EGL_FALSE, (void), ())
SFPEW_FORWARD_EGL(EGLContext, eglGetCurrentContext, EGL_NO_CONTEXT, (void), ())

// EGL 1.5
SFPEW_FORWARD_EGL(EGLSync, eglCreateSync, EGL_NO_SYNC, (EGLDisplay dpy, EGLenum type, const EGLAttrib* attrib_list),
                  (dpy, type, attrib_list))
SFPEW_FORWARD_EGL(EGLBoolean, eglDestroySync, EGL_FALSE, (EGLDisplay dpy, EGLSync sync), (dpy, sync))
SFPEW_FORWARD_EGL(EGLint, eglClientWaitSync, EGL_FALSE, (EGLDisplay dpy, EGLSync sync, EGLint flags, EGLTime timeout),
                  (dpy, sync, flags, timeout))
SFPEW_FORWARD_EGL(EGLBoolean, eglGetSyncAttrib, EGL_FALSE,
                  (EGLDisplay dpy, EGLSync sync, EGLint attribute, EGLAttrib* value), (dpy, sync, attribute, value))
SFPEW_FORWARD_EGL(EGLImage, eglCreateImage, EGL_NO_IMAGE,
                  (EGLDisplay dpy, EGLContext ctx, EGLenum target, EGLClientBuffer buffer,
                   const EGLAttrib* attrib_list),
                  (dpy, ctx, target, buffer, attrib_list))
SFPEW_FORWARD_EGL(EGLBoolean, eglDestroyImage, EGL_FALSE, (EGLDisplay dpy, EGLImage image), (dpy, image))
SFPEW_FORWARD_EGL(EGLDisplay, eglGetPlatformDisplay, EGL_NO_DISPLAY,
                  (EGLenum platform, void* native_display, const EGLAttrib* attrib_list),
                  (platform, native_display, attrib_list))
SFPEW_FORWARD_EGL(EGLSurface, eglCreatePlatformWindowSurface, EGL_NO_SURFACE,
                  (EGLDisplay dpy, EGLConfig config, void* native_window, const EGLAttrib* attrib_list),
                  (dpy, config, native_window, attrib_list))
SFPEW_FORWARD_EGL(EGLSurface, eglCreatePlatformPixmapSurface, EGL_NO_SURFACE,
                  (EGLDisplay dpy, EGLConfig config, void* native_pixmap, const EGLAttrib* attrib_list),
                  (dpy, config, native_pixmap, attrib_list))
SFPEW_FORWARD_EGL(EGLBoolean, eglWaitSync, EGL_FALSE, (EGLDisplay dpy, EGLSync sync, EGLint flags),
                  (dpy, sync, flags))

#undef SFPEW_FORWARD_EGL
