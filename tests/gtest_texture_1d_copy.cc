// SimpleFPEWrapper - tests/gtest_texture_1d_copy.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

#include "sfpew_gtest.h"

#include <algorithm>
#include <array>

namespace {

using sfpew_test::ContextTest;
using sfpew_test::DesktopContextTest;
using sfpew_test::GLbitfield;
using sfpew_test::GLenum;
using sfpew_test::GLfloat;
using sfpew_test::GLint;
using sfpew_test::GLsizei;
using sfpew_test::GLubyte;
using sfpew_test::GLuint;
using sfpew_test::LibraryTest;
using sfpew_test::PixelProbe;

constexpr GLbitfield GL_COLOR_BUFFER_BIT_ = 0x00004000;
constexpr GLenum GL_QUADS_ = 0x0007;
constexpr GLenum GL_SCISSOR_TEST_ = 0x0C11;
constexpr GLenum GL_TEXTURE_1D_ = 0x0DE0;
constexpr GLenum GL_TEXTURE_2D_ = 0x0DE1;
constexpr GLenum GL_PROXY_TEXTURE_1D_ = 0x8063;
constexpr GLenum GL_TEXTURE_WIDTH_ = 0x1000;
constexpr GLenum GL_TEXTURE_HEIGHT_ = 0x1001;
constexpr GLenum GL_TEXTURE_INTERNAL_FORMAT_ = 0x1003;
constexpr GLenum GL_TEXTURE_MIN_FILTER_ = 0x2801;
constexpr GLenum GL_TEXTURE_MAG_FILTER_ = 0x2800;
constexpr GLenum GL_TEXTURE_WRAP_S_ = 0x2802;
constexpr GLenum GL_TEXTURE_COMPRESSED_IMAGE_SIZE_ = 0x86A0;
constexpr GLenum GL_TEXTURE_COMPRESSED_ = 0x86A1;
constexpr GLenum GL_NEAREST_ = 0x2600;
constexpr GLenum GL_CLAMP_TO_EDGE_ = 0x812F;
constexpr GLenum GL_RGBA_ = 0x1908;
constexpr GLenum GL_RGBA8_ = 0x8058;
constexpr GLenum GL_COMPRESSED_RGBA_ = 0x84EE;
constexpr GLenum GL_COMPRESSED_RGBA_S3TC_DXT1_EXT_ = 0x83F1;
constexpr GLenum GL_COMPILE_ = 0x1300;
constexpr GLenum GL_NO_ERROR_ = 0;
constexpr GLenum GL_INVALID_ENUM_ = 0x0500;
constexpr GLenum GL_INVALID_VALUE_ = 0x0501;
constexpr GLenum GL_INVALID_OPERATION_ = 0x0502;

class Texture1DCopyTest : public ContextTest {
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
        copy_image_ = Get<void (*)(GLenum, GLint, GLenum, GLint, GLint, GLsizei, GLint)>(
            "glCopyTexImage1D");
        copy_sub_image_ = Get<void (*)(GLenum, GLint, GLint, GLint, GLint, GLsizei)>(
            "glCopyTexSubImage1D");
        compressed_image_ =
            Get<void (*)(GLenum, GLint, GLenum, GLsizei, GLint, GLsizei, const void*)>(
                "glCompressedTexImage1D");
        compressed_sub_image_ =
            Get<void (*)(GLenum, GLint, GLint, GLsizei, GLenum, GLsizei, const void*)>(
                "glCompressedTexSubImage1D");
        get_level_ =
            Get<void (*)(GLenum, GLint, GLenum, GLint*)>("glGetTexLevelParameteriv");
        clear_color_ = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glClearColor");
        clear_ = Get<void (*)(GLbitfield)>("glClear");
        scissor_ = Get<void (*)(GLint, GLint, GLsizei, GLsizei)>("glScissor");
        enable_ = Get<void (*)(GLenum)>("glEnable");
        disable_ = Get<void (*)(GLenum)>("glDisable");
        begin_ = Get<void (*)(GLenum)>("glBegin");
        end_ = Get<void (*)()>("glEnd");
        color4f_ = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glColor4f");
        tex_coord1f_ = Get<void (*)(GLfloat)>("glTexCoord1f");
        vertex2f_ = Get<void (*)(GLfloat, GLfloat)>("glVertex2f");
        finish_ = Get<void (*)()>("glFinish");
        read_pixels_ =
            Get<void (*)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*)>(
                "glReadPixels");
    }

    void Drain() const {
        while (get_error_() != GL_NO_ERROR_) {}
    }

    GLuint NewTexture() const {
        GLuint texture = 0;
        gen_textures_(1, &texture);
        EXPECT_NE(texture, 0u);
        bind_texture_(GL_TEXTURE_1D_, texture);
        tex_parameteri_(GL_TEXTURE_1D_, GL_TEXTURE_MIN_FILTER_, GL_NEAREST_);
        tex_parameteri_(GL_TEXTURE_1D_, GL_TEXTURE_MAG_FILTER_, GL_NEAREST_);
        tex_parameteri_(GL_TEXTURE_1D_, GL_TEXTURE_WRAP_S_, GL_CLAMP_TO_EDGE_);
        EXPECT_EQ(get_error_(), GL_NO_ERROR_)
            << "the emulated 1D target must also work for texture parameters";
        return texture;
    }

    void PaintRedGreenFramebuffer() const {
        disable_(GL_TEXTURE_1D_);
        disable_(GL_SCISSOR_TEST_);
        clear_color_(1.0f, 0.0f, 0.0f, 1.0f);
        clear_(GL_COLOR_BUFFER_BIT_);
        enable_(GL_SCISSOR_TEST_);
        scissor_(size() / 2, 0, size() / 2, size());
        clear_color_(0.0f, 1.0f, 0.0f, 1.0f);
        clear_(GL_COLOR_BUFFER_BIT_);
        disable_(GL_SCISSOR_TEST_);
    }

    void DrawTexture() const {
        enable_(GL_TEXTURE_1D_);
        color4f_(1.0f, 1.0f, 1.0f, 1.0f);
        begin_(GL_QUADS_);
        tex_coord1f_(0.0f);
        vertex2f_(-1.0f, -1.0f);
        tex_coord1f_(1.0f);
        vertex2f_(1.0f, -1.0f);
        tex_coord1f_(1.0f);
        vertex2f_(1.0f, 1.0f);
        tex_coord1f_(0.0f);
        vertex2f_(-1.0f, 1.0f);
        end_();
        finish_();
    }

    PixelProbe::Rgba PixelAt(int x) const {
        return PixelProbe(read_pixels_).At(x, size() / 2);
    }

    GLint Level(GLenum pname) const {
        GLint value = -1;
        get_level_(GL_TEXTURE_1D_, 0, pname, &value);
        EXPECT_EQ(get_error_(), GL_NO_ERROR_);
        return value;
    }

    GLenum (*get_error_)() = nullptr;
    void (*gen_textures_)(GLsizei, GLuint*) = nullptr;
    void (*delete_textures_)(GLsizei, const GLuint*) = nullptr;
    void (*bind_texture_)(GLenum, GLuint) = nullptr;
    void (*tex_parameteri_)(GLenum, GLenum, GLint) = nullptr;
    void (*copy_image_)(GLenum, GLint, GLenum, GLint, GLint, GLsizei, GLint) = nullptr;
    void (*copy_sub_image_)(GLenum, GLint, GLint, GLint, GLint, GLsizei) = nullptr;
    void (*compressed_image_)(GLenum, GLint, GLenum, GLsizei, GLint, GLsizei, const void*) =
        nullptr;
    void (*compressed_sub_image_)(GLenum, GLint, GLint, GLsizei, GLenum, GLsizei,
                                  const void*) = nullptr;
    void (*get_level_)(GLenum, GLint, GLenum, GLint*) = nullptr;
    void (*clear_color_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*clear_)(GLbitfield) = nullptr;
    void (*scissor_)(GLint, GLint, GLsizei, GLsizei) = nullptr;
    void (*enable_)(GLenum) = nullptr;
    void (*disable_)(GLenum) = nullptr;
    void (*begin_)(GLenum) = nullptr;
    void (*end_)() = nullptr;
    void (*color4f_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*tex_coord1f_)(GLfloat) = nullptr;
    void (*vertex2f_)(GLfloat, GLfloat) = nullptr;
    void (*finish_)() = nullptr;
    void (*read_pixels_)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*) = nullptr;
};

