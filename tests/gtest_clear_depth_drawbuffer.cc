// SimpleFPEWrapper - tests/gtest_clear_depth_drawbuffer.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

#include "sfpew_gtest.h"

#include <cstdint>

namespace {

using sfpew_test::ContextTest;
using sfpew_test::DesktopContextTest;
using sfpew_test::GLbitfield;
using sfpew_test::GLboolean;
using sfpew_test::GLdouble;
using sfpew_test::GLenum;
using sfpew_test::GLfloat;
using sfpew_test::GLint;
using sfpew_test::GLsizei;
using sfpew_test::GLubyte;
using sfpew_test::GLuint;

constexpr GLenum GL_NO_ERROR_ = 0;
constexpr GLenum GL_INVALID_ENUM_ = 0x0500;
constexpr GLenum GL_INVALID_OPERATION_ = 0x0502;
constexpr GLenum GL_COLOR_BUFFER_BIT_ = 0x00004000;
constexpr GLenum GL_DEPTH_BUFFER_BIT_ = 0x00000100;
constexpr GLenum GL_DEPTH_TEST_ = 0x0B71;
constexpr GLenum GL_LESS_ = 0x0201;
constexpr GLenum GL_DEPTH_CLEAR_VALUE_ = 0x0B73;
constexpr GLenum GL_DRAW_BUFFER_ = 0x0C01;
constexpr GLenum GL_NONE_ = 0;
constexpr GLenum GL_FRONT_ = 0x0404;
constexpr GLenum GL_BACK_ = 0x0405;
constexpr GLenum GL_TRIANGLES_ = 0x0004;
constexpr GLenum GL_RGBA_ = 0x1908;
constexpr GLenum GL_UNSIGNED_BYTE_ = 0x1401;
constexpr GLenum GL_COMPILE_ = 0x1300;
constexpr GLenum GL_FRAMEBUFFER_ = 0x8D40;
constexpr GLenum GL_COLOR_ATTACHMENT0_ = 0x8CE0;
constexpr GLenum GL_ANY_SAMPLES_PASSED_ = 0x8C2F;
constexpr GLenum GL_QUERY_RESULT_ = 0x8866;
constexpr GLenum GL_QUERY_RESULT_AVAILABLE_ = 0x8867;

struct Api {
    explicit Api(ContextTest* test)
        : clear_depth(test->Get<void (*)(GLdouble)>("glClearDepth")),
          clear_depth_f(test->Get<void (*)(GLfloat)>("glClearDepthf")),
          draw_buffer(test->Get<void (*)(GLenum)>("glDrawBuffer")),
          get_floatv(test->Get<void (*)(GLenum, GLfloat*)>("glGetFloatv")),
          get_integerv(test->Get<void (*)(GLenum, GLint*)>("glGetIntegerv")),
          clear_color(test->Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glClearColor")),
          clear(test->Get<void (*)(GLbitfield)>("glClear")),
          enable(test->Get<void (*)(GLenum)>("glEnable")),
          disable(test->Get<void (*)(GLenum)>("glDisable")),
          depth_func(test->Get<void (*)(GLenum)>("glDepthFunc")),
          begin(test->Get<void (*)(GLenum)>("glBegin")),
          end(test->Get<void (*)()>("glEnd")),
          color3f(test->Get<void (*)(GLfloat, GLfloat, GLfloat)>("glColor3f")),
          vertex3f(test->Get<void (*)(GLfloat, GLfloat, GLfloat)>("glVertex3f")),
          read_pixels(test->Get<void (*)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum,
                                         void*)>("glReadPixels")),
          gen_lists(test->Get<GLuint (*)(GLsizei)>("glGenLists")),
          new_list(test->Get<void (*)(GLuint, GLenum)>("glNewList")),
          end_list(test->Get<void (*)()>("glEndList")),
          call_list(test->Get<void (*)(GLuint)>("glCallList")),
          delete_lists(test->Get<void (*)(GLuint, GLsizei)>("glDeleteLists")),
          gen_framebuffers(test->Get<void (*)(GLsizei, GLuint*)>("glGenFramebuffers")),
          bind_framebuffer(test->Get<void (*)(GLenum, GLuint)>("glBindFramebuffer")),
          delete_framebuffers(
              test->Get<void (*)(GLsizei, const GLuint*)>("glDeleteFramebuffers")),
          gen_queries(test->Get<void (*)(GLsizei, GLuint*)>("glGenQueries")),
          delete_queries(test->Get<void (*)(GLsizei, const GLuint*)>("glDeleteQueries")),
          begin_query(test->Get<void (*)(GLenum, GLuint)>("glBeginQuery")),
          end_query(test->Get<void (*)(GLenum)>("glEndQuery")),
          get_query_uiv(
              test->Get<void (*)(GLuint, GLenum, GLuint*)>("glGetQueryObjectuiv")),
          get_query_iv(test->Get<void (*)(GLuint, GLenum, GLint*)>("glGetQueryObjectiv")),
          get_query_i64v(
              test->Get<void (*)(GLuint, GLenum, std::int64_t*)>("glGetQueryObjecti64v")),
          get_query_ui64v(
              test->Get<void (*)(GLuint, GLenum, std::uint64_t*)>("glGetQueryObjectui64v")),
          finish(test->Get<void (*)()>("glFinish")),
          get_error(test->Get<GLenum (*)()>("glGetError")) {}

