// SimpleFPEWrapper - tests/gtest_imaging_base_format.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// plans/17 P9/P11/P5: what a color table's or a convolution filter's BASE
// INTERNAL FORMAT means for the channels it does not carry. GL 2.1 3.6.3
// (glColorTable's "Resulting Texture Components" table, and the matching
// one for convolution) passes those channels through the stage untouched;
// this file pins each arm that has a channel to pass through, plus the
// format/type pairing rule the same file's validator enforces.
//
// Every probe colour here is symmetric in R and B, or is judged on alpha,
// so the llvmpipe BGRA readback swizzle cannot decide the outcome.

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
constexpr GLenum GL_INVALID_OPERATION_ = 0x0502;
constexpr GLenum GL_UNSIGNED_BYTE_3_3_2_ = 0x8032;
constexpr GLenum GL_UNSIGNED_SHORT_4_4_4_4_ = 0x8033;
constexpr GLenum GL_UNSIGNED_SHORT_5_6_5_ = 0x8363;
constexpr GLenum GL_FLOAT_ = 0x1406;
constexpr GLenum GL_ALPHA_ = 0x1906;
constexpr GLenum GL_RGB_ = 0x1907;
constexpr GLenum GL_RGBA_ = 0x1908;
constexpr GLenum GL_LUMINANCE_ = 0x1909;
constexpr GLenum GL_BGR_ = 0x80E0;
constexpr GLenum GL_BGRA_ = 0x80E1;
constexpr GLbitfield GL_COLOR_BUFFER_BIT_ = 0x00004000;
constexpr GLenum GL_CONVOLUTION_2D_ = 0x8011;
constexpr GLenum GL_CONVOLUTION_BORDER_MODE_ = 0x8013;
constexpr GLenum GL_REDUCE_ = 0x8016;
constexpr GLenum GL_COLOR_TABLE_ = 0x80D0;
constexpr GLenum GL_REPLICATE_BORDER_ = 0x8153;

class ImagingBaseFormatTest : public ContextTest {
protected:
    void SetUp() override {
        ContextTest::SetUp();
        if (::testing::Test::IsSkipped() || ::testing::Test::HasFatalFailure()) return;
        using MakeCurrentFn = EGLBoolean (*)(EGLDisplay, EGLSurface, EGLSurface, EGLContext);
        auto wrapper_make_current = Get<MakeCurrentFn>("eglMakeCurrent");
        ASSERT_TRUE(wrapper_make_current(display(), surface(), surface(), eglGetCurrentContext()));
    }
};

TEST_F(ImagingBaseFormatTest, ColorTableReplacesOnlyTheChannelsItsBaseFormatDefines) {
    auto color_table =
        Get<void (*)(GLenum, GLenum, GLsizei, GLenum, GLenum, const void*)>("glColorTable");
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

    viewport(0, 0, size(), size());
    clear_color(0, 0, 0, 1);
    window_pos(8, 8);
    pixel_zoom(8, 8);
    const PixelProbe probe(read_pixels);

    // GL_ALPHA yields (R, G, B, A[a]): only alpha is looked up.
    const std::array<GLfloat, 8> alpha_quarter = {0, 0, 0, 0.25f, 0, 0, 0, 0.25f};
    const std::array<GLfloat, 4> magenta = {1, 0, 1, 1};
    color_table(GL_COLOR_TABLE_, GL_ALPHA_, 2, GL_RGBA_, GL_FLOAT_, alpha_quarter.data());
    clear(GL_COLOR_BUFFER_BIT_);
    enable(GL_COLOR_TABLE_);
    draw_pixels(1, 1, GL_RGBA_, GL_FLOAT_, magenta.data());
    disable(GL_COLOR_TABLE_);
    const auto through_alpha_table = probe.At(12, 12);
    EXPECT_NEAR(through_alpha_table.r, 255, 8) << "an ALPHA table must not touch red";
    EXPECT_NEAR(through_alpha_table.b, 255, 8) << "an ALPHA table must not touch blue";
    EXPECT_LE(through_alpha_table.g, static_cast<GLubyte>(8));
    EXPECT_NEAR(through_alpha_table.a, 64, 8) << "an ALPHA table must look alpha up";

    // GL_LUMINANCE yields (L[r], L[g], L[b], A): alpha passes through.
    const std::array<GLfloat, 8> luminance_half = {0.5f, 0, 0, 0, 0.5f, 0, 0, 0};
    const std::array<GLfloat, 4> quarter_alpha_white = {1, 1, 1, 0.25f};
    color_table(GL_COLOR_TABLE_, GL_LUMINANCE_, 2, GL_RGBA_, GL_FLOAT_, luminance_half.data());
    clear(GL_COLOR_BUFFER_BIT_);
    enable(GL_COLOR_TABLE_);
    draw_pixels(1, 1, GL_RGBA_, GL_FLOAT_, quarter_alpha_white.data());
    disable(GL_COLOR_TABLE_);
    const auto through_luminance_table = probe.At(12, 12);
    EXPECT_NEAR(through_luminance_table.r, 128, 8);
    EXPECT_NEAR(through_luminance_table.b, 128, 8);
    EXPECT_NEAR(through_luminance_table.a, 64, 8) << "a LUMINANCE table must not touch alpha";

    // GL_RGB yields (R[r], G[g], B[b], A): alpha passes through here too.
    const std::array<GLfloat, 8> rgb_quarter = {0.25f, 0.25f, 0.25f, 0,
                                                0.25f, 0.25f, 0.25f, 0};
    const std::array<GLfloat, 4> half_alpha_white = {1, 1, 1, 0.5f};
    color_table(GL_COLOR_TABLE_, GL_RGB_, 2, GL_RGBA_, GL_FLOAT_, rgb_quarter.data());
    clear(GL_COLOR_BUFFER_BIT_);
    enable(GL_COLOR_TABLE_);
    draw_pixels(1, 1, GL_RGBA_, GL_FLOAT_, half_alpha_white.data());
    disable(GL_COLOR_TABLE_);
    const auto through_rgb_table = probe.At(12, 12);
    EXPECT_NEAR(through_rgb_table.r, 64, 8);
    EXPECT_NEAR(through_rgb_table.b, 64, 8);
    EXPECT_NEAR(through_rgb_table.a, 128, 8) << "an RGB table must not touch alpha";

    EXPECT_EQ(get_error(), GL_NO_ERROR_);
}

