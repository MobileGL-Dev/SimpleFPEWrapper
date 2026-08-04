// SimpleFPEWrapper - tests/gtest_selection_vbo.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// Selection is a CPU transform path, but legacy applications still source
// picking geometry from every vertex-array shape used for ordinary rendering.
// These cases pin the two sources that used to be skipped: VBO offsets and
// non-float components. The indexed case also keeps its indices in an EBO so
// both buffer targets must be read without leaking backend binding changes.

#include "sfpew_gtest.h"

#include <array>
#include <cstdint>

namespace {

using sfpew_test::ContextTest;
using sfpew_test::GLenum;
using sfpew_test::GLint;
using sfpew_test::GLsizei;
using sfpew_test::GLsizeiptr;
using sfpew_test::GLuint;
using sfpew_test::GLubyte;

using GLshort = std::int16_t;

constexpr GLenum GL_POINTS_ = 0x0000;
constexpr GLenum GL_DOUBLE_ = 0x140A;
constexpr GLenum GL_SHORT_ = 0x1402;
constexpr GLenum GL_UNSIGNED_BYTE_ = 0x1401;
constexpr GLenum GL_RENDER_ = 0x1C00;
constexpr GLenum GL_SELECT_ = 0x1C02;
constexpr GLenum GL_ARRAY_BUFFER_ = 0x8892;
constexpr GLenum GL_ELEMENT_ARRAY_BUFFER_ = 0x8893;
constexpr GLenum GL_ARRAY_BUFFER_BINDING_ = 0x8894;
constexpr GLenum GL_ELEMENT_ARRAY_BUFFER_BINDING_ = 0x8895;
constexpr GLenum GL_STATIC_DRAW_ = 0x88E4;
constexpr GLenum GL_VERTEX_ARRAY_ = 0x8074;
constexpr GLenum GL_NO_ERROR_ = 0;

class SelectionVboTest : public ContextTest {
protected:
    void SetUp() override {
        ContextTest::SetUp();
        if (::testing::Test::IsSkipped() || ::testing::Test::HasFatalFailure()) return;

        select_buffer_ = Get<void (*)(GLsizei, GLuint*)>("glSelectBuffer");
        render_mode_ = Get<GLint (*)(GLenum)>("glRenderMode");
        init_names_ = Get<void (*)()>("glInitNames");
        push_name_ = Get<void (*)(GLuint)>("glPushName");
        gen_buffers_ = Get<void (*)(GLsizei, GLuint*)>("glGenBuffers");
        bind_buffer_ = Get<void (*)(GLenum, GLuint)>("glBindBuffer");
        buffer_data_ = Get<void (*)(GLenum, GLsizeiptr, const void*, GLenum)>("glBufferData");
        vertex_pointer_ =
            Get<void (*)(GLint, GLenum, GLsizei, const void*)>("glVertexPointer");
        enable_client_state_ = Get<void (*)(GLenum)>("glEnableClientState");
        draw_arrays_ = Get<void (*)(GLenum, GLint, GLsizei)>("glDrawArrays");
        draw_elements_ =
            Get<void (*)(GLenum, GLsizei, GLenum, const void*)>("glDrawElements");
        get_integerv_ = Get<void (*)(GLenum, GLint*)>("glGetIntegerv");
        get_error_ = Get<GLenum (*)()>("glGetError");
        get_error_();
    }

    void BeginSelection(GLuint name) {
        selection_.fill(0xDEADBEEFu);
        select_buffer_(static_cast<GLsizei>(selection_.size()), selection_.data());
        ASSERT_EQ(render_mode_(GL_SELECT_), 0);
        init_names_();
        push_name_(name);
    }

    void ExpectSingleHit(GLuint name) {
        ASSERT_EQ(render_mode_(GL_RENDER_), 1);
        EXPECT_EQ(selection_[0], 1u);
        EXPECT_LE(selection_[1], selection_[2]);
        EXPECT_EQ(selection_[3], name);
    }

    void ExpectNoHit() { EXPECT_EQ(render_mode_(GL_RENDER_), 0); }