    void Drain() const {
        while (get_error() != GL_NO_ERROR_) {}
    }

    GLint Integer(GLenum pname) const {
        GLint value = -1;
        get_integerv(pname, &value);
        return value;
    }

    GLfloat Float(GLenum pname) const {
        GLfloat value = -1.0f;
        get_floatv(pname, &value);
        return value;
    }

    void DrawTriangle() const {
        color3f(0.0f, 1.0f, 0.0f);
        begin(GL_TRIANGLES_);
        vertex3f(-1.0f, -1.0f, 0.0f);
        vertex3f(1.0f, -1.0f, 0.0f);
        vertex3f(0.0f, 1.0f, 0.0f);
        end();
    }

    GLubyte GreenAtCenter() const {
        GLubyte pixel[4] = {};
        read_pixels(32, 24, 1, 1, GL_RGBA_, GL_UNSIGNED_BYTE_, pixel);
        return pixel[1];
    }

    void (*clear_depth)(GLdouble);
    void (*clear_depth_f)(GLfloat);
    void (*draw_buffer)(GLenum);
    void (*get_floatv)(GLenum, GLfloat*);
    void (*get_integerv)(GLenum, GLint*);
    void (*clear_color)(GLfloat, GLfloat, GLfloat, GLfloat);
    void (*clear)(GLbitfield);
    void (*enable)(GLenum);
    void (*disable)(GLenum);
    void (*depth_func)(GLenum);
    void (*begin)(GLenum);
    void (*end)();
    void (*color3f)(GLfloat, GLfloat, GLfloat);
    void (*vertex3f)(GLfloat, GLfloat, GLfloat);
    void (*read_pixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*);
    GLuint (*gen_lists)(GLsizei);
    void (*new_list)(GLuint, GLenum);
    void (*end_list)();
    void (*call_list)(GLuint);
    void (*delete_lists)(GLuint, GLsizei);
    void (*gen_framebuffers)(GLsizei, GLuint*);
    void (*bind_framebuffer)(GLenum, GLuint);
    void (*delete_framebuffers)(GLsizei, const GLuint*);
    void (*gen_queries)(GLsizei, GLuint*);
    void (*delete_queries)(GLsizei, const GLuint*);
    void (*begin_query)(GLenum, GLuint);
    void (*end_query)(GLenum);
    void (*get_query_uiv)(GLuint, GLenum, GLuint*);
    void (*get_query_iv)(GLuint, GLenum, GLint*);
    void (*get_query_i64v)(GLuint, GLenum, std::int64_t*);
    void (*get_query_ui64v)(GLuint, GLenum, std::uint64_t*);
    void (*finish)();
    GLenum (*get_error)();
};

void CheckDepthAndDrawBuffer(ContextTest* test) {
    Api gl(test);
    gl.Drain();

    gl.clear_depth(0.5);
    EXPECT_FLOAT_EQ(gl.Float(GL_DEPTH_CLEAR_VALUE_), 0.5f);
    gl.clear_depth(-4.0);
    EXPECT_FLOAT_EQ(gl.Float(GL_DEPTH_CLEAR_VALUE_), 0.0f);
    gl.clear_depth_f(4.0f);
    EXPECT_FLOAT_EQ(gl.Float(GL_DEPTH_CLEAR_VALUE_), 1.0f);
    EXPECT_EQ(gl.get_error(), GL_NO_ERROR_);

    const GLuint double_list = gl.gen_lists(1);
    const GLuint float_list = gl.gen_lists(1);
    ASSERT_NE(double_list, 0u);
    ASSERT_NE(float_list, 0u);
    gl.clear_depth(0.25);
    gl.new_list(double_list, GL_COMPILE_);
    gl.clear_depth(0.75);
    gl.end_list();
    gl.new_list(float_list, GL_COMPILE_);
    gl.clear_depth_f(0.5f);
    gl.end_list();
    EXPECT_FLOAT_EQ(gl.Float(GL_DEPTH_CLEAR_VALUE_), 0.25f);
    gl.call_list(double_list);
    EXPECT_FLOAT_EQ(gl.Float(GL_DEPTH_CLEAR_VALUE_), 0.75f);
    gl.call_list(float_list);
    EXPECT_FLOAT_EQ(gl.Float(GL_DEPTH_CLEAR_VALUE_), 0.5f);
    gl.delete_lists(double_list, 1);
    gl.delete_lists(float_list, 1);

    gl.draw_buffer(GL_BACK_);
    ASSERT_EQ(gl.get_error(), GL_NO_ERROR_);
    gl.clear_color(0.0f, 0.0f, 0.0f, 1.0f);
    gl.enable(GL_DEPTH_TEST_);
    gl.depth_func(GL_LESS_);
    gl.clear_depth(0.0);
    gl.clear(GL_COLOR_BUFFER_BIT_ | GL_DEPTH_BUFFER_BIT_);
    gl.DrawTriangle();
    EXPECT_LT(gl.GreenAtCenter(), 20u) << "depth clear 0 must reject a z=0 triangle under LESS";
    gl.clear_depth(1.0);
    gl.clear(GL_COLOR_BUFFER_BIT_ | GL_DEPTH_BUFFER_BIT_);
    gl.DrawTriangle();
    EXPECT_GT(gl.GreenAtCenter(), 150u) << "depth clear 1 must admit the same triangle";

    gl.disable(GL_DEPTH_TEST_);
    gl.draw_buffer(GL_BACK_);
    gl.clear(GL_COLOR_BUFFER_BIT_);
    gl.draw_buffer(GL_NONE_);
    gl.DrawTriangle();
    gl.draw_buffer(GL_BACK_);
    EXPECT_LT(gl.GreenAtCenter(), 20u) << "GL_NONE must suppress color writes";
    gl.DrawTriangle();
    EXPECT_GT(gl.GreenAtCenter(), 150u) << "GL_BACK must restore surface color writes";

    gl.draw_buffer(GL_FRONT_);
    EXPECT_EQ(gl.get_error(), GL_NO_ERROR_);
    EXPECT_EQ(gl.Integer(GL_DRAW_BUFFER_), static_cast<GLint>(GL_BACK_));
    gl.draw_buffer(0xDEADu);
    EXPECT_EQ(gl.get_error(), GL_INVALID_ENUM_);

    const GLuint draw_list = gl.gen_lists(1);
    ASSERT_NE(draw_list, 0u);
    gl.new_list(draw_list, GL_COMPILE_);
    gl.draw_buffer(GL_NONE_);
    gl.end_list();
    EXPECT_EQ(gl.Integer(GL_DRAW_BUFFER_), static_cast<GLint>(GL_BACK_));
    gl.call_list(draw_list);
    EXPECT_EQ(gl.Integer(GL_DRAW_BUFFER_), static_cast<GLint>(GL_NONE_));
    gl.delete_lists(draw_list, 1);
    gl.draw_buffer(GL_BACK_);

    GLuint framebuffer = 0;
    gl.gen_framebuffers(1, &framebuffer);
    ASSERT_NE(framebuffer, 0u);
    gl.bind_framebuffer(GL_FRAMEBUFFER_, framebuffer);
    gl.draw_buffer(GL_BACK_);
    EXPECT_EQ(gl.get_error(), GL_INVALID_OPERATION_);
    gl.draw_buffer(GL_COLOR_ATTACHMENT0_);
    EXPECT_EQ(gl.get_error(), GL_NO_ERROR_);
    EXPECT_EQ(gl.Integer(GL_DRAW_BUFFER_), static_cast<GLint>(GL_COLOR_ATTACHMENT0_));
    gl.bind_framebuffer(GL_FRAMEBUFFER_, 0);
    gl.draw_buffer(GL_COLOR_ATTACHMENT0_);
    EXPECT_EQ(gl.get_error(), GL_INVALID_OPERATION_);
    gl.draw_buffer(GL_BACK_);
    EXPECT_EQ(gl.get_error(), GL_NO_ERROR_);
    gl.delete_framebuffers(1, &framebuffer);
}

void CheckSignedQueryObject(ContextTest* test) {
    Api gl(test);
    gl.Drain();

    EXPECT_EQ(test->Get<void*>("glGetQueryObjectivARB"),
              test->Get<void*>("glGetQueryObjectiv"));
    EXPECT_EQ(test->Get<void*>("glGetQueryObjectivEXT"),
              test->Get<void*>("glGetQueryObjectiv"));

    GLuint query = 0;
    gl.gen_queries(1, &query);
    ASSERT_NE(query, 0u);
    gl.begin_query(GL_ANY_SAMPLES_PASSED_, query);
    gl.DrawTriangle();
    gl.end_query(GL_ANY_SAMPLES_PASSED_);
    gl.finish();

    GLuint unsigned_result = 0;
    GLint signed_result = -1;
    gl.get_query_uiv(query, GL_QUERY_RESULT_, &unsigned_result);
    gl.get_query_iv(query, GL_QUERY_RESULT_, &signed_result);
    EXPECT_EQ(signed_result, static_cast<GLint>(unsigned_result));

    GLint available = 0;
    gl.get_query_iv(query, GL_QUERY_RESULT_AVAILABLE_, &available);
    EXPECT_EQ(available, 1);

    std::int64_t signed_64 = -1;
    std::uint64_t unsigned_64 = 0;
    gl.get_query_i64v(query, GL_QUERY_RESULT_, &signed_64);
    gl.get_query_ui64v(query, GL_QUERY_RESULT_, &unsigned_64);
    EXPECT_EQ(signed_64, static_cast<std::int64_t>(unsigned_result));
    EXPECT_EQ(unsigned_64, static_cast<std::uint64_t>(unsigned_result));
    EXPECT_EQ(gl.get_error(), GL_NO_ERROR_);

    signed_result = -77;
    gl.get_query_iv(query, 0xDEADu, &signed_result);
    EXPECT_EQ(gl.get_error(), GL_INVALID_ENUM_);
    EXPECT_EQ(signed_result, -77) << "an errored query must leave the output untouched";

    signed_result = -88;
    gl.get_query_iv(0xFFFFFFFEu, GL_QUERY_RESULT_, &signed_result);
    EXPECT_EQ(gl.get_error(), GL_INVALID_OPERATION_);
    EXPECT_EQ(signed_result, -88) << "an unknown object must leave the output untouched";
    gl.delete_queries(1, &query);
}

class ClearDepthDrawBufferTest : public ContextTest {};
class ClearDepthDrawBufferDesktopTest : public DesktopContextTest {};

TEST_F(ClearDepthDrawBufferTest, DepthAndDrawBufferContractOnGles) {
    CheckDepthAndDrawBuffer(this);
}

TEST_F(ClearDepthDrawBufferDesktopTest, DepthAndDrawBufferContractOnDesktopCore) {
    CheckDepthAndDrawBuffer(this);
}

TEST_F(ClearDepthDrawBufferTest, SignedQueryObjectOnGles) {
    CheckSignedQueryObject(this);
}

TEST_F(ClearDepthDrawBufferDesktopTest, SignedQueryObjectOnDesktopCore) {
    CheckSignedQueryObject(this);
}

} // namespace
