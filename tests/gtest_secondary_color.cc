// SimpleFPEWrapper - tests/gtest_secondary_color.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

#include "sfpew_gtest.h"

#include <cstdint>
#include <limits>

namespace {

using sfpew_test::ContextTest;
using sfpew_test::GLenum;
using sfpew_test::GLfloat;
using sfpew_test::GLint;
using sfpew_test::GLsizei;
using sfpew_test::GLuint;

constexpr GLenum GL_NO_ERROR_ = 0;
constexpr GLenum GL_CURRENT_COLOR_ = 0x0B00;
constexpr GLenum GL_CURRENT_SECONDARY_COLOR_ = 0x8459;
constexpr GLenum GL_SECONDARY_COLOR_ARRAY_SIZE_ = 0x845A;
constexpr GLenum GL_COLOR_SUM_ = 0x8458;
constexpr GLenum GL_CURRENT_BIT_ = 0x00000001;
constexpr GLenum GL_TRIANGLES_ = 0x0004;
constexpr GLenum GL_COMPILE_ = 0x1300;

class SecondaryColorTest : public ContextTest {
protected:
    void SetUp() override {
        ContextTest::SetUp();
        if (::testing::Test::IsSkipped() || ::testing::Test::HasFatalFailure()) return;
        get_floatv_ = Get<void (*)(GLenum, GLfloat*)>("glGetFloatv");
        get_integerv_ = Get<void (*)(GLenum, GLint*)>("glGetIntegerv");
        get_error_ = Get<GLenum (*)()>("glGetError");
    }

    void ExpectSecondary(GLfloat red, GLfloat green, GLfloat blue) {
        GLfloat value[4] = {};
        get_floatv_(GL_CURRENT_SECONDARY_COLOR_, value);
        EXPECT_NEAR(value[0], red, 1e-6f);
        EXPECT_NEAR(value[1], green, 1e-6f);
        EXPECT_NEAR(value[2], blue, 1e-6f);
        EXPECT_FLOAT_EQ(value[3], 1.0f);
        EXPECT_EQ(get_error_(), GL_NO_ERROR_);
    }

