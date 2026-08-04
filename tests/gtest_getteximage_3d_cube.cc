// SimpleFPEWrapper - tests/gtest_getteximage_3d_cube.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// defects-plan.md 1.2: glGetTexImage only ever accepted GL_TEXTURE_2D (and
// 1D remapped onto it); GL_TEXTURE_3D and the six cube faces fell through to
// GL_INVALID_ENUM regardless of backend, even on desktop GL where the
// backend's own glGetTexImage handles them natively. Same ceiling as
// glGetCompressedTexImage: forward exactly on desktop, refuse loudly on
// GLES (no texture readback there at all), never silently return garbage.

#include "sfpew_gtest.h"

#include <algorithm>

namespace {

using sfpew_test::ContextTest;
using sfpew_test::DesktopContextTest;
using sfpew_test::GLenum;
using sfpew_test::GLint;
using sfpew_test::GLsizei;
using sfpew_test::GLubyte;
using sfpew_test::GLuint;

constexpr GLenum GL_NO_ERROR_ = 0;
constexpr GLenum GL_INVALID_OPERATION_ = 0x0502;
constexpr GLenum GL_TEXTURE_3D_ = 0x806F;
constexpr GLenum GL_TEXTURE_CUBE_MAP_ = 0x8513;
constexpr GLenum GL_TEXTURE_CUBE_MAP_POSITIVE_X_ = 0x8515;
constexpr GLenum GL_RGBA_ = 0x1908;
constexpr GLenum GL_RGBA8_ = 0x8058;
constexpr GLenum GL_UNSIGNED_BYTE_ = 0x1401;
constexpr GLenum GL_TEXTURE_MIN_FILTER_ = 0x2801;
constexpr GLenum GL_TEXTURE_MAG_FILTER_ = 0x2800;
constexpr GLenum GL_NEAREST_ = 0x2600;
constexpr GLenum GL_TEXTURE_WIDTH_ = 0x1000;
constexpr GLenum GL_TEXTURE_HEIGHT_ = 0x1001;
constexpr GLenum GL_TEXTURE_DEPTH_ = 0x8071;
constexpr GLenum GL_TEXTURE_INTERNAL_FORMAT_ = 0x1003;

TEST_F(DesktopContextTest, ThreeDTextureRoundTripsOnDesktop) {
    auto gen_textures = Get<void (*)(GLsizei, GLuint*)>("glGenTextures");
    auto bind_texture = Get<void (*)(GLenum, GLuint)>("glBindTexture");
    auto tex_parameter = Get<void (*)(GLenum, GLenum, GLint)>("glTexParameteri");
    auto tex_image_3d =
        Get<void (*)(GLenum, GLint, GLint, GLsizei, GLsizei, GLsizei, GLint, GLenum, GLenum,
                    const void*)>("glTexImage3D");
    auto get_tex_image =
        Get<void (*)(GLenum, GLint, GLenum, GLenum, void*)>("glGetTexImage");
    auto get_error = Get<GLenum (*)()>("glGetError");
    ASSERT_NE(tex_image_3d, nullptr);
    ASSERT_NE(get_tex_image, nullptr);

    // 2x2x2: each of the 8 texels a distinct colour, so a scrambled slice
    // order or a wrong readback size shows up immediately.
    const GLubyte upload[2 * 2 * 2 * 4] = {
        255, 0,   0,   255, 0, 255, 0,   255, 0,   0, 255, 255, 255, 255, 0, 255,
        255, 0, 255, 255, 0,   255, 255, 255, 128, 128, 128, 255, 64,  64,  64, 255,
    };
    GLuint texture = 0;
    gen_textures(1, &texture);
    bind_texture(GL_TEXTURE_3D_, texture);
    tex_parameter(GL_TEXTURE_3D_, GL_TEXTURE_MIN_FILTER_, GL_NEAREST_);
    tex_parameter(GL_TEXTURE_3D_, GL_TEXTURE_MAG_FILTER_, GL_NEAREST_);
    tex_image_3d(GL_TEXTURE_3D_, 0, GL_RGBA8_, 2, 2, 2, 0, GL_RGBA_, GL_UNSIGNED_BYTE_, upload);
    ASSERT_EQ(get_error(), GL_NO_ERROR_);

    GLubyte readback[2 * 2 * 2 * 4] = {};
    get_tex_image(GL_TEXTURE_3D_, 0, GL_RGBA_, GL_UNSIGNED_BYTE_, readback);
    EXPECT_EQ(get_error(), GL_NO_ERROR_);
    for (size_t i = 0; i < sizeof(upload); ++i) EXPECT_EQ(readback[i], upload[i]) << "byte " << i;
}

TEST_F(DesktopContextTest, CubeFaceRoundTripsOnDesktop) {
    auto gen_textures = Get<void (*)(GLsizei, GLuint*)>("glGenTextures");
    auto bind_texture = Get<void (*)(GLenum, GLuint)>("glBindTexture");
    auto tex_parameter = Get<void (*)(GLenum, GLenum, GLint)>("glTexParameteri");
    auto tex_image_2d =
        Get<void (*)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum,
                    const void*)>("glTexImage2D");
    auto get_tex_image =
        Get<void (*)(GLenum, GLint, GLenum, GLenum, void*)>("glGetTexImage");
    auto get_error = Get<GLenum (*)()>("glGetError");

