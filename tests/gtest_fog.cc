// SimpleFPEWrapper - tests/gtest_fog.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// Numeric fog verification: every fog factor is computed on the CPU from
// the GL 2.1 formulas and compared against what the wrapper renders, so a
// wrong distance, a flipped mix or an ignored state shows up as a number
// rather than as "the fog looks odd". Covers GL_LINEAR / GL_EXP / GL_EXP2
// and both GL_FOG_COORD_SRC sources (GL 1.4 core).

#include "sfpew_gtest.h"

#include <cmath>
#include <optional>

namespace {

using sfpew_test::ContextTest;
using sfpew_test::GLbitfield;
using sfpew_test::GLenum;
using sfpew_test::GLfloat;
using sfpew_test::GLint;
using sfpew_test::GLsizei;
using sfpew_test::GLubyte;
using sfpew_test::PixelProbe;

constexpr int kWindow = 64;
constexpr GLenum GL_QUADS_ = 0x0007;
constexpr GLbitfield GL_COLOR_BUFFER_BIT_ = 0x00004000;
constexpr GLenum GL_FOG_ = 0x0B60;
constexpr GLenum GL_FOG_MODE_ = 0x0B65;
constexpr GLenum GL_FOG_DENSITY_ = 0x0B62;
constexpr GLenum GL_FOG_START_ = 0x0B63;
constexpr GLenum GL_FOG_END_ = 0x0B64;
constexpr GLenum GL_FOG_COLOR_ = 0x0B66;
constexpr GLenum GL_FOG_COORD_SRC_ = 0x8450;
constexpr GLenum GL_FRAGMENT_DEPTH_ = 0x8452;
constexpr GLenum GL_FOG_COORD_ = 0x8451;
constexpr GLenum GL_LINEAR_ = 0x2601;
constexpr GLenum GL_EXP_ = 0x0800;
constexpr GLenum GL_EXP2_ = 0x0801;
constexpr GLenum GL_PROJECTION_ = 0x1701;
constexpr GLenum GL_MODELVIEW_ = 0x1700;

class FogTest : public ContextTest {
protected:
    FogTest() : ContextTest(sfpew_test::Backend::GLES3, kWindow) {}

    void SetUp() override {
        ContextTest::SetUp();
        if (::testing::Test::IsSkipped()) return;

        clear_color_ = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glClearColor");
        clear_ = Get<void (*)(GLbitfield)>("glClear");
        begin_ = Get<void (*)(GLenum)>("glBegin");
        end_ = Get<void (*)()>("glEnd");
        color3f_ = Get<void (*)(GLfloat, GLfloat, GLfloat)>("glColor3f");
        vertex3f_ = Get<void (*)(GLfloat, GLfloat, GLfloat)>("glVertex3f");
        fog_coordf_ = GetOptional<void (*)(GLfloat)>("glFogCoordf");
        enable_ = Get<void (*)(GLenum)>("glEnable");
        fogi_ = Get<void (*)(GLenum, GLint)>("glFogi");
        fogf_ = Get<void (*)(GLenum, GLfloat)>("glFogf");
        fogfv_ = Get<void (*)(GLenum, const GLfloat*)>("glFogfv");
        matrix_mode_ = Get<void (*)(GLenum)>("glMatrixMode");
        load_identity_ = Get<void (*)()>("glLoadIdentity");
        ortho_ = Get<void (*)(double, double, double, double, double, double)>("glOrtho");
        get_error_ = Get<GLenum (*)()>("glGetError");
        auto read_pixels =
            Get<void (*)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*)>("glReadPixels");
        ASSERT_NE(read_pixels, nullptr);
        probe_.emplace(read_pixels);

        // Ortho so eye-space z is exactly what we pass as the vertex z.
        matrix_mode_(GL_PROJECTION_);
        load_identity_();
        ortho_(-1, 1, -1, 1, -1000, 1000);
        matrix_mode_(GL_MODELVIEW_);
        load_identity_();

        static const GLfloat cyan[4] = {0.0f, 1.0f, 1.0f, 1.0f};
        clear_color_(0.0f, 0.0f, 0.0f, 1.0f);
        enable_(GL_FOG_);
        fogfv_(GL_FOG_COLOR_, cyan);
    }

    void DrawAtDepth(float eye_depth, float fog_coord, bool use_coord) {
        clear_(GL_COLOR_BUFFER_BIT_);
        begin_(GL_QUADS_);
        color3f_(1.0f, 0.0f, 0.0f);
        if (use_coord) fog_coordf_(fog_coord);
        vertex3f_(-1.0f, -1.0f, -eye_depth);
        if (use_coord) fog_coordf_(fog_coord);
        vertex3f_(1.0f, -1.0f, -eye_depth);
        if (use_coord) fog_coordf_(fog_coord);
        vertex3f_(1.0f, 1.0f, -eye_depth);
        if (use_coord) fog_coordf_(fog_coord);
        vertex3f_(-1.0f, 1.0f, -eye_depth);
        end_();
    }

