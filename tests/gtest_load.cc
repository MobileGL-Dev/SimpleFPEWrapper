// SimpleFPEWrapper - tests/gtest_load.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// What has to hold before any context exists (plans/01):
//  1. dlopen()ing the wrapper performs no backend work: no static
//     constructor may load libEGL or throw (the old code std::terminate'd
//     without libEGL).
//  2. Resolving wrapper-implemented symbols does not touch the backend.
//  3. Resolving an unknown symbol triggers lazy backend init; whatever the
//     result, the process must survive.
//
// These deliberately do not use the context fixture: the point is what
// happens before, and with, nothing loaded.

#include "sfpew_gtest.h"

#include <cstdio>
#include <cstring>

namespace {

bool MapsContain(const char* needle) {
    std::FILE* maps = std::fopen("/proc/self/maps", "r");
    if (maps == nullptr) return false;
    char line[1024];
    bool found = false;
    while (std::fgets(line, sizeof line, maps) != nullptr) {
        if (std::strstr(line, needle) != nullptr) {
            found = true;
            break;
        }
    }
    std::fclose(maps);
    return found;
}

using ResolveFn = void* (*)(const char*);

class LoadTest : public ::testing::Test {
protected:
    void SetUp() override {
        egl_preloaded_ = MapsContain("libEGL");
        handle_ = dlopen(WRAPPER_LIB_PATH, RTLD_NOW | RTLD_LOCAL);
        ASSERT_NE(handle_, nullptr) << "dlopen: " << dlerror();
    }

    void TearDown() override {
        if (handle_ != nullptr) dlclose(handle_);
    }

    // True once libEGL is in the process for a reason this test caused.
    bool EglAppeared() const { return !egl_preloaded_ && MapsContain("libEGL"); }

    void* handle_ = nullptr;
    bool egl_preloaded_ = false;
};

TEST_F(LoadTest, LoadingAloneDoesNotStartTheBackend) {
    EXPECT_FALSE(EglAppeared()) << "dlopen alone loaded libEGL - static init is back";
}

TEST_F(LoadTest, WrapperSymbolsResolveWithoutTheBackend) {
    auto resolve = reinterpret_cast<ResolveFn>(dlsym(handle_, "eglGetProcAddress"));
    ASSERT_NE(resolve, nullptr) << "the wrapper exports no eglGetProcAddress";

    // Served straight from the resolver table, so it must not need a driver.
    EXPECT_NE(resolve("glBegin"), nullptr) << "resolver returned null for glBegin";
    EXPECT_FALSE(EglAppeared()) << "resolving a wrapper symbol loaded libEGL";
}

TEST_F(LoadTest, UnknownSymbolIsSurvivable) {
    auto resolve = reinterpret_cast<ResolveFn>(dlsym(handle_, "eglGetProcAddress"));
    ASSERT_NE(resolve, nullptr);

    // Falls through to the backend and triggers lazy init. Null is a legal
    // answer - there may be no driver here - and dying is not, which is the
    // whole assertion: reaching the next line at all.
    void* unknown = resolve("glSfpewDefinitelyUnknown123");
    SUCCEED() << "survived; unknown symbol resolved to " << unknown;
}

} // namespace
