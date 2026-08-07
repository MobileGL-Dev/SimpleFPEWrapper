// SimpleFPEWrapper - tests/gtest_midprim_repack.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// An attribute that grows part way through a glBegin/glEnd block (glColor3f
// for the first vertices, glColor4f for the rest) rewrites every vertex
// collected so far into the wider layout. The rewrite has to read those
// vertices at the pitch they were actually packed at - which includes the fog
// coordinate and the secondary color, both of which sit between the color and
// the texture coordinates in the stream. Reading them at a pitch that leaves
// those slots out takes every vertex from the wrong base and appends a
// remainder that no longer matches what the rest of the block adds, so the
// primitive falls apart.

#include "sfpew_gtest.h"

#include <optional>

namespace {

using sfpew_test::ContextTest;
using sfpew_test::GLbitfield;
using sfpew_test::GLenum;
using sfpew_test::GLfloat;
using sfpew_test::GLint;
using sfpew_test::GLsizei;
using sfpew_test::PixelProbe;

constexpr int kFramebuffer = 64;
constexpr GLbitfield GL_COLOR_BUFFER_BIT_ = 0x00004000;
constexpr GLenum GL_QUADS_ = 0x0007;
constexpr GLenum GL_MODELVIEW_ = 0x1700;
constexpr GLenum GL_NO_ERROR_ = 0;

class MidPrimitiveRepackTest : public ContextTest {
protected:
    MidPrimitiveRepackTest() : ContextTest(sfpew_test::Backend::GLES3, kFramebuffer) {}

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
        secondary_color3f_ = Get<void (*)(GLfloat, GLfloat, GLfloat)>("glSecondaryColor3f");
        fog_coordf_ = Get<void (*)(GLfloat)>("glFogCoordf");
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

    // A full-screen quad whose color widens from three to four components
    // half way through, which is what forces the repack.
    void WideningQuad() {
        begin_(GL_QUADS_);
        color3f_(0.0f, 1.0f, 0.0f);
        vertex2f_(-1.0f, -1.0f);
        vertex2f_(1.0f, -1.0f);
        color4f_(0.0f, 1.0f, 0.0f, 1.0f);
        vertex2f_(1.0f, 1.0f);
        vertex2f_(-1.0f, 1.0f);
        end_();
    }

    void ExpectGreen(int x, int y, const char* what) {
        const PixelProbe::Rgba p = probe_->At(x, y);
        EXPECT_GT(p.g, 150) << what << ": pixel(" << x << ',' << y << ") = (" << (int)p.r << ','
                            << (int)p.g << ',' << (int)p.b << ')';
    }

    void ExpectFullScreen(const char* what) {
        ExpectGreen(4, 4, what);
        ExpectGreen(59, 4, what);
        ExpectGreen(4, 59, what);
        ExpectGreen(59, 59, what);
        EXPECT_EQ(get_error_(), GL_NO_ERROR_) << what;
    }

    void (*clear_color_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*clear_)(GLbitfield) = nullptr;
    void (*begin_)(GLenum) = nullptr;
    void (*end_)() = nullptr;
    void (*color3f_)(GLfloat, GLfloat, GLfloat) = nullptr;
    void (*color4f_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*vertex2f_)(GLfloat, GLfloat) = nullptr;
    void (*secondary_color3f_)(GLfloat, GLfloat, GLfloat) = nullptr;
    void (*fog_coordf_)(GLfloat) = nullptr;
    void (*matrix_mode_)(GLenum) = nullptr;
    void (*load_identity_)() = nullptr;
    GLenum (*get_error_)() = nullptr;
    std::optional<PixelProbe> probe_;
};

// The baseline: with neither slot in the stream the repack already worked.
TEST_F(MidPrimitiveRepackTest, WithoutFogOrSecondaryColorTheQuadIsWhole) {
    WideningQuad();
    ExpectFullScreen("mid-primitive color growth, plain layout");
}

TEST_F(MidPrimitiveRepackTest, SecondaryColorInTheStreamSurvivesTheRepack) {
    secondary_color3f_(0.0f, 0.0f, 0.0f);
    ASSERT_EQ(get_error_(), GL_NO_ERROR_) << "glSecondaryColor3f";
    WideningQuad();
    ExpectFullScreen("mid-primitive color growth with a secondary color");
}

TEST_F(MidPrimitiveRepackTest, FogCoordinateInTheStreamSurvivesTheRepack) {
    fog_coordf_(0.5f);
    ASSERT_EQ(get_error_(), GL_NO_ERROR_) << "glFogCoordf";
    WideningQuad();
    ExpectFullScreen("mid-primitive color growth with a fog coordinate");
}

} // namespace
