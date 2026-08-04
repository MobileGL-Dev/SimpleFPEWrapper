// SimpleFPEWrapper - tests/gtest_logicop.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// defects-plan.md 1.6: glLogicOp did not exist anywhere in the tree, and
// GL_LOGIC_OP_MODE was hardcoded to GL_COPY. ES 3.0 core removed
// fixed-function logic ops entirely and there is no extension-free way to
// emulate one in a fragment shader on that floor, so this is honest state
// tracking only, the same pattern already used for texture residency:
// glLogicOp itself is real (validates, stores, records into display lists),
// but GL_COLOR_LOGIC_OP/GL_INDEX_LOGIC_OP deliberately keep reporting
// disabled rather than advertise a render effect that is not present.

#include "sfpew_gtest.h"

namespace {

using sfpew_test::ContextTest;
using sfpew_test::GLenum;
using sfpew_test::GLint;
using sfpew_test::GLsizei;
using sfpew_test::GLuint;
using sfpew_test::LibraryTest;

constexpr GLenum GL_NO_ERROR_ = 0;
constexpr GLenum GL_INVALID_ENUM_ = 0x0500;
constexpr GLenum GL_CLEAR_ = 0x1500;
constexpr GLenum GL_COPY_ = 0x1503;
constexpr GLenum GL_XOR_ = 0x1506;
constexpr GLenum GL_SET_ = 0x150F;
constexpr GLenum GL_LOGIC_OP_MODE_ = 0x0BF0;
constexpr GLenum GL_COLOR_LOGIC_OP_ = 0x0BF2;
constexpr GLenum GL_INDEX_LOGIC_OP_ = 0x0BF1;
constexpr GLenum GL_COMPILE_ = 0x1300;

class LogicOpTest : public ContextTest {
protected:
    void SetUp() override {
        ContextTest::SetUp();
        if (::testing::Test::IsSkipped()) return;
        logic_op_ = Get<void (*)(GLenum)>("glLogicOp");
        get_integerv_ = Get<void (*)(GLenum, GLint*)>("glGetIntegerv");
        get_error_ = Get<GLenum (*)()>("glGetError");
        enable_ = Get<void (*)(GLenum)>("glEnable");
        is_enabled_ = Get<sfpew_test::GLboolean (*)(GLenum)>("glIsEnabled");
        ASSERT_NE(logic_op_, nullptr);
    }

    GLenum ModeNow() {
        GLint value = 0;
        get_integerv_(GL_LOGIC_OP_MODE_, &value);
        return static_cast<GLenum>(value);
    }

    void (*logic_op_)(GLenum) = nullptr;
    void (*get_integerv_)(GLenum, GLint*) = nullptr;
    GLenum (*get_error_)() = nullptr;
    void (*enable_)(GLenum) = nullptr;
    sfpew_test::GLboolean (*is_enabled_)(GLenum) = nullptr;
};

TEST_F(LogicOpTest, StateRoundTripsThroughEveryLegalOpcode) {
    EXPECT_EQ(ModeNow(), GL_COPY_) << "GL_COPY is the GL default";
    logic_op_(GL_XOR_);
    EXPECT_EQ(ModeNow(), GL_XOR_);
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
    logic_op_(GL_CLEAR_);
    EXPECT_EQ(ModeNow(), GL_CLEAR_);
    logic_op_(GL_SET_);
    EXPECT_EQ(ModeNow(), GL_SET_);
}

TEST_F(LogicOpTest, InvalidOpcodeRaisesInvalidEnumAndLeavesStateAlone) {
    logic_op_(GL_XOR_);
    ASSERT_EQ(ModeNow(), GL_XOR_);
    logic_op_(static_cast<GLenum>(0xDEAD));
    EXPECT_EQ(get_error_(), GL_INVALID_ENUM_);
    EXPECT_EQ(ModeNow(), GL_XOR_) << "a rejected opcode must not change the stored mode";
}

TEST_F(LogicOpTest, EnablingTheCapabilityStillReportsDisabled) {
    // Deliberate, documented boundary - not a bug. See the file header.
    enable_(GL_COLOR_LOGIC_OP_);
    EXPECT_EQ(is_enabled_(GL_COLOR_LOGIC_OP_), sfpew_test::GL_FALSE_);
    enable_(GL_INDEX_LOGIC_OP_);
    EXPECT_EQ(is_enabled_(GL_INDEX_LOGIC_OP_), sfpew_test::GL_FALSE_);
}

TEST_F(LogicOpTest, CompilesIntoAndReplaysFromADisplayList) {
    auto gen_lists = Get<GLuint (*)(GLsizei)>("glGenLists");
    auto new_list = Get<void (*)(GLuint, GLenum)>("glNewList");
    auto end_list = Get<void (*)()>("glEndList");
    auto call_list = Get<void (*)(GLuint)>("glCallList");
    const GLuint list = gen_lists(1);
    ASSERT_NE(list, 0u);

    logic_op_(GL_COPY_);
    new_list(list, GL_COMPILE_);
    logic_op_(GL_XOR_);
    end_list();
    EXPECT_EQ(ModeNow(), GL_COPY_) << "compiling must not execute the command";
    call_list(list);
    EXPECT_EQ(ModeNow(), GL_XOR_) << "replay must apply it";
}

TEST_F(LibraryTest, ResolvesWithoutAContext) {
    auto logic_op = Get<void (*)(GLenum)>("glLogicOp");
    EXPECT_NE(logic_op, nullptr);
}

} // namespace
