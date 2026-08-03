// SimpleFPEWrapper - tests/gtest_immediate_merge.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// Consecutive small glBegin/glEnd runs are merged into a single draw call
// (plans/12). That rewrites strips, fans and quads into independent
// primitives and delays the draw until some other entry point flushes it, so
// this test pins down what must stay observable:
//   A. every mergeable primitive still covers the pixels it covered alone,
//   B. per-run colors survive the merge (run N does not take run N+1's color),
//   C. a matrix change between two runs is NOT absorbed by the merge,
//   D. a lone run still lands (nothing is left pending at glReadPixels).
//   E. draw order across the merge boundary is preserved.

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
constexpr GLenum GL_TRIANGLES_ = 0x0004;
constexpr GLenum GL_TRIANGLE_STRIP_ = 0x0005;
constexpr GLenum GL_TRIANGLE_FAN_ = 0x0006;
constexpr GLenum GL_QUADS_ = 0x0007;
constexpr GLenum GL_QUAD_STRIP_ = 0x0008;
constexpr GLenum GL_POLYGON_ = 0x0009;
constexpr GLenum GL_MODELVIEW_ = 0x1700;
constexpr GLenum GL_NO_ERROR_ = 0;

class ImmediateMergeTest : public ContextTest {
protected:
    ImmediateMergeTest() : ContextTest(sfpew_test::Backend::GLES3, kFramebuffer) {}

    void SetUp() override {
        ContextTest::SetUp();
        if (::testing::Test::IsSkipped()) return;

        clear_color_ = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glClearColor");
        clear_ = Get<void (*)(GLbitfield)>("glClear");
        begin_ = Get<void (*)(GLenum)>("glBegin");
        end_ = Get<void (*)()>("glEnd");
        color3f_ = Get<void (*)(GLfloat, GLfloat, GLfloat)>("glColor3f");
        vertex2f_ = Get<void (*)(GLfloat, GLfloat)>("glVertex2f");
        get_error_ = Get<GLenum (*)()>("glGetError");
        finish_ = Get<void (*)()>("glFinish");
        matrix_mode_ = Get<void (*)(GLenum)>("glMatrixMode");
        load_identity_ = Get<void (*)()>("glLoadIdentity");
        translatef_ = Get<void (*)(GLfloat, GLfloat, GLfloat)>("glTranslatef");
        auto read_pixels =
            Get<void (*)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*)>("glReadPixels");
        ASSERT_NE(read_pixels, nullptr);
        probe_.emplace(read_pixels);

        matrix_mode_(GL_MODELVIEW_);
        load_identity_();
    }

    // One 0.8x0.8 NDC box centered at (cx, 0), expressed in whichever
    // primitive is asked for. Every form covers the box's center pixel, so a
    // correct expansion is observable no matter which rewrite path the
    // merger took.
    void Box(GLenum primitive, GLfloat cx, GLfloat r, GLfloat g, GLfloat b) {
        const GLfloat l = cx - 0.4f, rt = cx + 0.4f, bt = -0.4f, tp = 0.4f;
        begin_(primitive);
        color3f_(r, g, b);
        switch (primitive) {
        case GL_TRIANGLES_:
            // Two independent triangles covering the box.
            vertex2f_(l, bt);
            vertex2f_(rt, bt);
            vertex2f_(rt, tp);
            vertex2f_(rt, tp);
            vertex2f_(l, tp);
            vertex2f_(l, bt);
            break;
        case GL_TRIANGLE_STRIP_:
            // Strip order: bl, br, tl, tr.
            vertex2f_(l, bt);
            vertex2f_(rt, bt);
            vertex2f_(l, tp);
            vertex2f_(rt, tp);
            break;
        case GL_QUAD_STRIP_:
            // Quad-strip order: bl, tl, br, tr forms one quad.
            vertex2f_(l, bt);
            vertex2f_(l, tp);
            vertex2f_(rt, bt);
            vertex2f_(rt, tp);
            break;
        default:
            // QUADS, POLYGON and TRIANGLE_FAN all take the perimeter in order.
            vertex2f_(l, bt);
            vertex2f_(rt, bt);
            vertex2f_(rt, tp);
            vertex2f_(l, tp);
            break;
        }
        end_();
    }

    // A run long enough that the merger declines it (> the vertex limit), so
    // it draws directly. Covers the whole framebuffer.
    void BigQuad(GLfloat r, GLfloat g, GLfloat b) {
        begin_(GL_QUADS_);
        color3f_(r, g, b);
        for (int i = 0; i < 40; ++i) {
            // 40 stacked full-width strips: 160 vertices, past the merge limit.
            const GLfloat y0 = -1.0f + static_cast<GLfloat>(i) * 0.05f;
            const GLfloat y1 = y0 + 0.05f;
            vertex2f_(-1.0f, y0);
            vertex2f_(1.0f, y0);
            vertex2f_(1.0f, y1);
            vertex2f_(-1.0f, y1);
        }
        end_();
    }

    // r/g/b < 0 means "don't care"; the point is which primitive covered the
    // pixel, not exact shading, so this is tolerant rather than exact.
    void ExpectPixel(int x, int y, int r, int g, int b, const char* what) {
        const PixelProbe::Rgba p = probe_->At(x, y);
        const bool ok = (r < 0 || (p.r > 200) == (r > 0)) && (g < 0 || (p.g > 200) == (g > 0)) &&
                        (b < 0 || (p.b > 200) == (b > 0));
        EXPECT_TRUE(ok) << what << ": pixel(" << x << ',' << y << ") = (" << (int)p.r << ','
                        << (int)p.g << ',' << (int)p.b << "), expected (" << r << ',' << g << ','
                        << b << ')';
    }

    void (*clear_color_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*clear_)(GLbitfield) = nullptr;
    void (*begin_)(GLenum) = nullptr;
    void (*end_)() = nullptr;
    void (*color3f_)(GLfloat, GLfloat, GLfloat) = nullptr;
    void (*vertex2f_)(GLfloat, GLfloat) = nullptr;
    GLenum (*get_error_)() = nullptr;
    void (*finish_)() = nullptr;
    void (*matrix_mode_)(GLenum) = nullptr;
    void (*load_identity_)() = nullptr;
    void (*translatef_)(GLfloat, GLfloat, GLfloat) = nullptr;
    std::optional<PixelProbe> probe_;
};

