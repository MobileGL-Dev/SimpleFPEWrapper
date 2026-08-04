// SimpleFPEWrapper - tests/gtest_pushattrib_depth_stencil_scissor.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// defects-plan.md 1.1: glPushAttrib/glPopAttrib did not capture depth,
// stencil or scissor state at all, even though the shadow they need
// (fixed_function_state_t::backend_state) was already tracked for every
// draw. Covers: each bit restoring in isolation, an un-pushed bit's changes
// surviving a pop (proves the mask actually gates what restores), and one
// render-backed case proving GL_DEPTH_TEST's enable really comes back.

#include "sfpew_gtest.h"

#include <optional>

namespace {

using sfpew_test::ContextTest;
using sfpew_test::GLbitfield;
using sfpew_test::GLboolean;
using sfpew_test::GLenum;
using sfpew_test::GLfloat;
using sfpew_test::GLint;
using sfpew_test::GLsizei;
using sfpew_test::GLuint;
using sfpew_test::PixelProbe;

constexpr GLbitfield GL_DEPTH_BUFFER_BIT_ = 0x00000100;
constexpr GLbitfield GL_STENCIL_BUFFER_BIT_ = 0x00000400;
constexpr GLbitfield GL_COLOR_BUFFER_BIT_ = 0x00004000;
constexpr GLbitfield GL_SCISSOR_BIT_ = 0x00080000;
constexpr GLbitfield GL_ENABLE_BIT_ = 0x00002000;
constexpr GLbitfield GL_ALL_ATTRIB_BITS_ = 0xFFFFFFFFu;
constexpr GLenum GL_DEPTH_TEST_ = 0x0B71;
constexpr GLenum GL_STENCIL_TEST_ = 0x0B90;
constexpr GLenum GL_SCISSOR_TEST_ = 0x0C11;
constexpr GLenum GL_DEPTH_FUNC_ = 0x0B74;
constexpr GLenum GL_DEPTH_WRITEMASK_ = 0x0B72;
constexpr GLenum GL_STENCIL_FUNC_ = 0x0B92;
constexpr GLenum GL_STENCIL_REF_ = 0x0B97;
constexpr GLenum GL_STENCIL_FAIL_ = 0x0B94;
constexpr GLenum GL_SCISSOR_BOX_ = 0x0C10;
constexpr GLenum GL_ALWAYS_ = 0x0207;
constexpr GLenum GL_LESS_ = 0x0201;
constexpr GLenum GL_NOTEQUAL_ = 0x0205;
constexpr GLenum GL_KEEP_ = 0x1E00;
constexpr GLenum GL_REPLACE_ = 0x1E01;
constexpr GLenum GL_TRIANGLES_ = 0x0004;

class PushAttribDepthStencilScissorTest : public ContextTest {
protected:
    PushAttribDepthStencilScissorTest() : ContextTest(sfpew_test::Backend::GLES3, 32) {}

    void SetUp() override {
        ContextTest::SetUp();
        if (::testing::Test::IsSkipped()) return;

        push_attrib_ = Get<void (*)(GLbitfield)>("glPushAttrib");
        pop_attrib_ = Get<void (*)()>("glPopAttrib");
        enable_ = Get<void (*)(GLenum)>("glEnable");
        disable_ = Get<void (*)(GLenum)>("glDisable");
        is_enabled_ = Get<GLboolean (*)(GLenum)>("glIsEnabled");
        depth_func_ = Get<void (*)(GLenum)>("glDepthFunc");
        depth_mask_ = Get<void (*)(GLboolean)>("glDepthMask");
        stencil_func_ = Get<void (*)(GLenum, GLint, GLuint)>("glStencilFunc");
        stencil_op_ = Get<void (*)(GLenum, GLenum, GLenum)>("glStencilOp");
        scissor_ = Get<void (*)(GLint, GLint, GLint, GLint)>("glScissor");
        get_integer_ = Get<void (*)(GLenum, GLint*)>("glGetIntegerv");
        get_boolean_ = Get<void (*)(GLenum, GLboolean*)>("glGetBooleanv");
        clear_color_ = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glClearColor");
        clear_depth_ = Get<void (*)(GLfloat)>("glClearDepthf");
        clear_ = Get<void (*)(GLbitfield)>("glClear");
        begin_ = Get<void (*)(GLenum)>("glBegin");
        end_ = Get<void (*)()>("glEnd");
        color4f_ = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glColor4f");
        vertex3f_ = Get<void (*)(GLfloat, GLfloat, GLfloat)>("glVertex3f");
        auto read_pixels =
            Get<void (*)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*)>("glReadPixels");
        ASSERT_NE(read_pixels, nullptr);
        probe_.emplace(read_pixels);
    }

