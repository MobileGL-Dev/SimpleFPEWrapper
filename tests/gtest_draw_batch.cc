// SimpleFPEWrapper - tests/gtest_draw_batch.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// Ported from piglit's tests/general/draw-batch.c (MIT-style license; see
// piglit's COPYING). It draws the SAME four colored triangles four times
// over, once through each of the draw paths an OpenGL 1.x application has -
// glDrawElements, glDrawArrays, glBegin/glEnd and glCallList - putting each
// pass on its own row via glTranslatef, then probes all sixteen cells. Every
// row has to come out identical.
//
// That makes it a cross-check no single-path test can be: the four paths are
// separate code in this wrapper (drawElementsNow, drawArraysNow,
// drawImmediateVertices, and the display-list replay commands), they convert
// legacy primitives independently, and since the deferred draw-state work
// they also each decide for themselves when to hand the app's
// program/VAO/buffer bindings back. A path that leaks state into the next
// one, or that applies the modelview at the wrong time, shows up here as one
// row disagreeing with the other three.
//
// Interleaved between the passes are the state changes upstream uses to
// provoke exactly that: a modelview translate, a client-array
// enable/disable, and color changes.
//
// NOT ported: upstream also enables GL_COLOR_SUM and feeds a secondary
// color through glSecondaryColorPointer/glSecondaryColor3fv, expecting it
// summed into the fragment color. This wrapper tracks secondary color but
// never sums it (no GL_COLOR_SUM support in the generated shaders at all),
// so that part is omitted rather than xfailed - dropping it keeps the
// four-path equivalence check, which is the part that exercises this
// wrapper, fully enforced.

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
using sfpew_test::GLuint;
using sfpew_test::GLushort;
using sfpew_test::PixelProbe;

constexpr int kWindow = 128;
constexpr GLenum GL_TRIANGLES_ = 0x0004;
constexpr GLbitfield GL_COLOR_BUFFER_BIT_ = 0x00004000;
constexpr GLenum GL_UNSIGNED_SHORT_ = 0x1403;
constexpr GLenum GL_FLOAT_ = 0x1406;
constexpr GLenum GL_PROJECTION_ = 0x1701;
constexpr GLenum GL_MODELVIEW_ = 0x1700;
constexpr GLenum GL_VERTEX_ARRAY_ = 0x8074;
constexpr GLenum GL_COLOR_ARRAY_ = 0x8076;
constexpr GLenum GL_COMPILE_ = 0x1300;
constexpr GLenum GL_NO_ERROR_ = 0;

class DrawBatchTest : public ContextTest {
protected:
    DrawBatchTest() : ContextTest(sfpew_test::Backend::GLES3, kWindow) {}
};

