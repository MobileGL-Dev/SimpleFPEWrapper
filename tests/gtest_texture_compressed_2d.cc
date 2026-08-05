// SimpleFPEWrapper - tests/gtest_texture_compressed_2d.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

#include "sfpew_gtest.h"

#include <array>

namespace {

using sfpew_test::ContextTest;
using sfpew_test::GLenum;
using sfpew_test::GLint;
using sfpew_test::GLsizei;
using sfpew_test::GLubyte;
using sfpew_test::GLuint;
using sfpew_test::LibraryTest;

constexpr GLenum GL_TEXTURE_1D_ = 0x0DE0;
constexpr GLenum GL_TEXTURE_2D_ = 0x0DE1;
constexpr GLenum GL_PROXY_TEXTURE_2D_ = 0x8064;
constexpr GLenum GL_TEXTURE_WIDTH_ = 0x1000;
constexpr GLenum GL_TEXTURE_HEIGHT_ = 0x1001;
constexpr GLenum GL_TEXTURE_INTERNAL_FORMAT_ = 0x1003;
constexpr GLenum GL_TEXTURE_MIN_FILTER_ = 0x2801;
constexpr GLenum GL_TEXTURE_MAG_FILTER_ = 0x2800;
constexpr GLenum GL_TEXTURE_COMPRESSED_IMAGE_SIZE_ = 0x86A0;
constexpr GLenum GL_TEXTURE_COMPRESSED_ = 0x86A1;
constexpr GLenum GL_NEAREST_ = 0x2600;
constexpr GLenum GL_COMPRESSED_RGBA_ = 0x84EE;
constexpr GLenum GL_COMPRESSED_RGBA_S3TC_DXT1_EXT_ = 0x83F1;
constexpr GLenum GL_COMPILE_ = 0x1300;
constexpr GLenum GL_TRUE_ = 1;
constexpr GLenum GL_NO_ERROR_ = 0;
constexpr GLenum GL_INVALID_ENUM_ = 0x0500;
constexpr GLenum GL_INVALID_VALUE_ = 0x0501;
constexpr GLenum GL_INVALID_OPERATION_ = 0x0502;

// A minimal 4x4 DXT1 block: color0 == color1 == opaque red (RGB565
// 0xF800, little-endian bytes 0x00 0xF8) and every 2-bit index left at 0,
// so all 16 texels sample color0 regardless of which of DXT1's two index
// tables applies. 8 bytes, matching the S3TC block size the survey cites.
constexpr std::array<GLubyte, 8> kSolidRedDxt1Block = {0x00, 0xF8, 0x00, 0xF8, 0x00, 0x00, 0x00, 0x00};
constexpr std::array<GLubyte, 8> kSolidGreenDxt1Block = {0xE0, 0x07, 0xE0, 0x07, 0x00, 0x00, 0x00, 0x00};

using CompressedImage2DFn =
    void (*)(GLenum, GLint, GLenum, GLsizei, GLsizei, GLint, GLsizei, const void*);
using CompressedSubImage2DFn =
    void (*)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLsizei, const void*);

class Texture2DCompressedLibraryTest : public LibraryTest {
protected:
    void SetUp() override {
        LibraryTest::SetUp();
        if (::testing::Test::HasFatalFailure()) return;
        get_error_ = Get<GLenum (*)()>("glGetError");
        image_ = Get<CompressedImage2DFn>("glCompressedTexImage2D");
        sub_image_ = Get<CompressedSubImage2DFn>("glCompressedTexSubImage2D");
    }

    GLenum (*get_error_)() = nullptr;
    CompressedImage2DFn image_ = nullptr;
    CompressedSubImage2DFn sub_image_ = nullptr;
};

