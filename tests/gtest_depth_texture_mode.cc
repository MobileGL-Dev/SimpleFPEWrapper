// SimpleFPEWrapper - tests/gtest_depth_texture_mode.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// GL_ARB_depth_texture: GL_DEPTH_COMPONENT/16/24/32F pass straight through to
// the backend unmodified (both GLES3 and desktop GL3 core accept them
// natively), so the base upload-and-sample path needs no wrapper code. What
// this exercises is the legacy GL_DEPTH_TEXTURE_MODE fidelity on top of that:
// the GL_TEXTURE_SWIZZLE_RGBA the wrapper applies to reproduce the fixed
// pipeline's LUMINANCE/INTENSITY/ALPHA replication, including re-swizzling a
// texture already uploaded when its mode changes without a re-upload.

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
using sfpew_test::GLubyte;
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
constexpr GLenum GL_RGBA_ = 0x1908;
constexpr GLenum GL_UNSIGNED_BYTE_ = 0x1401;
constexpr GLenum GL_FLOAT_ = 0x1406;
constexpr GLenum GL_PROJECTION_ = 0x1701;
constexpr GLenum GL_MODELVIEW_ = 0x1700;
constexpr GLenum GL_NO_ERROR_ = 0;
constexpr GLenum GL_DEPTH_TEXTURE_MODE_ = 0x884B;
constexpr GLenum GL_LUMINANCE_ = 0x1909;
constexpr GLenum GL_INTENSITY_ = 0x8049;
constexpr GLenum GL_ALPHA_ = 0x1906;

constexpr int kSize = 64;

