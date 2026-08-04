// SimpleFPEWrapper - tests/gtest_imaging_histogram_minmax.cc
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
using sfpew_test::GLbitfield;
using sfpew_test::GLboolean;
using sfpew_test::GLenum;
using sfpew_test::GLfloat;
using sfpew_test::GLint;
using sfpew_test::GLsizei;
using sfpew_test::GLuint;
using sfpew_test::PixelProbe;

constexpr GLboolean GL_FALSE_ = 0;
constexpr GLboolean GL_TRUE_ = 1;
constexpr GLenum GL_NO_ERROR_ = 0;
constexpr GLenum GL_FLOAT_ = 0x1406;
constexpr GLenum GL_UNSIGNED_INT_ = 0x1405;
constexpr GLenum GL_RGBA_ = 0x1908;
constexpr GLbitfield GL_COLOR_BUFFER_BIT_ = 0x00004000;
constexpr GLenum GL_HISTOGRAM_ = 0x8024;
constexpr GLenum GL_HISTOGRAM_WIDTH_ = 0x8026;
constexpr GLenum GL_HISTOGRAM_SINK_ = 0x802D;
constexpr GLenum GL_MINMAX_ = 0x802E;
constexpr GLenum GL_MINMAX_FORMAT_ = 0x802F;

class ImagingHistogramMinmaxTest : public ContextTest {
protected:
    void SetUp() override {
        ContextTest::SetUp();
        if (::testing::Test::IsSkipped() || ::testing::Test::HasFatalFailure()) return;
        using MakeCurrentFn = EGLBoolean (*)(EGLDisplay, EGLSurface, EGLSurface, EGLContext);
        auto wrapper_make_current = Get<MakeCurrentFn>("eglMakeCurrent");
        ASSERT_TRUE(wrapper_make_current(display(), surface(), surface(), eglGetCurrentContext()));
    }
};

TEST_F(ImagingHistogramMinmaxTest, CollectionQueriesAndResetUseTheTransferredPixels) {
    auto histogram = Get<void (*)(GLenum, GLsizei, GLenum, GLboolean)>("glHistogram");
    auto minmax = Get<void (*)(GLenum, GLenum, GLboolean)>("glMinmax");
    auto get_histogram =
        Get<void (*)(GLenum, GLboolean, GLenum, GLenum, void*)>("glGetHistogram");
    auto get_minmax =
        Get<void (*)(GLenum, GLboolean, GLenum, GLenum, void*)>("glGetMinmax");
    auto get_histogram_parameter =
        Get<void (*)(GLenum, GLenum, GLint*)>("glGetHistogramParameteriv");
    auto get_minmax_parameter =
        Get<void (*)(GLenum, GLenum, GLint*)>("glGetMinmaxParameteriv");
    auto enable = Get<void (*)(GLenum)>("glEnable");
    auto disable = Get<void (*)(GLenum)>("glDisable");
    auto viewport = Get<void (*)(GLint, GLint, GLsizei, GLsizei)>("glViewport");
    auto window_pos = Get<void (*)(GLint, GLint)>("glWindowPos2i");
    auto draw_pixels =
        Get<void (*)(GLsizei, GLsizei, GLenum, GLenum, const void*)>("glDrawPixels");
    auto get_error = Get<GLenum (*)()>("glGetError");

    histogram(GL_HISTOGRAM_, 4, GL_RGBA_, GL_FALSE_);
    minmax(GL_MINMAX_, GL_RGBA_, GL_FALSE_);
    std::array<GLfloat, 8> initial{};
    get_minmax(GL_MINMAX_, GL_FALSE_, GL_RGBA_, GL_FLOAT_, initial.data());
    EXPECT_EQ(initial, (std::array<GLfloat, 8>{1, 1, 1, 1, 0, 0, 0, 0}));

    GLint parameter = 0;
    get_histogram_parameter(GL_HISTOGRAM_, GL_HISTOGRAM_WIDTH_, &parameter);
    EXPECT_EQ(parameter, 4);
    get_histogram_parameter(GL_HISTOGRAM_, GL_HISTOGRAM_SINK_, &parameter);
    EXPECT_EQ(parameter, 0);
    get_minmax_parameter(GL_MINMAX_, GL_MINMAX_FORMAT_, &parameter);
    EXPECT_EQ(parameter, static_cast<GLint>(GL_RGBA_));

    enable(GL_HISTOGRAM_);
    enable(GL_MINMAX_);
    viewport(0, 0, size(), size());
    window_pos(8, 8);
    const std::array<GLfloat, 8> source = {1, 0, 0, 1, 0, 0, 1, 1};
    draw_pixels(2, 1, GL_RGBA_, GL_FLOAT_, source.data());
    disable(GL_HISTOGRAM_);
    disable(GL_MINMAX_);

    std::array<GLuint, 16> counts{};
    get_histogram(GL_HISTOGRAM_, GL_TRUE_, GL_RGBA_, GL_UNSIGNED_INT_, counts.data());
    EXPECT_EQ(counts[0 * 4 + 0], 1u);
    EXPECT_EQ(counts[3 * 4 + 0], 1u);
    EXPECT_EQ(counts[0 * 4 + 1], 2u);
    EXPECT_EQ(counts[0 * 4 + 2], 1u);
    EXPECT_EQ(counts[3 * 4 + 2], 1u);
    EXPECT_EQ(counts[3 * 4 + 3], 2u);

    std::array<GLfloat, 8> extrema{};
    get_minmax(GL_MINMAX_, GL_TRUE_, GL_RGBA_, GL_FLOAT_, extrema.data());
    EXPECT_EQ(extrema, (std::array<GLfloat, 8>{0, 0, 0, 1, 1, 0, 1, 1}));

    counts.fill(99u);
    get_histogram(GL_HISTOGRAM_, GL_FALSE_, GL_RGBA_, GL_UNSIGNED_INT_, counts.data());
    for (GLuint count : counts) EXPECT_EQ(count, 0u);
    get_minmax(GL_MINMAX_, GL_FALSE_, GL_RGBA_, GL_FLOAT_, extrema.data());
    EXPECT_EQ(extrema, (std::array<GLfloat, 8>{1, 1, 1, 1, 0, 0, 0, 0}));
    EXPECT_EQ(get_error(), GL_NO_ERROR_);
}

