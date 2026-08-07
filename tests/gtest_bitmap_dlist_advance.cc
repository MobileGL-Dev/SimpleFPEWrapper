// SimpleFPEWrapper - tests/gtest_bitmap_dlist_advance.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// plans/17 P13: glBitmap's recording used to sit INSIDE the
// "width > 0 && height > 0 && bitmap != nullptr" drawing guard and AFTER the
// raster-position-valid early return, so a glBitmap carrying no ink was never
// compiled while its raster advance still ran at compile time.
//
// That form is not exotic. /docsgl/gl2/glBitmap.xhtml's own Notes give it as
// THE way to move the raster position outside the viewport ("call glBitmap
// with NULL as the bitmap parameter and with xmove and ymove set to the
// offsets of the new raster position"), and glXUseXFont/wglUseFontBitmaps
// emit exactly that shape for a blank glyph - one display list per character,
// space being glBitmap(0, 0, 0, 0, 8, 0, NULL). With the list empty, the next
// glyph paints where the space should have moved past.
//
// The payload half is P15's rule applied here: the bitmap is snapshotted at
// COMPILE time under the pixel-store state in effect then, and glPixelStore's
// reference page says the state at execution "is not significant".
//
// Ink is green: PixelProbe::IsLitAt reads the green channel, and llvmpipe's
// R/B swizzle cannot reach it.

#include "sfpew_gtest.h"

#include <array>
#include <string>
#include <vector>

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
constexpr GLenum GL_COMPILE_ = 0x1300;
constexpr GLenum GL_COMPILE_AND_EXECUTE_ = 0x1301;
constexpr GLenum GL_UNPACK_ALIGNMENT_ = 0x0CF5;
constexpr GLenum GL_UNPACK_ROW_LENGTH_ = 0x0CF2;
constexpr GLenum GL_UNPACK_SKIP_ROWS_ = 0x0CF3;
constexpr GLenum GL_UNPACK_SKIP_PIXELS_ = 0x0CF4;
constexpr GLenum GL_UNPACK_LSB_FIRST_ = 0x0CF1;
constexpr GLenum GL_PIXEL_UNPACK_BUFFER_ = 0x88EC;
constexpr GLenum GL_STATIC_DRAW_ = 0x88E4;
constexpr GLenum GL_CURRENT_RASTER_POSITION_ = 0x0B07;
constexpr GLenum GL_CURRENT_RASTER_POSITION_VALID_ = 0x0B08;

// The glyph every case draws: 8 wide, 8 tall, solid. At the default
// GL_UNPACK_ALIGNMENT of 4 its rows are four bytes apart (GL 2.1 3.6.4,
// a*ceil(l/(8a))), which commit 0a3969a corrected and this file relies on.
constexpr GLsizei kGlyph = 8;
constexpr GLsizei kAdvance = 8;
std::array<GLubyte, 32> SolidGlyph() {
    std::array<GLubyte, 32> rows{};
    for (int row = 0; row < kGlyph; ++row) rows[static_cast<size_t>(row) * 4u] = 0xFF;
    return rows;
}

