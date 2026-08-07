// SimpleFPEWrapper - tests/gtest_readpixels_formats.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// plans/17 P1/P4: the read direction of the pixel pipeline.
//
// P1 - a GLES backend accepts exactly two glReadPixels format/type pairs
// (ES 3.0 4.3.1: GL_RGBA+GL_UNSIGNED_BYTE plus the implementation-chosen
// pair), and rejects everything else with GL_INVALID_OPERATION, which per
// spec leaves the caller's buffer entirely unwritten. The wrapper used to
// hand all twelve packed types and the GL_RGB/GL_LUMINANCE/GL_FLOAT scalar
// pairs straight to the backend, so every one of them returned whatever the
// caller's buffer already held - including Minecraft's ScreenShotHelper,
// which asks for GL_BGRA+GL_UNSIGNED_INT_8_8_8_8_REV verbatim.
//
// P4 - the one converted pair that did work (GL_BGRA+GL_UNSIGNED_BYTE) asked
// the backend for RGBA using the caller's full GL_PACK_* layout and then
// byte-swapped a tight width*height*4 run from the caller's base pointer, so
// it swapped the wrong addresses whenever the layout was not tight.
//
// Expected bytes are computed from GL 2.1 3.6.4's packing formulas
// (/docsgl/gl2/glReadPixels.xhtml): each component is
// round(c * (2^n - 1)) placed in its field, the fields packed most- to
// least-significant for a plain type and least- to most- for a _REV one,
// and the resulting word stored in host byte order.

#include "sfpew_gtest.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

using sfpew_test::ContextTest;
using sfpew_test::GLbitfield;
using sfpew_test::GLenum;
using sfpew_test::GLfloat;
using sfpew_test::GLint;
using sfpew_test::GLsizei;
using sfpew_test::GLubyte;

constexpr GLenum GL_NO_ERROR_ = 0;
constexpr GLbitfield GL_COLOR_BUFFER_BIT_ = 0x00004000;
constexpr GLenum GL_DITHER_ = 0x0BD0;

constexpr GLenum GL_RED_ = 0x1903;
constexpr GLenum GL_ALPHA_ = 0x1906;
constexpr GLenum GL_RGB_ = 0x1907;
constexpr GLenum GL_RGBA_ = 0x1908;
constexpr GLenum GL_LUMINANCE_ = 0x1909;
constexpr GLenum GL_LUMINANCE_ALPHA_ = 0x190A;
constexpr GLenum GL_BGR_ = 0x80E0;
constexpr GLenum GL_BGRA_ = 0x80E1;

constexpr GLenum GL_UNSIGNED_BYTE_ = 0x1401;
constexpr GLenum GL_UNSIGNED_SHORT_ = 0x1403;
constexpr GLenum GL_FLOAT_ = 0x1406;
constexpr GLenum GL_UNSIGNED_BYTE_3_3_2_ = 0x8032;
constexpr GLenum GL_UNSIGNED_BYTE_2_3_3_REV_ = 0x8362;
constexpr GLenum GL_UNSIGNED_SHORT_5_6_5_ = 0x8363;
constexpr GLenum GL_UNSIGNED_SHORT_5_6_5_REV_ = 0x8364;
constexpr GLenum GL_UNSIGNED_SHORT_4_4_4_4_ = 0x8033;
constexpr GLenum GL_UNSIGNED_SHORT_4_4_4_4_REV_ = 0x8365;
constexpr GLenum GL_UNSIGNED_SHORT_5_5_5_1_ = 0x8034;
constexpr GLenum GL_UNSIGNED_SHORT_1_5_5_5_REV_ = 0x8366;
constexpr GLenum GL_UNSIGNED_INT_8_8_8_8_ = 0x8035;
constexpr GLenum GL_UNSIGNED_INT_8_8_8_8_REV_ = 0x8367;
constexpr GLenum GL_UNSIGNED_INT_10_10_10_2_ = 0x8036;
constexpr GLenum GL_UNSIGNED_INT_2_10_10_10_REV_ = 0x8368;

constexpr GLenum GL_PACK_ROW_LENGTH_ = 0x0D02;
constexpr GLenum GL_PACK_SKIP_ROWS_ = 0x0D03;
constexpr GLenum GL_PACK_SKIP_PIXELS_ = 0x0D04;

