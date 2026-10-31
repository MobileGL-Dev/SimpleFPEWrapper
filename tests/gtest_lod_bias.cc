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
// got sampled. A two-level texture with a solid, distinct color per level,
// sampled at its native texel:pixel ratio (bias 0 lands on level 0 by
// construction), proves the bias actually reaches the sampler: a large
// positive bias must push the same draw onto level 1.
//
// GL 1.4's per-texture GL_TEXTURE_LOD_BIAS goes the same way: GLES has no
// such texture parameter, so the wrapper keeps it and adds it to the unit's
// bias in the shader - GL samples with the sum of the two.

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

class LodBiasTest : public ContextTest {
protected:
    void SetUp() override {
        ContextTest::SetUp();
        if (IsSkipped() || HasFatalFailure()) return;
        clear_ = Get<void (*)(GLbitfield)>("glClear");
        draw_arrays_ = Get<void (*)(GLenum, GLint, GLsizei)>("glDrawArrays");
        get_error_ = Get<GLenum (*)()>("glGetError");
        finish_ = Get<void (*)()>("glFinish");
        gen_textures_ = Get<void (*)(GLsizei, GLuint*)>("glGenTextures");
        bind_texture_ = Get<void (*)(GLenum, GLuint)>("glBindTexture");
        tex_image2d_ =
            Get<void (*)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*)>(
                "glTexImage2D");
        tex_parameteri_ = Get<void (*)(GLenum, GLenum, GLint)>("glTexParameteri");
        tex_parameterf_ = Get<void (*)(GLenum, GLenum, GLfloat)>("glTexParameterf");
        get_tex_parameterfv_ = Get<void (*)(GLenum, GLenum, GLfloat*)>("glGetTexParameterfv");
        tex_envf_ = Get<void (*)(GLenum, GLenum, GLfloat)>("glTexEnvf");
        auto read_pixels =
            Get<void (*)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*)>("glReadPixels");
        ASSERT_NE(read_pixels, nullptr);
        probe_.emplace(read_pixels);

        // Native texel:pixel ratio: the quad exactly fills a viewport the same
        // size as level 0, so the implicit LOD is 0 by construction and bias 0
        // must sample level 0 - the baseline every case checks before trusting
        // any bias effect at all.
        Get<void (*)(GLint, GLint, GLsizei, GLsizei)>("glViewport")(0, 0, size(), size());
        static const GLfloat pos[] = {-1, -1, 1, -1, 1, 1, -1, -1, 1, 1, -1, 1};
        static const GLfloat uv[] = {0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1};
        Get<void (*)(GLint, GLenum, GLsizei, const void*)>("glVertexPointer")(2, GL_FLOAT_, 0, pos);
        Get<void (*)(GLint, GLenum, GLsizei, const void*)>("glTexCoordPointer")(2, GL_FLOAT_, 0, uv);
        auto enable_client_state = Get<void (*)(GLenum)>("glEnableClientState");
        enable_client_state(GL_VERTEX_ARRAY_);
        enable_client_state(GL_TEXTURE_COORD_ARRAY_);
        Get<void (*)(GLenum)>("glEnable")(GL_TEXTURE_2D_);
        Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glColor4f")(1.0f, 1.0f, 1.0f, 1.0f);
        Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glClearColor")(0.0f, 0.0f, 0.0f, 1.0f);
    }

    // A two-level texture, level 0 red and level 1 green, bound.
    GLuint MakeTexture() {
        const int tex_size = size();
        std::vector<GLubyte> level0(static_cast<size_t>(tex_size) * tex_size * 4);
        for (size_t i = 0; i < level0.size(); i += 4) {
            level0[i + 0] = 255;
            level0[i + 3] = 255;
        }
        const int level1_size = tex_size / 2;
        std::vector<GLubyte> level1(static_cast<size_t>(level1_size) * level1_size * 4);
        for (size_t i = 0; i < level1.size(); i += 4) {
            level1[i + 1] = 255;
            level1[i + 3] = 255;
        }
        GLuint tex = 0;
        gen_textures_(1, &tex);
        bind_texture_(GL_TEXTURE_2D_, tex);
        tex_parameteri_(GL_TEXTURE_2D_, GL_TEXTURE_MIN_FILTER_, GL_NEAREST_MIPMAP_NEAREST_);
        tex_parameteri_(GL_TEXTURE_2D_, GL_TEXTURE_MAG_FILTER_, GL_NEAREST_);
        tex_parameteri_(GL_TEXTURE_2D_, GL_TEXTURE_MAX_LEVEL_, 1);
        tex_image2d_(GL_TEXTURE_2D_, 0, GL_RGBA_, tex_size, tex_size, 0, GL_RGBA_, GL_UNSIGNED_BYTE_,
                     level0.data());
        tex_image2d_(GL_TEXTURE_2D_, 1, GL_RGBA_, level1_size, level1_size, 0, GL_RGBA_,
                     GL_UNSIGNED_BYTE_, level1.data());
        return tex;
    }

    // True when the centre pixel came from level 1 (green), false for level 0
    // (red); fails the test on anything else.
    bool SamplesLevelOne(const char* tag) {
        clear_(GL_COLOR_BUFFER_BIT_);
        draw_arrays_(GL_TRIANGLES_, 0, 6);
        finish_();
        const auto px = probe_->At(size() / 2, size() / 2);
        const bool red = px.r > 200 && px.g < 60, green = px.g > 200 && px.r < 60;
        EXPECT_TRUE(red || green) << tag << ": neither level (" << int(px.r) << ", " << int(px.g) << ")";
        return green;
    }

    void (*clear_)(GLbitfield) = nullptr;
    void (*draw_arrays_)(GLenum, GLint, GLsizei) = nullptr;
    GLenum (*get_error_)() = nullptr;
    void (*finish_)() = nullptr;
    void (*gen_textures_)(GLsizei, GLuint*) = nullptr;
    void (*bind_texture_)(GLenum, GLuint) = nullptr;
    void (*tex_image2d_)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*) = nullptr;
    void (*tex_parameteri_)(GLenum, GLenum, GLint) = nullptr;
    void (*tex_parameterf_)(GLenum, GLenum, GLfloat) = nullptr;
    void (*get_tex_parameterfv_)(GLenum, GLenum, GLfloat*) = nullptr;
    void (*tex_envf_)(GLenum, GLenum, GLfloat) = nullptr;
    std::optional<PixelProbe> probe_;
};

