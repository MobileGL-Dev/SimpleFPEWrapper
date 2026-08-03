// SimpleFPEWrapper - tests/gtest_list_current_attr.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// Display-list current-attribute semantics against the glEndList run
// compiler: (1) a compiled Begin/End run that never sets color must
// inherit the CURRENT color at glCallList time (GL 2.1: current state
// applies), and (2) a glColor executed FROM a list must update the
// current color so a later immediate draw uses it. Both regressed once
// during the run-compiler bring-up; this locks them in.
//
// One test, both phases, in one process - exactly the order the original
// smoke test ran them in. The phases deliberately share a process: phase 2
// exercises the state left behind by phase 1's replay, and splitting them
// into separate TESTs (and therefore separate processes, by the suite's
// process-per-test rule) would test phase 2 against a fresh context, which
// is a different scenario from the one this test documents.

#include "sfpew_gtest.h"

#include <optional>

namespace {

using sfpew_test::ContextTest;
using sfpew_test::GLbitfield;
using sfpew_test::GLenum;
using sfpew_test::GLfloat;
using sfpew_test::GLint;
using sfpew_test::GLsizei;
using sfpew_test::GLuint;
using sfpew_test::PixelProbe;

constexpr GLbitfield GL_COLOR_BUFFER_BIT_ = 0x00004000;
constexpr GLenum GL_TRIANGLES_ = 0x0004;
constexpr GLenum GL_QUADS_ = 0x0007;
constexpr GLenum GL_COMPILE_ = 0x1300;
constexpr GLenum GL_NO_ERROR_ = 0;

class ListCurrentAttrTest : public ContextTest {
protected:
    void SetUp() override {
        ContextTest::SetUp();
        if (::testing::Test::IsSkipped()) return;

        clear_color_ = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glClearColor");
        clear_ = Get<void (*)(GLbitfield)>("glClear");
        begin_ = Get<void (*)(GLenum)>("glBegin");
        end_ = Get<void (*)()>("glEnd");
        color3f_ = Get<void (*)(GLfloat, GLfloat, GLfloat)>("glColor3f");
        vertex2f_ = Get<void (*)(GLfloat, GLfloat)>("glVertex2f");
        gen_lists_ = Get<GLuint (*)(GLsizei)>("glGenLists");
        new_list_ = Get<void (*)(GLuint, GLenum)>("glNewList");
        end_list_ = Get<void (*)()>("glEndList");
        call_list_ = Get<void (*)(GLuint)>("glCallList");
        get_error_ = Get<GLenum (*)()>("glGetError");
        auto read_pixels =
            Get<void (*)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*)>("glReadPixels");
        ASSERT_NE(read_pixels, nullptr);
        probe_.emplace(read_pixels);
    }

    // r/g/b are booleans, exactly like the original C helper: nonzero means
    // "that channel must be high (>= 200)", zero means "must be low (<= 50)".
    // -1 means don't care.
    void ExpectPixel(int r, int g, int b, const char* what) {
        const PixelProbe::Rgba p = probe_->At(32, 32);
        const bool ok = (r < 0 || (p.r >= 200) == (r != 0)) &&
                        (g < 0 || (p.g >= 200) == (g != 0)) &&
                        (b < 0 || (p.b >= 200) == (b != 0));
        EXPECT_TRUE(ok) << what << ": pixel (" << (int)p.r << ',' << (int)p.g << ',' << (int)p.b
                        << ") expected (" << r << ',' << g << ',' << b << ')';
    }

    void (*clear_color_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*clear_)(GLbitfield) = nullptr;
    void (*begin_)(GLenum) = nullptr;
    void (*end_)() = nullptr;
    void (*color3f_)(GLfloat, GLfloat, GLfloat) = nullptr;
    void (*vertex2f_)(GLfloat, GLfloat) = nullptr;
    GLuint (*gen_lists_)(GLsizei) = nullptr;
    void (*new_list_)(GLuint, GLenum) = nullptr;
    void (*end_list_)() = nullptr;
    void (*call_list_)(GLuint) = nullptr;
    GLenum (*get_error_)() = nullptr;
    std::optional<PixelProbe> probe_;
};

TEST_F(ListCurrentAttrTest, RunCompilerPreservesCurrentAttributeSemantics) {
    clear_color_(0.0f, 0.0f, 0.0f, 1.0f);
    get_error_();

    // --- Phase 1: a compiled run that never sets color inherits the
    // call-time current color. ---
    const GLuint colorless = gen_lists_(1);
    new_list_(colorless, GL_COMPILE_);
    begin_(GL_QUADS_);
    vertex2f_(-1.0f, -1.0f);
    vertex2f_(1.0f, -1.0f);
    vertex2f_(1.0f, 1.0f);
    vertex2f_(-1.0f, 1.0f);
    end_();
    end_list_();

    clear_(GL_COLOR_BUFFER_BIT_);
    color3f_(1.0f, 0.0f, 0.0f); // current color at call time: red
    call_list_(colorless);
    ExpectPixel(1, 0, 0, "colorless list with red current color");

    color3f_(0.0f, 0.0f, 1.0f); // and again with blue: the list must follow
    call_list_(colorless);
    ExpectPixel(0, 0, 1, "colorless list with blue current color");

    // --- Phase 2: a glColor executed FROM a list stays current after
    // glCallList, so a later immediate draw uses it. ---
    const GLuint greening = gen_lists_(1);
    new_list_(greening, GL_COMPILE_);
    begin_(GL_TRIANGLES_);
    color3f_(0.0f, 1.0f, 0.0f);
    vertex2f_(-1.0f, -1.0f);
    vertex2f_(1.0f, -1.0f);
    vertex2f_(0.0f, 1.0f);
    end_();
    end_list_();

    color3f_(1.0f, 0.0f, 0.0f); // red before the call
    call_list_(greening);       // list paints green and leaves green current

    clear_(GL_COLOR_BUFFER_BIT_);
    begin_(GL_QUADS_); // colorless immediate quad: must come out green
    vertex2f_(-1.0f, -1.0f);
    vertex2f_(1.0f, -1.0f);
    vertex2f_(1.0f, 1.0f);
    vertex2f_(-1.0f, 1.0f);
    end_();
    ExpectPixel(0, 1, 0, "immediate quad after green-setting list");

    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

} // namespace
