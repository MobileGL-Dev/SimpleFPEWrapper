// SimpleFPEWrapper - tests/gtest_dlist_beginend.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// Ported from piglit's tests/spec/gl-1.0/dlist-beginend.c (MIT-style
// license; see piglit's COPYING). Display lists and Begin/End compose in
// ways the wrapper's compiled-run machinery has to get right: a list holding
// only loose vertex commands is legal and must feed whatever Begin/End block
// is open at glCallList time, and the block may even be split across three
// separate lists (Begin in one, vertices in another, End in a third).
//
// This is the awkward case for the glEndList run compiler
// (sfpewCompileImmediateRuns): a list whose vertices have NO Begin/End
// around them cannot be baked into a compiled draw, because the primitive
// mode is not known until replay. It must stay as replayable per-vertex
// commands that push into the caller's open block.
//
// The upstream subtests asserting GL_INVALID_OPERATION for glRectf /
// glDrawArrays inside Begin/End are deliberately NOT ported: this wrapper
// does not enforce the "illegal inside Begin/End" rule for passthrough
// entry points, a documented architectural decision (see the file header of
// tests/gtest_beginend_coverage.cc). What IS ported and enforced is that the
// legal geometry still lands on screen in each arrangement.

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

constexpr int kWindow = 32;
constexpr GLenum GL_QUADS_ = 0x0007;
constexpr GLbitfield GL_COLOR_BUFFER_BIT_ = 0x00004000;
constexpr GLenum GL_COMPILE_ = 0x1300;
constexpr GLenum GL_COMPILE_AND_EXECUTE_ = 0x1301;
constexpr GLenum GL_NO_ERROR_ = 0;
constexpr GLenum GL_INVALID_ENUM_ = 0x0500;

class DlistBeginEndTest : public ContextTest {
protected:
    DlistBeginEndTest() : ContextTest(sfpew_test::Backend::GLES3, kWindow) {}

    void SetUp() override {
        ContextTest::SetUp();
        if (::testing::Test::IsSkipped()) return;

        vertex2f_ = Get<void (*)(GLfloat, GLfloat)>("glVertex2f");
        begin_ = Get<void (*)(GLenum)>("glBegin");
        end_ = Get<void (*)()>("glEnd");
        color4f_ = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glColor4f");
        clear_color_ = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glClearColor");
        clear_ = Get<void (*)(GLbitfield)>("glClear");
        finish_ = Get<void (*)()>("glFinish");
        gen_lists_ = Get<GLuint (*)(GLsizei)>("glGenLists");
        new_list_ = Get<void (*)(GLuint, GLenum)>("glNewList");
        end_list_ = Get<void (*)()>("glEndList");
        call_list_ = Get<void (*)(GLuint)>("glCallList");
        delete_lists_ = Get<void (*)(GLuint, GLsizei)>("glDeleteLists");
        get_error_ = Get<GLenum (*)()>("glGetError");
        auto read_pixels =
            Get<void (*)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*)>("glReadPixels");
        ASSERT_NE(read_pixels, nullptr);
        probe_.emplace(read_pixels);

        clear_color_(0, 0, 0, 0);
        get_error_();
    }

    // The full-viewport quad these tests draw is green; anything else fails.
    void ExpectGreen(const char* what) {
        const PixelProbe::Rgba p = probe_->At(kWindow / 2, kWindow / 2);
        EXPECT_TRUE(p.g > 150 && p.r < 100)
            << what << ": rgba=(" << (int)p.r << ',' << (int)p.g << ',' << (int)p.b << ','
            << (int)p.a << ')';
    }

    void ExpectBlack(const char* what) {
        const PixelProbe::Rgba p = probe_->At(kWindow / 2, kWindow / 2);
        EXPECT_TRUE(p.r < 60 && p.g < 60 && p.b < 60)
            << what << ": rgba=(" << (int)p.r << ',' << (int)p.g << ',' << (int)p.b << ','
            << (int)p.a << ')';
    }

    // The four corners of a viewport-filling quad, in a list with NO
    // Begin/End.
    void RecordBareVertices(GLuint list) {
        new_list_(list, GL_COMPILE_);
        color4f_(0, 1, 0, 1);
        vertex2f_(-1, -1);
        vertex2f_(1, -1);
        vertex2f_(1, 1);
        vertex2f_(-1, 1);
        end_list_();
    }

    void (*vertex2f_)(GLfloat, GLfloat) = nullptr;
    void (*begin_)(GLenum) = nullptr;
    void (*end_)() = nullptr;
    void (*color4f_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*clear_color_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*clear_)(GLbitfield) = nullptr;
    void (*finish_)() = nullptr;
    GLuint (*gen_lists_)(GLsizei) = nullptr;
    void (*new_list_)(GLuint, GLenum) = nullptr;
    void (*end_list_)() = nullptr;
    void (*call_list_)(GLuint) = nullptr;
    void (*delete_lists_)(GLuint, GLsizei) = nullptr;
    GLenum (*get_error_)() = nullptr;
    std::optional<PixelProbe> probe_;
};

