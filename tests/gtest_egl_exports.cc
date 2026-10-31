// SimpleFPEWrapper - tests/gtest_egl_exports.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// The wrapper as a whole libEGL (egl_exports.cpp): a renderer plugin points a
// launcher's EGL library at it as well as its GL library, so every EGL 1.5
// core command must be exported, and a session driven entirely through those
// exports must work. And SFPEW_EGL, which picks the backend underneath: a
// bare library name is found beside the wrapper - where a plugin ships its
// backends - and a backend that cannot be loaded is an error, never a silent
// switch to the system's.
//
// Every case loads its own wrapper and sets SFPEW_EGL before it does; the
// backend is chosen once per process, which ctest gives each case.

#include "sfpew_gtest.h"

#include <EGL/egl.h>
#include <dlfcn.h>
#include <filesystem>
#include <string>
#include <unistd.h>

namespace {

constexpr const char* kEgl15Core[] = {
    "eglBindAPI", "eglBindTexImage", "eglChooseConfig", "eglClientWaitSync", "eglCopyBuffers",
    "eglCreateContext", "eglCreateImage", "eglCreatePbufferFromClientBuffer", "eglCreatePbufferSurface",
    "eglCreatePixmapSurface", "eglCreatePlatformPixmapSurface", "eglCreatePlatformWindowSurface",
    "eglCreateSync", "eglCreateWindowSurface", "eglDestroyContext", "eglDestroyImage", "eglDestroySurface",
    "eglDestroySync", "eglGetConfigAttrib", "eglGetConfigs", "eglGetCurrentContext", "eglGetCurrentDisplay",
    "eglGetCurrentSurface", "eglGetDisplay", "eglGetError", "eglGetPlatformDisplay", "eglGetProcAddress",
    "eglGetSyncAttrib", "eglInitialize", "eglMakeCurrent", "eglQueryAPI", "eglQueryContext",
    "eglQueryString", "eglQuerySurface", "eglReleaseTexImage", "eglReleaseThread", "eglSurfaceAttrib",
    "eglSwapBuffers", "eglSwapInterval", "eglTerminate", "eglWaitClient", "eglWaitGL", "eglWaitNative",
    "eglWaitSync",
};

// The libEGL this test binary links: the backend every case here asks for,
// under one name or another.
std::string SystemEglPath() {
    Dl_info info{};
    if (dladdr(reinterpret_cast<void*>(&eglGetDisplay), &info) == 0 || info.dli_fname == nullptr) return {};
    return std::filesystem::canonical(info.dli_fname).string();
}

class EglExportsTest : public ::testing::Test {
protected:
    void TearDown() override {
        if (wrapper_ != nullptr) dlclose(wrapper_);
        unsetenv("SFPEW_EGL");
        if (!scratch_.empty()) std::filesystem::remove_all(scratch_);
    }

    // A private copy of the wrapper in a fresh directory, so a backend can be
    // put beside it.
    std::string WrapperCopy() {
        char pattern[] = "/tmp/sfpew-egl-exports-XXXXXX";
        const char* dir = mkdtemp(pattern);
        if (dir == nullptr) return {};
        scratch_ = dir;
        const std::string copy = scratch_ + "/libSimpleFPEWrapper.so";
        std::filesystem::copy_file(WRAPPER_LIB_PATH, copy);
        return copy;
    }

    void RunSession();

    template <typename Fn>
    Fn Export(const char* name) {
        return reinterpret_cast<Fn>(dlsym(wrapper_, name));
    }

