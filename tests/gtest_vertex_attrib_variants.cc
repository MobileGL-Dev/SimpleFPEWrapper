// SimpleFPEWrapper - tests/gtest_vertex_attrib_variants.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

#include "sfpew_gtest.h"

#include <cstdint>
#include <limits>

namespace {

using sfpew_test::ContextTest;
using sfpew_test::GLbitfield;
using sfpew_test::GLboolean;
using sfpew_test::GLchar;
using sfpew_test::GLdouble;
using sfpew_test::GLenum;
using sfpew_test::GLfloat;
using sfpew_test::GLint;
using sfpew_test::GLsizei;
using sfpew_test::GLsizeiptr;
using sfpew_test::GLubyte;
using sfpew_test::GLuint;
using sfpew_test::GLushort;
using sfpew_test::PixelProbe;

using GLbyte = std::int8_t;
using GLshort = std::int16_t;

constexpr GLenum GL_NO_ERROR_ = 0;
constexpr GLenum GL_INVALID_VALUE_ = 0x0501;
constexpr GLenum GL_CURRENT_VERTEX_ATTRIB_ = 0x8626;
constexpr GLenum GL_MAX_VERTEX_ATTRIBS_ = 0x8869;
constexpr GLenum GL_COMPILE_ = 0x1300;
constexpr GLenum GL_ARRAY_BUFFER_ = 0x8892;
constexpr GLenum GL_STATIC_DRAW_ = 0x88E4;
constexpr GLenum GL_FLOAT_ = 0x1406;
constexpr GLenum GL_TRIANGLES_ = 0x0004;
constexpr GLenum GL_VERTEX_SHADER_ = 0x8B31;
constexpr GLenum GL_FRAGMENT_SHADER_ = 0x8B30;
constexpr GLenum GL_COMPILE_STATUS_ = 0x8B81;
constexpr GLenum GL_LINK_STATUS_ = 0x8B82;
constexpr GLbitfield GL_COLOR_BUFFER_BIT_ = 0x00004000;

class VertexAttribVariantsTest : public ContextTest {
protected:
    void SetUp() override {
        ContextTest::SetUp();
        if (::testing::Test::IsSkipped() || ::testing::Test::HasFatalFailure()) return;
        get_attribfv_ =
            Get<void (*)(GLuint, GLenum, GLfloat*)>("glGetVertexAttribfv");
        get_integerv_ = Get<void (*)(GLenum, GLint*)>("glGetIntegerv");
        get_error_ = Get<GLenum (*)()>("glGetError");
        get_integerv_(GL_MAX_VERTEX_ATTRIBS_, &max_attribs_);
        ASSERT_GT(max_attribs_, 1);
        get_error_();
    }

    void ExpectAttrib(GLfloat x, GLfloat y, GLfloat z, GLfloat w) {
        GLfloat value[4] = {};
        get_attribfv_(1, GL_CURRENT_VERTEX_ATTRIB_, value);
        EXPECT_NEAR(value[0], x, 1e-6f);
        EXPECT_NEAR(value[1], y, 1e-6f);
        EXPECT_NEAR(value[2], z, 1e-6f);
        EXPECT_NEAR(value[3], w, 1e-6f);
        EXPECT_EQ(get_error_(), GL_NO_ERROR_);
    }

