// SimpleFPEWrapper - tests/global_resolver_backend.c
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// A backend for gtest_egl_exports.cc that resolves GL names the way a
// desktop-GL layer may: dlsym(RTLD_DEFAULT) first, its EGL's resolver after.
// Every other EGL entry is the system libEGL's, found through this library's
// dependencies, and the GL definitions it is meant to hand out are the
// system GLES library's, a dependency as well - reachable from this
// library's own scope, never from the global one.

#include <EGL/egl.h>
#include <dlfcn.h>
#include <stddef.h>

typedef __eglMustCastToProperFunctionPointerType (*ResolveFn)(const char*);

__attribute__((visibility("default"))) __eglMustCastToProperFunctionPointerType
eglGetProcAddress(const char* name) {
    void* global = dlsym(RTLD_DEFAULT, name);
    if (global != NULL) return (__eglMustCastToProperFunctionPointerType)global;
    static ResolveFn system_resolver;
    if (system_resolver == NULL) {
        void* egl = dlopen("libEGL.so.1", RTLD_NOW | RTLD_LOCAL);
        if (egl != NULL) system_resolver = (ResolveFn)dlsym(egl, "eglGetProcAddress");
    }
    return system_resolver != NULL ? system_resolver(name) : NULL;
}
