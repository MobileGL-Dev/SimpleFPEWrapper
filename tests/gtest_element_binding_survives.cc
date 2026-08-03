// SimpleFPEWrapper - tests/gtest_element_binding_survives.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// An app that keeps its indices in a GL_ELEMENT_ARRAY_BUFFER on VAO 0 must find
// that binding intact after a fixed-function draw runs in between. The wrapper
// binds its own element buffer for GL_QUADS index synthesis, and the draw guard
// restores the app's binding afterwards.
//
// sfpewFlushDeferredDrawState() now elides that restore when its shadow proves
// VAO 0 already holds the value - which is the normal case, because the wrapper
// binds its element buffer while its OWN VAO is current and an element binding
// is VAO state. Measured 100% redundant (20000/20000 restores in bench.mcgui,
// 140000/140000 in bench.mcentity), so the restore was pure per-draw cost.
//
// Sequence, all on VAO 0:
//   1. app uploads indices to its own element buffer, draws from it  -> green
//   2. a fixed-function immediate GL_QUADS draw runs in between (this is what
//      makes the wrapper bind its own element buffer and hold the app's state)
//   3. app repeats its indexed draw WITHOUT re-binding anything      -> green
//
// Step 3 is the assertion: if the element binding did not survive, the draw
// reads indices from whatever the wrapper left bound - its own quad-index
// buffer, whose contents are unrelated - and the result is not the green quad.

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
using sfpew_test::GLuint;
using sfpew_test::GLubyte;
using sfpew_test::GLushort;
using sfpew_test::PixelProbe;

constexpr GLbitfield GL_COLOR_BUFFER_BIT_ = 0x00004000;
constexpr GLenum GL_TRIANGLES_ = 0x0004;
constexpr GLenum GL_QUADS_ = 0x0007;
constexpr GLenum GL_FLOAT_ = 0x1406;
constexpr GLenum GL_UNSIGNED_SHORT_ = 0x1403;
constexpr GLenum GL_ARRAY_BUFFER_ = 0x8892;
constexpr GLenum GL_ELEMENT_ARRAY_BUFFER_ = 0x8893;
constexpr GLenum GL_STATIC_DRAW_ = 0x88E4;
constexpr GLenum GL_VERTEX_SHADER_ = 0x8B31;
constexpr GLenum GL_FRAGMENT_SHADER_ = 0x8B30;
constexpr GLenum GL_COMPILE_STATUS_ = 0x8B81;
constexpr GLenum GL_LINK_STATUS_ = 0x8B82;
constexpr GLenum GL_ELEMENT_ARRAY_BUFFER_BINDING_ = 0x8895;
constexpr GLenum GL_NO_ERROR_ = 0;

using ElementBindingSurvivesTest = ContextTest;

