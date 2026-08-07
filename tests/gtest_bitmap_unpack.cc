// SimpleFPEWrapper - tests/gtest_bitmap_unpack.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// plans/17 P2/P3: how glBitmap reads the bitmap it is handed. GL 2.1 3.6.4
// gives bitmap rows a stride of a*ceil(l/(8a)) bytes (a = GL_UNPACK_ALIGNMENT,
// default FOUR, l = GL_UNPACK_ROW_LENGTH or width), with GL_UNPACK_SKIP_ROWS /
// GL_UNPACK_SKIP_PIXELS selecting a sub-rectangle - and, per glBitmap's own
// reference page, "if a non-zero named buffer object is bound to the
// GL_PIXEL_UNPACK_BUFFER target ... bitmap is treated as a byte offset into
// the buffer object's data store".
//
// The bitmap's row 0 is its BOTTOM row, so a bitmap drawn at window y0 puts
// row j at window row y0 + j.

#include "sfpew_gtest.h"

#include <array>
#include <string>

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
constexpr GLenum GL_UNPACK_ALIGNMENT_ = 0x0CF5;
constexpr GLenum GL_UNPACK_ROW_LENGTH_ = 0x0CF2;
constexpr GLenum GL_UNPACK_SKIP_ROWS_ = 0x0CF3;
constexpr GLenum GL_UNPACK_SKIP_PIXELS_ = 0x0CF4;
constexpr GLenum GL_PIXEL_UNPACK_BUFFER_ = 0x88EC;
constexpr GLenum GL_STATIC_DRAW_ = 0x88E4;

class BitmapUnpackTest : public ContextTest {
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
        color4f_ = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glColor4f");
        window_pos_ = Get<void (*)(GLint, GLint)>("glWindowPos2i");
        bitmap_ = Get<void (*)(GLsizei, GLsizei, GLfloat, GLfloat, GLfloat, GLfloat,
                               const GLubyte*)>("glBitmap");
        pixel_store_ = Get<void (*)(GLenum, GLint)>("glPixelStorei");
        read_pixels_ = Get<PixelProbe::ReadPixelsFn>("glReadPixels");
        gen_buffers_ = Get<void (*)(GLsizei, GLuint*)>("glGenBuffers");
        bind_buffer_ = Get<void (*)(GLenum, GLuint)>("glBindBuffer");
        buffer_data_ = Get<void (*)(GLenum, GLsizeiptr, const void*, GLenum)>("glBufferData");
        delete_buffers_ = Get<void (*)(GLsizei, const GLuint*)>("glDeleteBuffers");

        viewport_(0, 0, size(), size());
        clear_color_(0.0f, 0.0f, 0.0f, 1.0f);
        clear_(GL_COLOR_BUFFER_BIT_);
        // Green ink: the raster color is latched by glWindowPos2i, and green
        // is what PixelProbe::IsLitAt reads.
        color4f_(0.0f, 1.0f, 0.0f, 1.0f);
    }

    // Row `row` of the drawn bitmap as a lit/dark string, one character per
    // bitmap column, for readable failure output.
    std::string RowPattern(int x0, int y, int width) const {
        const PixelProbe probe(read_pixels_);
        std::string pattern;
        for (int i = 0; i < width; ++i) pattern.push_back(probe.IsLitAt(x0 + i, y) ? '#' : '.');
        return pattern;
    }

    GLenum (*get_error_)() = nullptr;
    void (*viewport_)(GLint, GLint, GLsizei, GLsizei) = nullptr;
    void (*clear_color_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*clear_)(GLbitfield) = nullptr;
    void (*color4f_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*window_pos_)(GLint, GLint) = nullptr;
    void (*bitmap_)(GLsizei, GLsizei, GLfloat, GLfloat, GLfloat, GLfloat, const GLubyte*) = nullptr;
    void (*pixel_store_)(GLenum, GLint) = nullptr;
    PixelProbe::ReadPixelsFn read_pixels_ = nullptr;
    void (*gen_buffers_)(GLsizei, GLuint*) = nullptr;
    void (*bind_buffer_)(GLenum, GLuint) = nullptr;
    void (*buffer_data_)(GLenum, GLsizeiptr, const void*, GLenum) = nullptr;
    void (*delete_buffers_)(GLsizei, const GLuint*) = nullptr;
};