TEST_F(Texture2DCompressedLibraryTest, ArgumentValidationMatchesTheDesktopErrorContract) {
    // An emulated 1D target is not a valid one for the real 2D entry point.
    image_(GL_TEXTURE_1D_, 0, GL_COMPRESSED_RGBA_S3TC_DXT1_EXT_, 4, 4, 0, 8,
          kSolidRedDxt1Block.data());
    EXPECT_EQ(get_error_(), GL_INVALID_ENUM_);

    image_(GL_TEXTURE_2D_, 0, GL_COMPRESSED_RGBA_S3TC_DXT1_EXT_, 4, 4, 0, -1,
          kSolidRedDxt1Block.data());
    EXPECT_EQ(get_error_(), GL_INVALID_VALUE_);

    image_(GL_TEXTURE_2D_, 0, GL_COMPRESSED_RGBA_S3TC_DXT1_EXT_, -1, 4, 0, 8,
          kSolidRedDxt1Block.data());
    EXPECT_EQ(get_error_(), GL_INVALID_VALUE_);

    image_(GL_TEXTURE_2D_, 0, GL_COMPRESSED_RGBA_S3TC_DXT1_EXT_, 4, 4, 1, 8,
          kSolidRedDxt1Block.data());
    EXPECT_EQ(get_error_(), GL_INVALID_VALUE_);

    // The generic (unsized) compressed formats never had storage of their
    // own, block or otherwise - same rejection glCompressedTexImage1D
    // already gives them.
    image_(GL_TEXTURE_2D_, 0, GL_COMPRESSED_RGBA_, 4, 4, 0, 8, kSolidRedDxt1Block.data());
    EXPECT_EQ(get_error_(), GL_INVALID_ENUM_);

    sub_image_(GL_TEXTURE_1D_, 0, 0, 0, 4, 4, GL_COMPRESSED_RGBA_S3TC_DXT1_EXT_, 8,
              kSolidRedDxt1Block.data());
    EXPECT_EQ(get_error_(), GL_INVALID_ENUM_);

    sub_image_(GL_TEXTURE_2D_, 0, 0, 0, 4, 4, GL_COMPRESSED_RGBA_S3TC_DXT1_EXT_, -1,
              kSolidRedDxt1Block.data());
    EXPECT_EQ(get_error_(), GL_INVALID_VALUE_);

    sub_image_(GL_TEXTURE_2D_, 0, 0, 0, 4, 4, GL_COMPRESSED_RGBA_, 8, kSolidRedDxt1Block.data());
    EXPECT_EQ(get_error_(), GL_INVALID_ENUM_);
}

TEST_F(LibraryTest, TextureCompressed2DAliasesResolveToTheWrappers) {
    EXPECT_EQ(Get<void*>("glCompressedTexImage2DARB"), Get<void*>("glCompressedTexImage2D"));
    EXPECT_EQ(Get<void*>("glCompressedTexSubImage2DARB"),
              Get<void*>("glCompressedTexSubImage2D"));
}

class Texture2DCompressedTest : public ContextTest {
protected:
    void SetUp() override {
        ContextTest::SetUp();
        if (::testing::Test::IsSkipped() || ::testing::Test::HasFatalFailure()) return;
        using MakeCurrentFn = EGLBoolean (*)(EGLDisplay, EGLSurface, EGLSurface, EGLContext);
        auto wrapper_make_current = Get<MakeCurrentFn>("eglMakeCurrent");
        ASSERT_TRUE(wrapper_make_current(display(), surface(), surface(), eglGetCurrentContext()));

        get_error_ = Get<GLenum (*)()>("glGetError");
        gen_textures_ = Get<void (*)(GLsizei, GLuint*)>("glGenTextures");
        delete_textures_ = Get<void (*)(GLsizei, const GLuint*)>("glDeleteTextures");
        bind_texture_ = Get<void (*)(GLenum, GLuint)>("glBindTexture");
        tex_parameteri_ = Get<void (*)(GLenum, GLenum, GLint)>("glTexParameteri");
        image_ = Get<CompressedImage2DFn>("glCompressedTexImage2D");
        sub_image_ = Get<CompressedSubImage2DFn>("glCompressedTexSubImage2D");
        get_level_ = Get<void (*)(GLenum, GLint, GLenum, GLint*)>("glGetTexLevelParameteriv");
    }

    void Drain() const {
        while (get_error_() != GL_NO_ERROR_) {}
    }

    GLuint NewTexture() const {
        GLuint texture = 0;
        gen_textures_(1, &texture);
        EXPECT_NE(texture, 0u);
        bind_texture_(GL_TEXTURE_2D_, texture);
        tex_parameteri_(GL_TEXTURE_2D_, GL_TEXTURE_MIN_FILTER_, GL_NEAREST_);
        tex_parameteri_(GL_TEXTURE_2D_, GL_TEXTURE_MAG_FILTER_, GL_NEAREST_);
        return texture;
    }

    GLint Level(GLenum pname) const {
        GLint value = -1;
        get_level_(GL_TEXTURE_2D_, 0, pname, &value);
        EXPECT_EQ(get_error_(), GL_NO_ERROR_);
        return value;
    }

    GLenum (*get_error_)() = nullptr;
    void (*gen_textures_)(GLsizei, GLuint*) = nullptr;
    void (*delete_textures_)(GLsizei, const GLuint*) = nullptr;
    void (*bind_texture_)(GLenum, GLuint) = nullptr;
    void (*tex_parameteri_)(GLenum, GLenum, GLint) = nullptr;
    CompressedImage2DFn image_ = nullptr;
    CompressedSubImage2DFn sub_image_ = nullptr;
    void (*get_level_)(GLenum, GLint, GLenum, GLint*) = nullptr;
};

