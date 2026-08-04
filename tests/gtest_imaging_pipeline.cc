// SimpleFPEWrapper - tests/gtest_imaging_pipeline.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

#include "sfpew_gtest.h"

#include <array>
#include <string>

namespace {

using sfpew_test::ContextTest;
using sfpew_test::GLbitfield;
using sfpew_test::GLboolean;
using sfpew_test::GLenum;
using sfpew_test::GLfloat;
using sfpew_test::GLint;
using sfpew_test::GLsizei;
using sfpew_test::GLubyte;
using sfpew_test::PixelProbe;

constexpr GLenum GL_NO_ERROR_ = 0;
constexpr GLenum GL_FLOAT_ = 0x1406;
constexpr GLenum GL_UNSIGNED_BYTE_ = 0x1401;
constexpr GLenum GL_RGBA_ = 0x1908;
constexpr GLenum GL_EXTENSIONS_ = 0x1F03;
constexpr GLbitfield GL_PIXEL_MODE_BIT_ = 0x00000020;
constexpr GLbitfield GL_ENABLE_BIT_ = 0x00002000;
constexpr GLbitfield GL_COLOR_BUFFER_BIT_ = 0x00004000;
constexpr GLenum GL_CONVOLUTION_1D_ = 0x8010;
constexpr GLenum GL_POST_CONVOLUTION_RED_SCALE_ = 0x801C;
constexpr GLenum GL_COLOR_TABLE_ = 0x80D0;
constexpr GLenum GL_POST_CONVOLUTION_COLOR_TABLE_ = 0x80D1;

class ImagingPipelineTest : public ContextTest {
protected:
    void SetUp() override {
        ContextTest::SetUp();
        if (::testing::Test::IsSkipped() || ::testing::Test::HasFatalFailure()) return;
        using MakeCurrentFn = EGLBoolean (*)(EGLDisplay, EGLSurface, EGLSurface, EGLContext);
        auto wrapper_make_current = Get<MakeCurrentFn>("eglMakeCurrent");
        ASSERT_TRUE(wrapper_make_current(display(), surface(), surface(), eglGetCurrentContext()));
    }
};

TEST_F(ImagingPipelineTest, StagesComposeInSpecificationOrderAndAttribStacksRestoreState) {
    auto color_table = Get<void (*)(GLenum, GLenum, GLsizei, GLenum, GLenum, const void*)>(
        "glColorTable");
    auto convolution =
        Get<void (*)(GLenum, GLenum, GLsizei, GLenum, GLenum, const void*)>(
            "glConvolutionFilter1D");
    auto enable = Get<void (*)(GLenum)>("glEnable");
    auto disable = Get<void (*)(GLenum)>("glDisable");
    auto is_enabled = Get<GLboolean (*)(GLenum)>("glIsEnabled");
    auto push_attrib = Get<void (*)(GLbitfield)>("glPushAttrib");
    auto pop_attrib = Get<void (*)()>("glPopAttrib");
    auto pixel_transfer = Get<void (*)(GLenum, GLfloat)>("glPixelTransferf");
    auto get_float = Get<void (*)(GLenum, GLfloat*)>("glGetFloatv");
    auto viewport = Get<void (*)(GLint, GLint, GLsizei, GLsizei)>("glViewport");
    auto clear_color = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glClearColor");
    auto clear = Get<void (*)(GLbitfield)>("glClear");
    auto window_pos = Get<void (*)(GLint, GLint)>("glWindowPos2i");
    auto pixel_zoom = Get<void (*)(GLfloat, GLfloat)>("glPixelZoom");
    auto draw_pixels =
        Get<void (*)(GLsizei, GLsizei, GLenum, GLenum, const void*)>("glDrawPixels");
    auto read_pixels = Get<PixelProbe::ReadPixelsFn>("glReadPixels");
    auto get_string = Get<const GLubyte* (*)(GLenum)>("glGetString");
    auto get_error = Get<GLenum (*)()>("glGetError");

    disable(GL_COLOR_TABLE_);
    push_attrib(GL_ENABLE_BIT_);
    enable(GL_COLOR_TABLE_);
    pop_attrib();
    EXPECT_EQ(is_enabled(GL_COLOR_TABLE_), static_cast<GLboolean>(0));

    pixel_transfer(GL_POST_CONVOLUTION_RED_SCALE_, 0.25f);
    push_attrib(GL_PIXEL_MODE_BIT_);
    pixel_transfer(GL_POST_CONVOLUTION_RED_SCALE_, 0.75f);
    pop_attrib();
    GLfloat restored_scale = 0.0f;
    get_float(GL_POST_CONVOLUTION_RED_SCALE_, &restored_scale);
    EXPECT_FLOAT_EQ(restored_scale, 0.25f);
    pixel_transfer(GL_POST_CONVOLUTION_RED_SCALE_, 1.0f);

    // First table inverts RGB. After the two-tap average, the post table maps
    // its high half to 0.25. Applying convolution before inversion would make
    // the first output black, so the probe distinguishes the stage order.
    const std::array<GLfloat, 8> invert = {1, 1, 1, 0, 0, 0, 0, 1};
    const std::array<GLfloat, 8> quarter = {0, 0, 0, 0, 0.25f, 0.25f, 0.25f, 1};
    const std::array<GLfloat, 8> average = {0.5f, 0.5f, 0.5f, 0.5f,
                                            0.5f, 0.5f, 0.5f, 0.5f};
    color_table(GL_COLOR_TABLE_, GL_RGBA_, 2, GL_RGBA_, GL_FLOAT_, invert.data());
    color_table(GL_POST_CONVOLUTION_COLOR_TABLE_, GL_RGBA_, 2, GL_RGBA_, GL_FLOAT_,
                quarter.data());
    convolution(GL_CONVOLUTION_1D_, GL_RGBA_, 2, GL_RGBA_, GL_FLOAT_, average.data());
    enable(GL_COLOR_TABLE_);
    enable(GL_CONVOLUTION_1D_);
    enable(GL_POST_CONVOLUTION_COLOR_TABLE_);

    viewport(0, 0, size(), size());
    clear_color(0, 0, 0, 1);
    clear(GL_COLOR_BUFFER_BIT_);
    window_pos(8, 8);
    pixel_zoom(8, 8);
    const std::array<GLfloat, 12> source = {0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1};
    draw_pixels(3, 1, GL_RGBA_, GL_FLOAT_, source.data());
    disable(GL_COLOR_TABLE_);
    disable(GL_CONVOLUTION_1D_);
    disable(GL_POST_CONVOLUTION_COLOR_TABLE_);

    const PixelProbe probe(read_pixels);
    const auto first = probe.At(12, 12);
    const auto second = probe.At(20, 12);
    EXPECT_NEAR(first.r, 64, 10);
    EXPECT_NEAR(first.g, 64, 10);
    EXPECT_NEAR(first.b, 64, 10);
    EXPECT_LE(second.r, static_cast<GLubyte>(8));
    EXPECT_LE(second.g, static_cast<GLubyte>(8));
    EXPECT_LE(second.b, static_cast<GLubyte>(8));

    const GLubyte* extensions = get_string(GL_EXTENSIONS_);
    ASSERT_NE(extensions, nullptr);
    EXPECT_NE(std::string(reinterpret_cast<const char*>(extensions)).find("GL_ARB_imaging"),
              std::string::npos);
    EXPECT_EQ(get_error(), GL_NO_ERROR_);
}

TEST_F(ImagingPipelineTest, DisabledPipelinePreservesTheExistingDrawPixelsPath) {
    auto viewport = Get<void (*)(GLint, GLint, GLsizei, GLsizei)>("glViewport");
    auto clear_color = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glClearColor");
    auto clear = Get<void (*)(GLbitfield)>("glClear");
    auto window_pos = Get<void (*)(GLint, GLint)>("glWindowPos2i");
    auto pixel_zoom = Get<void (*)(GLfloat, GLfloat)>("glPixelZoom");
    auto draw_pixels =
        Get<void (*)(GLsizei, GLsizei, GLenum, GLenum, const void*)>("glDrawPixels");
    auto read_pixels = Get<PixelProbe::ReadPixelsFn>("glReadPixels");

    viewport(0, 0, size(), size());
    clear_color(0, 0, 0, 1);
    clear(GL_COLOR_BUFFER_BIT_);
    window_pos(8, 8);
    pixel_zoom(8, 8);
    const std::array<GLubyte, 8> source = {255, 0, 0, 255, 0, 255, 0, 255};
    draw_pixels(2, 1, GL_RGBA_, GL_UNSIGNED_BYTE_, source.data());

    const PixelProbe probe(read_pixels);
    const auto red = probe.At(12, 12);
    const auto green = probe.At(20, 12);
    EXPECT_EQ(red.r, 255);
    EXPECT_EQ(red.g, 0);
    EXPECT_EQ(green.r, 0);
    EXPECT_EQ(green.g, 255);
}

} // namespace
