// SimpleFPEWrapper - tests/gtest_line_stipple.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// defects-plan.md 1.5: glLineStipple had exact state/queries but no
// rasterization consumer - lines always drew solid regardless of the
// pattern. Covers the immediate-mode dedicated-shader emulation in
// linestipple.cpp: on/off alternation, disabling returns to solid, a wider
// factor widens the dash, GL_LINE_STRIP accumulates distance continuously
// across a bend rather than resetting per segment, and GL_LINE_LOOP's
// closing segment actually renders.
//
// Window space: viewport is 64x64 (ContextTest::kDefaultSize) and every
// line here is drawn horizontally through the middle row with identity
// matrices, so ndc_x -> window_x is the plain (ndc+1)/2*64 map and no
// per-test geometry math is needed beyond that.

#include "sfpew_gtest.h"

#include <optional>

namespace {

using sfpew_test::ContextTest;
using sfpew_test::GLbitfield;
using sfpew_test::GLenum;
using sfpew_test::GLfloat;
using sfpew_test::GLint;
using sfpew_test::GLushort;
using sfpew_test::PixelProbe;

constexpr GLbitfield GL_COLOR_BUFFER_BIT_ = 0x00004000;
constexpr GLenum GL_LINE_STIPPLE_ = 0x0B24;
constexpr GLenum GL_LINES_ = 0x0001;
constexpr GLenum GL_LINE_STRIP_ = 0x0003;
constexpr GLenum GL_LINE_LOOP_ = 0x0002;
constexpr int kSize = 64;
// A horizontal line at NDC y=0.0 maps to window y=32.0 exactly - the boundary
// between rows 31 and 32, not the interior of either. This driver's diamond-
// exit rule rasterizes that boundary line into row 31, not 32 (confirmed by
// probing every row around the midpoint with plain, unstippled rendering).
constexpr int kMidY = kSize / 2 - 1;

class LineStippleTest : public ContextTest {
protected:
    LineStippleTest() : ContextTest(sfpew_test::Backend::GLES3, kSize) {}

    void SetUp() override {
        ContextTest::SetUp();
        if (::testing::Test::IsSkipped()) return;
        enable_ = Get<void (*)(GLenum)>("glEnable");
        disable_ = Get<void (*)(GLenum)>("glDisable");
        line_stipple_ = Get<void (*)(GLint, GLushort)>("glLineStipple");
        clear_color_ = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glClearColor");
        clear_ = Get<void (*)(GLbitfield)>("glClear");
        begin_ = Get<void (*)(GLenum)>("glBegin");
        end_ = Get<void (*)()>("glEnd");
        color4f_ = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glColor4f");
        vertex2f_ = Get<void (*)(GLfloat, GLfloat)>("glVertex2f");
        get_error_ = Get<GLenum (*)()>("glGetError");
        auto read_pixels =
            Get<void (*)(sfpew_test::GLint, sfpew_test::GLint, sfpew_test::GLsizei,
                        sfpew_test::GLsizei, GLenum, GLenum, void*)>("glReadPixels");
        ASSERT_NE(read_pixels, nullptr);
        probe_.emplace(read_pixels);
        clear_color_(0.0f, 0.0f, 0.0f, 1.0f);
    }

    static GLfloat NdcX(int window_x) { return (GLfloat)window_x / kSize * 2.0f - 1.0f; }

    bool LitAt(int window_x) { return probe_->At(window_x, kMidY).g > 150; }

    void DrawHorizontalLine(int x0, int x1) {
        color4f_(0.0f, 1.0f, 0.0f, 1.0f);
        begin_(GL_LINES_);
        vertex2f_(NdcX(x0), 0.0f);
        vertex2f_(NdcX(x1), 0.0f);
        end_();
    }

