// SimpleFPEWrapper - tests/gtest_compressed_dlist_pbo.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// glCompressedTex{Sub,}Image{1,2}D compiled into a display list while a
// GL_PIXEL_UNPACK_BUFFER is bound: `data` is a byte offset into that buffer,
// not an address, and the recorder used to memcpy imageSize bytes from it
// (plans/17 P14 - SIGSEGV). GL 2.1 4.4.6 has the list hold the pixel data as
// it was when compiled, so the payload must be read out of the buffer at
// compile time and stay authoritative afterwards, however the app refills,
// rebinds or deletes that buffer.
//
// Judgements are made on the green channel alone: llvmpipe reads R and B back
// swapped and none of this is about component order.

#include "sfpew_gtest.h"

#include <algorithm>
#include <array>
#include <optional>

namespace {

using sfpew_test::ContextTest;
using sfpew_test::GLbitfield;
using sfpew_test::GLenum;
using sfpew_test::GLfloat;
using sfpew_test::GLint;
using sfpew_test::GLsizei;
using sfpew_test::GLsizeiptr;
using sfpew_test::GLintptr;
using sfpew_test::GLubyte;
using sfpew_test::GLuint;
using sfpew_test::PixelProbe;

constexpr GLbitfield GL_COLOR_BUFFER_BIT_ = 0x00004000;
constexpr GLenum GL_QUADS_ = 0x0007;
constexpr GLenum GL_TEXTURE_1D_ = 0x0DE0;
constexpr GLenum GL_TEXTURE_2D_ = 0x0DE1;
constexpr GLenum GL_TEXTURE_MIN_FILTER_ = 0x2801;
constexpr GLenum GL_TEXTURE_MAG_FILTER_ = 0x2800;
constexpr GLenum GL_TEXTURE_WIDTH_ = 0x1000;
constexpr GLenum GL_NEAREST_ = 0x2600;
constexpr GLenum GL_COMPRESSED_RGBA_S3TC_DXT1_EXT_ = 0x83F1;
constexpr GLenum GL_PIXEL_UNPACK_BUFFER_ = 0x88EC;
constexpr GLenum GL_STATIC_DRAW_ = 0x88E4;
constexpr GLenum GL_COMPILE_ = 0x1300;
constexpr GLenum GL_PROJECTION_ = 0x1701;
constexpr GLenum GL_MODELVIEW_ = 0x1700;
constexpr GLenum GL_NO_ERROR_ = 0;
constexpr GLenum GL_INVALID_OPERATION_ = 0x0502;

// The two 4x4 DXT1 blocks the compressed-texture suite already uses: both
// color endpoints equal and every 2-bit index zero, so all 16 texels are the
// endpoint color whichever index table the decoder picks.
constexpr std::array<GLubyte, 8> kRedBlock = {0x00, 0xF8, 0x00, 0xF8, 0x00, 0x00, 0x00, 0x00};
constexpr std::array<GLubyte, 8> kGreenBlock = {0xE0, 0x07, 0xE0, 0x07, 0x00, 0x00, 0x00, 0x00};

// The payload sits at a non-zero offset on purpose: offset 0 is the one value
// the pre-fix recorder survived, because its null check short-circuited the
// memcpy.
constexpr GLintptr kPayloadOffset = 8;

using CompressedImage1DFn = void (*)(GLenum, GLint, GLenum, GLsizei, GLint, GLsizei, const void*);
using CompressedSubImage1DFn = void (*)(GLenum, GLint, GLint, GLsizei, GLenum, GLsizei, const void*);
using CompressedImage2DFn =
    void (*)(GLenum, GLint, GLenum, GLsizei, GLsizei, GLint, GLsizei, const void*);
using CompressedSubImage2DFn =
    void (*)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLsizei, const void*);

constexpr int kSize = 64;

class CompressedListPboTest : public ContextTest {
protected:
    CompressedListPboTest() : ContextTest(sfpew_test::Backend::GLES3, kSize) {}

