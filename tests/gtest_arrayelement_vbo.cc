// SimpleFPEWrapper - tests/gtest_arrayelement_vbo.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// glArrayElement dereferences the enabled client arrays at element i and
// feeds the values through the immediate-mode current-value path. GL 2.1
// section 2.8 makes no distinction between an array in client memory and one
// in a buffer object; the wrapper did, and skipped every buffer-backed array
// outright - an indexed immediate-mode primitive over VBO arrays produced no
// geometry at all (plans/16 M5).
//
// The same VBO is drawn twice, once with glDrawArrays and once with
// glBegin/glArrayElement/glEnd, and the two framebuffers must agree: that
// pins the fix to "reads the buffer" rather than "draws something", and the
// glDrawArrays render doubles as proof the VBO declaration itself is sound.
//
// Magenta and green are used because both survive an R/B swap unchanged
// (llvmpipe's BGRA readback quirk).

#include "sfpew_gtest.h"

#include <vector>

namespace {

using sfpew_test::ContextTest;
using sfpew_test::GLbitfield;
using sfpew_test::GLenum;
using sfpew_test::GLfloat;
using sfpew_test::GLint;
using sfpew_test::GLsizei;
using sfpew_test::GLsizeiptr;
using sfpew_test::GLubyte;
using sfpew_test::GLuint;

constexpr int kSize = 64;
constexpr GLbitfield GL_COLOR_BUFFER_BIT_ = 0x00004000;
constexpr GLenum GL_TRIANGLES_ = 0x0004;
constexpr GLenum GL_UNSIGNED_BYTE_ = 0x1401;
constexpr GLenum GL_FLOAT_ = 0x1406;
constexpr GLenum GL_RGBA_ = 0x1908;
constexpr GLenum GL_NO_ERROR_ = 0;
constexpr GLenum GL_VERTEX_ARRAY_ = 0x8074;
constexpr GLenum GL_COLOR_ARRAY_ = 0x8076;
constexpr GLenum GL_ARRAY_BUFFER_ = 0x8892;
constexpr GLenum GL_STATIC_DRAW_ = 0x88E4;

// Two triangles, each a flat color, so a dropped or mis-indexed element shows
// up as the wrong half being wrong rather than as a uniformly blank frame.
constexpr GLfloat kInterleaved[6][6] = {
    {-1.0f, -1.0f, 1.0f, 0.0f, 1.0f, 1.0f}, {0.0f, -1.0f, 1.0f, 0.0f, 1.0f, 1.0f},
    {-1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 1.0f},  {0.0f, -1.0f, 0.0f, 1.0f, 0.0f, 1.0f},
    {1.0f, -1.0f, 0.0f, 1.0f, 0.0f, 1.0f},  {1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f},
};

class ArrayElementVboTest : public ContextTest {
protected:
    ArrayElementVboTest() : ContextTest(sfpew_test::Backend::GLES3, kSize) {}

    void SetUp() override {
        ContextTest::SetUp();
        if (::testing::Test::IsSkipped()) return;

        clear_color_ = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glClearColor");
        clear_ = Get<void (*)(GLbitfield)>("glClear");
        finish_ = Get<void (*)()>("glFinish");
        get_error_ = Get<GLenum (*)()>("glGetError");
        begin_ = Get<void (*)(GLenum)>("glBegin");
        end_ = Get<void (*)()>("glEnd");
        array_element_ = Get<void (*)(GLint)>("glArrayElement");
        draw_arrays_ = Get<void (*)(GLenum, GLint, GLsizei)>("glDrawArrays");
        gen_buffers_ = Get<void (*)(GLsizei, GLuint*)>("glGenBuffers");
        delete_buffers_ = Get<void (*)(GLsizei, const GLuint*)>("glDeleteBuffers");
        bind_buffer_ = Get<void (*)(GLenum, GLuint)>("glBindBuffer");
        buffer_data_ = Get<void (*)(GLenum, GLsizeiptr, const void*, GLenum)>("glBufferData");
        enable_client_state_ = Get<void (*)(GLenum)>("glEnableClientState");
        disable_client_state_ = Get<void (*)(GLenum)>("glDisableClientState");
        vertex_pointer_ = Get<void (*)(GLint, GLenum, GLsizei, const void*)>("glVertexPointer");
        color_pointer_ = Get<void (*)(GLint, GLenum, GLsizei, const void*)>("glColorPointer");
        read_pixels_ =
            Get<void (*)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*)>("glReadPixels");
        ASSERT_NE(array_element_, nullptr);
        ASSERT_NE(read_pixels_, nullptr);

        gen_buffers_(1, &vbo_);
        ASSERT_NE(vbo_, 0u);
        bind_buffer_(GL_ARRAY_BUFFER_, vbo_);
        buffer_data_(GL_ARRAY_BUFFER_, static_cast<GLsizeiptr>(sizeof kInterleaved), kInterleaved,
                     GL_STATIC_DRAW_);