class Texture1DCopyLibraryTest : public LibraryTest {
protected:
    void SetUp() override {
        LibraryTest::SetUp();
        if (::testing::Test::HasFatalFailure()) return;
        get_error_ = Get<GLenum (*)()>("glGetError");
        copy_image_ = Get<void (*)(GLenum, GLint, GLenum, GLint, GLint, GLsizei, GLint)>(
            "glCopyTexImage1D");
        copy_sub_image_ = Get<void (*)(GLenum, GLint, GLint, GLint, GLint, GLsizei)>(
            "glCopyTexSubImage1D");
        compressed_image_ =
            Get<void (*)(GLenum, GLint, GLenum, GLsizei, GLint, GLsizei, const void*)>(
                "glCompressedTexImage1D");
        compressed_sub_image_ =
            Get<void (*)(GLenum, GLint, GLint, GLsizei, GLenum, GLsizei, const void*)>(
                "glCompressedTexSubImage1D");
    }

    GLenum (*get_error_)() = nullptr;
    void (*copy_image_)(GLenum, GLint, GLenum, GLint, GLint, GLsizei, GLint) = nullptr;
    void (*copy_sub_image_)(GLenum, GLint, GLint, GLint, GLint, GLsizei) = nullptr;
    void (*compressed_image_)(GLenum, GLint, GLenum, GLsizei, GLint, GLsizei, const void*) =
        nullptr;
    void (*compressed_sub_image_)(GLenum, GLint, GLint, GLsizei, GLenum, GLsizei,
                                  const void*) = nullptr;
};

