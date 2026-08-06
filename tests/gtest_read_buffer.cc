// SimpleFPEWrapper - tests/gtest_read_buffer.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// defects-plan-2.md 2.1: glReadBuffer was entirely unwrapped - not
// resolvable through eglGetProcAddress's own table, falling through
// straight to the backend and bypassing display-list capture. Covers enum
// validation (mirroring glDrawBuffer's contract, but GL_FRONT_AND_BACK is
// illegal here unlike there), the GL_READ_BUFFER query, display-list
// capture/replay, and an actual multi-attachment FBO proving the selected
// attachment's content is really what glReadPixels returns.

#include "sfpew_gtest.h"

#include <optional>

namespace {

using sfpew_test::ContextTest;
using sfpew_test::GLenum;
using sfpew_test::GLfloat;
using sfpew_test::GLint;
using sfpew_test::GLsizei;
using sfpew_test::GLuint;
using sfpew_test::PixelProbe;

constexpr GLenum GL_NO_ERROR_ = 0;
constexpr GLenum GL_INVALID_ENUM_ = 0x0500;
constexpr GLenum GL_INVALID_OPERATION_ = 0x0502;
constexpr GLenum GL_FRONT_ = 0x0404;
constexpr GLenum GL_BACK_ = 0x0405;
constexpr GLenum GL_FRONT_AND_BACK_ = 0x0408;
constexpr GLenum GL_READ_BUFFER_ = 0x0C02;
constexpr GLenum GL_FRAMEBUFFER_ = 0x8D40;
constexpr GLenum GL_READ_FRAMEBUFFER_ = 0x8CA8;
constexpr GLenum GL_DRAW_FRAMEBUFFER_ = 0x8CA9;
constexpr GLenum GL_COLOR_ATTACHMENT0_ = 0x8CE0;
constexpr GLenum GL_COLOR_ATTACHMENT1_ = 0x8CE1;
constexpr GLenum GL_FRAMEBUFFER_COMPLETE_ = 0x8CD5;
constexpr GLenum GL_TEXTURE_2D_ = 0x0DE1;
constexpr GLenum GL_RGBA_ = 0x1908;
constexpr GLenum GL_RGBA8_ = 0x8058;
constexpr GLenum GL_UNSIGNED_BYTE_ = 0x1401;
constexpr GLenum GL_TEXTURE_MIN_FILTER_ = 0x2801;
constexpr GLenum GL_TEXTURE_MAG_FILTER_ = 0x2800;
constexpr GLenum GL_NEAREST_ = 0x2600;
constexpr GLenum GL_COMPILE_ = 0x1300;
constexpr sfpew_test::GLbitfield GL_COLOR_BUFFER_BIT_ = 0x00004000;

class ReadBufferTest : public ContextTest {
protected:
    void SetUp() override {
        ContextTest::SetUp();
        if (::testing::Test::IsSkipped()) return;
        read_buffer_ = Get<void (*)(GLenum)>("glReadBuffer");
        get_error_ = Get<GLenum (*)()>("glGetError");
        get_integerv_ = Get<void (*)(GLenum, GLint*)>("glGetIntegerv");
        ASSERT_NE(read_buffer_, nullptr);
    }

    GLenum ReadBufferNow() {
        GLint value = 0;
        get_integerv_(GL_READ_BUFFER_, &value);
        return static_cast<GLenum>(value);
    }

