// SimpleFPEWrapper - tests/gtest_drawpixels_formats.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

#include "sfpew_gtest.h"

#include <array>
#include <cstdint>

namespace {

using sfpew_test::ContextTest;
using sfpew_test::GLbitfield;
using sfpew_test::GLboolean;
using sfpew_test::GLenum;
using sfpew_test::GLfloat;
using sfpew_test::GLint;
using sfpew_test::GLsizei;
using sfpew_test::GLsizeiptr;
using sfpew_test::GLubyte;
using sfpew_test::GLuint;
using sfpew_test::GLushort;
using sfpew_test::LibraryTest;
using sfpew_test::PixelProbe;

constexpr GLenum GL_NO_ERROR_ = 0;
constexpr GLenum GL_INVALID_ENUM_ = 0x0500;
constexpr GLenum GL_INVALID_VALUE_ = 0x0501;
constexpr GLenum GL_INVALID_OPERATION_ = 0x0502;
constexpr GLbitfield GL_DEPTH_BUFFER_BIT_ = 0x00000100;
constexpr GLbitfield GL_COLOR_BUFFER_BIT_ = 0x00004000;
constexpr GLenum GL_DEPTH_TEST_ = 0x0B71;
constexpr GLenum GL_ALWAYS_ = 0x0207;
constexpr GLenum GL_RED_ = 0x1903;
constexpr GLenum GL_GREEN_ = 0x1904;
constexpr GLenum GL_BLUE_ = 0x1905;
constexpr GLenum GL_RGB_ = 0x1907;
constexpr GLenum GL_RGBA_ = 0x1908;
constexpr GLenum GL_BGR_ = 0x80E0;
constexpr GLenum GL_LUMINANCE_ALPHA_ = 0x190A;
constexpr GLenum GL_STENCIL_INDEX_ = 0x1901;
constexpr GLenum GL_DEPTH_COMPONENT_ = 0x1902;
constexpr GLenum GL_UNSIGNED_BYTE_ = 0x1401;
constexpr GLenum GL_SHORT_ = 0x1402;
constexpr GLenum GL_UNSIGNED_SHORT_ = 0x1403;
constexpr GLenum GL_FLOAT_ = 0x1406;
constexpr GLenum GL_HALF_FLOAT_ = 0x140B;
constexpr GLenum GL_UNSIGNED_SHORT_5_6_5_ = 0x8363;
constexpr GLenum GL_UNSIGNED_SHORT_4_4_4_4_ = 0x8033;
constexpr GLenum GL_UNSIGNED_INT_2_10_10_10_REV_ = 0x8368;
constexpr GLenum GL_PIXEL_UNPACK_BUFFER_ = 0x88EC;
constexpr GLenum GL_PIXEL_UNPACK_BUFFER_BINDING_ = 0x88EF;
constexpr GLenum GL_STATIC_DRAW_ = 0x88E4;
constexpr GLenum GL_UNPACK_ALIGNMENT_ = 0x0CF5;
constexpr GLenum GL_UNPACK_ROW_LENGTH_ = 0x0CF2;
constexpr GLenum GL_UNPACK_SKIP_ROWS_ = 0x0CF3;
constexpr GLenum GL_UNPACK_SKIP_PIXELS_ = 0x0CF4;
constexpr GLenum GL_RED_SCALE_ = 0x0D14;
constexpr GLenum GL_RED_BIAS_ = 0x0D15;
constexpr GLenum GL_COLOR_WRITEMASK_ = 0x0C23;
constexpr GLenum GL_QUADS_ = 0x0007;
constexpr GLenum GL_LESS_ = 0x0201;

class DrawPixelsLibraryTest : public LibraryTest {};

TEST_F(DrawPixelsLibraryTest, ValidationAndExplicitStencilRefusalNeedNoContext) {
    auto draw_pixels = Get<void (*)(GLsizei, GLsizei, GLenum, GLenum, const void*)>(
        "glDrawPixels");
    auto get_error = Get<GLenum (*)()>("glGetError");
    const std::array<GLubyte, 4> pixel = {0, 0, 0, 0};

    draw_pixels(-1, 1, GL_RGBA_, GL_UNSIGNED_BYTE_, pixel.data());
    EXPECT_EQ(get_error(), GL_INVALID_VALUE_);
    draw_pixels(1, 1, 0xDEAD, GL_UNSIGNED_BYTE_, pixel.data());
    EXPECT_EQ(get_error(), GL_INVALID_ENUM_);
    draw_pixels(1, 1, GL_RGBA_, 0xDEAD, pixel.data());
    EXPECT_EQ(get_error(), GL_INVALID_ENUM_);
    draw_pixels(1, 1, GL_RGB_, GL_UNSIGNED_SHORT_4_4_4_4_, pixel.data());
    EXPECT_EQ(get_error(), GL_INVALID_OPERATION_);
    draw_pixels(1, 1, GL_STENCIL_INDEX_, GL_UNSIGNED_BYTE_, pixel.data());
    EXPECT_EQ(get_error(), GL_INVALID_OPERATION_);
}

class DrawPixelsTest : public ContextTest {
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
        window_pos_ = Get<void (*)(GLint, GLint)>("glWindowPos2i");
        pixel_zoom_ = Get<void (*)(GLfloat, GLfloat)>("glPixelZoom");
        pixel_transfer_ = Get<void (*)(GLenum, GLfloat)>("glPixelTransferf");
        draw_pixels_ = Get<void (*)(GLsizei, GLsizei, GLenum, GLenum, const void*)>(
            "glDrawPixels");
        read_pixels_ = Get<void (*)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*)>(
            "glReadPixels");
        pixel_store_ = Get<void (*)(GLenum, GLint)>("glPixelStorei");
        gen_buffers_ = Get<void (*)(GLsizei, GLuint*)>("glGenBuffers");
        bind_buffer_ = Get<void (*)(GLenum, GLuint)>("glBindBuffer");
        buffer_data_ = Get<void (*)(GLenum, GLsizeiptr, const void*, GLenum)>("glBufferData");
        delete_buffers_ = Get<void (*)(GLsizei, const GLuint*)>("glDeleteBuffers");
        get_integer_ = Get<void (*)(GLenum, GLint*)>("glGetIntegerv");
        enable_ = Get<void (*)(GLenum)>("glEnable");
        disable_ = Get<void (*)(GLenum)>("glDisable");
        depth_func_ = Get<void (*)(GLenum)>("glDepthFunc");
        depth_mask_ = Get<void (*)(GLboolean)>("glDepthMask");
        clear_depth_ = Get<void (*)(GLfloat)>("glClearDepthf");
        get_boolean_ = Get<void (*)(GLenum, GLboolean*)>("glGetBooleanv");
        begin_ = Get<void (*)(GLenum)>("glBegin");
        end_ = Get<void (*)()>("glEnd");
        color4f_ = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glColor4f");
        vertex3f_ = Get<void (*)(GLfloat, GLfloat, GLfloat)>("glVertex3f");

