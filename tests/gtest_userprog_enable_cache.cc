// SimpleFPEWrapper - tests/gtest_userprog_enable_cache.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// sfpewSendUserProgramAttributes() skips glEnableVertexAttribArray when
// fpe_user_vao_enabled already has the bit: enable state is per-VAO and
// persists, and that VAO is wrapper-owned so the mask is authoritative.
// RenderDoc 1.16-Optifine/1-frame19661.rdc measured 365 of 373 enables on it
// as redundant - 9.9% of the whole frame's call count.
//
// The failure mode that skip could introduce is an attribute that the mask
// claims is enabled while the VAO has it disabled: geometry silently loses an
// input. The mask is only cleared by a draw whose layout DROPS that slot, so
// the case to pin is shrink-then-regrow:
//
//   draw 1  fpe_Vertex + fpe_Color   -> both enabled, mask = {0,2}
//   draw 2  fpe_Vertex only          -> color disabled, mask = {0}
//   draw 3  fpe_Vertex + fpe_Color   -> color MUST be enabled again
//
// If the skip consulted a stale mask, draw 3 would read color from the
// constant current-value instead of the array and come out wrong.

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
using sfpew_test::GLubyte;
using sfpew_test::GLuint;
using sfpew_test::PixelProbe;

constexpr GLbitfield GL_COLOR_BUFFER_BIT_ = 0x00004000;
constexpr GLenum GL_TRIANGLES_ = 0x0004;
constexpr GLenum GL_FLOAT_ = 0x1406;
constexpr GLenum GL_VERTEX_SHADER_ = 0x8B31;
constexpr GLenum GL_FRAGMENT_SHADER_ = 0x8B30;
constexpr GLenum GL_COMPILE_STATUS_ = 0x8B81;
constexpr GLenum GL_LINK_STATUS_ = 0x8B82;
constexpr GLenum GL_VERTEX_ARRAY_ = 0x8074;
constexpr GLenum GL_COLOR_ARRAY_ = 0x8076;
constexpr GLenum GL_NO_ERROR_ = 0;

using UserProgEnableCacheTest = ContextTest;

