// SimpleFPEWrapper - tests/gtest_texture_legacy_swizzle.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// GL_ALPHA/GL_LUMINANCE/GL_LUMINANCE_ALPHA/GL_INTENSITY have no GLES3
// storage, so the wrapper uploads them as R8/RG8 plus a
// GL_TEXTURE_SWIZZLE_RGBA that reproduces the fixed-pipeline sampling. The
// swizzle lives on the TEXTURE OBJECT, not on the level it was uploaded
// with: re-specifying the same object with a format that needs no swizzle
// has to take it back off again (plans/17 P17), while a swizzle the app set
// for itself must survive - image specification never touches texture
// parameter state in GL.
//
// Every judgement here is made on the green channel: llvmpipe reads R and B
// back swapped, and none of these cases is about component order.

#include "sfpew_gtest.h"

#include <algorithm>
#include <optional>

namespace {

using sfpew_test::ContextTest;
using sfpew_test::GLbitfield;
using sfpew_test::GLenum;
using sfpew_test::GLfloat;
using sfpew_test::GLint;
using sfpew_test::GLsizei;
using sfpew_test::GLubyte;
using sfpew_test::GLuint;
using sfpew_test::PixelProbe;

constexpr GLbitfield GL_COLOR_BUFFER_BIT_ = 0x00004000;
constexpr GLenum GL_QUADS_ = 0x0007;
constexpr GLenum GL_TEXTURE_2D_ = 0x0DE1;
constexpr GLenum GL_TEXTURE_MIN_FILTER_ = 0x2801;
constexpr GLenum GL_TEXTURE_MAG_FILTER_ = 0x2800;
constexpr GLenum GL_NEAREST_ = 0x2600;
constexpr GLenum GL_ALPHA_ = 0x1906;
constexpr GLenum GL_RGB_ = 0x1907;
constexpr GLenum GL_RGBA_ = 0x1908;
constexpr GLenum GL_LUMINANCE_ = 0x1909;
constexpr GLenum GL_INTENSITY_ = 0x8049;
constexpr GLenum GL_RGB8_ = 0x8051;
constexpr GLenum GL_RGBA8_ = 0x8058;
constexpr GLenum GL_UNSIGNED_BYTE_ = 0x1401;
constexpr GLenum GL_FLOAT_ = 0x1406;
constexpr GLenum GL_DEPTH_COMPONENT_ = 0x1902;
constexpr GLenum GL_DEPTH_COMPONENT32F_ = 0x8CAC;
constexpr GLenum GL_DEPTH_TEXTURE_MODE_ = 0x884B;
constexpr GLenum GL_TEXTURE_SWIZZLE_R_ = 0x8E42;
constexpr GLenum GL_TEXTURE_SWIZZLE_G_ = 0x8E43;
constexpr GLenum GL_TEXTURE_SWIZZLE_A_ = 0x8E45;
constexpr GLint GL_RED_ = 0x1903;
constexpr GLint GL_GREEN_ = 0x1904;
constexpr GLenum GL_PROJECTION_ = 0x1701;
constexpr GLenum GL_MODELVIEW_ = 0x1700;
constexpr GLenum GL_NO_ERROR_ = 0;

constexpr int kSize = 64;

class LegacySwizzleTest : public ContextTest {
protected:
    LegacySwizzleTest() : ContextTest(sfpew_test::Backend::GLES3, kSize) {}

    void SetUp() override {
        ContextTest::SetUp();
        if (::testing::Test::IsSkipped()) return;
        clear_color_ = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glClearColor");
        clear_ = Get<void (*)(GLbitfield)>("glClear");
        begin_ = Get<void (*)(GLenum)>("glBegin");
        end_ = Get<void (*)()>("glEnd");
        color4f_ = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glColor4f");
        vertex2f_ = Get<void (*)(GLfloat, GLfloat)>("glVertex2f");
        tex_coord2f_ = Get<void (*)(GLfloat, GLfloat)>("glTexCoord2f");
        enable_ = Get<void (*)(GLenum)>("glEnable");
        gen_textures_ = Get<void (*)(GLsizei, GLuint*)>("glGenTextures");
        bind_texture_ = Get<void (*)(GLenum, GLuint)>("glBindTexture");
        tex_image2d_ = Get<void (*)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum,
                                    const void*)>("glTexImage2D");
        tex_parameteri_ = Get<void (*)(GLenum, GLenum, GLint)>("glTexParameteri");
        get_parameter_i_ = Get<void (*)(GLenum, GLenum, GLint*)>("glGetTexParameteriv");
        matrix_mode_ = Get<void (*)(GLenum)>("glMatrixMode");
        load_identity_ = Get<void (*)()>("glLoadIdentity");
        get_error_ = Get<GLenum (*)()>("glGetError");
        auto read_pixels =
            Get<void (*)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*)>("glReadPixels");
        probe_.emplace(read_pixels);

