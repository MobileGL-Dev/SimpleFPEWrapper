// SimpleFPEWrapper - tests/gtest_gl3_core.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// A GL 3.3 core frontend, doing exactly what a GLAD/GLEW/LWJGL3 engine
// does: discover the version, then render through a VAO with explicit
// layout(location = N) attributes and its own fragment output. Two
// wrapper-side failures used to make this impossible - an "OpenGL ES"
// version string that desktop loaders cannot parse, and a compat prelude
// whose auto-mapped attributes and fpe_FragColor collided with the
// shader's own declarations.

#include "sfpew_gtest.h"

#include <cstring>
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

constexpr int kWindow = 64;
constexpr GLbitfield GL_COLOR_BUFFER_BIT_ = 0x00004000;
constexpr GLenum GL_ARRAY_BUFFER_ = 0x8892;
constexpr GLenum GL_STATIC_DRAW_ = 0x88E4;
constexpr GLenum GL_FLOAT_ = 0x1406;
constexpr GLenum GL_TRIANGLES_ = 0x0004;
constexpr GLenum GL_MAJOR_VERSION_ = 0x821B;
constexpr GLenum GL_MINOR_VERSION_ = 0x821C;
constexpr GLenum GL_VERSION_ = 0x1F02;
constexpr GLenum GL_SHADING_LANGUAGE_VERSION_ = 0x8B8C;
constexpr GLenum GL_VERTEX_SHADER_ = 0x8B31;
constexpr GLenum GL_FRAGMENT_SHADER_ = 0x8B30;
constexpr GLenum GL_COMPILE_STATUS_ = 0x8B81;
constexpr GLenum GL_LINK_STATUS_ = 0x8B82;
constexpr GLenum GL_NO_ERROR_ = 0;

using Gl3CoreTest = ContextTest;

TEST_F(Gl3CoreTest, VersionDiscoveryMatchesWhatDesktopLoadersNeed) {
    auto get_string = Get<const GLubyte* (*)(GLenum)>("glGetString");
    auto get_integerv = Get<void (*)(GLenum, GLint*)>("glGetIntegerv");
    ASSERT_NE(get_string, nullptr);
    ASSERT_NE(get_integerv, nullptr);

    GLint major = -1, minor = -1;
    get_integerv(GL_MAJOR_VERSION_, &major);
    get_integerv(GL_MINOR_VERSION_, &minor);
    const char* ver = reinterpret_cast<const char*>(get_string(GL_VERSION_));
    const char* glsl = reinterpret_cast<const char*>(get_string(GL_SHADING_LANGUAGE_VERSION_));

    // A desktop loader parses the leading "<major>.<minor>" of GL_VERSION.
    ASSERT_NE(ver, nullptr);
    EXPECT_TRUE(ver[0] >= '0' && ver[0] <= '9')
        << "GL_VERSION must start with a digit - a desktop GL loader "
           "(GLAD/GLEW/LWJGL3) cannot parse it and aborts init: "
        << ver;
    EXPECT_GE(major, 3) << "GL_MAJOR_VERSION < 3";
    ASSERT_NE(glsl, nullptr);
    EXPECT_TRUE(glsl[0] >= '0' && glsl[0] <= '9')
        << "GL_SHADING_LANGUAGE_VERSION must start with a digit: " << glsl;
    // The reported string and the integer queries must tell one story.
    EXPECT_EQ(ver[0] - '0', major)
        << "GL_VERSION says " << ver[0] << ".x but GL_MAJOR_VERSION says " << major;
    // The backend's identity must stay visible (additive contract). A desktop
    // backend is reported verbatim; an ES backend is presented as its desktop
    // equivalent.
    EXPECT_NE(std::strstr(ver, "OpenGL ES"), nullptr)
        << "the backend's identity must stay visible in GL_VERSION: " << ver;
}

