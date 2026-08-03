// SimpleFPEWrapper - tests/gtest_orthpos.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// Ported from piglit's tests/spec/gl-1.0/orthpos.c (MIT-style license; see
// piglit's COPYING), covering its immediate-mode cases: points, vertical
// lines, horizontal lines and 1x1 quads.
//
// Under an orthographic projection an application can address individual
// pixels, and OpenGL 1.x apps rely on that heavily for 2D drawing. The test
// tiles a square region one primitive per pixel, alternating two colors,
// with blending on. Reading it back then distinguishes three distinct
// failure modes at once:
//
//   - a GAP (background pixel inside the region) means a primitive was
//     rasterized to the wrong pixel or dropped;
//   - an OVERLAP (a pixel blended twice, so brighter than a single 0.5-alpha
//     draw) means two primitives claimed the same pixel;
//   - a BAD EDGE (an unfilled pixel on the region border, or a filled pixel
//     just outside it) means the transform chain has an off-by-half bias.
//
// For this wrapper that exercises the whole fixed-function transform chain
// it synthesizes: glOrtho building the projection matrix, the generated
// vertex shader applying it, and the viewport transform - any half-pixel
// error anywhere in it shows up here and nowhere else in the suite.
//
// Line cases follow OpenGL's diamond-exit rule the same way upstream does:
// the terminal vertex is specified one pixel past the last pixel that should
// be lit, so the lines are treated as half-open.

#include "sfpew_gtest.h"

#include <optional>

namespace {

using sfpew_test::ContextTest;
using sfpew_test::GLbitfield;
using sfpew_test::GLenum;
using sfpew_test::GLfloat;
using sfpew_test::GLint;
using sfpew_test::GLsizei;
using sfpew_test::GLubyte;

constexpr int kWindow = 64;
constexpr int kDraw = 32; // the tiled region is [1, DRAW] on both axes
constexpr GLenum GL_POINTS_ = 0x0000;
constexpr GLenum GL_LINES_ = 0x0001;
constexpr GLenum GL_QUADS_ = 0x0007;
constexpr GLbitfield GL_COLOR_BUFFER_BIT_ = 0x00004000;
constexpr GLenum GL_PROJECTION_ = 0x1701;
constexpr GLenum GL_MODELVIEW_ = 0x1700;
constexpr GLenum GL_BLEND_ = 0x0BE2;
constexpr GLenum GL_SRC_ALPHA_ = 0x0302;
constexpr GLenum GL_ONE_MINUS_SRC_ALPHA_ = 0x0303;
constexpr GLenum GL_NO_ERROR_ = 0;

class OrthPosTest : public ContextTest {
protected:
    OrthPosTest() : ContextTest(sfpew_test::Backend::GLES3, kWindow) {}

    void SetUp() override {
        ContextTest::SetUp();
        if (::testing::Test::IsSkipped()) return;

        vertex2i_ = Get<void (*)(GLint, GLint)>("glVertex2i");
        begin_ = Get<void (*)(GLenum)>("glBegin");
        end_ = Get<void (*)()>("glEnd");
        color4f_ = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glColor4f");
        clear_color_ = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glClearColor");
        clear_ = Get<void (*)(GLbitfield)>("glClear");
        ortho_ = Get<void (*)(double, double, double, double, double, double)>("glOrtho");
        matrix_mode_ = Get<void (*)(GLenum)>("glMatrixMode");
        load_identity_ = Get<void (*)()>("glLoadIdentity");
        translatef_ = Get<void (*)(GLfloat, GLfloat, GLfloat)>("glTranslatef");
        viewport_ = Get<void (*)(GLint, GLint, GLsizei, GLsizei)>("glViewport");
        enable_ = Get<void (*)(GLenum)>("glEnable");
        blend_func_ = Get<void (*)(GLenum, GLenum)>("glBlendFunc");
        finish_ = Get<void (*)()>("glFinish");
        get_error_ = Get<GLenum (*)()>("glGetError");
        read_pixels_ =
            Get<void (*)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*)>("glReadPixels");
        ASSERT_NE(read_pixels_, nullptr);

        viewport_(0, 0, kWindow, kWindow);
        matrix_mode_(GL_PROJECTION_);
        load_identity_();
        ortho_(0, kWindow, 0, kWindow, -1, 1);
        matrix_mode_(GL_MODELVIEW_);
        load_identity_();
        // The classic pixel-perfect-2D bias, straight from upstream: integer
        // vertex coordinates land exactly on pixel CORNERS, where which pixel
        // a point or line claims is a tie. Nudging by 0.375 puts them safely
        // inside one pixel without changing which one. Without it every case
        // here is ambiguous by construction and the results say nothing about
        // the transform chain.
        translatef_(0.375f, 0.375f, 0.0f);
        clear_color_(0, 0, 0, 0);
        blend_func_(GL_SRC_ALPHA_, GL_ONE_MINUS_SRC_ALPHA_);
        enable_(GL_BLEND_);
        get_error_();
    }

    void SetColor(bool alternate) {
        if (alternate)
            color4f_(0.0f, 1.0f, 0.0f, 0.5f);
        else
            color4f_(1.0f, 0.0f, 0.0f, 0.5f);
    }

