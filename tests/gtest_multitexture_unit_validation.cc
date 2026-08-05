// SimpleFPEWrapper - tests/gtest_multitexture_unit_validation.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// glActiveTexture and glClientActiveTexture used to accept any GLenum
// without checking it against MAX_TEX. glActiveTexture's thread-local
// active-unit shadow would then adopt the out-of-range value even on a call
// the real backend itself rejects with GL_INVALID_ENUM (which per spec
// leaves the real active unit untouched), desyncing every later per-unit
// read from the real driver state. glClientActiveTexture was worse: its
// unchecked value flows straight into vp2idx()'s array index for
// glTexCoordPointer, an out-of-bounds write past the fixed
// attributes[VERTEX_POINTER_COUNT] array for any enum at or beyond
// GL_TEXTURE0+MAX_TEX.

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
constexpr GLenum GL_TRIANGLES_ = 0x0004;
constexpr GLenum GL_FLOAT_ = 0x1406;
constexpr GLenum GL_RGBA_ = 0x1908;
constexpr GLenum GL_UNSIGNED_BYTE_ = 0x1401;
constexpr GLenum GL_TEXTURE_2D_ = 0x0DE1;
constexpr GLenum GL_NEAREST_ = 0x2600;
constexpr GLenum GL_TEXTURE_MIN_FILTER_ = 0x2801;
constexpr GLenum GL_TEXTURE_MAG_FILTER_ = 0x2800;
constexpr GLenum GL_TEXTURE0_ = 0x84C0;
constexpr GLenum GL_VERTEX_ARRAY_ = 0x8074;
constexpr GLenum GL_TEXTURE_COORD_ARRAY_ = 0x8078;
constexpr GLenum GL_INVALID_ENUM_ = 0x0500;
constexpr GLenum GL_NO_ERROR_ = 0;

constexpr int kSize = 32;

class MultitextureUnitValidationTest : public ContextTest {
protected:
    MultitextureUnitValidationTest() : ContextTest(sfpew_test::Backend::GLES3, kSize) {}

    void SetUp() override {
        ContextTest::SetUp();
        if (::testing::Test::IsSkipped()) return;

        clear_color_ = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glClearColor");
        clear_ = Get<void (*)(GLbitfield)>("glClear");
        draw_arrays_ = Get<void (*)(GLenum, GLint, GLsizei)>("glDrawArrays");
        finish_ = Get<void (*)()>("glFinish");
        get_error_ = Get<GLenum (*)()>("glGetError");
        enable_ = Get<void (*)(GLenum)>("glEnable");
        active_texture_ = Get<void (*)(GLenum)>("glActiveTexture");
        client_active_texture_ = Get<void (*)(GLenum)>("glClientActiveTexture");
        gen_textures_ = Get<void (*)(GLsizei, GLuint*)>("glGenTextures");
        bind_texture_ = Get<void (*)(GLenum, GLuint)>("glBindTexture");
        tex_image2d_ =
            Get<void (*)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum,
                         const void*)>("glTexImage2D");
        tex_parameteri_ = Get<void (*)(GLenum, GLenum, GLint)>("glTexParameteri");
        enable_client_state_ = Get<void (*)(GLenum)>("glEnableClientState");
        vertex_pointer_ = Get<void (*)(GLint, GLenum, GLsizei, const void*)>("glVertexPointer");
        tex_coord_pointer_ =
            Get<void (*)(GLint, GLenum, GLsizei, const void*)>("glTexCoordPointer");
        read_pixels_ =
            Get<void (*)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*)>("glReadPixels");
        ASSERT_NE(read_pixels_, nullptr);
        get_error_();
    }

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

    void (*clear_color_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*clear_)(GLbitfield) = nullptr;
    void (*draw_arrays_)(GLenum, GLint, GLsizei) = nullptr;
    void (*finish_)() = nullptr;
    GLenum (*get_error_)() = nullptr;
    void (*enable_)(GLenum) = nullptr;
    void (*active_texture_)(GLenum) = nullptr;
    void (*client_active_texture_)(GLenum) = nullptr;
    void (*gen_textures_)(GLsizei, GLuint*) = nullptr;
    void (*bind_texture_)(GLenum, GLuint) = nullptr;
    void (*tex_image2d_)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum,
                         const void*) = nullptr;
    void (*tex_parameteri_)(GLenum, GLenum, GLint) = nullptr;
    void (*enable_client_state_)(GLenum) = nullptr;
    void (*vertex_pointer_)(GLint, GLenum, GLsizei, const void*) = nullptr;
    void (*tex_coord_pointer_)(GLint, GLenum, GLsizei, const void*) = nullptr;
    void (*read_pixels_)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*) = nullptr;
};

