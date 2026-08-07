// SimpleFPEWrapper - tests/gtest_binding_query_barrier.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// glGetIntegerv must never hand the application one of the wrapper's own
// object names. A fixed-function draw leaves the backend holding the
// wrapper's program, VAO, array buffer and element buffer until the next
// entry barrier (plans/12), so a query that forwards straight to the backend
// in that window reports fpe_vao / fpe_element_ring as if the app had bound
// them - the app then "restores" a wrapper object, or worse, believes its own
// buffer is still bound when it is not.
//
// GL_CURRENT_PROGRAM and GL_ARRAY_BUFFER_BINDING were already answered from
// the wrapper's logical shadows; GL_VERTEX_ARRAY_BINDING and
// GL_ELEMENT_ARRAY_BUFFER_BINDING fell through (plans/16 M7, reproduced as
// "0 before the draw, 4 after").
//
// The draw here is a client-array GL_QUADS glDrawArrays: quads are what makes
// the wrapper bind its own element ring, and glDrawArrays with program 0
// deliberately does NOT hand the app's state back, so the save is still held
// when the queries below run. Nothing between the draw and the queries may
// take an entry barrier, or the bug hides.

#include "sfpew_gtest.h"

namespace {

using sfpew_test::ContextTest;
using sfpew_test::GLbitfield;
using sfpew_test::GLenum;
using sfpew_test::GLfloat;
using sfpew_test::GLint;
using sfpew_test::GLsizei;
using sfpew_test::GLsizeiptr;
using sfpew_test::GLuint;
using sfpew_test::GLushort;

constexpr GLbitfield GL_COLOR_BUFFER_BIT_ = 0x00004000;
constexpr GLenum GL_QUADS_ = 0x0007;
constexpr GLenum GL_FLOAT_ = 0x1406;
constexpr GLenum GL_VERTEX_ARRAY_ = 0x8074;
constexpr GLenum GL_ELEMENT_ARRAY_BUFFER_ = 0x8893;
constexpr GLenum GL_STATIC_DRAW_ = 0x88E4;
constexpr GLenum GL_VERTEX_ARRAY_BINDING_ = 0x85B5;
constexpr GLenum GL_ELEMENT_ARRAY_BUFFER_BINDING_ = 0x8895;
constexpr GLenum GL_NO_ERROR_ = 0;

const GLfloat kQuad[] = {-0.5f, -0.5f, 0.5f, -0.5f, 0.5f, 0.5f, -0.5f, 0.5f};
const GLushort kIndices[] = {0, 1, 2, 0, 2, 3};

using BindingQueryBarrierTest = ContextTest;

TEST_F(BindingQueryBarrierTest, BindingQueriesNeverReportWrapperObjectsOnVao0) {
    auto clear_color = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glClearColor");
    auto clear = Get<void (*)(GLbitfield)>("glClear");
    auto get_error = Get<GLenum (*)()>("glGetError");
    auto get_integerv = Get<void (*)(GLenum, GLint*)>("glGetIntegerv");
    auto get_floatv = Get<void (*)(GLenum, GLfloat*)>("glGetFloatv");
    auto gen_buffers = Get<void (*)(GLsizei, GLuint*)>("glGenBuffers");
    auto bind_buffer = Get<void (*)(GLenum, GLuint)>("glBindBuffer");
    auto buffer_data = Get<void (*)(GLenum, GLsizeiptr, const void*, GLenum)>("glBufferData");
    auto enable_client_state = Get<void (*)(GLenum)>("glEnableClientState");
    auto disable_client_state = Get<void (*)(GLenum)>("glDisableClientState");
    auto vertex_pointer = Get<void (*)(GLint, GLenum, GLsizei, const void*)>("glVertexPointer");
    auto color4f = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glColor4f");
    auto draw_arrays = Get<void (*)(GLenum, GLint, GLsizei)>("glDrawArrays");
    ASSERT_NE(draw_arrays, nullptr);

    GLuint ibo = 0;
    gen_buffers(1, &ibo);
    bind_buffer(GL_ELEMENT_ARRAY_BUFFER_, ibo);
    buffer_data(GL_ELEMENT_ARRAY_BUFFER_, static_cast<GLsizeiptr>(sizeof kIndices), kIndices,
                GL_STATIC_DRAW_);

    clear_color(0.0f, 0.0f, 0.0f, 1.0f);
    clear(GL_COLOR_BUFFER_BIT_);

    GLint vao_before = -1, element_before = -1;
    get_integerv(GL_VERTEX_ARRAY_BINDING_, &vao_before);
    get_integerv(GL_ELEMENT_ARRAY_BUFFER_BINDING_, &element_before);
    EXPECT_EQ(vao_before, 0) << "no VAO was ever bound";
    EXPECT_EQ(element_before, static_cast<GLint>(ibo)) << "the app's element buffer";

    color4f(0.0f, 1.0f, 0.0f, 1.0f);
    enable_client_state(GL_VERTEX_ARRAY_);
    vertex_pointer(2, GL_FLOAT_, 0, kQuad);
    draw_arrays(GL_QUADS_, 0, 4);

    // Deliberately no glFinish/glFlush/glGetError here: either would take the
    // entry barrier and restore the app's state before the queries run.
    GLint vao_after = -1, element_after = -1;
    get_integerv(GL_VERTEX_ARRAY_BINDING_, &vao_after);
    EXPECT_EQ(vao_after, 0)
        << "GL_VERTEX_ARRAY_BINDING reported " << vao_after
        << " while a fixed-function draw held the app's state - that is the wrapper's fpe_vao";
    get_integerv(GL_ELEMENT_ARRAY_BUFFER_BINDING_, &element_after);
    EXPECT_EQ(element_after, static_cast<GLint>(ibo))
        << "GL_ELEMENT_ARRAY_BUFFER_BINDING reported " << element_after
        << " instead of the app's buffer " << ibo << " - that is the wrapper's element ring";

    // The other three getters must agree with glGetIntegerv about the same
    // binding, which is the invariant ffp_state_query.cpp is built around.
    GLfloat vao_as_float = -1.0f;
    get_floatv(GL_VERTEX_ARRAY_BINDING_, &vao_as_float);
    EXPECT_EQ(vao_as_float, 0.0f) << "glGetFloatv disagreed with glGetIntegerv";

    disable_client_state(GL_VERTEX_ARRAY_);
    bind_buffer(GL_ELEMENT_ARRAY_BUFFER_, 0);
    EXPECT_EQ(get_error(), GL_NO_ERROR_);
}

TEST_F(BindingQueryBarrierTest, BindingQueriesReportTheAppsOwnVaoAfterAFixedFunctionDraw) {
    auto clear_color = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glClearColor");
    auto clear = Get<void (*)(GLbitfield)>("glClear");
    auto get_error = Get<GLenum (*)()>("glGetError");
    auto get_integerv = Get<void (*)(GLenum, GLint*)>("glGetIntegerv");
    auto gen_buffers = Get<void (*)(GLsizei, GLuint*)>("glGenBuffers");
    auto bind_buffer = Get<void (*)(GLenum, GLuint)>("glBindBuffer");
    auto buffer_data = Get<void (*)(GLenum, GLsizeiptr, const void*, GLenum)>("glBufferData");
    auto gen_vertex_arrays = Get<void (*)(GLsizei, GLuint*)>("glGenVertexArrays");
    auto bind_vertex_array = Get<void (*)(GLuint)>("glBindVertexArray");
    auto delete_vertex_arrays = Get<void (*)(GLsizei, const GLuint*)>("glDeleteVertexArrays");
    auto enable_client_state = Get<void (*)(GLenum)>("glEnableClientState");
    auto disable_client_state = Get<void (*)(GLenum)>("glDisableClientState");
    auto vertex_pointer = Get<void (*)(GLint, GLenum, GLsizei, const void*)>("glVertexPointer");
    auto color4f = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glColor4f");
    auto draw_arrays = Get<void (*)(GLenum, GLint, GLsizei)>("glDrawArrays");
    ASSERT_NE(gen_vertex_arrays, nullptr);

    GLuint vao = 0;
    gen_vertex_arrays(1, &vao);
    ASSERT_NE(vao, 0u);
    bind_vertex_array(vao);

    GLuint ibo = 0;
    gen_buffers(1, &ibo);
    bind_buffer(GL_ELEMENT_ARRAY_BUFFER_, ibo);
    buffer_data(GL_ELEMENT_ARRAY_BUFFER_, static_cast<GLsizeiptr>(sizeof kIndices), kIndices,
                GL_STATIC_DRAW_);

    clear_color(0.0f, 0.0f, 0.0f, 1.0f);
    clear(GL_COLOR_BUFFER_BIT_);

    color4f(0.0f, 1.0f, 0.0f, 1.0f);
    enable_client_state(GL_VERTEX_ARRAY_);
    vertex_pointer(2, GL_FLOAT_, 0, kQuad);
    draw_arrays(GL_QUADS_, 0, 4);

    GLint vao_after = -1, element_after = -1;
    get_integerv(GL_VERTEX_ARRAY_BINDING_, &vao_after);
    EXPECT_EQ(vao_after, static_cast<GLint>(vao))
        << "GL_VERTEX_ARRAY_BINDING reported " << vao_after << " instead of the app's VAO " << vao;
    get_integerv(GL_ELEMENT_ARRAY_BUFFER_BINDING_, &element_after);
    EXPECT_EQ(element_after, static_cast<GLint>(ibo))
        << "GL_ELEMENT_ARRAY_BUFFER_BINDING reported " << element_after
        << " instead of the app's buffer " << ibo;

    disable_client_state(GL_VERTEX_ARRAY_);
    bind_buffer(GL_ELEMENT_ARRAY_BUFFER_, 0);
    bind_vertex_array(0);
    delete_vertex_arrays(1, &vao);
    EXPECT_EQ(get_error(), GL_NO_ERROR_);
}

} // namespace