    // Every pixel of [1, DRAW]^2 must be filled exactly once; everything
    // outside must be untouched. One 0.5-alpha draw over black lands near
    // 127; two land near 191. Classify with a wide band so this stays about
    // geometry, not blend precision.
    void Verify(const char* what) {
        static GLubyte img[kWindow * kWindow * 4];
        read_pixels_(0, 0, kWindow, kWindow, 0x1908 /* GL_RGBA */, 0x1401 /* GL_UNSIGNED_BYTE */,
                     img);

        int gaps = 0, overlaps = 0, other = 0, bad_outside = 0;
        int gx = -1, gy = -1, ox = -1, oy = -1, bx = -1, by = -1;

        for (int y = 0; y < kWindow; ++y) {
            for (int x = 0; x < kWindow; ++x) {
                const GLubyte* p = &img[(y * kWindow + x) * 4];
                const int r = p[0], g = p[1], b = p[2];
                const int inside = x >= 1 && x <= kDraw && y >= 1 && y <= kDraw;
                enum class Kind { Background, Green, Red, Overdrawn, Other } k;
                if (r < 40 && g < 40 && b < 40) {
                    k = Kind::Background;
                } else if (b >= 40 || (r >= 40 && g >= 40)) {
                    k = Kind::Other;
                } else {
                    const int v = r >= 40 ? r : g;
                    if (v > 160) {
                        k = Kind::Overdrawn;
                    } else if (v < 90) {
                        k = Kind::Other;
                    } else {
                        k = r >= 40 ? Kind::Red : Kind::Green;
                    }
                }
                if (inside) {
                    if (k == Kind::Background) {
                        if (gaps++ == 0) {
                            gx = x;
                            gy = y;
                        }
                    } else if (k == Kind::Overdrawn) {
                        if (overlaps++ == 0) {
                            ox = x;
                            oy = y;
                        }
                    } else if (k == Kind::Other) {
                        ++other;
                    }
                } else if (k != Kind::Background) {
                    if (bad_outside++ == 0) {
                        bx = x;
                        by = y;
                    }
                }
            }
        }

        EXPECT_EQ(gaps, 0) << what << ": " << kDraw << 'x' << kDraw
                           << " region tiled exactly, but " << gaps << " gap(s), first at (" << gx
                           << ',' << gy << ')';
        EXPECT_EQ(overlaps, 0) << what << ": " << overlaps << " overlap(s), first at (" << ox
                               << ',' << oy << ')';
        EXPECT_EQ(other, 0) << what << ": " << other << " unclassified pixel(s)";
        EXPECT_EQ(bad_outside, 0) << what << ": " << bad_outside
                                  << " lit pixel(s) outside the region, first at (" << bx << ','
                                  << by << ')';
    }

    void (*vertex2i_)(GLint, GLint) = nullptr;
    void (*begin_)(GLenum) = nullptr;
    void (*end_)() = nullptr;
    void (*color4f_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*clear_color_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*clear_)(GLbitfield) = nullptr;
    void (*ortho_)(double, double, double, double, double, double) = nullptr;
    void (*matrix_mode_)(GLenum) = nullptr;
    void (*load_identity_)() = nullptr;
    void (*translatef_)(GLfloat, GLfloat, GLfloat) = nullptr;
    void (*viewport_)(GLint, GLint, GLsizei, GLsizei) = nullptr;
    void (*enable_)(GLenum) = nullptr;
    void (*blend_func_)(GLenum, GLenum) = nullptr;
    void (*finish_)() = nullptr;
    GLenum (*get_error_)() = nullptr;
    void (*read_pixels_)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*) = nullptr;
};

TEST_F(OrthPosTest, UnitPointsTileTheRegion) {
    clear_(GL_COLOR_BUFFER_BIT_);
    begin_(GL_POINTS_);
    for (int px = 1; px <= kDraw; ++px)
        for (int py = 1; py <= kDraw; ++py) {
            SetColor(((px ^ py) & 1) != 0);
            vertex2i_(px, py);
        }
    end_();
    finish_();
    Verify("immediate points");
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

TEST_F(OrthPosTest, VerticalLinesTileTheRegion) {
    // Half-open: terminal vertex one past the end.
    clear_(GL_COLOR_BUFFER_BIT_);
    begin_(GL_LINES_);
    for (int px = 1; px <= kDraw; ++px) {
        SetColor((px & 1) != 0);
        vertex2i_(px, 1);
        vertex2i_(px, kDraw + 1);
    }
    end_();
    finish_();
    Verify("immediate vlines");
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

TEST_F(OrthPosTest, HorizontalLinesTileTheRegion) {
    clear_(GL_COLOR_BUFFER_BIT_);
    begin_(GL_LINES_);
    for (int py = 1; py <= kDraw; ++py) {
        SetColor((py & 1) != 0);
        vertex2i_(1, py);
        vertex2i_(kDraw + 1, py);
    }
    end_();
    finish_();
    Verify("immediate hlines");
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

TEST_F(OrthPosTest, UnitQuadsTileTheRegion) {
    clear_(GL_COLOR_BUFFER_BIT_);
    begin_(GL_QUADS_);
    for (int px = 1; px <= kDraw; ++px)
        for (int py = 1; py <= kDraw; ++py) {
            SetColor(((px ^ py) & 1) != 0);
            vertex2i_(px, py);
            vertex2i_(px + 1, py);
            vertex2i_(px + 1, py + 1);
            vertex2i_(px, py + 1);
        }
    end_();
    finish_();
    Verify("immediate 1x1 quads");
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

} // namespace