    void (*get_floatv_)(GLenum, GLfloat*) = nullptr;
    void (*get_integerv_)(GLenum, GLint*) = nullptr;
    GLenum (*get_error_)() = nullptr;
};

TEST_F(SecondaryColorTest, ByteScalarAndVectorNormalize) {
    using T = std::int8_t;
    auto scalar = Get<void (*)(T, T, T)>("glSecondaryColor3b");
    auto vector = Get<void (*)(const T*)>("glSecondaryColor3bv");
    scalar(std::numeric_limits<T>::min(), 0, std::numeric_limits<T>::max());
    ExpectSecondary(-1.0f, 0.0f, 1.0f);
    const T value[3] = {std::numeric_limits<T>::max(), std::numeric_limits<T>::min(), 0};
    vector(value);
    ExpectSecondary(1.0f, -1.0f, 0.0f);
}

TEST_F(SecondaryColorTest, ShortScalarAndVectorNormalize) {
    using T = std::int16_t;
    auto scalar = Get<void (*)(T, T, T)>("glSecondaryColor3s");
    auto vector = Get<void (*)(const T*)>("glSecondaryColor3sv");
    scalar(std::numeric_limits<T>::min(), 0, std::numeric_limits<T>::max());
    ExpectSecondary(-1.0f, 0.0f, 1.0f);
    const T value[3] = {std::numeric_limits<T>::max(), std::numeric_limits<T>::min(), 0};
    vector(value);
    ExpectSecondary(1.0f, -1.0f, 0.0f);
}

TEST_F(SecondaryColorTest, IntScalarAndVectorNormalize) {
    using T = std::int32_t;
    auto scalar = Get<void (*)(T, T, T)>("glSecondaryColor3i");
    auto vector = Get<void (*)(const T*)>("glSecondaryColor3iv");
    scalar(std::numeric_limits<T>::min(), 0, std::numeric_limits<T>::max());
    ExpectSecondary(-1.0f, 0.0f, 1.0f);
    const T value[3] = {std::numeric_limits<T>::max(), std::numeric_limits<T>::min(), 0};
    vector(value);
    ExpectSecondary(1.0f, -1.0f, 0.0f);
}

TEST_F(SecondaryColorTest, FloatScalarAndVectorRemainUnclamped) {
    auto scalar = Get<void (*)(GLfloat, GLfloat, GLfloat)>("glSecondaryColor3f");
    auto vector = Get<void (*)(const GLfloat*)>("glSecondaryColor3fv");
    scalar(-0.25f, 0.5f, 1.25f);
    ExpectSecondary(-0.25f, 0.5f, 1.25f);
    const GLfloat value[3] = {1.5f, -0.5f, 0.125f};
    vector(value);
    ExpectSecondary(1.5f, -0.5f, 0.125f);
}

TEST_F(SecondaryColorTest, DoubleScalarAndVectorRemainUnclamped) {
    using T = double;
    auto scalar = Get<void (*)(T, T, T)>("glSecondaryColor3d");
    auto vector = Get<void (*)(const T*)>("glSecondaryColor3dv");
    scalar(-0.25, 0.5, 1.25);
    ExpectSecondary(-0.25f, 0.5f, 1.25f);
    const T value[3] = {1.5, -0.5, 0.125};
    vector(value);
    ExpectSecondary(1.5f, -0.5f, 0.125f);
}

TEST_F(SecondaryColorTest, UnsignedByteScalarAndVectorNormalize) {
    using T = std::uint8_t;
    auto scalar = Get<void (*)(T, T, T)>("glSecondaryColor3ub");
    auto vector = Get<void (*)(const T*)>("glSecondaryColor3ubv");
    scalar(std::numeric_limits<T>::max(), 0, 128);
    ExpectSecondary(1.0f, 0.0f, 128.0f / 255.0f);
    const T value[3] = {0, std::numeric_limits<T>::max(), 64};
    vector(value);
    ExpectSecondary(0.0f, 1.0f, 64.0f / 255.0f);
}

TEST_F(SecondaryColorTest, UnsignedShortScalarAndVectorNormalize) {
    using T = std::uint16_t;
    auto scalar = Get<void (*)(T, T, T)>("glSecondaryColor3us");
    auto vector = Get<void (*)(const T*)>("glSecondaryColor3usv");
    scalar(std::numeric_limits<T>::max(), 0, 32768);
    ExpectSecondary(1.0f, 0.0f, 32768.0f / 65535.0f);
    const T value[3] = {0, std::numeric_limits<T>::max(), 16384};
    vector(value);
    ExpectSecondary(0.0f, 1.0f, 16384.0f / 65535.0f);
}

TEST_F(SecondaryColorTest, UnsignedIntScalarAndVectorNormalize) {
    using T = std::uint32_t;
    auto scalar = Get<void (*)(T, T, T)>("glSecondaryColor3ui");
    auto vector = Get<void (*)(const T*)>("glSecondaryColor3uiv");
    scalar(std::numeric_limits<T>::max(), 0, 0x80000000u);
    ExpectSecondary(1.0f, 0.0f, 0.5f);
    const T value[3] = {0, std::numeric_limits<T>::max(), 0x40000000u};
    vector(value);
    ExpectSecondary(0.0f, 1.0f, 0.25f);
}

TEST_F(SecondaryColorTest, BeginEndCurrentBitAndCompiledListPreserveState) {
    auto primary = Get<void (*)(GLfloat, GLfloat, GLfloat)>("glColor3f");
    auto secondary = Get<void (*)(GLfloat, GLfloat, GLfloat)>("glSecondaryColor3f");
    auto secondary_vector = Get<void (*)(const GLfloat*)>("glSecondaryColor3fv");
    auto begin = Get<void (*)(GLenum)>("glBegin");
    auto end = Get<void (*)()>("glEnd");
    auto vertex = Get<void (*)(GLfloat, GLfloat, GLfloat)>("glVertex3f");
    auto push_attrib = Get<void (*)(unsigned)>("glPushAttrib");
    auto pop_attrib = Get<void (*)()>("glPopAttrib");

    primary(0.25f, 0.5f, 0.75f);
    begin(GL_TRIANGLES_);
    secondary(0.1f, 0.2f, 0.3f);
    vertex(-0.5f, -0.5f, 0.0f);
    vertex(0.5f, -0.5f, 0.0f);
    vertex(0.0f, 0.5f, 0.0f);
    end();
    ExpectSecondary(0.1f, 0.2f, 0.3f);
    GLfloat current_primary[4] = {};
    get_floatv_(GL_CURRENT_COLOR_, current_primary);
    EXPECT_FLOAT_EQ(current_primary[0], 0.25f);
    EXPECT_FLOAT_EQ(current_primary[1], 0.5f);
    EXPECT_FLOAT_EQ(current_primary[2], 0.75f);

    push_attrib(GL_CURRENT_BIT_);
    secondary(0.7f, 0.8f, 0.9f);
    pop_attrib();
    ExpectSecondary(0.1f, 0.2f, 0.3f);

    const GLuint list = Get<GLuint (*)(GLsizei)>("glGenLists")(1);
    ASSERT_NE(list, 0u);
    auto new_list = Get<void (*)(GLuint, GLenum)>("glNewList");
    auto end_list = Get<void (*)()>("glEndList");
    auto call_list = Get<void (*)(GLuint)>("glCallList");
    GLfloat captured[3] = {0.8f, 0.6f, 0.4f};
    new_list(list, GL_COMPILE_);
    begin(GL_TRIANGLES_);
    secondary_vector(captured);
    vertex(-0.5f, -0.5f, 0.0f);
    vertex(0.5f, -0.5f, 0.0f);
    vertex(0.0f, 0.5f, 0.0f);
    end();
    end_list();
    ExpectSecondary(0.1f, 0.2f, 0.3f);
    captured[0] = captured[1] = captured[2] = 0.0f;
    call_list(list);
    ExpectSecondary(0.8f, 0.6f, 0.4f);
}

TEST_F(SecondaryColorTest, AliasesAndExistingQueriesCoverTheWholeSurface) {
    constexpr const char* names[] = {
        "glSecondaryColor3b",  "glSecondaryColor3s",  "glSecondaryColor3i",
        "glSecondaryColor3f",  "glSecondaryColor3d",  "glSecondaryColor3ub",
        "glSecondaryColor3us", "glSecondaryColor3ui", "glSecondaryColor3bv",
        "glSecondaryColor3sv", "glSecondaryColor3iv", "glSecondaryColor3fv",
        "glSecondaryColor3dv", "glSecondaryColor3ubv", "glSecondaryColor3usv",
        "glSecondaryColor3uiv",
    };
    for (const char* canonical : names) {
        std::string alias = canonical;
        alias += "EXT";
        EXPECT_EQ(Get<void*>(canonical), Get<void*>(alias.c_str())) << canonical;
    }

    GLint array_size = 0;
    get_integerv_(GL_SECONDARY_COLOR_ARRAY_SIZE_, &array_size);
    EXPECT_EQ(array_size, 3);
    auto is_enabled = Get<unsigned char (*)(GLenum)>("glIsEnabled");
    EXPECT_EQ(is_enabled(GL_COLOR_SUM_), 0);
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

} // namespace