        matrix_mode_(GL_PROJECTION_);
        load_identity_();
        matrix_mode_(GL_MODELVIEW_);
        load_identity_();
        clear_color_(0.0f, 0.0f, 0.0f, 0.0f);
        while (get_error_() != GL_NO_ERROR_) {}
    }

    GLuint NewTexture() {
        GLuint texture = 0;
        gen_textures_(1, &texture);
        bind_texture_(GL_TEXTURE_2D_, texture);
        tex_parameteri_(GL_TEXTURE_2D_, GL_TEXTURE_MIN_FILTER_, GL_NEAREST_);
        tex_parameteri_(GL_TEXTURE_2D_, GL_TEXTURE_MAG_FILTER_, GL_NEAREST_);
        return texture;
    }

    void DrawTexturedQuad() {
        clear_(GL_COLOR_BUFFER_BIT_);
        begin_(GL_QUADS_);
        color4f_(1.0f, 1.0f, 1.0f, 1.0f);
        tex_coord2f_(0.0f, 0.0f);
        vertex2f_(-1.0f, -1.0f);
        tex_coord2f_(1.0f, 0.0f);
        vertex2f_(1.0f, -1.0f);
        tex_coord2f_(1.0f, 1.0f);
        vertex2f_(1.0f, 1.0f);
        tex_coord2f_(0.0f, 1.0f);
        vertex2f_(-1.0f, 1.0f);
        end_();
    }

    PixelProbe::Rgba Sample() {
        DrawTexturedQuad();
        EXPECT_EQ(get_error_(), GL_NO_ERROR_);
        return probe_->At(kSize / 2, kSize / 2);
    }

    GLint Swizzle(GLenum component) {
        GLint value = 0;
        get_parameter_i_(GL_TEXTURE_2D_, component, &value);
        return value;
    }

    void (*clear_color_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*clear_)(GLbitfield) = nullptr;
    void (*begin_)(GLenum) = nullptr;
    void (*end_)() = nullptr;
    void (*color4f_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*vertex2f_)(GLfloat, GLfloat) = nullptr;
    void (*tex_coord2f_)(GLfloat, GLfloat) = nullptr;
    void (*enable_)(GLenum) = nullptr;
    void (*gen_textures_)(GLsizei, GLuint*) = nullptr;
    void (*bind_texture_)(GLenum, GLuint) = nullptr;
    void (*tex_image2d_)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum,
                         const void*) = nullptr;
    void (*tex_parameteri_)(GLenum, GLenum, GLint) = nullptr;
    void (*get_parameter_i_)(GLenum, GLenum, GLint*) = nullptr;
    void (*matrix_mode_)(GLenum) = nullptr;
    void (*load_identity_)() = nullptr;
    GLenum (*get_error_)() = nullptr;
    std::optional<PixelProbe> probe_;
};

TEST_F(LegacySwizzleTest, RespecifyingALuminanceTextureAsRgba8DropsTheLuminanceSwizzle) {
    NewTexture();
    const GLubyte luminance = 255;
    tex_image2d_(GL_TEXTURE_2D_, 0, static_cast<GLint>(GL_LUMINANCE_), 1, 1, 0, GL_LUMINANCE_,
                 GL_UNSIGNED_BYTE_, &luminance);
    enable_(GL_TEXTURE_2D_);
    ASSERT_GT(Sample().g, 200) << "sanity check: the luminance upload replicates into green";

    static const GLubyte red[4] = {255, 0, 0, 255};
    tex_image2d_(GL_TEXTURE_2D_, 0, static_cast<GLint>(GL_RGBA8_), 1, 1, 0, GL_RGBA_,
                 GL_UNSIGNED_BYTE_, red);

    const auto pixel = Sample();
    EXPECT_LE(pixel.g, 20) << "a pure-red RGBA8 texel sampled as white: the GL_LUMINANCE swizzle "
                              "survived the re-specification";
    EXPECT_GE(std::max(pixel.r, pixel.b), 200) << "the red component itself is still there";
    EXPECT_EQ(Swizzle(GL_TEXTURE_SWIZZLE_G_), GL_GREEN_);
}