TEST_F(Texture1DCopyLibraryTest, ArgumentValidationAndCompressedListErrorsWorkWithoutAContext) {
    std::array<GLubyte, 8> block{};
    copy_image_(GL_TEXTURE_2D_, 0, GL_RGBA8_, 0, 0, 4, 0);
    EXPECT_EQ(get_error_(), GL_INVALID_ENUM_);
    copy_image_(GL_TEXTURE_1D_, -1, GL_RGBA8_, 0, 0, 4, 0);
    EXPECT_EQ(get_error_(), GL_INVALID_VALUE_);
    copy_sub_image_(GL_TEXTURE_1D_, 0, -1, 0, 0, 4);
    EXPECT_EQ(get_error_(), GL_INVALID_VALUE_);
    compressed_image_(GL_TEXTURE_1D_, 0, GL_COMPRESSED_RGBA_, 4, 0, 8, block.data());
    EXPECT_EQ(get_error_(), GL_INVALID_ENUM_);
    compressed_sub_image_(GL_TEXTURE_1D_, 0, 0, 4, GL_COMPRESSED_RGBA_S3TC_DXT1_EXT_, -1,
                          block.data());
    EXPECT_EQ(get_error_(), GL_INVALID_VALUE_);

    const GLuint list = Get<GLuint (*)(GLsizei)>("glGenLists")(1);
    ASSERT_NE(list, 0u);
    auto new_list = Get<void (*)(GLuint, GLenum)>("glNewList");
    auto end_list = Get<void (*)()>("glEndList");
    auto call_list = Get<void (*)(GLuint)>("glCallList");
    auto delete_lists = Get<void (*)(GLuint, GLsizei)>("glDeleteLists");
    new_list(list, GL_COMPILE_);
    compressed_image_(GL_TEXTURE_1D_, 0, GL_COMPRESSED_RGBA_S3TC_DXT1_EXT_, 4, 0, 8,
                      block.data());
    end_list();
    EXPECT_EQ(get_error_(), GL_NO_ERROR_)
        << "a compressed upload records during GL_COMPILE without executing";
    call_list(list);
    EXPECT_EQ(get_error_(), GL_INVALID_OPERATION_)
        << "the recorded block-compressed 1D command must fail on replay";
    delete_lists(list, 1);
}