TEST_F(DrawBatchTest, AllFourDrawPathsAgreeAcrossInterleavedStateChanges) {
    auto vertex2fv = Get<void (*)(const GLfloat*)>("glVertex2fv");
    auto begin = Get<void (*)(GLenum)>("glBegin");
    auto end = Get<void (*)()>("glEnd");
    auto color3fv = Get<void (*)(const GLfloat*)>("glColor3fv");
    auto clear_color = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glClearColor");
    auto clear = Get<void (*)(GLbitfield)>("glClear");
    auto ortho = Get<void (*)(double, double, double, double, double, double)>("glOrtho");
    auto matrix_mode = Get<void (*)(GLenum)>("glMatrixMode");
    auto load_identity = Get<void (*)()>("glLoadIdentity");
    auto translatef = Get<void (*)(GLfloat, GLfloat, GLfloat)>("glTranslatef");
    auto finish = Get<void (*)()>("glFinish");
    auto vertex_pointer = Get<void (*)(GLint, GLenum, GLsizei, const void*)>("glVertexPointer");
    auto color_pointer = Get<void (*)(GLint, GLenum, GLsizei, const void*)>("glColorPointer");
    auto enable_client_state = Get<void (*)(GLenum)>("glEnableClientState");
    auto disable_client_state = Get<void (*)(GLenum)>("glDisableClientState");
    auto draw_arrays = Get<void (*)(GLenum, GLint, GLsizei)>("glDrawArrays");
    auto draw_elements = Get<void (*)(GLenum, GLsizei, GLenum, const void*)>("glDrawElements");
    auto gen_lists = Get<GLuint (*)(GLsizei)>("glGenLists");
    auto new_list = Get<void (*)(GLuint, GLenum)>("glNewList");
    auto end_list = Get<void (*)()>("glEndList");
    auto call_list = Get<void (*)(GLuint)>("glCallList");
    auto get_error = Get<GLenum (*)()>("glGetError");
    auto read_pixels =
        Get<void (*)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*)>("glReadPixels");
    ASSERT_NE(read_pixels, nullptr);
    PixelProbe probe(read_pixels);

    // Four right triangles, one per column, interleaved as {x, y, r, g, b}
    // exactly like upstream's array layout.
    static GLfloat array[] = {
        10, 10, 1, 0, 0, 27, 10, 1, 0, 0, 10, 30, 1, 0, 0,
        30, 10, 0, 1, 0, 47, 10, 0, 1, 0, 30, 30, 0, 1, 0,
        50, 10, 0, 0, 1, 67, 10, 0, 0, 1, 50, 30, 0, 0, 1,
        70, 10, 1, 0, 1, 87, 10, 1, 0, 1, 70, 30, 1, 0, 1,
    };
    static const GLushort indices[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
    // Expected color of each column, as 0/1 per channel.
    static const int expect[4][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {1, 0, 1}};

    matrix_mode(GL_PROJECTION_);
    load_identity();
    ortho(0, kWindow, 0, kWindow, -1, 1);
    matrix_mode(GL_MODELVIEW_);
    load_identity();
    clear_color(0, 0, 0, 0);
    get_error();

    // State change: declare the client arrays, then clear. The arrays must
    // survive the clear (upstream's comment: "The vertex array state should
    // be preserved after glClear").
    vertex_pointer(2, GL_FLOAT_, 20, array);
    color_pointer(3, GL_FLOAT_, 20, array + 2);
    enable_client_state(GL_VERTEX_ARRAY_);
    enable_client_state(GL_COLOR_ARRAY_);
    clear(GL_COLOR_BUFFER_BIT_);

    // Row 0: glDrawElements, one call per triangle.
    for (int i = 0; i < 4; ++i) draw_elements(GL_TRIANGLES_, 3, GL_UNSIGNED_SHORT_, indices + i * 3);

    // State change: modelview. Row 1: glDrawArrays.
    translatef(0, 30, 0);
    for (int i = 0; i < 4; ++i) draw_arrays(GL_TRIANGLES_, i * 3, 3);

    // State change: drop the client arrays entirely, so the immediate-mode
    // rows cannot accidentally source from them.
    disable_client_state(GL_VERTEX_ARRAY_);
    disable_client_state(GL_COLOR_ARRAY_);

    // Row 2: glBegin/glEnd.
    translatef(0, 30, 0);
    for (int i = 0; i < 4; ++i) {
        begin(GL_TRIANGLES_);
        for (int j = 0; j < 3; ++j) {
            color3fv(array + i * 15 + j * 5 + 2);
            vertex2fv(array + i * 15 + j * 5);
        }
        end();
    }

    // Row 3: the same Begin/End runs, compiled into display lists.
    translatef(0, 30, 0);
    const GLuint base = gen_lists(4);
    for (int i = 0; i < 4; ++i) {
        new_list(base + i, GL_COMPILE_);
        begin(GL_TRIANGLES_);
        for (int j = 0; j < 3; ++j) {
            color3fv(array + i * 15 + j * 5 + 2);
            vertex2fv(array + i * 15 + j * 5);
        }
        end();
        end_list();
    }
    for (int i = 0; i < 4; ++i) call_list(base + i);
    finish();

    static const char* row_names[4] = {"DrawElements", "DrawArrays", "Begin/End", "CallList"};
    static const int row_y[4] = {15, 45, 75, 105};
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            const int px = 15 + col * 20;
            const PixelProbe::Rgba p = probe.At(px, row_y[row]);
            const bool ok = (p.r > 150) == (expect[col][0] != 0) &&
                            (p.g > 150) == (expect[col][1] != 0) &&
                            (p.b > 150) == (expect[col][2] != 0);
            EXPECT_TRUE(ok) << row_names[row] << " row " << row << " col " << col << " at ("
                            << px << ',' << row_y[row] << "): rgb=(" << (int)p.r << ','
                            << (int)p.g << ',' << (int)p.b << ") want=("
                            << expect[col][0] * 255 << ',' << expect[col][1] * 255 << ','
                            << expect[col][2] * 255 << ')';
        }
    }
    EXPECT_EQ(get_error(), GL_NO_ERROR_);
}

} // namespace