constexpr GLenum GL_TEXTURE_2D_ = 0x0DE1;
constexpr GLenum GL_TEXTURE_MIN_FILTER_ = 0x2801;
constexpr GLenum GL_NEAREST_ = 0x2600;

// The source pixel every case below reads back: deliberately three distinct
// low bytes so a wrong component order or a dropped row is a wrong VALUE,
// not just a wrong-looking one. Alpha is opaque so the 1- and 2-bit alpha
// fields of the packed types have an unambiguous answer.
constexpr GLubyte kSrcR = 0x10, kSrcG = 0x20, kSrcB = 0x30, kSrcA = 0xFF;
// Prefills the destination: any byte still holding it after a read is a byte
// glReadPixels never wrote.
constexpr GLubyte kSentinel = 0xCD;

// The GL_PACK_* cases need a sentinel that VARIES within each four-byte
// group instead: the defect they are about rewrites bytes outside the
// rectangle by swapping byte 0 with byte 2, which a uniform filler would
// absorb without a trace.
GLubyte PatternAt(size_t index) { return static_cast<GLubyte>(0xC0u + (index & 3u)); }

std::vector<GLubyte> PatternBuffer(size_t bytes) {
    std::vector<GLubyte> buffer(bytes);
    for (size_t i = 0; i < bytes; ++i) buffer[i] = PatternAt(i);
    return buffer;
}

class ReadPixelsFormatsTest : public ContextTest {
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
        disable_ = Get<void (*)(GLenum)>("glDisable");
        read_pixels_ =
            Get<void (*)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*)>("glReadPixels");
        pixel_store_ = Get<void (*)(GLenum, GLint)>("glPixelStorei");

        viewport_(0, 0, size(), size());
        // A dithered clear could perturb a byte-exact expectation.
        disable_(GL_DITHER_);
        ClearTo(kSrcR, kSrcG, kSrcB, kSrcA);
    }

    void ClearTo(GLubyte r, GLubyte g, GLubyte b, GLubyte a) {
        clear_color_(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f);
        clear_(GL_COLOR_BUFFER_BIT_);
        while (get_error_() != GL_NO_ERROR_) {
        }
    }

    // Every case depends on the framebuffer really holding the byte values
    // the expectations were computed from, through the one pair every
    // backend floor is required to accept.
    void AssertSourcePixel(GLubyte r, GLubyte g, GLubyte b, GLubyte a) {
        std::array<GLubyte, 4> pixel = {kSentinel, kSentinel, kSentinel, kSentinel};
        read_pixels_(0, 0, 1, 1, GL_RGBA_, GL_UNSIGNED_BYTE_, pixel.data());
        ASSERT_EQ(get_error_(), GL_NO_ERROR_);
        ASSERT_EQ(pixel[0], r);
        ASSERT_EQ(pixel[1], g);
        ASSERT_EQ(pixel[2], b);
        ASSERT_EQ(pixel[3], a);
    }

    // Reads one pixel with `format`/`type` into a sentinel-filled buffer and
    // compares the leading bytes; everything past `expected` must still hold
    // the sentinel, which is what proves the encoder did not overrun.
    void ExpectPixelBytes(const char* what, GLenum format, GLenum type,
                          const std::vector<GLubyte>& expected) {
        SCOPED_TRACE(what);
        std::array<GLubyte, 32> buffer;
        buffer.fill(kSentinel);
        read_pixels_(0, 0, 1, 1, format, type, buffer.data());
        EXPECT_EQ(get_error_(), GL_NO_ERROR_);
        for (size_t i = 0; i < expected.size(); ++i) {
            EXPECT_EQ(buffer[i], expected[i])
                << "byte " << i << ": got 0x" << std::hex << (unsigned)buffer[i] << ", want 0x"
                << (unsigned)expected[i] << std::dec
                << (buffer[i] == kSentinel ? " (buffer never written)" : "");
        }
        for (size_t i = expected.size(); i < buffer.size(); ++i)
            EXPECT_EQ(buffer[i], kSentinel) << "byte " << i << " past the pixel was overwritten";
    }

    GLenum (*get_error_)() = nullptr;
    void (*viewport_)(GLint, GLint, GLsizei, GLsizei) = nullptr;
    void (*clear_color_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*clear_)(GLbitfield) = nullptr;
    void (*disable_)(GLenum) = nullptr;
    void (*read_pixels_)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*) = nullptr;
    void (*pixel_store_)(GLenum, GLint) = nullptr;
};