    void (*get_attribfv_)(GLuint, GLenum, GLfloat*) = nullptr;
    void (*get_integerv_)(GLenum, GLint*) = nullptr;
    GLenum (*get_error_)() = nullptr;
    GLint max_attribs_ = 0;
};

TEST_F(VertexAttribVariantsTest, DoubleAndShortFormsExpandWithoutNormalization) {
    Get<void (*)(GLuint, GLdouble)>("glVertexAttrib1d")(1, 1.5);
    ExpectAttrib(1.5f, 0.0f, 0.0f, 1.0f);
    const GLdouble d1[1] = {-2.5};
    Get<void (*)(GLuint, const GLdouble*)>("glVertexAttrib1dv")(1, d1);
    ExpectAttrib(-2.5f, 0.0f, 0.0f, 1.0f);

    Get<void (*)(GLuint, GLdouble, GLdouble)>("glVertexAttrib2d")(1, 1.5, 2.5);
    ExpectAttrib(1.5f, 2.5f, 0.0f, 1.0f);
    const GLdouble d2[2] = {-1.25, 3.75};
    Get<void (*)(GLuint, const GLdouble*)>("glVertexAttrib2dv")(1, d2);
    ExpectAttrib(-1.25f, 3.75f, 0.0f, 1.0f);

    Get<void (*)(GLuint, GLdouble, GLdouble, GLdouble)>("glVertexAttrib3d")(1, 1, 2, 3);
    ExpectAttrib(1, 2, 3, 1);
    const GLdouble d3[3] = {4, 5, 6};
    Get<void (*)(GLuint, const GLdouble*)>("glVertexAttrib3dv")(1, d3);
    ExpectAttrib(4, 5, 6, 1);

    Get<void (*)(GLuint, GLdouble, GLdouble, GLdouble, GLdouble)>("glVertexAttrib4d")(
        1, 7, 8, 9, 10);
    ExpectAttrib(7, 8, 9, 10);
    const GLdouble d4[4] = {-7, -8, -9, -10};
    Get<void (*)(GLuint, const GLdouble*)>("glVertexAttrib4dv")(1, d4);
    ExpectAttrib(-7, -8, -9, -10);

    Get<void (*)(GLuint, GLshort)>("glVertexAttrib1s")(1, 5);
    ExpectAttrib(5, 0, 0, 1);
    const GLshort s1[1] = {-5};
    Get<void (*)(GLuint, const GLshort*)>("glVertexAttrib1sv")(1, s1);
    ExpectAttrib(-5, 0, 0, 1);

    Get<void (*)(GLuint, GLshort, GLshort)>("glVertexAttrib2s")(1, 5, 6);
    ExpectAttrib(5, 6, 0, 1);
    const GLshort s2[2] = {-5, -6};
    Get<void (*)(GLuint, const GLshort*)>("glVertexAttrib2sv")(1, s2);
    ExpectAttrib(-5, -6, 0, 1);

    Get<void (*)(GLuint, GLshort, GLshort, GLshort)>("glVertexAttrib3s")(1, 5, 6, 7);
    ExpectAttrib(5, 6, 7, 1);
    const GLshort s3[3] = {-5, -6, -7};
    Get<void (*)(GLuint, const GLshort*)>("glVertexAttrib3sv")(1, s3);
    ExpectAttrib(-5, -6, -7, 1);

    Get<void (*)(GLuint, GLshort, GLshort, GLshort, GLshort)>("glVertexAttrib4s")(
        1, 5, 6, 7, 8);
    ExpectAttrib(5, 6, 7, 8);
    const GLshort s4[4] = {-5, -6, -7, -8};
    Get<void (*)(GLuint, const GLshort*)>("glVertexAttrib4sv")(1, s4);
    ExpectAttrib(-5, -6, -7, -8);
}

TEST_F(VertexAttribVariantsTest, NormalizedFormsUseTheFullIntegerRange) {
    const GLbyte b[4] = {std::numeric_limits<GLbyte>::min(), 0,
                         std::numeric_limits<GLbyte>::max(), 64};
    Get<void (*)(GLuint, const GLbyte*)>("glVertexAttrib4Nbv")(1, b);
    ExpectAttrib(-1.0f, 0.0f, 1.0f, 64.0f / 127.0f);

    const GLshort s[4] = {std::numeric_limits<GLshort>::min(), 0,
                          std::numeric_limits<GLshort>::max(), 16384};
    Get<void (*)(GLuint, const GLshort*)>("glVertexAttrib4Nsv")(1, s);
    ExpectAttrib(-1.0f, 0.0f, 1.0f, 16384.0f / 32767.0f);

    const GLint i[4] = {std::numeric_limits<GLint>::min(), 0,
                        std::numeric_limits<GLint>::max(), 0x40000000};
    Get<void (*)(GLuint, const GLint*)>("glVertexAttrib4Niv")(1, i);
    ExpectAttrib(-1.0f, 0.0f, 1.0f, 0.5f);

    Get<void (*)(GLuint, GLubyte, GLubyte, GLubyte, GLubyte)>("glVertexAttrib4Nub")(
        1, 255, 0, 128, 255);
    ExpectAttrib(1.0f, 0.0f, 128.0f / 255.0f, 1.0f);

    const GLubyte ub[4] = {0, 255, 64, 128};
    Get<void (*)(GLuint, const GLubyte*)>("glVertexAttrib4Nubv")(1, ub);
    ExpectAttrib(0.0f, 1.0f, 64.0f / 255.0f, 128.0f / 255.0f);

    const GLushort us[4] = {0, 65535, 16384, 32768};
    Get<void (*)(GLuint, const GLushort*)>("glVertexAttrib4Nusv")(1, us);
    ExpectAttrib(0.0f, 1.0f, 16384.0f / 65535.0f, 32768.0f / 65535.0f);

    const GLuint ui[4] = {0, std::numeric_limits<GLuint>::max(), 0x40000000u, 0x80000000u};
    Get<void (*)(GLuint, const GLuint*)>("glVertexAttrib4Nuiv")(1, ui);
    ExpectAttrib(0.0f, 1.0f, 0.25f, 0.5f);
}

TEST_F(VertexAttribVariantsTest, PlainIntegerVectorsUseDirectNumericCasts) {
    const GLbyte b[4] = {-5, 6, 7, 8};
    Get<void (*)(GLuint, const GLbyte*)>("glVertexAttrib4bv")(1, b);
    ExpectAttrib(-5, 6, 7, 8);

    const GLint i[4] = {-50, 60, 70, 80};
    Get<void (*)(GLuint, const GLint*)>("glVertexAttrib4iv")(1, i);
    ExpectAttrib(-50, 60, 70, 80);

    const GLubyte ub[4] = {255, 2, 3, 4};
    Get<void (*)(GLuint, const GLubyte*)>("glVertexAttrib4ubv")(1, ub);
    ExpectAttrib(255, 2, 3, 4);

    const GLuint ui[4] = {500, 600, 700, 800};
    Get<void (*)(GLuint, const GLuint*)>("glVertexAttrib4uiv")(1, ui);
    ExpectAttrib(500, 600, 700, 800);

    const GLushort us[4] = {50, 60, 70, 80};
    Get<void (*)(GLuint, const GLushort*)>("glVertexAttrib4usv")(1, us);
    ExpectAttrib(50, 60, 70, 80);
}

TEST_F(VertexAttribVariantsTest, DoubleGetterDisplayListsAliasesAndValidationAgree) {
    const GLshort initial[4] = {1, 2, 3, 4};
    auto attrib4sv = Get<void (*)(GLuint, const GLshort*)>("glVertexAttrib4sv");
    attrib4sv(1, initial);

    const GLuint list = Get<GLuint (*)(GLsizei)>("glGenLists")(1);
    ASSERT_NE(list, 0u);
    auto new_list = Get<void (*)(GLuint, GLenum)>("glNewList");
    auto end_list = Get<void (*)()>("glEndList");
    auto call_list = Get<void (*)(GLuint)>("glCallList");
    GLshort captured[4] = {5, 6, 7, 8};
    new_list(list, GL_COMPILE_);
    attrib4sv(1, captured);
    end_list();
    ExpectAttrib(1, 2, 3, 4);
    captured[0] = captured[1] = captured[2] = captured[3] = 0;
    call_list(list);
    ExpectAttrib(5, 6, 7, 8);

    GLdouble widened[4] = {};
    Get<void (*)(GLuint, GLenum, GLdouble*)>("glGetVertexAttribdv")(
        1, GL_CURRENT_VERTEX_ATTRIB_, widened);
    EXPECT_DOUBLE_EQ(widened[0], 5.0);
    EXPECT_DOUBLE_EQ(widened[1], 6.0);
    EXPECT_DOUBLE_EQ(widened[2], 7.0);
    EXPECT_DOUBLE_EQ(widened[3], 8.0);
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);

