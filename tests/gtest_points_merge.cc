// SimpleFPEWrapper - tests/gtest_points_merge.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// Small glBegin/glEnd runs merge into one draw call. GL_POINTS is 0, the same
// value as GL_NONE, so a "not mergeable" answer spelled GL_NONE is
// indistinguishable from "merge as points" - which made every GL_POINTS run
// pay a full state submission of its own. A pbuffer cannot show that in
// pixels, so this asserts on sfpewImmediateBatchPendingForTest() as the
// batch-ordering tests do, and separately that merging points still puts them
// where they belong and does not swallow a following non-point run.

#include "sfpew_gtest.h"

#include <optional>

namespace {

using sfpew_test::ContextTest;
using sfpew_test::GLbitfield;
using sfpew_test::GLenum;
using sfpew_test::GLfloat;
using sfpew_test::GLint;
using sfpew_test::GLsizei;
using sfpew_test::PixelProbe;

constexpr int kFramebuffer = 64;
constexpr GLbitfield GL_COLOR_BUFFER_BIT_ = 0x00004000;
constexpr GLenum GL_POINTS_ = 0x0000;
constexpr GLenum GL_TRIANGLE_STRIP_ = 0x0005;
constexpr GLenum GL_MODELVIEW_ = 0x1700;
constexpr GLenum GL_NO_ERROR_ = 0;

class PointsMergeTest : public ContextTest {
protected:
    PointsMergeTest() : ContextTest(sfpew_test::Backend::GLES3, kFramebuffer) {}

    void SetUp() override {
        ContextTest::SetUp();
        if (::testing::Test::IsSkipped() || ::testing::Test::HasFatalFailure()) return;

        clear_color_ = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glClearColor");
        clear_ = Get<void (*)(GLbitfield)>("glClear");
        begin_ = Get<void (*)(GLenum)>("glBegin");
        end_ = Get<void (*)()>("glEnd");
        color3f_ = Get<void (*)(GLfloat, GLfloat, GLfloat)>("glColor3f");
        vertex2f_ = Get<void (*)(GLfloat, GLfloat)>("glVertex2f");
        point_size_ = Get<void (*)(GLfloat)>("glPointSize");
        matrix_mode_ = Get<void (*)(GLenum)>("glMatrixMode");
        load_identity_ = Get<void (*)()>("glLoadIdentity");
        get_error_ = Get<GLenum (*)()>("glGetError");
        pending_ = Dlsym<int (*)()>("sfpewImmediateBatchPendingForTest");
        ASSERT_NE(pending_, nullptr) << "sfpewImmediateBatchPendingForTest not exported";
        auto read_pixels =
            Get<void (*)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*)>("glReadPixels");
        ASSERT_NE(read_pixels, nullptr);
        probe_.emplace(read_pixels);

        matrix_mode_(GL_MODELVIEW_);
        load_identity_();
        point_size_(8.0f);
        clear_color_(0.0f, 0.0f, 0.0f, 1.0f);
        clear_(GL_COLOR_BUFFER_BIT_);
        get_error_();
    }

    void Point(GLfloat x, GLfloat y) {
        begin_(GL_POINTS_);
        color3f_(0.0f, 1.0f, 0.0f);
        vertex2f_(x, y);
        end_();
    }

    void (*clear_color_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*clear_)(GLbitfield) = nullptr;
    void (*begin_)(GLenum) = nullptr;
    void (*end_)() = nullptr;
    void (*color3f_)(GLfloat, GLfloat, GLfloat) = nullptr;
    void (*vertex2f_)(GLfloat, GLfloat) = nullptr;
    void (*point_size_)(GLfloat) = nullptr;
    void (*matrix_mode_)(GLenum) = nullptr;
    void (*load_identity_)() = nullptr;
    GLenum (*get_error_)() = nullptr;
    int (*pending_)() = nullptr;
    std::optional<PixelProbe> probe_;
};

TEST_F(PointsMergeTest, PointRunsAreMergeable) {
    Point(-0.5f, 0.0f);
    EXPECT_TRUE(pending_() != 0) << "a lone GL_POINTS run was submitted instead of held";
    Point(0.5f, 0.0f);
    EXPECT_TRUE(pending_() != 0) << "a second GL_POINTS run did not join the first";

    // Held or not, both points have to land, in the right places.
    SFPEW_EXPECT_LIT(*probe_, 16, 32, true, "left point of a merged pair");
    SFPEW_EXPECT_LIT(*probe_, 48, 32, true, "right point of a merged pair");
    SFPEW_EXPECT_LIT(*probe_, 32, 8, false, "nothing between the merged points");
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

// A points batch must not absorb a run that only looks like it targets
// GL_POINTS because "not concatenable" is spelled with the same value.
TEST_F(PointsMergeTest, AStripAfterAPointsBatchKeepsItsOwnGeometry) {
    Point(-0.5f, 0.5f);
    begin_(GL_TRIANGLE_STRIP_);
    color3f_(0.0f, 1.0f, 0.0f);
    vertex2f_(0.1f, -0.9f);
    vertex2f_(0.9f, -0.9f);
    vertex2f_(0.1f, -0.1f);
    vertex2f_(0.9f, -0.1f);
    end_();

    SFPEW_EXPECT_LIT(*probe_, 16, 48, true, "point run before a strip");
    SFPEW_EXPECT_LIT(*probe_, 48, 16, true, "strip after a point run");
    SFPEW_EXPECT_LIT(*probe_, 8, 8, false, "neither run covers the bottom-left corner");
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

} // namespace