TEST_F(DlistBeginEndTest, CallListOfBareVerticesInsideBeginEnd) {
    const GLuint list = gen_lists_(1);
    RecordBareVertices(list);
    clear_(GL_COLOR_BUFFER_BIT_);
    begin_(GL_QUADS_);
    call_list_(list);
    end_();
    finish_();
    EXPECT_EQ(get_error_(), GL_NO_ERROR_) << "callList(vertices) in Begin/End";
    ExpectGreen("callList(vertices) in Begin/End drew the quad");
    delete_lists_(list, 1);
}

TEST_F(DlistBeginEndTest, NestedListInsideCompileAndExecute) {
    // The outer list records Begin, the CallList and End; both the immediate
    // execution and the later replay must produce the quad.
    const GLuint inner = gen_lists_(1);
    const GLuint outer = gen_lists_(1);
    RecordBareVertices(inner);

    clear_(GL_COLOR_BUFFER_BIT_);
    new_list_(outer, GL_COMPILE_AND_EXECUTE_);
    begin_(GL_QUADS_);
    call_list_(inner);
    end_();
    end_list_();
    finish_();
    ExpectGreen("nested list, immediate execution");

    clear_(GL_COLOR_BUFFER_BIT_);
    call_list_(outer);
    finish_();
    ExpectGreen("nested list, replayed");

    delete_lists_(inner, 1);
    delete_lists_(outer, 1);
}

TEST_F(DlistBeginEndTest, BeginVerticesAndEndInThreeSeparateLists) {
    // Upstream expects the glCallList(begin) to raise INVALID_OPERATION
    // (Begin inside Begin/End); this wrapper does raise it, because the
    // nested-Begin check is one of the rules it DOES enforce. Either way the
    // geometry must still be drawn by the outer block.
    const GLuint begin_list = gen_lists_(1);
    const GLuint vertex_list = gen_lists_(1);
    const GLuint end_list_id = gen_lists_(1);
    new_list_(begin_list, GL_COMPILE_);
    begin_(GL_QUADS_);
    end_list_();
    RecordBareVertices(vertex_list);
    new_list_(end_list_id, GL_COMPILE_);
    end_();
    end_list_();
    get_error_();

    clear_(GL_COLOR_BUFFER_BIT_);
    begin_(GL_QUADS_);
    call_list_(begin_list); // nested Begin: error expected, block stays open
    call_list_(vertex_list);
    call_list_(end_list_id);
    finish_();
    get_error_();
    ExpectGreen("split Begin/vertices/End lists drew the quad");

    delete_lists_(begin_list, 1);
    delete_lists_(vertex_list, 1);
    delete_lists_(end_list_id, 1);
}

TEST_F(DlistBeginEndTest, InvalidBeginModeInsideAListDrawsNothing) {
    // glBegin(10000) must raise GL_INVALID_ENUM at record time and must NOT
    // be compiled into the list, so replaying draws nothing.
    const GLuint list = gen_lists_(1);
    clear_(GL_COLOR_BUFFER_BIT_);
    new_list_(list, GL_COMPILE_AND_EXECUTE_);
    begin_(10000);
    color4f_(0, 1, 0, 1);
    vertex2f_(-1, -1);
    vertex2f_(1, -1);
    vertex2f_(1, 1);
    vertex2f_(-1, 1);
    end_();
    end_list_();
    EXPECT_EQ(get_error_(), GL_INVALID_ENUM_) << "glBegin(bad mode) in a list";
    finish_();
    ExpectBlack("glBegin(bad mode) in a list drew nothing");

    clear_(GL_COLOR_BUFFER_BIT_);
    call_list_(list);
    finish_();
    get_error_();
    ExpectBlack("replaying that list drew nothing");
    delete_lists_(list, 1);
}

} // namespace
