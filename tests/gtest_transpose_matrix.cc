// SimpleFPEWrapper - tests/gtest_transpose_matrix.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

#include "sfpew_gtest.h"

#include <array>
#include <cstring>
#include <string>

namespace {

using sfpew_test::ContextTest;
using sfpew_test::GLdouble;
using sfpew_test::GLenum;
using sfpew_test::GLfloat;
using sfpew_test::GLint;
using sfpew_test::GLsizei;
using sfpew_test::GLuint;

constexpr GLenum GL_MODELVIEW_ = 0x1700;
constexpr GLenum GL_PROJECTION_ = 0x1701;
constexpr GLenum GL_TEXTURE_ = 0x1702;
constexpr GLenum GL_COLOR_ = 0x1800;
constexpr GLenum GL_MODELVIEW_MATRIX_ = 0x0BA6;
constexpr GLenum GL_PROJECTION_MATRIX_ = 0x0BA7;
constexpr GLenum GL_TEXTURE_MATRIX_ = 0x0BA8;
constexpr GLenum GL_COLOR_MATRIX_ = 0x80B1;
constexpr GLenum GL_TRANSPOSE_MODELVIEW_MATRIX_ = 0x84E3;
constexpr GLenum GL_TRANSPOSE_PROJECTION_MATRIX_ = 0x84E4;
constexpr GLenum GL_TRANSPOSE_TEXTURE_MATRIX_ = 0x84E5;
constexpr GLenum GL_TRANSPOSE_COLOR_MATRIX_ = 0x84E6;
constexpr GLenum GL_EXTENSIONS_ = 0x1F03;
constexpr GLenum GL_COMPILE_ = 0x1300;
constexpr GLenum GL_NO_ERROR_ = 0;

template <typename T>
std::array<GLfloat, 16> ColumnMajorFromRowMajor(const std::array<T, 16>& input) {
    std::array<GLfloat, 16> output{};
    for (int row = 0; row < 4; ++row)
        for (int column = 0; column < 4; ++column)
            output[column * 4 + row] = static_cast<GLfloat>(input[row * 4 + column]);
    return output;
}

std::array<GLfloat, 16> Multiply(const std::array<GLfloat, 16>& left,
                                 const std::array<GLfloat, 16>& right) {
    std::array<GLfloat, 16> output{};
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            for (int k = 0; k < 4; ++k)
                output[column * 4 + row] += left[k * 4 + row] * right[column * 4 + k];
    return output;
}

class TransposeMatrixTest : public ContextTest {
protected:
    void SetUp() override {
        ContextTest::SetUp();
        if (::testing::Test::IsSkipped() || ::testing::Test::HasFatalFailure()) return;
        matrix_mode_ = Get<void (*)(GLenum)>("glMatrixMode");
        load_matrix_f_ = Get<void (*)(const GLfloat*)>("glLoadMatrixf");
        load_transpose_f_ = Get<void (*)(const GLfloat*)>("glLoadTransposeMatrixf");
        load_transpose_d_ = Get<void (*)(const GLdouble*)>("glLoadTransposeMatrixd");
        mult_transpose_f_ = Get<void (*)(const GLfloat*)>("glMultTransposeMatrixf");
        mult_transpose_d_ = Get<void (*)(const GLdouble*)>("glMultTransposeMatrixd");
        get_floatv_ = Get<void (*)(GLenum, GLfloat*)>("glGetFloatv");
        get_string_ = Get<const unsigned char* (*)(GLenum)>("glGetString");
        get_error_ = Get<GLenum (*)()>("glGetError");
    }

    std::array<GLfloat, 16> Read(GLenum pname) {
        std::array<GLfloat, 16> value{};
        get_floatv_(pname, value.data());
        return value;
    }

    void ExpectMatrix(const std::array<GLfloat, 16>& actual,
                      const std::array<GLfloat, 16>& expected) {
        for (int i = 0; i < 16; ++i) EXPECT_FLOAT_EQ(actual[i], expected[i]) << "element " << i;
        EXPECT_EQ(get_error_(), GL_NO_ERROR_);
    }

