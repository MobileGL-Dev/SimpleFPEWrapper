// SimpleFPEWrapper - tests/gtest_imaging_color_matrix.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// plans/17 P10: the color matrix stage and its post-matrix scale/bias have
// no glEnable in GL - 3.6.3 runs them unconditionally. So neither may
// depend on some OTHER imaging stage happening to be enabled to take
// effect. Both cases below set exactly one of the two and nothing else.
//
// The matrix here scales R and B by the same factor rather than permuting
// them: the point is that the stage ran at all, and a permutation would be
// indistinguishable from the llvmpipe BGRA readback swizzle.

#include "sfpew_gtest.h"

#include <array>

namespace {

using sfpew_test::ContextTest;
using sfpew_test::GLbitfield;
using sfpew_test::GLenum;
using sfpew_test::GLfloat;
using sfpew_test::GLint;
using sfpew_test::GLsizei;
using sfpew_test::GLubyte;
using sfpew_test::PixelProbe;

constexpr GLenum GL_NO_ERROR_ = 0;
constexpr GLenum GL_FLOAT_ = 0x1406;
constexpr GLenum GL_RGBA_ = 0x1908;
constexpr GLenum GL_MODELVIEW_ = 0x1700;
constexpr GLenum GL_COLOR_ = 0x1800;
constexpr GLbitfield GL_COLOR_BUFFER_BIT_ = 0x00004000;
constexpr GLenum GL_POST_COLOR_MATRIX_ALPHA_SCALE_ = 0x80B7;

class ImagingColorMatrixTest : public ContextTest {
protected:
    void SetUp() override {
        ContextTest::SetUp();
        if (::testing::Test::IsSkipped() || ::testing::Test::HasFatalFailure()) return;
        using MakeCurrentFn = EGLBoolean (*)(EGLDisplay, EGLSurface, EGLSurface, EGLContext);
        auto wrapper_make_current = Get<MakeCurrentFn>("eglMakeCurrent");
        ASSERT_TRUE(wrapper_make_current(display(), surface(), surface(), eglGetCurrentContext()));
    }
};

TEST_F(ImagingColorMatrixTest, ColorMatrixAloneStillTransformsDrawPixels) {
    auto matrix_mode = Get<void (*)(GLenum)>("glMatrixMode");
    auto load_matrix = Get<void (*)(const GLfloat*)>("glLoadMatrixf");
    auto load_identity = Get<void (*)()>("glLoadIdentity");
    auto viewport = Get<void (*)(GLint, GLint, GLsizei, GLsizei)>("glViewport");
    auto clear_color = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glClearColor");
    auto clear = Get<void (*)(GLbitfield)>("glClear");
    auto window_pos = Get<void (*)(GLint, GLint)>("glWindowPos2i");
    auto pixel_zoom = Get<void (*)(GLfloat, GLfloat)>("glPixelZoom");
    auto draw_pixels =
        Get<void (*)(GLsizei, GLsizei, GLenum, GLenum, const void*)>("glDrawPixels");
    auto read_pixels = Get<PixelProbe::ReadPixelsFn>("glReadPixels");
    auto get_error = Get<GLenum (*)()>("glGetError");

    const std::array<GLfloat, 16> halve_red_and_blue = {
        0.5f, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0.5f, 0, 0, 0, 0, 1,
    };
    const std::array<GLfloat, 4> magenta = {1, 0, 1, 1};

    viewport(0, 0, size(), size());
    clear_color(0, 0, 0, 1);
    clear(GL_COLOR_BUFFER_BIT_);
    window_pos(8, 8);
    pixel_zoom(8, 8);
    matrix_mode(GL_COLOR_);
    load_matrix(halve_red_and_blue.data());
    draw_pixels(1, 1, GL_RGBA_, GL_FLOAT_, magenta.data());
    // Back to identity before the readback: glReadPixels runs the same
    // transfer, and would otherwise halve the probe a second time.
    load_identity();
    matrix_mode(GL_MODELVIEW_);

    const PixelProbe probe(read_pixels);
    const auto transformed = probe.At(12, 12);
    EXPECT_NEAR(transformed.r, 128, 8);
    EXPECT_NEAR(transformed.b, 128, 8);
    EXPECT_LE(transformed.g, static_cast<GLubyte>(8));
    EXPECT_EQ(get_error(), GL_NO_ERROR_);
}

TEST_F(ImagingColorMatrixTest, PostColorMatrixScaleAloneStillScalesDrawPixels) {
    auto pixel_transfer = Get<void (*)(GLenum, GLfloat)>("glPixelTransferf");
    auto viewport = Get<void (*)(GLint, GLint, GLsizei, GLsizei)>("glViewport");
    auto clear_color = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glClearColor");
    auto clear = Get<void (*)(GLbitfield)>("glClear");
    auto window_pos = Get<void (*)(GLint, GLint)>("glWindowPos2i");
    auto pixel_zoom = Get<void (*)(GLfloat, GLfloat)>("glPixelZoom");
    auto draw_pixels =
        Get<void (*)(GLsizei, GLsizei, GLenum, GLenum, const void*)>("glDrawPixels");
    auto read_pixels = Get<PixelProbe::ReadPixelsFn>("glReadPixels");
    auto get_error = Get<GLenum (*)()>("glGetError");

    const std::array<GLfloat, 4> white = {1, 1, 1, 1};

    viewport(0, 0, size(), size());
    clear_color(0, 0, 0, 1);
    clear(GL_COLOR_BUFFER_BIT_);
    window_pos(8, 8);
    pixel_zoom(8, 8);
    pixel_transfer(GL_POST_COLOR_MATRIX_ALPHA_SCALE_, 0.25f);
    draw_pixels(1, 1, GL_RGBA_, GL_FLOAT_, white.data());
    pixel_transfer(GL_POST_COLOR_MATRIX_ALPHA_SCALE_, 1.0f);

    const PixelProbe probe(read_pixels);
    const auto scaled = probe.At(12, 12);
    EXPECT_NEAR(scaled.r, 255, 8);
    EXPECT_NEAR(scaled.b, 255, 8);
    EXPECT_NEAR(scaled.a, 64, 8) << "GL_POST_COLOR_MATRIX_ALPHA_SCALE was dropped";
    EXPECT_EQ(get_error(), GL_NO_ERROR_);
}

} // namespace
