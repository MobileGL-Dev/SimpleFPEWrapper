// SimpleFPEWrapper - tests/gtest_polygon_mode.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// glPolygonMode(GL_LINE) and glPolygonMode(GL_POINT) emulation, on BOTH draw
// paths and for every primitive glPolygonMode applies to.
//
// GLES has no polygon mode, so the wrapper emulates it: GL_LINE expands each
// filled primitive into the GL_LINES pairs that outline it, GL_POINT draws
// its vertices as GL_POINTS. The client-array path had this from the start;
// the immediate-mode path ignored polygon mode entirely and always drew
// filled until it was added alongside this test, so a glBegin/glEnd
// wireframe silently came out solid.
//
// What each case checks, on a rectangle drawn near the edges of the
// viewport: under GL_LINE the border pixels are lit and the INTERIOR is not
// (that is the whole point - a filled draw lights both); under GL_POINT the
// corners are lit and the edges between them are not.
//
// The quad cases matter beyond their own correctness. Merging small
// immediate runs rewrites quads and fans into independent triangles, which
// is invisible when filled but would draw each quad's DIAGONAL once
// outlined. Drawing several quads in one Begin/End, plus consecutive
// single-quad runs that would otherwise merge, pins that the outline stays
// the application's own.
//
// Per-face polygon modes (front != back) are deliberately not covered:
// splitting a draw by facing needs CPU-side facing tests, so the wrapper
// documents that as a gap and rasterizes filled.

#include "sfpew_gtest.h"

#include <optional>
#include <ostream>

namespace {

using sfpew_test::ContextTest;
using sfpew_test::GLbitfield;
using sfpew_test::GLenum;
using sfpew_test::GLfloat;
using sfpew_test::GLint;
using sfpew_test::GLsizei;
using sfpew_test::GLubyte;
using sfpew_test::PixelProbe;

constexpr int kWindow = 32;
constexpr GLenum GL_TRIANGLES_ = 0x0004;
constexpr GLenum GL_TRIANGLE_STRIP_ = 0x0005;
constexpr GLenum GL_TRIANGLE_FAN_ = 0x0006;
constexpr GLenum GL_QUADS_ = 0x0007;
constexpr GLenum GL_QUAD_STRIP_ = 0x0008;
constexpr GLenum GL_POLYGON_ = 0x0009;
constexpr GLenum GL_FILL_ = 0x1B02;
constexpr GLenum GL_LINE_ = 0x1B01;
constexpr GLenum GL_POINT_ = 0x1B00;
constexpr GLenum GL_FRONT_AND_BACK_ = 0x0408;
constexpr GLbitfield GL_COLOR_BUFFER_BIT_ = 0x00004000;
constexpr GLenum GL_FLOAT_ = 0x1406;
constexpr GLenum GL_PROJECTION_ = 0x1701;
constexpr GLenum GL_MODELVIEW_ = 0x1700;
constexpr GLenum GL_VERTEX_ARRAY_ = 0x8074;
constexpr GLenum GL_NO_ERROR_ = 0;

// The rectangle every case outlines, in window coordinates.
constexpr int kX0 = 6, kY0 = 6, kX1 = 26, kY1 = 26;

// Vertex lists tracing the same rectangle for each primitive mode. Counts
// are chosen so every mode covers the identical area.
constexpr GLfloat kQuad[] = {kX0, kY0, kX1, kY0, kX1, kY1, kX0, kY1};
constexpr GLfloat kTris[] = {kX0, kY0, kX1, kY0, kX1, kY1, kX1, kY1, kX0, kY1, kX0, kY0};
constexpr GLfloat kStrip[] = {kX0, kY0, kX1, kY0, kX0, kY1, kX1, kY1};

struct PrimitiveCase {
    const char* name;
    GLenum prim;
    const GLfloat* verts;
    int count;
};

void PrintTo(const PrimitiveCase& c, std::ostream* os) { *os << c.name; }

constexpr PrimitiveCase kCases[] = {
    {"Quads", GL_QUADS_, kQuad, 4},
    {"Polygon", GL_POLYGON_, kQuad, 4},
    {"TriangleFan", GL_TRIANGLE_FAN_, kQuad, 4},
    {"Triangles", GL_TRIANGLES_, kTris, 6},
    {"TriangleStrip", GL_TRIANGLE_STRIP_, kStrip, 4},
    {"QuadStrip", GL_QUAD_STRIP_, kStrip, 4},
};

class PolygonModeTest : public ContextTest {
protected:
    PolygonModeTest() : ContextTest(sfpew_test::Backend::GLES3, kWindow) {}