    void (*matrix_mode_)(GLenum) = nullptr;
    void (*load_matrix_f_)(const GLfloat*) = nullptr;
    void (*load_transpose_f_)(const GLfloat*) = nullptr;
    void (*load_transpose_d_)(const GLdouble*) = nullptr;
    void (*mult_transpose_f_)(const GLfloat*) = nullptr;
    void (*mult_transpose_d_)(const GLdouble*) = nullptr;
    void (*get_floatv_)(GLenum, GLfloat*) = nullptr;
    const unsigned char* (*get_string_)(GLenum) = nullptr;
    GLenum (*get_error_)() = nullptr;
};

TEST_F(TransposeMatrixTest, FloatLoadUsesRowMajorInput) {
    const std::array<GLfloat, 16> row_major = {
        1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
    };
    matrix_mode_(GL_MODELVIEW_);
    load_transpose_f_(row_major.data());
    ExpectMatrix(Read(GL_MODELVIEW_MATRIX_), ColumnMajorFromRowMajor(row_major));
    ExpectMatrix(Read(GL_TRANSPOSE_MODELVIEW_MATRIX_), row_major);
}

TEST_F(TransposeMatrixTest, DoubleLoadAndProjectionGetterAgree) {
    const std::array<GLdouble, 16> row_major = {
        0.25, 1.5, 2.75, 4, 5.25, 6.5, 7.75, 8,
        9.25, 10.5, 11.75, 12, 13.25, 14.5, 15.75, 16,
    };
    matrix_mode_(GL_PROJECTION_);
    load_transpose_d_(row_major.data());
    ExpectMatrix(Read(GL_PROJECTION_MATRIX_), ColumnMajorFromRowMajor(row_major));
    ExpectMatrix(Read(GL_TRANSPOSE_PROJECTION_MATRIX_),
                 ColumnMajorFromRowMajor(ColumnMajorFromRowMajor(row_major)));
}

TEST_F(TransposeMatrixTest, FloatAndDoubleMultiplyComposeOnTheRight) {
    const std::array<GLfloat, 16> base = {
        2, 0, 0, 0, 1, 3, 0, 0, 0, 2, 4, 0, 5, 6, 7, 1,
    };
    const std::array<GLfloat, 16> float_row = {
        1, 2, 0, 0, 0, 1, 3, 0, 4, 0, 1, 0, 5, 6, 7, 1,
    };
    const std::array<GLdouble, 16> double_row = {
        1, 0, 2, 0, 3, 1, 0, 0, 0, 4, 1, 0, 2, 3, 4, 1,
    };

    matrix_mode_(GL_MODELVIEW_);
    load_matrix_f_(base.data());
    mult_transpose_f_(float_row.data());
    auto expected = Multiply(base, ColumnMajorFromRowMajor(float_row));
    ExpectMatrix(Read(GL_MODELVIEW_MATRIX_), expected);

    mult_transpose_d_(double_row.data());
    expected = Multiply(expected, ColumnMajorFromRowMajor(double_row));
    ExpectMatrix(Read(GL_MODELVIEW_MATRIX_), expected);
}

TEST_F(TransposeMatrixTest, TextureAndColorTransposeGettersUseTheirOwnStacks) {
    const std::array<GLfloat, 16> texture = {
        1, 0, 0, 9, 0, 2, 0, 8, 0, 0, 3, 7, 4, 5, 6, 1,
    };
    const std::array<GLdouble, 16> color = {
        1, 2, 3, 0, 0, 1, 4, 0, 5, 6, 1, 0, 0.1, 0.2, 0.3, 1,
    };
    matrix_mode_(GL_TEXTURE_);
    load_transpose_f_(texture.data());
    ExpectMatrix(Read(GL_TEXTURE_MATRIX_), ColumnMajorFromRowMajor(texture));
    ExpectMatrix(Read(GL_TRANSPOSE_TEXTURE_MATRIX_), texture);

    matrix_mode_(GL_COLOR_);
    load_transpose_d_(color.data());
    ExpectMatrix(Read(GL_COLOR_MATRIX_), ColumnMajorFromRowMajor(color));
    ExpectMatrix(Read(GL_TRANSPOSE_COLOR_MATRIX_),
                 ColumnMajorFromRowMajor(ColumnMajorFromRowMajor(color)));
}

TEST_F(TransposeMatrixTest, DisplayListOwnsTheMatrixAndDefersExecution) {
    auto gen_lists = Get<GLuint (*)(GLsizei)>("glGenLists");
    auto new_list = Get<void (*)(GLuint, GLenum)>("glNewList");
    auto end_list = Get<void (*)()>("glEndList");
    auto call_list = Get<void (*)(GLuint)>("glCallList");
    const std::array<GLfloat, 16> identity = {
        1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1,
    };
    std::array<GLfloat, 16> row_major = {
        1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
    };
    const auto expected = ColumnMajorFromRowMajor(row_major);

    matrix_mode_(GL_MODELVIEW_);
    load_matrix_f_(identity.data());
    const GLuint list = gen_lists(1);
    ASSERT_NE(list, 0u);
    new_list(list, GL_COMPILE_);
    load_transpose_f_(row_major.data());
    end_list();
    ExpectMatrix(Read(GL_MODELVIEW_MATRIX_), identity);

    row_major.fill(99.0f);
    call_list(list);
    ExpectMatrix(Read(GL_MODELVIEW_MATRIX_), expected);
}

TEST_F(TransposeMatrixTest, ArbAliasesAndExtensionResolve) {
    EXPECT_EQ(Get<void*>("glLoadTransposeMatrixfARB"),
              Get<void*>("glLoadTransposeMatrixf"));
    EXPECT_EQ(Get<void*>("glLoadTransposeMatrixdARB"),
              Get<void*>("glLoadTransposeMatrixd"));
    EXPECT_EQ(Get<void*>("glMultTransposeMatrixfARB"),
              Get<void*>("glMultTransposeMatrixf"));
    EXPECT_EQ(Get<void*>("glMultTransposeMatrixdARB"),
              Get<void*>("glMultTransposeMatrixd"));
    const auto* extensions = reinterpret_cast<const char*>(get_string_(GL_EXTENSIONS_));
    ASSERT_NE(extensions, nullptr);
    EXPECT_NE(std::string(extensions).find("GL_ARB_transpose_matrix"), std::string::npos);
}

} // namespace
