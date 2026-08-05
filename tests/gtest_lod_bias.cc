// SimpleFPEWrapper - tests/gtest_lod_bias.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// GL_EXT_texture_lod_bias: glTexEnvf(GL_TEXTURE_FILTER_CONTROL,
// GL_TEXTURE_LOD_BIAS, ...) was already captured into per-unit state but
// never reached the generated shader, so it had no effect on which mip level
// got sampled. A two-level texture with a solid, distinct colour per level,
// sampled at its native texel:pixel ratio (bias 0 lands on level 0 by
// construction), proves the bias actually reaches the sampler: a large
// positive bias must push the same draw onto level 1.

#include "sfpew_gtest.h"

#include <optional>
#include <vector>

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
constexpr GLenum GL_TRIANGLES_ = 0x0004;
constexpr GLenum GL_FLOAT_ = 0x1406;
constexpr GLenum GL_RGBA_ = 0x1908;
constexpr GLenum GL_UNSIGNED_BYTE_ = 0x1401;
constexpr GLenum GL_TEXTURE_2D_ = 0x0DE1;
constexpr GLenum GL_TEXTURE_MIN_FILTER_ = 0x2801;
constexpr GLenum GL_TEXTURE_MAG_FILTER_ = 0x2800;
constexpr GLenum GL_NEAREST_ = 0x2600;
constexpr GLenum GL_NEAREST_MIPMAP_NEAREST_ = 0x2700;
constexpr GLenum GL_TEXTURE_MAX_LEVEL_ = 0x813D;
constexpr GLenum GL_TEXTURE_FILTER_CONTROL_ = 0x8500;
constexpr GLenum GL_TEXTURE_LOD_BIAS_ = 0x8501;
constexpr GLenum GL_VERTEX_ARRAY_ = 0x8074;
constexpr GLenum GL_TEXTURE_COORD_ARRAY_ = 0x8078;
constexpr GLenum GL_NO_ERROR_ = 0;

using LodBiasTest = ContextTest;

