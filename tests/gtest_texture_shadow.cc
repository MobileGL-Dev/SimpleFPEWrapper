// SimpleFPEWrapper - tests/gtest_texture_shadow.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// GL_ARB_shadow: GL_TEXTURE_COMPARE_MODE == GL_COMPARE_R_TO_TEXTURE on a
// depth texture turns a sample into a depth comparison - the R texcoord is
// the reference depth (GL 1.4 3.8.14 convention), compared against the
// stored depth via GL_TEXTURE_COMPARE_FUNC - instead of returning the raw
// depth value. GL_TEXTURE_COMPARE_MODE/_FUNC are real GLES3/GL-core enums
// the backend already stores and answers on its own; what this exercises is
// the wrapper's generated-shader side of it (sampler2DShadow codegen and the
// per-unit resync that picks it), not the passthrough plumbing.

#include "sfpew_gtest.h"

#include <cmath>
#include <optional>

namespace {

using sfpew_test::ContextTest;
using sfpew_test::GLbitfield;
using sfpew_test::GLenum;
using sfpew_test::GLfloat;
using sfpew_test::GLint;
using sfpew_test::GLsizei;
using sfpew_test::GLuint;
using sfpew_test::PixelProbe;

constexpr GLbitfield GL_COLOR_BUFFER_BIT_ = 0x00004000;
constexpr GLenum GL_QUADS_ = 0x0007;
constexpr GLenum GL_TEXTURE_2D_ = 0x0DE1;
constexpr GLenum GL_TEXTURE_MIN_FILTER_ = 0x2801;
constexpr GLenum GL_TEXTURE_MAG_FILTER_ = 0x2800;
constexpr GLenum GL_NEAREST_ = 0x2600;
constexpr GLenum GL_DEPTH_COMPONENT_ = 0x1902;
constexpr GLenum GL_DEPTH_COMPONENT32F_ = 0x8CAC;
constexpr GLenum GL_FLOAT_ = 0x1406;
constexpr GLenum GL_PROJECTION_ = 0x1701;
constexpr GLenum GL_MODELVIEW_ = 0x1700;
constexpr GLenum GL_NO_ERROR_ = 0;
constexpr GLenum GL_NONE_ = 0;
constexpr GLenum GL_TEXTURE_COMPARE_MODE_ = 0x884C;
constexpr GLenum GL_TEXTURE_COMPARE_FUNC_ = 0x884D;
constexpr GLenum GL_COMPARE_R_TO_TEXTURE_ = 0x884E;
constexpr GLenum GL_LEQUAL_ = 0x0203;

constexpr int kSize = 64;

class TextureShadowTest : public ContextTest {
protected:
    TextureShadowTest() : ContextTest(sfpew_test::Backend::GLES3, kSize) {}

    void SetUp() override {
        ContextTest::SetUp();
        if (::testing::Test::IsSkipped()) return;
        clear_color_ = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glClearColor");
        clear_ = Get<void (*)(GLbitfield)>("glClear");
        begin_ = Get<void (*)(GLenum)>("glBegin");
        end_ = Get<void (*)()>("glEnd");
        color4f_ = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glColor4f");
        vertex2f_ = Get<void (*)(GLfloat, GLfloat)>("glVertex2f");
        tex_coord3f_ = Get<void (*)(GLfloat, GLfloat, GLfloat)>("glTexCoord3f");
        enable_ = Get<void (*)(GLenum)>("glEnable");
        gen_textures_ = Get<void (*)(GLsizei, GLuint*)>("glGenTextures");
        bind_texture_ = Get<void (*)(GLenum, GLuint)>("glBindTexture");
        tex_image2d_ = Get<void (*)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum,
                                    const void*)>("glTexImage2D");
        tex_parameteri_ = Get<void (*)(GLenum, GLenum, GLint)>("glTexParameteri");
        tex_parameterf_ = Get<void (*)(GLenum, GLenum, GLfloat)>("glTexParameterf");
        tex_parameteriv_ = Get<void (*)(GLenum, GLenum, const GLint*)>("glTexParameteriv");
        tex_parameterfv_ = Get<void (*)(GLenum, GLenum, const GLfloat*)>("glTexParameterfv");
        get_parameter_i_ = Get<void (*)(GLenum, GLenum, GLint*)>("glGetTexParameteriv");
        get_parameter_f_ = Get<void (*)(GLenum, GLenum, GLfloat*)>("glGetTexParameterfv");
        matrix_mode_ = Get<void (*)(GLenum)>("glMatrixMode");
        load_identity_ = Get<void (*)()>("glLoadIdentity");
        get_error_ = Get<GLenum (*)()>("glGetError");
        auto read_pixels =
            Get<void (*)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*)>("glReadPixels");
        ASSERT_NE(read_pixels, nullptr);
        probe_.emplace(read_pixels);