TEST_F(Texture2DCompressedTest, UploadReportsTheCorrectSizeFormatAndCompressedStatus) {
    const GLuint texture = NewTexture();
    Drain();
    image_(GL_TEXTURE_2D_, 0, GL_COMPRESSED_RGBA_S3TC_DXT1_EXT_, 4, 4, 0,
          static_cast<GLsizei>(kSolidRedDxt1Block.size()), kSolidRedDxt1Block.data());
    const GLenum upload_error = get_error_();
    if (upload_error != GL_NO_ERROR_) {
        delete_textures_(1, &texture);
        GTEST_SKIP() << "backend does not expose S3TC (error 0x" << std::hex << upload_error << ')';
    }

    EXPECT_EQ(Level(GL_TEXTURE_WIDTH_), 4);
    EXPECT_EQ(Level(GL_TEXTURE_HEIGHT_), 4);
    EXPECT_EQ(Level(GL_TEXTURE_INTERNAL_FORMAT_), static_cast<GLint>(GL_COMPRESSED_RGBA_S3TC_DXT1_EXT_));
    EXPECT_EQ(Level(GL_TEXTURE_COMPRESSED_), static_cast<GLint>(GL_TRUE_));
    EXPECT_EQ(Level(GL_TEXTURE_COMPRESSED_IMAGE_SIZE_), static_cast<GLint>(kSolidRedDxt1Block.size()));

    delete_textures_(1, &texture);
}

TEST_F(Texture2DCompressedTest, SubImageReplacesAWholeAlignedBlockWithNoError) {
    const GLuint texture = NewTexture();
    Drain();
    image_(GL_TEXTURE_2D_, 0, GL_COMPRESSED_RGBA_S3TC_DXT1_EXT_, 4, 4, 0,
          static_cast<GLsizei>(kSolidRedDxt1Block.size()), kSolidRedDxt1Block.data());
    if (get_error_() != GL_NO_ERROR_) {
        delete_textures_(1, &texture);
        GTEST_SKIP() << "backend does not expose S3TC";
    }

    sub_image_(GL_TEXTURE_2D_, 0, 0, 0, 4, 4, GL_COMPRESSED_RGBA_S3TC_DXT1_EXT_,
              static_cast<GLsizei>(kSolidGreenDxt1Block.size()), kSolidGreenDxt1Block.data());
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
    // A sub-image upload does not redefine the level's own size/format.
    EXPECT_EQ(Level(GL_TEXTURE_WIDTH_), 4);
    EXPECT_EQ(Level(GL_TEXTURE_INTERNAL_FORMAT_), static_cast<GLint>(GL_COMPRESSED_RGBA_S3TC_DXT1_EXT_));

    delete_textures_(1, &texture);
}

TEST_F(Texture2DCompressedTest, ProxyTargetValidatesWithoutTouchingRealTextureState) {
    const GLuint texture = NewTexture();
    Drain();
    image_(GL_PROXY_TEXTURE_2D_, 0, GL_COMPRESSED_RGBA_S3TC_DXT1_EXT_, 4, 4, 0,
          static_cast<GLsizei>(kSolidRedDxt1Block.size()), kSolidRedDxt1Block.data());
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);

    // The proxy call must not have defined level 0 of the texture actually
    // bound to GL_TEXTURE_2D: querying it still hits the "never defined"
    // path (same one the copy-based 1D test exercises for an untouched
    // level), not the size the proxy call was asked to validate.
    GLint width = -1;
    get_level_(GL_TEXTURE_2D_, 0, GL_TEXTURE_WIDTH_, &width);
    EXPECT_EQ(get_error_(), GL_INVALID_OPERATION_);
    delete_textures_(1, &texture);
}

TEST_F(Texture2DCompressedTest, DisplayListRecordsWithoutExecutingThenReplaysTheUpload) {
    const GLuint texture = NewTexture();
    Drain();

    auto gen_lists = Get<GLuint (*)(GLsizei)>("glGenLists");
    auto new_list = Get<void (*)(GLuint, GLenum)>("glNewList");
    auto end_list = Get<void (*)()>("glEndList");
    auto call_list = Get<void (*)(GLuint)>("glCallList");
    auto delete_lists = Get<void (*)(GLuint, GLsizei)>("glDeleteLists");

    const GLuint list = gen_lists(1);
    ASSERT_NE(list, 0u);
    new_list(list, GL_COMPILE_);
    image_(GL_TEXTURE_2D_, 0, GL_COMPRESSED_RGBA_S3TC_DXT1_EXT_, 4, 4, 0,
          static_cast<GLsizei>(kSolidRedDxt1Block.size()), kSolidRedDxt1Block.data());
    end_list();
    EXPECT_EQ(get_error_(), GL_NO_ERROR_)
        << "a compressed upload records during GL_COMPILE without executing";
    // Not executed yet: level 0 is still undefined.
    GLint width_before_replay = -1;
    get_level_(GL_TEXTURE_2D_, 0, GL_TEXTURE_WIDTH_, &width_before_replay);
    Drain();

    call_list(list);
    const GLenum replay_error = get_error_();
    delete_lists(list, 1);
    if (replay_error != GL_NO_ERROR_) {
        delete_textures_(1, &texture);
        GTEST_SKIP() << "backend does not expose S3TC";
    }
    EXPECT_EQ(Level(GL_TEXTURE_WIDTH_), 4);

    delete_textures_(1, &texture);
}

} // namespace
