// SimpleFPEWrapper - tests/gtest_dlist_shademodel.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// Ported from piglit's tests/spec/gl-1.0/dlist-shademodel.c (MIT-style
// license; see piglit's COPYING). A display list sets GL_FLAT, draws a quad,
// redundantly sets GL_FLAT again, and draws a second quad; the list is then
// replayed while GL_SMOOTH is the live state. Both quads must come out flat
// shaded in their provoking vertex's color.
//
// Two things about this wrapper make it worth pinning:
//
//   - The shade model recorded in the list has to win over the live one, and
//     has to still apply after glEndList bakes the Begin/End runs into
//     compiled draws (sfpewCompileImmediateRuns). The redundant second
//     glShadeModel sits between two otherwise-mergeable runs, which is
//     exactly the arrangement upstream wrote it for.
//
//   - GL_QUADS has no GLES equivalent, so the wrapper rewrites each quad
//     into two triangles. Flat shading takes its color from the LAST vertex
//     of each primitive, so the rewrite has to preserve which vertex that
//     is: desktop GL_QUADS takes vertex 3 (the fourth), while the naive
//     triangle split (0,1,2)+(2,3,0) ends at vertex 2 and then vertex 0.
//     Getting that wrong shades both halves with the wrong color and no
//     other test in the suite would notice.

#include "sfpew_gtest.h"

#include <tuple>

namespace {

using sfpew_test::ContextTest;
using sfpew_test::GLbitfield;
using sfpew_test::GLenum;
using sfpew_test::GLfloat;
using sfpew_test::GLint;
using sfpew_test::GLsizei;
using sfpew_test::GLuint;
using sfpew_test::PixelProbe;

constexpr int kWindow = 64;
constexpr GLenum GL_QUADS_ = 0x0007;
constexpr GLbitfield GL_COLOR_BUFFER_BIT_ = 0x00004000;
constexpr GLenum GL_COMPILE_ = 0x1300;
constexpr GLenum GL_FLAT_ = 0x1D00;
constexpr GLenum GL_SMOOTH_ = 0x1D01;
constexpr GLenum GL_NO_ERROR_ = 0;

constexpr GLfloat kRed[] = {1.0f, 0.0f, 0.0f};
constexpr GLfloat kGreen[] = {0.0f, 1.0f, 0.0f};

using DlistShadeModelTest = ContextTest;

TEST_F(DlistShadeModelTest, GlFlatRecordedInAListSurvivesCompilation) {
    auto vertex2f = Get<void (*)(GLfloat, GLfloat)>("glVertex2f");
    auto begin = Get<void (*)(GLenum)>("glBegin");
    auto end = Get<void (*)()>("glEnd");
    auto color3fv = Get<void (*)(const GLfloat*)>("glColor3fv");
    auto shade_model = Get<void (*)(GLenum)>("glShadeModel");
    auto clear_color = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glClearColor");
    auto clear = Get<void (*)(GLbitfield)>("glClear");
    auto finish = Get<void (*)()>("glFinish");
    auto gen_lists = Get<GLuint (*)(GLsizei)>("glGenLists");
    auto new_list = Get<void (*)(GLuint, GLenum)>("glNewList");
    auto end_list = Get<void (*)()>("glEndList");
    auto call_list = Get<void (*)(GLuint)>("glCallList");
    auto delete_lists = Get<void (*)(GLuint, GLsizei)>("glDeleteLists");
    auto get_error = Get<GLenum (*)()>("glGetError");
    auto read_pixels =
        Get<void (*)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*)>("glReadPixels");
    ASSERT_NE(read_pixels, nullptr);
    PixelProbe probe(read_pixels);

    clear_color(0, 0, 0, 0);
    get_error();

    const GLuint list = gen_lists(1);
    new_list(list, GL_COMPILE_);
    shade_model(GL_FLAT_);

    // Left half. Vertices alternate red/green; the LAST one is green, so a
    // correctly flat-shaded quad is entirely green.
    begin(GL_QUADS_);
    color3fv(kRed);
    vertex2f(-1, -1);
    color3fv(kGreen);
    vertex2f(0, -1);
    color3fv(kRed);
    vertex2f(0, 1);
    color3fv(kGreen);
    vertex2f(-1, 1);
    end();

    // Redundant, and deliberately so: it sits between two runs the wrapper
    // would otherwise merge into one draw.
    shade_model(GL_FLAT_);

    // Right half, same color pattern.
    begin(GL_QUADS_);
    color3fv(kRed);
    vertex2f(0, -1);
    color3fv(kGreen);
    vertex2f(1, -1);
    color3fv(kRed);
    vertex2f(1, 1);
    color3fv(kGreen);
    vertex2f(0, 1);
    end();
    end_list();

    // The live state is SMOOTH; the list's own GL_FLAT must win.
    shade_model(GL_SMOOTH_);
    clear(GL_COLOR_BUFFER_BIT_);
    call_list(list);
    finish();

    for (const auto& [x, y, what] : {
             std::tuple{16, 16, "left quad flat-shaded green"},
             std::tuple{48, 16, "right quad flat-shaded green"},
             std::tuple{16, 48, "left quad, upper area"},
             std::tuple{48, 48, "right quad, upper area"},
         }) {
        const PixelProbe::Rgba p = probe.At(x, y);
        EXPECT_TRUE(p.g > 150 && p.r < 100)
            << what << " at (" << x << ", " << y << "): rgb=(" << (int)p.r << ", " << (int)p.g
            << ", " << (int)p.b << "), want flat green";
    }

    delete_lists(list, 1);
    EXPECT_EQ(get_error(), GL_NO_ERROR_);
}

} // namespace