        matrix_mode_(GL_PROJECTION_);
        load_identity_();
        matrix_mode_(GL_MODELVIEW_);
        load_identity_();
        clear_color_(0.0f, 0.0f, 0.0f, 0.0f);
        get_error_();
    }

    // A single texel so s/t never matter - only the R coordinate driven per
    // draw distinguishes the two cases below.
    GLuint UploadDepthTexture(GLfloat depth) {
        GLuint texture = 0;
        gen_textures_(1, &texture);
        bind_texture_(GL_TEXTURE_2D_, texture);
        tex_parameteri_(GL_TEXTURE_2D_, GL_TEXTURE_MIN_FILTER_, GL_NEAREST_);
        tex_parameteri_(GL_TEXTURE_2D_, GL_TEXTURE_MAG_FILTER_, GL_NEAREST_);
        tex_image2d_(GL_TEXTURE_2D_, 0, GL_DEPTH_COMPONENT32F_, 1, 1, 0, GL_DEPTH_COMPONENT_,
                    GL_FLOAT_, &depth);
        return texture;
    }

    void DrawTexturedQuad(GLfloat r) {
        clear_(GL_COLOR_BUFFER_BIT_);
        begin_(GL_QUADS_);
        color4f_(1.0f, 1.0f, 1.0f, 1.0f);
        tex_coord3f_(0.0f, 0.0f, r);
        vertex2f_(-1.0f, -1.0f);
        tex_coord3f_(1.0f, 0.0f, r);
        vertex2f_(1.0f, -1.0f);
        tex_coord3f_(1.0f, 1.0f, r);
        vertex2f_(1.0f, 1.0f);
        tex_coord3f_(0.0f, 1.0f, r);
        vertex2f_(-1.0f, 1.0f);
        end_();
    }

    void (*clear_color_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*clear_)(GLbitfield) = nullptr;
    void (*begin_)(GLenum) = nullptr;
    void (*end_)() = nullptr;
    void (*color4f_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*vertex2f_)(GLfloat, GLfloat) = nullptr;
    void (*tex_coord3f_)(GLfloat, GLfloat, GLfloat) = nullptr;
    void (*enable_)(GLenum) = nullptr;
    void (*gen_textures_)(GLsizei, GLuint*) = nullptr;
    void (*bind_texture_)(GLenum, GLuint) = nullptr;
    void (*tex_image2d_)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum,
                         const void*) = nullptr;
    void (*tex_parameteri_)(GLenum, GLenum, GLint) = nullptr;
    void (*tex_parameterf_)(GLenum, GLenum, GLfloat) = nullptr;
    void (*tex_parameteriv_)(GLenum, GLenum, const GLint*) = nullptr;
    void (*tex_parameterfv_)(GLenum, GLenum, const GLfloat*) = nullptr;
    void (*get_parameter_i_)(GLenum, GLenum, GLint*) = nullptr;
    void (*get_parameter_f_)(GLenum, GLenum, GLfloat*) = nullptr;
    void (*matrix_mode_)(GLenum) = nullptr;
    void (*load_identity_)() = nullptr;
    GLenum (*get_error_)() = nullptr;
    std::optional<PixelProbe> probe_;
};

