// SimpleFPEWrapper - tests/gtest_ffp_vbo_arrays.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// Fixed-function vertex arrays sourced from the app's own VBO: glVertexPointer
// and glColorPointer given BUFFER OFFSETS while GL_ARRAY_BUFFER is bound,
// rather than client-memory pointers. That is how Minecraft 1.16 + OptiFine
// feeds its world draws, and nothing else in the suite covered it - every
// other fixed-function test passes client pointers, which takes the opposite
// branch (upload into the wrapper's own ring buffer).
//
// It is also the shape where sfpewBackendBindAttributeBuffer() elides its
// glBindBuffer: the attribute source IS the buffer the app already has bound,
// and the draw guard leaves the backend on the app's binding, so the bind is a
// provable no-op. If that elision were wrong the draw would source attributes
// from whatever buffer happened to be bound - the wrapper's ring, or nothing -
// and the geometry would be garbage or missing.

#include "sfpew_gtest.h"

#include <optional>

namespace {

using sfpew_test::ContextTest;
using sfpew_test::GLbitfield;
using sfpew_test::GLchar;
using sfpew_test::GLenum;
using sfpew_test::GLfloat;
using sfpew_test::GLint;
using sfpew_test::GLsizei;
using sfpew_test::GLsizeiptr;
using sfpew_test::GLuint;
using sfpew_test::GLubyte;
using sfpew_test::PixelProbe;

constexpr GLbitfield GL_COLOR_BUFFER_BIT_ = 0x00004000;
constexpr GLenum GL_TRIANGLES_ = 0x0004;
constexpr GLenum GL_FLOAT_ = 0x1406;
constexpr GLenum GL_UNSIGNED_BYTE_ = 0x1401;
constexpr GLenum GL_ARRAY_BUFFER_ = 0x8892;
constexpr GLenum GL_STATIC_DRAW_ = 0x88E4;
constexpr GLenum GL_VERTEX_SHADER_ = 0x8B31;
constexpr GLenum GL_FRAGMENT_SHADER_ = 0x8B30;
constexpr GLenum GL_COMPILE_STATUS_ = 0x8B81;
constexpr GLenum GL_LINK_STATUS_ = 0x8B82;
constexpr GLenum GL_VERTEX_ARRAY_ = 0x8074;
constexpr GLenum GL_COLOR_ARRAY_ = 0x8076;
constexpr GLenum GL_NO_ERROR_ = 0;

class FfpVboArraysTest : public ContextTest {
protected:
    void SetUp() override {
        ContextTest::SetUp();
        if (::testing::Test::IsSkipped()) return;

        clear_color_ = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glClearColor");
        clear_ = Get<void (*)(GLbitfield)>("glClear");
        draw_arrays_ = Get<void (*)(GLenum, GLint, GLsizei)>("glDrawArrays");
        draw_elements_ = Get<void (*)(GLenum, GLsizei, GLenum, const void*)>("glDrawElements");
        get_error_ = Get<GLenum (*)()>("glGetError");
        finish_ = Get<void (*)()>("glFinish");
        use_program_ = Get<void (*)(GLuint)>("glUseProgram");
        create_shader_ = Get<GLuint (*)(GLenum)>("glCreateShader");
        shader_source_ = Get<void (*)(GLuint, GLsizei, const GLchar* const*, const GLint*)>(
            "glShaderSource");
        compile_shader_ = Get<void (*)(GLuint)>("glCompileShader");
        get_shaderiv_ = Get<void (*)(GLuint, GLenum, GLint*)>("glGetShaderiv");
        create_program_ = Get<GLuint (*)()>("glCreateProgram");
        attach_shader_ = Get<void (*)(GLuint, GLuint)>("glAttachShader");
        link_program_ = Get<void (*)(GLuint)>("glLinkProgram");
        get_programiv_ = Get<void (*)(GLuint, GLenum, GLint*)>("glGetProgramiv");
        get_program_info_log_ =
            Get<void (*)(GLuint, GLsizei, GLsizei*, GLchar*)>("glGetProgramInfoLog");
        gen_buffers_ = Get<void (*)(GLsizei, GLuint*)>("glGenBuffers");
        bind_buffer_ = Get<void (*)(GLenum, GLuint)>("glBindBuffer");
        buffer_data_ = Get<void (*)(GLenum, GLsizeiptr, const void*, GLenum)>("glBufferData");
        enable_client_state_ = Get<void (*)(GLenum)>("glEnableClientState");
        disable_client_state_ = Get<void (*)(GLenum)>("glDisableClientState");
        vertex_pointer_ = Get<void (*)(GLint, GLenum, GLsizei, const void*)>("glVertexPointer");
        color_pointer_ = Get<void (*)(GLint, GLenum, GLsizei, const void*)>("glColorPointer");
        auto read_pixels =
            Get<void (*)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*)>("glReadPixels");
        ASSERT_NE(read_pixels, nullptr);
        probe_.emplace(read_pixels);

