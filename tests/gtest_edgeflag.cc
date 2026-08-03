// SimpleFPEWrapper - tests/gtest_edgeflag.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// Ported from piglit's tests/spec/gl-1.0/edgeflag.c, edgeflag-quads.c and
// edgeflag-const.c (MIT-style license; see piglit's COPYING), merged into
// one file because all three fail on the same single missing feature.
//
// In GL_LINE polygon mode only edges whose leading vertex had
// glEdgeFlag(GL_TRUE) are drawn. The three cases are: a GL_POLYGON with its
// verticals flagged off, two GL_QUADS in one Begin/End with the same
// pattern (upstream's note: some hardware cannot do per-vertex edge flags on
// quad lists, so they must be split before submission), and a constant
// glEdgeFlag set OUTSIDE Begin/End applying to a whole primitive.
//
// How the wrapper honors these: advance() collects per-vertex edge flags
// into a lazily-populated array parallel to the interleaved vertex stream
// (empty = "all boundary", the GL default, so runs that never clear the
// flag pay one compare per vertex and no allocation), and the shared
// wireframe expansion (sfpewBuildWireframeIndices) drops every edge whose
// LEADING vertex has the flag cleared - GL's rule: the flag current when a
// vertex is specified controls the edge that begins at it.
//
// This test began life as the port's xfail (WILL_FAIL) documenting the gap;
// the GL_LINE emulation and the edge-flag plumbing landed in that order,
// un-xfailing it exactly as the original file header demanded.

#include "sfpew_gtest.h"

#include <optional>

namespace {

using sfpew_test::ContextTest;
using sfpew_test::GLbitfield;
using sfpew_test::GLboolean;
using sfpew_test::GLenum;
using sfpew_test::GLfloat;
using sfpew_test::GLint;
using sfpew_test::GLsizei;
using sfpew_test::PixelProbe;

constexpr int kWindow = 32;
constexpr GLenum GL_QUADS_ = 0x0007;
constexpr GLenum GL_POLYGON_ = 0x0009;
constexpr GLenum GL_LINE_ = 0x1B01;
constexpr GLenum GL_FRONT_AND_BACK_ = 0x0408;
constexpr GLbitfield GL_COLOR_BUFFER_BIT_ = 0x00004000;
constexpr GLenum GL_PROJECTION_ = 0x1701;
constexpr GLenum GL_MODELVIEW_ = 0x1700;
constexpr GLboolean GL_TRUE_ = 1;
constexpr GLboolean GL_FALSE_ = 0;

class EdgeFlagTest : public ContextTest {
protected:
    EdgeFlagTest() : ContextTest(sfpew_test::Backend::GLES3, kWindow) {}

    void SetUp() override {
        ContextTest::SetUp();
        if (::testing::Test::IsSkipped()) return;

        vertex2f_ = Get<void (*)(GLfloat, GLfloat)>("glVertex2f");
        begin_ = Get<void (*)(GLenum)>("glBegin");
        end_ = Get<void (*)()>("glEnd");
        edge_flag_ = Get<void (*)(GLboolean)>("glEdgeFlag");
        color4f_ = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glColor4f");
        clear_color_ = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glClearColor");
        clear_ = Get<void (*)(GLbitfield)>("glClear");
        polygon_mode_ = Get<void (*)(GLenum, GLenum)>("glPolygonMode");
        ortho_ = Get<void (*)(double, double, double, double, double, double)>("glOrtho");
        matrix_mode_ = Get<void (*)(GLenum)>("glMatrixMode");
        load_identity_ = Get<void (*)()>("glLoadIdentity");
        finish_ = Get<void (*)()>("glFinish");
        flush_ = Get<void (*)()>("glFlush");
        auto read_pixels =
            Get<void (*)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*)>("glReadPixels");
        ASSERT_NE(read_pixels, nullptr);
        probe_.emplace(read_pixels);

        matrix_mode_(GL_PROJECTION_);
        load_identity_();
        ortho_(0, kWindow, 0, kWindow, -1, 1);
        matrix_mode_(GL_MODELVIEW_);
        load_identity_();
        polygon_mode_(GL_FRONT_AND_BACK_, GL_LINE_);
        clear_color_(0.0f, 0.0f, 0.0f, 0.0f);
        color4f_(0, 1, 0, 0);
    }

    // One axis-aligned rectangle with the horizontal edges flagged TRUE
    // (boundary, must draw) and the verticals FALSE (must be suppressed).
    void FlaggedRect(GLenum prim, float x0, float y0, float x1, float y1) {
        begin_(prim);
        edge_flag_(GL_TRUE_);
        vertex2f_(x0, y0);
        edge_flag_(GL_FALSE_);
        vertex2f_(x1, y0);
        edge_flag_(GL_TRUE_);
        vertex2f_(x1, y1);
        edge_flag_(GL_FALSE_);
        vertex2f_(x0, y1);
        end_();
    }

