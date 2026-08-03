// SimpleFPEWrapper - tests/gtest_userobject_quads.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// Sodium draws terrain with its OWN program and its OWN VAO of generic
// attributes, then issues glDrawArrays(GL_QUADS, first, count). That is legal
// GL 2.1, but mode 7 does not exist in GLES: passing it through raw is
// GL_INVALID_ENUM and the whole draw is dropped. RenderDoc capture
// sfpew-1.16-sodium-issue.rdc has 503 such draws in EID 130..2311.
//
// Unlike the fixed-function paths this draw cannot be moved to fpe_vao - the
// app's attributes live in the app's VAO and must not be touched. Only the
// element binding is borrowed, and since that is VAO state it has to be
// restored. Both halves are checked here:
//   A. a GL_QUADS draw through a user program's own VAO actually renders
//   B. the app's GL_ELEMENT_ARRAY_BUFFER binding survives that draw
//   C. the non-zero `first` form (Sodium draws sub-ranges) lands correctly
//   D. the indexed form, which must read the app's indices back and expand
//      them rather than just swapping the mode

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
constexpr GLenum GL_QUADS_ = 0x0007;
constexpr GLenum GL_FLOAT_ = 0x1406;
constexpr GLenum GL_UNSIGNED_BYTE_ = 0x1401;
constexpr GLenum GL_ARRAY_BUFFER_ = 0x8892;
constexpr GLenum GL_ELEMENT_ARRAY_BUFFER_ = 0x8893;
constexpr GLenum GL_ELEMENT_ARRAY_BUFFER_BINDING_ = 0x8895;
constexpr GLenum GL_STATIC_DRAW_ = 0x88E4;
constexpr GLenum GL_VERTEX_SHADER_ = 0x8B31;
constexpr GLenum GL_FRAGMENT_SHADER_ = 0x8B30;
constexpr GLenum GL_COMPILE_STATUS_ = 0x8B81;
constexpr GLenum GL_LINK_STATUS_ = 0x8B82;
constexpr GLenum GL_NO_ERROR_ = 0;

using UserObjectQuadsTest = ContextTest;

