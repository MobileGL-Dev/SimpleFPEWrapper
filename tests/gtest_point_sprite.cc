// SimpleFPEWrapper - tests/gtest_point_sprite.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// defects-plan-2.md 2.2/2.3/2.4: GL_POINT_SPRITE/GL_COORD_REPLACE had no
// gl_PointCoord consumer at all (a point rendered with a flat/absent
// texcoord regardless), GL_POINT_FADE_THRESHOLD_SIZE was stored but never
// touched fragment alpha, and GL_POINT_SMOOTH was hardcoded disabled with
// no edge falloff. All three only affect fragments the uber-shader can
// identify as belonging to a GL_POINTS draw at runtime (IsPointPrimitive
// in fpe_shadergen.cpp), since the program is not primitive-keyed - every
// test here draws a lone large point so its footprint is easy to sample at
// specific offsets from center.

#include "sfpew_gtest.h"

#include <optional>

namespace {

using sfpew_test::ContextTest;
using sfpew_test::GLbitfield;
using sfpew_test::GLboolean;
using sfpew_test::GLenum;
using sfpew_test::GLfloat;
using sfpew_test::GLint;
using sfpew_test::GLsizei;
using sfpew_test::GLubyte;
using sfpew_test::GLuint;
using sfpew_test::PixelProbe;

constexpr GLenum GL_NO_ERROR_ = 0;
constexpr GLenum GL_POINT_SPRITE_ = 0x8861;
constexpr GLenum GL_COORD_REPLACE_ = 0x8862;
constexpr GLenum GL_POINT_SMOOTH_ = 0x0B10;
constexpr GLenum GL_POINT_SPRITE_COORD_ORIGIN_ = 0x8CA0;
constexpr GLenum GL_LOWER_LEFT_ = 0x8CA1;
constexpr GLenum GL_UPPER_LEFT_ = 0x8CA2;
constexpr GLenum GL_POINT_DISTANCE_ATTENUATION_ = 0x8129;
constexpr GLenum GL_POINT_FADE_THRESHOLD_SIZE_ = 0x8128;
constexpr GLenum GL_POINTS_ = 0x0000;
constexpr GLenum GL_PROJECTION_ = 0x1701;
constexpr GLenum GL_MODELVIEW_ = 0x1700;
constexpr GLenum GL_TEXTURE_2D_ = 0x0DE1;
constexpr GLenum GL_TEXTURE_MIN_FILTER_ = 0x2801;
constexpr GLenum GL_TEXTURE_MAG_FILTER_ = 0x2800;
constexpr GLenum GL_NEAREST_ = 0x2600;
constexpr GLenum GL_RGBA_ = 0x1908;
constexpr GLenum GL_RGBA8_ = 0x8058;
constexpr GLenum GL_UNSIGNED_BYTE_ = 0x1401;
constexpr GLenum GL_BLEND_ = 0x0BE2;
constexpr GLenum GL_SRC_ALPHA_ = 0x0302;
constexpr GLenum GL_ONE_MINUS_SRC_ALPHA_ = 0x0303;
constexpr GLbitfield GL_COLOR_BUFFER_BIT_ = 0x00004000;

constexpr int kSize = 64;
constexpr GLfloat kPointSize = 24.0f; // radius 12px, comfortably inside a 64px viewport

class PointSpriteTest : public ContextTest {
protected:
    PointSpriteTest() : ContextTest(sfpew_test::Backend::GLES3, kSize) {}

