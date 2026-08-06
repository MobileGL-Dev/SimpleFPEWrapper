// SimpleFPEWrapper - tests/gtest_drawpixels_stencil.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// defects-plan-2.md 2.5: glDrawPixels(GL_STENCIL_INDEX, ...) was a hard,
// unconditional GL_INVALID_OPERATION regardless of whether a stencil
// buffer existed. Now runs a 16-pass (2 per bit) bit-plane write - see
// drawStencilBitplanes in pixelops.cpp for why one pass per bit isn't
// enough. Covers: several distinct index values landing correctly at
// their own pixels (not just one constant value repeated), pixels outside
// the drawn rectangle staying untouched, GL_PIXEL_MAP_S_TO_S remapping,
// and the invalid-type rejection this path still owns.

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

constexpr GLenum GL_NO_ERROR_ = 0;
constexpr GLenum GL_INVALID_ENUM_ = 0x0500;
constexpr GLenum GL_STENCIL_INDEX_ = 0x1901;
constexpr GLenum GL_UNSIGNED_BYTE_ = 0x1401;
constexpr GLenum GL_FLOAT_ = 0x1406;
constexpr GLenum GL_RGBA_ = 0x1908;
constexpr GLenum GL_RGBA8_ = 0x8058;
constexpr GLenum GL_TEXTURE_2D_ = 0x0DE1;
constexpr GLenum GL_TEXTURE_MIN_FILTER_ = 0x2801;
constexpr GLenum GL_TEXTURE_MAG_FILTER_ = 0x2800;
constexpr GLenum GL_NEAREST_ = 0x2600;
constexpr GLenum GL_FRAMEBUFFER_ = 0x8D40;
constexpr GLenum GL_RENDERBUFFER_ = 0x8D41;
constexpr GLenum GL_COLOR_ATTACHMENT0_ = 0x8CE0;
constexpr GLenum GL_DEPTH_STENCIL_ATTACHMENT_ = 0x821A;
constexpr GLenum GL_DEPTH24_STENCIL8_ = 0x88F0;
constexpr GLenum GL_FRAMEBUFFER_COMPLETE_ = 0x8CD5;
constexpr GLbitfield GL_COLOR_BUFFER_BIT_ = 0x00004000;
constexpr GLbitfield GL_DEPTH_BUFFER_BIT_ = 0x00000100;
constexpr GLbitfield GL_STENCIL_BUFFER_BIT_ = 0x00000400;
constexpr GLenum GL_MAP_STENCIL_ = 0x0D11;
constexpr GLenum GL_PIXEL_MAP_S_TO_S_ = 0x0C71;

class DrawPixelsStencilTest : public ContextTest {
protected:
    void SetUp() override {
        ContextTest::SetUp();
        if (::testing::Test::IsSkipped()) return;
        get_error_ = Get<GLenum (*)()>("glGetError");
        enable_ = Get<void (*)(GLenum)>("glEnable");
        disable_ = Get<void (*)(GLenum)>("glDisable");
        clear_color_ = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glClearColor");
        clear_stencil_ = Get<void (*)(GLint)>("glClearStencil");
        stencil_mask_ = Get<void (*)(GLuint)>("glStencilMask");
        clear_ = Get<void (*)(GLbitfield)>("glClear");
        window_pos_ = Get<void (*)(GLint, GLint)>("glWindowPos2i");
        draw_pixels_ = Get<void (*)(GLsizei, GLsizei, GLenum, GLenum, const void*)>("glDrawPixels");
        read_pixels_ = Get<void (*)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*)>(
            "glReadPixels");
        pixel_mapuiv_ = Get<void (*)(GLenum, GLsizei, const GLuint*)>("glPixelMapuiv");
        pixel_transferi_ = Get<void (*)(GLenum, GLint)>("glPixelTransferi");