TEST_F(UserProgEnableCacheTest, EnableMaskSurvivesShrinkAndRegrow) {
    auto clear_color = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glClearColor");
    auto clear = Get<void (*)(GLbitfield)>("glClear");
    auto draw_arrays = Get<void (*)(GLenum, GLint, GLsizei)>("glDrawArrays");
    auto get_error = Get<GLenum (*)()>("glGetError");
    auto finish = Get<void (*)()>("glFinish");
    auto use_program = Get<void (*)(GLuint)>("glUseProgram");
    auto create_shader = Get<GLuint (*)(GLenum)>("glCreateShader");
    auto shader_source = Get<void (*)(GLuint, GLsizei, const GLchar* const*, const GLint*)>(
        "glShaderSource");
    auto compile_shader = Get<void (*)(GLuint)>("glCompileShader");
    auto get_shaderiv = Get<void (*)(GLuint, GLenum, GLint*)>("glGetShaderiv");
    auto create_program = Get<GLuint (*)()>("glCreateProgram");
    auto attach_shader = Get<void (*)(GLuint, GLuint)>("glAttachShader");
    auto link_program = Get<void (*)(GLuint)>("glLinkProgram");
    auto get_programiv = Get<void (*)(GLuint, GLenum, GLint*)>("glGetProgramiv");
    auto get_program_info_log =
        Get<void (*)(GLuint, GLsizei, GLsizei*, GLchar*)>("glGetProgramInfoLog");
    auto enable_client_state = Get<void (*)(GLenum)>("glEnableClientState");
    auto disable_client_state = Get<void (*)(GLenum)>("glDisableClientState");
    auto vertex_pointer = Get<void (*)(GLint, GLenum, GLsizei, const void*)>("glVertexPointer");
    auto color_pointer = Get<void (*)(GLint, GLenum, GLsizei, const void*)>("glColorPointer");
    auto color4f = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glColor4f");
    auto read_pixels =
        Get<void (*)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*)>("glReadPixels");
    ASSERT_NE(read_pixels, nullptr);
    PixelProbe probe(read_pixels);

    // fpe_-named inputs are what sfpewUserProgramAttribLocations resolves, so
    // this program is fed by the fixed-function arrays through fpe_user_vao -
    // the VAO whose enable mask this test is about.
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
    const GLuint vs = create_shader(GL_VERTEX_SHADER_);
    const GLuint fs = create_shader(GL_FRAGMENT_SHADER_);
    shader_source(vs, 1, &vs_src, nullptr);
    shader_source(fs, 1, &fs_src, nullptr);
    compile_shader(vs);
    compile_shader(fs);
    GLint ok = 0;
    get_shaderiv(vs, GL_COMPILE_STATUS_, &ok);
    ASSERT_NE(ok, 0) << "VS compile";
    get_shaderiv(fs, GL_COMPILE_STATUS_, &ok);
    ASSERT_NE(ok, 0) << "FS compile";
    const GLuint program = create_program();
    attach_shader(program, vs);
    attach_shader(program, fs);
    link_program(program);
    get_programiv(program, GL_LINK_STATUS_, &ok);
    ASSERT_NE(ok, 0) << "program link";

    // Two full-screen triangles; the color array is green everywhere.
    static const GLfloat pos[] = {
        -1, -1, 1, -1, 1, 1, -1, -1, 1, 1, -1, 1,
    };
    static const GLfloat green[] = {
        0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1,
    };
    use_program(program);
    vertex_pointer(2, GL_FLOAT_, 0, pos);
    color_pointer(4, GL_FLOAT_, 0, green);
    // Draw 1: both arrays on. Establishes mask = {fpe_Vertex, fpe_Color}.
    enable_client_state(GL_VERTEX_ARRAY_);
    enable_client_state(GL_COLOR_ARRAY_);
    clear_color(0.0f, 0.0f, 1.0f, 1.0f);

    const auto draw_and_expect = [&](int r, int g, int b, const char* what) {
        clear(GL_COLOR_BUFFER_BIT_);
        draw_arrays(GL_TRIANGLES_, 0, 6);
        finish();
        const PixelProbe::Rgba p = probe.At(32, 32);
        EXPECT_TRUE((p.r > 200) == (r > 0) && (p.g > 200) == (g > 0) && (p.b > 200) == (b > 0))
            << what << ": pixel = (" << (int)p.r << ',' << (int)p.g << ',' << (int)p.b
            << "), expected (" << r << ',' << g << ',' << b << ')';
    };

    draw_and_expect(0, 1, 0, "draw 1: vertex+color arrays -> green");
    // Draw 2: same layout repeated. This is the draw whose enables the mask
    // lets us skip; it must still render identically.
    draw_and_expect(0, 1, 0, "draw 2: repeated layout still green (enable skipped)");
    // Draw 3: layout SHRINKS - color array off, constant red current value.
    // The wrapper must disable that location and feed the constant.
    disable_client_state(GL_COLOR_ARRAY_);
    color4f(1.0f, 0.0f, 0.0f, 1.0f);
    draw_and_expect(1, 0, 0, "draw 3: color array off -> constant red");
    // Draw 4: layout REGROWS. The color location was disabled by draw 3, so
    // its mask bit is clear and the enable must be re-issued. A stale mask
    // would leave it disabled and keep feeding the red constant.
    enable_client_state(GL_COLOR_ARRAY_);
    draw_and_expect(0, 1, 0, "draw 4: color array back on -> green again (enable re-issued)");

    disable_client_state(GL_VERTEX_ARRAY_);
    disable_client_state(GL_COLOR_ARRAY_);
    use_program(0);
    EXPECT_EQ(get_error(), GL_NO_ERROR_);
}

} // namespace