// An out-of-range unit must be rejected before the shadow adopts it - proven
// behaviorally: bind red to unit 0, issue the rejected call, then bind green
// "on the active unit" again. If the shadow had wrongly moved to the
// out-of-range value, this second bind would target a different (or
// crashing) unit and unit 0 would still read back red.
TEST_F(MultitextureUnitValidationTest, ActiveTextureRejectsOutOfRangeUnitAndLeavesStateUnchanged) {
    static const GLfloat pos[] = {-1, -1, 1, -1, 1, 1, -1, -1, 1, 1, -1, 1};
    static const GLfloat uv[] = {0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1};
    enable_client_state_(GL_VERTEX_ARRAY_);
    enable_client_state_(GL_TEXTURE_COORD_ARRAY_);
    vertex_pointer_(2, GL_FLOAT_, 0, pos);
    tex_coord_pointer_(2, GL_FLOAT_, 0, uv);

    active_texture_(GL_TEXTURE0_);
    MakeSolidTexture(255, 0, 0, 255);
    enable_(GL_TEXTURE_2D_);
    ASSERT_EQ(get_error_(), GL_NO_ERROR_);

    constexpr GLenum kWayOutOfRange = GL_TEXTURE0_ + 999u;
    active_texture_(kWayOutOfRange);
    EXPECT_EQ(get_error_(), GL_INVALID_ENUM_);

    // Still targeting unit 0 if the rejected call left the active unit
    // alone; binds and re-samples green there.
    MakeSolidTexture(0, 255, 0, 255);
    enable_(GL_TEXTURE_2D_);

    clear_color_(0.0f, 0.0f, 0.0f, 1.0f);
    clear_(GL_COLOR_BUFFER_BIT_);
    draw_arrays_(GL_TRIANGLES_, 0, 6);
    finish_();

    PixelProbe probe(read_pixels_);
    const auto pixel = probe.At(kSize / 2, kSize / 2);
    ASSERT_EQ(get_error_(), GL_NO_ERROR_);
    EXPECT_TRUE(pixel.g >= 200 && pixel.r <= 50)
        << "pixel (" << (int)pixel.r << ',' << (int)pixel.g << ',' << (int)pixel.b
        << ") expected green: the rejected glActiveTexture call must not have moved the active "
           "unit away from unit 0";
}

// The dangerous path is glClientActiveTexture's value flowing unclamped into
// vp2idx()'s array index. Rejecting it up front is the fix; this proves the
// rejection actually happens and that legitimate use immediately afterward
// is unaffected (no crash, no corrupted array state).
TEST_F(MultitextureUnitValidationTest, ClientActiveTextureRejectsOutOfRangeUnitWithoutCorruption) {
    constexpr GLenum kWayOutOfRange = GL_TEXTURE0_ + 999u;
    client_active_texture_(kWayOutOfRange);
    EXPECT_EQ(get_error_(), GL_INVALID_ENUM_);

    static const GLfloat pos[] = {-1, -1, 1, -1, 1, 1, -1, -1, 1, 1, -1, 1};
    static const GLfloat uv[] = {0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1};
    client_active_texture_(GL_TEXTURE0_);
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
    enable_client_state_(GL_VERTEX_ARRAY_);
    enable_client_state_(GL_TEXTURE_COORD_ARRAY_);
    vertex_pointer_(2, GL_FLOAT_, 0, pos);
    tex_coord_pointer_(2, GL_FLOAT_, 0, uv);

    active_texture_(GL_TEXTURE0_);
    MakeSolidTexture(0, 0, 255, 255);
    enable_(GL_TEXTURE_2D_);

    clear_color_(0.0f, 0.0f, 0.0f, 1.0f);
    clear_(GL_COLOR_BUFFER_BIT_);
    draw_arrays_(GL_TRIANGLES_, 0, 6);
    finish_();

    PixelProbe probe(read_pixels_);
    const auto pixel = probe.At(kSize / 2, kSize / 2);
    ASSERT_EQ(get_error_(), GL_NO_ERROR_);
    EXPECT_TRUE(pixel.b >= 200 && pixel.r <= 50 && pixel.g <= 50)
        << "pixel (" << (int)pixel.r << ',' << (int)pixel.g << ',' << (int)pixel.b
        << ") expected blue: unit 0's texcoord array must still work correctly after the "
           "rejected out-of-range glClientActiveTexture call";
}

} // namespace
