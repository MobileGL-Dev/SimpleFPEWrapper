// SimpleFPEWrapper - SimpleFPEWrapper/init.cpp
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

#include "init.h"
#include "fpe/fpe.hpp"

#include <cstdio>
#include <cstdlib>

SFPEW::External::EGLFunctionsTable g_eglFuncs;
SFPEW::External::BackendGLFunctionsTable g_glFuncs;

namespace {

// Loading the backend at dlopen time ran inside a static constructor; a
// missing libEGL threw from it and terminated the host process before it
// could even report an error. Resolve the tables on first use instead and
// keep every failure inside the library boundary.
bool InitBackend() noexcept {
    try {
        std::string eglLibName;
        const char* envEglLib = std::getenv("SFPEW_EGL");
        eglLibName = envEglLib ? envEglLib : "libEGL.so";

        if (!SFPEW::Utils::BackendLoader::AcquireEGLFunctions(g_eglFuncs, eglLibName) ||
            g_eglFuncs.eglGetProcAddress == nullptr) {
            std::fprintf(stderr, "SFPEW: failed to acquire EGL functions from '%s'\n", eglLibName.c_str());
            return false;
        }

        if (!SFPEW::Utils::BackendLoader::AcquireBackendGLFunctions(g_glFuncs, g_eglFuncs.eglGetProcAddress) ||
            g_glFuncs.glGetString == nullptr) {
            std::fprintf(stderr, "SFPEW: failed to acquire backend GL functions\n");
            return false;
        } // FIXME: actually we should acquire gl functions after egl initialization

        return true;
    } catch (...) {
        // Never let an exception cross the extern "C" surface above us.
        std::fprintf(stderr, "SFPEW: backend initialization threw; wrapper disabled\n");
        return false;
    }
}

} // namespace

bool sfpewEnsureBackend() noexcept {
    // Magic-static: thread-safe, runs once; a failed attempt stays failed
    // and every caller degrades to a no-op instead of terminating.
    static const bool ok = InitBackend();
    return ok;
}