    const GLubyte upload[2 * 2 * 4] = {
        10, 20, 30, 255, 40, 50, 60, 255, 70, 80, 90, 255, 100, 110, 120, 255,
    };
    GLuint texture = 0;
    gen_textures(1, &texture);
    bind_texture(GL_TEXTURE_CUBE_MAP_, texture);
    tex_parameter(GL_TEXTURE_CUBE_MAP_, GL_TEXTURE_MIN_FILTER_, GL_NEAREST_);
    tex_parameter(GL_TEXTURE_CUBE_MAP_, GL_TEXTURE_MAG_FILTER_, GL_NEAREST_);
    for (int face = 0; face < 6; ++face) {
        tex_image_2d(static_cast<GLenum>(GL_TEXTURE_CUBE_MAP_POSITIVE_X_ + face), 0, GL_RGBA8_, 2,
                    2, 0, GL_RGBA_, GL_UNSIGNED_BYTE_, upload);
    }
    ASSERT_EQ(get_error(), GL_NO_ERROR_);

    GLubyte readback[2 * 2 * 4] = {};
    get_tex_image(GL_TEXTURE_CUBE_MAP_POSITIVE_X_ + 3 /* NEGATIVE_Y */, 0, GL_RGBA_,
                  GL_UNSIGNED_BYTE_, readback);
    EXPECT_EQ(get_error(), GL_NO_ERROR_);
    for (size_t i = 0; i < sizeof(upload); ++i) EXPECT_EQ(readback[i], upload[i]) << "byte " << i;
}

TEST_F(ContextTest, ThreeDAndCubeFailLoudlyAndLeaveTheBufferUntouchedOnGles) {
    auto get_tex_image =
        Get<void (*)(GLenum, GLint, GLenum, GLenum, void*)>("glGetTexImage");
    auto get_error = Get<GLenum (*)()>("glGetError");
    ASSERT_NE(get_tex_image, nullptr);

    GLubyte sentinel[16];
    std::fill(std::begin(sentinel), std::end(sentinel), GLubyte{0xAB});
    get_tex_image(GL_TEXTURE_3D_, 0, GL_RGBA_, GL_UNSIGNED_BYTE_, sentinel);
    EXPECT_EQ(get_error(), GL_INVALID_OPERATION_);
    for (GLubyte b : sentinel) EXPECT_EQ(b, 0xAB) << "GLES must not touch the caller's buffer";

    std::fill(std::begin(sentinel), std::end(sentinel), GLubyte{0xCD});
    get_tex_image(GL_TEXTURE_CUBE_MAP_POSITIVE_X_, 0, GL_RGBA_, GL_UNSIGNED_BYTE_, sentinel);
    EXPECT_EQ(get_error(), GL_INVALID_OPERATION_);
    for (GLubyte b : sentinel) EXPECT_EQ(b, 0xCD);
}