class BitmapListAdvanceTest : public ContextTest {
protected:
    void SetUp() override {
        ContextTest::SetUp();
        if (::testing::Test::IsSkipped() || ::testing::Test::HasFatalFailure()) return;
        using MakeCurrentFn = EGLBoolean (*)(EGLDisplay, EGLSurface, EGLSurface, EGLContext);
        auto wrapper_make_current = Get<MakeCurrentFn>("eglMakeCurrent");
        ASSERT_TRUE(wrapper_make_current(display(), surface(), surface(), eglGetCurrentContext()));

        get_error_ = Get<GLenum (*)()>("glGetError");
        get_integer_ = Get<void (*)(GLenum, GLint*)>("glGetIntegerv");
        get_float_ = Get<void (*)(GLenum, GLfloat*)>("glGetFloatv");
        viewport_ = Get<void (*)(GLint, GLint, GLsizei, GLsizei)>("glViewport");
        clear_color_ = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glClearColor");
        clear_ = Get<void (*)(GLbitfield)>("glClear");
        color4f_ = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glColor4f");
        window_pos_ = Get<void (*)(GLint, GLint)>("glWindowPos2i");
        raster_pos2f_ = Get<void (*)(GLfloat, GLfloat)>("glRasterPos2f");
        bitmap_ = Get<void (*)(GLsizei, GLsizei, GLfloat, GLfloat, GLfloat, GLfloat,
                               const GLubyte*)>("glBitmap");
        pixel_store_ = Get<void (*)(GLenum, GLint)>("glPixelStorei");
        read_pixels_ = Get<PixelProbe::ReadPixelsFn>("glReadPixels");
        gen_buffers_ = Get<void (*)(GLsizei, GLuint*)>("glGenBuffers");
        bind_buffer_ = Get<void (*)(GLenum, GLuint)>("glBindBuffer");
        buffer_data_ = Get<void (*)(GLenum, GLsizeiptr, const void*, GLenum)>("glBufferData");
        delete_buffers_ = Get<void (*)(GLsizei, const GLuint*)>("glDeleteBuffers");
        gen_lists_ = Get<GLuint (*)(GLsizei)>("glGenLists");
        new_list_ = Get<void (*)(GLuint, GLenum)>("glNewList");
        end_list_ = Get<void (*)()>("glEndList");
        call_list_ = Get<void (*)(GLuint)>("glCallList");
        delete_lists_ = Get<void (*)(GLuint, GLsizei)>("glDeleteLists");
        matrix_mode_ = Get<void (*)(GLenum)>("glMatrixMode");
        load_identity_ = Get<void (*)()>("glLoadIdentity");

        viewport_(0, 0, size(), size());
        matrix_mode_(0x1701 /* GL_PROJECTION */);
        load_identity_();
        matrix_mode_(0x1700 /* GL_MODELVIEW */);
        load_identity_();
        clear_color_(0.0f, 0.0f, 0.0f, 1.0f);
        clear_(GL_COLOR_BUFFER_BIT_);
        color4f_(0.0f, 1.0f, 0.0f, 1.0f);
        Drain();
    }

    void Drain() {
        while (get_error_() != GL_NO_ERROR_) {}
    }

    void Clear() { clear_(GL_COLOR_BUFFER_BIT_); }

    // The x of the leftmost lit column on row y, or -1.
    int FirstLitColumn(int y) const {
        const PixelProbe probe(read_pixels_);
        for (int x = 0; x < size(); ++x) {
            if (probe.IsLitAt(x, y)) return x;
        }
        return -1;
    }

    int LitCount() const { return PixelProbe(read_pixels_).FindLit(0, 0, size(), size()).count; }

    std::vector<GLubyte> Snapshot() const {
        std::vector<GLubyte> pixels(static_cast<size_t>(size()) * size() * 4u);
        read_pixels_(0, 0, size(), size(), 0x1908 /* GL_RGBA */, 0x1401 /* GL_UNSIGNED_BYTE */,
                     pixels.data());
        return pixels;
    }

    static int Differences(const std::vector<GLubyte>& a, const std::vector<GLubyte>& b) {
        int differing = 0;
        for (size_t i = 0; i + 3 < a.size() && i + 3 < b.size(); i += 4) {
            if (a[i] != b[i] || a[i + 1] != b[i + 1] || a[i + 2] != b[i + 2] ||
                a[i + 3] != b[i + 3]) {
                ++differing;
            }
        }
        return differing;
    }

    GLfloat RasterX() const {
        GLfloat position[4] = {0, 0, 0, 0};
        get_float_(GL_CURRENT_RASTER_POSITION_, position);
        return position[0];
    }