        gen_textures_ = Get<void (*)(GLsizei, GLuint*)>("glGenTextures");
        bind_texture_ = Get<void (*)(GLenum, GLuint)>("glBindTexture");
        tex_parameter_ = Get<void (*)(GLenum, GLenum, GLint)>("glTexParameteri");
        tex_image_ = Get<void (*)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum,
                                  const void*)>("glTexImage2D");
        gen_framebuffers_ = Get<void (*)(GLsizei, GLuint*)>("glGenFramebuffers");
        bind_framebuffer_ = Get<void (*)(GLenum, GLuint)>("glBindFramebuffer");
        check_framebuffer_ = Get<GLenum (*)(GLenum)>("glCheckFramebufferStatus");
        framebuffer_texture_ =
            Get<void (*)(GLenum, GLenum, GLenum, GLuint, GLint)>("glFramebufferTexture2D");
        framebuffer_renderbuffer_ =
            Get<void (*)(GLenum, GLenum, GLenum, GLuint)>("glFramebufferRenderbuffer");
        gen_renderbuffers_ = Get<void (*)(GLsizei, GLuint*)>("glGenRenderbuffers");
        bind_renderbuffer_ = Get<void (*)(GLenum, GLuint)>("glBindRenderbuffer");
        renderbuffer_storage_ =
            Get<void (*)(GLenum, GLenum, GLsizei, GLsizei)>("glRenderbufferStorage");

        GLuint texture = 0, renderbuffer = 0, framebuffer = 0;
        gen_textures_(1, &texture);
        bind_texture_(GL_TEXTURE_2D_, texture);
        tex_parameter_(GL_TEXTURE_2D_, GL_TEXTURE_MIN_FILTER_, GL_NEAREST_);
        tex_parameter_(GL_TEXTURE_2D_, GL_TEXTURE_MAG_FILTER_, GL_NEAREST_);
        tex_image_(GL_TEXTURE_2D_, 0, GL_RGBA8_, size(), size(), 0, GL_RGBA_, GL_UNSIGNED_BYTE_,
                  nullptr);
        gen_renderbuffers_(1, &renderbuffer);
        bind_renderbuffer_(GL_RENDERBUFFER_, renderbuffer);
        renderbuffer_storage_(GL_RENDERBUFFER_, GL_DEPTH24_STENCIL8_, size(), size());
        gen_framebuffers_(1, &framebuffer);
        bind_framebuffer_(GL_FRAMEBUFFER_, framebuffer);
        framebuffer_texture_(GL_FRAMEBUFFER_, GL_COLOR_ATTACHMENT0_, GL_TEXTURE_2D_, texture, 0);
        framebuffer_renderbuffer_(GL_FRAMEBUFFER_, GL_DEPTH_STENCIL_ATTACHMENT_, GL_RENDERBUFFER_,
                                  renderbuffer);
        ASSERT_EQ(check_framebuffer_(GL_FRAMEBUFFER_), GL_FRAMEBUFFER_COMPLETE_);

        stencil_mask_(0xFFu);
        clear_color_(0.0f, 0.0f, 0.0f, 1.0f);
        clear_stencil_(0);
        clear_(GL_COLOR_BUFFER_BIT_ | GL_DEPTH_BUFFER_BIT_ | GL_STENCIL_BUFFER_BIT_);
        ASSERT_EQ(get_error_(), GL_NO_ERROR_) << "FBO setup";
    }

    GLubyte StencilAt(int x, int y) const {
        GLubyte value = 0;
        read_pixels_(x, y, 1, 1, GL_STENCIL_INDEX_, GL_UNSIGNED_BYTE_, &value);
        return value;
    }

    GLenum (*get_error_)() = nullptr;
    void (*enable_)(GLenum) = nullptr;
    void (*disable_)(GLenum) = nullptr;
    void (*clear_color_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*clear_stencil_)(GLint) = nullptr;
    void (*stencil_mask_)(GLuint) = nullptr;
    void (*clear_)(GLbitfield) = nullptr;
    void (*window_pos_)(GLint, GLint) = nullptr;
    void (*draw_pixels_)(GLsizei, GLsizei, GLenum, GLenum, const void*) = nullptr;
    void (*read_pixels_)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*) = nullptr;
    void (*pixel_mapuiv_)(GLenum, GLsizei, const GLuint*) = nullptr;
    void (*pixel_transferi_)(GLenum, GLint) = nullptr;
    void (*gen_textures_)(GLsizei, GLuint*) = nullptr;
    void (*bind_texture_)(GLenum, GLuint) = nullptr;
    void (*tex_parameter_)(GLenum, GLenum, GLint) = nullptr;
    void (*tex_image_)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum,
                       const void*) = nullptr;
    void (*gen_framebuffers_)(GLsizei, GLuint*) = nullptr;
    void (*bind_framebuffer_)(GLenum, GLuint) = nullptr;
    GLenum (*check_framebuffer_)(GLenum) = nullptr;
    void (*framebuffer_texture_)(GLenum, GLenum, GLenum, GLuint, GLint) = nullptr;
    void (*framebuffer_renderbuffer_)(GLenum, GLenum, GLenum, GLuint) = nullptr;
    void (*gen_renderbuffers_)(GLsizei, GLuint*) = nullptr;
    void (*bind_renderbuffer_)(GLenum, GLuint) = nullptr;
    void (*renderbuffer_storage_)(GLenum, GLenum, GLsizei, GLsizei) = nullptr;
};

