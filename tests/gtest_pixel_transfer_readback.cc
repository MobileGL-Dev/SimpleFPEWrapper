// SimpleFPEWrapper - tests/gtest_pixel_transfer_readback.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// defects-plan-3.md #1/#2: glReadPixels' ordinary (non-ARB_imaging) path and
// glCopyPixels(GL_COLOR) never applied GL 2.1 3.6.3's basic pixel transfer
// (GL_*_SCALE/GL_*_BIAS, GL_MAP_COLOR) at all - only the write direction
// (glDrawPixels) did. glCopyPixels(GL_DEPTH/GL_STENCIL)'s own transfer is
// covered in gtest_copypixels_depth.cc, next to its existing FBO/probe-quad
// fixture; this file is the color side, plus the regression that motivated
// sfpewDepthPixelTransferReadPixels taking `format`: a glReadPixels of a
// color format must ignore an unrelated GL_DEPTH_SCALE/BIAS setting.

#include "sfpew_gtest.h"

namespace {

using sfpew_test::ContextTest;
using sfpew_test::GLbitfield;
using sfpew_test::GLenum;
using sfpew_test::GLfloat;
using sfpew_test::GLint;
using sfpew_test::GLsizei;
using sfpew_test::PixelProbe;

constexpr GLenum GL_NO_ERROR_ = 0;
constexpr GLbitfield GL_COLOR_BUFFER_BIT_ = 0x00004000;
constexpr GLenum GL_RGBA_ = 0x1908;
constexpr GLenum GL_UNSIGNED_BYTE_ = 0x1401;
constexpr GLenum GL_COLOR_ = 0x1800;
constexpr GLenum GL_RED_SCALE_ = 0x0D14;
constexpr GLenum GL_RED_BIAS_ = 0x0D15;
constexpr GLenum GL_GREEN_SCALE_ = 0x0D18;
constexpr GLenum GL_GREEN_BIAS_ = 0x0D19;
constexpr GLenum GL_DEPTH_SCALE_ = 0x0D1E;
constexpr GLenum GL_DEPTH_BIAS_ = 0x0D1F;
constexpr GLenum GL_MAP_COLOR_ = 0x0D10;
constexpr GLenum GL_PIXEL_MAP_R_TO_R_ = 0x0C76;

class PixelTransferReadbackTest : public ContextTest {
protected:
    void SetUp() override {
        ContextTest::SetUp();
        if (::testing::Test::IsSkipped() || ::testing::Test::HasFatalFailure()) return;
        using MakeCurrentFn = EGLBoolean (*)(EGLDisplay, EGLSurface, EGLSurface, EGLContext);
        auto wrapper_make_current = Get<MakeCurrentFn>("eglMakeCurrent");
        ASSERT_TRUE(wrapper_make_current(display(), surface(), surface(), eglGetCurrentContext()));

        get_error_ = Get<GLenum (*)()>("glGetError");
        viewport_ = Get<void (*)(GLint, GLint, GLsizei, GLsizei)>("glViewport");
        clear_color_ = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glClearColor");
        clear_ = Get<void (*)(GLbitfield)>("glClear");
        pixel_transferf_ = Get<void (*)(GLenum, GLfloat)>("glPixelTransferf");
        pixel_mapfv_ = Get<void (*)(GLenum, GLsizei, const GLfloat*)>("glPixelMapfv");
        read_pixels_ = Get<PixelProbe::ReadPixelsFn>("glReadPixels");
        window_pos_ = Get<void (*)(GLint, GLint)>("glWindowPos2i");
        pixel_zoom_ = Get<void (*)(GLfloat, GLfloat)>("glPixelZoom");
        copy_pixels_ = Get<void (*)(GLint, GLint, GLsizei, GLsizei, GLenum)>("glCopyPixels");

        viewport_(0, 0, size(), size());
        pixel_zoom_(1.0f, 1.0f);
    }