    void SetUp() override {
        ContextTest::SetUp();
        if (::testing::Test::IsSkipped()) return;
        clear_color_ = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glClearColor");
        clear_ = Get<void (*)(GLbitfield)>("glClear");
        begin_ = Get<void (*)(GLenum)>("glBegin");
        end_ = Get<void (*)()>("glEnd");
        color4f_ = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glColor4f");
        vertex2f_ = Get<void (*)(GLfloat, GLfloat)>("glVertex2f");
        tex_coord2f_ = Get<void (*)(GLfloat, GLfloat)>("glTexCoord2f");
        enable_ = Get<void (*)(GLenum)>("glEnable");
        gen_textures_ = Get<void (*)(GLsizei, GLuint*)>("glGenTextures");
        bind_texture_ = Get<void (*)(GLenum, GLuint)>("glBindTexture");
        tex_parameteri_ = Get<void (*)(GLenum, GLenum, GLint)>("glTexParameteri");
        gen_buffers_ = Get<void (*)(GLsizei, GLuint*)>("glGenBuffers");
        delete_buffers_ = Get<void (*)(GLsizei, const GLuint*)>("glDeleteBuffers");
        bind_buffer_ = Get<void (*)(GLenum, GLuint)>("glBindBuffer");
        buffer_data_ = Get<void (*)(GLenum, GLsizeiptr, const void*, GLenum)>("glBufferData");
        buffer_sub_data_ = Get<void (*)(GLenum, GLintptr, GLsizeiptr, const void*)>("glBufferSubData");
        image_ = Get<CompressedImage2DFn>("glCompressedTexImage2D");
        sub_image_ = Get<CompressedSubImage2DFn>("glCompressedTexSubImage2D");
        image_1d_ = Get<CompressedImage1DFn>("glCompressedTexImage1D");
        sub_image_1d_ = Get<CompressedSubImage1DFn>("glCompressedTexSubImage1D");
        get_level_ = Get<void (*)(GLenum, GLint, GLenum, GLint*)>("glGetTexLevelParameteriv");
        gen_lists_ = Get<GLuint (*)(GLsizei)>("glGenLists");
        new_list_ = Get<void (*)(GLuint, GLenum)>("glNewList");
        end_list_ = Get<void (*)()>("glEndList");
        call_list_ = Get<void (*)(GLuint)>("glCallList");
        delete_lists_ = Get<void (*)(GLuint, GLsizei)>("glDeleteLists");
        matrix_mode_ = Get<void (*)(GLenum)>("glMatrixMode");
        load_identity_ = Get<void (*)()>("glLoadIdentity");
        get_error_ = Get<GLenum (*)()>("glGetError");
        probe_.emplace(
            Get<void (*)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*)>("glReadPixels"));

        matrix_mode_(GL_PROJECTION_);
        load_identity_();
        matrix_mode_(GL_MODELVIEW_);
        load_identity_();
        clear_color_(0.0f, 0.0f, 0.0f, 0.0f);
        Drain();
    }

    void Drain() {
        while (get_error_() != GL_NO_ERROR_) {}
    }

    GLuint NewTexture() {
        GLuint texture = 0;
        gen_textures_(1, &texture);
        bind_texture_(GL_TEXTURE_2D_, texture);
        tex_parameteri_(GL_TEXTURE_2D_, GL_TEXTURE_MIN_FILTER_, GL_NEAREST_);
        tex_parameteri_(GL_TEXTURE_2D_, GL_TEXTURE_MAG_FILTER_, GL_NEAREST_);
        return texture;
    }

    // A pixel unpack buffer holding one block at kPayloadOffset.
    GLuint NewPayloadBuffer(const std::array<GLubyte, 8>& block) {
        GLuint buffer = 0;
        gen_buffers_(1, &buffer);
        bind_buffer_(GL_PIXEL_UNPACK_BUFFER_, buffer);
        std::array<GLubyte, kPayloadOffset + 8> contents{};
        for (size_t i = 0; i < block.size(); ++i) contents[kPayloadOffset + i] = block[i];
        buffer_data_(GL_PIXEL_UNPACK_BUFFER_, static_cast<GLsizeiptr>(contents.size()),
                     contents.data(), GL_STATIC_DRAW_);
        return buffer;
    }

    // Whether this backend takes the S3TC block at all - the suite has
    // nothing to say about display-list capture on one that does not.
    bool CompressedUploadWorks() {
        const GLuint probe_texture = NewTexture();
        Drain();
        image_(GL_TEXTURE_2D_, 0, GL_COMPRESSED_RGBA_S3TC_DXT1_EXT_, 4, 4, 0,
               static_cast<GLsizei>(kRedBlock.size()), kRedBlock.data());
        const bool ok = get_error_() == GL_NO_ERROR_;
        auto delete_textures = Get<void (*)(GLsizei, const GLuint*)>("glDeleteTextures");
        delete_textures(1, &probe_texture);
        return ok;
    }

    PixelProbe::Rgba SampleTexture() {
        enable_(GL_TEXTURE_2D_);
        clear_(GL_COLOR_BUFFER_BIT_);
        begin_(GL_QUADS_);
        color4f_(1.0f, 1.0f, 1.0f, 1.0f);
        tex_coord2f_(0.25f, 0.25f);
        vertex2f_(-1.0f, -1.0f);
        tex_coord2f_(0.75f, 0.25f);
        vertex2f_(1.0f, -1.0f);
        tex_coord2f_(0.75f, 0.75f);
        vertex2f_(1.0f, 1.0f);
        tex_coord2f_(0.25f, 0.75f);
        vertex2f_(-1.0f, 1.0f);
        end_();
        return probe_->At(kSize / 2, kSize / 2);
    }

    GLint LevelWidth() {
        GLint width = -1;
        get_level_(GL_TEXTURE_2D_, 0, GL_TEXTURE_WIDTH_, &width);
        return width;
    }

    void (*clear_color_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*clear_)(GLbitfield) = nullptr;
    void (*begin_)(GLenum) = nullptr;
    void (*end_)() = nullptr;
    void (*color4f_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*vertex2f_)(GLfloat, GLfloat) = nullptr;
    void (*tex_coord2f_)(GLfloat, GLfloat) = nullptr;
    void (*enable_)(GLenum) = nullptr;
    void (*gen_textures_)(GLsizei, GLuint*) = nullptr;
    void (*bind_texture_)(GLenum, GLuint) = nullptr;
    void (*tex_parameteri_)(GLenum, GLenum, GLint) = nullptr;
    void (*gen_buffers_)(GLsizei, GLuint*) = nullptr;
    void (*delete_buffers_)(GLsizei, const GLuint*) = nullptr;
    void (*bind_buffer_)(GLenum, GLuint) = nullptr;
    void (*buffer_data_)(GLenum, GLsizeiptr, const void*, GLenum) = nullptr;
    void (*buffer_sub_data_)(GLenum, GLintptr, GLsizeiptr, const void*) = nullptr;
    CompressedImage2DFn image_ = nullptr;
    CompressedSubImage2DFn sub_image_ = nullptr;
    CompressedImage1DFn image_1d_ = nullptr;
    CompressedSubImage1DFn sub_image_1d_ = nullptr;
    void (*get_level_)(GLenum, GLint, GLenum, GLint*) = nullptr;
    GLuint (*gen_lists_)(GLsizei) = nullptr;
    void (*new_list_)(GLuint, GLenum) = nullptr;
    void (*end_list_)() = nullptr;
    void (*call_list_)(GLuint) = nullptr;
    void (*delete_lists_)(GLuint, GLsizei) = nullptr;
    void (*matrix_mode_)(GLenum) = nullptr;
    void (*load_identity_)() = nullptr;
    GLenum (*get_error_)() = nullptr;
    std::optional<PixelProbe> probe_;
};

