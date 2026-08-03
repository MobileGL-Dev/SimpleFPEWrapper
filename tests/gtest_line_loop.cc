// SimpleFPEWrapper - tests/gtest_line_loop.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// Ported from piglit's tests/spec/gl-1.0/long-line-loop.c (MIT-style
// license; see piglit's COPYING), whose point is that a GL_LINE_LOOP's
// CLOSING segment - the one from the last vertex back to the first - must be
// drawn, and must stay drawn as the vertex count grows.
//
// Upstream checks this by drawing a many-segment circle as a LINE_LOOP and
// again as a LINE_STRIP with the first vertex repeated, then requiring the
// two renderings to be pixel-identical. This port instead traces a rectangle
// outline (with collinear intermediate vertices to reach any desired vertex
// count) and probes the midpoint of each of the four edges, which is
// resolution-independent and does not depend on line-rasterization tie-break
// rules that differ between drivers.
//
// The vertex counts matter to THIS wrapper specifically. Small runs are
// merged and rewritten into independent GL_LINES by appendMergedRun, which
// has to synthesize the closing segment itself (`emit(count - 1); emit(0)`);
// runs past the merge-size limit skip the expander and reach the backend as
// a real GL_LINE_LOOP. Both paths are covered here, plus the client-array
// path that upstream uses. A closing segment dropped in the expander would
// otherwise only show up as a subtly open outline in an app.

#include "sfpew_gtest.h"

#include <optional>
#include <string>

namespace {

using sfpew_test::ContextTest;
using sfpew_test::GLbitfield;
using sfpew_test::GLenum;
using sfpew_test::GLfloat;
using sfpew_test::GLint;
using sfpew_test::GLsizei;
using sfpew_test::PixelProbe;

constexpr int kWindow = 64;
constexpr GLenum GL_LINE_LOOP_ = 0x0002;
constexpr GLbitfield GL_COLOR_BUFFER_BIT_ = 0x00004000;
constexpr GLenum GL_FLOAT_ = 0x1406;
constexpr GLenum GL_PROJECTION_ = 0x1701;
constexpr GLenum GL_MODELVIEW_ = 0x1700;
constexpr GLenum GL_VERTEX_ARRAY_ = 0x8074;
constexpr GLenum GL_NO_ERROR_ = 0;

// The outline the test traces, in window coordinates.
constexpr int kX0 = 8, kY0 = 8, kX1 = 56, kY1 = 56;

// Walk the rectangle perimeter counter-clockwise, subdividing each side so
// the loop has `perSide * 4` vertices in total. The extra vertices are
// collinear, so the drawn shape is the same rectangle at every count.
void BuildOutline(int perSide, GLfloat* out) {
    int n = 0;
    for (int i = 0; i < perSide; ++i) { // bottom, left to right
        const float t = static_cast<float>(i) / static_cast<float>(perSide);
        out[n++] = kX0 + (kX1 - kX0) * t;
        out[n++] = kY0 + 0.5f;
    }
    for (int i = 0; i < perSide; ++i) { // right, bottom to top
        const float t = static_cast<float>(i) / static_cast<float>(perSide);
        out[n++] = kX1 - 0.5f;
        out[n++] = kY0 + (kY1 - kY0) * t;
    }
    for (int i = 0; i < perSide; ++i) { // top, right to left
        const float t = static_cast<float>(i) / static_cast<float>(perSide);
        out[n++] = kX1 - (kX1 - kX0) * t;
        out[n++] = kY1 - 0.5f;
    }
    for (int i = 0; i < perSide; ++i) { // left, top to bottom
        const float t = static_cast<float>(i) / static_cast<float>(perSide);
        out[n++] = kX0 + 0.5f;
        out[n++] = kY1 - (kY1 - kY0) * t;
    }
}

class LineLoopTest : public ContextTest {
protected:
    LineLoopTest() : ContextTest(sfpew_test::Backend::GLES3, kWindow) {}