    // obj color red (1,0,0), fog color cyan (0,1,1): the red channel IS the
    // fog factor and green/blue are (1 - factor), so one probe reads both.
    void ExpectFactor(const char* tag, float expected_factor) {
        const PixelProbe::Rgba px = probe_->At(kWindow / 2, kWindow / 2);
        const float got = px.r / 255.0f;
        EXPECT_NEAR(got, expected_factor, 0.02f)
            << tag << ": expected f=" << expected_factor << " got f=" << got;
    }

    void (*clear_color_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*clear_)(GLbitfield) = nullptr;
    void (*begin_)(GLenum) = nullptr;
    void (*end_)() = nullptr;
    void (*color3f_)(GLfloat, GLfloat, GLfloat) = nullptr;
    void (*vertex3f_)(GLfloat, GLfloat, GLfloat) = nullptr;
    void (*fog_coordf_)(GLfloat) = nullptr;
    void (*enable_)(GLenum) = nullptr;
    void (*fogi_)(GLenum, GLint) = nullptr;
    void (*fogf_)(GLenum, GLfloat) = nullptr;
    void (*fogfv_)(GLenum, const GLfloat*) = nullptr;
    void (*matrix_mode_)(GLenum) = nullptr;
    void (*load_identity_)() = nullptr;
    void (*ortho_)(double, double, double, double, double, double) = nullptr;
    GLenum (*get_error_)() = nullptr;
    std::optional<PixelProbe> probe_;
};

TEST_F(FogTest, LinearExpAndExp2MatchTheSpecFormulas) {
    // --- GL_LINEAR: f = (end - c) / (end - start) ---
    fogi_(GL_FOG_MODE_, GL_LINEAR_);
    fogf_(GL_FOG_START_, 0.0f);
    fogf_(GL_FOG_END_, 100.0f);
    DrawAtDepth(50.0f, 0, false);
    ExpectFactor("LINEAR start=0 end=100 depth=50", 0.5f);
    DrawAtDepth(25.0f, 0, false);
    ExpectFactor("LINEAR start=0 end=100 depth=25", 0.75f);
    fogf_(GL_FOG_START_, 20.0f);
    fogf_(GL_FOG_END_, 60.0f);
    DrawAtDepth(40.0f, 0, false);
    ExpectFactor("LINEAR start=20 end=60 depth=40", 0.5f);

    // --- GL_EXP: f = exp(-density * c) ---
    fogi_(GL_FOG_MODE_, GL_EXP_);
    fogf_(GL_FOG_DENSITY_, 0.02f);
    DrawAtDepth(50.0f, 0, false);
    ExpectFactor("EXP density=0.02 depth=50", std::exp(-0.02f * 50.0f));
    fogf_(GL_FOG_DENSITY_, 0.05f);
    DrawAtDepth(20.0f, 0, false);
    ExpectFactor("EXP density=0.05 depth=20", std::exp(-0.05f * 20.0f));

    // --- GL_EXP2: f = exp(-(density * c)^2) ---
    fogi_(GL_FOG_MODE_, GL_EXP2_);
    fogf_(GL_FOG_DENSITY_, 0.02f);
    DrawAtDepth(50.0f, 0, false);
    ExpectFactor("EXP2 density=0.02 depth=50",
                 std::exp(-(0.02f * 50.0f) * (0.02f * 50.0f)));

    EXPECT_EQ(get_error_(), 0u);
}

TEST_F(FogTest, FogCoordSrcSelectsTheDistanceSource) {
    ASSERT_NE(fog_coordf_, nullptr) << "glFogCoordf does not resolve (GL 1.4 core)";

    fogi_(GL_FOG_MODE_, GL_LINEAR_);
    fogf_(GL_FOG_START_, 0.0f);
    fogf_(GL_FOG_END_, 100.0f);
    fogi_(GL_FOG_COORD_SRC_, GL_FOG_COORD_);
    // eye depth 90 but fog coord 25: a correct implementation uses 25.
    DrawAtDepth(90.0f, 25.0f, true);
    ExpectFactor("FOG_COORD src: coord=25 (eye depth 90)", 0.75f);
    fogi_(GL_FOG_COORD_SRC_, GL_FRAGMENT_DEPTH_);
    DrawAtDepth(90.0f, 25.0f, true);
    ExpectFactor("FRAGMENT_DEPTH src back: depth=90", 0.10f);

    EXPECT_EQ(get_error_(), 0u);
}

} // namespace