TEST_F(ImagingBaseFormatTest, LuminanceConvolutionFilterLeavesAlphaUnfiltered) {
    auto filter_2d =
        Get<void (*)(GLenum, GLenum, GLsizei, GLsizei, GLenum, GLenum, const void*)>(
            "glConvolutionFilter2D");
    auto parameter_i = Get<void (*)(GLenum, GLenum, GLint)>("glConvolutionParameteri");
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

    // A 3x3 box blur whose coefficients live in the luminance channel only.
    // Nine taps of 1/9 leave a flat source flat, so RGB is unchanged either
    // way - alpha is the whole measurement, and a filter that convolved it
    // with the 1.0 filler would sum 9 x 0.2 x 1.0 and clamp to opaque.
    std::array<GLfloat, 36> ninths{};
    for (int tap = 0; tap < 9; ++tap) ninths[static_cast<size_t>(tap) * 4u] = 1.0f / 9.0f;
    filter_2d(GL_CONVOLUTION_2D_, GL_LUMINANCE_, 3, 3, GL_RGBA_, GL_FLOAT_, ninths.data());
    parameter_i(GL_CONVOLUTION_2D_, GL_CONVOLUTION_BORDER_MODE_,
                static_cast<GLint>(GL_REPLICATE_BORDER_));

    std::array<GLfloat, 36> source{};
    for (int pixel = 0; pixel < 9; ++pixel) {
        source[static_cast<size_t>(pixel) * 4u + 0] = 0.5f;
        source[static_cast<size_t>(pixel) * 4u + 1] = 0.5f;
        source[static_cast<size_t>(pixel) * 4u + 2] = 0.5f;
        source[static_cast<size_t>(pixel) * 4u + 3] = 0.2f;
    }

    viewport(0, 0, size(), size());
    clear_color(0, 0, 0, 1);
    clear(GL_COLOR_BUFFER_BIT_);
    window_pos(8, 8);
    pixel_zoom(8, 8);
    enable(GL_CONVOLUTION_2D_);
    draw_pixels(3, 3, GL_RGBA_, GL_FLOAT_, source.data());
    disable(GL_CONVOLUTION_2D_);

    const PixelProbe probe(read_pixels);
    const auto filtered = probe.At(12, 12);
    EXPECT_NEAR(filtered.r, 128, 8);
    EXPECT_NEAR(filtered.b, 128, 8);
    EXPECT_NEAR(filtered.a, 51, 8) << "a LUMINANCE filter must not filter alpha";
    EXPECT_EQ(get_error(), GL_NO_ERROR_);
}

