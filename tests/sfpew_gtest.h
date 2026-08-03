// SimpleFPEWrapper - tests/sfpew_gtest.h
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// What every test of this library needs before it can test anything: the
// wrapper loaded by path, a current context, and its entry points resolved
// through the wrapper's own eglGetProcAddress rather than the loader's - the
// point of the exercise being what the wrapper answers, not what the driver
// underneath would have.
//
// A machine with no EGL, no ES 3 config or no context skips rather than
// fails. That distinction is the whole reason these are not plain asserts:
// a headless build machine has nothing to say about a rendering bug, and a
// suite that goes red there teaches everyone to ignore it.
#pragma once

#include <gtest/gtest.h>

#include <dlfcn.h>
#include <EGL/egl.h>

#include <string>
#include <vector>

namespace sfpew_test {

// The GL types the tests use. Deliberately not GL/gl.h: these run against
// the wrapper through dlopen, so the header would only be a second opinion
// about types the ABI already fixes.
using GLenum = unsigned int;
using GLuint = unsigned int;
using GLbitfield = unsigned int;
using GLboolean = unsigned char;
using GLubyte = unsigned char;
using GLushort = unsigned short;
using GLint = int;
using GLsizei = int;
using GLfloat = float;
using GLdouble = double;
using GLchar = char;
using GLsizeiptr = long;
using GLintptr = long;

constexpr GLboolean GL_FALSE_ = 0;
constexpr GLboolean GL_TRUE_ = 1;

// Which client API the case wants underneath. The two cannot share a
// process - see the note in CMakeLists.txt - so a case that wants the
// desktop side says so, and gtest_discover_tests keeps them apart.
enum class Backend {
    GLES3,
    // A core profile, never whatever the driver hands out by default: a
    // compatibility context answers everything GL 2.1 ever had, so it would
    // pass tests that are about the wrapper answering instead.
    DesktopCore,
};

// The wrapper loaded and nothing else. For the parts that are pure
// bookkeeping - display-list recording, error latching, state the wrapper
// answers out of its own memory - which must work with no driver in sight,
// because on a headless machine that is exactly what they get.
class LibraryTest : public ::testing::Test {
protected:
    void SetUp() override {
        library_ = dlopen(WRAPPER_LIB_PATH, RTLD_NOW | RTLD_LOCAL);
        ASSERT_NE(library_, nullptr) << "dlopen(" << WRAPPER_LIB_PATH << "): " << dlerror();
        resolve_ = reinterpret_cast<void* (*)(const char*)>(dlsym(library_, "eglGetProcAddress"));
        ASSERT_NE(resolve_, nullptr) << "the wrapper exports no eglGetProcAddress";
    }

    void TearDown() override {
        if (library_ != nullptr) dlclose(library_);
    }

public:
    template <typename Fn>
    Fn Get(const char* name) {
        void* address = resolve_ != nullptr ? resolve_(name) : nullptr;
        EXPECT_NE(address, nullptr) << "the wrapper does not resolve " << name;
        return reinterpret_cast<Fn>(address);
    }

private:
    void* library_ = nullptr;
    void* (*resolve_)(const char*) = nullptr;
};

// One current context, torn down after the case. Default is a 64x64 pbuffer,
// which is what most of these read pixels out of.
class ContextTest : public ::testing::Test {
public:
    static constexpr int kDefaultSize = 64;

protected:
    explicit ContextTest(Backend backend = Backend::GLES3, int size = kDefaultSize)
        : backend_(backend), size_(size) {}

