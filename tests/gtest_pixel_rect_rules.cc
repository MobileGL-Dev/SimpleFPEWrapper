// SimpleFPEWrapper - tests/gtest_pixel_rect_rules.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// plans/17 P16/P5: two rules the pixel-rectangle commands owe the spec.
//
// P16 - a pixel rectangle is not a polygon: GL 2.1 rasterizes it straight to
// fragments, so face culling has no say in whether it appears. The wrapper
// draws it as a triangle strip, which makes it culling-sensitive unless the
// path says otherwise - and on a GLES backend glCopyPixels on the default
// framebuffer always takes that quad path (read_fbo == draw_fbo).
//
// P5 - glDrawPixels' reference page: GL_INVALID_OPERATION if type is one of
// GL_UNSIGNED_BYTE_3_3_2, GL_UNSIGNED_BYTE_2_3_3_REV, GL_UNSIGNED_SHORT_5_6_5
// or GL_UNSIGNED_SHORT_5_6_5_REV "and format is not GL_RGB" - GL_BGR is not
// exempt, unlike the four-field packed types which do accept GL_BGRA.

#include "sfpew_gtest.h"

#include <array>
#include <cstdint>

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
constexpr GLenum GL_INVALID_OPERATION_ = 0x0502;
constexpr GLbitfield GL_COLOR_BUFFER_BIT_ = 0x00004000;
constexpr GLenum GL_CULL_FACE_ = 0x0B44;
constexpr GLenum GL_FRONT_ = 0x0404;
constexpr GLenum GL_BACK_ = 0x0405;
constexpr GLenum GL_FRONT_AND_BACK_ = 0x0408;
constexpr GLenum GL_CW_ = 0x0900;
constexpr GLenum GL_CCW_ = 0x0901;
constexpr GLenum GL_COLOR_ = 0x1800;
constexpr GLenum GL_RGB_ = 0x1907;
constexpr GLenum GL_RGBA_ = 0x1908;
constexpr GLenum GL_BGR_ = 0x80E0;
constexpr GLenum GL_BGRA_ = 0x80E1;
constexpr GLenum GL_UNSIGNED_BYTE_ = 0x1401;
constexpr GLenum GL_UNSIGNED_BYTE_3_3_2_ = 0x8032;
constexpr GLenum GL_UNSIGNED_BYTE_2_3_3_REV_ = 0x8362;
constexpr GLenum GL_UNSIGNED_SHORT_5_6_5_ = 0x8363;
constexpr GLenum GL_UNSIGNED_SHORT_5_6_5_REV_ = 0x8364;
constexpr GLenum GL_UNSIGNED_SHORT_4_4_4_4_ = 0x8033;

class PixelRectRulesTest : public ContextTest {
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
        enable_ = Get<void (*)(GLenum)>("glEnable");
        cull_face_ = Get<void (*)(GLenum)>("glCullFace");
        front_face_ = Get<void (*)(GLenum)>("glFrontFace");
        window_pos_ = Get<void (*)(GLint, GLint)>("glWindowPos2i");
        pixel_zoom_ = Get<void (*)(GLfloat, GLfloat)>("glPixelZoom");
        draw_pixels_ = Get<void (*)(GLsizei, GLsizei, GLenum, GLenum, const void*)>("glDrawPixels");
        copy_pixels_ = Get<void (*)(GLint, GLint, GLsizei, GLsizei, GLenum)>("glCopyPixels");
        read_pixels_ = Get<PixelProbe::ReadPixelsFn>("glReadPixels");

