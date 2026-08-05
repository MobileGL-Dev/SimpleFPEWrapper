// SimpleFPEWrapper - tests/gtest_texture_env_crossbar.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// GL_ARB_texture_env_crossbar: a GL_COMBINE unit's GL_SOURCE{0,1,2}_{RGB,
// ALPHA} may name GL_TEXTUREn for ANY active n, not just a unit sampled
// earlier in iteration order. fpe_shadergen.cpp's add_fs_body used to
// texture and combine each unit in one interleaved ascending pass, so a
// DESCENDING cross-unit reference (unit 0's combiner sourcing GL_TEXTURE1,
// a unit not yet sampled at that point in the old single loop) silently
// read texcolor1 as vec4(0.0) - i.e. always black - instead of unit 1's
// real texture color. This exercises exactly that descending case.

#include "sfpew_gtest.h"

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
constexpr GLenum GL_RGBA_ = 0x1908;
constexpr GLenum GL_UNSIGNED_BYTE_ = 0x1401;
constexpr GLenum GL_TEXTURE_2D_ = 0x0DE1;
constexpr GLenum GL_NEAREST_ = 0x2600;
constexpr GLenum GL_TEXTURE_MIN_FILTER_ = 0x2801;
constexpr GLenum GL_TEXTURE_MAG_FILTER_ = 0x2800;
constexpr GLenum GL_TEXTURE0_ = 0x84C0;
constexpr GLenum GL_TEXTURE1_ = 0x84C1;
constexpr GLenum GL_TEXTURE_ENV_ = 0x2300;
constexpr GLenum GL_TEXTURE_ENV_MODE_ = 0x2200;
constexpr GLenum GL_COMBINE_ = 0x8570;
constexpr GLenum GL_COMBINE_RGB_ = 0x8571;
constexpr GLenum GL_COMBINE_ALPHA_ = 0x8572;
constexpr GLenum GL_SRC0_RGB_ = 0x8580;
constexpr GLenum GL_SRC0_ALPHA_ = 0x8588;
constexpr GLenum GL_OPERAND0_RGB_ = 0x8590;
constexpr GLenum GL_SRC_COLOR_ = 0x0300;
constexpr GLenum GL_REPLACE_ = 0x1E01;
constexpr GLenum GL_MODULATE_ = 0x2100;
constexpr GLenum GL_NO_ERROR_ = 0;

constexpr int kSize = 64;

class TextureEnvCrossbarTest : public ContextTest {
protected:
    TextureEnvCrossbarTest() : ContextTest(sfpew_test::Backend::GLES3, kSize) {}

    void SetUp() override {
        ContextTest::SetUp();
        if (::testing::Test::IsSkipped()) return;

        clear_color_ = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glClearColor");
        clear_ = Get<void (*)(GLbitfield)>("glClear");
        begin_ = Get<void (*)(GLenum)>("glBegin");
        end_ = Get<void (*)()>("glEnd");
        color3f_ = Get<void (*)(GLfloat, GLfloat, GLfloat)>("glColor3f");
        vertex2f_ = Get<void (*)(GLfloat, GLfloat)>("glVertex2f");
        multi_tex_coord2f_ = Get<void (*)(GLenum, GLfloat, GLfloat)>("glMultiTexCoord2f");
        read_pixels_ =
            Get<void (*)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*)>("glReadPixels");
        get_error_ = Get<GLenum (*)()>("glGetError");
        enable_ = Get<void (*)(GLenum)>("glEnable");
        active_texture_ = Get<void (*)(GLenum)>("glActiveTexture");
        gen_textures_ = Get<void (*)(GLsizei, GLuint*)>("glGenTextures");
        bind_texture_ = Get<void (*)(GLenum, GLuint)>("glBindTexture");
        tex_image2d_ =
            Get<void (*)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum,
                         const void*)>("glTexImage2D");
        tex_parameteri_ = Get<void (*)(GLenum, GLenum, GLint)>("glTexParameteri");
        tex_envi_ = Get<void (*)(GLenum, GLenum, GLint)>("glTexEnvi");
        ASSERT_NE(read_pixels_, nullptr);
        get_error_();
    }

    // A single-texel 2D texture bound and filtered nearest, on whichever
    // unit is currently active.
    GLuint MakeSolidTexture(GLubyte r, GLubyte g, GLubyte b, GLubyte a) {
        GLuint texture = 0;
        gen_textures_(1, &texture);
        bind_texture_(GL_TEXTURE_2D_, texture);
        const GLubyte texel[4] = {r, g, b, a};
        tex_image2d_(GL_TEXTURE_2D_, 0, GL_RGBA_, 1, 1, 0, GL_RGBA_, GL_UNSIGNED_BYTE_, texel);
        tex_parameteri_(GL_TEXTURE_2D_, GL_TEXTURE_MIN_FILTER_, GL_NEAREST_);
        tex_parameteri_(GL_TEXTURE_2D_, GL_TEXTURE_MAG_FILTER_, GL_NEAREST_);
        return texture;
    }

    void Quad() {
        begin_(GL_QUADS_);
        color3f_(1.0f, 1.0f, 1.0f);
        multi_tex_coord2f_(GL_TEXTURE0_, 0.0f, 0.0f);
        multi_tex_coord2f_(GL_TEXTURE1_, 0.0f, 0.0f);
        vertex2f_(-1.0f, -1.0f);
        multi_tex_coord2f_(GL_TEXTURE0_, 1.0f, 0.0f);
        multi_tex_coord2f_(GL_TEXTURE1_, 1.0f, 0.0f);
        vertex2f_(1.0f, -1.0f);
        multi_tex_coord2f_(GL_TEXTURE0_, 1.0f, 1.0f);
        multi_tex_coord2f_(GL_TEXTURE1_, 1.0f, 1.0f);
        vertex2f_(1.0f, 1.0f);
        multi_tex_coord2f_(GL_TEXTURE0_, 0.0f, 1.0f);
        multi_tex_coord2f_(GL_TEXTURE1_, 0.0f, 1.0f);
        vertex2f_(-1.0f, 1.0f);
        end_();
    }

    void (*clear_color_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*clear_)(GLbitfield) = nullptr;
    void (*begin_)(GLenum) = nullptr;
    void (*end_)() = nullptr;
    void (*color3f_)(GLfloat, GLfloat, GLfloat) = nullptr;
    void (*vertex2f_)(GLfloat, GLfloat) = nullptr;
    void (*multi_tex_coord2f_)(GLenum, GLfloat, GLfloat) = nullptr;
    void (*read_pixels_)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*) = nullptr;
    GLenum (*get_error_)() = nullptr;
    void (*enable_)(GLenum) = nullptr;
    void (*active_texture_)(GLenum) = nullptr;
    void (*gen_textures_)(GLsizei, GLuint*) = nullptr;
    void (*bind_texture_)(GLenum, GLuint) = nullptr;
    void (*tex_image2d_)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum,
                         const void*) = nullptr;
    void (*tex_parameteri_)(GLenum, GLenum, GLint) = nullptr;
    void (*tex_envi_)(GLenum, GLenum, GLint) = nullptr;
};