    void SetUp() override {
        ContextTest::SetUp();
        if (::testing::Test::IsSkipped()) return;
        enable_ = Get<void (*)(GLenum)>("glEnable");
        disable_ = Get<void (*)(GLenum)>("glDisable");
        is_enabled_ = Get<GLboolean (*)(GLenum)>("glIsEnabled");
        tex_envi_ = Get<void (*)(GLenum, GLenum, GLint)>("glTexEnvi");
        point_parameterfv_ = Get<void (*)(GLenum, const GLfloat*)>("glPointParameterfv");
        point_size_ = Get<void (*)(GLfloat)>("glPointSize");
        viewport_ = Get<void (*)(GLint, GLint, GLsizei, GLsizei)>("glViewport");
        matrix_mode_ = Get<void (*)(GLenum)>("glMatrixMode");
        load_identity_ = Get<void (*)()>("glLoadIdentity");
        clear_color_ = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glClearColor");
        clear_ = Get<void (*)(GLbitfield)>("glClear");
        begin_ = Get<void (*)(GLenum)>("glBegin");
        end_ = Get<void (*)()>("glEnd");
        color4f_ = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glColor4f");
        vertex3f_ = Get<void (*)(GLfloat, GLfloat, GLfloat)>("glVertex3f");
        get_error_ = Get<GLenum (*)()>("glGetError");
        gen_textures_ = Get<void (*)(GLsizei, GLuint*)>("glGenTextures");
        bind_texture_ = Get<void (*)(GLenum, GLuint)>("glBindTexture");
        tex_parameteri_ = Get<void (*)(GLenum, GLenum, GLint)>("glTexParameteri");
        tex_image2d_ = Get<void (*)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum,
                                    const void*)>("glTexImage2D");
        auto read_pixels =
            Get<void (*)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*)>("glReadPixels");
        ASSERT_NE(read_pixels, nullptr);
        probe_.emplace(read_pixels);

        viewport_(0, 0, kSize, kSize);
        matrix_mode_(GL_PROJECTION_);
        load_identity_();
        matrix_mode_(GL_MODELVIEW_);
        load_identity_();
        point_size_(kPointSize);
        clear_color_(0.0f, 0.0f, 0.0f, 1.0f);
    }

    GLuint UploadTexture(GLsizei width, GLsizei height, const GLubyte* rgba) {
        GLuint texture = 0;
        gen_textures_(1, &texture);
        bind_texture_(GL_TEXTURE_2D_, texture);
        tex_parameteri_(GL_TEXTURE_2D_, GL_TEXTURE_MIN_FILTER_, GL_NEAREST_);
        tex_parameteri_(GL_TEXTURE_2D_, GL_TEXTURE_MAG_FILTER_, GL_NEAREST_);
        tex_image2d_(GL_TEXTURE_2D_, 0, GL_RGBA8_, width, height, 0, GL_RGBA_, GL_UNSIGNED_BYTE_,
                    rgba);
        return texture;
    }

    void DrawPointAtOrigin() {
        clear_(GL_COLOR_BUFFER_BIT_);
        begin_(GL_POINTS_);
        color4f_(1.0f, 1.0f, 1.0f, 1.0f);
        vertex3f_(0.0f, 0.0f, 0.0f);
        end_();
    }

    void (*enable_)(GLenum) = nullptr;
    void (*disable_)(GLenum) = nullptr;
    GLboolean (*is_enabled_)(GLenum) = nullptr;
    void (*tex_envi_)(GLenum, GLenum, GLint) = nullptr;
    void (*point_parameterfv_)(GLenum, const GLfloat*) = nullptr;
    void (*point_size_)(GLfloat) = nullptr;
    void (*viewport_)(GLint, GLint, GLsizei, GLsizei) = nullptr;
    void (*matrix_mode_)(GLenum) = nullptr;
    void (*load_identity_)() = nullptr;
    void (*clear_color_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*clear_)(GLbitfield) = nullptr;
    void (*begin_)(GLenum) = nullptr;
    void (*end_)() = nullptr;
    void (*color4f_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*vertex3f_)(GLfloat, GLfloat, GLfloat) = nullptr;
    GLenum (*get_error_)() = nullptr;
    void (*gen_textures_)(GLsizei, GLuint*) = nullptr;
    void (*bind_texture_)(GLenum, GLuint) = nullptr;
    void (*tex_parameteri_)(GLenum, GLenum, GLint) = nullptr;
    void (*tex_image2d_)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum,
                         const void*) = nullptr;
    std::optional<PixelProbe> probe_;
};

