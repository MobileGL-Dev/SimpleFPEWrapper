// SimpleFPEWrapper - tests/gtest_texture_3d_cube.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// defects-plan.md 1.7: GL_TEXTURE_3D and GL_TEXTURE_CUBE_MAP had real
// binding/upload/getter support, but the FPE fixed-function fragment shader
// only ever declared and sampled sampler2D - a 3D or cube map bound on an
// FPE-driven unit rendered whatever garbage a sampler2D reinterpreting that
// texture's storage produced (or nothing, if the driver rejected the type
// mismatch). Covers: an actual sample reaching the framebuffer for each
// target, glIsEnabled tracking the two new enables independently of
// GL_TEXTURE_2D, and the GL 2.1 3.8.14 texture-application priority order
// (cube map beats 3D beats 2D) when more than one target is enabled on the
// same unit at once.

#include "sfpew_gtest.h"

#include <cstring>
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

constexpr GLbitfield GL_COLOR_BUFFER_BIT_ = 0x00004000;
constexpr GLenum GL_QUADS_ = 0x0007;
constexpr GLenum GL_NO_ERROR_ = 0;
constexpr GLenum GL_RGBA_ = 0x1908;
constexpr GLenum GL_RGBA8_ = 0x8058;
constexpr GLenum GL_UNSIGNED_BYTE_ = 0x1401;
constexpr GLenum GL_TEXTURE_2D_ = 0x0DE1;
constexpr GLenum GL_TEXTURE_3D_ = 0x806F;
constexpr GLenum GL_TEXTURE_CUBE_MAP_ = 0x8513;
constexpr GLenum GL_TEXTURE_CUBE_MAP_POSITIVE_X_ = 0x8515;
constexpr GLenum GL_TEXTURE_CUBE_MAP_NEGATIVE_X_ = 0x8516;
constexpr GLenum GL_TEXTURE_MIN_FILTER_ = 0x2801;
constexpr GLenum GL_TEXTURE_MAG_FILTER_ = 0x2800;
constexpr GLenum GL_NEAREST_ = 0x2600;

class Texture3DCubeTest : public ContextTest {
protected:
    Texture3DCubeTest() : ContextTest(sfpew_test::Backend::GLES3, 16) {}

    void SetUp() override {
        ContextTest::SetUp();
        if (::testing::Test::IsSkipped()) return;
        enable_ = Get<void (*)(GLenum)>("glEnable");
        disable_ = Get<void (*)(GLenum)>("glDisable");
        is_enabled_ = Get<GLboolean (*)(GLenum)>("glIsEnabled");
        clear_color_ = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glClearColor");
        clear_ = Get<void (*)(GLbitfield)>("glClear");
        begin_ = Get<void (*)(GLenum)>("glBegin");
        end_ = Get<void (*)()>("glEnd");
        color4f_ = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glColor4f");
        tex_coord3f_ = Get<void (*)(GLfloat, GLfloat, GLfloat)>("glTexCoord3f");
        vertex2f_ = Get<void (*)(GLfloat, GLfloat)>("glVertex2f");
        get_error_ = Get<GLenum (*)()>("glGetError");
        gen_textures_ = Get<void (*)(GLsizei, GLuint*)>("glGenTextures");
        bind_texture_ = Get<void (*)(GLenum, GLuint)>("glBindTexture");
        tex_parameteri_ = Get<void (*)(GLenum, GLenum, GLint)>("glTexParameteri");
        tex_image2d_ = Get<void (*)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum,
                                    const void*)>("glTexImage2D");
        tex_image3d_ = Get<void (*)(GLenum, GLint, GLint, GLsizei, GLsizei, GLsizei, GLint, GLenum,
                                    GLenum, const void*)>("glTexImage3D");
        auto read_pixels =
            Get<void (*)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*)>("glReadPixels");
        ASSERT_NE(read_pixels, nullptr);
        probe_.emplace(read_pixels);
    }

    void DrawFullscreenQuadWithTexCoord(GLfloat s, GLfloat t, GLfloat r) {
        begin_(GL_QUADS_);
        tex_coord3f_(s, t, r);
        vertex2f_(-1.0f, -1.0f);
        tex_coord3f_(s, t, r);
        vertex2f_(1.0f, -1.0f);
        tex_coord3f_(s, t, r);
        vertex2f_(1.0f, 1.0f);
        tex_coord3f_(s, t, r);
        vertex2f_(-1.0f, 1.0f);
        end_();
    }

    void (*enable_)(GLenum) = nullptr;
    void (*disable_)(GLenum) = nullptr;
    GLboolean (*is_enabled_)(GLenum) = nullptr;
    void (*clear_color_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*clear_)(GLbitfield) = nullptr;
    void (*begin_)(GLenum) = nullptr;
    void (*end_)() = nullptr;
    void (*color4f_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*tex_coord3f_)(GLfloat, GLfloat, GLfloat) = nullptr;
    void (*vertex2f_)(GLfloat, GLfloat) = nullptr;
    GLenum (*get_error_)() = nullptr;
    void (*gen_textures_)(GLsizei, GLuint*) = nullptr;
    void (*bind_texture_)(GLenum, GLuint) = nullptr;
    void (*tex_parameteri_)(GLenum, GLenum, GLint) = nullptr;
    void (*tex_image2d_)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum,
                         const void*) = nullptr;
    void (*tex_image3d_)(GLenum, GLint, GLint, GLsizei, GLsizei, GLsizei, GLint, GLenum, GLenum,
                         const void*) = nullptr;
    std::optional<PixelProbe> probe_;
};