    GLenum (*get_error_)() = nullptr;
    void (*get_integer_)(GLenum, GLint*) = nullptr;
    void (*get_float_)(GLenum, GLfloat*) = nullptr;
    void (*viewport_)(GLint, GLint, GLsizei, GLsizei) = nullptr;
    void (*clear_color_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*clear_)(GLbitfield) = nullptr;
    void (*color4f_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*window_pos_)(GLint, GLint) = nullptr;
    void (*raster_pos2f_)(GLfloat, GLfloat) = nullptr;
    void (*bitmap_)(GLsizei, GLsizei, GLfloat, GLfloat, GLfloat, GLfloat, const GLubyte*) = nullptr;
    void (*pixel_store_)(GLenum, GLint) = nullptr;
    PixelProbe::ReadPixelsFn read_pixels_ = nullptr;
    void (*gen_buffers_)(GLsizei, GLuint*) = nullptr;
    void (*bind_buffer_)(GLenum, GLuint) = nullptr;
    void (*buffer_data_)(GLenum, GLsizeiptr, const void*, GLenum) = nullptr;
    void (*delete_buffers_)(GLsizei, const GLuint*) = nullptr;
    GLuint (*gen_lists_)(GLsizei) = nullptr;
    void (*new_list_)(GLuint, GLenum) = nullptr;
    void (*end_list_)() = nullptr;
    void (*call_list_)(GLuint) = nullptr;
    void (*delete_lists_)(GLuint, GLsizei) = nullptr;
    void (*matrix_mode_)(GLenum) = nullptr;
    void (*load_identity_)() = nullptr;
};

// The canonical font layout: one display list per character, space being
// glBitmap(0, 0, 0, 0, 8, 0, NULL).
TEST_F(BitmapListAdvanceTest, ABlankGlyphListStillAdvancesTheRaster) {
    const auto glyph = SolidGlyph();

    const GLuint space = gen_lists_(1);
    ASSERT_NE(space, 0u);
    new_list_(space, GL_COMPILE_);
    bitmap_(0, 0, 0.0f, 0.0f, static_cast<GLfloat>(kAdvance), 0.0f, nullptr);
    end_list_();
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);

    const GLuint letter = gen_lists_(1);
    new_list_(letter, GL_COMPILE_);
    bitmap_(kGlyph, kGlyph, 0.0f, 0.0f, static_cast<GLfloat>(kAdvance), 0.0f, glyph.data());
    end_list_();

    Clear();
    window_pos_(0, 8);
    call_list_(space);
    call_list_(letter);
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);

    EXPECT_EQ(FirstLitColumn(8), kAdvance)
        << "the blank glyph's list was empty, so the letter painted where the space should have "
           "moved past";
    delete_lists_(space, 1);
    delete_lists_(letter, 1);
}

TEST_F(BitmapListAdvanceTest, TheAdvanceDoesNotRunWhileTheListCompiles) {
    window_pos_(0, 8);
    ASSERT_EQ(RasterX(), 0.0f);

    const GLuint space = gen_lists_(1);
    new_list_(space, GL_COMPILE_);
    bitmap_(0, 0, 0.0f, 0.0f, static_cast<GLfloat>(kAdvance), 0.0f, nullptr);
    end_list_();

    EXPECT_EQ(RasterX(), 0.0f) << "GL_COMPILE advanced the raster position instead of compiling";
    call_list_(space);
    EXPECT_EQ(RasterX(), static_cast<GLfloat>(kAdvance)) << "the replay did not advance the raster";
    delete_lists_(space, 1);
}