TEST_F(CompressedListPboTest, ImageRecordsThePayloadOutOfTheUnpackBufferAtCompileTime) {
    if (!CompressedUploadWorks()) GTEST_SKIP() << "backend does not expose S3TC";
    NewTexture();
    const GLuint buffer = NewPayloadBuffer(kRedBlock);
    Drain();

    const GLuint list = gen_lists_(1);
    ASSERT_NE(list, 0u);
    new_list_(list, GL_COMPILE_);
    image_(GL_TEXTURE_2D_, 0, GL_COMPRESSED_RGBA_S3TC_DXT1_EXT_, 4, 4, 0, 8,
           reinterpret_cast<const void*>(kPayloadOffset));
    end_list_();
    EXPECT_EQ(get_error_(), GL_NO_ERROR_) << "compiling a PBO-sourced compressed upload";

    // The list owns its copy from here on: refill the buffer with the other
    // color, then take the buffer away entirely.
    buffer_sub_data_(GL_PIXEL_UNPACK_BUFFER_, kPayloadOffset,
                     static_cast<GLsizeiptr>(kGreenBlock.size()), kGreenBlock.data());
    bind_buffer_(GL_PIXEL_UNPACK_BUFFER_, 0);
    delete_buffers_(1, &buffer);
    Drain();

    call_list_(list);
    EXPECT_EQ(get_error_(), GL_NO_ERROR_) << "replaying the recorded upload";
    delete_lists_(list, 1);
    ASSERT_EQ(LevelWidth(), 4) << "the replay defined no image at all";

    const auto pixel = SampleTexture();
    EXPECT_LE(pixel.g, 20) << "the replay used the buffer's LATER contents, not the ones the "
                              "command was compiled with";
    EXPECT_GE(std::max(pixel.r, pixel.b), 100) << "no compressed image was decoded";
}

