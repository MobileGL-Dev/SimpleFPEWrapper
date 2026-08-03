// SimpleFPEWrapper - tests/gtest_userprog_multiattrib.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// The generic-attribute-0 aliasing rule (GL 2.1: attribute 0 aliases gl_Vertex)
// lets a shader that declares its own single position input be fed by
// glVertexPointer - that is the MC 1.16 PostPass/blit shape and
// tests/gtest_userobject_blit.cc covers it.
//
// But a shader with SEVERAL attributes pairs with its own VAO/VBO and must NOT
// be hijacked into consuming the fixed-function arrays. OptiFine's 1.12 world
// shader is that shape (position + color + texcoord + ...). When it was still
// current in the program shadow and the game returned to the main menu, every
// FFP draw routed into sfpewUserProgramFixedFunctionDrawArrays, which wired
// position only: no color, no texcoords, everything black. RenderDoc capture
// 1.12-Optifine/1-black-screen.rdc shows the signature - 26 draws, all with a
// single glVertexAttribPointer(index=0) and every texture bound to 0.
//
// This test pins the discrimination: a multi-attribute program must render
// through its OWN vertex state, unaffected by any enabled client arrays.

#include "sfpew_gtest.h"

#include <optional>

namespace {

using sfpew_test::ContextTest;
using sfpew_test::GLbitfield;
using sfpew_test::GLboolean;
using sfpew_test::GLchar;
using sfpew_test::GLenum;
using sfpew_test::GLfloat;
using sfpew_test::GLint;
using sfpew_test::GLsizei;
using sfpew_test::GLsizeiptr;
using sfpew_test::GLubyte;
using sfpew_test::GLuint;
using sfpew_test::PixelProbe;

constexpr GLbitfield GL_COLOR_BUFFER_BIT_ = 0x00004000;
constexpr GLenum GL_TRIANGLES_ = 0x0004;
constexpr GLenum GL_FLOAT_ = 0x1406;
constexpr GLenum GL_ARRAY_BUFFER_ = 0x8892;
constexpr GLenum GL_STATIC_DRAW_ = 0x88E4;
constexpr GLenum GL_VERTEX_SHADER_ = 0x8B31;
constexpr GLenum GL_FRAGMENT_SHADER_ = 0x8B30;
constexpr GLenum GL_COMPILE_STATUS_ = 0x8B81;
constexpr GLenum GL_LINK_STATUS_ = 0x8B82;
constexpr GLenum GL_VERTEX_ARRAY_ = 0x8074;
constexpr GLenum GL_COLOR_ARRAY_ = 0x8076;
constexpr GLenum GL_NO_ERROR_ = 0;

using UserProgMultiAttribTest = ContextTest;

TEST_F(UserProgMultiAttribTest, MultiAttributeProgramIsNotHijackedByClientArrays) {
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
    auto gen_buffers = Get<void (*)(GLsizei, GLuint*)>("glGenBuffers");
    auto bind_buffer = Get<void (*)(GLenum, GLuint)>("glBindBuffer");
    auto buffer_data = Get<void (*)(GLenum, GLsizeiptr, const void*, GLenum)>("glBufferData");
    auto gen_vertex_arrays = Get<void (*)(GLsizei, GLuint*)>("glGenVertexArrays");
    auto bind_vertex_array = Get<void (*)(GLuint)>("glBindVertexArray");
    auto vertex_attrib_pointer =
        Get<void (*)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*)>(
            "glVertexAttribPointer");
    auto enable_vertex_attrib_array = Get<void (*)(GLuint)>("glEnableVertexAttribArray");
    auto get_attrib_location = Get<GLint (*)(GLuint, const GLchar*)>("glGetAttribLocation");
    auto enable_client_state = Get<void (*)(GLenum)>("glEnableClientState");
    auto disable_client_state = Get<void (*)(GLenum)>("glDisableClientState");
    auto vertex_pointer = Get<void (*)(GLint, GLenum, GLsizei, const void*)>("glVertexPointer");
    auto color_pointer = Get<void (*)(GLint, GLenum, GLsizei, const void*)>("glColorPointer");
    auto read_pixels =
        Get<void (*)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*)>("glReadPixels");
    ASSERT_NE(read_pixels, nullptr);
    PixelProbe probe(read_pixels);

    // The OptiFine 1.12 world-shader shape: several attributes, fed from the
    // program's own VAO.
    static const char* vs_src =
        "#version 300 es\n"
        "in vec4 aPos;\n"
        "in vec3 aCol;\n"
        "out vec3 vCol;\n"
        "void main() { vCol = aCol; gl_Position = aPos; }\n";
    static const char* fs_src =
        "#version 300 es\n"
        "precision mediump float;\n"
        "in vec3 vCol;\n"
        "out vec4 o;\n"
        "void main() { o = vec4(vCol, 1.0); }\n";
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

    // Full-screen green quad in the program's own VAO, interleaved x,y,r,g,b.
    static const GLfloat verts[] = {
        -1, -1, 0, 1, 0, 1, -1, 0, 1, 0, 1, 1, 0, 1, 0,
        -1, -1, 0, 1, 0, 1, 1,  0, 1, 0, -1, 1, 0, 1, 0,
    };
    GLuint vao = 0, vbo = 0;
    gen_vertex_arrays(1, &vao);
    bind_vertex_array(vao);
    gen_buffers(1, &vbo);
    bind_buffer(GL_ARRAY_BUFFER_, vbo);
    buffer_data(GL_ARRAY_BUFFER_, static_cast<GLsizeiptr>(sizeof verts), verts, GL_STATIC_DRAW_);
    const GLint loc_pos = get_attrib_location(program, "aPos");
    const GLint loc_col = get_attrib_location(program, "aCol");
    ASSERT_GE(loc_pos, 0) << "aPos location";
    ASSERT_GE(loc_col, 0) << "aCol location";
    vertex_attrib_pointer(static_cast<GLuint>(loc_pos), 2, GL_FLOAT_, sfpew_test::GL_FALSE_,
                          5 * static_cast<GLsizei>(sizeof(GLfloat)), nullptr);
    vertex_attrib_pointer(static_cast<GLuint>(loc_col), 3, GL_FLOAT_, sfpew_test::GL_FALSE_,
                          5 * static_cast<GLsizei>(sizeof(GLfloat)),
                          reinterpret_cast<const void*>(2 * sizeof(GLfloat)));
    enable_vertex_attrib_array(static_cast<GLuint>(loc_pos));
    enable_vertex_attrib_array(static_cast<GLuint>(loc_col));

    // Fixed-function client arrays are ALSO enabled and point at unrelated
    // geometry. This is the trap: the wrapper must not treat them as this
    // program's vertex source. Color is deliberately red so a hijack is
    // visible as red-or-black instead of green.
    static const GLfloat ff_pos[] = {-0.2f, -0.2f, 0.2f, -0.2f, 0.0f, 0.2f};
    static const GLfloat ff_col[] = {1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1};
    enable_client_state(GL_VERTEX_ARRAY_);
    enable_client_state(GL_COLOR_ARRAY_);
    vertex_pointer(2, GL_FLOAT_, 0, ff_pos);
    color_pointer(4, GL_FLOAT_, 0, ff_col);
    use_program(program);
    clear_color(0.0f, 0.0f, 1.0f, 1.0f);
    clear(GL_COLOR_BUFFER_BIT_);
    draw_arrays(GL_TRIANGLES_, 0, 6);
    finish();

    // The program's own green quad must cover the framebuffer. A hijack wires
    // only position from ffPos and drops the color input, which reads as
    // black (or leaves the clear color where the small triangle misses).
    const auto expect_green = [&](int x, int y, const char* what) {
        const PixelProbe::Rgba p = probe.At(x, y);
        EXPECT_TRUE(p.g > 200 && p.r <= 50 && p.b <= 50)
            << what << ": pixel(" << x << ',' << y << ") = (" << (int)p.r << ',' << (int)p.g
            << ',' << (int)p.b << ')';
    };
    expect_green(32, 32, "multi-attribute program draws from its own VAO (center)");
    expect_green(6, 6, "multi-attribute program draws from its own VAO (corner)");

    disable_client_state(GL_VERTEX_ARRAY_);
    disable_client_state(GL_COLOR_ARRAY_);
    use_program(0);
    bind_vertex_array(0);
    EXPECT_EQ(get_error(), GL_NO_ERROR_);
}

} // namespace