    void (*enable_)(GLenum) = nullptr;
    void (*disable_)(GLenum) = nullptr;
    void (*line_stipple_)(GLint, GLushort) = nullptr;
    void (*clear_color_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*clear_)(GLbitfield) = nullptr;
    void (*begin_)(GLenum) = nullptr;
    void (*end_)() = nullptr;
    void (*color4f_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*vertex2f_)(GLfloat, GLfloat) = nullptr;
    GLenum (*get_error_)() = nullptr;
    std::optional<PixelProbe> probe_;
};

TEST_F(LineStippleTest, PatternAlternatesOnAndOffAlongTheLine) {
    // 0x00FF, factor 1: bits 0-7 on (pixels 0-7 of each 16px period), bits
    // 8-15 off (pixels 8-15). A 64px line is exactly four periods.
    line_stipple_(1, 0x00FF);
    enable_(GL_LINE_STIPPLE_);
    clear_(GL_COLOR_BUFFER_BIT_);
    DrawHorizontalLine(0, kSize);

    EXPECT_TRUE(LitAt(3)) << "period 0, on-band";
    EXPECT_FALSE(LitAt(11)) << "period 0, off-band";
    EXPECT_TRUE(LitAt(19)) << "period 1, on-band";
    EXPECT_FALSE(LitAt(27)) << "period 1, off-band";
    EXPECT_TRUE(LitAt(35)) << "period 2, on-band";
    EXPECT_FALSE(LitAt(43)) << "period 2, off-band";
    EXPECT_EQ(get_error_(), 0);
}

TEST_F(LineStippleTest, DisablingReturnsToSolid) {
    line_stipple_(1, 0x00FF);
    enable_(GL_LINE_STIPPLE_);
    disable_(GL_LINE_STIPPLE_);
    clear_(GL_COLOR_BUFFER_BIT_);
    DrawHorizontalLine(0, kSize);

    EXPECT_TRUE(LitAt(3));
    EXPECT_TRUE(LitAt(11)) << "would be off-band if stippling were still active";
    EXPECT_TRUE(LitAt(19));
    EXPECT_TRUE(LitAt(27));
}

TEST_F(LineStippleTest, LargerFactorWidensTheDash) {
    // pattern bit0 only, factor 8: pixels 0-7 on, then off until distance
    // 128 (past the whole 64px line) - the whole rest of the line is dark.
    line_stipple_(8, 0x0001);
    enable_(GL_LINE_STIPPLE_);
    clear_(GL_COLOR_BUFFER_BIT_);
    DrawHorizontalLine(0, kSize);

    EXPECT_TRUE(LitAt(3)) << "within the widened 8px dash";
    EXPECT_FALSE(LitAt(20)) << "well past the single dash, factor 8";
    EXPECT_FALSE(LitAt(50));
}

TEST_F(LineStippleTest, LineStripAccumulatesDistanceAcrossABendNotPerSegment) {
    // v0=0, v1=10, v2=40 (window x). Sampling 3px into the second segment
    // (window x=13): continuous distance from v0 is 13 -> bit 13, in the
    // OFF nibble of 0x00FF -> must be UNLIT. A wrong per-segment reset
    // would compute local distance 3 from v1 -> bit 3, ON -> LIT. The two
    // interpretations disagree at this exact point by construction.
    line_stipple_(1, 0x00FF);
    enable_(GL_LINE_STIPPLE_);
    clear_(GL_COLOR_BUFFER_BIT_);
    color4f_(0.0f, 1.0f, 0.0f, 1.0f);
    begin_(GL_LINE_STRIP_);
    vertex2f_(NdcX(0), 0.0f);
    vertex2f_(NdcX(10), 0.0f);
    vertex2f_(NdcX(40), 0.0f);
    end_();

    EXPECT_FALSE(LitAt(13))
        << "continuous accumulation across the v1 bend must keep this in the off-band";
    EXPECT_EQ(get_error_(), 0);
}

TEST_F(LineStippleTest, LineLoopClosingSegmentRenders) {
    // A flat two-point "loop": v0 at window x=0, v1 at window x=32. The
    // implicit closing segment retraces v1 back to v0. With stippling
    // disabled first as a sanity baseline, then enabled: the loop must
    // still produce visible (if partial) content, proving the closing
    // segment is not silently dropped by the extra-vertex handling.
    color4f_(0.0f, 1.0f, 0.0f, 1.0f);
    clear_(GL_COLOR_BUFFER_BIT_);
    begin_(GL_LINE_LOOP_);
    vertex2f_(NdcX(0), 0.0f);
    vertex2f_(NdcX(32), 0.0f);
    end_();
    EXPECT_TRUE(LitAt(16)) << "sanity baseline: solid loop must render";

    line_stipple_(1, 0x00FF);
    enable_(GL_LINE_STIPPLE_);
    clear_(GL_COLOR_BUFFER_BIT_);
    begin_(GL_LINE_LOOP_);
    vertex2f_(NdcX(0), 0.0f);
    vertex2f_(NdcX(32), 0.0f);
    end_();
    EXPECT_TRUE(LitAt(3)) << "start of the outbound segment must be lit (distance 0)";
    EXPECT_EQ(get_error_(), 0);
}

} // namespace