TEST_F(CompressedListPboTest, ReplayIgnoresWhateverUnpackBufferIsBoundAtCallTime) {
    if (!CompressedUploadWorks()) GTEST_SKIP() << "backend does not expose S3TC";
    NewTexture();
    const GLuint compile_buffer = NewPayloadBuffer(kRedBlock);
    Drain();

    const GLuint list = gen_lists_(1);
    ASSERT_NE(list, 0u);
    new_list_(list, GL_COMPILE_);
    image_(GL_TEXTURE_2D_, 0, GL_COMPRESSED_RGBA_S3TC_DXT1_EXT_, 4, 4, 0, 8,
           reinterpret_cast<const void*>(kPayloadOffset));
    end_list_();
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);

    // A different buffer, still bound while the list is executed: the captured
    // payload is client memory by now, so nothing may re-read it as an offset.
    bind_buffer_(GL_PIXEL_UNPACK_BUFFER_, 0);
    delete_buffers_(1, &compile_buffer);
    const GLuint replay_buffer = NewPayloadBuffer(kGreenBlock);
    Drain();

    call_list_(list);
    EXPECT_EQ(get_error_(), GL_NO_ERROR_) << "replaying with an unpack buffer bound";
    delete_lists_(list, 1);
    bind_buffer_(GL_PIXEL_UNPACK_BUFFER_, 0);
    delete_buffers_(1, &replay_buffer);
    ASSERT_EQ(LevelWidth(), 4) << "the replay defined no image at all";

    const auto pixel = SampleTexture();
    EXPECT_LE(pixel.g, 20) << "the replay read the buffer bound at glCallList time";
    EXPECT_GE(std::max(pixel.r, pixel.b), 100) << "no compressed image was decoded";
}

TEST_F(CompressedListPboTest, SubImageRecordsThePayloadOutOfTheUnpackBuffer) {
    if (!CompressedUploadWorks()) GTEST_SKIP() << "backend does not expose S3TC";
    NewTexture();
    image_(GL_TEXTURE_2D_, 0, GL_COMPRESSED_RGBA_S3TC_DXT1_EXT_, 4, 4, 0,
           static_cast<GLsizei>(kRedBlock.size()), kRedBlock.data());
    const GLuint buffer = NewPayloadBuffer(kGreenBlock);
    Drain();

    const GLuint list = gen_lists_(1);
    ASSERT_NE(list, 0u);
    new_list_(list, GL_COMPILE_);
    sub_image_(GL_TEXTURE_2D_, 0, 0, 0, 4, 4, GL_COMPRESSED_RGBA_S3TC_DXT1_EXT_, 8,
               reinterpret_cast<const void*>(kPayloadOffset));
    end_list_();
    EXPECT_EQ(get_error_(), GL_NO_ERROR_) << "compiling a PBO-sourced compressed sub-upload";

    buffer_sub_data_(GL_PIXEL_UNPACK_BUFFER_, kPayloadOffset,
                     static_cast<GLsizeiptr>(kRedBlock.size()), kRedBlock.data());
    bind_buffer_(GL_PIXEL_UNPACK_BUFFER_, 0);
    delete_buffers_(1, &buffer);
    Drain();

    call_list_(list);
    EXPECT_EQ(get_error_(), GL_NO_ERROR_) << "replaying the recorded sub-upload";
    delete_lists_(list, 1);

    EXPECT_GE(SampleTexture().g, 100) << "the sub-image replay did not carry the payload the "
                                         "command was compiled with";
}

TEST_F(CompressedListPboTest, OneDimensionalEntriesDoNotDereferenceTheOffsetEither) {
    NewTexture();
    const GLuint buffer = NewPayloadBuffer(kRedBlock);
    Drain();

    const GLuint list = gen_lists_(1);
    ASSERT_NE(list, 0u);
    new_list_(list, GL_COMPILE_);
    image_1d_(GL_TEXTURE_1D_, 0, GL_COMPRESSED_RGBA_S3TC_DXT1_EXT_, 4, 0, 8,
              reinterpret_cast<const void*>(kPayloadOffset));
    sub_image_1d_(GL_TEXTURE_1D_, 0, 0, 4, GL_COMPRESSED_RGBA_S3TC_DXT1_EXT_, 8,
                  reinterpret_cast<const void*>(kPayloadOffset));
    end_list_();
    EXPECT_EQ(get_error_(), GL_NO_ERROR_) << "compiling a PBO-sourced 1D compressed upload";

    bind_buffer_(GL_PIXEL_UNPACK_BUFFER_, 0);
    delete_buffers_(1, &buffer);
    Drain();

    // A block-compressed format has no one-row representation; the replay must
    // reach the same refusal the immediate call gives, not a crash.
    call_list_(list);
    EXPECT_EQ(get_error_(), GL_INVALID_OPERATION_);
    delete_lists_(list, 1);
}

} // namespace