        viewport_(0, 0, size(), size());
        pixel_zoom_(8.0f, 8.0f);
        clear_color_(0.0f, 0.0f, 0.0f, 1.0f);
        clear_(GL_COLOR_BUFFER_BIT_ | GL_DEPTH_BUFFER_BIT_);
    }

    PixelProbe::Rgba At(int x, int y) const { return PixelProbe(read_pixels_).At(x, y); }

    void ExpectColor(int x, int y, int red, int green, int blue, int tolerance = 8) const {
        const auto pixel = At(x, y);
        EXPECT_NEAR(pixel.r, red, tolerance);
        EXPECT_NEAR(pixel.g, green, tolerance);
        EXPECT_NEAR(pixel.b, blue, tolerance);
    }

    GLenum (*get_error_)() = nullptr;
    void (*viewport_)(GLint, GLint, GLsizei, GLsizei) = nullptr;
    void (*clear_color_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*clear_)(GLbitfield) = nullptr;
    void (*window_pos_)(GLint, GLint) = nullptr;
    void (*pixel_zoom_)(GLfloat, GLfloat) = nullptr;
    void (*pixel_transfer_)(GLenum, GLfloat) = nullptr;
    void (*draw_pixels_)(GLsizei, GLsizei, GLenum, GLenum, const void*) = nullptr;
    PixelProbe::ReadPixelsFn read_pixels_ = nullptr;
    void (*pixel_store_)(GLenum, GLint) = nullptr;
    void (*gen_buffers_)(GLsizei, GLuint*) = nullptr;
    void (*bind_buffer_)(GLenum, GLuint) = nullptr;
    void (*buffer_data_)(GLenum, GLsizeiptr, const void*, GLenum) = nullptr;
    void (*delete_buffers_)(GLsizei, const GLuint*) = nullptr;
    void (*get_integer_)(GLenum, GLint*) = nullptr;
    void (*enable_)(GLenum) = nullptr;
    void (*disable_)(GLenum) = nullptr;
    void (*depth_func_)(GLenum) = nullptr;
    void (*depth_mask_)(GLboolean) = nullptr;
    void (*clear_depth_)(GLfloat) = nullptr;
    void (*get_boolean_)(GLenum, GLboolean*) = nullptr;
    void (*begin_)(GLenum) = nullptr;
    void (*end_)() = nullptr;
    void (*color4f_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*vertex3f_)(GLfloat, GLfloat, GLfloat) = nullptr;
};