    void SetUp() override {
        library_ = dlopen(WRAPPER_LIB_PATH, RTLD_NOW | RTLD_LOCAL);
        ASSERT_NE(library_, nullptr) << "dlopen(" << WRAPPER_LIB_PATH << "): " << dlerror();
        resolve_ = reinterpret_cast<ResolveFn>(dlsym(library_, "eglGetProcAddress"));
        ASSERT_NE(resolve_, nullptr) << "the wrapper exports no eglGetProcAddress";

        display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (display_ == EGL_NO_DISPLAY || !eglInitialize(display_, nullptr, nullptr))
            GTEST_SKIP() << "no EGL display";

        const bool desktop = backend_ == Backend::DesktopCore;
        const EGLint config_attribs[] = {EGL_SURFACE_TYPE,
                                         EGL_PBUFFER_BIT,
                                         EGL_RENDERABLE_TYPE,
                                         desktop ? EGL_OPENGL_BIT : EGL_OPENGL_ES3_BIT,
                                         EGL_RED_SIZE,
                                         8,
                                         EGL_GREEN_SIZE,
                                         8,
                                         EGL_BLUE_SIZE,
                                         8,
                                         EGL_ALPHA_SIZE,
                                         8,
                                         EGL_DEPTH_SIZE,
                                         16,
                                         EGL_NONE};
        EGLint count = 0;
        if (!eglChooseConfig(display_, config_attribs, &config_, 1, &count) || count == 0)
            GTEST_SKIP() << (desktop ? "no desktop GL config" : "no ES3 config");

        const EGLint surface_attribs[] = {EGL_WIDTH, size_, EGL_HEIGHT, size_, EGL_NONE};
        surface_ = eglCreatePbufferSurface(display_, config_, surface_attribs);
        if (surface_ == EGL_NO_SURFACE) GTEST_SKIP() << "no pbuffer surface";

        if (!eglBindAPI(desktop ? EGL_OPENGL_API : EGL_OPENGL_ES_API))
            GTEST_SKIP() << (desktop ? "no desktop GL API" : "no ES API");

        // EGL 1.5 spelling for the profile request.
        const EGLint core_attribs[] = {0x3098 /* EGL_CONTEXT_MAJOR_VERSION */,
                                       3,
                                       0x30FB /* EGL_CONTEXT_MINOR_VERSION */,
                                       3,
                                       0x30FD /* EGL_CONTEXT_OPENGL_PROFILE_MASK */,
                                       0x00000001 /* CORE_PROFILE_BIT */,
                                       EGL_NONE};
        const EGLint es_attribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
        context_ = eglCreateContext(display_, config_, EGL_NO_CONTEXT,
                                    desktop ? core_attribs : es_attribs);
        if (context_ == EGL_NO_CONTEXT)
            GTEST_SKIP() << (desktop ? "no core profile context" : "no ES3 context");
        if (!eglMakeCurrent(display_, surface_, surface_, context_))
            GTEST_SKIP() << "no current context";
    }

    void TearDown() override {
        if (display_ != EGL_NO_DISPLAY) {
            eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            if (context_ != EGL_NO_CONTEXT) eglDestroyContext(display_, context_);
            if (surface_ != EGL_NO_SURFACE) eglDestroySurface(display_, surface_);
        }
        // The display stays initialized on purpose: eglTerminate would pull
        // it out from under the wrapper's own cached handles, and the
        // process is about to end anyway.
        if (library_ != nullptr) dlclose(library_);
    }

public:
    // A wrapper entry point, typed. A name the wrapper does not answer is a
    // failure in itself - the test was written for a surface that is meant
    // to exist.
    template <typename Fn>
    Fn Get(const char* name) {
        void* address = resolve_ != nullptr ? resolve_(name) : nullptr;
        EXPECT_NE(address, nullptr) << "the wrapper does not resolve " << name;
        return reinterpret_cast<Fn>(address);
    }

    // Same, but for entry points a test only uses if they are there.
    template <typename Fn>
    Fn GetOptional(const char* name) {
        return reinterpret_cast<Fn>(resolve_ != nullptr ? resolve_(name) : nullptr);
    }

    // A symbol straight off the library, for the test-only hooks the wrapper
    // exports that are not GL entry points (sfpewMarkBufferInternalForTest and
    // friends): those answer dlsym, not eglGetProcAddress.
    template <typename Fn>
    Fn Dlsym(const char* name) {
        return reinterpret_cast<Fn>(library_ != nullptr ? ::dlsym(library_, name) : nullptr);
    }

    EGLDisplay display() const { return display_; }
    EGLSurface surface() const { return surface_; }
    int size() const { return size_; }

protected:

private:
    using ResolveFn = void* (*)(const char*);