// P1, the case that motivated it: Minecraft 1.7-1.12's ScreenShotHelper asks
// for the whole framebuffer this way, and on a GLES backend used to get its
// own uninitialized buffer back.
TEST_F(ReadPixelsFormatsTest, MinecraftScreenshotBgraUnsignedInt8888Rev) {
    AssertSourcePixel(kSrcR, kSrcG, kSrcB, kSrcA);
    const GLsizei w = size(), h = size();
    std::vector<GLubyte> buffer(static_cast<size_t>(w) * h * 4u, kSentinel);
    read_pixels_(0, 0, w, h, GL_BGRA_, GL_UNSIGNED_INT_8_8_8_8_REV_, buffer.data());
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
    // _REV puts component 0 in the LOW field, and GL_BGRA's component 0 is
    // blue: word 0xFF102030, stored little-endian, i.e. B,G,R,A. This case is
    // specifically about component order, so it asserts that exact byte
    // sequence rather than a channel relationship.
    size_t wrong = 0;
    size_t first_wrong = 0;
    for (size_t i = 0; i < buffer.size(); i += 4) {
        if (buffer[i + 0] != kSrcB || buffer[i + 1] != kSrcG || buffer[i + 2] != kSrcR ||
            buffer[i + 3] != kSrcA) {
            if (wrong == 0) first_wrong = i;
            ++wrong;
        }
    }
    EXPECT_EQ(wrong, 0u) << wrong << " of " << (buffer.size() / 4) << " pixels wrong; first at "
                         << (first_wrong / 4) << " = " << std::hex << (unsigned)buffer[first_wrong]
                         << " " << (unsigned)buffer[first_wrong + 1] << " "
                         << (unsigned)buffer[first_wrong + 2] << " "
                         << (unsigned)buffer[first_wrong + 3];
}

// P1: all twelve packed types, each with a format the pairing rules allow.
TEST_F(ReadPixelsFormatsTest, PackedTypesEncodeExactBytes) {
    AssertSourcePixel(kSrcR, kSrcG, kSrcB, kSrcA);

    // R=16/255 G=32/255 B=48/255 A=1: r*7=0.44->0, g*7=0.88->1, b*3=0.56->1.
    ExpectPixelBytes("GL_RGB + GL_UNSIGNED_BYTE_3_3_2", GL_RGB_, GL_UNSIGNED_BYTE_3_3_2_,
                     {0x05});
    ExpectPixelBytes("GL_RGB + GL_UNSIGNED_BYTE_2_3_3_REV", GL_RGB_, GL_UNSIGNED_BYTE_2_3_3_REV_,
                     {0x48});
    // r*31=1.95->2, g*63=7.91->8, b*31=5.84->6 => 0x1106.
    ExpectPixelBytes("GL_RGB + GL_UNSIGNED_SHORT_5_6_5", GL_RGB_, GL_UNSIGNED_SHORT_5_6_5_,
                     {0x06, 0x11});
    ExpectPixelBytes("GL_RGB + GL_UNSIGNED_SHORT_5_6_5_REV", GL_RGB_, GL_UNSIGNED_SHORT_5_6_5_REV_,
                     {0x02, 0x31});
    // r*15=0.94->1, g*15=1.88->2, b*15=2.82->3, a*15=15 => 0x123F.
    ExpectPixelBytes("GL_RGBA + GL_UNSIGNED_SHORT_4_4_4_4", GL_RGBA_, GL_UNSIGNED_SHORT_4_4_4_4_,
                     {0x3F, 0x12});
    ExpectPixelBytes("GL_RGBA + GL_UNSIGNED_SHORT_4_4_4_4_REV", GL_RGBA_,
                     GL_UNSIGNED_SHORT_4_4_4_4_REV_, {0x21, 0xF3});
    // r*31=2, g*31=3.89->4, b*31=6, a*1=1 => 0x110D.
    ExpectPixelBytes("GL_RGBA + GL_UNSIGNED_SHORT_5_5_5_1", GL_RGBA_, GL_UNSIGNED_SHORT_5_5_5_1_,
                     {0x0D, 0x11});
    ExpectPixelBytes("GL_RGBA + GL_UNSIGNED_SHORT_1_5_5_5_REV", GL_RGBA_,
                     GL_UNSIGNED_SHORT_1_5_5_5_REV_, {0x82, 0x98});
    // The 8-bit fields are the source bytes themselves: 0x102030FF / 0xFF302010.
    ExpectPixelBytes("GL_RGBA + GL_UNSIGNED_INT_8_8_8_8", GL_RGBA_, GL_UNSIGNED_INT_8_8_8_8_,
                     {0xFF, 0x30, 0x20, 0x10});
    ExpectPixelBytes("GL_RGBA + GL_UNSIGNED_INT_8_8_8_8_REV", GL_RGBA_,
                     GL_UNSIGNED_INT_8_8_8_8_REV_, {0x10, 0x20, 0x30, 0xFF});
    // r*1023=64.2->64, g*1023=128.4->128, b*1023=192.6->193, a*3=3.
    ExpectPixelBytes("GL_RGBA + GL_UNSIGNED_INT_10_10_10_2", GL_RGBA_,
                     GL_UNSIGNED_INT_10_10_10_2_, {0x07, 0x03, 0x08, 0x10});
    ExpectPixelBytes("GL_RGBA + GL_UNSIGNED_INT_2_10_10_10_REV", GL_RGBA_,
                     GL_UNSIGNED_INT_2_10_10_10_REV_, {0x40, 0x00, 0x12, 0xCC});

    // The BGRA spellings of the same words - component order swapped, field
    // layout identical.
    ExpectPixelBytes("GL_BGRA + GL_UNSIGNED_INT_8_8_8_8_REV", GL_BGRA_,
                     GL_UNSIGNED_INT_8_8_8_8_REV_, {0x30, 0x20, 0x10, 0xFF});
    ExpectPixelBytes("GL_BGRA + GL_UNSIGNED_SHORT_4_4_4_4", GL_BGRA_, GL_UNSIGNED_SHORT_4_4_4_4_,
                     {0x1F, 0x32});
}

