// SimpleFPEWrapper - tests/gtest_bgra_color_array.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// glColorPointer(GL_BGRA, ...) is GL 3.2 / ARB_vertex_array_bgra: four
// normalized unsigned bytes whose ORDER is blue, green, red, alpha. It is a
// component order, not a component count - and 0x80E1 read as a count is what
// the wrapper used to store, which put a nonsense stride on every following
// calculation.
//
// GLES has no such format, so the wrapper hands the driver a plain
// four-component array and undoes the order in the generated shader. A
// desktop GL backend takes GL_BGRA itself. Either way the drawn colour must
// be the one the application described, which is what this checks: the same
// bytes are drawn twice, once declared BGRA and once RGBA, and the two must
// come out as each other's channel swap.
//
// The probe colours keep R == B out of the picture on purpose - a swap has to
// be visible - so this test would fail on a driver that swaps R and B behind
// our back. That is the point.

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
using sfpew_test::PixelProbe;

constexpr GLbitfield GL_COLOR_BUFFER_BIT_ = 0x00004000;
constexpr GLenum GL_TRIANGLES_ = 0x0004;
constexpr GLenum GL_FLOAT_ = 0x1406;
constexpr GLenum GL_BGRA_ = 0x80E1;
constexpr GLenum GL_UNSIGNED_BYTE_ = 0x1401;
constexpr GLenum GL_VERTEX_ARRAY_ = 0x8074;
constexpr GLenum GL_COLOR_ARRAY_ = 0x8076;
constexpr GLenum GL_NO_ERROR_ = 0;

using BgraColorArrayTest = ContextTest;

TEST_F(BgraColorArrayTest, ComponentOrderSurvivesTheDraw) {
    auto clear_color = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glClearColor");
    auto clear = Get<void (*)(GLbitfield)>("glClear");
    auto draw_arrays = Get<void (*)(GLenum, GLint, GLsizei)>("glDrawArrays");
    auto get_error = Get<GLenum (*)()>("glGetError");
    auto finish = Get<void (*)()>("glFinish");
    auto enable_client_state = Get<void (*)(GLenum)>("glEnableClientState");
    auto disable_client_state = Get<void (*)(GLenum)>("glDisableClientState");
    auto vertex_pointer = Get<void (*)(GLint, GLenum, GLsizei, const void*)>("glVertexPointer");
    auto color_pointer = Get<void (*)(GLint, GLenum, GLsizei, const void*)>("glColorPointer");
    auto read_pixels =
        Get<void (*)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*)>("glReadPixels");
    ASSERT_NE(read_pixels, nullptr);
    PixelProbe probe(read_pixels);

    static const GLfloat pos[] = {-1, -1, 1, -1, 1, 1, -1, -1, 1, 1, -1, 1};
    // One byte quadruple per vertex. Read as B,G,R,A this is red; read as
    // R,G,B,A it is blue. Alpha stays opaque either way.
    static const GLubyte packed[] = {
        0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255,
        0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255,
    };

    vertex_pointer(2, GL_FLOAT_, 0, pos);
    enable_client_state(GL_VERTEX_ARRAY_);
    enable_client_state(GL_COLOR_ARRAY_);
    clear_color(0.0f, 0.0f, 0.0f, 1.0f);

    color_pointer(GL_BGRA_, GL_UNSIGNED_BYTE_, 0, packed);
    clear(GL_COLOR_BUFFER_BIT_);
    draw_arrays(GL_TRIANGLES_, 0, 6);
    finish();
    const PixelProbe::Rgba p1 = probe.At(32, 32);
    EXPECT_TRUE(p1.r > 200 && p1.g <= 50 && p1.b <= 50)
        << "GL_BGRA colour array must reach the shader in B,G,R,A order: (" << (int)p1.r << ','
        << (int)p1.g << ',' << (int)p1.b << ')';

    // The same bytes declared the ordinary way must come out the other way
    // round, which is what makes the check above about the ORDER and not
    // about the wrapper drawing some fixed colour.
    color_pointer(4, GL_UNSIGNED_BYTE_, 0, packed);
    clear(GL_COLOR_BUFFER_BIT_);
    draw_arrays(GL_TRIANGLES_, 0, 6);
    finish();
    const PixelProbe::Rgba p2 = probe.At(32, 32);
    EXPECT_TRUE(p2.r <= 50 && p2.g <= 50 && p2.b > 200)
        << "the same bytes as RGBA must draw the swapped colour: (" << (int)p2.r << ',' << (int)p2.g
        << ',' << (int)p2.b << ')';

    // Back to BGRA: the two must not share a generated program, and the
    // switch back has to take effect.
    color_pointer(GL_BGRA_, GL_UNSIGNED_BYTE_, 0, packed);
    clear(GL_COLOR_BUFFER_BIT_);
    draw_arrays(GL_TRIANGLES_, 0, 6);
    finish();
    const PixelProbe::Rgba p3 = probe.At(32, 32);
    EXPECT_TRUE(p3.r > 200 && p3.g <= 50 && p3.b <= 50)
        << "switching back to GL_BGRA must take effect: (" << (int)p3.r << ',' << (int)p3.g << ','
        << (int)p3.b << ')';

    disable_client_state(GL_COLOR_ARRAY_);
    disable_client_state(GL_VERTEX_ARRAY_);
    EXPECT_EQ(get_error(), GL_NO_ERROR_);
}

} // namespace