TEST_F(DrawPixelsTest, ScalarAndPackedFormatsExpandAndTransferCorrectly) {
    const GLubyte full = 255;
    const GLushort full_short = 65535;
    const std::array<GLfloat, 2> luminance_alpha = {0.5f, 1.0f};
    const std::array<int16_t, 3> bgr_short = {0, 0, 32767};
    const std::array<uint16_t, 4> rgba_half = {0, 0x3C00, 0, 0x3C00};
    const GLushort packed_red = 0xF800;
    const GLushort packed_green = 0x0F0F;
    const uint32_t packed_blue_rev = 0xC0000000u | (1023u << 20u);

    window_pos_(4, 4);
    draw_pixels_(1, 1, GL_GREEN_, GL_UNSIGNED_BYTE_, &full);
    window_pos_(16, 4);
    draw_pixels_(1, 1, GL_BLUE_, GL_UNSIGNED_SHORT_, &full_short);
    window_pos_(28, 4);
    draw_pixels_(1, 1, GL_LUMINANCE_ALPHA_, GL_FLOAT_, luminance_alpha.data());
    window_pos_(40, 4);
    draw_pixels_(1, 1, GL_BGR_, GL_SHORT_, bgr_short.data());
    window_pos_(52, 4);
    draw_pixels_(1, 1, GL_RGBA_, GL_HALF_FLOAT_, rgba_half.data());
    window_pos_(4, 20);
    draw_pixels_(1, 1, GL_RGB_, GL_UNSIGNED_SHORT_5_6_5_, &packed_red);
    window_pos_(16, 20);
    draw_pixels_(1, 1, GL_RGBA_, GL_UNSIGNED_SHORT_4_4_4_4_, &packed_green);
    window_pos_(28, 20);
    draw_pixels_(1, 1, GL_RGBA_, GL_UNSIGNED_INT_2_10_10_10_REV_, &packed_blue_rev);

    pixel_transfer_(GL_RED_SCALE_, 0.5f);
    pixel_transfer_(GL_RED_BIAS_, 0.25f);
    window_pos_(40, 20);
    draw_pixels_(1, 1, GL_RED_, GL_UNSIGNED_BYTE_, &full);
    pixel_transfer_(GL_RED_SCALE_, 1.0f);
    pixel_transfer_(GL_RED_BIAS_, 0.0f);

    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
    ExpectColor(8, 8, 0, 255, 0);
    ExpectColor(20, 8, 0, 0, 255);
    ExpectColor(32, 8, 128, 128, 128);
    ExpectColor(44, 8, 255, 0, 0);
    ExpectColor(56, 8, 0, 255, 0);
    ExpectColor(8, 24, 255, 0, 0);
    ExpectColor(20, 24, 0, 255, 0);
    ExpectColor(32, 24, 0, 0, 255);
    ExpectColor(44, 24, 191, 0, 0);
}