    void SetUp() override {
        ContextTest::SetUp();
        if (::testing::Test::IsSkipped()) return;

        vertex2f_ = Get<void (*)(GLfloat, GLfloat)>("glVertex2f");
        begin_ = Get<void (*)(GLenum)>("glBegin");
        end_ = Get<void (*)()>("glEnd");
        color3f_ = Get<void (*)(GLfloat, GLfloat, GLfloat)>("glColor3f");
        clear_color_ = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glClearColor");
        clear_ = Get<void (*)(GLbitfield)>("glClear");
        polygon_mode_ = Get<void (*)(GLenum, GLenum)>("glPolygonMode");
        ortho_ = Get<void (*)(double, double, double, double, double, double)>("glOrtho");
        matrix_mode_ = Get<void (*)(GLenum)>("glMatrixMode");
        load_identity_ = Get<void (*)()>("glLoadIdentity");
        translatef_ = Get<void (*)(GLfloat, GLfloat, GLfloat)>("glTranslatef");
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
        // Integer window coordinates land exactly on pixel CORNERS, where
        // which pixel a line or point claims is a tie. The usual
        // pixel-perfect-2D nudge puts them unambiguously inside the intended
        // pixel (same bias tests/gtest_orthpos.cc uses).
        translatef_(0.375f, 0.375f, 0.0f);
        clear_color_(0, 0, 0, 0);
        color3f_(0, 1, 0);
        get_error_();
    }

    bool IsLitAt(int x, int y) const { return probe_->IsLitAt(x, y); }

    void DrawImmediate(GLenum prim, const GLfloat* verts, int count) {
        begin_(prim);
        for (int i = 0; i < count; ++i) vertex2f_(verts[i * 2], verts[i * 2 + 1]);
        end_();
    }

    void DrawArrays(GLenum prim, const GLfloat* verts, int count) {
        enable_client_state_(GL_VERTEX_ARRAY_);
        vertex_pointer_(2, GL_FLOAT_, 0, verts);
        draw_arrays_(prim, 0, count);
        disable_client_state_(GL_VERTEX_ARRAY_);
    }

    // GL_LINE: all four borders drawn, interior hollow.
    //
    // The interior probe sits deliberately OFF both corner-to-corner
    // diagonals. The triangle modes decompose the rectangle into two
    // triangles and each triangle is its own primitive, so their shared edge
    // - a diagonal across the rectangle - is a real part of the wireframe
    // and is drawn. Probing the center would therefore be testing the
    // diagonal, not the fill.
    void CheckOutline(const char* what) {
        const int mx = (kX0 + kX1) / 2, my = (kY0 + kY1) / 2;
        const int ix = kX0 + 4, iy = kY0 + 6; // off y == x and off x + y == X1 + Y0
        SFPEW_EXPECT_LIT(*probe_, mx, kY0, true, what);
        SFPEW_EXPECT_LIT(*probe_, mx, kY1, true, what);
        SFPEW_EXPECT_LIT(*probe_, kX0, my, true, what);
        SFPEW_EXPECT_LIT(*probe_, kX1, my, true, what);
        SFPEW_EXPECT_LIT(*probe_, ix, iy, false, what);
    }

    // GL_POINT: the corner vertices are drawn, the spans between them are not.
    void CheckPoints(const char* what) {
        const int mx = (kX0 + kX1) / 2, my = (kY0 + kY1) / 2;
        SFPEW_EXPECT_LIT(*probe_, kX0, kY0, true, what);
        SFPEW_EXPECT_LIT(*probe_, mx, kY0, false, what);
        SFPEW_EXPECT_LIT(*probe_, mx, my, false, what);
    }