TEST_F(UserObjectQuadsTest, UserVaoQuadsRenderAndTheElementBindingSurvives) {
    auto clear_color = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glClearColor");
    auto clear = Get<void (*)(GLbitfield)>("glClear");
    auto draw_arrays = Get<void (*)(GLenum, GLint, GLsizei)>("glDrawArrays");
    auto get_error = Get<GLenum (*)()>("glGetError");
    auto finish = Get<void (*)()>("glFinish");
    auto get_integerv = Get<void (*)(GLenum, GLint*)>("glGetIntegerv");
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
    auto draw_elements = Get<void (*)(GLenum, GLsizei, GLenum, const void*)>("glDrawElements");
    auto read_pixels =
        Get<void (*)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*)>("glReadPixels");
    ASSERT_NE(read_pixels, nullptr);
    PixelProbe probe(read_pixels);

    static const char* vs_src =
        "#version 300 es\n"
        "in vec2 aPos;\n"
        "in vec3 aCol;\n"
        "out vec3 vCol;\n"
        "void main() { vCol = aCol; gl_Position = vec4(aPos, 0.0, 1.0); }\n";
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
    delete_shader(vs);
    delete_shader(fs);

    // Two quads: red left, green right, interleaved x,y,r,g,b.
    static const GLfloat verts[] = {
        -1.0f, -1.0f, 1, 0, 0, 0.0f, -1.0f, 1, 0, 0, 0.0f, 1.0f, 1, 0, 0, -1.0f, 1.0f, 1, 0, 0,
        0.0f,  -1.0f, 0, 1, 0, 1.0f, -1.0f, 0, 1, 0, 1.0f, 1.0f, 0, 1, 0, 0.0f,  1.0f, 0, 1, 0,
    };
    GLuint vao = 0, vbo = 0, app_ibo = 0;
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
    // The app parks its own element buffer in this VAO. The wrapper borrows
    // the element binding to convert GL_QUADS, so this must come back
    // unchanged.
    static const GLubyte dummy[] = {0, 1, 2};
    gen_buffers(1, &app_ibo);
    bind_buffer(GL_ELEMENT_ARRAY_BUFFER_, app_ibo);
    buffer_data(GL_ELEMENT_ARRAY_BUFFER_, static_cast<GLsizeiptr>(sizeof dummy), dummy,
               GL_STATIC_DRAW_);
    use_program(program);

    const auto expect = [&](int x, int y, int r, int g, int b, const char* what) {
        const PixelProbe::Rgba p = probe.At(x, y);
        EXPECT_TRUE((p.r > 200) == (r > 0) && (p.g > 200) == (g > 0) && (p.b > 200) == (b > 0))
            << what << ": pixel(" << x << ',' << y << ") = (" << (int)p.r << ',' << (int)p.g
            << ',' << (int)p.b << "), expected (" << r << ',' << g << ',' << b << ')';
    };

    // A: both quads. Raw GL_QUADS to a GLES backend is GL_INVALID_ENUM and
    // the draw vanishes, leaving the clear color.
    clear_color(0.0f, 0.0f, 1.0f, 1.0f);
    clear(GL_COLOR_BUFFER_BIT_);
    draw_arrays(GL_QUADS_, 0, 8);
    finish();
    expect(16, 32, 1, 0, 0, "A: user-VAO GL_QUADS draw, left quad red");
    expect(48, 32, 0, 1, 0, "A: user-VAO GL_QUADS draw, right quad green");

    // B: the app's element binding must be exactly what it set.
    GLint bound_elem = -1;
    get_integerv(GL_ELEMENT_ARRAY_BUFFER_BINDING_, &bound_elem);
    EXPECT_EQ(bound_elem, static_cast<GLint>(app_ibo))
        << "B: app's element-array binding must survive the quad conversion";

    // C: non-zero first, the form Sodium uses for sub-ranges. Only the second
    // quad is drawn, so the left half must stay at the clear color.
    clear(GL_COLOR_BUFFER_BIT_);
    draw_arrays(GL_QUADS_, 4, 4);
    finish();
    expect(48, 32, 0, 1, 0, "C: first=4 sub-range draws the second quad");
    expect(16, 32, 0, 0, 1, "C: first=4 leaves the first quad undrawn");

    // D: the indexed form. Same own-VAO situation, but GL_QUADS arrives
    // through glDrawElements, so the wrapper has to read the app's indices
    // back and expand them rather than just swapping the mode.
    static const GLubyte quad_idx[] = {4, 5, 6, 7}; // the right-hand quad only
    GLuint idx_buf = 0;
    gen_buffers(1, &idx_buf);
    bind_buffer(GL_ELEMENT_ARRAY_BUFFER_, idx_buf);
    buffer_data(GL_ELEMENT_ARRAY_BUFFER_, static_cast<GLsizeiptr>(sizeof quad_idx), quad_idx,
               GL_STATIC_DRAW_);
    clear(GL_COLOR_BUFFER_BIT_);
    draw_elements(GL_QUADS_, 4, GL_UNSIGNED_BYTE_, nullptr);
    finish();
    expect(48, 32, 0, 1, 0, "D: indexed GL_QUADS draws the referenced quad");
    expect(16, 32, 0, 0, 1, "D: indexed GL_QUADS leaves the other quad undrawn");
    get_integerv(GL_ELEMENT_ARRAY_BUFFER_BINDING_, &bound_elem);
    EXPECT_EQ(bound_elem, static_cast<GLint>(idx_buf))
        << "D: app's element binding must survive the indexed conversion";

    use_program(0);
    bind_vertex_array(0);
    delete_program(program);
    EXPECT_EQ(get_error(), GL_NO_ERROR_);
}

} // namespace