// Unit 0's GL_COMBINE sources GL_SOURCE0_RGB/ALPHA = GL_TEXTURE1 - a unit
// sampled AFTER unit 0 in ascending iteration order, the descending
// cross-unit reference the fix in add_fs_body's two-pass split (sample all
// units, then combine all units) exists for. Unit 0's texture is red, unit
// 1's is blue; unit 0's combiner ignores its own texcolor entirely and
// just relays unit 1's. Unit 1 is left at the GL_MODULATE default, so the
// final pixel is (unit-0-combine-result) * (unit 1's own blue texel).
//
// Broken (pre-fix): unit 0's crossbar read returns vec4(0.0) (black,
// alpha 0) because unit 1 has not been sampled yet at that point in a
// single interleaved loop -> final RGBA is (0,0,0,0).
// Fixed: unit 0's combine result is blue (0,0,255,255) -> modulated by
// unit 1's own blue texel stays blue (0,0,255,255).
TEST_F(TextureEnvCrossbarTest, DescendingCrossbarReferenceReadsTheReferencedUnit) {
    active_texture_(GL_TEXTURE0_);
    const GLuint tex0 = MakeSolidTexture(255, 0, 0, 255);
    enable_(GL_TEXTURE_2D_);

    active_texture_(GL_TEXTURE1_);
    const GLuint tex1 = MakeSolidTexture(0, 0, 255, 255);
    enable_(GL_TEXTURE_2D_);
    (void)tex0;
    (void)tex1;

    // Unit 0: GL_COMBINE, GL_REPLACE sourced entirely from GL_TEXTURE1 -
    // the forward/descending crossbar reference.
    active_texture_(GL_TEXTURE0_);
    tex_envi_(GL_TEXTURE_ENV_, GL_TEXTURE_ENV_MODE_, GL_COMBINE_);
    tex_envi_(GL_TEXTURE_ENV_, GL_COMBINE_RGB_, GL_REPLACE_);
    tex_envi_(GL_TEXTURE_ENV_, GL_SRC0_RGB_, static_cast<GLint>(GL_TEXTURE1_));
    tex_envi_(GL_TEXTURE_ENV_, GL_OPERAND0_RGB_, GL_SRC_COLOR_);
    tex_envi_(GL_TEXTURE_ENV_, GL_COMBINE_ALPHA_, GL_REPLACE_);
    tex_envi_(GL_TEXTURE_ENV_, GL_SRC0_ALPHA_, static_cast<GLint>(GL_TEXTURE1_));

    // Unit 1: left at the GL_MODULATE default (its own blue texel).

    clear_color_(0.0f, 0.0f, 0.0f, 1.0f);
    clear_(GL_COLOR_BUFFER_BIT_);
    Quad();

    PixelProbe probe(read_pixels_);
    const auto pixel = probe.At(kSize / 2, kSize / 2);
    ASSERT_EQ(get_error_(), GL_NO_ERROR_);
    EXPECT_TRUE(pixel.b >= 200 && pixel.r <= 50 && pixel.g <= 50 && pixel.a >= 200)
        << "descending crossbar pixel (" << (int)pixel.r << ',' << (int)pixel.g << ','
        << (int)pixel.b << ',' << (int)pixel.a
        << ") expected opaque blue (unit 0's combine relaying unit 1's texture)";
}

} // namespace
