// SimpleFPEWrapper - tests/gtest_userobject_blit.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// Minecraft 1.16 fabulous mode: PostPass binds its OWN shader program that
// declares "in vec4 Position" (not gl_Vertex, not fpe_Vertex), then feeds the
// blit quad through glVertexPointer + glDrawArrays(GL_QUADS). On desktop GL the
// legacy aliasing rule routes generic attribute 0 to gl_Vertex, so attribute 0
// (Position) receives the vertex data automatically. The wrapper's
// sfpewUserProgramAttribLocations probed only fpe_Vertex, found nothing, and
// returned false - the draw fell through to raw glDrawArrays(GL_QUADS) on the
// GLES backend with nothing bound to Position, collapsing all four vertices to
// (0,0,0,1). RenderDoc capture, 1.16 fabulous, EID 1844.

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
constexpr GLenum GL_QUADS_ = 0x0007;
constexpr GLenum GL_FLOAT_ = 0x1406;
constexpr GLenum GL_VERTEX_ARRAY_ = 0x8074;
constexpr GLenum GL_FRAGMENT_SHADER_ = 0x8B30;
constexpr GLenum GL_VERTEX_SHADER_ = 0x8B31;
constexpr GLenum GL_COMPILE_STATUS_ = 0x8B81;
constexpr GLenum GL_LINK_STATUS_ = 0x8B82;
constexpr GLenum GL_NO_ERROR_ = 0;

using UserObjectBlitTest = ContextTest;

TEST_F(UserObjectBlitTest, AttributeZeroAliasingFeedsPositionFromVertexPointer) {
    auto clear_color = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glClearColor");
    auto clear = Get<void (*)(GLbitfield)>("glClear");
    auto enable_client_state = Get<void (*)(GLenum)>("glEnableClientState");
    auto disable_client_state = Get<void (*)(GLenum)>("glDisableClientState");
    auto vertex_pointer = Get<void (*)(GLint, GLenum, GLsizei, const void*)>("glVertexPointer");
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
    auto delete_shader = Get<void (*)(GLuint)>("glDeleteShader");
    auto delete_program = Get<void (*)(GLuint)>("glDeleteProgram");
    auto read_pixels =
        Get<void (*)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*)>("glReadPixels");
    ASSERT_NE(read_pixels, nullptr);
    PixelProbe probe(read_pixels);

    // A shader that uses "Position" at location 0 (like MC 1.16 PostPass/
    // blit). The name is NOT fpe_Vertex, so the wrapper must fall back to the
    // GL attribute-0 / gl_Vertex aliasing rule.
    static const char* vs_src =
        "#version 300 es\n"
        "in vec4 Position;\n"
        "void main() { gl_Position = Position; }\n";
    static const char* fs_src =
        "#version 300 es\n"
        "precision mediump float;\n"
        "out vec4 o;\n"
        "void main() { o = vec4(1.0, 0.0, 0.0, 1.0); }\n";
    const GLuint vs = create_shader(GL_VERTEX_SHADER_);
    const GLuint fs = create_shader(GL_FRAGMENT_SHADER_);
    shader_source(vs, 1, &vs_src, nullptr);
    shader_source(fs, 1, &fs_src, nullptr);
    compile_shader(vs);
    compile_shader(fs);
    GLint ok = 0;
    get_shaderiv(vs, GL_COMPILE_STATUS_, &ok);
    ASSERT_NE(ok, 0) << "VS compile failed";
    get_shaderiv(fs, GL_COMPILE_STATUS_, &ok);
    ASSERT_NE(ok, 0) << "FS compile failed";
    const GLuint program = create_program();
    attach_shader(program, vs);
    attach_shader(program, fs);
    link_program(program);
    get_programiv(program, GL_LINK_STATUS_, &ok);
    ASSERT_NE(ok, 0) << "program link";
    delete_shader(vs);
    delete_shader(fs);

    // Feed the blit quad through the fixed-function vertex array path,
    // exactly as MC 1.16's BufferUploader.end does.
    static const GLfloat quad[] = {
        -1.0f, -1.0f, 0.0f,
        1.0f,  -1.0f, 0.0f,
        1.0f,  1.0f,  0.0f,
        -1.0f, 1.0f,  0.0f,
    };
    clear_color(0.0f, 0.0f, 1.0f, 1.0f);
    clear(GL_COLOR_BUFFER_BIT_);
    use_program(program);
    enable_client_state(GL_VERTEX_ARRAY_);
    vertex_pointer(3, GL_FLOAT_, 0, quad);
    draw_arrays(GL_QUADS_, 0, 4);
    finish();

    // Without the aliasing fallback this was raw glDrawArrays(GL_QUADS) on
    // the ES backend with nothing bound to Position: all four vertices read
    // (0,0,0,1) and the quad collapsed to one point (center pixel stayed
    // blue).
    const PixelProbe::Rgba p = probe.At(32, 32);
    EXPECT_TRUE(p.r > 200 && p.g <= 50 && p.b <= 50)
        << "user program with 'in vec4 Position' must receive vertex array "
           "data: pixel = ("
        << (int)p.r << ',' << (int)p.g << ',' << (int)p.b << ')';

    use_program(0);
    disable_client_state(GL_VERTEX_ARRAY_);
    delete_program(program);
    EXPECT_EQ(get_error(), GL_NO_ERROR_);
}

} // namespace