    void* wrapper_ = nullptr;
    std::string scratch_;
};

TEST_F(EglExportsTest, EveryEgl15CoreCommandIsExported) {
    wrapper_ = dlopen(WRAPPER_LIB_PATH, RTLD_NOW | RTLD_LOCAL);
    ASSERT_NE(wrapper_, nullptr) << dlerror();
    for (const char* name : kEgl15Core) EXPECT_NE(dlsym(wrapper_, name), nullptr) << name << " is not exported";
}

// Display, config, surface, context and make-current through the wrapper's
// own exports alone, then a clear read back through its GL.
void EglExportsTest::RunSession() {
    auto get_display = Export<EGLDisplay (*)(EGLNativeDisplayType)>("eglGetDisplay");
    auto initialize = Export<EGLBoolean (*)(EGLDisplay, EGLint*, EGLint*)>("eglInitialize");
    auto choose_config = Export<EGLBoolean (*)(EGLDisplay, const EGLint*, EGLConfig*, EGLint, EGLint*)>("eglChooseConfig");
    auto create_pbuffer = Export<EGLSurface (*)(EGLDisplay, EGLConfig, const EGLint*)>("eglCreatePbufferSurface");
    auto bind_api = Export<EGLBoolean (*)(EGLenum)>("eglBindAPI");
    auto create_context = Export<EGLContext (*)(EGLDisplay, EGLConfig, EGLContext, const EGLint*)>("eglCreateContext");
    auto make_current = Export<EGLBoolean (*)(EGLDisplay, EGLSurface, EGLSurface, EGLContext)>("eglMakeCurrent");
    auto get_current_context = Export<EGLContext (*)()>("eglGetCurrentContext");
    auto destroy_context = Export<EGLBoolean (*)(EGLDisplay, EGLContext)>("eglDestroyContext");
    auto destroy_surface = Export<EGLBoolean (*)(EGLDisplay, EGLSurface)>("eglDestroySurface");
    auto resolve = Export<void* (*)(const char*)>("eglGetProcAddress");

    const EGLDisplay display = get_display(EGL_DEFAULT_DISPLAY);
    if (display == EGL_NO_DISPLAY || !initialize(display, nullptr, nullptr)) GTEST_SKIP() << "no EGL display";
    const EGLint config_attribs[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, 0x40 /* ES3 */,
                                     EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_NONE};
    EGLConfig config = nullptr;
    EGLint count = 0;
    if (!choose_config(display, config_attribs, &config, 1, &count) || count == 0) GTEST_SKIP() << "no ES3 config";
    const EGLint surface_attribs[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
    const EGLSurface surface = create_pbuffer(display, config, surface_attribs);
    ASSERT_NE(surface, EGL_NO_SURFACE);
    ASSERT_TRUE(bind_api(EGL_OPENGL_ES_API));
    const EGLint context_attribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    const EGLContext context = create_context(display, config, EGL_NO_CONTEXT, context_attribs);
    ASSERT_NE(context, EGL_NO_CONTEXT);
    ASSERT_TRUE(make_current(display, surface, surface, context));
    EXPECT_EQ(get_current_context(), context) << "eglGetCurrentContext must report the context just made current";

    auto clear_color = reinterpret_cast<void (*)(float, float, float, float)>(resolve("glClearColor"));
    auto clear = reinterpret_cast<void (*)(unsigned)>(resolve("glClear"));
    auto read_pixels =
        reinterpret_cast<void (*)(int, int, int, int, unsigned, unsigned, void*)>(resolve("glReadPixels"));
    ASSERT_NE(read_pixels, nullptr);
    clear_color(0.0f, 1.0f, 0.0f, 1.0f);
    clear(0x00004000 /* GL_COLOR_BUFFER_BIT */);
    unsigned char pixel[4] = {};
    read_pixels(8, 8, 1, 1, 0x1908 /* GL_RGBA */, 0x1401 /* GL_UNSIGNED_BYTE */, pixel);
    EXPECT_LT(pixel[0], 20);
    EXPECT_GT(pixel[1], 235) << "the clear did not reach the surface made current through the exports";

    auto get_error = reinterpret_cast<unsigned (*)()>(resolve("glGetError"));
    ASSERT_NE(get_error, nullptr);
    while (get_error() != 0 /* GL_NO_ERROR */) {
    }

    make_current(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    destroy_context(display, context);
    destroy_surface(display, surface);
}

TEST_F(EglExportsTest, ASessionRunsThroughTheExportsAlone) {
    wrapper_ = dlopen(WRAPPER_LIB_PATH, RTLD_NOW | RTLD_LOCAL);
    ASSERT_NE(wrapper_, nullptr) << dlerror();
    RunSession();
}

// A launcher loads the wrapper globally, and a backend that resolves GL
// names with dlsym(RTLD_DEFAULT) then finds the wrapper's own exports ahead
// of its own definitions. Taking those as the backend's made every call
// forward to itself: glGetError's tail call spun forever, as the drain loop
// above would here. The alarm turns that hang into a failure.
TEST_F(EglExportsTest, BackendResolvingGloballyIsNotHandedTheWrapper) {
    setenv("SFPEW_EGL", SFPEW_GLOBAL_RESOLVER_BACKEND, 1);
    wrapper_ = dlopen(WRAPPER_LIB_PATH, RTLD_NOW | RTLD_GLOBAL);
    ASSERT_NE(wrapper_, nullptr) << dlerror();
    alarm(60);
    RunSession();
    alarm(0);
}

// A bare SFPEW_EGL name is found beside the wrapper: here a backend alias
// for the system libEGL, which no search path holds under that name.
TEST_F(EglExportsTest, BareBackendNameIsFoundBesideTheWrapper) {
    const std::string system_egl = SystemEglPath();
    ASSERT_FALSE(system_egl.empty());
    const std::string copy = WrapperCopy();
    ASSERT_FALSE(copy.empty());
    std::filesystem::create_symlink(system_egl, scratch_ + "/libsfpew-test-backend.so");
    setenv("SFPEW_EGL", "libsfpew-test-backend.so", 1);
    wrapper_ = dlopen(copy.c_str(), RTLD_NOW | RTLD_LOCAL);
    ASSERT_NE(wrapper_, nullptr) << dlerror();
    auto get_display = Export<EGLDisplay (*)(EGLNativeDisplayType)>("eglGetDisplay");
    EXPECT_NE(get_display(EGL_DEFAULT_DISPLAY), EGL_NO_DISPLAY)
        << "the backend beside the wrapper was not loaded";
}

// A backend that cannot be loaded leaves the wrapper without one - it must
// not quietly run on the system libEGL instead of the backend chosen.
TEST_F(EglExportsTest, MissingBackendIsNotReplacedByTheSystemOne) {
    const std::string copy = WrapperCopy();
    ASSERT_FALSE(copy.empty());
    setenv("SFPEW_EGL", "libsfpew-no-such-backend.so", 1);
    wrapper_ = dlopen(copy.c_str(), RTLD_NOW | RTLD_LOCAL);
    ASSERT_NE(wrapper_, nullptr) << dlerror();
    auto get_display = Export<EGLDisplay (*)(EGLNativeDisplayType)>("eglGetDisplay");
    EXPECT_EQ(get_display(EGL_DEFAULT_DISPLAY), EGL_NO_DISPLAY) << "fell back to a backend nobody chose";
}

} // namespace
