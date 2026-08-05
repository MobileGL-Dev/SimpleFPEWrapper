// SimpleFPEWrapper - tests/gtest_client_state_validation.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// glEnableClientState/glDisableClientState resolve their cap through
// vp_mask(), which returns 0 for anything it does not recognize. Both entry
// points used that 0 with no fallback - `enabled_pointers |= 0` and
// `enabled_pointers &= ~0` are themselves harmless no-ops, but the calls
// still silently swallowed an illegal enum instead of raising
// GL_INVALID_ENUM (plans/13 13.6), unlike glFogCoordPointer's unrecognized
// type (vertexpointer.cpp:457) or glInterleavedArrays' unrecognized format
// (vertexpointer.cpp:594) in the same file.

#include "sfpew_gtest.h"

namespace {

using sfpew_test::ContextTest;
using sfpew_test::GLbitfield;
using sfpew_test::GLenum;
using sfpew_test::GLfloat;
using sfpew_test::GLint;
using sfpew_test::GLsizei;
using sfpew_test::PixelProbe;

constexpr GLenum GL_NO_ERROR_ = 0;
constexpr GLenum GL_INVALID_ENUM_ = 0x0500;
constexpr GLbitfield GL_COLOR_BUFFER_BIT_ = 0x00004000;
constexpr GLenum GL_TRIANGLES_ = 0x0004;
constexpr GLenum GL_FLOAT_ = 0x1406;
constexpr GLenum GL_VERTEX_ARRAY_ = 0x8074;

using ClientStateValidationTest = ContextTest;

TEST_F(ClientStateValidationTest, UnrecognizedCapSetsInvalidEnumAndLeavesStateUsable) {
    auto enable_client_state = Get<void (*)(GLenum)>("glEnableClientState");
    auto disable_client_state = Get<void (*)(GLenum)>("glDisableClientState");
    auto get_error = Get<GLenum (*)()>("glGetError");
    auto clear_color = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glClearColor");
    auto clear = Get<void (*)(GLbitfield)>("glClear");
    auto draw_arrays = Get<void (*)(GLenum, GLint, GLsizei)>("glDrawArrays");
    auto vertex_pointer = Get<void (*)(GLint, GLenum, GLsizei, const void*)>("glVertexPointer");
    auto finish = Get<void (*)()>("glFinish");
    auto read_pixels =
        Get<void (*)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*)>("glReadPixels");
    ASSERT_NE(read_pixels, nullptr);
    PixelProbe probe(read_pixels);

    ASSERT_EQ(get_error(), GL_NO_ERROR_) << "residual error before the test began";

    constexpr GLenum kBogusCap = 0x9999;
    enable_client_state(kBogusCap);
    EXPECT_EQ(get_error(), GL_INVALID_ENUM_) << "glEnableClientState(unrecognized cap)";
    disable_client_state(kBogusCap);
    EXPECT_EQ(get_error(), GL_INVALID_ENUM_) << "glDisableClientState(unrecognized cap)";
    EXPECT_EQ(get_error(), GL_NO_ERROR_) << "only one error should have been latched per call";

    // enabled_pointers was never touched by the rejected calls (vp_mask's 0
    // makes both bitwise ops no-ops regardless): a legitimate enable right
    // after behaves exactly as if the bogus calls had never happened.
    static const GLfloat quad[] = {-1, -1, 1, -1, 1, 1, -1, -1, 1, 1, -1, 1};
    vertex_pointer(2, GL_FLOAT_, 0, quad);
    enable_client_state(GL_VERTEX_ARRAY_);
    EXPECT_EQ(get_error(), GL_NO_ERROR_) << "glEnableClientState(GL_VERTEX_ARRAY)";

    clear_color(0.0f, 0.0f, 1.0f, 1.0f);
    clear(GL_COLOR_BUFFER_BIT_);
    draw_arrays(GL_TRIANGLES_, 0, 6);
    finish();
    EXPECT_TRUE(probe.IsLitAt(32, 32)) << "GL_VERTEX_ARRAY still drives a normal draw";

    disable_client_state(GL_VERTEX_ARRAY_);
    EXPECT_EQ(get_error(), GL_NO_ERROR_) << "glDisableClientState(GL_VERTEX_ARRAY)";
}

} // namespace