TEST_F(LegacySwizzleTest, RespecifyingAnAlphaTextureAsRgb8DropsTheAlphaSwizzle) {
    NewTexture();
    const GLubyte alpha = 255;
    tex_image2d_(GL_TEXTURE_2D_, 0, static_cast<GLint>(GL_ALPHA_), 1, 1, 0, GL_ALPHA_,
                 GL_UNSIGNED_BYTE_, &alpha);
    enable_(GL_TEXTURE_2D_);
    ASSERT_LE(Sample().g, 20) << "sanity check: a GL_ALPHA texture has no color of its own";

    static const GLubyte green[3] = {0, 255, 0};
    tex_image2d_(GL_TEXTURE_2D_, 0, static_cast<GLint>(GL_RGB8_), 1, 1, 0, GL_RGB_,
                 GL_UNSIGNED_BYTE_, green);

    EXPECT_GE(Sample().g, 200) << "an RGB8 green texel sampled as black: the GL_ALPHA swizzle "
                                  "survived the re-specification";
    EXPECT_EQ(Swizzle(GL_TEXTURE_SWIZZLE_R_), GL_RED_);
}

TEST_F(LegacySwizzleTest, RespecifyingADepthTextureAsRgba8DropsTheDepthModeSwizzle) {
    NewTexture();
    const GLfloat depth = 0.0f;
    tex_image2d_(GL_TEXTURE_2D_, 0, static_cast<GLint>(GL_DEPTH_COMPONENT32F_), 1, 1, 0,
                 GL_DEPTH_COMPONENT_, GL_FLOAT_, &depth);
    tex_parameteri_(GL_TEXTURE_2D_, GL_DEPTH_TEXTURE_MODE_, static_cast<GLint>(GL_INTENSITY_));
    enable_(GL_TEXTURE_2D_);
    ASSERT_LE(Sample().g, 20) << "sanity check: GL_INTENSITY replicates the 0.0 depth everywhere";

    static const GLubyte green[4] = {0, 255, 0, 255};
    tex_image2d_(GL_TEXTURE_2D_, 0, static_cast<GLint>(GL_RGBA8_), 1, 1, 0, GL_RGBA_,
                 GL_UNSIGNED_BYTE_, green);

    EXPECT_GE(Sample().g, 200) << "an RGBA8 green texel sampled as black: the "
                                  "GL_DEPTH_TEXTURE_MODE swizzle survived the re-specification";
}

TEST_F(LegacySwizzleTest, RespecifyingOneLegacyFormatAsAnotherAppliesTheNewSwizzle) {
    NewTexture();
    const GLubyte value = 128;
    tex_image2d_(GL_TEXTURE_2D_, 0, static_cast<GLint>(GL_LUMINANCE_), 1, 1, 0, GL_LUMINANCE_,
                 GL_UNSIGNED_BYTE_, &value);
    enable_(GL_TEXTURE_2D_);
    ASSERT_EQ(Swizzle(GL_TEXTURE_SWIZZLE_A_), 1 /* GL_ONE */)
        << "GL_LUMINANCE's alpha is the constant 1";

    tex_image2d_(GL_TEXTURE_2D_, 0, static_cast<GLint>(GL_INTENSITY_), 1, 1, 0, GL_LUMINANCE_,
                 GL_UNSIGNED_BYTE_, &value);
    EXPECT_EQ(Swizzle(GL_TEXTURE_SWIZZLE_A_), GL_RED_)
        << "GL_INTENSITY carries the single component into alpha as well";
    const auto pixel = Sample();
    EXPECT_NEAR(pixel.g, 128, 4);
}

TEST_F(LegacySwizzleTest, ASwizzleTheAppSetItselfSurvivesRespecification) {
    NewTexture();
    static const GLubyte red[4] = {255, 0, 0, 255};
    tex_image2d_(GL_TEXTURE_2D_, 0, static_cast<GLint>(GL_RGBA8_), 1, 1, 0, GL_RGBA_,
                 GL_UNSIGNED_BYTE_, red);
    // ARB_texture_swizzle state belongs to the texture object; glTexImage2D
    // specifies an image and is not allowed to reset it. Only a swizzle the
    // wrapper imposed for a legacy internalformat may be taken back off.
    tex_parameteri_(GL_TEXTURE_2D_, GL_TEXTURE_SWIZZLE_G_, GL_RED_);
    enable_(GL_TEXTURE_2D_);
    ASSERT_GE(Sample().g, 200) << "sanity check: the app's swizzle routes red into green";

    tex_image2d_(GL_TEXTURE_2D_, 0, static_cast<GLint>(GL_RGBA8_), 1, 1, 0, GL_RGBA_,
                 GL_UNSIGNED_BYTE_, red);
    EXPECT_EQ(Swizzle(GL_TEXTURE_SWIZZLE_G_), GL_RED_);
    EXPECT_GE(Sample().g, 200) << "the app's own swizzle was reset by a re-specification";
}

} // namespace