// P1: the scalar format/type pairs GL 2.1 has and a GLES backend refuses.
TEST_F(ReadPixelsFormatsTest, ScalarPairsEncodeExactBytes) {
    AssertSourcePixel(kSrcR, kSrcG, kSrcB, kSrcA);

    ExpectPixelBytes("GL_RGB + GL_UNSIGNED_BYTE", GL_RGB_, GL_UNSIGNED_BYTE_,
                     {kSrcR, kSrcG, kSrcB});
    ExpectPixelBytes("GL_BGR + GL_UNSIGNED_BYTE", GL_BGR_, GL_UNSIGNED_BYTE_,
                     {kSrcB, kSrcG, kSrcR});
    ExpectPixelBytes("GL_BGRA + GL_UNSIGNED_BYTE", GL_BGRA_, GL_UNSIGNED_BYTE_,
                     {kSrcB, kSrcG, kSrcR, kSrcA});
    ExpectPixelBytes("GL_RED + GL_UNSIGNED_BYTE", GL_RED_, GL_UNSIGNED_BYTE_, {kSrcR});
    ExpectPixelBytes("GL_ALPHA + GL_UNSIGNED_BYTE", GL_ALPHA_, GL_UNSIGNED_BYTE_, {kSrcA});
    // 16-bit normalization: c * 65535 / 255 replicates the byte.
    ExpectPixelBytes("GL_RGBA + GL_UNSIGNED_SHORT", GL_RGBA_, GL_UNSIGNED_SHORT_,
                     {0x10, 0x10, 0x20, 0x20, 0x30, 0x30, 0xFF, 0xFF});
}

TEST_F(ReadPixelsFormatsTest, FloatPairReturnsNormalizedComponents) {
    AssertSourcePixel(kSrcR, kSrcG, kSrcB, kSrcA);
    std::array<GLfloat, 4> pixel = {-1.0f, -1.0f, -1.0f, -1.0f};
    read_pixels_(0, 0, 1, 1, GL_RGBA_, GL_FLOAT_, pixel.data());
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
    EXPECT_FLOAT_EQ(pixel[0], kSrcR / 255.0f);
    EXPECT_FLOAT_EQ(pixel[1], kSrcG / 255.0f);
    EXPECT_FLOAT_EQ(pixel[2], kSrcB / 255.0f);
    EXPECT_FLOAT_EQ(pixel[3], kSrcA / 255.0f);
}