TEST_F(Texture3DCubeTest, ThreeDTextureSamplesTheCorrectSliceThroughTheFpePipeline) {
    // 2x2x2: slice 0 solid red, slice 1 solid green. A sampler2D
    // reinterpreting this storage (the pre-fix behavior) cannot land on
    // either pure color, so this discriminates a real 3D sample.
    const GLubyte red_slice[2 * 2 * 4] = {
        255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255,
    };
    const GLubyte green_slice[2 * 2 * 4] = {
        0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255,
    };
    GLubyte upload[2 * 2 * 2 * 4];
    std::memcpy(upload, red_slice, sizeof(red_slice));
    std::memcpy(upload + sizeof(red_slice), green_slice, sizeof(green_slice));

    GLuint texture = 0;
    gen_textures_(1, &texture);
    bind_texture_(GL_TEXTURE_3D_, texture);
    tex_parameteri_(GL_TEXTURE_3D_, GL_TEXTURE_MIN_FILTER_, GL_NEAREST_);
    tex_parameteri_(GL_TEXTURE_3D_, GL_TEXTURE_MAG_FILTER_, GL_NEAREST_);
    tex_image3d_(GL_TEXTURE_3D_, 0, GL_RGBA8_, 2, 2, 2, 0, GL_RGBA_, GL_UNSIGNED_BYTE_, upload);
    ASSERT_EQ(get_error_(), GL_NO_ERROR_);

    color4f_(1.0f, 1.0f, 1.0f, 1.0f);
    clear_color_(0.0f, 0.0f, 0.0f, 1.0f);
    enable_(GL_TEXTURE_3D_);

    clear_(GL_COLOR_BUFFER_BIT_);
    DrawFullscreenQuadWithTexCoord(0.5f, 0.5f, 0.25f); // slice 0 center
    const auto slice0 = probe_->At(8, 8);
    EXPECT_GT(slice0.r, 200) << "z=0.25 must sample slice 0 (red)";
    EXPECT_LT(slice0.g, 40);

    clear_(GL_COLOR_BUFFER_BIT_);
    DrawFullscreenQuadWithTexCoord(0.5f, 0.5f, 0.75f); // slice 1 center
    const auto slice1 = probe_->At(8, 8);
    EXPECT_LT(slice1.r, 40) << "z=0.75 must sample slice 1 (green), not slice 0";
    EXPECT_GT(slice1.g, 200);

    disable_(GL_TEXTURE_3D_);
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

TEST_F(Texture3DCubeTest, CubeMapSamplesTheCorrectFaceThroughTheFpePipeline) {
    const GLubyte pos_x[2 * 2 * 4] = {
        255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255,
    };
    const GLubyte neg_x[2 * 2 * 4] = {
        0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255,
    };
    const GLubyte other[2 * 2 * 4] = {
        0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255,
    };

    GLuint texture = 0;
    gen_textures_(1, &texture);
    bind_texture_(GL_TEXTURE_CUBE_MAP_, texture);
    tex_parameteri_(GL_TEXTURE_CUBE_MAP_, GL_TEXTURE_MIN_FILTER_, GL_NEAREST_);
    tex_parameteri_(GL_TEXTURE_CUBE_MAP_, GL_TEXTURE_MAG_FILTER_, GL_NEAREST_);
    for (int face = 0; face < 6; ++face) {
        const GLenum target = static_cast<GLenum>(GL_TEXTURE_CUBE_MAP_POSITIVE_X_ + face);
        const GLubyte* texels = target == GL_TEXTURE_CUBE_MAP_POSITIVE_X_   ? pos_x
                                : target == GL_TEXTURE_CUBE_MAP_NEGATIVE_X_ ? neg_x
                                                                             : other;
        tex_image2d_(target, 0, GL_RGBA8_, 2, 2, 0, GL_RGBA_, GL_UNSIGNED_BYTE_, texels);
    }
    ASSERT_EQ(get_error_(), GL_NO_ERROR_);

    color4f_(1.0f, 1.0f, 1.0f, 1.0f);
    clear_color_(0.0f, 0.0f, 0.0f, 1.0f);
    enable_(GL_TEXTURE_CUBE_MAP_);

    clear_(GL_COLOR_BUFFER_BIT_);
    DrawFullscreenQuadWithTexCoord(1.0f, 0.0f, 0.0f); // +X direction
    const auto plus_x = probe_->At(8, 8);
    EXPECT_GT(plus_x.r, 200) << "direction (1,0,0) must sample +X face (red)";
    EXPECT_LT(plus_x.b, 40);

    clear_(GL_COLOR_BUFFER_BIT_);
    DrawFullscreenQuadWithTexCoord(-1.0f, 0.0f, 0.0f); // -X direction
    const auto minus_x = probe_->At(8, 8);
    EXPECT_LT(minus_x.r, 40) << "direction (-1,0,0) must sample -X face (blue), not +X";
    EXPECT_GT(minus_x.b, 200);

    disable_(GL_TEXTURE_CUBE_MAP_);
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

TEST_F(Texture3DCubeTest, IsEnabledTracksThreeDAndCubeIndependentlyOfTwoD) {
    EXPECT_EQ(is_enabled_(GL_TEXTURE_2D_), sfpew_test::GL_FALSE_);
    EXPECT_EQ(is_enabled_(GL_TEXTURE_3D_), sfpew_test::GL_FALSE_);
    EXPECT_EQ(is_enabled_(GL_TEXTURE_CUBE_MAP_), sfpew_test::GL_FALSE_);

    enable_(GL_TEXTURE_3D_);
    EXPECT_EQ(is_enabled_(GL_TEXTURE_3D_), sfpew_test::GL_TRUE_);
    EXPECT_EQ(is_enabled_(GL_TEXTURE_2D_), sfpew_test::GL_FALSE_)
        << "enabling 3D must not also flip the 2D enable";
    EXPECT_EQ(is_enabled_(GL_TEXTURE_CUBE_MAP_), sfpew_test::GL_FALSE_);

    enable_(GL_TEXTURE_CUBE_MAP_);
    EXPECT_EQ(is_enabled_(GL_TEXTURE_CUBE_MAP_), sfpew_test::GL_TRUE_);
    EXPECT_EQ(is_enabled_(GL_TEXTURE_3D_), sfpew_test::GL_TRUE_) << "still on from above";

    disable_(GL_TEXTURE_3D_);
    EXPECT_EQ(is_enabled_(GL_TEXTURE_3D_), sfpew_test::GL_FALSE_);
    EXPECT_EQ(is_enabled_(GL_TEXTURE_CUBE_MAP_), sfpew_test::GL_TRUE_)
        << "disabling 3D must not also flip the cube enable";

    disable_(GL_TEXTURE_CUBE_MAP_);
    EXPECT_EQ(is_enabled_(GL_TEXTURE_CUBE_MAP_), sfpew_test::GL_FALSE_);
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

TEST_F(Texture3DCubeTest, CubeMapWinsOverTwoDWhenBothEnabledOnTheSameUnit) {
    // GL 2.1 3.8.14's texture application priority: cube map beats 2D when
    // both are enabled on the same unit at once.
    const GLubyte blue_2d[2 * 2 * 4] = {
        0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255,
    };
    const GLubyte red_cube_face[2 * 2 * 4] = {
        255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255,
    };

    GLuint tex2d = 0, texcube = 0;
    gen_textures_(1, &tex2d);
    bind_texture_(GL_TEXTURE_2D_, tex2d);
    tex_parameteri_(GL_TEXTURE_2D_, GL_TEXTURE_MIN_FILTER_, GL_NEAREST_);
    tex_parameteri_(GL_TEXTURE_2D_, GL_TEXTURE_MAG_FILTER_, GL_NEAREST_);
    tex_image2d_(GL_TEXTURE_2D_, 0, GL_RGBA8_, 2, 2, 0, GL_RGBA_, GL_UNSIGNED_BYTE_, blue_2d);

    gen_textures_(1, &texcube);
    bind_texture_(GL_TEXTURE_CUBE_MAP_, texcube);
    tex_parameteri_(GL_TEXTURE_CUBE_MAP_, GL_TEXTURE_MIN_FILTER_, GL_NEAREST_);
    tex_parameteri_(GL_TEXTURE_CUBE_MAP_, GL_TEXTURE_MAG_FILTER_, GL_NEAREST_);
    for (int face = 0; face < 6; ++face) {
        tex_image2d_(static_cast<GLenum>(GL_TEXTURE_CUBE_MAP_POSITIVE_X_ + face), 0, GL_RGBA8_, 2, 2,
                    0, GL_RGBA_, GL_UNSIGNED_BYTE_, red_cube_face);
    }
    ASSERT_EQ(get_error_(), GL_NO_ERROR_);

    color4f_(1.0f, 1.0f, 1.0f, 1.0f);
    clear_color_(0.0f, 0.0f, 0.0f, 1.0f);
    enable_(GL_TEXTURE_2D_);
    enable_(GL_TEXTURE_CUBE_MAP_);

    clear_(GL_COLOR_BUFFER_BIT_);
    DrawFullscreenQuadWithTexCoord(1.0f, 0.0f, 0.0f);
    const auto pixel = probe_->At(8, 8);
    EXPECT_GT(pixel.r, 200) << "cube map must win over 2D on the same unit";
    EXPECT_LT(pixel.b, 40) << "the 2D texture must not be sampled while cube map is also enabled";

    disable_(GL_TEXTURE_CUBE_MAP_);
    disable_(GL_TEXTURE_2D_);
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

} // namespace