    GLenum (*get_error_)() = nullptr;
    void (*viewport_)(GLint, GLint, GLsizei, GLsizei) = nullptr;
    void (*clear_color_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*clear_)(GLbitfield) = nullptr;
    void (*pixel_transferf_)(GLenum, GLfloat) = nullptr;
    void (*pixel_mapfv_)(GLenum, GLsizei, const GLfloat*) = nullptr;
    PixelProbe::ReadPixelsFn read_pixels_ = nullptr;
    void (*window_pos_)(GLint, GLint) = nullptr;
    void (*pixel_zoom_)(GLfloat, GLfloat) = nullptr;
    void (*copy_pixels_)(GLint, GLint, GLsizei, GLsizei, GLenum) = nullptr;
};

TEST_F(PixelTransferReadbackTest, DefaultTransferLeavesReadbackByteForByte) {
    clear_color_(0.6f, 0.4f, 0.2f, 1.0f);
    clear_(GL_COLOR_BUFFER_BIT_);
    const PixelProbe probe(read_pixels_);
    const auto pixel = probe.At(4, 4);
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
    EXPECT_NEAR(pixel.r, 153, 2); // 0.6 * 255
    EXPECT_NEAR(pixel.g, 102, 2); // 0.4 * 255
    EXPECT_NEAR(pixel.b, 51, 2);  // 0.2 * 255
}

TEST_F(PixelTransferReadbackTest, ReadPixelsAppliesScaleAndBiasPerChannel) {
    clear_color_(0.5f, 0.5f, 0.5f, 1.0f);
    clear_(GL_COLOR_BUFFER_BIT_);

    // Forces red to 1.0 regardless of the cleared value; green/blue stay at
    // their scale=1/bias=0 defaults and must be untouched.
    pixel_transferf_(GL_RED_SCALE_, 0.0f);
    pixel_transferf_(GL_RED_BIAS_, 1.0f);
    const PixelProbe probe(read_pixels_);
    const auto pixel = probe.At(4, 4);
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
    EXPECT_EQ(pixel.r, 255);
    EXPECT_NEAR(pixel.g, 128, 2);
    pixel_transferf_(GL_RED_SCALE_, 1.0f);
    pixel_transferf_(GL_RED_BIAS_, 0.0f);
}

TEST_F(PixelTransferReadbackTest, ReadPixelsAppliesMapColorAfterScaleAndBias) {
    // A hard threshold at 0.5: below it clamps to 0.0, at/above it to 1.0.
    const GLfloat table[2] = {0.0f, 1.0f};
    pixel_mapfv_(GL_PIXEL_MAP_R_TO_R_, 2, table);
    pixel_transferf_(GL_MAP_COLOR_, 1.0f);
    const PixelProbe probe(read_pixels_);

    clear_color_(0.8f, 0.0f, 0.0f, 1.0f);
    clear_(GL_COLOR_BUFFER_BIT_);
    EXPECT_EQ(probe.At(4, 4).r, 255) << "0.8 must land in the table's high half";

    clear_color_(0.2f, 0.0f, 0.0f, 1.0f);
    clear_(GL_COLOR_BUFFER_BIT_);
    EXPECT_EQ(probe.At(4, 4).r, 0) << "0.2 must land in the table's low half";

    pixel_transferf_(GL_MAP_COLOR_, 0.0f);
}

TEST_F(PixelTransferReadbackTest, ReadPixelsIgnoresUnrelatedDepthTransfer) {
    // Regression guard: sfpewDepthPixelTransferReadPixels must gate on
    // `format`, not just `type` - GL_UNSIGNED_BYTE is a legal type for both
    // GL_RGBA and GL_DEPTH_COMPONENT reads, so an app that happened to set
    // GL_DEPTH_SCALE/BIAS earlier (e.g. while reading a depth buffer
    // elsewhere) must not have it silently hijack a later color read.
    pixel_transferf_(GL_DEPTH_SCALE_, 0.0f);
    pixel_transferf_(GL_DEPTH_BIAS_, 1.0f);
    clear_color_(0.6f, 0.4f, 0.2f, 1.0f);
    clear_(GL_COLOR_BUFFER_BIT_);
    const PixelProbe probe(read_pixels_);
    const auto pixel = probe.At(4, 4);
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
    EXPECT_NEAR(pixel.r, 153, 2);
    EXPECT_NEAR(pixel.g, 102, 2);
    EXPECT_NEAR(pixel.b, 51, 2);
    pixel_transferf_(GL_DEPTH_SCALE_, 1.0f);
    pixel_transferf_(GL_DEPTH_BIAS_, 0.0f);
}

TEST_F(PixelTransferReadbackTest, CopyPixelsColorAppliesScaleAndBiasAndDefaultPathStaysRaw) {
    clear_color_(0.6f, 0.6f, 0.6f, 1.0f);
    clear_(GL_COLOR_BUFFER_BIT_);
    const PixelProbe probe(read_pixels_);

    window_pos_(32, 4);
    copy_pixels_(4, 4, 16, 16, GL_COLOR_);
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
    EXPECT_NEAR(probe.At(40, 12).g, 153, 3) << "map disabled: the fast blit path must stay untouched";

    pixel_transferf_(GL_GREEN_SCALE_, 0.0f);
    pixel_transferf_(GL_GREEN_BIAS_, 1.0f);
    window_pos_(32, 24);
    copy_pixels_(4, 4, 16, 16, GL_COLOR_);
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
    pixel_transferf_(GL_GREEN_SCALE_, 1.0f);
    pixel_transferf_(GL_GREEN_BIAS_, 0.0f);

    const auto transferred = probe.At(40, 32);
    EXPECT_EQ(transferred.g, 255);
    EXPECT_NEAR(transferred.r, 153, 3);
}

} // namespace