TEST_F(TextureShadowTest, CompareModeDefaultsToNoneAndRoundTripsThroughAllFourSetters) {
    UploadDepthTexture(0.5f);
    GLint mode = -1;
    get_parameter_i_(GL_TEXTURE_2D_, GL_TEXTURE_COMPARE_MODE_, &mode);
    EXPECT_EQ(mode, static_cast<GLint>(GL_NONE_));

    tex_parameteri_(GL_TEXTURE_2D_, GL_TEXTURE_COMPARE_MODE_,
                    static_cast<GLint>(GL_COMPARE_R_TO_TEXTURE_));
    get_parameter_i_(GL_TEXTURE_2D_, GL_TEXTURE_COMPARE_MODE_, &mode);
    EXPECT_EQ(mode, static_cast<GLint>(GL_COMPARE_R_TO_TEXTURE_));

    const GLint none_iv = static_cast<GLint>(GL_NONE_);
    tex_parameteriv_(GL_TEXTURE_2D_, GL_TEXTURE_COMPARE_MODE_, &none_iv);
    get_parameter_i_(GL_TEXTURE_2D_, GL_TEXTURE_COMPARE_MODE_, &mode);
    EXPECT_EQ(mode, static_cast<GLint>(GL_NONE_));

    tex_parameterf_(GL_TEXTURE_2D_, GL_TEXTURE_COMPARE_MODE_,
                    static_cast<GLfloat>(GL_COMPARE_R_TO_TEXTURE_));
    GLfloat mode_f = -1.0f;
    get_parameter_f_(GL_TEXTURE_2D_, GL_TEXTURE_COMPARE_MODE_, &mode_f);
    EXPECT_EQ(static_cast<GLenum>(mode_f), GL_COMPARE_R_TO_TEXTURE_);

    const GLfloat none_fv = static_cast<GLfloat>(GL_NONE_);
    tex_parameterfv_(GL_TEXTURE_2D_, GL_TEXTURE_COMPARE_MODE_, &none_fv);
    get_parameter_f_(GL_TEXTURE_2D_, GL_TEXTURE_COMPARE_MODE_, &mode_f);
    EXPECT_EQ(static_cast<GLenum>(mode_f), GL_NONE_);
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

TEST_F(TextureShadowTest, ShadowComparisonFailsWhenReferenceDepthExceedsStoredDepth) {
    UploadDepthTexture(0.5f);
    tex_parameteri_(GL_TEXTURE_2D_, GL_TEXTURE_COMPARE_MODE_,
                    static_cast<GLint>(GL_COMPARE_R_TO_TEXTURE_));
    tex_parameteri_(GL_TEXTURE_2D_, GL_TEXTURE_COMPARE_FUNC_, static_cast<GLint>(GL_LEQUAL_));
    enable_(GL_TEXTURE_2D_);

    // GL_LEQUAL: pass when ref <= stored. 0.8 > 0.5 stored, so this fails.
    DrawTexturedQuad(0.8f);
    ASSERT_EQ(get_error_(), GL_NO_ERROR_);

    const auto pixel = probe_->At(kSize / 2, kSize / 2);
    EXPECT_LE(pixel.r, 2) << "a failed shadow comparison must read back as 0.0";
    EXPECT_LE(pixel.g, 2);
    EXPECT_LE(pixel.b, 2);
}

TEST_F(TextureShadowTest, ShadowComparisonPassesWhenReferenceDepthIsBelowStoredDepth) {
    UploadDepthTexture(0.5f);
    tex_parameteri_(GL_TEXTURE_2D_, GL_TEXTURE_COMPARE_MODE_,
                    static_cast<GLint>(GL_COMPARE_R_TO_TEXTURE_));
    tex_parameteri_(GL_TEXTURE_2D_, GL_TEXTURE_COMPARE_FUNC_, static_cast<GLint>(GL_LEQUAL_));
    enable_(GL_TEXTURE_2D_);

    // GL_LEQUAL: pass when ref <= stored. 0.2 <= 0.5 stored, so this passes.
    DrawTexturedQuad(0.2f);
    ASSERT_EQ(get_error_(), GL_NO_ERROR_);

    const auto pixel = probe_->At(kSize / 2, kSize / 2);
    EXPECT_GE(pixel.r, 253) << "a passed shadow comparison must read back as 1.0";
    EXPECT_GE(pixel.g, 253);
    EXPECT_GE(pixel.b, 253);
}

TEST_F(TextureShadowTest, WithoutCompareModeSamplingReturnsRawDepthRegardlessOfReferenceCoord) {
    UploadDepthTexture(0.5f);
    enable_(GL_TEXTURE_2D_);

    // No GL_TEXTURE_COMPARE_MODE set (stays GL_NONE): an ordinary sampler2D
    // fetch ignores the R coordinate entirely and returns the raw depth,
    // replicated per the default GL_LUMINANCE depth-texture mode.
    DrawTexturedQuad(0.8f);
    ASSERT_EQ(get_error_(), GL_NO_ERROR_);

    const auto pixel = probe_->At(kSize / 2, kSize / 2);
    const int expected = static_cast<int>(std::lround(0.5 * 255.0));
    EXPECT_NEAR(pixel.r, expected, 2) << "R coordinate must not affect a non-shadow sample";
}

} // namespace