    std::array<GLuint, 16> selection_{};
    void (*select_buffer_)(GLsizei, GLuint*) = nullptr;
    GLint (*render_mode_)(GLenum) = nullptr;
    void (*init_names_)() = nullptr;
    void (*push_name_)(GLuint) = nullptr;
    void (*gen_buffers_)(GLsizei, GLuint*) = nullptr;
    void (*bind_buffer_)(GLenum, GLuint) = nullptr;
    void (*buffer_data_)(GLenum, GLsizeiptr, const void*, GLenum) = nullptr;
    void (*vertex_pointer_)(GLint, GLenum, GLsizei, const void*) = nullptr;
    void (*enable_client_state_)(GLenum) = nullptr;
    void (*draw_arrays_)(GLenum, GLint, GLsizei) = nullptr;
    void (*draw_elements_)(GLenum, GLsizei, GLenum, const void*) = nullptr;
    void (*get_integerv_)(GLenum, GLint*) = nullptr;
    GLenum (*get_error_)() = nullptr;
};

TEST_F(SelectionVboTest, DrawArraysReadsShortPositionsFromVbo) {
    constexpr GLshort positions[] = {2, 2, 0, 0};
    GLuint vbo = 0;
    gen_buffers_(1, &vbo);
    bind_buffer_(GL_ARRAY_BUFFER_, vbo);
    buffer_data_(GL_ARRAY_BUFFER_, static_cast<GLsizeiptr>(sizeof positions), positions,
                 GL_STATIC_DRAW_);
    vertex_pointer_(2, GL_SHORT_, 0, nullptr);
    enable_client_state_(GL_VERTEX_ARRAY_);

    BeginSelection(17);
    draw_arrays_(GL_POINTS_, 1, 1);
    ExpectSingleHit(17);

    GLint binding = 0;
    get_integerv_(GL_ARRAY_BUFFER_BINDING_, &binding);
    EXPECT_EQ(binding, static_cast<GLint>(vbo));
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

TEST_F(SelectionVboTest, DrawArraysConvertsClientDoublePositions) {
    constexpr double positions[] = {2.0, 2.0, 0.0, 0.0};
    bind_buffer_(GL_ARRAY_BUFFER_, 0);
    vertex_pointer_(2, GL_DOUBLE_, 0, positions);
    enable_client_state_(GL_VERTEX_ARRAY_);

    BeginSelection(23);
    draw_arrays_(GL_POINTS_, 1, 1);
    ExpectSingleHit(23);
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

TEST_F(SelectionVboTest, ElementBufferOffsetSelectsOnlyReferencedVboVertex) {
    constexpr GLshort positions[] = {0, 0, 2, 2};
    constexpr GLubyte indices[] = {1, 0};
    GLuint buffers[2] = {};
    gen_buffers_(2, buffers);

    bind_buffer_(GL_ARRAY_BUFFER_, buffers[0]);
    buffer_data_(GL_ARRAY_BUFFER_, static_cast<GLsizeiptr>(sizeof positions), positions,
                 GL_STATIC_DRAW_);
    vertex_pointer_(2, GL_SHORT_, 0, nullptr);
    enable_client_state_(GL_VERTEX_ARRAY_);

    bind_buffer_(GL_ELEMENT_ARRAY_BUFFER_, buffers[1]);
    buffer_data_(GL_ELEMENT_ARRAY_BUFFER_, static_cast<GLsizeiptr>(sizeof indices), indices,
                 GL_STATIC_DRAW_);

    // Index 1 is outside the clip volume even though unreferenced vertex 0 is
    // inside. Scanning the whole source range would report a false hit.
    BeginSelection(31);
    draw_elements_(GL_POINTS_, 1, GL_UNSIGNED_BYTE_, nullptr);
    ExpectNoHit();

    // Offset one byte selects index 0, proving the EBO offset is honored.
    BeginSelection(37);
    draw_elements_(GL_POINTS_, 1, GL_UNSIGNED_BYTE_, reinterpret_cast<const void*>(1));
    ExpectSingleHit(37);

    GLint array_binding = 0, element_binding = 0;
    get_integerv_(GL_ARRAY_BUFFER_BINDING_, &array_binding);
    get_integerv_(GL_ELEMENT_ARRAY_BUFFER_BINDING_, &element_binding);
    EXPECT_EQ(array_binding, static_cast<GLint>(buffers[0]));
    EXPECT_EQ(element_binding, static_cast<GLint>(buffers[1]));
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

} // namespace