// GL 2.1 defines a luminance read as the SUM of R, G and B (clamped), so the
// source here is red-only: that makes the sum and a plain red passthrough the
// same number, and keeps this case about "was the buffer written at all"
// rather than about which of the two imaging.cpp's shared encoder implements.
TEST_F(ReadPixelsFormatsTest, LuminancePairsEncodeExactBytes) {
    ClearTo(kSrcR, 0, 0, kSrcA);
    AssertSourcePixel(kSrcR, 0, 0, kSrcA);
    ExpectPixelBytes("GL_LUMINANCE + GL_UNSIGNED_BYTE", GL_LUMINANCE_, GL_UNSIGNED_BYTE_,
                     {kSrcR});
    ExpectPixelBytes("GL_LUMINANCE_ALPHA + GL_UNSIGNED_BYTE", GL_LUMINANCE_ALPHA_,
                     GL_UNSIGNED_BYTE_, {kSrcR, kSrcA});
}

// The pair every backend floor accepts must keep taking the raw backend path
// - imaging.h:40-46's performance bargain - and still be byte-exact.
TEST_F(ReadPixelsFormatsTest, RgbaUnsignedByteStaysExact) {
    AssertSourcePixel(kSrcR, kSrcG, kSrcB, kSrcA);
    ExpectPixelBytes("GL_RGBA + GL_UNSIGNED_BYTE", GL_RGBA_, GL_UNSIGNED_BYTE_,
                     {kSrcR, kSrcG, kSrcB, kSrcA});
}

// glGetTexImage reads back through this same wrapper glReadPixels
// (texture_image.cpp), so it inherited the hole and must be repaired by the
// same fix.
TEST_F(ReadPixelsFormatsTest, GetTexImageInheritsTheConversion) {
    auto gen_textures = Get<void (*)(GLsizei, unsigned int*)>("glGenTextures");
    auto bind_texture = Get<void (*)(GLenum, unsigned int)>("glBindTexture");
    auto tex_parameteri = Get<void (*)(GLenum, GLenum, GLint)>("glTexParameteri");
    auto tex_image_2d = Get<void (*)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum,
                                     const void*)>("glTexImage2D");
    auto get_tex_image = Get<void (*)(GLenum, GLint, GLenum, GLenum, void*)>("glGetTexImage");
    auto delete_textures = Get<void (*)(GLsizei, const unsigned int*)>("glDeleteTextures");

    unsigned int texture = 0;
    gen_textures(1, &texture);
    bind_texture(GL_TEXTURE_2D_, texture);
    tex_parameteri(GL_TEXTURE_2D_, GL_TEXTURE_MIN_FILTER_, GL_NEAREST_);
    const std::array<GLubyte, 4> texel = {kSrcR, kSrcG, kSrcB, kSrcA};
    tex_image_2d(GL_TEXTURE_2D_, 0, GL_RGBA_, 1, 1, 0, GL_RGBA_, GL_UNSIGNED_BYTE_, texel.data());
    while (get_error_() != GL_NO_ERROR_) {
    }

    std::array<GLubyte, 8> buffer;
    buffer.fill(kSentinel);
    get_tex_image(GL_TEXTURE_2D_, 0, GL_BGRA_, GL_UNSIGNED_INT_8_8_8_8_REV_, buffer.data());
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
    EXPECT_EQ(buffer[0], kSrcB);
    EXPECT_EQ(buffer[1], kSrcG);
    EXPECT_EQ(buffer[2], kSrcR);
    EXPECT_EQ(buffer[3], kSrcA);
    delete_textures(1, &texture);
}

// --- P4: the converted read must walk the caller's pack layout ------------

// GL_PACK_ROW_LENGTH puts the rows at a stride wider than the rectangle. The
// old tight swap loop only ever reached the first ROW, and spilled past the
// rectangle into the padding while doing it.
TEST_F(ReadPixelsFormatsTest, PackRowLengthAppliesToEveryRow) {
    AssertSourcePixel(kSrcR, kSrcG, kSrcB, kSrcA);
    constexpr GLint kRowLength = 32;
    constexpr GLsizei kRect = 4;
    std::vector<GLubyte> buffer = PatternBuffer(static_cast<size_t>(kRowLength) * kRect * 4u);
    pixel_store_(GL_PACK_ROW_LENGTH_, kRowLength);
    read_pixels_(0, 0, kRect, kRect, GL_BGRA_, GL_UNSIGNED_BYTE_, buffer.data());
    pixel_store_(GL_PACK_ROW_LENGTH_, 0);
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);

    for (int row = 0; row < kRect; ++row) {
        SCOPED_TRACE("row " + std::to_string(row));
        const size_t base = static_cast<size_t>(row) * kRowLength * 4u;
        for (int px = 0; px < kRect; ++px) {
            const size_t at = base + static_cast<size_t>(px) * 4u;
            EXPECT_EQ(buffer[at + 0], kSrcB) << "pixel " << px << " not in BGRA order";
            EXPECT_EQ(buffer[at + 1], kSrcG) << "pixel " << px;
            EXPECT_EQ(buffer[at + 2], kSrcR) << "pixel " << px << " not in BGRA order";
            EXPECT_EQ(buffer[at + 3], kSrcA) << "pixel " << px;
        }
        for (size_t at = base + kRect * 4u; at < base + kRowLength * 4u; ++at)
            EXPECT_EQ(buffer[at], PatternAt(at)) << "padding byte " << at << " was written";
    }
}