    void (*vertex2f_)(GLfloat, GLfloat) = nullptr;
    void (*begin_)(GLenum) = nullptr;
    void (*end_)() = nullptr;
    void (*color3f_)(GLfloat, GLfloat, GLfloat) = nullptr;
    void (*clear_color_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*clear_)(GLbitfield) = nullptr;
    void (*polygon_mode_)(GLenum, GLenum) = nullptr;
    void (*ortho_)(double, double, double, double, double, double) = nullptr;
    void (*matrix_mode_)(GLenum) = nullptr;
    void (*load_identity_)() = nullptr;
    void (*translatef_)(GLfloat, GLfloat, GLfloat) = nullptr;
    void (*finish_)() = nullptr;
    void (*vertex_pointer_)(GLint, GLenum, GLsizei, const void*) = nullptr;
    void (*enable_client_state_)(GLenum) = nullptr;
    void (*disable_client_state_)(GLenum) = nullptr;
    void (*draw_arrays_)(GLenum, GLint, GLsizei) = nullptr;
    GLenum (*get_error_)() = nullptr;
    std::optional<PixelProbe> probe_;
};

class LineModeCaseTest : public PolygonModeTest,
                         public ::testing::WithParamInterface<PrimitiveCase> {
protected:
    void SetUp() override {
        PolygonModeTest::SetUp();
        if (::testing::Test::IsSkipped()) return;
        polygon_mode_(GL_FRONT_AND_BACK_, GL_LINE_);
    }
};

class PointModeCaseTest : public PolygonModeTest,
                          public ::testing::WithParamInterface<PrimitiveCase> {
protected:
    void SetUp() override {
        PolygonModeTest::SetUp();
        if (::testing::Test::IsSkipped()) return;
        polygon_mode_(GL_FRONT_AND_BACK_, GL_POINT_);
    }
};

TEST_P(LineModeCaseTest, ImmediateModeOutlines) {
    const PrimitiveCase& c = GetParam();
    clear_(GL_COLOR_BUFFER_BIT_);
    DrawImmediate(c.prim, c.verts, c.count);
    finish_();
    CheckOutline("LINE glBegin");
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

TEST_P(LineModeCaseTest, DrawArraysOutlines) {
    const PrimitiveCase& c = GetParam();
    clear_(GL_COLOR_BUFFER_BIT_);
    DrawArrays(c.prim, c.verts, c.count);
    finish_();
    CheckOutline("LINE glDrawArrays");
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

TEST_P(PointModeCaseTest, ImmediateModeDrawsPoints) {
    const PrimitiveCase& c = GetParam();
    clear_(GL_COLOR_BUFFER_BIT_);
    DrawImmediate(c.prim, c.verts, c.count);
    finish_();
    CheckPoints("POINT glBegin");
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

TEST_P(PointModeCaseTest, DrawArraysDrawsPoints) {
    const PrimitiveCase& c = GetParam();
    clear_(GL_COLOR_BUFFER_BIT_);
    DrawArrays(c.prim, c.verts, c.count);
    finish_();
    CheckPoints("POINT glDrawArrays");
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

INSTANTIATE_TEST_SUITE_P(Primitives, LineModeCaseTest, ::testing::ValuesIn(kCases),
                         [](const ::testing::TestParamInfo<PrimitiveCase>& info) {
                             return info.param.name;
                         });
INSTANTIATE_TEST_SUITE_P(Primitives, PointModeCaseTest, ::testing::ValuesIn(kCases),
                         [](const ::testing::TestParamInfo<PrimitiveCase>& info) {
                             return info.param.name;
                         });

TEST_F(PolygonModeTest, TwoQuadsInOneBeginEndOutlinePerQuad) {
    // The outline must be per-quad, with no diagonal from a triangle rewrite.
    polygon_mode_(GL_FRONT_AND_BACK_, GL_LINE_);
    clear_(GL_COLOR_BUFFER_BIT_);
    begin_(GL_QUADS_);
    for (int i = 0; i < 4; ++i) vertex2f_(kQuad[i * 2], kQuad[i * 2 + 1]);
    // A second, degenerate-but-offscreen quad so the run holds two.
    for (int i = 0; i < 4; ++i) vertex2f_(kQuad[i * 2] + kWindow, kQuad[i * 2 + 1]);
    end_();
    finish_();
    CheckOutline("LINE two QUADS in one Begin/End");
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

TEST_F(PolygonModeTest, ConsecutiveQuadRunsKeepTheirOwnOutline) {
    // Consecutive single-quad runs: these are exactly what the run merger
    // would fuse into triangles if it were still merging under GL_LINE.
    polygon_mode_(GL_FRONT_AND_BACK_, GL_LINE_);
    clear_(GL_COLOR_BUFFER_BIT_);
    DrawImmediate(GL_QUADS_, kQuad, 4);
    DrawImmediate(GL_QUADS_, kQuad, 4);
    finish_();
    CheckOutline("LINE two consecutive QUADS runs");
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

TEST_F(PolygonModeTest, RestoringFillTakesEffectImmediately) {
    // Restoring the mode has to take effect immediately, including for a run
    // that would have been held back from merging a moment earlier.
    polygon_mode_(GL_FRONT_AND_BACK_, GL_LINE_);
    polygon_mode_(GL_FRONT_AND_BACK_, GL_FILL_);
    clear_(GL_COLOR_BUFFER_BIT_);
    DrawImmediate(GL_QUADS_, kQuad, 4);
    finish_();
    const int mx = (kX0 + kX1) / 2, my = (kY0 + kY1) / 2;
    SFPEW_EXPECT_LIT(*probe_, mx, my, true, "FILL glBegin QUADS");
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

} // namespace