    void (*push_attrib_)(GLbitfield) = nullptr;
    void (*pop_attrib_)() = nullptr;
    void (*enable_)(GLenum) = nullptr;
    void (*disable_)(GLenum) = nullptr;
    GLboolean (*is_enabled_)(GLenum) = nullptr;
    void (*depth_func_)(GLenum) = nullptr;
    void (*depth_mask_)(GLboolean) = nullptr;
    void (*stencil_func_)(GLenum, GLint, GLuint) = nullptr;
    void (*stencil_op_)(GLenum, GLenum, GLenum) = nullptr;
    void (*scissor_)(GLint, GLint, GLint, GLint) = nullptr;
    void (*get_integer_)(GLenum, GLint*) = nullptr;
    void (*get_boolean_)(GLenum, GLboolean*) = nullptr;
    void (*clear_color_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*clear_depth_)(GLfloat) = nullptr;
    void (*clear_)(GLbitfield) = nullptr;
    void (*begin_)(GLenum) = nullptr;
    void (*end_)() = nullptr;
    void (*color4f_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*vertex3f_)(GLfloat, GLfloat, GLfloat) = nullptr;
    std::optional<PixelProbe> probe_;
};

TEST_F(PushAttribDepthStencilScissorTest, DepthBufferBitRestoresFuncAndMaskNotEnable) {
    depth_func_(GL_LESS_);
    depth_mask_(sfpew_test::GL_TRUE_);
    enable_(GL_DEPTH_TEST_);

    push_attrib_(GL_DEPTH_BUFFER_BIT_);
    depth_func_(GL_ALWAYS_);
    depth_mask_(sfpew_test::GL_FALSE_);
    disable_(GL_DEPTH_TEST_); // NOT part of GL_DEPTH_BUFFER_BIT - must survive the pop
    pop_attrib_();

    GLint func = 0;
    GLboolean mask = sfpew_test::GL_FALSE_;
    get_integer_(GL_DEPTH_FUNC_, &func);
    get_boolean_(GL_DEPTH_WRITEMASK_, &mask);
    EXPECT_EQ(func, static_cast<GLint>(GL_LESS_));
    EXPECT_EQ(mask, sfpew_test::GL_TRUE_);
    EXPECT_EQ(is_enabled_(GL_DEPTH_TEST_), sfpew_test::GL_FALSE_)
        << "GL_DEPTH_TEST's enable belongs to GL_ENABLE_BIT, not GL_DEPTH_BUFFER_BIT";
}

TEST_F(PushAttribDepthStencilScissorTest, StencilBufferBitRestoresFuncRefAndOps) {
    stencil_func_(GL_ALWAYS_, 0, 0xFFu);
    stencil_op_(GL_KEEP_, GL_KEEP_, GL_KEEP_);

    push_attrib_(GL_STENCIL_BUFFER_BIT_);
    stencil_func_(GL_NOTEQUAL_, 7, 0x0Fu);
    stencil_op_(GL_REPLACE_, GL_REPLACE_, GL_REPLACE_);
    pop_attrib_();

    GLint func = 0, ref = -1, fail = 0;
    get_integer_(GL_STENCIL_FUNC_, &func);
    get_integer_(GL_STENCIL_REF_, &ref);
    get_integer_(GL_STENCIL_FAIL_, &fail);
    EXPECT_EQ(func, static_cast<GLint>(GL_ALWAYS_));
    EXPECT_EQ(ref, 0);
    EXPECT_EQ(fail, static_cast<GLint>(GL_KEEP_));
}

TEST_F(PushAttribDepthStencilScissorTest, ScissorBitRestoresTheBoxNotTheEnable) {
    scissor_(1, 2, 3, 4);
    enable_(GL_SCISSOR_TEST_);

    push_attrib_(GL_SCISSOR_BIT_);
    scissor_(10, 20, 30, 40);
    disable_(GL_SCISSOR_TEST_);
    pop_attrib_();

    GLint box[4] = {};
    get_integer_(GL_SCISSOR_BOX_, box);
    EXPECT_EQ(box[0], 1);
    EXPECT_EQ(box[1], 2);
    EXPECT_EQ(box[2], 3);
    EXPECT_EQ(box[3], 4);
    EXPECT_EQ(is_enabled_(GL_SCISSOR_TEST_), sfpew_test::GL_FALSE_)
        << "GL_SCISSOR_TEST's enable belongs to GL_ENABLE_BIT, not GL_SCISSOR_BIT";
}

TEST_F(PushAttribDepthStencilScissorTest, EnableBitAloneRestoresTheThreeEnablesNotTheValues) {
    depth_func_(GL_LESS_);
    enable_(GL_DEPTH_TEST_);
    enable_(GL_STENCIL_TEST_);
    disable_(GL_SCISSOR_TEST_);

    push_attrib_(GL_ENABLE_BIT_);
    depth_func_(GL_ALWAYS_); // NOT part of GL_ENABLE_BIT - must survive the pop
    disable_(GL_DEPTH_TEST_);
    disable_(GL_STENCIL_TEST_);
    enable_(GL_SCISSOR_TEST_);
    pop_attrib_();

    EXPECT_EQ(is_enabled_(GL_DEPTH_TEST_), sfpew_test::GL_TRUE_);
    EXPECT_EQ(is_enabled_(GL_STENCIL_TEST_), sfpew_test::GL_TRUE_);
    EXPECT_EQ(is_enabled_(GL_SCISSOR_TEST_), sfpew_test::GL_FALSE_);
    GLint func = 0;
    get_integer_(GL_DEPTH_FUNC_, &func);
    EXPECT_EQ(func, static_cast<GLint>(GL_ALWAYS_))
        << "GL_DEPTH_FUNC belongs to GL_DEPTH_BUFFER_BIT, not GL_ENABLE_BIT";
}