TEST_F(PointSpriteTest, IsEnabledRoundTrips) {
    EXPECT_EQ(is_enabled_(GL_POINT_SPRITE_), sfpew_test::GL_FALSE_);
    enable_(GL_POINT_SPRITE_);
    EXPECT_EQ(is_enabled_(GL_POINT_SPRITE_), sfpew_test::GL_TRUE_);
    disable_(GL_POINT_SPRITE_);
    EXPECT_EQ(is_enabled_(GL_POINT_SPRITE_), sfpew_test::GL_FALSE_);
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

TEST_F(PointSpriteTest, CoordReplaceSamplesVaryAcrossThePointFootprintOnTheSAxis) {
    // 2x1: left texel red, right texel green. Only the S axis matters here,
    // sidestepping GL_POINT_SPRITE_COORD_ORIGIN (which only affects T) -
    // that gets its own dedicated test below.
    const GLubyte texels[2 * 4] = {255, 0, 0, 255, 0, 255, 0, 255};
    UploadTexture(2, 1, texels);
    tex_envi_(GL_POINT_SPRITE_, GL_COORD_REPLACE_, 1);
    enable_(GL_TEXTURE_2D_);
    enable_(GL_POINT_SPRITE_);

    DrawPointAtOrigin();
    const int cx = kSize / 2, cy = kSize / 2;
    const auto left = probe_->At(cx - 8, cy);
    const auto right = probe_->At(cx + 8, cy);
    EXPECT_GT(left.r, 150) << "left side of the point must sample the red (s<0.5) texel";
    EXPECT_LT(left.g, 100);
    EXPECT_LT(right.r, 100) << "right side of the point must sample the green (s>0.5) texel";
    EXPECT_GT(right.g, 150);
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

TEST_F(PointSpriteTest, CoordReplaceDisabledLeavesTheFlatTexcoordUnaffected) {
    const GLubyte texels[2 * 4] = {255, 0, 0, 255, 0, 255, 0, 255};
    UploadTexture(2, 1, texels);
    // GL_COORD_REPLACE left at its default (off).
    enable_(GL_TEXTURE_2D_);
    enable_(GL_POINT_SPRITE_);

    DrawPointAtOrigin();
    const int cx = kSize / 2, cy = kSize / 2;
    const auto left = probe_->At(cx - 8, cy);
    const auto right = probe_->At(cx + 8, cy);
    EXPECT_EQ(left.r, right.r) << "without GL_COORD_REPLACE the whole point samples one texcoord";
    EXPECT_EQ(left.g, right.g);
}

TEST_F(PointSpriteTest, CoordOriginFlipsWhichRowIsAtTheTop) {
    // 1x2: row 0 (the default GL_UNPACK origin - v=0, "bottom" of the
    // texture image) is red, row 1 is green.
    const GLubyte texels[2 * 4] = {255, 0, 0, 255, 0, 255, 0, 255};
    UploadTexture(1, 2, texels);
    tex_envi_(GL_POINT_SPRITE_, GL_COORD_REPLACE_, 1);
    enable_(GL_TEXTURE_2D_);
    enable_(GL_POINT_SPRITE_);
    const int cx = kSize / 2, cy = kSize / 2;
    const int near_top = cy + 8; // high window y = visually near the top of the point

    DrawPointAtOrigin(); // default origin: GL_UPPER_LEFT
    const auto top_default = probe_->At(cx, near_top);
    EXPECT_GT(top_default.r, 150)
        << "GLSL ES's gl_PointCoord is upper-left-origin, matching GL's own "
           "GL_UPPER_LEFT default with no flip needed: t=0 (row 0, red) at the top";
    EXPECT_LT(top_default.g, 100);

    const GLfloat lower_left = static_cast<GLfloat>(GL_LOWER_LEFT_);
    point_parameterfv_(GL_POINT_SPRITE_COORD_ORIGIN_, &lower_left);
    DrawPointAtOrigin();
    const auto top_lower_left = probe_->At(cx, near_top);
    EXPECT_LT(top_lower_left.r, 100)
        << "GL_LOWER_LEFT flips t so the top of the point now reads t=1 (row 1, green)";
    EXPECT_GT(top_lower_left.g, 150);

    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

TEST_F(PointSpriteTest, SmoothFadesTheCornerButNotTheCenter) {
    enable_(GL_BLEND_);
    auto blend_func = Get<void (*)(GLenum, GLenum)>("glBlendFunc");
    blend_func(GL_SRC_ALPHA_, GL_ONE_MINUS_SRC_ALPHA_);
    enable_(GL_POINT_SMOOTH_);

    DrawPointAtOrigin(); // white point over a black clear
    const int cx = kSize / 2, cy = kSize / 2;
    const auto center = probe_->At(cx, cy);
    // The corner of the rasterized (square) point footprint: outside the
    // inscribed circle, where the radial falloff (fpe_shadergen.cpp) drives
    // alpha to (near) zero.
    // A few pixels back from the true edge (kPointSize/2 = 12px out) for
    // rasterization-boundary safety margin - well past 70% of the radius is
    // already deep in smoothstep's saturated r>=1.0 region regardless.
    const int corner_offset = static_cast<int>(kPointSize / 2.0f) - 3;
    const auto corner = probe_->At(cx + corner_offset, cy + corner_offset);
    EXPECT_GT(center.r, 200) << "point center keeps full alpha - opaque white over black";
    EXPECT_LT(corner.r, 100) << "smoothed corner blends mostly toward the black background";
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

TEST_F(PointSpriteTest, SmoothDisabledLeavesTheCornerOpaque) {
    enable_(GL_BLEND_);
    auto blend_func = Get<void (*)(GLenum, GLenum)>("glBlendFunc");
    blend_func(GL_SRC_ALPHA_, GL_ONE_MINUS_SRC_ALPHA_);
    // GL_POINT_SMOOTH left at its default (off).

    DrawPointAtOrigin();
    const int cx = kSize / 2, cy = kSize / 2;
    // A few pixels back from the true edge (kPointSize/2 = 12px out) for
    // rasterization-boundary safety margin - well past 70% of the radius is
    // already deep in smoothstep's saturated r>=1.0 region regardless.
    const int corner_offset = static_cast<int>(kPointSize / 2.0f) - 3;
    const auto corner = probe_->At(cx + corner_offset, cy + corner_offset);
    EXPECT_GT(corner.r, 200) << "without smoothing the whole square footprint stays opaque";
}

TEST_F(PointSpriteTest, FadeThresholdDimsPointsBelowThresholdSize) {
    // SetUp's identity projection leaves clip.z == eye.z with clip.w == 1,
    // so anything past eye z=-1 falls outside the [-1,1] NDC range and gets
    // near/far-clipped - fine for every other test here (they all draw at
    // z=0), but this one needs eye-space distances out to 6.0. Give this
    // test its own orthographic projection (x/y scale is untouched by
    // near/far, so NDC.xy stays 0 for a vertex at object-space (0,0,z)
    // regardless of z, matching every other test's window-center sampling).
    auto ortho = Get<void (*)(sfpew_test::GLdouble, sfpew_test::GLdouble, sfpew_test::GLdouble,
                              sfpew_test::GLdouble, sfpew_test::GLdouble, sfpew_test::GLdouble)>(
        "glOrtho");
    matrix_mode_(GL_PROJECTION_);
    ortho(-1.0, 1.0, -1.0, 1.0, 0.1, 20.0);
    matrix_mode_(GL_MODELVIEW_);

    // Quadratic distance attenuation: derived size shrinks with eye-space
    // distance (spec 3.3), same setup as gtest_point_parameters.cc's
    // DistanceAttenuationChangesRenderedPointArea.
    const GLfloat attenuation[3] = {0.0f, 0.0f, 1.0f};
    point_parameterfv_(GL_POINT_DISTANCE_ATTENUATION_, attenuation);
    const GLfloat threshold = 8.0f;
    point_parameterfv_(GL_POINT_FADE_THRESHOLD_SIZE_, &threshold);
    enable_(GL_BLEND_);
    auto blend_func = Get<void (*)(GLenum, GLenum)>("glBlendFunc");
    blend_func(GL_SRC_ALPHA_, GL_ONE_MINUS_SRC_ALPHA_);

    const auto draw_at = [&](GLfloat distance) {
        clear_(GL_COLOR_BUFFER_BIT_);
        begin_(GL_POINTS_);
        color4f_(1.0f, 1.0f, 1.0f, 1.0f);
        vertex3f_(0.0f, 0.0f, -distance);
        end_();
        return probe_->At(kSize / 2, kSize / 2).r;
    };

    // derivedSize = kPointSize / distance (quadratic attenuation, spec 3.3).
    // Close: distance 1.5 -> derived 16px, well above the 8px threshold -
    // fully lit, no fade. Both cases keep the rasterized point several
    // pixels wide (not shrunk to a near-invisible dot), so sampling dead
    // center reliably lands inside it either way.
    const auto near_red = draw_at(1.5f);
    // Far: distance 6.0 -> derived 4px, half the threshold - alpha scales
    // by (4/8)^2 = 0.25, faded toward the black background but still a
    // real, sampleable few-pixel point.
    const auto far_red = draw_at(6.0f);

    EXPECT_GT(near_red, 200) << "above the fade threshold: full alpha";
    EXPECT_LT(far_red, 150) << "below the fade threshold: alpha scales down by (size/threshold)^2";
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

} // namespace