TEST_F(LodBiasTest, PositiveBiasPushesTheSampleOntoTheNextMipLevel) {
    MakeTexture();
    ASSERT_FALSE(SamplesLevelOne("bias 0")) << "bias 0 at native resolution must sample level 0 (red)";
    tex_envf_(GL_TEXTURE_FILTER_CONTROL_, GL_TEXTURE_LOD_BIAS_, 4.0f);
    EXPECT_TRUE(SamplesLevelOne("unit bias 4")) << "a large positive LOD bias must push the sample onto level 1";
    // Back to 0: the effect must be parameterized, not a one-way latch.
    tex_envf_(GL_TEXTURE_FILTER_CONTROL_, GL_TEXTURE_LOD_BIAS_, 0.0f);
    EXPECT_FALSE(SamplesLevelOne("unit bias back to 0")) << "resetting the bias to 0 must sample level 0 again";
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

// The texture object's own bias: accepted and read back, added to the
// unit's, and carried by that texture only.
TEST_F(LodBiasTest, TextureBiasAddsToTheUnitBiasForThatTextureOnly) {
    const GLuint biased = MakeTexture();
    tex_parameterf_(GL_TEXTURE_2D_, GL_TEXTURE_LOD_BIAS_, 4.0f);
    ASSERT_EQ(get_error_(), GL_NO_ERROR_) << "GL_TEXTURE_LOD_BIAS is a GL 1.4 texture parameter";
    GLfloat value = 0.0f;
    get_tex_parameterfv_(GL_TEXTURE_2D_, GL_TEXTURE_LOD_BIAS_, &value);
    EXPECT_EQ(value, 4.0f);
    EXPECT_TRUE(SamplesLevelOne("texture bias 4")) << "the texture's bias did not reach the sampler";

    tex_envf_(GL_TEXTURE_FILTER_CONTROL_, GL_TEXTURE_LOD_BIAS_, -4.0f);
    EXPECT_FALSE(SamplesLevelOne("texture 4 + unit -4")) << "the two biases must sum, to 0 here";
    tex_envf_(GL_TEXTURE_FILTER_CONTROL_, GL_TEXTURE_LOD_BIAS_, 0.0f);

    MakeTexture();
    EXPECT_FALSE(SamplesLevelOne("another texture")) << "the bias belongs to the texture that was set";
    bind_texture_(GL_TEXTURE_2D_, biased);
    EXPECT_TRUE(SamplesLevelOne("the biased texture again"));
    tex_parameterf_(GL_TEXTURE_2D_, GL_TEXTURE_LOD_BIAS_, 0.0f);
    EXPECT_FALSE(SamplesLevelOne("texture bias back to 0"));
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

} // namespace
