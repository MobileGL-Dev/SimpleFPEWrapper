// SimpleFPEWrapper - tests/gtest_current_attrib_cache.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// A fixed-function draw does not re-send a current color, normal or texture
// coordinate the backend already holds (glstate_t::backend_current_attribs).
// That is only safe if every write to a generic attribute's current value is
// seen - including the GL 3.0+ setters the wrapper forwards rather than
// emulates. Here the application overwrites every index with
// glVertexAttribI4ui between two draws with the same current color; the
// second draw must still come out in that color.

#include "sfpew_gtest.h"

namespace {

using sfpew_test::ContextTest;
using sfpew_test::GLbitfield;
using sfpew_test::GLenum;
using sfpew_test::GLfloat;
using sfpew_test::GLint;
using sfpew_test::GLsizei;
using sfpew_test::GLuint;
using sfpew_test::PixelProbe;

constexpr GLbitfield GL_COLOR_BUFFER_BIT_ = 0x00004000;
constexpr GLenum GL_TRIANGLES_ = 0x0004;
constexpr GLenum GL_FLOAT_ = 0x1406;
constexpr GLenum GL_VERTEX_ARRAY_ = 0x8074;
constexpr GLenum GL_MAX_VERTEX_ATTRIBS_ = 0x8869;
constexpr GLenum GL_NO_ERROR_ = 0;

using CurrentAttribCacheTest = ContextTest;

TEST_F(CurrentAttribCacheTest, IntegerSetterBetweenDrawsDoesNotLeaveAStaleColor) {
    auto attrib_i4ui = Get<void (*)(GLuint, GLuint, GLuint, GLuint, GLuint)>("glVertexAttribI4ui");
    if (attrib_i4ui == nullptr) GTEST_SKIP() << "backend has no glVertexAttribI4ui";
    auto clear = Get<void (*)(GLbitfield)>("glClear");
    auto clear_color = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glClearColor");
    auto color4f = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glColor4f");
    auto vertex_pointer = Get<void (*)(GLint, GLenum, GLsizei, const void*)>("glVertexPointer");
    auto enable_client_state = Get<void (*)(GLenum)>("glEnableClientState");
    auto draw_arrays = Get<void (*)(GLenum, GLint, GLsizei)>("glDrawArrays");
    auto get_integerv = Get<void (*)(GLenum, GLint*)>("glGetIntegerv");
    auto get_error = Get<GLenum (*)()>("glGetError");
    auto read_pixels = Get<void (*)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*)>("glReadPixels");
    PixelProbe probe(read_pixels);

    static const GLfloat quad[] = {-1, -1, 1, -1, 1, 1, -1, -1, 1, 1, -1, 1};
    vertex_pointer(2, GL_FLOAT_, 0, quad);
    enable_client_state(GL_VERTEX_ARRAY_);
    clear_color(0.0f, 0.0f, 0.0f, 1.0f);
    const auto draw_green = [&] {
        color4f(0.0f, 1.0f, 0.0f, 1.0f);
        clear(GL_COLOR_BUFFER_BIT_);
        draw_arrays(GL_TRIANGLES_, 0, 6);
        return probe.At(size() / 2, size() / 2);
    };

    const auto first = draw_green();
    ASSERT_GT(first.g, 200);
    ASSERT_LT(first.r, 60);

    GLint attribs = 0;
    get_integerv(GL_MAX_VERTEX_ATTRIBS_, &attribs);
    ASSERT_GT(attribs, 0);
    for (GLint index = 0; index < attribs; ++index) attrib_i4ui(static_cast<GLuint>(index), 0, 0, 0, 0);

    const auto second = draw_green();
    EXPECT_GT(second.g, 200) << "the unchanged current color was not re-sent after glVertexAttribI4ui overwrote it";
    EXPECT_LT(second.r, 60);
    EXPECT_EQ(get_error(), GL_NO_ERROR_);
}

} // namespace