    void (*vertex2f_)(GLfloat, GLfloat) = nullptr;
    void (*begin_)(GLenum) = nullptr;
    void (*end_)() = nullptr;
    void (*edge_flag_)(GLboolean) = nullptr;
    void (*color4f_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*clear_color_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*clear_)(GLbitfield) = nullptr;
    void (*polygon_mode_)(GLenum, GLenum) = nullptr;
    void (*ortho_)(double, double, double, double, double, double) = nullptr;
    void (*matrix_mode_)(GLenum) = nullptr;
    void (*load_identity_)() = nullptr;
    void (*finish_)() = nullptr;
    void (*flush_)() = nullptr;
    std::optional<PixelProbe> probe_;
};

TEST_F(EdgeFlagTest, PolygonSuppressesVerticalEdges) {
    clear_(GL_COLOR_BUFFER_BIT_);
    FlaggedRect(GL_POLYGON_, 1.5f, 1.5f, 5.5f, 5.5f);
    finish_();
    SFPEW_EXPECT_LIT(*probe_, 3, 1, true, "polygon: bottom edge");
    SFPEW_EXPECT_LIT(*probe_, 3, 5, true, "polygon: top edge");
    SFPEW_EXPECT_LIT(*probe_, 1, 3, false, "polygon: left edge");
    SFPEW_EXPECT_LIT(*probe_, 5, 3, false, "polygon: right edge");
}

TEST_F(EdgeFlagTest, TwoQuadsInOneBeginEndEachSuppressTheirOwnVerticals) {
    clear_(GL_COLOR_BUFFER_BIT_);
    begin_(GL_QUADS_);
    edge_flag_(GL_TRUE_);
    vertex2f_(1.5f, 1.5f);
    edge_flag_(GL_FALSE_);
    vertex2f_(5.5f, 1.5f);
    edge_flag_(GL_TRUE_);
    vertex2f_(5.5f, 5.5f);
    edge_flag_(GL_FALSE_);
    vertex2f_(1.5f, 5.5f);
    edge_flag_(GL_TRUE_);
    vertex2f_(11.5f, 1.5f);
    edge_flag_(GL_FALSE_);
    vertex2f_(15.5f, 1.5f);
    edge_flag_(GL_TRUE_);
    vertex2f_(15.5f, 5.5f);
    edge_flag_(GL_FALSE_);
    vertex2f_(11.5f, 5.5f);
    end_();
    finish_();

    SFPEW_EXPECT_LIT(*probe_, 3, 1, true, "quads[0]: bottom edge");
    SFPEW_EXPECT_LIT(*probe_, 3, 5, true, "quads[0]: top edge");
    SFPEW_EXPECT_LIT(*probe_, 1, 3, false, "quads[0]: left edge");
    SFPEW_EXPECT_LIT(*probe_, 5, 3, false, "quads[0]: right edge");
    SFPEW_EXPECT_LIT(*probe_, 13, 1, true, "quads[1]: bottom edge");
    SFPEW_EXPECT_LIT(*probe_, 13, 5, true, "quads[1]: top edge");
    SFPEW_EXPECT_LIT(*probe_, 11, 3, false, "quads[1]: left edge");
    SFPEW_EXPECT_LIT(*probe_, 15, 3, false, "quads[1]: right edge");
}

TEST_F(EdgeFlagTest, ConstantFlagSetOutsideBeginEndAppliesToTheWholePrimitive) {
    // TRUE before the first primitive: every edge draws. FALSE before the
    // second: none does. The glFlush between them is upstream's way of
    // stopping the two runs from merging into one per-vertex-attributed
    // batch - which is exactly what this wrapper's run merging would do.
    clear_(GL_COLOR_BUFFER_BIT_);
    edge_flag_(GL_TRUE_);
    begin_(GL_POLYGON_);
    vertex2f_(1.5f, 1.5f);
    vertex2f_(5.5f, 1.5f);
    vertex2f_(5.5f, 5.5f);
    vertex2f_(1.5f, 5.5f);
    end_();
    flush_();
    edge_flag_(GL_FALSE_);
    begin_(GL_POLYGON_);
    vertex2f_(11.5f, 1.5f);
    vertex2f_(15.5f, 1.5f);
    vertex2f_(15.5f, 5.5f);
    vertex2f_(11.5f, 5.5f);
    end_();
    finish_();

    SFPEW_EXPECT_LIT(*probe_, 3, 1, true, "const TRUE: bottom edge");
    SFPEW_EXPECT_LIT(*probe_, 3, 5, true, "const TRUE: top edge");
    SFPEW_EXPECT_LIT(*probe_, 1, 3, true, "const TRUE: left edge");
    SFPEW_EXPECT_LIT(*probe_, 5, 3, true, "const TRUE: right edge");
    SFPEW_EXPECT_LIT(*probe_, 13, 1, false, "const FALSE: bottom edge");
    SFPEW_EXPECT_LIT(*probe_, 13, 5, false, "const FALSE: top edge");
    SFPEW_EXPECT_LIT(*probe_, 11, 3, false, "const FALSE: left edge");
    SFPEW_EXPECT_LIT(*probe_, 15, 3, false, "const FALSE: right edge");
}

} // namespace
