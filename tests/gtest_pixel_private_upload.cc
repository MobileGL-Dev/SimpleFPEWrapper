// SimpleFPEWrapper - tests/gtest_pixel_private_upload.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// plans/17 P6/P7: the pixel paths upload buffers of their OWN - the stencil
// bit-plane source glDrawPixels(GL_STENCIL_INDEX) builds, the 1x1 white
// texture the accumulation emulation multiplies by, the accumulation storage
// itself - and those uploads are tightly packed client memory that the
// caller's GL_UNPACK_* state and bound GL_PIXEL_UNPACK_BUFFER must not be
// allowed to reinterpret. Leaving GL_UNPACK_ROW_LENGTH set makes the driver
// read far past the end of a private heap buffer (ASAN: heap-buffer-overflow);
// leaving an unpack buffer bound turns the private heap address into a byte
// offset into that buffer, and the upload fails outright.

#include "sfpew_gtest.h"

#include <array>

namespace {

using sfpew_test::ContextTest;
using sfpew_test::GLbitfield;
using sfpew_test::GLenum;
using sfpew_test::GLfloat;
using sfpew_test::GLint;
using sfpew_test::GLsizei;
using sfpew_test::GLsizeiptr;
using sfpew_test::GLubyte;
using sfpew_test::GLuint;
using sfpew_test::PixelProbe;

constexpr GLenum GL_NO_ERROR_ = 0;
constexpr GLbitfield GL_COLOR_BUFFER_BIT_ = 0x00004000;
constexpr GLbitfield GL_DEPTH_BUFFER_BIT_ = 0x00000100;
constexpr GLbitfield GL_STENCIL_BUFFER_BIT_ = 0x00000400;
constexpr GLenum GL_STENCIL_INDEX_ = 0x1901;
constexpr GLenum GL_UNSIGNED_BYTE_ = 0x1401;
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
constexpr GLenum GL_UNPACK_ROW_LENGTH_ = 0x0CF2;
constexpr GLenum GL_PIXEL_UNPACK_BUFFER_ = 0x88EC;
constexpr GLenum GL_STATIC_DRAW_ = 0x88E4;
constexpr GLenum GL_ACCUM_ = 0x0100;
constexpr GLenum GL_LOAD_ = 0x0101;
constexpr GLenum GL_ADD_ = 0x0104;
constexpr GLenum GL_RETURN_ = 0x0102;

// A 4x4 stencil image of sixteen distinct non-zero indices, drawn at four
// window pixels per texel so no probe lands on the quad's internal diagonal
// (see gtest_drawpixels_stencil.cc for why texel centers on that seam are a
// rasterizer boundary case, not a wrapper bug).
constexpr int kImage = 4;
constexpr int kZoom = 4;
constexpr GLubyte kIndices[kImage * kImage] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
                                               0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x0F, 0x1F};

class StencilPrivateUploadTest : public ContextTest {
protected:
    void SetUp() override {
        ContextTest::SetUp();
        if (::testing::Test::IsSkipped() || ::testing::Test::HasFatalFailure()) return;
        get_error_ = Get<GLenum (*)()>("glGetError");
        viewport_ = Get<void (*)(GLint, GLint, GLsizei, GLsizei)>("glViewport");
        clear_color_ = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glClearColor");
        clear_stencil_ = Get<void (*)(GLint)>("glClearStencil");
        stencil_mask_ = Get<void (*)(GLuint)>("glStencilMask");
        clear_ = Get<void (*)(GLbitfield)>("glClear");
        window_pos_ = Get<void (*)(GLint, GLint)>("glWindowPos2i");
        pixel_zoom_ = Get<void (*)(GLfloat, GLfloat)>("glPixelZoom");
        draw_pixels_ = Get<void (*)(GLsizei, GLsizei, GLenum, GLenum, const void*)>("glDrawPixels");
        read_pixels_ = Get<void (*)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*)>(
            "glReadPixels");
        pixel_store_ = Get<void (*)(GLenum, GLint)>("glPixelStorei");
        gen_buffers_ = Get<void (*)(GLsizei, GLuint*)>("glGenBuffers");
        bind_buffer_ = Get<void (*)(GLenum, GLuint)>("glBindBuffer");
        buffer_data_ = Get<void (*)(GLenum, GLsizeiptr, const void*, GLenum)>("glBufferData");
        delete_buffers_ = Get<void (*)(GLsizei, const GLuint*)>("glDeleteBuffers");