        const GLsizei stride = 6 * static_cast<GLsizei>(sizeof(GLfloat));
        vertex_pointer_(2, GL_FLOAT_, stride, nullptr);
        color_pointer_(4, GL_FLOAT_, stride,
                       reinterpret_cast<const void*>(2 * sizeof(GLfloat)));
        enable_client_state_(GL_VERTEX_ARRAY_);
        enable_client_state_(GL_COLOR_ARRAY_);
        clear_color_(0.0f, 0.0f, 0.0f, 1.0f);
    }

    void TearDown() override {
        if (delete_buffers_ != nullptr && vbo_ != 0) {
            disable_client_state_(GL_VERTEX_ARRAY_);
            disable_client_state_(GL_COLOR_ARRAY_);
            bind_buffer_(GL_ARRAY_BUFFER_, 0);
            delete_buffers_(1, &vbo_);
        }
        ContextTest::TearDown();
    }

    void Capture(std::vector<GLubyte>* out) {
        out->resize(static_cast<size_t>(kSize) * kSize * 4);
        finish_();
        read_pixels_(0, 0, kSize, kSize, GL_RGBA_, GL_UNSIGNED_BYTE_, out->data());
    }

    static bool IsColor(const std::vector<GLubyte>& fb, int x, int y, int r, int g, int b) {
        const size_t i = (static_cast<size_t>(y) * kSize + static_cast<size_t>(x)) * 4;
        const auto near = [](GLubyte got, int want) {
            return want > 128 ? got > 200 : got <= 50;
        };
        return near(fb[i], r) && near(fb[i + 1], g) && near(fb[i + 2], b);
    }

    void (*clear_color_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*clear_)(GLbitfield) = nullptr;
    void (*finish_)() = nullptr;
    GLenum (*get_error_)() = nullptr;
    void (*begin_)(GLenum) = nullptr;
    void (*end_)() = nullptr;
    void (*array_element_)(GLint) = nullptr;
    void (*draw_arrays_)(GLenum, GLint, GLsizei) = nullptr;
    void (*gen_buffers_)(GLsizei, GLuint*) = nullptr;
    void (*delete_buffers_)(GLsizei, const GLuint*) = nullptr;
    void (*bind_buffer_)(GLenum, GLuint) = nullptr;
    void (*buffer_data_)(GLenum, GLsizeiptr, const void*, GLenum) = nullptr;
    void (*enable_client_state_)(GLenum) = nullptr;
    void (*disable_client_state_)(GLenum) = nullptr;
    void (*vertex_pointer_)(GLint, GLenum, GLsizei, const void*) = nullptr;
    void (*color_pointer_)(GLint, GLenum, GLsizei, const void*) = nullptr;
    void (*read_pixels_)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*) = nullptr;
    GLuint vbo_ = 0;
};

TEST_F(ArrayElementVboTest, IndexedImmediateModeReadsBufferBackedArrays) {
    std::vector<GLubyte> from_draw_arrays, from_array_element;

    clear_(GL_COLOR_BUFFER_BIT_);
    draw_arrays_(GL_TRIANGLES_, 0, 6);
    Capture(&from_draw_arrays);
    ASSERT_TRUE(IsColor(from_draw_arrays, 16, 20, 255, 0, 255))
        << "the VBO declaration itself must draw a magenta left triangle";
    ASSERT_TRUE(IsColor(from_draw_arrays, 47, 20, 0, 255, 0))
        << "and a green right triangle";

    clear_(GL_COLOR_BUFFER_BIT_);
    begin_(GL_TRIANGLES_);
    for (GLint i = 0; i < 6; ++i) array_element_(i);
    end_();
    Capture(&from_array_element);

    EXPECT_TRUE(IsColor(from_array_element, 16, 20, 255, 0, 255))
        << "glArrayElement must read position and color out of the bound buffer";
    EXPECT_TRUE(IsColor(from_array_element, 47, 20, 0, 255, 0));
    EXPECT_EQ(from_array_element, from_draw_arrays)
        << "the buffer-backed indexed path must produce the same picture as glDrawArrays";
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

// The readback borrows GL_ARRAY_BUFFER; a draw straight afterwards must still
// source from the application's buffer, i.e. the borrow put the binding back.
TEST_F(ArrayElementVboTest, ReadbackLeavesTheApplicationsBindingInPlace) {
    std::vector<GLubyte> before, after;

    clear_(GL_COLOR_BUFFER_BIT_);
    draw_arrays_(GL_TRIANGLES_, 0, 6);
    Capture(&before);

    begin_(GL_TRIANGLES_);
    for (GLint i = 0; i < 6; ++i) array_element_(i);
    end_();
    finish_();

    clear_(GL_COLOR_BUFFER_BIT_);
    draw_arrays_(GL_TRIANGLES_, 0, 6);
    Capture(&after);

    EXPECT_EQ(after, before) << "glArrayElement must not disturb the array-buffer binding";
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

} // namespace