    constexpr const char* names[] = {
        "glVertexAttrib1d",   "glVertexAttrib1dv",  "glVertexAttrib2d",
        "glVertexAttrib2dv",  "glVertexAttrib3d",   "glVertexAttrib3dv",
        "glVertexAttrib4d",   "glVertexAttrib4dv",  "glVertexAttrib1s",
        "glVertexAttrib1sv",  "glVertexAttrib2s",   "glVertexAttrib2sv",
        "glVertexAttrib3s",   "glVertexAttrib3sv",  "glVertexAttrib4s",
        "glVertexAttrib4sv",  "glVertexAttrib4Nbv", "glVertexAttrib4Nsv",
        "glVertexAttrib4Niv", "glVertexAttrib4Nub", "glVertexAttrib4Nubv",
        "glVertexAttrib4Nusv", "glVertexAttrib4Nuiv", "glVertexAttrib4bv",
        "glVertexAttrib4iv",  "glVertexAttrib4ubv", "glVertexAttrib4uiv",
        "glVertexAttrib4usv", "glGetVertexAttribdv",
    };
    for (const char* canonical : names) {
        std::string alias = canonical;
        alias += "ARB";
        EXPECT_EQ(Get<void*>(canonical), Get<void*>(alias.c_str())) << canonical;
    }