TEST_F(Texture1DCopyTest, CopyAndSubCopyProduceTheExpectedSampledRows) {
    const GLuint texture = NewTexture();
    PaintRedGreenFramebuffer();
    copy_image_(GL_TEXTURE_1D_, 0, GL_RGBA8_, 0, size() / 2, size(), 0);
    ASSERT_EQ(get_error_(), GL_NO_ERROR_);

    EXPECT_EQ(Level(GL_TEXTURE_WIDTH_), size());
    EXPECT_EQ(Level(GL_TEXTURE_HEIGHT_), 1);
    EXPECT_EQ(Level(GL_TEXTURE_INTERNAL_FORMAT_), static_cast<GLint>(GL_RGBA8_));
    EXPECT_EQ(Level(GL_TEXTURE_COMPRESSED_), 0);

    clear_color_(0.0f, 0.0f, 0.0f, 1.0f);
    clear_(GL_COLOR_BUFFER_BIT_);
    DrawTexture();
    const PixelProbe::Rgba left = PixelAt(size() / 4);
    const PixelProbe::Rgba right = PixelAt(3 * size() / 4);
    EXPECT_GT(left.r, 200);
    EXPECT_LT(left.g, 40);
    EXPECT_GT(right.g, 200);
    EXPECT_LT(right.r, 40);

    // Replace the middle quarter from a blue source row. The two ends must
    // retain the original copy, proving xoffset and width survive the 1D->2D
    // mapping rather than redefining the whole image.
    disable_(GL_TEXTURE_1D_);
    clear_color_(0.0f, 0.0f, 1.0f, 1.0f);
    clear_(GL_COLOR_BUFFER_BIT_);
    copy_sub_image_(GL_TEXTURE_1D_, 0, size() * 3 / 8, 0, size() / 2, size() / 4);
    ASSERT_EQ(get_error_(), GL_NO_ERROR_);
    clear_color_(0.0f, 0.0f, 0.0f, 1.0f);
    clear_(GL_COLOR_BUFFER_BIT_);
    DrawTexture();
    const PixelProbe::Rgba middle = PixelAt(size() / 2);
    EXPECT_GT(PixelAt(size() / 8).r, 200);
    EXPECT_GT(middle.b, 200);
    EXPECT_LT(middle.r, 40);
    EXPECT_GT(PixelAt(7 * size() / 8).g, 200);

    GLint untouched = 91;
    get_level_(GL_TEXTURE_1D_, 0, GL_TEXTURE_COMPRESSED_IMAGE_SIZE_, &untouched);
    EXPECT_EQ(get_error_(), GL_INVALID_OPERATION_);
    delete_textures_(1, &texture);
}