TEST_F(ImmediateMergeTest, TwoDisjointQuadRunsKeepTheirOwnColorAndPixels) {
    clear_color_(0.0f, 0.0f, 1.0f, 1.0f);
    clear_(GL_COLOR_BUFFER_BIT_);
    Box(GL_QUADS_, -0.5f, 1.0f, 0.0f, 0.0f);
    Box(GL_QUADS_, 0.5f, 0.0f, 1.0f, 0.0f);
    finish_();
    ExpectPixel(16, 32, 1, 0, 0, "left quad of a merged pair");
    ExpectPixel(48, 32, 0, 1, 0, "right quad of a merged pair");
    ExpectPixel(32, 4, 0, 0, 1, "gap between the merged quads stays cleared");
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

class MergeablePrimitiveTest : public ImmediateMergeTest,
                               public ::testing::WithParamInterface<GLenum> {};

// Strips, fans and quad strips lose their implicit connectivity when
// rewritten, so a wrong expansion shows up as a missing or misplaced region.
TEST_P(MergeablePrimitiveTest, TwoRunsEachKeepTheirOwnPixels) {
    clear_color_(0.0f, 0.0f, 1.0f, 1.0f);
    clear_(GL_COLOR_BUFFER_BIT_);
    Box(GetParam(), -0.5f, 1.0f, 0.0f, 0.0f);
    Box(GetParam(), 0.5f, 0.0f, 1.0f, 0.0f);
    finish_();
    ExpectPixel(16, 32, 1, 0, 0, "left run");
    ExpectPixel(48, 32, 0, 1, 0, "right run");
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

INSTANTIATE_TEST_SUITE_P(
    Primitives, MergeablePrimitiveTest,
    ::testing::Values(GL_TRIANGLE_STRIP_, GL_TRIANGLE_FAN_, GL_QUAD_STRIP_, GL_POLYGON_,
                      GL_TRIANGLES_),
    [](const ::testing::TestParamInfo<GLenum>& info) {
        switch (info.param) {
        case GL_TRIANGLE_STRIP_: return std::string("TriangleStrip");
        case GL_TRIANGLE_FAN_: return std::string("TriangleFan");
        case GL_QUAD_STRIP_: return std::string("QuadStrip");
        case GL_POLYGON_: return std::string("Polygon");
        case GL_TRIANGLES_: return std::string("Triangles");
        default: return std::string("Unknown");
        }
    });

TEST_F(ImmediateMergeTest, MatrixChangeBetweenRunsIsNotAbsorbed) {
    // Both runs use the same local coordinates; only the second is
    // translated. Merging them would draw both at the translated place and
    // leave the first's pixels cleared.
    clear_color_(0.0f, 0.0f, 1.0f, 1.0f);
    clear_(GL_COLOR_BUFFER_BIT_);
    load_identity_();
    Box(GL_QUADS_, -0.5f, 1.0f, 0.0f, 0.0f);
    translatef_(1.0f, 0.0f, 0.0f);
    Box(GL_QUADS_, -0.5f, 0.0f, 1.0f, 0.0f);
    finish_();
    ExpectPixel(16, 32, 1, 0, 0, "run before the translate stays put");
    ExpectPixel(48, 32, 0, 1, 0, "run after the translate is translated");
    load_identity_();
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

TEST_F(ImmediateMergeTest, LonePendingRunIsFlushedByReadPixels) {
    clear_color_(0.0f, 0.0f, 1.0f, 1.0f);
    clear_(GL_COLOR_BUFFER_BIT_);
    Box(GL_QUADS_, 0.0f, 1.0f, 0.0f, 0.0f);
    ExpectPixel(32, 32, 1, 0, 0, "lone pending run must reach the framebuffer by ReadPixels alone");
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

TEST_F(ImmediateMergeTest, LaterLargeRunOverwritesAnEarlierMergedRun) {
    clear_color_(0.0f, 0.0f, 1.0f, 1.0f);
    clear_(GL_COLOR_BUFFER_BIT_);
    Box(GL_QUADS_, 0.0f, 1.0f, 0.0f, 0.0f);
    BigQuad(0.0f, 1.0f, 0.0f);
    finish_();
    ExpectPixel(32, 32, 0, 1, 0, "later large (non-mergeable) run must win draw order");
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

} // namespace