    Backend backend_;
    int size_;
    void* library_ = nullptr;
    ResolveFn resolve_ = nullptr;
    EGLDisplay display_ = EGL_NO_DISPLAY;
    EGLConfig config_ = nullptr;
    EGLSurface surface_ = EGL_NO_SURFACE;
    EGLContext context_ = EGL_NO_CONTEXT;
};

// The same, over a desktop core profile.
class DesktopContextTest : public ContextTest {
protected:
    DesktopContextTest() : ContextTest(Backend::DesktopCore) {}
};

// What most of these tests do to check a draw landed: read one pixel back
// and ask whether it is lit. "Lit" is the green channel past a threshold,
// which is what every ported piglit case here already agreed on - green
// because it is the channel every other piece of wrapper state (alpha
// blending, the clear colour, texturing) is least likely to touch by
// accident.
class PixelProbe {
public:
    using ReadPixelsFn = void (*)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*);

    explicit PixelProbe(ReadPixelsFn read_pixels) : read_pixels_(read_pixels) {}

    struct Rgba {
        GLubyte r = 0, g = 0, b = 0, a = 0;
    };

    Rgba At(int x, int y) const {
        Rgba pixel;
        read_pixels_(x, y, 1, 1, /*GL_RGBA*/ 0x1908, /*GL_UNSIGNED_BYTE*/ 0x1401, &pixel);
        return pixel;
    }

    // True when the green channel is past the threshold every ported piglit
    // probe in this suite has used.
    bool IsLitAt(int x, int y) const { return At(x, y).g > 150; }

    // Every pixel in an axis-aligned region, or nullptr if all of it is
    // black (RGB channels <= 20). What "nothing was drawn" checks read.
    struct LitPixel {
        int x = -1, y = -1;
        int count = 0;
    };
    LitPixel FindLit(int x, int y, int width, int height) const {
        std::vector<Rgba> pixels(static_cast<size_t>(width) * static_cast<size_t>(height));
        read_pixels_(x, y, width, height, /*GL_RGBA*/ 0x1908, /*GL_UNSIGNED_BYTE*/ 0x1401,
                    pixels.data());
        LitPixel result;
        for (int i = 0; i < width * height; ++i) {
            const Rgba& p = pixels[static_cast<size_t>(i)];
            if (p.r > 20 || p.g > 20 || p.b > 20) {
                if (result.count == 0) {
                    result.x = x + i % width;
                    result.y = y + i / width;
                }
                ++result.count;
            }
        }
        return result;
    }

    // The opposite check: at least one pixel in the region is non-black.
    bool AnyLit(int x, int y, int width, int height) const {
        return FindLit(x, y, width, height).count > 0;
    }

private:
    ReadPixelsFn read_pixels_;
};

// EXPECT_EQ(probe.IsLitAt(x, y), want) with a message that names the pixel,
// for the common case of many probes in a row.
#define SFPEW_EXPECT_LIT(probe, x, y, want, what)                                                   \
    EXPECT_EQ((probe).IsLitAt((x), (y)), (want)) << (what) << " at (" << (x) << ", " << (y) << ")"

// The whole region must be black: nothing there was drawable. Parameters are
// suffixed _sfpew_ because these expand into a plain do/while, textually -
// FindLit's own x/y arguments would otherwise collide with LitPixel's x/y
// members inside this body.
#define SFPEW_EXPECT_BLANK(probe, sfpew_x_, sfpew_y_, sfpew_w_, sfpew_h_, what)                     \
    do {                                                                                            \
        const auto sfpew_lit_ =                                                                     \
            (probe).FindLit((sfpew_x_), (sfpew_y_), (sfpew_w_), (sfpew_h_));                        \
        EXPECT_EQ(sfpew_lit_.count, 0)                                                              \
            << (what) << ": " << sfpew_lit_.count << " lit pixel(s), first at (" << sfpew_lit_.x    \
            << ", " << sfpew_lit_.y << ")";                                                          \
    } while (0)

} // namespace sfpew_test