        viewport_(0, 0, size(), size());
        clear_color_(0.0f, 0.0f, 0.0f, 1.0f);
        clear_(GL_COLOR_BUFFER_BIT_);
    }

    // A 4x4 all-green image drawn at four window pixels per texel.
    void DrawGreenRect(int x, int y) {
        std::array<GLubyte, 4 * 4 * 4> green{};
        for (size_t i = 0; i < green.size(); i += 4) {
            green[i + 1] = 255;
            green[i + 3] = 255;
        }
        pixel_zoom_(4.0f, 4.0f);
        window_pos_(x, y);
        draw_pixels_(4, 4, GL_RGBA_, GL_UNSIGNED_BYTE_, green.data());
    }

    int LitCount(int x, int y, int width, int height) const {
        return PixelProbe(read_pixels_).FindLit(x, y, width, height).count;
    }

    GLenum (*get_error_)() = nullptr;
    void (*viewport_)(GLint, GLint, GLsizei, GLsizei) = nullptr;
    void (*clear_color_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*clear_)(GLbitfield) = nullptr;
    void (*enable_)(GLenum) = nullptr;
    void (*cull_face_)(GLenum) = nullptr;
    void (*front_face_)(GLenum) = nullptr;
    void (*window_pos_)(GLint, GLint) = nullptr;
    void (*pixel_zoom_)(GLfloat, GLfloat) = nullptr;
    void (*draw_pixels_)(GLsizei, GLsizei, GLenum, GLenum, const void*) = nullptr;
    void (*copy_pixels_)(GLint, GLint, GLsizei, GLsizei, GLenum) = nullptr;
    PixelProbe::ReadPixelsFn read_pixels_ = nullptr;
};

TEST_F(PixelRectRulesTest, DrawPixelsSurvivesAReversedFrontFace) {
    enable_(GL_CULL_FACE_);
    cull_face_(GL_BACK_);
    front_face_(GL_CW_); // makes the drawer's quad back-facing
    DrawGreenRect(8, 8);
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
    EXPECT_EQ(LitCount(8, 8, 16, 16), 256);
}

TEST_F(PixelRectRulesTest, DrawPixelsSurvivesCullingBothFaces) {
    // The winding-independent half of the same rule: no orientation of the
    // quad can satisfy GL_FRONT_AND_BACK, so only "culling does not apply to
    // pixel rectangles" gets this drawn.
    enable_(GL_CULL_FACE_);
    cull_face_(GL_FRONT_AND_BACK_);
    front_face_(GL_CCW_);
    DrawGreenRect(8, 8);
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
    EXPECT_EQ(LitCount(8, 8, 16, 16), 256);
}

TEST_F(PixelRectRulesTest, CopyPixelsSurvivesCulling) {
    // glCopyPixels on the default framebuffer has read_fbo == draw_fbo, which
    // forces the same quad path even with no pixel transfer active.
    DrawGreenRect(8, 8);
    ASSERT_EQ(LitCount(8, 8, 16, 16), 256) << "source rectangle";

    enable_(GL_CULL_FACE_);
    cull_face_(GL_FRONT_AND_BACK_);
    pixel_zoom_(1.0f, 1.0f);
    window_pos_(32, 32);
    copy_pixels_(8, 8, 8, 8, GL_COLOR_);
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
    EXPECT_EQ(LitCount(32, 32, 8, 8), 64);
}

TEST_F(PixelRectRulesTest, ThreeFieldPackedTypesRequireExactlyRgb) {
    const std::array<GLenum, 4> three_field = {GL_UNSIGNED_BYTE_3_3_2_,
                                               GL_UNSIGNED_BYTE_2_3_3_REV_,
                                               GL_UNSIGNED_SHORT_5_6_5_,
                                               GL_UNSIGNED_SHORT_5_6_5_REV_};
    const std::array<uint32_t, 1> pixel = {0xF800F800u};
    window_pos_(8, 8);
    pixel_zoom_(1.0f, 1.0f);
    for (GLenum type : three_field) {
        draw_pixels_(1, 1, GL_BGR_, type, pixel.data());
        EXPECT_EQ(get_error_(), GL_INVALID_OPERATION_) << "GL_BGR with type " << std::hex << type;
        draw_pixels_(1, 1, GL_RGB_, type, pixel.data());
        EXPECT_EQ(get_error_(), GL_NO_ERROR_) << "GL_RGB with type " << std::hex << type;
    }
    // The four-field siblings are the contrast: those really do take GL_BGRA.
    draw_pixels_(1, 1, GL_BGRA_, GL_UNSIGNED_SHORT_4_4_4_4_, pixel.data());
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

} // namespace