TEST_F(DrawPixelsTest, PboOffsetsAndUnpackLayoutAreAppliedAndRestored) {
    std::array<GLubyte, 4 * 4 * 3> source{};
    const auto set = [&](int row, int column, std::array<GLubyte, 4> color) {
        const size_t offset = static_cast<size_t>(row * 4 + column) * 4u;
        for (size_t component = 0; component < 4; ++component)
            source[offset + component] = color[component];
    };
    set(1, 1, {255, 0, 0, 255});
    set(1, 2, {0, 255, 0, 255});
    set(2, 1, {0, 0, 255, 255});
    set(2, 2, {255, 255, 255, 255});

    GLuint pbo = 0;
    gen_buffers_(1, &pbo);
    ASSERT_NE(pbo, 0u);
    bind_buffer_(GL_PIXEL_UNPACK_BUFFER_, pbo);
    buffer_data_(GL_PIXEL_UNPACK_BUFFER_, static_cast<GLsizeiptr>(source.size()), source.data(),
                 GL_STATIC_DRAW_);
    pixel_store_(GL_UNPACK_ALIGNMENT_, 4);
    pixel_store_(GL_UNPACK_ROW_LENGTH_, 4);
    pixel_store_(GL_UNPACK_SKIP_ROWS_, 1);
    pixel_store_(GL_UNPACK_SKIP_PIXELS_, 1);

    window_pos_(8, 8);
    draw_pixels_(2, 2, GL_RGBA_, GL_UNSIGNED_BYTE_, nullptr);

    GLint binding = 0, row_length = 0, skip_rows = 0, skip_pixels = 0;
    get_integer_(GL_PIXEL_UNPACK_BUFFER_BINDING_, &binding);
    get_integer_(GL_UNPACK_ROW_LENGTH_, &row_length);
    get_integer_(GL_UNPACK_SKIP_ROWS_, &skip_rows);
    get_integer_(GL_UNPACK_SKIP_PIXELS_, &skip_pixels);
    EXPECT_EQ(binding, static_cast<GLint>(pbo));
    EXPECT_EQ(row_length, 4);
    EXPECT_EQ(skip_rows, 1);
    EXPECT_EQ(skip_pixels, 1);

    bind_buffer_(GL_PIXEL_UNPACK_BUFFER_, 0);
    pixel_store_(GL_UNPACK_ROW_LENGTH_, 0);
    pixel_store_(GL_UNPACK_SKIP_ROWS_, 0);
    pixel_store_(GL_UNPACK_SKIP_PIXELS_, 0);
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
    ExpectColor(12, 12, 255, 0, 0);
    ExpectColor(20, 12, 0, 255, 0);
    ExpectColor(12, 20, 0, 0, 255);
    ExpectColor(20, 20, 255, 255, 255);
    delete_buffers_(1, &pbo);
}