// Which source pixel an unfiltered channel comes from once GL_REDUCE has
// shrunk the image. Three distinct source alphas and a filter wide enough to
// collapse them to one output pixel make that pixel's alpha name its origin:
// 0.20 is the leftmost source pixel (the footprint's ORIGIN, which is what
// the desktop driver passes through), 0.50 would be its centre, and an alpha
// that was filtered along with luminance saturates to opaque.
TEST_F(ImagingBaseFormatTest, ConvolutionUnderReduceKeepsTheOriginPixelsUnfilteredChannels) {
    auto filter_2d =
        Get<void (*)(GLenum, GLenum, GLsizei, GLsizei, GLenum, GLenum, const void*)>(
            "glConvolutionFilter2D");
    auto parameter_i = Get<void (*)(GLenum, GLenum, GLint)>("glConvolutionParameteri");
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

    const GLfloat third = 1.0f / 3.0f;
    const std::array<GLfloat, 12> thirds = {third, 0, 0, 0, third, 0, 0, 0, third, 0, 0, 0};
    const std::array<GLfloat, 12> graded_alpha = {0.5f, 0.5f, 0.5f, 0.20f,
                                                  0.5f, 0.5f, 0.5f, 0.50f,
                                                  0.5f, 0.5f, 0.5f, 0.80f};
    filter_2d(GL_CONVOLUTION_2D_, GL_LUMINANCE_, 3, 1, GL_RGBA_, GL_FLOAT_, thirds.data());
    parameter_i(GL_CONVOLUTION_2D_, GL_CONVOLUTION_BORDER_MODE_, static_cast<GLint>(GL_REDUCE_));

    viewport(0, 0, size(), size());
    clear_color(0, 0, 0, 1);
    clear(GL_COLOR_BUFFER_BIT_);
    window_pos(8, 8);
    pixel_zoom(8, 8);
    enable(GL_CONVOLUTION_2D_);
    draw_pixels(3, 1, GL_RGBA_, GL_FLOAT_, graded_alpha.data());
    disable(GL_CONVOLUTION_2D_);

    const PixelProbe probe(read_pixels);
    const auto reduced = probe.At(12, 12);
    EXPECT_NEAR(reduced.r, 128, 8);
    EXPECT_NEAR(reduced.b, 128, 8);
    EXPECT_NEAR(reduced.a, 51, 8) << "alpha must pass through from the leftmost source pixel";
    EXPECT_EQ(get_error(), GL_NO_ERROR_);
}

class ImagingBaseFormatLibraryTest : public LibraryTest {};

TEST_F(ImagingBaseFormatLibraryTest, ThreeFieldPackedTypesRequireExactlyRgb) {
    auto color_table =
        Get<void (*)(GLenum, GLenum, GLsizei, GLenum, GLenum, const void*)>("glColorTable");
    auto get_error = Get<GLenum (*)()>("glGetError");

    const std::array<GLubyte, 8> bytes{};
    // GL 2.1 3.6.4: a three-field packed type pairs with GL_RGB and nothing
    // else - GL_BGR has no defined field order for these, unlike the
    // four-field types where GL_BGRA is legal.
    color_table(GL_COLOR_TABLE_, GL_RGBA_, 2, GL_BGR_, GL_UNSIGNED_SHORT_5_6_5_, bytes.data());
    EXPECT_EQ(get_error(), GL_INVALID_OPERATION_) << "GL_BGR + GL_UNSIGNED_SHORT_5_6_5";
    color_table(GL_COLOR_TABLE_, GL_RGBA_, 2, GL_BGR_, GL_UNSIGNED_BYTE_3_3_2_, bytes.data());
    EXPECT_EQ(get_error(), GL_INVALID_OPERATION_) << "GL_BGR + GL_UNSIGNED_BYTE_3_3_2";
    color_table(GL_COLOR_TABLE_, GL_RGBA_, 2, GL_RGB_, GL_UNSIGNED_SHORT_4_4_4_4_, bytes.data());
    EXPECT_EQ(get_error(), GL_INVALID_OPERATION_) << "GL_RGB + GL_UNSIGNED_SHORT_4_4_4_4";

    color_table(GL_COLOR_TABLE_, GL_RGBA_, 2, GL_RGB_, GL_UNSIGNED_SHORT_5_6_5_, bytes.data());
    EXPECT_EQ(get_error(), GL_NO_ERROR_) << "GL_RGB + GL_UNSIGNED_SHORT_5_6_5 is legal";
    color_table(GL_COLOR_TABLE_, GL_RGBA_, 2, GL_BGRA_, GL_UNSIGNED_SHORT_4_4_4_4_, bytes.data());
    EXPECT_EQ(get_error(), GL_NO_ERROR_) << "GL_BGRA + GL_UNSIGNED_SHORT_4_4_4_4 is legal";
}

} // namespace
