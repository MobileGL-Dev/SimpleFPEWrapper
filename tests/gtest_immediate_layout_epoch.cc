// SimpleFPEWrapper - tests/gtest_immediate_layout_epoch.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// A compiled display-list glBegin/glEnd run leaves the caller's current
// attribute sizes behind, exactly as a live replay would (GL: attribute
// setters executed from a list update current state). The packing layout the
// immediate path uses is revalidated against fixed_function_draw_data_t's
// size stamp alone, so a run that changes a component count without moving
// the stamp leaves every FOLLOWING glBegin/glEnd packing to the old layout
// while glEnd declares the new stride - the primitive then reads its
// vertices at the wrong pitch and degenerates.
//
// The list here declares a 4-component color where the caller has a
// 3-component one, which is the glColor3f/glColor4f mix any application that
// records lists hits.

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

constexpr int kFramebuffer = 64;
constexpr GLbitfield GL_COLOR_BUFFER_BIT_ = 0x00004000;
constexpr GLenum GL_QUADS_ = 0x0007;
constexpr GLenum GL_COMPILE_ = 0x1300;
constexpr GLenum GL_MODELVIEW_ = 0x1700;
constexpr GLenum GL_NO_ERROR_ = 0;

class ImmediateLayoutEpochTest : public ContextTest {
protected:
    ImmediateLayoutEpochTest() : ContextTest(sfpew_test::Backend::GLES3, kFramebuffer) {}

    void SetUp() override {
        ContextTest::SetUp();
        if (::testing::Test::IsSkipped() || ::testing::Test::HasFatalFailure()) return;

        clear_color_ = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glClearColor");
        clear_ = Get<void (*)(GLbitfield)>("glClear");
        begin_ = Get<void (*)(GLenum)>("glBegin");
        end_ = Get<void (*)()>("glEnd");
        color3f_ = Get<void (*)(GLfloat, GLfloat, GLfloat)>("glColor3f");
        color4f_ = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glColor4f");
        vertex2f_ = Get<void (*)(GLfloat, GLfloat)>("glVertex2f");
        gen_lists_ = Get<GLuint (*)(GLsizei)>("glGenLists");
        new_list_ = Get<void (*)(GLuint, GLenum)>("glNewList");
        end_list_ = Get<void (*)()>("glEndList");
        call_list_ = Get<void (*)(GLuint)>("glCallList");
        matrix_mode_ = Get<void (*)(GLenum)>("glMatrixMode");
        load_identity_ = Get<void (*)()>("glLoadIdentity");
        get_error_ = Get<GLenum (*)()>("glGetError");
        auto read_pixels =
            Get<void (*)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*)>("glReadPixels");
        ASSERT_NE(read_pixels, nullptr);
        probe_.emplace(read_pixels);

        matrix_mode_(GL_MODELVIEW_);
        load_identity_();
        clear_color_(0.0f, 0.0f, 0.0f, 1.0f);
        clear_(GL_COLOR_BUFFER_BIT_);
        get_error_();
    }

    void Quad(GLfloat l, GLfloat b, GLfloat r, GLfloat t) {
        vertex2f_(l, b);
        vertex2f_(r, b);
        vertex2f_(r, t);
        vertex2f_(l, t);
    }

    void ExpectGreen(int x, int y, const char* what) {
        const PixelProbe::Rgba p = probe_->At(x, y);
        EXPECT_GT(p.g, 150) << what << ": pixel(" << x << ',' << y << ") = (" << (int)p.r << ','
                            << (int)p.g << ',' << (int)p.b << ')';
    }

    void (*clear_color_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*clear_)(GLbitfield) = nullptr;
    void (*begin_)(GLenum) = nullptr;
    void (*end_)() = nullptr;
    void (*color3f_)(GLfloat, GLfloat, GLfloat) = nullptr;
    void (*color4f_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*vertex2f_)(GLfloat, GLfloat) = nullptr;
    GLuint (*gen_lists_)(GLsizei) = nullptr;
    void (*new_list_)(GLuint, GLenum) = nullptr;
    void (*end_list_)() = nullptr;
    void (*call_list_)(GLuint) = nullptr;
    void (*matrix_mode_)(GLenum) = nullptr;
    void (*load_identity_)() = nullptr;
    GLenum (*get_error_)() = nullptr;
    std::optional<PixelProbe> probe_;
};

TEST_F(ImmediateLayoutEpochTest, ListRunWideningAnAttributeRelaysOutTheNextRun) {
    // Recorded first: the caller's own layout must be the one in force when
    // the list is replayed, and recording is what leaves the packing layout
    // stamped for a size block that is thrown away at glEndList.
    const GLuint list = gen_lists_(1);
    ASSERT_NE(list, 0u);
    new_list_(list, GL_COMPILE_);
    begin_(GL_QUADS_);
    color4f_(0.0f, 1.0f, 0.0f, 1.0f); // 4 components
    Quad(-0.2f, -0.2f, 0.2f, 0.2f);
    end_();
    end_list_();
    ASSERT_EQ(get_error_(), GL_NO_ERROR_) << "recording the list";

    // The caller's layout: a 3-component color. This is the run that stamps
    // the packing layout the run after the glCallList must not reuse.
    begin_(GL_QUADS_);
    color3f_(0.0f, 0.0f, 1.0f);
    Quad(-0.1f, -0.1f, 0.1f, 0.1f);
    end_();

    call_list_(list);

    // No attribute setter at all here: the run inherits the color the list
    // left behind, four components wide, and must pack it that way.
    begin_(GL_QUADS_);
    Quad(-1.0f, -1.0f, 1.0f, 1.0f);
    end_();

    ExpectGreen(4, 4, "full-screen run after a list widened the color");
    ExpectGreen(59, 4, "full-screen run after a list widened the color");
    ExpectGreen(4, 59, "full-screen run after a list widened the color");
    ExpectGreen(59, 59, "full-screen run after a list widened the color");
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

} // namespace