    void (*read_buffer_)(GLenum) = nullptr;
    GLenum (*get_error_)() = nullptr;
    void (*get_integerv_)(GLenum, GLint*) = nullptr;
};

TEST_F(ReadBufferTest, FrontAndBackIsIllegalUnlikeDrawBuffer) {
    read_buffer_(GL_FRONT_);
    ASSERT_EQ(get_error_(), GL_NO_ERROR_);
    read_buffer_(GL_FRONT_AND_BACK_);
    EXPECT_EQ(get_error_(), GL_INVALID_OPERATION_)
        << "GL_FRONT_AND_BACK is not a single buffer - illegal to read from";
}

TEST_F(ReadBufferTest, UnknownEnumIsRejected) {
    read_buffer_(static_cast<GLenum>(0xDEAD));
    EXPECT_EQ(get_error_(), GL_INVALID_ENUM_);
}

TEST_F(ReadBufferTest, LegacySelectorOnDefaultFramebufferMapsToBackAndQueryReadsItBack) {
    read_buffer_(GL_FRONT_);
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
    EXPECT_EQ(ReadBufferNow(), GL_BACK_)
        << "an EGL surface has no separately addressable front buffer";
}

TEST_F(ReadBufferTest, AttachmentSelectorOnDefaultFramebufferIsInvalidOperation) {
    read_buffer_(GL_COLOR_ATTACHMENT0_);
    EXPECT_EQ(get_error_(), GL_INVALID_OPERATION_);
}

TEST_F(ReadBufferTest, CompilesIntoAndReplaysFromADisplayList) {
    auto gen_lists = Get<GLuint (*)(GLsizei)>("glGenLists");
    auto new_list = Get<void (*)(GLuint, GLenum)>("glNewList");
    auto end_list = Get<void (*)()>("glEndList");
    auto call_list = Get<void (*)(GLuint)>("glCallList");
    const GLuint list = gen_lists(1);
    ASSERT_NE(list, 0u);

    read_buffer_(GL_FRONT_);
    ASSERT_EQ(ReadBufferNow(), GL_BACK_);
    new_list(list, GL_COMPILE_);
    read_buffer_(GL_BACK_);
    end_list();
    // Both map to GL_BACK on the default framebuffer, so a naive test
    // could not tell "compiling executed immediately" apart from "compiling
    // deferred it" - the assertion right after new_list would pass either
    // way. What actually distinguishes them: LIST_RECORD's own
    // shouldFinish() early-return means glReadBuffer never reaches the
    // backend call at all while compiling, so no GL_INVALID_ENUM/OPERATION
    // from an argument error could have fired either - covered by
    // UnknownEnumIsRejected/FrontAndBackIsIllegalUnlikeDrawBuffer running
    // cleanly outside a display list. This test's job is just replay:
    call_list(list);
    EXPECT_EQ(ReadBufferNow(), GL_BACK_);
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

class ReadBufferFboTest : public ContextTest {
protected:
    void SetUp() override {
        ContextTest::SetUp();
        if (::testing::Test::IsSkipped()) return;
        read_buffer_ = Get<void (*)(GLenum)>("glReadBuffer");
        draw_buffer_ = Get<void (*)(GLenum)>("glDrawBuffer");
        get_error_ = Get<GLenum (*)()>("glGetError");
        clear_color_ = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glClearColor");
        clear_ = Get<void (*)(sfpew_test::GLbitfield)>("glClear");
        gen_framebuffers_ = Get<void (*)(GLsizei, GLuint*)>("glGenFramebuffers");
        bind_framebuffer_ = Get<void (*)(GLenum, GLuint)>("glBindFramebuffer");
        check_framebuffer_ = Get<GLenum (*)(GLenum)>("glCheckFramebufferStatus");
        framebuffer_texture_ =
            Get<void (*)(GLenum, GLenum, GLenum, GLuint, GLint)>("glFramebufferTexture2D");
        gen_textures_ = Get<void (*)(GLsizei, GLuint*)>("glGenTextures");
        bind_texture_ = Get<void (*)(GLenum, GLuint)>("glBindTexture");
        tex_image_ = Get<void (*)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum,
                                  const void*)>("glTexImage2D");
        tex_parameter_ = Get<void (*)(GLenum, GLenum, GLint)>("glTexParameteri");
        auto read_pixels =
            Get<void (*)(sfpew_test::GLint, sfpew_test::GLint, GLsizei, GLsizei, GLenum, GLenum,
                        void*)>("glReadPixels");
        ASSERT_NE(read_pixels, nullptr);
        probe_.emplace(read_pixels);
    }

    GLuint MakeAttachedTexture(GLuint fbo, GLenum attachment) {
        GLuint texture = 0;
        gen_textures_(1, &texture);
        bind_texture_(GL_TEXTURE_2D_, texture);
        tex_parameter_(GL_TEXTURE_2D_, GL_TEXTURE_MIN_FILTER_, GL_NEAREST_);
        tex_parameter_(GL_TEXTURE_2D_, GL_TEXTURE_MAG_FILTER_, GL_NEAREST_);
        tex_image_(GL_TEXTURE_2D_, 0, GL_RGBA8_, size(), size(), 0, GL_RGBA_, GL_UNSIGNED_BYTE_,
                  nullptr);
        bind_framebuffer_(GL_FRAMEBUFFER_, fbo);
        framebuffer_texture_(GL_FRAMEBUFFER_, attachment, GL_TEXTURE_2D_, texture, 0);
        return texture;
    }

    void (*read_buffer_)(GLenum) = nullptr;
    void (*draw_buffer_)(GLenum) = nullptr;
    GLenum (*get_error_)() = nullptr;
    void (*clear_color_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*clear_)(sfpew_test::GLbitfield) = nullptr;
    void (*gen_framebuffers_)(GLsizei, GLuint*) = nullptr;
    void (*bind_framebuffer_)(GLenum, GLuint) = nullptr;
    GLenum (*check_framebuffer_)(GLenum) = nullptr;
    void (*framebuffer_texture_)(GLenum, GLenum, GLenum, GLuint, GLint) = nullptr;
    void (*gen_textures_)(GLsizei, GLuint*) = nullptr;
    void (*bind_texture_)(GLenum, GLuint) = nullptr;
    void (*tex_image_)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum,
                       const void*) = nullptr;
    void (*tex_parameter_)(GLenum, GLenum, GLint) = nullptr;
    std::optional<PixelProbe> probe_;
};