TEST_F(BitmapUnpackTest, RowStrideFollowsUnpackAlignmentNotTightPacking) {
    // 8 wide at the DEFAULT alignment of 4: 4*ceil(8/32) = 4 bytes per row,
    // not the 1 byte a tight packing would use. Rows 0 and 2 are solid, rows
    // 1 and 3 empty; read at a stride of 1 the four rows come out of bytes
    // 0..3, i.e. lit/dark/dark/dark.
    const std::array<GLubyte, 16> rows = {0xFF, 0x00, 0x00, 0x00,  // row 0 (bottom)
                                          0x00, 0x00, 0x00, 0x00,  // row 1
                                          0xFF, 0x00, 0x00, 0x00,  // row 2
                                          0x00, 0x00, 0x00, 0x00}; // row 3
    GLint alignment = 0;
    Get<void (*)(GLenum, GLint*)>("glGetIntegerv")(GL_UNPACK_ALIGNMENT_, &alignment);
    ASSERT_EQ(alignment, 4) << "GL's default GL_UNPACK_ALIGNMENT is 4, and this case is about it";

    window_pos_(8, 8);
    bitmap_(8, 4, 0.0f, 0.0f, 0.0f, 0.0f, rows.data());
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);

    EXPECT_EQ(RowPattern(8, 8, 8), "########") << "bitmap row 0";
    EXPECT_EQ(RowPattern(8, 9, 8), "........") << "bitmap row 1";
    EXPECT_EQ(RowPattern(8, 10, 8), "########") << "bitmap row 2";
    EXPECT_EQ(RowPattern(8, 11, 8), "........") << "bitmap row 3";
}

TEST_F(BitmapUnpackTest, TightPackingStillWorksAtAlignmentOne) {
    // The companion of the case above: at alignment 1 the stride IS
    // ceil(width/8), so this one passed before the fix too and holds it in
    // place.
    const std::array<GLubyte, 4> rows = {0xFF, 0x00, 0xFF, 0x00};
    pixel_store_(GL_UNPACK_ALIGNMENT_, 1);
    window_pos_(8, 8);
    bitmap_(8, 4, 0.0f, 0.0f, 0.0f, 0.0f, rows.data());
    pixel_store_(GL_UNPACK_ALIGNMENT_, 4);
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);

    EXPECT_EQ(RowPattern(8, 8, 8), "########") << "bitmap row 0";
    EXPECT_EQ(RowPattern(8, 9, 8), "........") << "bitmap row 1";
    EXPECT_EQ(RowPattern(8, 10, 8), "########") << "bitmap row 2";
    EXPECT_EQ(RowPattern(8, 11, 8), "........") << "bitmap row 3";
}

TEST_F(BitmapUnpackTest, RowLengthAndSkipsSelectASubRectangle) {
    // ROW_LENGTH 24 at alignment 1: 3 bytes per row. SKIP_ROWS 1 skips the
    // first row; SKIP_PIXELS 4 starts four BITS into the row, which is a bit
    // offset, not a byte one - it is what makes rows 0x0F/0x00 and 0x00/0xF0
    // read as 11110000 and 00001111 for the 8 columns the bitmap asks for.
    const std::array<GLubyte, 9> source = {0xAA, 0xAA, 0xAA,  // skipped row
                                           0x0F, 0x00, 0x00,  // bitmap row 0: pixels 4..7 set
                                           0x00, 0xF0, 0x00}; // bitmap row 1: pixels 8..11 set
    pixel_store_(GL_UNPACK_ALIGNMENT_, 1);
    pixel_store_(GL_UNPACK_ROW_LENGTH_, 24);
    pixel_store_(GL_UNPACK_SKIP_ROWS_, 1);
    pixel_store_(GL_UNPACK_SKIP_PIXELS_, 4);
    window_pos_(8, 8);
    bitmap_(8, 2, 0.0f, 0.0f, 0.0f, 0.0f, source.data());
    pixel_store_(GL_UNPACK_ALIGNMENT_, 4);
    pixel_store_(GL_UNPACK_ROW_LENGTH_, 0);
    pixel_store_(GL_UNPACK_SKIP_ROWS_, 0);
    pixel_store_(GL_UNPACK_SKIP_PIXELS_, 0);
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);

    EXPECT_EQ(RowPattern(8, 8, 8), "####....") << "bitmap row 0";
    EXPECT_EQ(RowPattern(8, 9, 8), "....####") << "bitmap row 1";
}