TEST_F(LodBiasTest, PositiveBiasPushesTheSampleOntoTheNextMipLevel) {
    auto clear_color = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glClearColor");
    auto clear = Get<void (*)(GLbitfield)>("glClear");
    auto viewport = Get<void (*)(GLint, GLint, GLsizei, GLsizei)>("glViewport");
    auto draw_arrays = Get<void (*)(GLenum, GLint, GLsizei)>("glDrawArrays");
    auto get_error = Get<GLenum (*)()>("glGetError");
    auto finish = Get<void (*)()>("glFinish");
    auto enable = Get<void (*)(GLenum)>("glEnable");
    auto enable_client_state = Get<void (*)(GLenum)>("glEnableClientState");
    auto disable_client_state = Get<void (*)(GLenum)>("glDisableClientState");
    auto vertex_pointer = Get<void (*)(GLint, GLenum, GLsizei, const void*)>("glVertexPointer");
    auto tex_coord_pointer =
        Get<void (*)(GLint, GLenum, GLsizei, const void*)>("glTexCoordPointer");
    auto gen_textures = Get<void (*)(GLsizei, GLuint*)>("glGenTextures");
    auto bind_texture = Get<void (*)(GLenum, GLuint)>("glBindTexture");
    auto tex_image2d =
        Get<void (*)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*)>(
            "glTexImage2D");
    auto tex_parameteri = Get<void (*)(GLenum, GLenum, GLint)>("glTexParameteri");
    auto tex_envf = Get<void (*)(GLenum, GLenum, GLfloat)>("glTexEnvf");
    auto color4f = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glColor4f");
    auto read_pixels =
        Get<void (*)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*)>("glReadPixels");
    ASSERT_NE(read_pixels, nullptr);
    PixelProbe probe(read_pixels);

    // Native texel:pixel ratio: the quad exactly fills a viewport the same
    // size as level 0, so the implicit LOD is 0 by construction and bias 0
    // must sample level 0 - the baseline this test checks before trusting
    // any bias effect at all.
    const int tex_size = size();
    viewport(0, 0, tex_size, tex_size);

    std::vector<GLubyte> level0(static_cast<size_t>(tex_size) * tex_size * 4);
    for (size_t i = 0; i < level0.size(); i += 4) {
        level0[i + 0] = 255;
        level0[i + 1] = 0;
        level0[i + 2] = 0;
        level0[i + 3] = 255;
    }
    const int level1_size = tex_size / 2;
    std::vector<GLubyte> level1(static_cast<size_t>(level1_size) * level1_size * 4);
    for (size_t i = 0; i < level1.size(); i += 4) {
        level1[i + 0] = 0;
        level1[i + 1] = 255;
        level1[i + 2] = 0;
        level1[i + 3] = 255;
    }

    GLuint tex = 0;
    gen_textures(1, &tex);
    bind_texture(GL_TEXTURE_2D_, tex);
    tex_parameteri(GL_TEXTURE_2D_, GL_TEXTURE_MIN_FILTER_, GL_NEAREST_MIPMAP_NEAREST_);
    tex_parameteri(GL_TEXTURE_2D_, GL_TEXTURE_MAG_FILTER_, GL_NEAREST_);
    tex_parameteri(GL_TEXTURE_2D_, GL_TEXTURE_MAX_LEVEL_, 1);
    tex_image2d(GL_TEXTURE_2D_, 0, GL_RGBA_, tex_size, tex_size, 0, GL_RGBA_, GL_UNSIGNED_BYTE_,
               level0.data());
    tex_image2d(GL_TEXTURE_2D_, 1, GL_RGBA_, level1_size, level1_size, 0, GL_RGBA_,
               GL_UNSIGNED_BYTE_, level1.data());
    enable(GL_TEXTURE_2D_);

    static const GLfloat pos[] = {-1, -1, 1, -1, 1, 1, -1, -1, 1, 1, -1, 1};
    static const GLfloat uv[] = {0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1};
    vertex_pointer(2, GL_FLOAT_, 0, pos);
    tex_coord_pointer(2, GL_FLOAT_, 0, uv);
    enable_client_state(GL_VERTEX_ARRAY_);
    enable_client_state(GL_TEXTURE_COORD_ARRAY_);
    color4f(1.0f, 1.0f, 1.0f, 1.0f);
    clear_color(0.0f, 0.0f, 0.0f, 1.0f);

    const auto draw_and_probe = [&] {
        clear(GL_COLOR_BUFFER_BIT_);
        draw_arrays(GL_TRIANGLES_, 0, 6);
        finish();
        return probe.At(tex_size / 2, tex_size / 2);
    };

    const auto baseline = draw_and_probe();
    ASSERT_GT(baseline.r, 200) << "bias 0 at native resolution must sample level 0 (red)";
    ASSERT_LT(baseline.g, 60);

    tex_envf(GL_TEXTURE_FILTER_CONTROL_, GL_TEXTURE_LOD_BIAS_, 4.0f);
    const auto biased = draw_and_probe();
    EXPECT_LT(biased.r, 60) << "a large positive LOD bias must push the sample onto level 1 (green)";
    EXPECT_GT(biased.g, 200);

    // Back to 0: the effect must be parameterized, not a one-way latch.
    tex_envf(GL_TEXTURE_FILTER_CONTROL_, GL_TEXTURE_LOD_BIAS_, 0.0f);
    const auto reset = draw_and_probe();
    EXPECT_GT(reset.r, 200) << "resetting the bias to 0 must sample level 0 (red) again";
    EXPECT_LT(reset.g, 60);

    disable_client_state(GL_TEXTURE_COORD_ARRAY_);
    disable_client_state(GL_VERTEX_ARRAY_);
    EXPECT_EQ(get_error(), GL_NO_ERROR_);
}

} // namespace