TEST_F(Gl3CoreTest, CoreProfileRenderPathLands) {
    auto gen_vertex_arrays = Get<void (*)(GLsizei, GLuint*)>("glGenVertexArrays");
    auto bind_vertex_array = Get<void (*)(GLuint)>("glBindVertexArray");
    auto gen_buffers = Get<void (*)(GLsizei, GLuint*)>("glGenBuffers");
    auto bind_buffer = Get<void (*)(GLenum, GLuint)>("glBindBuffer");
    auto buffer_data = Get<void (*)(GLenum, GLsizeiptr, const void*, GLenum)>("glBufferData");
    auto create_shader = Get<GLuint (*)(GLenum)>("glCreateShader");
    auto shader_source = Get<void (*)(GLuint, GLsizei, const GLchar* const*, const GLint*)>(
        "glShaderSource");
    auto compile_shader = Get<void (*)(GLuint)>("glCompileShader");
    auto get_shaderiv = Get<void (*)(GLuint, GLenum, GLint*)>("glGetShaderiv");
    auto get_shader_info_log = Get<void (*)(GLuint, GLsizei, GLsizei*, GLchar*)>("glGetShaderInfoLog");
    auto create_program = Get<GLuint (*)()>("glCreateProgram");
    auto attach_shader = Get<void (*)(GLuint, GLuint)>("glAttachShader");
    auto link_program = Get<void (*)(GLuint)>("glLinkProgram");
    auto get_programiv = Get<void (*)(GLuint, GLenum, GLint*)>("glGetProgramiv");
    auto get_program_info_log =
        Get<void (*)(GLuint, GLsizei, GLsizei*, GLchar*)>("glGetProgramInfoLog");
    auto use_program = Get<void (*)(GLuint)>("glUseProgram");
    auto vertex_attrib_pointer =
        Get<void (*)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*)>(
            "glVertexAttribPointer");
    auto enable_vertex_attrib_array = Get<void (*)(GLuint)>("glEnableVertexAttribArray");
    auto clear_color = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glClearColor");
    auto clear = Get<void (*)(GLbitfield)>("glClear");
    auto draw_arrays = Get<void (*)(GLenum, GLint, GLsizei)>("glDrawArrays");
    auto get_error = Get<GLenum (*)()>("glGetError");
    auto read_pixels =
        Get<void (*)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*)>("glReadPixels");
    ASSERT_NE(read_pixels, nullptr);
    PixelProbe probe(read_pixels);

    GLuint vao = 0, vbo = 0;
    gen_vertex_arrays(1, &vao);
    bind_vertex_array(vao);
    gen_buffers(1, &vbo);
    bind_buffer(GL_ARRAY_BUFFER_, vbo);
    // interleaved position(2) + color(3), fed via explicit locations
    static const GLfloat verts[] = {
        -0.9f, -0.9f, 1, 0, 0, 0.9f, -0.9f, 1, 0, 0, 0.0f, 0.9f, 1, 0, 0,
    };
    buffer_data(GL_ARRAY_BUFFER_, static_cast<GLsizeiptr>(sizeof verts), verts, GL_STATIC_DRAW_);

    static const char* vs_src =
        "#version 330 core\n"
        "layout(location = 0) in vec2 aPos;\n"
        "layout(location = 1) in vec3 aColor;\n"
        "out vec3 vColor;\n"
        "void main() { vColor = aColor; gl_Position = vec4(aPos, 0.0, 1.0); }\n";
    static const char* fs_src =
        "#version 330 core\n"
        "in vec3 vColor;\n"
        "out vec4 FragColor;\n"
        "void main() { FragColor = vec4(vColor, 1.0); }\n";
    const GLuint vs = create_shader(GL_VERTEX_SHADER_);
    shader_source(vs, 1, &vs_src, nullptr);
    compile_shader(vs);
    GLint ok = 0;
    get_shaderiv(vs, GL_COMPILE_STATUS_, &ok);
    ASSERT_NE(ok, 0) << "VS compile failed";
    const GLuint fs = create_shader(GL_FRAGMENT_SHADER_);
    shader_source(fs, 1, &fs_src, nullptr);
    compile_shader(fs);
    get_shaderiv(fs, GL_COMPILE_STATUS_, &ok);
    ASSERT_NE(ok, 0) << "FS compile failed";
    const GLuint program = create_program();
    attach_shader(program, vs);
    attach_shader(program, fs);
    link_program(program);
    get_programiv(program, GL_LINK_STATUS_, &ok);
    ASSERT_NE(ok, 0) << "link failed";
    use_program(program);

    // The app addresses the locations it declared in the shader; it never
    // calls glBindAttribLocation nor glGetAttribLocation.
    vertex_attrib_pointer(0, 2, GL_FLOAT_, sfpew_test::GL_FALSE_,
                          5 * static_cast<GLsizei>(sizeof(GLfloat)), nullptr);
    enable_vertex_attrib_array(0);
    vertex_attrib_pointer(1, 3, GL_FLOAT_, sfpew_test::GL_FALSE_,
                          5 * static_cast<GLsizei>(sizeof(GLfloat)),
                          reinterpret_cast<const void*>(2 * sizeof(GLfloat)));
    enable_vertex_attrib_array(1);

    clear_color(0, 0, 1, 1);
    clear(GL_COLOR_BUFFER_BIT_);
    draw_arrays(GL_TRIANGLES_, 0, 3);

    const PixelProbe::Rgba px = probe.At(kWindow / 2, kWindow / 2);
    EXPECT_TRUE(px.r >= 200 && px.b <= 50)
        << "the GL3 core draw must land: explicit layout(location) inputs "
           "are not reaching the declared slots - center pixel = ("
        << (int)px.r << ',' << (int)px.g << ',' << (int)px.b << ')';
    EXPECT_EQ(get_error(), GL_NO_ERROR_);
}

} // namespace
