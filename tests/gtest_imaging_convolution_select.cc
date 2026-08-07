// SimpleFPEWrapper - tests/gtest_imaging_convolution_select.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// plans/17 P12: which convolution filter a 2D pixel rectangle gets. Every
// image that reaches the transfer from glDrawPixels/glReadPixels/glTexImage
// is two-dimensional, and GL 2.1 3.6.3 picks between GL_CONVOLUTION_2D
// (winning) and GL_SEPARABLE_2D for those; GL_CONVOLUTION_1D applies to
// one-dimensional images only and never competes here.

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
constexpr GLbitfield GL_COLOR_BUFFER_BIT_ = 0x00004000;
constexpr GLenum GL_CONVOLUTION_1D_ = 0x8010;
constexpr GLenum GL_CONVOLUTION_2D_ = 0x8011;

class ImagingConvolutionSelectTest : public ContextTest {
protected:
    void SetUp() override {
        ContextTest::SetUp();
        if (::testing::Test::IsSkipped() || ::testing::Test::HasFatalFailure()) return;
        using MakeCurrentFn = EGLBoolean (*)(EGLDisplay, EGLSurface, EGLSurface, EGLContext);
        auto wrapper_make_current = Get<MakeCurrentFn>("eglMakeCurrent");
        ASSERT_TRUE(wrapper_make_current(display(), surface(), surface(), eglGetCurrentContext()));
    }
};

TEST_F(ImagingConvolutionSelectTest, TwoDimensionalFilterWinsOverTheOneDimensionalOne) {
    auto filter_1d =
        Get<void (*)(GLenum, GLenum, GLsizei, GLenum, GLenum, const void*)>(
            "glConvolutionFilter1D");
    auto filter_2d =
        Get<void (*)(GLenum, GLenum, GLsizei, GLsizei, GLenum, GLenum, const void*)>(
            "glConvolutionFilter2D");
    auto enable = Get<void (*)(GLenum)>("glEnable");
    auto disable = Get<void (*)(GLenum)>("glDisable");
    auto viewport = Get<void (*)(GLint, GLint, GLsizei, GLsizei)>("glViewport");
    auto clear_color = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glClearColor");
    auto clear = Get<void (*)(GLbitfield)>("glClear");
    auto window_pos = Get<void (*)(GLint, GLint)>("glWindowPos2i");
    auto pixel_zoom = Get<void (*)(GLfloat, GLfloat)>("glPixelZoom");
    auto draw_pixels =
        Get<void (*)(GLsizei, GLsizei, GLenum, GLenum, const void*)>("glDrawPixels");
    auto read_pixels = Get<PixelProbe::ReadPixelsFn>("glReadPixels");
    auto get_error = Get<GLenum (*)()>("glGetError");

    // Two filters that cannot be confused for one another. The 2D one is a
    // single centre tap, so a white source stays white and GL_REDUCE takes
    // 8x8 down to 6x6. The 1D one keeps green only, and its two taps would
    // reduce to 7x8 instead - a different colour AND a different rectangle.
    std::array<GLfloat, 36> centre_tap{};
    centre_tap[16] = centre_tap[17] = centre_tap[18] = centre_tap[19] = 1.0f;
    const std::array<GLfloat, 8> green_halves = {0, 0.5f, 0, 0.5f, 0, 0.5f, 0, 0.5f};
    std::array<GLfloat, 8 * 8 * 4> white;
    white.fill(1.0f);

    filter_1d(GL_CONVOLUTION_1D_, GL_RGBA_, 2, GL_RGBA_, GL_FLOAT_, green_halves.data());
    filter_2d(GL_CONVOLUTION_2D_, GL_RGBA_, 3, 3, GL_RGBA_, GL_FLOAT_, centre_tap.data());

    viewport(0, 0, size(), size());
    clear_color(0, 0, 0, 1);
    clear(GL_COLOR_BUFFER_BIT_);
    window_pos(0, 0);
    pixel_zoom(4, 4);
    enable(GL_CONVOLUTION_1D_);
    enable(GL_CONVOLUTION_2D_);
    draw_pixels(8, 8, GL_RGBA_, GL_FLOAT_, white.data());
    disable(GL_CONVOLUTION_1D_);
    disable(GL_CONVOLUTION_2D_);

    const PixelProbe probe(read_pixels);
    const auto inside = probe.At(2, 2);
    EXPECT_NEAR(inside.r, 255, 8) << "the 3x3 centre tap never ran";
    EXPECT_NEAR(inside.b, 255, 8) << "the 3x3 centre tap never ran";
    EXPECT_NEAR(inside.g, 255, 8);
    // 6 reduced columns at zoom 4 end at x = 24; the 1D filter's 7 would
    // reach x = 28.
    SFPEW_EXPECT_BLANK(probe, 24, 0, 8, 24, "the image is wider than GL_REDUCE with a 3x3 filter");
    EXPECT_EQ(get_error(), GL_NO_ERROR_);
}

} // namespace
