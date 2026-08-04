// SimpleFPEWrapper - tests/gtest_imaging_color_table.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

#include "sfpew_gtest.h"

#include <array>

namespace {

using sfpew_test::GLenum;
using sfpew_test::GLfloat;
using sfpew_test::GLint;
using sfpew_test::GLsizei;
using sfpew_test::GLuint;
using sfpew_test::LibraryTest;

constexpr GLenum GL_NO_ERROR_ = 0;
constexpr GLenum GL_INVALID_VALUE_ = 0x0501;
constexpr GLenum GL_COMPILE_ = 0x1300;
constexpr GLenum GL_FLOAT_ = 0x1406;
constexpr GLenum GL_RGBA_ = 0x1908;
constexpr GLenum GL_RGBA8_ = 0x8058;
constexpr GLenum GL_COLOR_TABLE_ = 0x80D0;
constexpr GLenum GL_PROXY_COLOR_TABLE_ = 0x80D3;
constexpr GLenum GL_COLOR_TABLE_SCALE_ = 0x80D6;
constexpr GLenum GL_COLOR_TABLE_BIAS_ = 0x80D7;
constexpr GLenum GL_COLOR_TABLE_FORMAT_ = 0x80D8;
constexpr GLenum GL_COLOR_TABLE_WIDTH_ = 0x80D9;
constexpr GLenum GL_COLOR_TABLE_RED_SIZE_ = 0x80DA;
constexpr GLenum GL_UNPACK_SWAP_BYTES_ = 0x0CF0;

class ImagingColorTableTest : public LibraryTest {};

TEST_F(ImagingColorTableTest, UploadSubloadProxyAndParametersRoundTrip) {
    auto color_table = Get<void (*)(GLenum, GLenum, GLsizei, GLenum, GLenum, const void*)>(
        "glColorTable");
    auto color_sub_table =
        Get<void (*)(GLenum, GLsizei, GLsizei, GLenum, GLenum, const void*)>(
            "glColorSubTable");
    auto table_parameter =
        Get<void (*)(GLenum, GLenum, const GLfloat*)>("glColorTableParameterfv");
    auto get_table =
        Get<void (*)(GLenum, GLenum, GLenum, void*)>("glGetColorTable");
    auto get_parameter =
        Get<void (*)(GLenum, GLenum, GLint*)>("glGetColorTableParameteriv");
    auto get_error = Get<GLenum (*)()>("glGetError");

    const std::array<GLfloat, 4> scale = {0.5f, 0.5f, 0.5f, 1.0f};
    const std::array<GLfloat, 4> bias = {0.25f, 0.0f, 0.0f, 0.0f};
    table_parameter(GL_COLOR_TABLE_, GL_COLOR_TABLE_SCALE_, scale.data());
    table_parameter(GL_COLOR_TABLE_, GL_COLOR_TABLE_BIAS_, bias.data());

    const std::array<GLfloat, 8> source = {0.0f, 0.5f, 1.0f, 1.0f,
                                           1.0f, 0.0f, 0.5f, 0.5f};
    color_table(GL_COLOR_TABLE_, GL_RGBA8_, 2, GL_RGBA_, GL_FLOAT_, source.data());
    std::array<GLfloat, 8> returned{};
    get_table(GL_COLOR_TABLE_, GL_RGBA_, GL_FLOAT_, returned.data());
    EXPECT_FLOAT_EQ(returned[0], 0.25f);
    EXPECT_FLOAT_EQ(returned[1], 0.25f);
    EXPECT_FLOAT_EQ(returned[2], 0.5f);
    EXPECT_FLOAT_EQ(returned[3], 1.0f);

    const std::array<GLfloat, 4> replacement = {0.5f, 1.0f, 0.0f, 1.0f};
    color_sub_table(GL_COLOR_TABLE_, 1, 1, GL_RGBA_, GL_FLOAT_, replacement.data());
    get_table(GL_COLOR_TABLE_, GL_RGBA_, GL_FLOAT_, returned.data());
    EXPECT_FLOAT_EQ(returned[4], 0.5f);
    EXPECT_FLOAT_EQ(returned[5], 0.5f);
    EXPECT_FLOAT_EQ(returned[6], 0.0f);

    GLint value = 0;
    get_parameter(GL_COLOR_TABLE_, GL_COLOR_TABLE_FORMAT_, &value);
    EXPECT_EQ(value, static_cast<GLint>(GL_RGBA8_));
    get_parameter(GL_COLOR_TABLE_, GL_COLOR_TABLE_WIDTH_, &value);
    EXPECT_EQ(value, 2);
    get_parameter(GL_COLOR_TABLE_, GL_COLOR_TABLE_RED_SIZE_, &value);
    EXPECT_EQ(value, 8);

    color_table(GL_PROXY_COLOR_TABLE_, GL_RGBA8_, 8, GL_RGBA_, GL_FLOAT_, nullptr);
    get_parameter(GL_PROXY_COLOR_TABLE_, GL_COLOR_TABLE_WIDTH_, &value);
    EXPECT_EQ(value, 8);
    get_parameter(GL_COLOR_TABLE_, GL_COLOR_TABLE_WIDTH_, &value);
    EXPECT_EQ(value, 2);

    color_table(GL_COLOR_TABLE_, GL_RGBA8_, 3, GL_RGBA_, GL_FLOAT_, source.data());
    EXPECT_EQ(get_error(), GL_INVALID_VALUE_);
    EXPECT_EQ(get_error(), GL_NO_ERROR_);
}

TEST_F(ImagingColorTableTest, DisplayListOwnsCanonicalDataAndRecordsProxyUploads) {
    auto color_table = Get<void (*)(GLenum, GLenum, GLsizei, GLenum, GLenum, const void*)>(
        "glColorTable");
    auto get_table =
        Get<void (*)(GLenum, GLenum, GLenum, void*)>("glGetColorTable");
    auto get_parameter =
        Get<void (*)(GLenum, GLenum, GLint*)>("glGetColorTableParameteriv");
    auto pixel_store = Get<void (*)(GLenum, GLint)>("glPixelStorei");
    auto gen_lists = Get<GLuint (*)(GLsizei)>("glGenLists");
    auto new_list = Get<void (*)(GLuint, GLenum)>("glNewList");
    auto end_list = Get<void (*)()>("glEndList");
    auto call_list = Get<void (*)(GLuint)>("glCallList");

    std::array<GLfloat, 8> source = {0.0f, 0.25f, 0.5f, 1.0f,
                                     1.0f, 0.75f, 0.5f, 1.0f};
    const GLuint list = gen_lists(1);
    ASSERT_NE(list, 0u);
    new_list(list, GL_COMPILE_);
    color_table(GL_COLOR_TABLE_, GL_RGBA8_, 2, GL_RGBA_, GL_FLOAT_, source.data());
    color_table(GL_PROXY_COLOR_TABLE_, GL_RGBA8_, 4, GL_RGBA_, GL_FLOAT_, nullptr);
    end_list();
    source.fill(0.0f);

    pixel_store(GL_UNPACK_SWAP_BYTES_, 1);
    call_list(list);
    std::array<GLfloat, 8> returned{};
    get_table(GL_COLOR_TABLE_, GL_RGBA_, GL_FLOAT_, returned.data());
    EXPECT_FLOAT_EQ(returned[1], 0.25f);
    EXPECT_FLOAT_EQ(returned[4], 1.0f);

    GLint proxy_width = 0;
    get_parameter(GL_PROXY_COLOR_TABLE_, GL_COLOR_TABLE_WIDTH_, &proxy_width);
    EXPECT_EQ(proxy_width, 4);
}

} // namespace