// GL_PACK_SKIP_ROWS offsets where the rectangle starts. The old loop swapped
// from the caller's base pointer instead, rewriting rows the caller never
// asked to be touched.
TEST_F(ReadPixelsFormatsTest, PackSkipRowsLeavesEarlierRowsUntouched) {
    AssertSourcePixel(kSrcR, kSrcG, kSrcB, kSrcA);
    constexpr GLint kSkipRows = 3;
    constexpr GLsizei kRect = 4;
    constexpr size_t kRowBytes = static_cast<size_t>(kRect) * 4u;
    std::vector<GLubyte> buffer = PatternBuffer((kSkipRows + kRect) * kRowBytes);
    pixel_store_(GL_PACK_SKIP_ROWS_, kSkipRows);
    read_pixels_(0, 0, kRect, kRect, GL_BGRA_, GL_UNSIGNED_BYTE_, buffer.data());
    pixel_store_(GL_PACK_SKIP_ROWS_, 0);
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);

    for (size_t at = 0; at < kSkipRows * kRowBytes; ++at)
        EXPECT_EQ(buffer[at], PatternAt(at))
            << "byte " << at << " lies before the requested rectangle and was rewritten";
    for (size_t at = kSkipRows * kRowBytes; at < buffer.size(); at += 4) {
        EXPECT_EQ(buffer[at + 0], kSrcB) << "pixel at byte " << at << " not in BGRA order";
        EXPECT_EQ(buffer[at + 1], kSrcG) << "pixel at byte " << at;
        EXPECT_EQ(buffer[at + 2], kSrcR) << "pixel at byte " << at << " not in BGRA order";
        EXPECT_EQ(buffer[at + 3], kSrcA) << "pixel at byte " << at;
    }
}

// GL_PACK_SKIP_PIXELS offsets the first column the same way.
TEST_F(ReadPixelsFormatsTest, PackSkipPixelsLeavesEarlierPixelsUntouched) {
    AssertSourcePixel(kSrcR, kSrcG, kSrcB, kSrcA);
    constexpr GLint kSkipPixels = 5;
    constexpr GLsizei kRect = 4;
    constexpr GLint kRowLength = kSkipPixels + kRect;
    std::vector<GLubyte> buffer = PatternBuffer(static_cast<size_t>(kRowLength) * kRect * 4u);
    pixel_store_(GL_PACK_ROW_LENGTH_, kRowLength);
    pixel_store_(GL_PACK_SKIP_PIXELS_, kSkipPixels);
    read_pixels_(0, 0, kRect, kRect, GL_BGRA_, GL_UNSIGNED_BYTE_, buffer.data());
    pixel_store_(GL_PACK_SKIP_PIXELS_, 0);
    pixel_store_(GL_PACK_ROW_LENGTH_, 0);
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);

    for (int row = 0; row < kRect; ++row) {
        SCOPED_TRACE("row " + std::to_string(row));
        const size_t base = static_cast<size_t>(row) * kRowLength * 4u;
        for (size_t at = base; at < base + kSkipPixels * 4u; ++at)
            EXPECT_EQ(buffer[at], PatternAt(at)) << "skipped byte " << at << " was written";
        for (int px = 0; px < kRect; ++px) {
            const size_t at = base + static_cast<size_t>(kSkipPixels + px) * 4u;
            EXPECT_EQ(buffer[at + 0], kSrcB) << "pixel " << px << " not in BGRA order";
            EXPECT_EQ(buffer[at + 2], kSrcR) << "pixel " << px << " not in BGRA order";
        }
    }
}

} // namespace