        auto gen_textures = Get<void (*)(GLsizei, GLuint*)>("glGenTextures");
        auto bind_texture = Get<void (*)(GLenum, GLuint)>("glBindTexture");
        auto tex_parameter = Get<void (*)(GLenum, GLenum, GLint)>("glTexParameteri");
        auto tex_image = Get<void (*)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum,
                                      const void*)>("glTexImage2D");
        auto gen_framebuffers = Get<void (*)(GLsizei, GLuint*)>("glGenFramebuffers");
        auto bind_framebuffer = Get<void (*)(GLenum, GLuint)>("glBindFramebuffer");
        auto check_framebuffer = Get<GLenum (*)(GLenum)>("glCheckFramebufferStatus");
        auto framebuffer_texture =
            Get<void (*)(GLenum, GLenum, GLenum, GLuint, GLint)>("glFramebufferTexture2D");
        auto framebuffer_renderbuffer =
            Get<void (*)(GLenum, GLenum, GLenum, GLuint)>("glFramebufferRenderbuffer");
        auto gen_renderbuffers = Get<void (*)(GLsizei, GLuint*)>("glGenRenderbuffers");
        auto bind_renderbuffer = Get<void (*)(GLenum, GLuint)>("glBindRenderbuffer");
        auto renderbuffer_storage =
            Get<void (*)(GLenum, GLenum, GLsizei, GLsizei)>("glRenderbufferStorage");

        GLuint texture = 0, renderbuffer = 0, framebuffer = 0;
        gen_textures(1, &texture);
        bind_texture(GL_TEXTURE_2D_, texture);
        tex_parameter(GL_TEXTURE_2D_, GL_TEXTURE_MIN_FILTER_, GL_NEAREST_);
        tex_parameter(GL_TEXTURE_2D_, GL_TEXTURE_MAG_FILTER_, GL_NEAREST_);
        tex_image(GL_TEXTURE_2D_, 0, GL_RGBA8_, size(), size(), 0, GL_RGBA_, GL_UNSIGNED_BYTE_,
                  nullptr);
        gen_renderbuffers(1, &renderbuffer);
        bind_renderbuffer(GL_RENDERBUFFER_, renderbuffer);
        renderbuffer_storage(GL_RENDERBUFFER_, GL_DEPTH24_STENCIL8_, size(), size());
        gen_framebuffers(1, &framebuffer);
        bind_framebuffer(GL_FRAMEBUFFER_, framebuffer);
        framebuffer_texture(GL_FRAMEBUFFER_, GL_COLOR_ATTACHMENT0_, GL_TEXTURE_2D_, texture, 0);
        framebuffer_renderbuffer(GL_FRAMEBUFFER_, GL_DEPTH_STENCIL_ATTACHMENT_, GL_RENDERBUFFER_,
                                 renderbuffer);
        ASSERT_EQ(check_framebuffer(GL_FRAMEBUFFER_), GL_FRAMEBUFFER_COMPLETE_);

        viewport_(0, 0, size(), size());
        stencil_mask_(0xFFu);
        clear_color_(0.0f, 0.0f, 0.0f, 1.0f);
        clear_stencil_(0);
        clear_(GL_COLOR_BUFFER_BIT_ | GL_DEPTH_BUFFER_BIT_ | GL_STENCIL_BUFFER_BIT_);
        pixel_zoom_(static_cast<GLfloat>(kZoom), static_cast<GLfloat>(kZoom));
        ASSERT_EQ(get_error_(), GL_NO_ERROR_) << "FBO setup";
    }

    GLubyte StencilAt(int x, int y) const {
        GLubyte value = 0;
        read_pixels_(x, y, 1, 1, GL_STENCIL_INDEX_, GL_UNSIGNED_BYTE_, &value);
        return value;
    }

    // Every texel of the 4x4 image, probed one pixel in from its own corner.
    void ExpectImageLanded(int x0, int y0) const {
        for (int row = 0; row < kImage; ++row) {
            for (int column = 0; column < kImage; ++column) {
                EXPECT_EQ(static_cast<int>(StencilAt(x0 + column * kZoom + 1,
                                                     y0 + row * kZoom + 1)),
                          static_cast<int>(kIndices[row * kImage + column]))
                    << "texel (" << column << ", " << row << ")";
            }
        }
    }

    GLenum (*get_error_)() = nullptr;
    void (*viewport_)(GLint, GLint, GLsizei, GLsizei) = nullptr;
    void (*clear_color_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*clear_stencil_)(GLint) = nullptr;
    void (*stencil_mask_)(GLuint) = nullptr;
    void (*clear_)(GLbitfield) = nullptr;
    void (*window_pos_)(GLint, GLint) = nullptr;
    void (*pixel_zoom_)(GLfloat, GLfloat) = nullptr;
    void (*draw_pixels_)(GLsizei, GLsizei, GLenum, GLenum, const void*) = nullptr;
    void (*read_pixels_)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*) = nullptr;
    void (*pixel_store_)(GLenum, GLint) = nullptr;
    void (*gen_buffers_)(GLsizei, GLuint*) = nullptr;
    void (*bind_buffer_)(GLenum, GLuint) = nullptr;
    void (*buffer_data_)(GLenum, GLsizeiptr, const void*, GLenum) = nullptr;
    void (*delete_buffers_)(GLsizei, const GLuint*) = nullptr;
};

