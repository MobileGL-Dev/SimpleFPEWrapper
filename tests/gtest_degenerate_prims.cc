// SimpleFPEWrapper - tests/gtest_degenerate_prims.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// Ported from piglit's tests/general/degenerate-prims.c (MIT-style license;
// see piglit's COPYING). Every primitive mode drawn with FEWER vertices than
// one complete primitive group must draw nothing at all - GL drops the
// incomplete trailing group.
//
// This matters more here than on a native driver. The wrapper rewrites the
// legacy modes GL has but GLES does not: GL_QUADS becomes indexed triangles
// via (count / 4) * 6 indices, GL_QUAD_STRIP becomes GL_TRIANGLE_STRIP and
// GL_POLYGON becomes GL_TRIANGLE_FAN. A rewrite that rounded the wrong way,
// or that forwarded a 3-vertex QUAD_STRIP straight through as a triangle
// strip (which WOULD draw a triangle), turns "nothing" into visible
// geometry. Upstream notes GL_QUADS/GL_QUAD_STRIP with 3 verts as a repeat
// offender in Mesa for exactly this reason.
//
// Upstream drives this through client arrays; this port runs every case
// through both glDrawArrays (the commit_fpe_state_on_draw rewrite) and
// glBegin/glEnd (the drawImmediateVertices rewrite, plus its small-run
// merging), because the two paths convert independently.

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
using sfpew_test::PixelProbe;

constexpr int kWindow = 32;
constexpr GLenum GL_POINTS_ = 0x0000;
constexpr GLenum GL_LINES_ = 0x0001;
constexpr GLenum GL_LINE_LOOP_ = 0x0002;
constexpr GLenum GL_LINE_STRIP_ = 0x0003;
constexpr GLenum GL_TRIANGLES_ = 0x0004;
constexpr GLenum GL_TRIANGLE_STRIP_ = 0x0005;
constexpr GLenum GL_TRIANGLE_FAN_ = 0x0006;
constexpr GLenum GL_QUADS_ = 0x0007;
constexpr GLenum GL_QUAD_STRIP_ = 0x0008;
constexpr GLenum GL_POLYGON_ = 0x0009;
constexpr GLbitfield GL_COLOR_BUFFER_BIT_ = 0x00004000;
constexpr GLenum GL_FLOAT_ = 0x1406;
constexpr GLenum GL_VERTEX_ARRAY_ = 0x8074;
constexpr GLenum GL_PROJECTION_ = 0x1701;
constexpr GLenum GL_MODELVIEW_ = 0x1700;
constexpr GLenum GL_NO_ERROR_ = 0;

// Upstream's vertex sets: each covers the whole viewport, so ANY primitive
// that does get assembled is impossible to miss.
constexpr GLfloat kVerts2[2][2] = {{-1, -1}, {1, 1}};
constexpr GLfloat kVerts3[3][2] = {{-1, -1}, {1, -1}, {0, 1}};
constexpr GLfloat kVerts4[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};

struct Case {
    const char* name;
    GLenum prim;
    int count; // fewer vertices than one complete group
    const GLfloat* verts;
};

// So gtest's parameter printer names cases by their case name instead of
// falling back to a raw byte dump of the struct, which is what CMake's
// gtest_discover_tests then puts INTO the ctest test name.
void PrintTo(const Case& c, std::ostream* os) { *os << c.name; }

constexpr Case kCases[] = {
    {"Points", GL_POINTS_, 0, &kVerts2[0][0]},
    {"Lines", GL_LINES_, 1, &kVerts2[0][0]},
    {"LineStrip", GL_LINE_STRIP_, 1, &kVerts2[0][0]},
    {"LineLoop", GL_LINE_LOOP_, 1, &kVerts2[0][0]},
    {"Triangles", GL_TRIANGLES_, 2, &kVerts3[0][0]},
    {"TriangleStrip", GL_TRIANGLE_STRIP_, 2, &kVerts3[0][0]},
    {"TriangleFan", GL_TRIANGLE_FAN_, 2, &kVerts3[0][0]},
    {"Quads", GL_QUADS_, 3, &kVerts4[0][0]},
    {"QuadStrip", GL_QUAD_STRIP_, 3, &kVerts4[0][0]},
    {"Polygon", GL_POLYGON_, 2, &kVerts4[0][0]},
};

class DegeneratePrimsTest : public ContextTest {
protected:
    DegeneratePrimsTest() : ContextTest(sfpew_test::Backend::GLES3, kWindow) {}

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
        ortho_(-1, 1, -1, 1, -1, 1);
        matrix_mode_(GL_MODELVIEW_);
        load_identity_();
        clear_color_(0.0f, 0.0f, 0.0f, 0.0f);
        color3f_(1, 1, 1);
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

class DegeneratePrimsCaseTest : public DegeneratePrimsTest,
                                public ::testing::WithParamInterface<Case> {};

TEST_P(DegeneratePrimsCaseTest, DrawArraysDrawsNothing) {
    const Case& c = GetParam();
    enable_client_state_(GL_VERTEX_ARRAY_);
    clear_(GL_COLOR_BUFFER_BIT_);
    vertex_pointer_(2, GL_FLOAT_, 0, c.verts);
    draw_arrays_(c.prim, 0, c.count);
    finish_();
    disable_client_state_(GL_VERTEX_ARRAY_);
    SFPEW_EXPECT_BLANK(*probe_, 0, 0, kWindow, kWindow, "glDrawArrays");
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

TEST_P(DegeneratePrimsCaseTest, ImmediateModeDrawsNothing) {
    const Case& c = GetParam();
    clear_(GL_COLOR_BUFFER_BIT_);
    begin_(c.prim);
    for (int v = 0; v < c.count; ++v) vertex2f_(c.verts[v * 2], c.verts[v * 2 + 1]);
    end_();
    finish_();
    SFPEW_EXPECT_BLANK(*probe_, 0, 0, kWindow, kWindow, "glBegin/glEnd");
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

INSTANTIATE_TEST_SUITE_P(Primitives, DegeneratePrimsCaseTest, ::testing::ValuesIn(kCases),
                         [](const ::testing::TestParamInfo<Case>& info) {
                             return info.param.name;
                         });

} // namespace