TEST_F(Texture1DCopyTest, InvalidInputsUseTheDesktopErrorContract) {
    const GLuint texture = NewTexture();
    std::array<GLubyte, 8> block{};

    copy_image_(GL_TEXTURE_2D_, 0, GL_RGBA8_, 0, 0, 4, 0);
    EXPECT_EQ(get_error_(), GL_INVALID_ENUM_);
    copy_image_(GL_TEXTURE_1D_, 0, GL_RGBA8_, 0, 0, 4, 1);
    EXPECT_EQ(get_error_(), GL_INVALID_VALUE_);
    copy_image_(GL_TEXTURE_1D_, -1, GL_RGBA8_, 0, 0, 4, 0);
    EXPECT_EQ(get_error_(), GL_INVALID_VALUE_);
    copy_sub_image_(GL_TEXTURE_2D_, 0, 0, 0, 0, 4);
    EXPECT_EQ(get_error_(), GL_INVALID_ENUM_);
    copy_sub_image_(GL_TEXTURE_1D_, 0, -1, 0, 0, 4);
    EXPECT_EQ(get_error_(), GL_INVALID_VALUE_);

    compressed_image_(GL_TEXTURE_1D_, 0, GL_COMPRESSED_RGBA_S3TC_DXT1_EXT_, 4, 0, -1,
                      block.data());
    EXPECT_EQ(get_error_(), GL_INVALID_VALUE_);
    compressed_image_(GL_TEXTURE_1D_, 0, GL_COMPRESSED_RGBA_, 4, 0, 8, block.data());
    EXPECT_EQ(get_error_(), GL_INVALID_ENUM_);
    compressed_image_(GL_TEXTURE_1D_, 0, GL_COMPRESSED_RGBA_S3TC_DXT1_EXT_, 4, 0, 8,
                      block.data());
    EXPECT_EQ(get_error_(), GL_INVALID_OPERATION_);
    compressed_sub_image_(GL_TEXTURE_1D_, 0, 0, 4, GL_COMPRESSED_RGBA_S3TC_DXT1_EXT_, 8,
                          block.data());
    EXPECT_EQ(get_error_(), GL_INVALID_OPERATION_);

    // Proxy targets validate but allocate no N x 1 backend storage.
    compressed_image_(GL_PROXY_TEXTURE_1D_, 0, GL_COMPRESSED_RGBA_S3TC_DXT1_EXT_, 4, 0, 8,
                      block.data());
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
    delete_textures_(1, &texture);
}

TEST_F(Texture1DCopyTest, CopyAndCompressedFailureReplayFromDisplayLists) {
    const GLuint texture = NewTexture();
    PaintRedGreenFramebuffer();
    copy_image_(GL_TEXTURE_1D_, 0, GL_RGBA8_, 0, size() / 2, size(), 0);
    ASSERT_EQ(get_error_(), GL_NO_ERROR_);

    auto gen_lists = Get<GLuint (*)(GLsizei)>("glGenLists");
    auto new_list = Get<void (*)(GLuint, GLenum)>("glNewList");
    auto end_list = Get<void (*)()>("glEndList");
    auto call_list = Get<void (*)(GLuint)>("glCallList");
    auto delete_lists = Get<void (*)(GLuint, GLsizei)>("glDeleteLists");
    const GLuint copy_list = gen_lists(1);
    ASSERT_NE(copy_list, 0u);
    new_list(copy_list, GL_COMPILE_);
    copy_image_(GL_TEXTURE_1D_, 0, GL_RGBA8_, 0, size() / 2, 8, 0);
    end_list();
    EXPECT_EQ(Level(GL_TEXTURE_WIDTH_), size());
    call_list(copy_list);
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
    EXPECT_EQ(Level(GL_TEXTURE_WIDTH_), 8);

    const GLuint compressed_list = gen_lists(1);
    ASSERT_NE(compressed_list, 0u);
    std::array<GLubyte, 8> block{};
    new_list(compressed_list, GL_COMPILE_);
    compressed_image_(GL_TEXTURE_1D_, 0, GL_COMPRESSED_RGBA_S3TC_DXT1_EXT_, 4, 0, 8,
                      block.data());
    end_list();
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
    std::fill(block.begin(), block.end(), 0xff); // replay owns the original bytes
    call_list(compressed_list);
    EXPECT_EQ(get_error_(), GL_INVALID_OPERATION_);

    delete_lists(copy_list, 1);
    delete_lists(compressed_list, 1);
    delete_textures_(1, &texture);
}

TEST_F(LibraryTest, Texture1DHistoricalAliasesResolveToTheWrappers) {
    EXPECT_EQ(Get<void*>("glCopyTexImage1DEXT"), Get<void*>("glCopyTexImage1D"));
    EXPECT_EQ(Get<void*>("glCopyTexSubImage1DEXT"), Get<void*>("glCopyTexSubImage1D"));
    EXPECT_EQ(Get<void*>("glCompressedTexImage1DARB"), Get<void*>("glCompressedTexImage1D"));
    EXPECT_EQ(Get<void*>("glCompressedTexSubImage1DARB"),
              Get<void*>("glCompressedTexSubImage1D"));
    EXPECT_EQ(Get<void*>("glGetCompressedTexImageARB"),
              Get<void*>("glGetCompressedTexImage"));
}