TEST_F(BitmapListAdvanceTest, BlankAndInkedGlyphsAreThreeWayEquivalent) {
    const auto glyph = SolidGlyph();
    auto emit = [&] {
        bitmap_(0, 0, 0.0f, 0.0f, static_cast<GLfloat>(kAdvance), 0.0f, nullptr);
        bitmap_(kGlyph, kGlyph, 0.0f, 0.0f, static_cast<GLfloat>(kAdvance), 0.0f, glyph.data());
    };

    Clear();
    const std::vector<GLubyte> cleared = Snapshot();
    window_pos_(0, 8);
    emit();
    const std::vector<GLubyte> reference = Snapshot();
    ASSERT_NE(Differences(cleared, reference), 0) << "the immediate run drew nothing";

    Clear();
    window_pos_(0, 8);
    const GLuint list = gen_lists_(1);
    new_list_(list, GL_COMPILE_);
    emit();
    end_list_();
    EXPECT_EQ(Differences(cleared, Snapshot()), 0) << "GL_COMPILE drew instead of compiling";
    EXPECT_EQ(RasterX(), 0.0f) << "GL_COMPILE advanced the raster position";
    call_list_(list);
    EXPECT_EQ(Differences(reference, Snapshot()), 0)
        << "the glCallList replay does not match the immediate run";

    Clear();
    window_pos_(0, 8);
    const GLuint both = gen_lists_(1);
    new_list_(both, GL_COMPILE_AND_EXECUTE_);
    emit();
    end_list_();
    EXPECT_EQ(Differences(reference, Snapshot()), 0) << "GL_COMPILE_AND_EXECUTE did not draw";
    Clear();
    window_pos_(0, 8);
    call_list_(both);
    EXPECT_EQ(Differences(reference, Snapshot()), 0)
        << "replaying a GL_COMPILE_AND_EXECUTE list";
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
    delete_lists_(list, 1);
    delete_lists_(both, 1);
}

TEST_F(BitmapListAdvanceTest, RecordingDoesNotDependOnTheRasterBeingValidWhenCompiled) {
    const auto glyph = SolidGlyph();

    // Raster validity is an execution-time condition. Compile with it false.
    raster_pos2f_(-5.0f, -5.0f);
    GLint valid = -1;
    get_integer_(GL_CURRENT_RASTER_POSITION_VALID_, &valid);
    ASSERT_EQ(valid, 0) << "this case needs an invalid raster position to compile under";

    const GLuint list = gen_lists_(1);
    new_list_(list, GL_COMPILE_);
    bitmap_(kGlyph, kGlyph, 0.0f, 0.0f, static_cast<GLfloat>(kAdvance), 0.0f, glyph.data());
    end_list_();
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);

    Clear();
    window_pos_(16, 8);
    call_list_(list);
    EXPECT_EQ(FirstLitColumn(8), 16)
        << "the glBitmap was dropped because the raster position was invalid while compiling";
    delete_lists_(list, 1);
}

// A guard rather than a reproduction: an INKED glBitmap was already recorded,
// and already snapshotted, before P13 - it is the payload-free form the
// drawing guard swallowed. This pins the snapshot down so the P13 restructure
// cannot lose it.
TEST_F(BitmapListAdvanceTest, ThePayloadIsSnapshotAtCompileTime) {
    std::array<GLubyte, 32> glyph = SolidGlyph();

    Clear();
    window_pos_(0, 8);
    bitmap_(kGlyph, kGlyph, 0.0f, 0.0f, 0.0f, 0.0f, glyph.data());
    const std::vector<GLubyte> reference = Snapshot();
    const int reference_lit = LitCount();
    ASSERT_EQ(reference_lit, kGlyph * kGlyph);

    const GLuint list = gen_lists_(1);
    new_list_(list, GL_COMPILE_);
    bitmap_(kGlyph, kGlyph, 0.0f, 0.0f, 0.0f, 0.0f, glyph.data());
    end_list_();

    // The list owns its copy: blank the source out from under it.
    glyph.fill(0x00);

    Clear();
    window_pos_(0, 8);
    call_list_(list);
    EXPECT_EQ(Differences(reference, Snapshot()), 0)
        << "the replay read the caller's buffer, which no longer holds the compiled glyph";
    delete_lists_(list, 1);
}