class DepthTextureModeTest : public ContextTest {
protected:
    DepthTextureModeTest() : ContextTest(sfpew_test::Backend::GLES3, kSize) {}

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

TEST_F(DepthTextureModeTest, DefaultLuminanceModeReplicatesDepthAcrossRgbWithOpaqueAlpha) {
    UploadDepthTexture(0.6f);
    enable_(GL_TEXTURE_2D_);
    DrawTexturedQuad();
    ASSERT_EQ(get_error_(), GL_NO_ERROR_);

    const auto pixel = probe_->At(kSize / 2, kSize / 2);
    const int expected = static_cast<int>(std::lround(0.6 * 255.0));
    EXPECT_NEAR(pixel.r, expected, 2) << "R must read the sampled depth value";
    EXPECT_EQ(pixel.r, pixel.g) << "default GL_LUMINANCE mode replicates depth into G";
    EXPECT_EQ(pixel.g, pixel.b) << "default GL_LUMINANCE mode replicates depth into B";
    EXPECT_EQ(pixel.a, 255) << "GL_LUMINANCE's alpha component is the fixed 1.0";
}

TEST_F(DepthTextureModeTest, ModeDefaultsToLuminanceAndRoundTripsThroughAllFourSetters) {
    UploadDepthTexture(0.5f);
    GLint mode = -1;
    get_parameter_i_(GL_TEXTURE_2D_, GL_DEPTH_TEXTURE_MODE_, &mode);
    EXPECT_EQ(mode, static_cast<GLint>(GL_LUMINANCE_));

    tex_parameteri_(GL_TEXTURE_2D_, GL_DEPTH_TEXTURE_MODE_, static_cast<GLint>(GL_ALPHA_));
    get_parameter_i_(GL_TEXTURE_2D_, GL_DEPTH_TEXTURE_MODE_, &mode);
    EXPECT_EQ(mode, static_cast<GLint>(GL_ALPHA_));

    const GLint intensity_iv = static_cast<GLint>(GL_INTENSITY_);
    tex_parameteriv_(GL_TEXTURE_2D_, GL_DEPTH_TEXTURE_MODE_, &intensity_iv);
    get_parameter_i_(GL_TEXTURE_2D_, GL_DEPTH_TEXTURE_MODE_, &mode);
    EXPECT_EQ(mode, static_cast<GLint>(GL_INTENSITY_));

    tex_parameterf_(GL_TEXTURE_2D_, GL_DEPTH_TEXTURE_MODE_, static_cast<GLfloat>(GL_LUMINANCE_));
    GLfloat mode_f = -1.0f;
    get_parameter_f_(GL_TEXTURE_2D_, GL_DEPTH_TEXTURE_MODE_, &mode_f);
    EXPECT_EQ(static_cast<GLenum>(mode_f), GL_LUMINANCE_);

    const GLfloat alpha_fv = static_cast<GLfloat>(GL_ALPHA_);
    tex_parameterfv_(GL_TEXTURE_2D_, GL_DEPTH_TEXTURE_MODE_, &alpha_fv);
    get_parameter_f_(GL_TEXTURE_2D_, GL_DEPTH_TEXTURE_MODE_, &mode_f);
    EXPECT_EQ(static_cast<GLenum>(mode_f), GL_ALPHA_);
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

TEST_F(DepthTextureModeTest, ChangingModeOnAnAlreadyUploadedTextureReswizzlesWithoutReupload) {
    UploadDepthTexture(0.6f);
    enable_(GL_TEXTURE_2D_);
    DrawTexturedQuad();
    ASSERT_EQ(get_error_(), GL_NO_ERROR_);
    const auto before = probe_->At(kSize / 2, kSize / 2);
    const int expected = static_cast<int>(std::lround(0.6 * 255.0));
    EXPECT_NEAR(before.r, expected, 2) << "sanity check: default LUMINANCE replication happened";

    // No glTexImage2D call between here and the redraw below - the swizzle
    // change must take effect on the texture object already uploaded.
    tex_parameteri_(GL_TEXTURE_2D_, GL_DEPTH_TEXTURE_MODE_, static_cast<GLint>(GL_ALPHA_));
    DrawTexturedQuad();
    ASSERT_EQ(get_error_(), GL_NO_ERROR_);

    const auto after = probe_->At(kSize / 2, kSize / 2);
    EXPECT_LE(after.r, 2) << "GL_ALPHA mode zeroes the color channels";
    EXPECT_LE(after.g, 2);
    EXPECT_LE(after.b, 2);
    EXPECT_NEAR(after.a, expected, 2) << "GL_ALPHA mode carries the depth value in alpha instead";
}

TEST_F(DepthTextureModeTest, SettingModeOnANonDepthTextureDoesNotAlterItsColorSampling) {
    GLuint texture = 0;
    gen_textures_(1, &texture);
    bind_texture_(GL_TEXTURE_2D_, texture);
    tex_parameteri_(GL_TEXTURE_2D_, GL_TEXTURE_MIN_FILTER_, GL_NEAREST_);
    tex_parameteri_(GL_TEXTURE_2D_, GL_TEXTURE_MAG_FILTER_, GL_NEAREST_);
    static const GLubyte green_texel[4] = {0, 255, 0, 255};
    tex_image2d_(GL_TEXTURE_2D_, 0, GL_RGBA_, 1, 1, 0, GL_RGBA_, GL_UNSIGNED_BYTE_, green_texel);

    // GL_DEPTH_TEXTURE_MODE is meaningless off a depth texture; setting it
    // must not touch this object's swizzle (findTextureLevel's format guard).
    tex_parameteri_(GL_TEXTURE_2D_, GL_DEPTH_TEXTURE_MODE_, static_cast<GLint>(GL_ALPHA_));
    enable_(GL_TEXTURE_2D_);
    DrawTexturedQuad();
    ASSERT_EQ(get_error_(), GL_NO_ERROR_);

    const auto pixel = probe_->At(kSize / 2, kSize / 2);
    EXPECT_LE(pixel.r, 20);
    EXPECT_GE(pixel.g, 200) << "still the plain RGBA green texel, unswizzled";
    EXPECT_LE(pixel.b, 20);
    EXPECT_GE(pixel.a, 200);
}

} // namespace
