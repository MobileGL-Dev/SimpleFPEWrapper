// SimpleFPEWrapper - tests/gtest_feedback_payloads.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

#include "sfpew_gtest.h"

#include <array>

namespace {

using sfpew_test::ContextTest;
using sfpew_test::GLenum;
using sfpew_test::GLfloat;
using sfpew_test::GLint;
using sfpew_test::GLsizei;

constexpr GLenum GL_RENDER_ = 0x1C00;
constexpr GLenum GL_FEEDBACK_ = 0x1C01;
constexpr GLenum GL_3D_COLOR_ = 0x0602;
constexpr GLenum GL_3D_COLOR_TEXTURE_ = 0x0603;
constexpr GLenum GL_4D_COLOR_TEXTURE_ = 0x0604;
constexpr GLenum GL_POINT_TOKEN_ = 0x0701;
constexpr GLenum GL_POINTS_ = 0x0000;
constexpr GLenum GL_TEXTURE_ = 0x1702;
constexpr GLenum GL_PROJECTION_ = 0x1701;
constexpr GLenum GL_MODELVIEW_ = 0x1700;
constexpr GLenum GL_NO_ERROR_ = 0;

class FeedbackPayloadTest : public ContextTest {
protected:
    void SetUp() override {
        ContextTest::SetUp();
        if (::testing::Test::IsSkipped() || ::testing::Test::HasFatalFailure()) return;

        feedback_buffer_ = Get<void (*)(GLsizei, GLenum, GLfloat*)>("glFeedbackBuffer");
        render_mode_ = Get<GLint (*)(GLenum)>("glRenderMode");
        begin_ = Get<void (*)(GLenum)>("glBegin");
        end_ = Get<void (*)()>("glEnd");
        color4f_ = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glColor4f");
        texcoord4f_ =
            Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glTexCoord4f");
        vertex4f_ = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glVertex4f");
        matrix_mode_ = Get<void (*)(GLenum)>("glMatrixMode");
        load_identity_ = Get<void (*)()>("glLoadIdentity");
        translatef_ = Get<void (*)(GLfloat, GLfloat, GLfloat)>("glTranslatef");
        viewport_ = Get<void (*)(GLint, GLint, GLsizei, GLsizei)>("glViewport");
        get_error_ = Get<GLenum (*)()>("glGetError");

        viewport_(0, 0, size(), size());
        matrix_mode_(GL_PROJECTION_);
        load_identity_();
        matrix_mode_(GL_TEXTURE_);
        load_identity_();
        translatef_(10.0f, 20.0f, 30.0f);
        matrix_mode_(GL_MODELVIEW_);
        load_identity_();
        get_error_();
    }

    GLint Capture(GLenum type, std::array<GLfloat, 32>& feedback) {
        feedback.fill(-99.0f);
        feedback_buffer_(static_cast<GLsizei>(feedback.size()), type, feedback.data());
        EXPECT_EQ(render_mode_(GL_FEEDBACK_), 0);
        color4f_(0.25f, 0.5f, 0.75f, 1.0f);
        texcoord4f_(1.0f, 2.0f, 3.0f, 1.0f);
        begin_(GL_POINTS_);
        vertex4f_(0.0f, 0.0f, 0.0f, 2.0f);
        end_();
        return render_mode_(GL_RENDER_);
    }

    void ExpectCoordinatePrefix(const std::array<GLfloat, 32>& feedback) {
        EXPECT_FLOAT_EQ(feedback[0], static_cast<GLfloat>(GL_POINT_TOKEN_));
        EXPECT_FLOAT_EQ(feedback[1], static_cast<GLfloat>(size()) * 0.5f);
        EXPECT_FLOAT_EQ(feedback[2], static_cast<GLfloat>(size()) * 0.5f);
        EXPECT_FLOAT_EQ(feedback[3], 0.5f);
    }

    void ExpectColor(const std::array<GLfloat, 32>& feedback, size_t offset) {
        EXPECT_FLOAT_EQ(feedback[offset + 0], 0.25f);
        EXPECT_FLOAT_EQ(feedback[offset + 1], 0.5f);
        EXPECT_FLOAT_EQ(feedback[offset + 2], 0.75f);
        EXPECT_FLOAT_EQ(feedback[offset + 3], 1.0f);
    }

    void ExpectTransformedTexcoord(const std::array<GLfloat, 32>& feedback, size_t offset) {
        EXPECT_FLOAT_EQ(feedback[offset + 0], 11.0f);
        EXPECT_FLOAT_EQ(feedback[offset + 1], 22.0f);
        EXPECT_FLOAT_EQ(feedback[offset + 2], 33.0f);
        EXPECT_FLOAT_EQ(feedback[offset + 3], 1.0f);
    }

    void (*feedback_buffer_)(GLsizei, GLenum, GLfloat*) = nullptr;
    GLint (*render_mode_)(GLenum) = nullptr;
    void (*begin_)(GLenum) = nullptr;
    void (*end_)() = nullptr;
    void (*color4f_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*texcoord4f_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*vertex4f_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*matrix_mode_)(GLenum) = nullptr;
    void (*load_identity_)() = nullptr;
    void (*translatef_)(GLfloat, GLfloat, GLfloat) = nullptr;
    void (*viewport_)(GLint, GLint, GLsizei, GLsizei) = nullptr;
    GLenum (*get_error_)() = nullptr;
};

TEST_F(FeedbackPayloadTest, ThreeDimensionalColorCarriesRgba) {
    std::array<GLfloat, 32> feedback{};
    EXPECT_EQ(Capture(GL_3D_COLOR_, feedback), 8);
    ExpectCoordinatePrefix(feedback);
    ExpectColor(feedback, 4);
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

TEST_F(FeedbackPayloadTest, ThreeDimensionalColorTextureCarriesBothPayloads) {
    std::array<GLfloat, 32> feedback{};
    EXPECT_EQ(Capture(GL_3D_COLOR_TEXTURE_, feedback), 12);
    ExpectCoordinatePrefix(feedback);
    ExpectColor(feedback, 4);
    ExpectTransformedTexcoord(feedback, 8);
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

TEST_F(FeedbackPayloadTest, FourDimensionalColorTextureCarriesClipW) {
    std::array<GLfloat, 32> feedback{};
    EXPECT_EQ(Capture(GL_4D_COLOR_TEXTURE_, feedback), 13);
    ExpectCoordinatePrefix(feedback);
    EXPECT_FLOAT_EQ(feedback[4], 2.0f);
    ExpectColor(feedback, 5);
    ExpectTransformedTexcoord(feedback, 9);
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

} // namespace