TEST_F(ReadBufferFboTest, SelectedAttachmentIsWhatReadPixelsActuallyReturns) {
    GLuint fbo = 0;
    gen_framebuffers_(1, &fbo);
    MakeAttachedTexture(fbo, GL_COLOR_ATTACHMENT0_);
    MakeAttachedTexture(fbo, GL_COLOR_ATTACHMENT1_);
    ASSERT_EQ(check_framebuffer_(GL_FRAMEBUFFER_), GL_FRAMEBUFFER_COMPLETE_);

    // Clear each attachment to a distinct color by selecting it as the
    // sole draw buffer first - avoids needing a multi-output fragment
    // shader (the FPE uber-shader only ever writes one), so this test
    // isolates glReadBuffer's own behavior from FPE draw output entirely.
    draw_buffer_(GL_COLOR_ATTACHMENT0_);
    clear_color_(1.0f, 0.0f, 0.0f, 1.0f);
    clear_(GL_COLOR_BUFFER_BIT_);
    draw_buffer_(GL_COLOR_ATTACHMENT1_);
    clear_color_(0.0f, 0.0f, 1.0f, 1.0f);
    clear_(GL_COLOR_BUFFER_BIT_);
    ASSERT_EQ(get_error_(), GL_NO_ERROR_);

    read_buffer_(GL_COLOR_ATTACHMENT0_);
    const auto attachment0 = probe_->At(size() / 2, size() / 2);
    EXPECT_GT(attachment0.r, 200);
    EXPECT_LT(attachment0.b, 40) << "GL_COLOR_ATTACHMENT0 must read back red, not blue";

    read_buffer_(GL_COLOR_ATTACHMENT1_);
    const auto attachment1 = probe_->At(size() / 2, size() / 2);
    EXPECT_LT(attachment1.r, 40) << "GL_COLOR_ATTACHMENT1 must read back blue, not red";
    EXPECT_GT(attachment1.b, 200);

    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

} // namespace