TEST_F(Texture1DCopyTest, CompressedReadbackFailsLoudlyAndDoesNotTouchMemoryOnGles) {
    auto get_compressed =
        Get<void (*)(GLenum, GLint, void*)>("glGetCompressedTexImage");
    std::array<GLubyte, 8> output;
    output.fill(0xa5);
    get_compressed(GL_TEXTURE_2D_, 0, output.data());
    EXPECT_EQ(get_error_(), GL_INVALID_OPERATION_);
    EXPECT_TRUE(std::all_of(output.begin(), output.end(), [](GLubyte v) { return v == 0xa5; }));

    get_compressed(GL_TEXTURE_2D_, -1, output.data());
    EXPECT_EQ(get_error_(), GL_INVALID_VALUE_);
    get_compressed(0xdead, 0, output.data());
    EXPECT_EQ(get_error_(), GL_INVALID_ENUM_);
}

class Texture1DCopyDesktopTest : public DesktopContextTest {
protected:
    void SetUp() override {
        DesktopContextTest::SetUp();
        if (::testing::Test::IsSkipped() || ::testing::Test::HasFatalFailure()) return;
        using MakeCurrentFn = EGLBoolean (*)(EGLDisplay, EGLSurface, EGLSurface, EGLContext);
        auto wrapper_make_current = Get<MakeCurrentFn>("eglMakeCurrent");
        ASSERT_TRUE(wrapper_make_current(display(), surface(), surface(), eglGetCurrentContext()));
    }
};

TEST_F(Texture1DCopyDesktopTest, DesktopCompressedReadbackRoundTripsTheDriverPayload) {
    auto get_error = Get<GLenum (*)()>("glGetError");
    auto gen_textures = Get<void (*)(GLsizei, GLuint*)>("glGenTextures");
    auto bind_texture = Get<void (*)(GLenum, GLuint)>("glBindTexture");
    auto delete_textures = Get<void (*)(GLsizei, const GLuint*)>("glDeleteTextures");
    auto upload = Get<void (*)(GLenum, GLint, GLenum, GLsizei, GLsizei, GLint, GLsizei,
                               const void*)>("glCompressedTexImage2D");
    auto readback = Get<void (*)(GLenum, GLint, void*)>("glGetCompressedTexImage");
    auto readback_arb = Get<void (*)(GLenum, GLint, void*)>("glGetCompressedTexImageARB");
    EXPECT_EQ(reinterpret_cast<void*>(readback), reinterpret_cast<void*>(readback_arb));

    while (get_error() != GL_NO_ERROR_) {}
    GLuint texture = 0;
    gen_textures(1, &texture);
    ASSERT_NE(texture, 0u);
    bind_texture(GL_TEXTURE_2D_, texture);
    const std::array<GLubyte, 8> source = {0x00, 0xf8, 0xe0, 0x07, 0xe4, 0xe4, 0xe4, 0xe4};
    upload(GL_TEXTURE_2D_, 0, GL_COMPRESSED_RGBA_S3TC_DXT1_EXT_, 4, 4, 0,
           static_cast<GLsizei>(source.size()), source.data());
    const GLenum upload_error = get_error();
    if (upload_error != GL_NO_ERROR_) {
        delete_textures(1, &texture);
        GTEST_SKIP() << "desktop core backend does not expose S3TC (error 0x" << std::hex
                     << upload_error << ')';
    }

    std::array<GLubyte, 8> output{};
    readback(GL_TEXTURE_2D_, 0, output.data());
    EXPECT_EQ(get_error(), GL_NO_ERROR_);
    EXPECT_EQ(output, source);
    delete_textures(1, &texture);
}

} // namespace