    Get<void (*)(GLuint, GLshort)>("glVertexAttrib1s")(static_cast<GLuint>(max_attribs_), 1);
    EXPECT_EQ(get_error_(), GL_INVALID_VALUE_);
    Get<void (*)(GLuint, GLenum, GLdouble*)>("glGetVertexAttribdv")(
        static_cast<GLuint>(max_attribs_), GL_CURRENT_VERTEX_ATTRIB_, widened);
    EXPECT_EQ(get_error_(), GL_INVALID_VALUE_);
}

TEST_F(VertexAttribVariantsTest, NormalizedConstantAttributeReachesAUserShader) {
    auto create_shader = Get<GLuint (*)(GLenum)>("glCreateShader");
    auto shader_source = Get<void (*)(GLuint, GLsizei, const GLchar* const*, const GLint*)>(
        "glShaderSource");
    auto compile_shader = Get<void (*)(GLuint)>("glCompileShader");
    auto get_shaderiv = Get<void (*)(GLuint, GLenum, GLint*)>("glGetShaderiv");
    auto get_shader_info_log =
        Get<void (*)(GLuint, GLsizei, GLsizei*, GLchar*)>("glGetShaderInfoLog");
    auto create_program = Get<GLuint (*)()>("glCreateProgram");
    auto attach_shader = Get<void (*)(GLuint, GLuint)>("glAttachShader");
    auto link_program = Get<void (*)(GLuint)>("glLinkProgram");
    auto get_programiv = Get<void (*)(GLuint, GLenum, GLint*)>("glGetProgramiv");
    auto use_program = Get<void (*)(GLuint)>("glUseProgram");

    const auto compile = [&](GLenum type, const char* source) {
        const GLuint shader = create_shader(type);
        shader_source(shader, 1, &source, nullptr);
        compile_shader(shader);
        GLint ok = 0;
        get_shaderiv(shader, GL_COMPILE_STATUS_, &ok);
        if (ok == 0) {
            char log[4096] = {};
            get_shader_info_log(shader, sizeof log, nullptr, log);
            ADD_FAILURE() << "shader compile failed: " << log;
            return GLuint{0};
        }
        return shader;
    };

    static const char* vertex_source =
        "#version 300 es\n"
        "layout(location=0) in vec2 position;\n"
        "layout(location=1) in vec4 color;\n"
        "out vec4 varyingColor;\n"
        "void main() { gl_Position=vec4(position,0.0,1.0); varyingColor=color; }\n";
    static const char* fragment_source =
        "#version 300 es\n"
        "precision highp float;\n"
        "in vec4 varyingColor;\n"
        "out vec4 fragmentColor;\n"
        "void main() { fragmentColor=varyingColor; }\n";
    const GLuint vertex = compile(GL_VERTEX_SHADER_, vertex_source);
    const GLuint fragment = compile(GL_FRAGMENT_SHADER_, fragment_source);
    ASSERT_NE(vertex, 0u);
    ASSERT_NE(fragment, 0u);
    const GLuint program = create_program();
    attach_shader(program, vertex);
    attach_shader(program, fragment);
    link_program(program);
    GLint linked = 0;
    get_programiv(program, GL_LINK_STATUS_, &linked);
    ASSERT_NE(linked, 0);
    use_program(program);

    auto gen_vertex_arrays = Get<void (*)(GLsizei, GLuint*)>("glGenVertexArrays");
    auto bind_vertex_array = Get<void (*)(GLuint)>("glBindVertexArray");
    auto gen_buffers = Get<void (*)(GLsizei, GLuint*)>("glGenBuffers");
    auto bind_buffer = Get<void (*)(GLenum, GLuint)>("glBindBuffer");
    auto buffer_data = Get<void (*)(GLenum, GLsizeiptr, const void*, GLenum)>("glBufferData");
    auto vertex_attrib_pointer =
        Get<void (*)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*)>(
            "glVertexAttribPointer");
    auto enable_vertex_attrib_array = Get<void (*)(GLuint)>("glEnableVertexAttribArray");
    auto disable_vertex_attrib_array = Get<void (*)(GLuint)>("glDisableVertexAttribArray");
    auto viewport = Get<void (*)(GLint, GLint, GLsizei, GLsizei)>("glViewport");
    auto clear_color = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glClearColor");
    auto clear = Get<void (*)(GLbitfield)>("glClear");
    auto draw_arrays = Get<void (*)(GLenum, GLint, GLsizei)>("glDrawArrays");
    auto read_pixels =
        Get<void (*)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*)>("glReadPixels");

    GLuint vao = 0, buffer = 0;
    gen_vertex_arrays(1, &vao);
    bind_vertex_array(vao);
    gen_buffers(1, &buffer);
    bind_buffer(GL_ARRAY_BUFFER_, buffer);
    static const GLfloat positions[] = {-0.9f, -0.9f, 0.9f, -0.9f, 0.0f, 0.9f};
    buffer_data(GL_ARRAY_BUFFER_, static_cast<GLsizeiptr>(sizeof positions), positions,
                GL_STATIC_DRAW_);
    vertex_attrib_pointer(0, 2, GL_FLOAT_, sfpew_test::GL_FALSE_, 0, nullptr);
    enable_vertex_attrib_array(0);
    disable_vertex_attrib_array(1);
    Get<void (*)(GLuint, GLubyte, GLubyte, GLubyte, GLubyte)>("glVertexAttrib4Nub")(
        1, 0, 255, 0, 255);

    viewport(0, 0, size(), size());
    clear_color(1, 0, 0, 1);
    clear(GL_COLOR_BUFFER_BIT_);
    draw_arrays(GL_TRIANGLES_, 0, 3);
    PixelProbe probe(read_pixels);
    const auto pixel = probe.At(size() / 2, size() / 2);
    EXPECT_TRUE(pixel.g >= 220 && pixel.r <= 30 && pixel.b <= 30)
        << "normalized constant attribute did not reach the shader: (" << (int)pixel.r << ','
        << (int)pixel.g << ',' << (int)pixel.b << ')';
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

} // namespace