TEST_F(DrawPixelsTest, DepthPixelsWriteDepthWithoutChangingColorOrColorMask) {
    // glReadPixels(GL_DEPTH_COMPONENT) comes back GL_INVALID_OPERATION on
    // this driver on every framebuffer - default or a texture+renderbuffer
    // FBO alike (verified independently: the write itself completes with no
    // error, only the readback rejects the format) - so depth is checked the
    // way an application actually observes it: render a second, distinctly-
    // coloured quad through the ordinary depth-tested pipeline and see
    // whether IT wins or loses against the depth glDrawPixels wrote, at two
    // window depths straddling the written value.
    //
    // This test's own probe quad was, ironically, the thing hiding the real
    // bug: depth testing/writemask/GL_ALWAYS all read back correct at draw
    // time via direct backend queries, yet the probe consistently behaved as
    // though the depth buffer were untouched. Root cause turned out to be in
    // SHADOWED_STATE (ordered_passthrough.cpp), not in drawQuad at all: on
    // the very FIRST call to a SHADOWED_STATE-gated entry point in a
    // process, `shadow.known && (shadow_test)` short-circuited past
    // shadow_test's side effect (the line that actually records the new
    // value), so the backend received the right value but the shadow kept
    // its compile-time default. This test's own depth_func_(GL_ALWAYS_) was
    // that first call, so when the probe quad below then asked for
    // GL_LESS, the shadow's stale belief made that call look redundant and
    // it never reached the backend - the probe silently kept testing with
    // GL_ALWAYS against whatever was ACTUALLY there, which is exactly "wins
    // no matter what the depth buffer holds". Fixed by making shadow_test
    // always evaluate, gating only the redundant-call skip on shadow.known.
    clear_color_(0.8f, 0.1f, 0.6f, 1.0f);
    clear_depth_(1.0f);
    clear_(GL_COLOR_BUFFER_BIT_ | GL_DEPTH_BUFFER_BIT_);
    enable_(GL_DEPTH_TEST_);
    depth_func_(GL_ALWAYS_);
    depth_mask_(sfpew_test::GL_TRUE_);

    const GLfloat depth = 0.25f;
    window_pos_(8, 8);
    pixel_zoom_(16.0f, 16.0f);
    draw_pixels_(1, 1, GL_DEPTH_COMPONENT_, GL_FLOAT_, &depth);
    ExpectColor(16, 16, 204, 26, 153, 10); // the depth write must not touch color

    std::array<GLboolean, 4> mask{};
    get_boolean_(GL_COLOR_WRITEMASK_, mask.data());
    EXPECT_EQ(mask[0], sfpew_test::GL_TRUE_);
    EXPECT_EQ(mask[1], sfpew_test::GL_TRUE_);
    EXPECT_EQ(mask[2], sfpew_test::GL_TRUE_);
    EXPECT_EQ(mask[3], sfpew_test::GL_TRUE_);

    // With the default depth range [0,1] and identity matrices, window depth
    // is (ndc.z + 1) / 2 - the same convention glDrawPixels' depth quad uses
    // internally. A full-viewport quad at ndc.z=0.0 lands at window depth
    // 0.5, deeper than the 0.25 just written; one at ndc.z=-0.8 lands at 0.1,
    // in front of it.
    depth_func_(GL_LESS_);
    color4f_(0.0f, 1.0f, 0.0f, 1.0f);
    begin_(GL_QUADS_);
    vertex3f_(-1.0f, -1.0f, 0.0f);
    vertex3f_(1.0f, -1.0f, 0.0f);
    vertex3f_(1.0f, 1.0f, 0.0f);
    vertex3f_(-1.0f, 1.0f, 0.0f);
    end_();
    ExpectColor(16, 16, 204, 26, 153, 10); // deeper quad lost: background survives

    color4f_(1.0f, 1.0f, 0.0f, 1.0f);
    begin_(GL_QUADS_);
    vertex3f_(-1.0f, -1.0f, -0.8f);
    vertex3f_(1.0f, -1.0f, -0.8f);
    vertex3f_(1.0f, 1.0f, -0.8f);
    vertex3f_(-1.0f, 1.0f, -0.8f);
    end_();
    ExpectColor(16, 16, 255, 255, 0, 10); // nearer quad won: its color shows

    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
    disable_(GL_DEPTH_TEST_);
}

} // namespace
