// SimpleFPEWrapper - tests/gtest_color_sum.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// defects-plan.md 1.4: glSecondaryColor3* already existed (state, queries,
// display-list capture), but the generated fragment shader never summed it
// into the fragment color and GL_COLOR_SUM read back permanently disabled.
// Covers: the sum actually reaching the framebuffer, GL_COLOR_SUM's real
// enable state, alpha staying untouched (spec: secondary color has none),
// and the no-secondary-color-ever-set case not breaking the shader.

#include "sfpew_gtest.h"

#include <optional>

namespace {

using sfpew_test::ContextTest;
using sfpew_test::GLbitfield;
using sfpew_test::GLenum;
using sfpew_test::GLfloat;
using sfpew_test::PixelProbe;

constexpr GLenum GL_COLOR_SUM_ = 0x8458;
constexpr GLbitfield GL_COLOR_BUFFER_BIT_ = 0x00004000;
constexpr GLenum GL_QUADS_ = 0x0007;
constexpr GLenum GL_NO_ERROR_ = 0;

class ColorSumTest : public ContextTest {
protected:
    ColorSumTest() : ContextTest(sfpew_test::Backend::GLES3, 16) {}

    void SetUp() override {
        ContextTest::SetUp();
        if (::testing::Test::IsSkipped()) return;
        enable_ = Get<void (*)(GLenum)>("glEnable");
        disable_ = Get<void (*)(GLenum)>("glDisable");
        is_enabled_ = Get<sfpew_test::GLboolean (*)(GLenum)>("glIsEnabled");
        clear_color_ = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glClearColor");
        clear_ = Get<void (*)(GLbitfield)>("glClear");
        begin_ = Get<void (*)(GLenum)>("glBegin");
        end_ = Get<void (*)()>("glEnd");
        color4f_ = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glColor4f");
        secondary_color3f_ =
            Get<void (*)(GLfloat, GLfloat, GLfloat)>("glSecondaryColor3f");
        vertex2f_ = Get<void (*)(GLfloat, GLfloat)>("glVertex2f");
        get_error_ = Get<GLenum (*)()>("glGetError");
        auto read_pixels =
            Get<void (*)(sfpew_test::GLint, sfpew_test::GLint, sfpew_test::GLsizei,
                        sfpew_test::GLsizei, GLenum, GLenum, void*)>("glReadPixels");
        ASSERT_NE(read_pixels, nullptr);
        probe_.emplace(read_pixels);
    }

    void DrawFullscreenQuad() {
        begin_(GL_QUADS_);
        vertex2f_(-1.0f, -1.0f);
        vertex2f_(1.0f, -1.0f);
        vertex2f_(1.0f, 1.0f);
        vertex2f_(-1.0f, 1.0f);
        end_();
    }

    void (*enable_)(GLenum) = nullptr;
    void (*disable_)(GLenum) = nullptr;
    sfpew_test::GLboolean (*is_enabled_)(GLenum) = nullptr;
    void (*clear_color_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*clear_)(GLbitfield) = nullptr;
    void (*begin_)(GLenum) = nullptr;
    void (*end_)() = nullptr;
    void (*color4f_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*secondary_color3f_)(GLfloat, GLfloat, GLfloat) = nullptr;
    void (*vertex2f_)(GLfloat, GLfloat) = nullptr;
    GLenum (*get_error_)() = nullptr;
    std::optional<PixelProbe> probe_;
};

TEST_F(ColorSumTest, EnabledSumsIntoTheFramebufferDisabledLeavesPrimaryOnly) {
    EXPECT_EQ(is_enabled_(GL_COLOR_SUM_), sfpew_test::GL_FALSE_);

    color4f_(1.0f, 0.0f, 0.0f, 1.0f);
    secondary_color3f_(0.0f, 1.0f, 0.0f);

    clear_color_(0.0f, 0.0f, 0.0f, 1.0f);
    clear_(GL_COLOR_BUFFER_BIT_);
    DrawFullscreenQuad();
    const auto disabled = probe_->At(8, 8);
    EXPECT_GT(disabled.r, 200) << "disabled: primary red only";
    EXPECT_LT(disabled.g, 40) << "disabled: secondary green must not show";

    enable_(GL_COLOR_SUM_);
    EXPECT_EQ(is_enabled_(GL_COLOR_SUM_), sfpew_test::GL_TRUE_);
    clear_(GL_COLOR_BUFFER_BIT_);
    DrawFullscreenQuad();
    const auto enabled = probe_->At(8, 8);
    EXPECT_GT(enabled.r, 200) << "enabled: red + green -> yellow, red channel still high";
    EXPECT_GT(enabled.g, 200) << "enabled: green channel now shows the secondary color";

    disable_(GL_COLOR_SUM_);
    EXPECT_EQ(is_enabled_(GL_COLOR_SUM_), sfpew_test::GL_FALSE_);
    clear_(GL_COLOR_BUFFER_BIT_);
    DrawFullscreenQuad();
    const auto redisabled = probe_->At(8, 8);
    EXPECT_GT(redisabled.r, 200);
    EXPECT_LT(redisabled.g, 40) << "back to disabled: green must disappear again";

    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

TEST_F(ColorSumTest, AlphaIsNotSummedSecondaryColorHasNoAlphaComponent) {
    color4f_(1.0f, 0.0f, 0.0f, 0.5f);
    secondary_color3f_(0.0f, 1.0f, 0.0f); // spec fixes secondary alpha at 1.0
    enable_(GL_COLOR_SUM_);
    clear_color_(0.0f, 0.0f, 0.0f, 0.0f);
    clear_(GL_COLOR_BUFFER_BIT_);
    DrawFullscreenQuad();
    const auto pixel = probe_->At(8, 8);
    // Blending is not enabled, so alpha simply writes through: 0.5, not
    // clamped to 1.0 by an errant += on the alpha channel.
    EXPECT_NEAR(static_cast<int>(pixel.a), 128, 12) << "alpha must come from the primary color alone";
}

TEST_F(ColorSumTest, EnabledWithoutEverSettingSecondaryColorStillRendersPrimary) {
    // GL_COLOR_SUM enabled but glSecondaryColor3* never called: the shader
    // generator must not reference an attribute that was never wired
    // (add_vs_inout only declares it when something fed attribute slot 6).
    color4f_(1.0f, 0.0f, 0.0f, 1.0f);
    enable_(GL_COLOR_SUM_);
    clear_color_(0.0f, 0.0f, 0.0f, 1.0f);
    clear_(GL_COLOR_BUFFER_BIT_);
    DrawFullscreenQuad();
    const auto pixel = probe_->At(8, 8);
    EXPECT_GT(pixel.r, 200);
    EXPECT_LT(pixel.g, 40) << "default secondary color is (0,0,0,1): summing it is a no-op";
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

} // namespace