        // A user program that takes the same attribute names the fixed-function
        // path feeds, so the app's own program consumes the arrays.
        static const char* vs_src =
            "#version 300 es\n"
            "in vec4 fpe_Vertex;\n"
            "in vec4 fpe_Color;\n"
            "out vec4 vCol;\n"
            "void main() { vCol = fpe_Color; gl_Position = fpe_Vertex; }\n";
        static const char* fs_src =
            "#version 300 es\n"
            "precision mediump float;\n"
            "in vec4 vCol;\n"
            "out vec4 o;\n"
            "void main() { o = vCol; }\n";

        const GLuint vs = create_shader_(GL_VERTEX_SHADER_);
        const GLuint fs = create_shader_(GL_FRAGMENT_SHADER_);
        shader_source_(vs, 1, &vs_src, nullptr);
        shader_source_(fs, 1, &fs_src, nullptr);
        compile_shader_(vs);
        compile_shader_(fs);
        GLint compiled = 0;
        get_shaderiv_(vs, GL_COMPILE_STATUS_, &compiled);
        ASSERT_NE(compiled, 0) << "vertex shader failed to compile";
        get_shaderiv_(fs, GL_COMPILE_STATUS_, &compiled);
        ASSERT_NE(compiled, 0) << "fragment shader failed to compile";
        program_ = create_program_();
        attach_shader_(program_, vs);
        attach_shader_(program_, fs);
        link_program_(program_);
        GLint linked = 0;
        get_programiv_(program_, GL_LINK_STATUS_, &linked);
        ASSERT_NE(linked, 0) << "program failed to link";
        use_program_(program_);

        // Interleaved x,y,r,g,b,a - two full-screen triangles, all green.
        // Held in the app's OWN buffer; the fixed-function pointers below are
        // offsets into it, so starting_pointer stays under the stride and the
        // wrapper sources attributes straight from this buffer instead of
        // uploading a copy.
        static const GLfloat verts[] = {
            -1, -1, 0, 1, 0, 1, 1, -1, 0, 1, 0, 1, 1, 1, 0, 1, 0, 1,
            -1, -1, 0, 1, 0, 1, 1, 1,  0, 1, 0, 1, -1, 1, 0, 1, 0, 1,
        };
        const GLsizei stride = 6 * static_cast<GLsizei>(sizeof(GLfloat));
        GLuint vbo = 0;
        gen_buffers_(1, &vbo);
        bind_buffer_(GL_ARRAY_BUFFER_, vbo);
        buffer_data_(GL_ARRAY_BUFFER_, static_cast<GLsizeiptr>(sizeof verts), verts,
                    GL_STATIC_DRAW_);

        vertex_pointer_(2, GL_FLOAT_, stride, nullptr);
        color_pointer_(4, GL_FLOAT_, stride,
                       reinterpret_cast<const void*>(2 * sizeof(GLfloat)));
        enable_client_state_(GL_VERTEX_ARRAY_);
        enable_client_state_(GL_COLOR_ARRAY_);

        clear_color_(0.0f, 0.0f, 1.0f, 1.0f);
        get_error_();
    }

    // r/g/b nonzero means "that channel must be high"; zero means low.
    void ExpectGreen(int x, int y, const char* what) {
        const PixelProbe::Rgba p = probe_->At(x, y);
        EXPECT_TRUE(p.g > 200 && p.r <= 200 && p.b <= 200)
            << what << ": pixel(" << x << ',' << y << ") = (" << (int)p.r << ',' << (int)p.g
            << ',' << (int)p.b << ')';
    }
    void ExpectBlue(int x, int y, const char* what) {
        const PixelProbe::Rgba p = probe_->At(x, y);
        EXPECT_TRUE(p.b > 200 && p.r <= 200 && p.g <= 200)
            << what << ": pixel(" << x << ',' << y << ") = (" << (int)p.r << ',' << (int)p.g
            << ',' << (int)p.b << ')';
    }