TEST_F(PushAttribDepthStencilScissorTest, AllAttribBitsRestoresEverythingTogether) {
    depth_func_(GL_LESS_);
    depth_mask_(sfpew_test::GL_TRUE_);
    enable_(GL_DEPTH_TEST_);
    stencil_func_(GL_ALWAYS_, 0, 0xFFu);
    enable_(GL_STENCIL_TEST_);
    scissor_(5, 6, 7, 8);
    disable_(GL_SCISSOR_TEST_);

    push_attrib_(GL_ALL_ATTRIB_BITS_);
    depth_func_(GL_ALWAYS_);
    depth_mask_(sfpew_test::GL_FALSE_);
    disable_(GL_DEPTH_TEST_);
    stencil_func_(GL_NOTEQUAL_, 3, 0x0Fu);
    disable_(GL_STENCIL_TEST_);
    scissor_(50, 60, 70, 80);
    enable_(GL_SCISSOR_TEST_);
    pop_attrib_();

    GLint func = 0, box[4] = {};
    GLboolean mask = sfpew_test::GL_FALSE_;
    get_integer_(GL_DEPTH_FUNC_, &func);
    get_boolean_(GL_DEPTH_WRITEMASK_, &mask);
    get_integer_(GL_SCISSOR_BOX_, box);
    EXPECT_EQ(func, static_cast<GLint>(GL_LESS_));
    EXPECT_EQ(mask, sfpew_test::GL_TRUE_);
    EXPECT_EQ(is_enabled_(GL_DEPTH_TEST_), sfpew_test::GL_TRUE_);
    EXPECT_EQ(is_enabled_(GL_STENCIL_TEST_), sfpew_test::GL_TRUE_);
    EXPECT_EQ(is_enabled_(GL_SCISSOR_TEST_), sfpew_test::GL_FALSE_);
    EXPECT_EQ(box[0], 5);
    EXPECT_EQ(box[1], 6);
    EXPECT_EQ(box[2], 7);
    EXPECT_EQ(box[3], 8);
}

TEST_F(PushAttribDepthStencilScissorTest, RenderedDepthTestEnableReallyComesBack) {
    clear_color_(0.0f, 0.0f, 0.0f, 1.0f);
    clear_depth_(1.0f);
    clear_(GL_COLOR_BUFFER_BIT_ | GL_DEPTH_BUFFER_BIT_);
    depth_func_(GL_LESS_);
    enable_(GL_DEPTH_TEST_);

    push_attrib_(GL_ENABLE_BIT_);
    disable_(GL_DEPTH_TEST_);

    // A far quad drawn AFTER a near one, with depth testing off: it must
    // paint over the near one since nothing is testing depth.
    color4f_(0.0f, 1.0f, 0.0f, 1.0f);
    begin_(GL_TRIANGLES_);
    vertex3f_(-1.0f, -1.0f, -0.5f);
    vertex3f_(1.0f, -1.0f, -0.5f);
    vertex3f_(0.0f, 1.0f, -0.5f);
    end_();
    color4f_(1.0f, 0.0f, 0.0f, 1.0f);
    begin_(GL_TRIANGLES_);
    vertex3f_(-1.0f, -1.0f, 0.5f);
    vertex3f_(1.0f, -1.0f, 0.5f);
    vertex3f_(0.0f, 1.0f, 0.5f);
    end_();
    const auto with_test_off = probe_->At(16, 12);
    EXPECT_GT(with_test_off.r, 200) << "depth test off: the later (farther) triangle wins";

    pop_attrib_(); // GL_DEPTH_TEST enable must come back on

    clear_(GL_COLOR_BUFFER_BIT_ | GL_DEPTH_BUFFER_BIT_);
    color4f_(0.0f, 1.0f, 0.0f, 1.0f);
    begin_(GL_TRIANGLES_);
    vertex3f_(-1.0f, -1.0f, -0.5f);
    vertex3f_(1.0f, -1.0f, -0.5f);
    vertex3f_(0.0f, 1.0f, -0.5f);
    end_();
    color4f_(1.0f, 0.0f, 0.0f, 1.0f);
    begin_(GL_TRIANGLES_);
    vertex3f_(-1.0f, -1.0f, 0.5f);
    vertex3f_(1.0f, -1.0f, 0.5f);
    vertex3f_(0.0f, 1.0f, 0.5f);
    end_();
    const auto with_test_restored = probe_->At(16, 12);
    EXPECT_GT(with_test_restored.g, 200)
        << "depth test restored by the pop: the nearer (green) triangle must win";
}

} // namespace