TEST_F(StencilPrivateUploadTest, CallerRowLengthDoesNotReachTheBitPlaneUpload) {
    // The caller's own source really is laid out at this row length, so the
    // read of IT is correct either way; what must not happen is the same row
    // length being applied to the 16-byte bit-plane buffer the wrapper builds
    // from it, which under ASAN is a heap-buffer-overflow read of ~196 bytes.
    constexpr int kRowLength = 64;
    std::array<GLubyte, kRowLength * kImage> source{};
    for (int row = 0; row < kImage; ++row)
        for (int column = 0; column < kImage; ++column)
            source[static_cast<size_t>(row * kRowLength + column)] =
                kIndices[row * kImage + column];

    pixel_store_(GL_UNPACK_ROW_LENGTH_, kRowLength);
    window_pos_(8, 8);
    draw_pixels_(kImage, kImage, GL_STENCIL_INDEX_, GL_UNSIGNED_BYTE_, source.data());
    pixel_store_(GL_UNPACK_ROW_LENGTH_, 0);

    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
    ExpectImageLanded(8, 8);
}

TEST_F(StencilPrivateUploadTest, BoundUnpackBufferDoesNotReachTheBitPlaneUpload) {
    // The source comes out of the buffer correctly (that path already
    // consults the binding); the bit-plane buffer that follows is client
    // memory, and with the buffer still bound its address is taken as an
    // offset into the buffer - the upload fails and the rectangle is never
    // painted.
    GLuint pbo = 0;
    gen_buffers_(1, &pbo);
    ASSERT_NE(pbo, 0u);
    bind_buffer_(GL_PIXEL_UNPACK_BUFFER_, pbo);
    buffer_data_(GL_PIXEL_UNPACK_BUFFER_, static_cast<GLsizeiptr>(sizeof(kIndices)), kIndices,
                 GL_STATIC_DRAW_);

    window_pos_(8, 8);
    draw_pixels_(kImage, kImage, GL_STENCIL_INDEX_, GL_UNSIGNED_BYTE_, nullptr);

    bind_buffer_(GL_PIXEL_UNPACK_BUFFER_, 0);
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
    ExpectImageLanded(8, 8);
    delete_buffers_(1, &pbo);
}