TEST_F(ElementBindingSurvivesTest, Va0ElementBindingSurvivesAFixedFunctionDraw) {
    auto clear_color = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glClearColor");
    auto clear = Get<void (*)(GLbitfield)>("glClear");
    auto draw_elements = Get<void (*)(GLenum, GLsizei, GLenum, const void*)>("glDrawElements");
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
    auto gen_buffers = Get<void (*)(GLsizei, GLuint*)>("glGenBuffers");
    auto bind_buffer = Get<void (*)(GLenum, GLuint)>("glBindBuffer");
    auto buffer_data = Get<void (*)(GLenum, GLsizeiptr, const void*, GLenum)>("glBufferData");
    auto vertex_attrib_pointer =
        Get<void (*)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*)>(
            "glVertexAttribPointer");
    auto enable_vertex_attrib_array = Get<void (*)(GLuint)>("glEnableVertexAttribArray");
    auto get_attrib_location = Get<GLint (*)(GLuint, const GLchar*)>("glGetAttribLocation");
    auto begin = Get<void (*)(GLenum)>("glBegin");
    auto end = Get<void (*)()>("glEnd");
    auto color4f = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glColor4f");
    auto vertex2f = Get<void (*)(GLfloat, GLfloat)>("glVertex2f");
    auto read_pixels =
        Get<void (*)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*)>("glReadPixels");
    ASSERT_NE(read_pixels, nullptr);
    PixelProbe probe(read_pixels);

    // A plain program with its own attributes, so the indexed draws below are
    // pure passthrough and depend only on the element binding being intact.
    static const char* vs_src =
        "#version 300 es\n"
        "in vec2 aPos;\n"
        "void main() { gl_Position = vec4(aPos, 0.0, 1.0); }\n";
    static const char* fs_src =
        "#version 300 es\n"
        "precision mediump float;\n"
        "out vec4 o;\n"
        "void main() { o = vec4(0.0, 1.0, 0.0, 1.0); }\n";

    const GLuint vs = create_shader(GL_VERTEX_SHADER_);
    const GLuint fs = create_shader(GL_FRAGMENT_SHADER_);
    shader_source(vs, 1, &vs_src, nullptr);
    shader_source(fs, 1, &fs_src, nullptr);
    compile_shader(vs);
    compile_shader(fs);
    GLint compiled = 0;
    get_shaderiv(vs, GL_COMPILE_STATUS_, &compiled);
    ASSERT_NE(compiled, 0) << "vertex shader failed to compile";
    get_shaderiv(fs, GL_COMPILE_STATUS_, &compiled);
    ASSERT_NE(compiled, 0) << "fragment shader failed to compile";
    const GLuint program = create_program();
    attach_shader(program, vs);
    attach_shader(program, fs);
    link_program(program);
    GLint linked = 0;
    get_programiv(program, GL_LINK_STATUS_, &linked);
    ASSERT_NE(linked, 0) << "program failed to link";

    // Four corners; the app's indices select two triangles covering the screen.
    static const GLfloat verts[] = {-1, -1, 1, -1, 1, 1, -1, 1};
    static const GLushort idx[] = {0, 1, 2, 0, 2, 3};

    GLuint vbo = 0, ibo = 0;
    gen_buffers(1, &vbo);
    bind_buffer(GL_ARRAY_BUFFER_, vbo);
    buffer_data(GL_ARRAY_BUFFER_, static_cast<GLsizeiptr>(sizeof verts), verts, GL_STATIC_DRAW_);
    gen_buffers(1, &ibo);
    bind_buffer(GL_ELEMENT_ARRAY_BUFFER_, ibo);
    buffer_data(GL_ELEMENT_ARRAY_BUFFER_, static_cast<GLsizeiptr>(sizeof idx), idx,
               GL_STATIC_DRAW_);

    const GLint loc_pos = get_attrib_location(program, "aPos");
    ASSERT_GE(loc_pos, 0) << "aPos attribute location";
    use_program(program);
    vertex_attrib_pointer(static_cast<GLuint>(loc_pos), 2, GL_FLOAT_, sfpew_test::GL_FALSE_, 0, nullptr);
    enable_vertex_attrib_array(static_cast<GLuint>(loc_pos));

    clear_color(0.0f, 0.0f, 1.0f, 1.0f);

    const auto expect_green = [&](int x, int y, const char* what) {
        const PixelProbe::Rgba p = probe.At(x, y);
        EXPECT_TRUE(p.g > 200 && p.r <= 50 && p.b <= 50)
            << what << ": pixel(" << x << ',' << y << ") = (" << (int)p.r << ',' << (int)p.g
            << ',' << (int)p.b << ')';
    };

    // 1. Baseline: the app's indexed draw works.
    clear(GL_COLOR_BUFFER_BIT_);
    draw_elements(GL_TRIANGLES_, 6, GL_UNSIGNED_SHORT_, nullptr);
    finish();
    expect_green(32, 32, "step 1: app indexed draw from its own element buffer");

    // 2. A fixed-function GL_QUADS draw in between. This is what makes the
    //    wrapper bind its own element buffer (quad index synthesis) and take
    //    the deferred save of the app's state.
    use_program(0);
    begin(GL_QUADS_);
    color4f(1.0f, 0.0f, 0.0f, 1.0f);
    vertex2f(-0.2f, -0.2f);
    vertex2f(0.2f, -0.2f);
    vertex2f(0.2f, 0.2f);
    vertex2f(-0.2f, 0.2f);
    end();
    finish();

    // The app's element binding must still be reported as its own buffer. This
    // catches a lost binding directly, not just through the rendering.
    GLint bound = 0;
    get_integerv(GL_ELEMENT_ARRAY_BUFFER_BINDING_, &bound);
    EXPECT_EQ(bound, static_cast<GLint>(ibo))
        << "step 2: element binding must be the app's buffer after an FPE draw";

    // 3. The app repeats its indexed draw WITHOUT re-binding anything.
    use_program(program);
    clear(GL_COLOR_BUFFER_BIT_);
    draw_elements(GL_TRIANGLES_, 6, GL_UNSIGNED_SHORT_, nullptr);
    finish();
    expect_green(32, 32, "step 3: indexed draw still correct with no re-bind");
    expect_green(4, 4, "step 3: covers the corner too (indices intact)");

    use_program(0);
    bind_buffer(GL_ARRAY_BUFFER_, 0);
    bind_buffer(GL_ELEMENT_ARRAY_BUFFER_, 0);
    EXPECT_EQ(get_error(), GL_NO_ERROR_);
}

} // namespace
