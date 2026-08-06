// SimpleFPEWrapper - tests/gtest_manual_mipgen.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// glGenerateMipmap is not a passthrough: after the backend call the wrapper
// rebuilds the 2D chain level by level with blits, because the mobile driver
// was caught leaving the HIGH levels of an NPOT render-target chain STALE -
// level 4 tracked the freshly rendered base while level 8 still held an image
// from seconds earlier. A shader pack's auto-exposure averaged the scene from
// level 6 of that chain, so the leftovers crushed the exposure and the screen
// went dead dark from some view angles.
//
// The repair must not corrupt what a healthy driver produces, so on a desktop
// GL stack this checks, for an NPOT texture sized to hit odd level dimensions:
//  - every level of the chain reads back the base's solid color;
//  - a SECOND upload + mipgen replaces every level (no stale content - on the
//    device this is the part the driver got wrong);
//  - framebuffer bindings and the scissor switch survive the wrapped call.
//
// The "Only" case sets SFPEW_MANUAL_MIPGEN=only, which skips the backend
// mipgen entirely - the stale-driver simulation - so ONLY the wrapper's blit
// chain can replace the garbage levels.

#include "sfpew_gtest.h"

#include <cstdlib>
#include <optional>

namespace {

using sfpew_test::ContextTest;
using sfpew_test::GLbitfield;
using sfpew_test::GLboolean;
using sfpew_test::GLenum;
using sfpew_test::GLint;
using sfpew_test::GLsizei;
using sfpew_test::GLubyte;
using sfpew_test::GLuint;

constexpr GLenum GL_TEXTURE_2D_ = 0x0DE1;
constexpr GLenum GL_TEXTURE_MIN_FILTER_ = 0x2801;
constexpr GLenum GL_TEXTURE_MAG_FILTER_ = 0x2800;
constexpr GLenum GL_LINEAR_ = 0x2601;
constexpr GLenum GL_LINEAR_MIPMAP_LINEAR_ = 0x2703;
constexpr GLenum GL_RGBA_ = 0x1908;
constexpr GLenum GL_UNSIGNED_BYTE_ = 0x1401;
constexpr GLenum GL_READ_FRAMEBUFFER_ = 0x8CA8;
constexpr GLenum GL_COLOR_ATTACHMENT0_ = 0x8CE0;
constexpr GLenum GL_READ_FRAMEBUFFER_BINDING_ = 0x8CAA;
constexpr GLenum GL_DRAW_FRAMEBUFFER_BINDING_ = 0x8CA6;
constexpr GLenum GL_SCISSOR_TEST_ = 0x0C11;
constexpr GLenum GL_NO_ERROR_ = 0;

constexpr int kTexW = 300;
constexpr int kTexH = 135;

class ManualMipgenTest : public ContextTest {
protected:
    void SetUp() override {
        ContextTest::SetUp();
        if (::testing::Test::IsSkipped()) return;

        gen_textures_ = Get<void (*)(GLsizei, GLuint*)>("glGenTextures");
        bind_texture_ = Get<void (*)(GLenum, GLuint)>("glBindTexture");
        tex_image2d_ =
            Get<void (*)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum,
                         const void*)>("glTexImage2D");
        tex_parameteri_ = Get<void (*)(GLenum, GLenum, GLint)>("glTexParameteri");
        generate_mipmap_ = Get<void (*)(GLenum)>("glGenerateMipmap");
        gen_framebuffers_ = Get<void (*)(GLsizei, GLuint*)>("glGenFramebuffers");
        bind_framebuffer_ = Get<void (*)(GLenum, GLuint)>("glBindFramebuffer");
        framebuffer_texture2d_ =
            Get<void (*)(GLenum, GLenum, GLenum, GLuint, GLint)>("glFramebufferTexture2D");
        read_pixels_ =
            Get<void (*)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*)>("glReadPixels");
        get_integerv_ = Get<void (*)(GLenum, GLint*)>("glGetIntegerv");
        enable_ = Get<void (*)(GLenum)>("glEnable");
        is_enabled_ = Get<GLboolean (*)(GLenum)>("glIsEnabled");
        get_error_ = Get<GLenum (*)()>("glGetError");
        finish_ = Get<void (*)()>("glFinish");
        ASSERT_NE(read_pixels_, nullptr);
    }

    void FillLevel0(GLubyte r, GLubyte g, GLubyte b) {
        for (int i = 0; i < kTexW * kTexH; ++i) {
            level0_[i * 4 + 0] = r;
            level0_[i * 4 + 1] = g;
            level0_[i * 4 + 2] = b;
            level0_[i * 4 + 3] = 255;
        }
    }

    // Solid input, so every level of a correct chain reads the same color
    // to within blit rounding.
    void ExpectChain(GLuint fbo, GLuint tex, GLubyte r, GLubyte g, GLubyte b, const char* what) {
        bind_framebuffer_(GL_READ_FRAMEBUFFER_, fbo);
        int w = kTexW, h = kTexH;
        for (int level = 1; w > 1 || h > 1; ++level) {
            w = w > 1 ? w >> 1 : 1;
            h = h > 1 ? h >> 1 : 1;
            framebuffer_texture2d_(GL_READ_FRAMEBUFFER_, GL_COLOR_ATTACHMENT0_, GL_TEXTURE_2D_,
                                   tex, level);
            GLubyte p[4] = {0, 0, 0, 0};
            read_pixels_(w / 2, h / 2, 1, 1, GL_RGBA_, GL_UNSIGNED_BYTE_, p);
            EXPECT_TRUE(p[0] > r - 12 && p[0] < r + 12 && p[1] > g - 12 && p[1] < g + 12 &&
                        p[2] > b - 12 && p[2] < b + 12)
                << what << ": level " << level << " = (" << (int)p[0] << ',' << (int)p[1] << ','
                << (int)p[2] << "), expected ~(" << (int)r << ',' << (int)g << ',' << (int)b
                << ')';
        }
        framebuffer_texture2d_(GL_READ_FRAMEBUFFER_, GL_COLOR_ATTACHMENT0_, GL_TEXTURE_2D_, 0, 0);
        bind_framebuffer_(GL_READ_FRAMEBUFFER_, 0);
    }

    void RunChain() {
        GLuint tex = 0;
        gen_textures_(1, &tex);
        bind_texture_(GL_TEXTURE_2D_, tex);
        tex_parameteri_(GL_TEXTURE_2D_, GL_TEXTURE_MIN_FILTER_, GL_LINEAR_MIPMAP_LINEAR_);
        tex_parameteri_(GL_TEXTURE_2D_, GL_TEXTURE_MAG_FILTER_, GL_LINEAR_);
        // Allocate every level up front, filled with magenta garbage. With
        // SFPEW_MANUAL_MIPGEN=only the wrapper skips the backend mipgen
        // entirely, so ONLY the wrapper's blit chain can replace this garbage
        // - any level it misses fails the color checks.
        FillLevel0(255, 0, 255);
        {
            int w = kTexW, h = kTexH;
            for (int level = 1; w > 1 || h > 1; ++level) {
                w = w > 1 ? w >> 1 : 1;
                h = h > 1 ? h >> 1 : 1;
                tex_image2d_(GL_TEXTURE_2D_, level, GL_RGBA_, w, h, 0, GL_RGBA_,
                             GL_UNSIGNED_BYTE_, level0_);
            }
        }
        GLuint fbo = 0;
        gen_framebuffers_(1, &fbo);
        // The wrapped call must put the framebuffer bindings and the scissor
        // switch back exactly as they were.
        enable_(GL_SCISSOR_TEST_);
        FillLevel0(200, 60, 20);
        tex_image2d_(GL_TEXTURE_2D_, 0, GL_RGBA_, kTexW, kTexH, 0, GL_RGBA_, GL_UNSIGNED_BYTE_,
                     level0_);
        // llvmpipe rebuilds the texture's storage at the next validation
        // point after the level-by-level definitions above, repopulating from
        // the uploads - which would silently discard whatever the repair
        // blits wrote in between. Force that validation FIRST with a
        // top-level read, the way the storage is already settled in the real
        // per-frame use (the levels exist long before each regeneration).
        {
            bind_framebuffer_(GL_READ_FRAMEBUFFER_, fbo);
            framebuffer_texture2d_(GL_READ_FRAMEBUFFER_, GL_COLOR_ATTACHMENT0_, GL_TEXTURE_2D_,
                                   tex, 8);
            GLubyte warm[4];
            read_pixels_(0, 0, 1, 1, GL_RGBA_, GL_UNSIGNED_BYTE_, warm);
            framebuffer_texture2d_(GL_READ_FRAMEBUFFER_, GL_COLOR_ATTACHMENT0_, GL_TEXTURE_2D_, 0,
                                   0);
            bind_framebuffer_(GL_READ_FRAMEBUFFER_, 0);
        }
        generate_mipmap_(GL_TEXTURE_2D_);
        finish_();
        ExpectChain(fbo, tex, 200, 60, 20, "first chain carries the base color to the top");
        // Re-upload and regenerate: every level must FOLLOW. Stale high
        // levels keeping the first color is exactly the device failure.
        FillLevel0(20, 180, 220);
        tex_image2d_(GL_TEXTURE_2D_, 0, GL_RGBA_, kTexW, kTexH, 0, GL_RGBA_, GL_UNSIGNED_BYTE_,
                     level0_);
        generate_mipmap_(GL_TEXTURE_2D_);
        finish_();
        ExpectChain(fbo, tex, 20, 180, 220, "regenerated chain follows the new base");
        EXPECT_NE(is_enabled_(GL_SCISSOR_TEST_), 0)
            << "scissor enable must survive glGenerateMipmap";
        GLint read_fb = -1, draw_fb = -1;
        get_integerv_(GL_READ_FRAMEBUFFER_BINDING_, &read_fb);
        get_integerv_(GL_DRAW_FRAMEBUFFER_BINDING_, &draw_fb);
        EXPECT_EQ(read_fb, 0) << "read framebuffer binding leaked";
        EXPECT_EQ(draw_fb, 0) << "draw framebuffer binding leaked";
        EXPECT_EQ(get_error_(), GL_NO_ERROR_);
    }

    void (*gen_textures_)(GLsizei, GLuint*) = nullptr;
    void (*bind_texture_)(GLenum, GLuint) = nullptr;
    void (*tex_image2d_)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum,
                         const void*) = nullptr;
    void (*tex_parameteri_)(GLenum, GLenum, GLint) = nullptr;
    void (*generate_mipmap_)(GLenum) = nullptr;
    void (*gen_framebuffers_)(GLsizei, GLuint*) = nullptr;
    void (*bind_framebuffer_)(GLenum, GLuint) = nullptr;
    void (*framebuffer_texture2d_)(GLenum, GLenum, GLenum, GLuint, GLint) = nullptr;
    void (*read_pixels_)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*) = nullptr;
    void (*get_integerv_)(GLenum, GLint*) = nullptr;
    void (*enable_)(GLenum) = nullptr;
    GLboolean (*is_enabled_)(GLenum) = nullptr;
    GLenum (*get_error_)() = nullptr;
    void (*finish_)() = nullptr;
    GLubyte level0_[kTexW * kTexH * 4];
};

TEST_F(ManualMipgenTest, WrappedCallRebuildsTheWholeChain) {
    RunChain();
}

class ManualMipgenOnlyTest : public ManualMipgenTest {
protected:
    void SetUp() override {
        // Set before any wrapper call so the getenv-based switch sees it.
        ASSERT_EQ(setenv("SFPEW_MANUAL_MIPGEN", "only", 1), 0);
        ManualMipgenTest::SetUp();
    }
};

TEST_F(ManualMipgenOnlyTest, BlitChainAloneProducesEveryLevel) {
    RunChain();
}

} // namespace