TEST_F(BitmapListAdvanceTest, ThePayloadFollowsTheCompileTimeUnpackState) {
    // The glyph laid out inside a wider image, two rows up and two BITS in:
    // GL_UNPACK_SKIP_PIXELS counts bits, so the sub-byte remainder is part of
    // what the compile-time state selects.
    constexpr GLint kRowLength = 24;
    constexpr size_t kStride = 4; // alignment 4, ceil(24/32)*4
    std::vector<GLubyte> image(kStride * 16u, 0x00);
    for (int row = 0; row < kGlyph; ++row) {
        // Eight lit columns starting at bit 2 of the row.
        image[(static_cast<size_t>(row) + 2u) * kStride + 0u] = 0x3F;
        image[(static_cast<size_t>(row) + 2u) * kStride + 1u] = 0xC0;
    }

    pixel_store_(GL_UNPACK_ROW_LENGTH_, kRowLength);
    pixel_store_(GL_UNPACK_SKIP_ROWS_, 2);
    pixel_store_(GL_UNPACK_SKIP_PIXELS_, 2);
    Clear();
    window_pos_(0, 8);
    bitmap_(kGlyph, kGlyph, 0.0f, 0.0f, 0.0f, 0.0f, image.data());
    const std::vector<GLubyte> reference = Snapshot();
    ASSERT_EQ(LitCount(), kGlyph * kGlyph) << "the immediate draw did not read the sub-rectangle";

    const GLuint list = gen_lists_(1);
    new_list_(list, GL_COMPILE_);
    bitmap_(kGlyph, kGlyph, 0.0f, 0.0f, 0.0f, 0.0f, image.data());
    end_list_();

    // glPixelStore: the modes in effect when a list is executed are not
    // significant. Change all of them, including the bit order.
    pixel_store_(GL_UNPACK_ROW_LENGTH_, 0);
    pixel_store_(GL_UNPACK_SKIP_ROWS_, 0);
    pixel_store_(GL_UNPACK_SKIP_PIXELS_, 0);
    pixel_store_(GL_UNPACK_ALIGNMENT_, 1);
    pixel_store_(GL_UNPACK_LSB_FIRST_, 1);
    Drain();

    Clear();
    window_pos_(0, 8);
    call_list_(list);
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
    EXPECT_EQ(Differences(reference, Snapshot()), 0)
        << "the replay was interpreted under the unpack state at glCallList time";

    // The application's own pixel-store state is where it left it.
    GLint alignment = -1, lsb_first = -1;
    get_integer_(GL_UNPACK_ALIGNMENT_, &alignment);
    get_integer_(GL_UNPACK_LSB_FIRST_, &lsb_first);
    EXPECT_EQ(alignment, 1);
    EXPECT_EQ(lsb_first, 1);
    delete_lists_(list, 1);
}

TEST_F(BitmapListAdvanceTest, RecordingReadsThroughThePixelUnpackBuffer) {
    // With a buffer bound, `bitmap` is a byte offset. The recorder used to
    // memcpy from it as an address (plans/17 P2's display-list half); a
    // non-zero offset is the shape that crashed, offset 0 having been
    // short-circuited by a null check.
    constexpr GLsizei kOffset = 16;
    const auto glyph = SolidGlyph();
    std::vector<GLubyte> contents(kOffset + glyph.size(), 0x00);
    for (size_t i = 0; i < glyph.size(); ++i) contents[kOffset + i] = glyph[i];

    Clear();
    window_pos_(0, 8);
    GLuint buffer = 0;
    gen_buffers_(1, &buffer);
    bind_buffer_(GL_PIXEL_UNPACK_BUFFER_, buffer);
    buffer_data_(GL_PIXEL_UNPACK_BUFFER_, static_cast<GLsizeiptr>(contents.size()),
                 contents.data(), GL_STATIC_DRAW_);
    Drain();

    const GLuint list = gen_lists_(1);
    new_list_(list, GL_COMPILE_);
    bitmap_(kGlyph, kGlyph, 0.0f, 0.0f, 0.0f, 0.0f,
            reinterpret_cast<const GLubyte*>(static_cast<uintptr_t>(kOffset)));
    end_list_();
    EXPECT_EQ(get_error_(), GL_NO_ERROR_) << "compiling a PBO-sourced glBitmap";

    bind_buffer_(GL_PIXEL_UNPACK_BUFFER_, 0);
    delete_buffers_(1, &buffer);
    Drain();

    Clear();
    window_pos_(0, 8);
    call_list_(list);
    EXPECT_EQ(get_error_(), GL_NO_ERROR_) << "replaying a PBO-sourced glBitmap";
    EXPECT_EQ(LitCount(), kGlyph * kGlyph)
        << "the payload was not read out of the unpack buffer at compile time";
    delete_lists_(list, 1);
}

} // namespace
