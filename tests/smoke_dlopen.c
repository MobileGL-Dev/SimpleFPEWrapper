// SimpleFPEWrapper - tests/smoke_dlopen.c
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// S1 exit-criteria smoke test (plans/01-build-safety-baseline.md):
//  1. dlopen()ing the wrapper performs no backend work: no static
//     constructor may load libEGL or throw (the old code std::terminate'd
//     without libEGL).
//  2. Resolving wrapper-implemented symbols does not touch the backend.
//  3. Resolving an unknown symbol triggers lazy backend init; whatever the
//     result, the process must survive.
// Written in C on purpose: it proves the extern "C" surface never leaks a
// C++ exception to a C caller.

#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

static int maps_contain(const char* needle) {
    FILE* maps = fopen("/proc/self/maps", "r");
    if (!maps) return -1;
    char line[1024];
    int found = 0;
    while (fgets(line, sizeof(line), maps)) {
        if (strstr(line, needle)) {
            found = 1;
            break;
        }
    }
    fclose(maps);
    return found;
}

int main(void) {
    const int egl_preloaded = maps_contain("libEGL");

    void* handle = dlopen(WRAPPER_LIB_PATH, RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        fprintf(stderr, "FAIL: dlopen: %s\n", dlerror());
        return 1;
    }

    if (!egl_preloaded && maps_contain("libEGL") == 1) {
        fprintf(stderr, "FAIL: dlopen alone loaded libEGL (static init is back?)\n");
        return 1;
    }

    typedef void* (*resolver_t)(const char*);
    resolver_t resolve = (resolver_t)dlsym(handle, "eglGetProcAddress");
    if (!resolve) {
        fprintf(stderr, "FAIL: wrapper does not export eglGetProcAddress\n");
        return 1;
    }

    // Wrapper-implemented symbol: served from the GETPROC table, must not
    // require the backend.
    if (!resolve("glBegin")) {
        fprintf(stderr, "FAIL: resolver returned null for wrapper symbol glBegin\n");
        return 1;
    }
    if (!egl_preloaded && maps_contain("libEGL") == 1) {
        fprintf(stderr, "FAIL: resolving a wrapper symbol loaded libEGL\n");
        return 1;
    }

    // Unknown symbol: falls through to the backend and triggers lazy init.
    // Null is a legal answer (no backend on this machine); dying is not.
    void* unknown = resolve("glSfpewDefinitelyUnknown123");
    printf("OK: survived; unknown symbol resolved to %p\n", unknown);
    return 0;
}
