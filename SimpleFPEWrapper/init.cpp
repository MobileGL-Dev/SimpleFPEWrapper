// SimpleFPEWrapper - SimpleFPEWrapper/init.cpp
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

#include "init.h"
#include "fpe/fpe.hpp"

SFPEW::External::EGLFunctionsTable g_eglFuncs;
SFPEW::External::BackendGLFunctionsTable g_glFuncs;

void Init() {
    std::string eglLibName;
    const char* envEglLib = std::getenv("SFPEW_EGL");
    if (envEglLib) {
        eglLibName = envEglLib;
    } else {
        eglLibName = "libEGL.so";
    }

    if (!SFPEW::Utils::BackendLoader::AcquireEGLFunctions(g_eglFuncs, eglLibName) ||
        g_eglFuncs.eglGetProcAddress == nullptr) {
        throw std::runtime_error("Failed to acquire EGL functions");
    }

    if (!SFPEW::Utils::BackendLoader::AcquireBackendGLFunctions(g_glFuncs, g_eglFuncs.eglGetProcAddress) ||
        g_glFuncs.glGetString == nullptr) {
        throw std::runtime_error("Failed to acquire BackendGL functions");
    } // FIXME: actually we should acquire gl functions after egl initialization

}

struct InitClass {
    InitClass() { Init(); }
};

static InitClass staticInitObject;