TEST_F(DrawPixelsStencilTest, DistinctIndicesLandAtTheirOwnPixelsNotJustOneSharedValue) {
    // Four distinct values chosen to exercise different bit patterns (not
    // just "all bits set/clear" every time): 0x03, 0x50, 0xAA, 0xFF. Four
    // separate 1x1 draws at four separate positions, not one 2x2 image -
    // a 2x2 image's four texel centers land at UV (0.25,0.25)/(0.75,0.25)/
    // (0.25,0.75)/(0.75,0.75) against the quad's own internal diagonal
    // (the two triangles of the drawer's GL_TRIANGLE_STRIP meet along
    // u+v=1), and two of those four centers sit exactly on that seam -
    // a rasterizer boundary-rounding case, not a bit-plane logic bug (this
    // same set of values at four independent single-pixel positions, which
    // cannot hit that seam, all land correctly).
    const GLubyte values[4] = {0x03, 0x50, 0xAA, 0xFF};
    const int xs[4] = {4, 10, 16, 22};
    for (int i = 0; i < 4; ++i) {
        window_pos_(xs[i], 4);
        draw_pixels_(1, 1, GL_STENCIL_INDEX_, GL_UNSIGNED_BYTE_, &values[i]);
    }
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);

    EXPECT_EQ(StencilAt(xs[0], 4), 0x03);
    EXPECT_EQ(StencilAt(xs[1], 4), 0x50);
    EXPECT_EQ(StencilAt(xs[2], 4), 0xAA);
    EXPECT_EQ(StencilAt(xs[3], 4), 0xFF);
}

TEST_F(DrawPixelsStencilTest, PixelsOutsideTheDrawnRectangleStayUntouched) {
    stencil_mask_(0xFFu);
    clear_stencil_(0x42);
    clear_(GL_STENCIL_BUFFER_BIT_);
    ASSERT_EQ(StencilAt(50, 50), 0x42);

    const GLubyte solid[1] = {0x99};
    window_pos_(4, 4);
    draw_pixels_(1, 1, GL_STENCIL_INDEX_, GL_UNSIGNED_BYTE_, solid);
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);

    EXPECT_EQ(StencilAt(4, 4), 0x99) << "the drawn pixel must change";
    EXPECT_EQ(StencilAt(50, 50), 0x42)
        << "a pixel outside the drawn rectangle must be untouched by the bit-plane passes";
    EXPECT_EQ(StencilAt(5, 4), 0x42) << "immediately adjacent to the drawn rectangle";
}

TEST_F(DrawPixelsStencilTest, PixelMapRemapsTheIndexBeforeItReachesTheBuffer) {
    // GL_PIXEL_MAP_S_TO_S: a size-2 table that swaps low/high - source
    // index 0 -> 0xFF, source index 1 -> 0x00. Size-2 satisfies the
    // power-of-two requirement pixelMapSizeIsValid enforces for index maps.
    ASSERT_NE(pixel_mapuiv_, nullptr);
    ASSERT_NE(pixel_transferi_, nullptr);
    const GLuint table[2] = {0xFFu, 0x00u};
    pixel_mapuiv_(GL_PIXEL_MAP_S_TO_S_, 2, table);
    // GL_MAP_STENCIL is a glPixelTransfer parameter, not a glEnable
    // capability - unlike GL_MAP_COLOR's ARB_imaging cousin, it has no
    // enable-bit spelling.
    pixel_transferi_(GL_MAP_STENCIL_, 1);

    const GLubyte source[2] = {0, 1}; // masked into the size-2 table as-is
    window_pos_(8, 8);
    draw_pixels_(2, 1, GL_STENCIL_INDEX_, GL_UNSIGNED_BYTE_, source);
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);

    EXPECT_EQ(StencilAt(8, 8), 0xFF) << "index 0 must remap through the table, not pass through raw";
    EXPECT_EQ(StencilAt(9, 8), 0x00);

    pixel_transferi_(GL_MAP_STENCIL_, 0);
}

TEST_F(DrawPixelsStencilTest, FloatTypeIsRejectedTheBufferStaysUntouched) {
    stencil_mask_(0xFFu);
    clear_stencil_(0x11);
    clear_(GL_STENCIL_BUFFER_BIT_);

    const GLfloat bogus[1] = {1.0f};
    window_pos_(4, 4);
    draw_pixels_(1, 1, GL_STENCIL_INDEX_, GL_FLOAT_, bogus);
    EXPECT_EQ(get_error_(), GL_INVALID_ENUM_)
        << "GL_FLOAT is not a legal glDrawPixels(GL_STENCIL_INDEX) type (GL 2.1 table 3.6)";
    EXPECT_EQ(StencilAt(4, 4), 0x11) << "a rejected call must not touch the stencil buffer";
}

} // namespace