// defects-plan-2.md 2.7: texture_metadata_cache_t (glGetTexLevelParameteriv's
// ES fallback - ES 3.0 has no such query natively) stayed 2D-only and
// glTexImage3D wasn't even wrapped, so a 3D texture or cube face's level
// parameters were unanswerable on GLES even though nothing about a level's
// width/height/depth/format needs pixel readback (unlike glGetTexImage
// above, which really does hit GLES's readback ceiling).
TEST_F(ContextTest, LevelParametersAnswerThreeDAndCubeOnGles) {
    auto gen_textures = Get<void (*)(GLsizei, GLuint*)>("glGenTextures");
    auto bind_texture = Get<void (*)(GLenum, GLuint)>("glBindTexture");
    auto tex_parameter = Get<void (*)(GLenum, GLenum, GLint)>("glTexParameteri");
    auto tex_image_3d =
        Get<void (*)(GLenum, GLint, GLint, GLsizei, GLsizei, GLsizei, GLint, GLenum, GLenum,
                    const void*)>("glTexImage3D");
    auto tex_image_2d =
        Get<void (*)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum,
                    const void*)>("glTexImage2D");
    auto get_level_parameter = Get<void (*)(GLenum, GLint, GLenum, GLint*)>("glGetTexLevelParameteriv");
    auto get_error = Get<GLenum (*)()>("glGetError");
    ASSERT_NE(tex_image_3d, nullptr);
    ASSERT_NE(get_level_parameter, nullptr);

    GLuint volume = 0;
    gen_textures(1, &volume);
    bind_texture(GL_TEXTURE_3D_, volume);
    tex_parameter(GL_TEXTURE_3D_, GL_TEXTURE_MIN_FILTER_, GL_NEAREST_);
    tex_parameter(GL_TEXTURE_3D_, GL_TEXTURE_MAG_FILTER_, GL_NEAREST_);
    tex_image_3d(GL_TEXTURE_3D_, 0, GL_RGBA8_, 4, 8, 16, 0, GL_RGBA_, GL_UNSIGNED_BYTE_, nullptr);
    ASSERT_EQ(get_error(), GL_NO_ERROR_);

    GLint width = 0, height = 0, depth = 0, internal_format = 0;
    get_level_parameter(GL_TEXTURE_3D_, 0, GL_TEXTURE_WIDTH_, &width);
    get_level_parameter(GL_TEXTURE_3D_, 0, GL_TEXTURE_HEIGHT_, &height);
    get_level_parameter(GL_TEXTURE_3D_, 0, GL_TEXTURE_DEPTH_, &depth);
    get_level_parameter(GL_TEXTURE_3D_, 0, GL_TEXTURE_INTERNAL_FORMAT_, &internal_format);
    EXPECT_EQ(width, 4);
    EXPECT_EQ(height, 8);
    EXPECT_EQ(depth, 16) << "GL_TEXTURE_DEPTH must come back real, not the width/height path's "
                             "always-1 default";
    EXPECT_EQ(internal_format, static_cast<GLint>(GL_RGBA8_));
    EXPECT_EQ(get_error(), GL_NO_ERROR_);

    GLuint cube = 0;
    gen_textures(1, &cube);
    bind_texture(GL_TEXTURE_CUBE_MAP_, cube);
    tex_parameter(GL_TEXTURE_CUBE_MAP_, GL_TEXTURE_MIN_FILTER_, GL_NEAREST_);
    tex_parameter(GL_TEXTURE_CUBE_MAP_, GL_TEXTURE_MAG_FILTER_, GL_NEAREST_);
    for (int face = 0; face < 6; ++face) {
        tex_image_2d(static_cast<GLenum>(GL_TEXTURE_CUBE_MAP_POSITIVE_X_ + face), 0, GL_RGBA8_, 32,
                    32, 0, GL_RGBA_, GL_UNSIGNED_BYTE_, nullptr);
    }
    ASSERT_EQ(get_error(), GL_NO_ERROR_);

    GLint face_width = 0, face_height = 0;
    // A face other than +X, proving the metadata isn't keyed on the exact
    // face enum (spec requires every face at a level to share size/format
    // for cube completeness, so one shared entry per texture+level is
    // correct, not an accidental collision).
    get_level_parameter(static_cast<GLenum>(GL_TEXTURE_CUBE_MAP_POSITIVE_X_ + 3), 0, GL_TEXTURE_WIDTH_,
                        &face_width);
    get_level_parameter(static_cast<GLenum>(GL_TEXTURE_CUBE_MAP_POSITIVE_X_ + 3), 0,
                        GL_TEXTURE_HEIGHT_, &face_height);
    EXPECT_EQ(face_width, 32);
    EXPECT_EQ(face_height, 32);
    EXPECT_EQ(get_error(), GL_NO_ERROR_);

    // The 3D texture and the cube map's answers must not have crossed -
    // proof the (name, target) key's target half is real, not decorative.
    GLint volume_width_again = 0;
    get_level_parameter(GL_TEXTURE_3D_, 0, GL_TEXTURE_WIDTH_, &volume_width_again);
    EXPECT_EQ(volume_width_again, 4) << "querying the cube map must not have clobbered the volume's entry";
}

} // namespace
