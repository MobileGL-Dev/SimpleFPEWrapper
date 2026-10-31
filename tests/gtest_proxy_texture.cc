// SimpleFPEWrapper - tests/gtest_proxy_texture.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// GL_PROXY_TEXTURE_2D asks whether an allocation would be accepted, without
// making one. An unsupported size is an answer (the proxy's level width
// reads 0), never an error - Minecraft probes its largest atlas exactly this
// way, from 32768 down. GLES has no proxy targets, so the wrapper answers
// from its own bookkeeping and must not hand the call to the backend.

#include "sfpew_gtest.h"

namespace {

using sfpew_test::ContextTest;
using sfpew_test::GLenum;
using sfpew_test::GLint;
using sfpew_test::GLsizei;

constexpr GLenum GL_PROXY_TEXTURE_2D_ = 0x8064;
constexpr GLenum GL_TEXTURE_WIDTH_ = 0x1000;
constexpr GLenum GL_RGBA_ = 0x1908;
constexpr GLenum GL_UNSIGNED_BYTE_ = 0x1401;
constexpr GLenum GL_MAX_TEXTURE_SIZE_ = 0x0D33;
constexpr GLenum GL_NO_ERROR_ = 0;

using ProxyTextureTest = ContextTest;

TEST_F(ProxyTextureTest, ProbingAnySizeAnswersThroughTheLevelWidthWithoutAnError) {
    auto tex_image2d = Get<void (*)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*)>(
        "glTexImage2D");
    auto level_parameteriv = Get<void (*)(GLenum, GLint, GLenum, GLint*)>("glGetTexLevelParameteriv");
    auto get_integerv = Get<void (*)(GLenum, GLint*)>("glGetIntegerv");
    auto get_error = Get<GLenum (*)()>("glGetError");
    get_error();
    GLint max_size = 0;
    get_integerv(GL_MAX_TEXTURE_SIZE_, &max_size);
    ASSERT_GT(max_size, 0);

    // Minecraft's probe sequence: halving from 32768 until one fits.
    for (GLsizei probe = 32768; probe >= 64; probe /= 2) {
        tex_image2d(GL_PROXY_TEXTURE_2D_, 0, GL_RGBA_, probe, probe, 0, GL_RGBA_, GL_UNSIGNED_BYTE_, nullptr);
        EXPECT_EQ(get_error(), GL_NO_ERROR_) << "probing " << probe << " raised an error";
        GLint width = -1;
        level_parameteriv(GL_PROXY_TEXTURE_2D_, 0, GL_TEXTURE_WIDTH_, &width);
        EXPECT_EQ(width, probe <= max_size ? probe : 0) << "probe " << probe << " (max " << max_size << ")";
    }
}

} // namespace