    void (*clear_color_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*clear_)(GLbitfield) = nullptr;
    void (*draw_arrays_)(GLenum, GLint, GLsizei) = nullptr;
    void (*draw_elements_)(GLenum, GLsizei, GLenum, const void*) = nullptr;
    GLenum (*get_error_)() = nullptr;
    void (*finish_)() = nullptr;
    void (*use_program_)(GLuint) = nullptr;
    GLuint (*create_shader_)(GLenum) = nullptr;
    void (*shader_source_)(GLuint, GLsizei, const GLchar* const*, const GLint*) = nullptr;
    void (*compile_shader_)(GLuint) = nullptr;
    void (*get_shaderiv_)(GLuint, GLenum, GLint*) = nullptr;
    GLuint (*create_program_)() = nullptr;
    void (*attach_shader_)(GLuint, GLuint) = nullptr;
    void (*link_program_)(GLuint) = nullptr;
    void (*get_programiv_)(GLuint, GLenum, GLint*) = nullptr;
    void (*get_program_info_log_)(GLuint, GLsizei, GLsizei*, GLchar*) = nullptr;
    void (*gen_buffers_)(GLsizei, GLuint*) = nullptr;
    void (*bind_buffer_)(GLenum, GLuint) = nullptr;
    void (*buffer_data_)(GLenum, GLsizeiptr, const void*, GLenum) = nullptr;
    void (*enable_client_state_)(GLenum) = nullptr;
    void (*disable_client_state_)(GLenum) = nullptr;
    void (*vertex_pointer_)(GLint, GLenum, GLsizei, const void*) = nullptr;
    void (*color_pointer_)(GLint, GLenum, GLsizei, const void*) = nullptr;
    std::optional<PixelProbe> probe_;
    GLuint program_ = 0;
};

TEST_F(FfpVboArraysTest, DrawsSourceAttributesFromTheAppsOwnBuffer) {
    // Draw 1 establishes the binding; draw 2 is the one whose attribute-buffer
    // bind is elided: the app's binding is unchanged and the guard still holds
    // it.
    clear_(GL_COLOR_BUFFER_BIT_);
    draw_arrays_(GL_TRIANGLES_, 0, 6);
    finish_();
    ExpectGreen(32, 32, "draw 1: VBO-backed FFP arrays, center");
    ExpectGreen(6, 6, "draw 1: VBO-backed FFP arrays, corner");

    clear_(GL_COLOR_BUFFER_BIT_);
    draw_arrays_(GL_TRIANGLES_, 0, 6);
    finish_();
    ExpectGreen(32, 32, "draw 2: repeated draw (bind elided), center");
    ExpectGreen(6, 6, "draw 2: repeated draw (bind elided), corner");
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

TEST_F(FfpVboArraysTest, ClientIndicesWorkWithVboBackedArrays) {
    // Same VBO-backed arrays, now indexed with indices in CLIENT memory and no
    // GL_ELEMENT_ARRAY_BUFFER bound. This is precisely the Minecraft 1.16 +
    // OptiFine world draw: userProgramDrawElements has to copy the indices into
    // its own ring (ES has no client-side index arrays) but must NOT walk them
    // to find the largest one - with the attributes in the app's buffer the
    // vertex count that scan produces has no consumer.
    static const GLubyte idx[] = {0, 1, 2, 3, 4, 5};
    clear_(GL_COLOR_BUFFER_BIT_);
    draw_elements_(GL_TRIANGLES_, 6, GL_UNSIGNED_BYTE_, idx);
    finish_();
    ExpectGreen(32, 32, "draw 3: VBO-backed arrays + client indices, center");
    ExpectGreen(6, 6, "draw 3: VBO-backed arrays + client indices, corner");

    // Indices that reference only the first triangle must light exactly it:
    // proves the draw honors the index data rather than the vertex range a
    // skipped scan would otherwise have implied.
    static const GLubyte idx_half[] = {0, 1, 2};
    clear_(GL_COLOR_BUFFER_BIT_);
    draw_elements_(GL_TRIANGLES_, 3, GL_UNSIGNED_BYTE_, idx_half);
    finish_();
    ExpectGreen(48, 16, "draw 4: partial index range covers its own triangle");
    ExpectBlue(8, 56, "draw 4: partial index range leaves the rest cleared");
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

} // namespace
