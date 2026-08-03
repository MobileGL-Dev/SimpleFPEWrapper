// SimpleFPEWrapper - tests/gtest_display_list_cpu.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// What display lists owe you before any driver is involved (plans/02
// section D, plans/06 6.2): a list that calls itself has to stop at the
// nesting cap rather than take the native stack down with it, recording
// under GL_COMPILE must not execute what it records, and replaying has to
// land on exactly what executing it directly would have.
//
// All of it is the wrapper's own bookkeeping, so none of it needs a context
// - which is also the point: it has to hold on a machine that has no driver.

#include "sfpew_gtest.h"

namespace {

using sfpew_test::GLenum;
using sfpew_test::GLfloat;
using sfpew_test::GLsizei;
using sfpew_test::GLuint;
using sfpew_test::LibraryTest;

constexpr GLenum GL_COMPILE_ = 0x1300;
constexpr GLenum GL_FOG_DENSITY_ = 0x0B62;
constexpr unsigned GL_FOG_BIT_ = 0x00000080;

class DisplayListCpuTest : public LibraryTest {
protected:
    void SetUp() override {
        LibraryTest::SetUp();
        if (::testing::Test::HasFatalFailure()) return;
        gen_lists_ = Get<GLuint (*)(GLsizei)>("glGenLists");
        new_list_ = Get<void (*)(GLuint, GLenum)>("glNewList");
        end_list_ = Get<void (*)()>("glEndList");
        call_list_ = Get<void (*)(GLuint)>("glCallList");
        fogf_ = Get<void (*)(GLenum, GLfloat)>("glFogf");
        get_floatv_ = Get<void (*)(GLenum, GLfloat*)>("glGetFloatv");
    }

    GLfloat FogDensity() {
        GLfloat value = -1.0f;
        get_floatv_(GL_FOG_DENSITY_, &value);
        return value;
    }

    GLuint (*gen_lists_)(GLsizei) = nullptr;
    void (*new_list_)(GLuint, GLenum) = nullptr;
    void (*end_list_)() = nullptr;
    void (*call_list_)(GLuint) = nullptr;
    void (*fogf_)(GLenum, GLfloat) = nullptr;
    void (*get_floatv_)(GLenum, GLfloat*) = nullptr;
};

TEST_F(DisplayListCpuTest, SelfReferentialListStopsAtTheNestingCap) {
    const GLuint list = gen_lists_(1);
    ASSERT_NE(list, 0u) << "glGenLists(1) returned 0";

    new_list_(list, GL_COMPILE_);
    call_list_(list); // recorded: the list calls itself
    end_list_();

    // The assertion is reaching the next line: unbounded recursion here took
    // the native stack with it.
    call_list_(list);
    SUCCEED() << "self-referential display list terminated at the nesting cap";
}

TEST_F(DisplayListCpuTest, CompileRecordsWithoutExecuting) {
    fogf_(GL_FOG_DENSITY_, 0.25f);

    const GLuint list = gen_lists_(1);
    new_list_(list, GL_COMPILE_);
    fogf_(GL_FOG_DENSITY_, 0.75f);
    end_list_();

    EXPECT_FLOAT_EQ(FogDensity(), 0.25f) << "GL_COMPILE executed the command it recorded";
}

TEST_F(DisplayListCpuTest, ReplayAppliesAndEqualsImmediateExecution) {
    fogf_(GL_FOG_DENSITY_, 0.25f);
    const GLuint list = gen_lists_(1);
    new_list_(list, GL_COMPILE_);
    fogf_(GL_FOG_DENSITY_, 0.75f);
    end_list_();

    call_list_(list);
    const GLfloat replayed = FogDensity();
    EXPECT_FLOAT_EQ(replayed, 0.75f) << "replay did not apply the recorded command";

    // Equivalence: executing the same command directly lands on the same
    // value, which is the property a display list is supposed to have.
    fogf_(GL_FOG_DENSITY_, 0.1f);
    fogf_(GL_FOG_DENSITY_, 0.75f);
    EXPECT_FLOAT_EQ(FogDensity(), replayed) << "replay and immediate execution disagree";
}

TEST_F(DisplayListCpuTest, FogBitRestoresFogParametersExactly) {
    auto push_attrib = Get<void (*)(unsigned)>("glPushAttrib");
    auto pop_attrib = Get<void (*)()>("glPopAttrib");
    ASSERT_NE(push_attrib, nullptr);
    ASSERT_NE(pop_attrib, nullptr);

    fogf_(GL_FOG_DENSITY_, 0.33f);
    push_attrib(GL_FOG_BIT_);
    fogf_(GL_FOG_DENSITY_, 0.9f);
    pop_attrib();

    EXPECT_FLOAT_EQ(FogDensity(), 0.33f) << "glPopAttrib(GL_FOG_BIT) did not restore fog state";
}

} // namespace
