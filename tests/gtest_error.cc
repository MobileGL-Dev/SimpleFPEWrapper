// SimpleFPEWrapper - tests/gtest_error.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// The wrapper's GL error machine (plans/02 section A), which is entirely its
// own: an invalid call latches, glGetError hands the error over exactly once
// and only the FIRST unread one survives. None of it needs a driver, and it
// has to work without one - a legacy frontend checks glGetError long before
// it has drawn anything.

#include "sfpew_gtest.h"

namespace {

using sfpew_test::GLenum;
using sfpew_test::GLsizei;
using sfpew_test::GLuint;
using sfpew_test::LibraryTest;

constexpr GLenum GL_NO_ERROR_ = 0;
constexpr GLenum GL_INVALID_ENUM_ = 0x0500;
constexpr GLenum GL_INVALID_VALUE_ = 0x0501;
constexpr GLenum GL_INVALID_OPERATION_ = 0x0502;
constexpr GLenum GL_STACK_OVERFLOW_ = 0x0503;
constexpr GLenum GL_STACK_UNDERFLOW_ = 0x0504;
constexpr GLenum GL_QUADS_ = 0x0007;

class ErrorTest : public LibraryTest {
protected:
    void SetUp() override {
        LibraryTest::SetUp();
        if (::testing::Test::HasFatalFailure()) return;
        get_error_ = Get<GLenum (*)()>("glGetError");
    }

    GLenum TakeError() { return get_error_(); }
    void DrainErrors() {
        while (get_error_() != GL_NO_ERROR_) {}
    }

    GLenum (*get_error_)() = nullptr;
};

TEST_F(ErrorTest, StartsClean) {
    EXPECT_EQ(TakeError(), GL_NO_ERROR_) << "an untouched context already had an error";
}

TEST_F(ErrorTest, FirstErrorWinsAndIsHandedOverOnce) {
    auto delete_lists = Get<void (*)(GLuint, GLsizei)>("glDeleteLists");
    auto gen_lists = Get<GLuint (*)(GLsizei)>("glGenLists");
    ASSERT_NE(delete_lists, nullptr);
    ASSERT_NE(gen_lists, nullptr);

    delete_lists(1, -1); // GL_INVALID_VALUE
    gen_lists(-5);       // would latch a second; the first has to win

    EXPECT_EQ(TakeError(), GL_INVALID_VALUE_);
    EXPECT_EQ(TakeError(), GL_NO_ERROR_) << "the error was not cleared when it was read";
}

TEST_F(ErrorTest, ZeroCountIsNotAnError) {
    auto gen_lists = Get<GLuint (*)(GLsizei)>("glGenLists");
    ASSERT_NE(gen_lists, nullptr);
    DrainErrors();

    EXPECT_EQ(gen_lists(0), 0u) << "glGenLists(0) must return 0";
    EXPECT_EQ(TakeError(), GL_NO_ERROR_) << "glGenLists(0) is legal and must not set an error";
}

// The rest is one table of "this call, that error". Each row is independent
// and drains first, so a row that fails does not spoil the next.
TEST_F(ErrorTest, MatrixStackContract) {
    auto matrix_mode = Get<void (*)(GLenum)>("glMatrixMode");
    auto push_matrix = Get<void (*)()>("glPushMatrix");
    auto pop_matrix = Get<void (*)()>("glPopMatrix");
    ASSERT_NE(matrix_mode, nullptr);
    ASSERT_NE(push_matrix, nullptr);
    ASSERT_NE(pop_matrix, nullptr);
    DrainErrors();

    matrix_mode(0x1234);
    EXPECT_EQ(TakeError(), GL_INVALID_ENUM_) << "glMatrixMode(bad enum)";

    pop_matrix();
    EXPECT_EQ(TakeError(), GL_STACK_UNDERFLOW_) << "glPopMatrix on an empty stack";

    for (int i = 0; i < 64; ++i) push_matrix(); // the modelview cap
    push_matrix();
    EXPECT_EQ(TakeError(), GL_STACK_OVERFLOW_) << "glPushMatrix past the cap";
    for (int i = 0; i < 64; ++i) pop_matrix();
    DrainErrors();
}

TEST_F(ErrorTest, BeginEndContract) {
    auto begin = Get<void (*)(GLenum)>("glBegin");
    auto end = Get<void (*)()>("glEnd");
    ASSERT_NE(begin, nullptr);
    ASSERT_NE(end, nullptr);
    DrainErrors();

    begin(0x1234);
    EXPECT_EQ(TakeError(), GL_INVALID_ENUM_) << "glBegin(bad mode)";

    end();
    EXPECT_EQ(TakeError(), GL_INVALID_OPERATION_) << "glEnd with no glBegin";

    begin(GL_QUADS_);
    begin(GL_QUADS_);
    EXPECT_EQ(TakeError(), GL_INVALID_OPERATION_) << "nested glBegin";
    end();
    DrainErrors();
}

TEST_F(ErrorTest, RemainingStacksAndEnumsContract) {
    auto alpha_func = Get<void (*)(GLenum, float)>("glAlphaFunc");
    auto pop_attrib = Get<void (*)()>("glPopAttrib");
    auto pop_name = Get<void (*)()>("glPopName");
    auto tex_geni = Get<void (*)(GLenum, GLenum, int)>("glTexGeni");
    ASSERT_NE(alpha_func, nullptr);
    ASSERT_NE(pop_attrib, nullptr);
    ASSERT_NE(pop_name, nullptr);
    ASSERT_NE(tex_geni, nullptr);
    DrainErrors();

    alpha_func(0x1234, 0.5f);
    EXPECT_EQ(TakeError(), GL_INVALID_ENUM_) << "glAlphaFunc(bad func)";

    pop_attrib();
    EXPECT_EQ(TakeError(), GL_STACK_UNDERFLOW_) << "glPopAttrib on an empty stack";

    pop_name();
    EXPECT_EQ(TakeError(), GL_STACK_UNDERFLOW_) << "glPopName on an empty stack";

    tex_geni(0x2000 /* GL_S */, 0x2500 /* GL_TEXTURE_GEN_MODE */, 0x1234);
    EXPECT_EQ(TakeError(), GL_INVALID_ENUM_) << "glTexGeni(bad mode)";

    tex_geni(0x1234, 0x2500, 0x2400);
    EXPECT_EQ(TakeError(), GL_INVALID_ENUM_) << "glTexGeni(bad coord)";
}

} // namespace