    void SetUp() override {
        ContextTest::SetUp();
        if (::testing::Test::IsSkipped()) return;

        vertex2f_ = Get<void (*)(GLfloat, GLfloat)>("glVertex2f");
        begin_ = Get<void (*)(GLenum)>("glBegin");
        end_ = Get<void (*)()>("glEnd");
        color3f_ = Get<void (*)(GLfloat, GLfloat, GLfloat)>("glColor3f");
        clear_color_ = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glClearColor");
        clear_ = Get<void (*)(GLbitfield)>("glClear");
        ortho_ = Get<void (*)(double, double, double, double, double, double)>("glOrtho");
        matrix_mode_ = Get<void (*)(GLenum)>("glMatrixMode");
        load_identity_ = Get<void (*)()>("glLoadIdentity");
        finish_ = Get<void (*)()>("glFinish");
        vertex_pointer_ = Get<void (*)(GLint, GLenum, GLsizei, const void*)>("glVertexPointer");
        enable_client_state_ = Get<void (*)(GLenum)>("glEnableClientState");
        disable_client_state_ = Get<void (*)(GLenum)>("glDisableClientState");
        draw_arrays_ = Get<void (*)(GLenum, GLint, GLsizei)>("glDrawArrays");
        get_error_ = Get<GLenum (*)()>("glGetError");
        auto read_pixels =
            Get<void (*)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*)>("glReadPixels");
        ASSERT_NE(read_pixels, nullptr);
        probe_.emplace(read_pixels);

        matrix_mode_(GL_PROJECTION_);
        load_identity_();
        ortho_(0, kWindow, 0, kWindow, -1, 1);
        matrix_mode_(GL_MODELVIEW_);
        load_identity_();
        clear_color_(0, 0, 0, 0);
        color3f_(0, 1, 0);
        get_error_();
    }

    // All four edges must be lit. The LEFT edge is the closing segment for
    // the vertex order built above, so it is the one that goes missing if
    // the loop is not closed. The interior must stay empty: a loop drawn as
    // a filled primitive, or a stray closing triangle, would light it up.
    void CheckOutline() {
        const int mx = (kX0 + kX1) / 2, my = (kY0 + kY1) / 2;
        SFPEW_EXPECT_LIT(*probe_, mx, kY0, true, "bottom edge");
        SFPEW_EXPECT_LIT(*probe_, kX1 - 1, my, true, "right edge");
        SFPEW_EXPECT_LIT(*probe_, mx, kY1 - 1, true, "top edge");
        SFPEW_EXPECT_LIT(*probe_, kX0, my, true, "left edge (the closing segment)");
        SFPEW_EXPECT_LIT(*probe_, mx, my, false, "interior");
    }

    void (*vertex2f_)(GLfloat, GLfloat) = nullptr;
    void (*begin_)(GLenum) = nullptr;
    void (*end_)() = nullptr;
    void (*color3f_)(GLfloat, GLfloat, GLfloat) = nullptr;
    void (*clear_color_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*clear_)(GLbitfield) = nullptr;
    void (*ortho_)(double, double, double, double, double, double) = nullptr;
    void (*matrix_mode_)(GLenum) = nullptr;
    void (*load_identity_)() = nullptr;
    void (*finish_)() = nullptr;
    void (*vertex_pointer_)(GLint, GLenum, GLsizei, const void*) = nullptr;
    void (*enable_client_state_)(GLenum) = nullptr;
    void (*disable_client_state_)(GLenum) = nullptr;
    void (*draw_arrays_)(GLenum, GLint, GLsizei) = nullptr;
    GLenum (*get_error_)() = nullptr;
    std::optional<PixelProbe> probe_;
};

// 4 verts merges and expands; 256 verts (64 per side) is past the merge-size
// limit and reaches the backend as a real GL_LINE_LOOP.
class LineLoopSizeTest : public LineLoopTest, public ::testing::WithParamInterface<int> {};

TEST_P(LineLoopSizeTest, BeginEndClosesTheLoop) {
    const int per_side = GetParam();
    const int count = per_side * 4;
    static GLfloat verts[4 * 64 * 2];
    BuildOutline(per_side, verts);

    clear_(GL_COLOR_BUFFER_BIT_);
    begin_(GL_LINE_LOOP_);
    for (int i = 0; i < count; ++i) vertex2f_(verts[i * 2], verts[i * 2 + 1]);
    end_();
    finish_();
    CheckOutline();
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

TEST_P(LineLoopSizeTest, DrawArraysClosesTheLoop) {
    const int per_side = GetParam();
    const int count = per_side * 4;
    static GLfloat verts[4 * 64 * 2];
    BuildOutline(per_side, verts);

    clear_(GL_COLOR_BUFFER_BIT_);
    enable_client_state_(GL_VERTEX_ARRAY_);
    vertex_pointer_(2, GL_FLOAT_, 0, verts);
    draw_arrays_(GL_LINE_LOOP_, 0, count);
    disable_client_state_(GL_VERTEX_ARRAY_);
    finish_();
    CheckOutline();
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

INSTANTIATE_TEST_SUITE_P(VertexCounts, LineLoopSizeTest, ::testing::Values(1, 4, 16, 64),
                         [](const ::testing::TestParamInfo<int>& info) {
                             return "PerSide" + std::to_string(info.param);
                         });

} // namespace