class AccumPrivateUploadTest : public ContextTest {
protected:
    void SetUp() override {
        ContextTest::SetUp();
        if (::testing::Test::IsSkipped() || ::testing::Test::HasFatalFailure()) return;
        using MakeCurrentFn = EGLBoolean (*)(EGLDisplay, EGLSurface, EGLSurface, EGLContext);
        auto wrapper_make_current = Get<MakeCurrentFn>("eglMakeCurrent");
        ASSERT_TRUE(wrapper_make_current(display(), surface(), surface(), eglGetCurrentContext()));

        get_error_ = Get<GLenum (*)()>("glGetError");
        viewport_ = Get<void (*)(GLint, GLint, GLsizei, GLsizei)>("glViewport");
        clear_color_ = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glClearColor");
        clear_ = Get<void (*)(GLbitfield)>("glClear");
        accum_ = Get<void (*)(GLenum, GLfloat)>("glAccum");
        read_pixels_ = Get<PixelProbe::ReadPixelsFn>("glReadPixels");
        gen_buffers_ = Get<void (*)(GLsizei, GLuint*)>("glGenBuffers");
        bind_buffer_ = Get<void (*)(GLenum, GLuint)>("glBindBuffer");
        buffer_data_ = Get<void (*)(GLenum, GLsizeiptr, const void*, GLenum)>("glBufferData");
        delete_buffers_ = Get<void (*)(GLsizei, const GLuint*)>("glDeleteBuffers");

        viewport_(0, 0, size(), size());
        // A judgement colour with R == B, so the llvmpipe BGRA swizzle quirk
        // cannot decide the outcome.
        clear_color_(0.2f, 0.2f, 0.2f, 1.0f);
        clear_(GL_COLOR_BUFFER_BIT_);
    }

    // GL_LOAD the cleared framebuffer, GL_ADD a constant, GL_RETURN it: the
    // GL_ADD leg is the one that multiplies the wrapper's private 1x1 white
    // texture by the constant, so a white texture that failed to upload turns
    // the whole sequence into a no-op.
    void AccumulateAndReturn() {
        accum_(GL_LOAD_, 1.0f);
        accum_(GL_ADD_, 0.25f);
        accum_(GL_RETURN_, 1.0f);
    }

    GLenum (*get_error_)() = nullptr;
    void (*viewport_)(GLint, GLint, GLsizei, GLsizei) = nullptr;
    void (*clear_color_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*clear_)(GLbitfield) = nullptr;
    void (*accum_)(GLenum, GLfloat) = nullptr;
    PixelProbe::ReadPixelsFn read_pixels_ = nullptr;
    void (*gen_buffers_)(GLsizei, GLuint*) = nullptr;
    void (*bind_buffer_)(GLenum, GLuint) = nullptr;
    void (*buffer_data_)(GLenum, GLsizeiptr, const void*, GLenum) = nullptr;
    void (*delete_buffers_)(GLsizei, const GLuint*) = nullptr;
};

TEST_F(AccumPrivateUploadTest, AccumulationWithoutABoundUnpackBufferIsTheBaseline) {
    AccumulateAndReturn();
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
    const auto pixel = PixelProbe(read_pixels_).At(32, 32);
    EXPECT_NEAR(pixel.r, 115, 8) << "0.2 accumulated plus 0.25";
    EXPECT_EQ(pixel.r, pixel.b) << "the source colour is symmetric in R and B";
}

TEST_F(AccumPrivateUploadTest, BoundUnpackBufferDoesNotReachTheAccumulationsOwnUploads) {
    // Two wrapper-private uploads run inside this sequence: the accumulation
    // buffer's own RGBA16F storage and the lazily created 1x1 white texture.
    // Both are glTexImage2D calls with client (or null) data, and this buffer
    // is far too small to serve either as an offset - so with it bound they
    // both fail, and the accumulated result never appears.
    const std::array<GLubyte, 16> junk{};
    GLuint pbo = 0;
    gen_buffers_(1, &pbo);
    ASSERT_NE(pbo, 0u);
    bind_buffer_(GL_PIXEL_UNPACK_BUFFER_, pbo);
    buffer_data_(GL_PIXEL_UNPACK_BUFFER_, static_cast<GLsizeiptr>(junk.size()), junk.data(),
                 GL_STATIC_DRAW_);

    AccumulateAndReturn();

    bind_buffer_(GL_PIXEL_UNPACK_BUFFER_, 0);
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
    const auto pixel = PixelProbe(read_pixels_).At(32, 32);
    EXPECT_NEAR(pixel.r, 115, 8) << "0.2 accumulated plus 0.25";
    EXPECT_EQ(pixel.r, pixel.b) << "the source colour is symmetric in R and B";
    delete_buffers_(1, &pbo);
}

} // namespace