TEST_F(BitmapUnpackTest, BitmapIsReadOutOfTheBoundUnpackBuffer) {
    // With a pixel unpack buffer bound, `bitmap` is a byte offset into it.
    // Dereferencing it as a client pointer is a SIGSEGV for any non-zero
    // offset (plans/17 P2), so a crash here IS the failure.
    const std::array<GLubyte, 8> source = {0xDE, 0xAD, 0xBE, 0xEF,  // padding before the offset
                                           0xFF, 0x00, 0xFF, 0x00}; // four rows, stride 1
    GLuint pbo = 0;
    gen_buffers_(1, &pbo);
    ASSERT_NE(pbo, 0u);
    bind_buffer_(GL_PIXEL_UNPACK_BUFFER_, pbo);
    buffer_data_(GL_PIXEL_UNPACK_BUFFER_, static_cast<GLsizeiptr>(source.size()), source.data(),
                 GL_STATIC_DRAW_);
    pixel_store_(GL_UNPACK_ALIGNMENT_, 1);

    window_pos_(8, 8);
    bitmap_(8, 4, 0.0f, 0.0f, 0.0f, 0.0f, reinterpret_cast<const GLubyte*>(4));

    pixel_store_(GL_UNPACK_ALIGNMENT_, 4);
    bind_buffer_(GL_PIXEL_UNPACK_BUFFER_, 0);
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
    delete_buffers_(1, &pbo);

    EXPECT_EQ(RowPattern(8, 8, 8), "########") << "bitmap row 0";
    EXPECT_EQ(RowPattern(8, 9, 8), "........") << "bitmap row 1";
    EXPECT_EQ(RowPattern(8, 10, 8), "########") << "bitmap row 2";
    EXPECT_EQ(RowPattern(8, 11, 8), "........") << "bitmap row 3";
}

TEST_F(BitmapUnpackTest, ZeroOffsetIntoTheUnpackBufferIsNotDroppedAsNull) {
    // Offset 0 is the one non-crashing case of the defect above, and the one
    // that fails silently instead: a null `bitmap` short-circuits the draw
    // even though the bits are sitting in the bound buffer.
    const std::array<GLubyte, 4> source = {0xFF, 0x00, 0xFF, 0x00};
    GLuint pbo = 0;
    gen_buffers_(1, &pbo);
    ASSERT_NE(pbo, 0u);
    bind_buffer_(GL_PIXEL_UNPACK_BUFFER_, pbo);
    buffer_data_(GL_PIXEL_UNPACK_BUFFER_, static_cast<GLsizeiptr>(source.size()), source.data(),
                 GL_STATIC_DRAW_);
    pixel_store_(GL_UNPACK_ALIGNMENT_, 1);

    window_pos_(8, 8);
    bitmap_(8, 4, 0.0f, 0.0f, 0.0f, 0.0f, nullptr);

    pixel_store_(GL_UNPACK_ALIGNMENT_, 4);
    bind_buffer_(GL_PIXEL_UNPACK_BUFFER_, 0);
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
    delete_buffers_(1, &pbo);

    EXPECT_EQ(RowPattern(8, 8, 8), "########") << "bitmap row 0";
    EXPECT_EQ(RowPattern(8, 10, 8), "########") << "bitmap row 2";
}

} // namespace