TEST_F(ImagingHistogramMinmaxTest, SinkCollectsButSuppressesTheDestinationWrite) {
    auto histogram = Get<void (*)(GLenum, GLsizei, GLenum, GLboolean)>("glHistogram");
    auto get_histogram =
        Get<void (*)(GLenum, GLboolean, GLenum, GLenum, void*)>("glGetHistogram");
    auto enable = Get<void (*)(GLenum)>("glEnable");
    auto disable = Get<void (*)(GLenum)>("glDisable");
    auto viewport = Get<void (*)(GLint, GLint, GLsizei, GLsizei)>("glViewport");
    auto clear_color = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glClearColor");
    auto clear = Get<void (*)(GLbitfield)>("glClear");
    auto window_pos = Get<void (*)(GLint, GLint)>("glWindowPos2i");
    auto pixel_zoom = Get<void (*)(GLfloat, GLfloat)>("glPixelZoom");
    auto draw_pixels =
        Get<void (*)(GLsizei, GLsizei, GLenum, GLenum, const void*)>("glDrawPixels");
    auto read_pixels = Get<PixelProbe::ReadPixelsFn>("glReadPixels");

    histogram(GL_HISTOGRAM_, 4, GL_RGBA_, GL_TRUE_);
    enable(GL_HISTOGRAM_);
    viewport(0, 0, size(), size());
    clear_color(0, 0, 0, 1);
    clear(GL_COLOR_BUFFER_BIT_);
    window_pos(8, 8);
    pixel_zoom(8, 8);
    const std::array<GLfloat, 4> white = {1, 1, 1, 1};
    draw_pixels(1, 1, GL_RGBA_, GL_FLOAT_, white.data());
    disable(GL_HISTOGRAM_);

    const PixelProbe probe(read_pixels);
    SFPEW_EXPECT_BLANK(probe, 8, 8, 8, 8, "histogram sink must consume DrawPixels");
    std::array<GLuint, 16> counts{};
    get_histogram(GL_HISTOGRAM_, GL_FALSE_, GL_RGBA_, GL_UNSIGNED_INT_, counts.data());
    EXPECT_EQ(counts[3 * 4 + 0], 1u);
    EXPECT_EQ(counts[3 * 4 + 3], 1u);
}

} // namespace
