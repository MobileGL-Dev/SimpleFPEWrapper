// SimpleFPEWrapper - tests/gtest_imaging_convolution.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

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
using sfpew_test::LibraryTest;
using sfpew_test::PixelProbe;

constexpr GLenum GL_NO_ERROR_ = 0;
constexpr GLenum GL_INVALID_VALUE_ = 0x0501;
constexpr GLenum GL_FLOAT_ = 0x1406;
constexpr GLenum GL_RGBA_ = 0x1908;
constexpr GLbitfield GL_COLOR_BUFFER_BIT_ = 0x00004000;
constexpr GLenum GL_CONVOLUTION_1D_ = 0x8010;
constexpr GLenum GL_CONVOLUTION_2D_ = 0x8011;
constexpr GLenum GL_SEPARABLE_2D_ = 0x8012;
constexpr GLenum GL_CONVOLUTION_BORDER_MODE_ = 0x8013;
constexpr GLenum GL_CONVOLUTION_FILTER_SCALE_ = 0x8014;
constexpr GLenum GL_CONVOLUTION_FORMAT_ = 0x8017;
constexpr GLenum GL_CONVOLUTION_WIDTH_ = 0x8018;
constexpr GLenum GL_REPLICATE_BORDER_ = 0x8153;

class ImagingConvolutionLibraryTest : public LibraryTest {};

TEST_F(ImagingConvolutionLibraryTest, FiltersParametersAndSeparablePartsRoundTrip) {
    auto filter_1d =
        Get<void (*)(GLenum, GLenum, GLsizei, GLenum, GLenum, const void*)>(
            "glConvolutionFilter1D");
    auto get_filter =
        Get<void (*)(GLenum, GLenum, GLenum, void*)>("glGetConvolutionFilter");
    auto parameter_i = Get<void (*)(GLenum, GLenum, GLint)>("glConvolutionParameteri");
    auto parameter_fv =
        Get<void (*)(GLenum, GLenum, const GLfloat*)>("glConvolutionParameterfv");
    auto get_parameter =
        Get<void (*)(GLenum, GLenum, GLint*)>("glGetConvolutionParameteriv");
    auto separable = Get<void (*)(GLenum, GLenum, GLsizei, GLsizei, GLenum, GLenum,
                                  const void*, const void*)>("glSeparableFilter2D");
    auto get_separable = Get<void (*)(GLenum, GLenum, GLenum, void*, void*, void*)>(
        "glGetSeparableFilter");
    auto get_error = Get<GLenum (*)()>("glGetError");

    const std::array<GLfloat, 4> scale = {2.0f, 1.0f, 1.0f, 1.0f};
    parameter_fv(GL_CONVOLUTION_1D_, GL_CONVOLUTION_FILTER_SCALE_, scale.data());
    parameter_i(GL_CONVOLUTION_1D_, GL_CONVOLUTION_BORDER_MODE_, GL_REPLICATE_BORDER_);
    const std::array<GLfloat, 8> kernel = {0.25f, 0.5f, 0.75f, 1.0f,
                                           0.5f, 0.25f, 0.0f, 1.0f};
    filter_1d(GL_CONVOLUTION_1D_, GL_RGBA_, 2, GL_RGBA_, GL_FLOAT_, kernel.data());
    std::array<GLfloat, 8> returned{};
    get_filter(GL_CONVOLUTION_1D_, GL_RGBA_, GL_FLOAT_, returned.data());
    EXPECT_FLOAT_EQ(returned[0], 0.5f);
    EXPECT_FLOAT_EQ(returned[4], 1.0f);

    GLint value = 0;
    get_parameter(GL_CONVOLUTION_1D_, GL_CONVOLUTION_BORDER_MODE_, &value);
    EXPECT_EQ(value, static_cast<GLint>(GL_REPLICATE_BORDER_));
    get_parameter(GL_CONVOLUTION_1D_, GL_CONVOLUTION_FORMAT_, &value);
    EXPECT_EQ(value, static_cast<GLint>(GL_RGBA_));
    get_parameter(GL_CONVOLUTION_1D_, GL_CONVOLUTION_WIDTH_, &value);
    EXPECT_EQ(value, 2);

    const std::array<GLfloat, 8> row = {1, 1, 1, 1, 2, 2, 2, 2};
    const std::array<GLfloat, 12> column = {3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5};
    separable(GL_SEPARABLE_2D_, GL_RGBA_, 2, 3, GL_RGBA_, GL_FLOAT_, row.data(),
              column.data());
    std::array<GLfloat, 8> returned_row{};
    std::array<GLfloat, 12> returned_column{};
    get_separable(GL_SEPARABLE_2D_, GL_RGBA_, GL_FLOAT_, returned_row.data(),
                  returned_column.data(), nullptr);
    EXPECT_EQ(returned_row, row);
    EXPECT_EQ(returned_column, column);

    filter_1d(GL_CONVOLUTION_1D_, GL_RGBA_, 33, GL_RGBA_, GL_FLOAT_, kernel.data());
    EXPECT_EQ(get_error(), GL_INVALID_VALUE_);
    EXPECT_EQ(get_error(), GL_NO_ERROR_);
}

class ImagingConvolutionContextTest : public ContextTest {
protected:
    void SetUp() override {
        ContextTest::SetUp();
        if (::testing::Test::IsSkipped() || ::testing::Test::HasFatalFailure()) return;
        using MakeCurrentFn = EGLBoolean (*)(EGLDisplay, EGLSurface, EGLSurface, EGLContext);
        auto wrapper_make_current = Get<MakeCurrentFn>("eglMakeCurrent");
        ASSERT_TRUE(wrapper_make_current(display(), surface(), surface(), eglGetCurrentContext()));
    }
};

TEST_F(ImagingConvolutionContextTest, ReduceShrinksTheDrawPixelsImage) {
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

    const std::array<GLfloat, 8> average = {0.5f, 0.5f, 0.5f, 0.5f,
                                            0.5f, 0.5f, 0.5f, 0.5f};
    filter_2d(GL_CONVOLUTION_2D_, GL_RGBA_, 2, 1, GL_RGBA_, GL_FLOAT_, average.data());
    enable(GL_CONVOLUTION_2D_);
    viewport(0, 0, size(), size());
    clear_color(0, 0, 0, 1);
    clear(GL_COLOR_BUFFER_BIT_);
    window_pos(8, 8);
    pixel_zoom(8, 8);
    const std::array<GLfloat, 12> source = {1, 0, 0, 1, 0, 1, 0, 1, 0, 0, 1, 1};
    draw_pixels(3, 1, GL_RGBA_, GL_FLOAT_, source.data());
    disable(GL_CONVOLUTION_2D_);

    const PixelProbe probe(read_pixels);
    const auto first = probe.At(12, 12);
    const auto second = probe.At(20, 12);
    const auto beyond_reduced_width = probe.At(28, 12);
    EXPECT_NEAR(first.r, 128, 10);
    EXPECT_NEAR(first.g, 128, 10);
    EXPECT_NEAR(second.g, 128, 10);
    EXPECT_NEAR(second.b, 128, 10);
    EXPECT_LE(beyond_reduced_width.r, static_cast<GLubyte>(8));
    EXPECT_LE(beyond_reduced_width.g, static_cast<GLubyte>(8));
    EXPECT_LE(beyond_reduced_width.b, static_cast<GLubyte>(8));
    EXPECT_EQ(get_error(), GL_NO_ERROR_);
}

} // namespace
