// SimpleFPEWrapper - tests/gtest_texture_residency.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

#include "sfpew_gtest.h"

#include <cmath>

namespace {

using sfpew_test::ContextTest;
using sfpew_test::GLboolean;
using sfpew_test::GLenum;
using sfpew_test::GLfloat;
using sfpew_test::GLint;
using sfpew_test::GLsizei;
using sfpew_test::GLuint;

constexpr GLenum GL_TEXTURE_2D_ = 0x0DE1;
constexpr GLenum GL_TEXTURE_PRIORITY_ = 0x8066;
constexpr GLenum GL_TEXTURE_RESIDENT_ = 0x8067;
constexpr GLenum GL_COMPILE_ = 0x1300;
constexpr GLenum GL_NO_ERROR_ = 0;
constexpr GLenum GL_INVALID_VALUE_ = 0x0501;
constexpr GLenum GL_INVALID_ENUM_ = 0x0500;

class TextureResidencyTest : public ContextTest {
protected:
    void SetUp() override {
        ContextTest::SetUp();
        if (::testing::Test::IsSkipped() || ::testing::Test::HasFatalFailure()) return;
        gen_textures_ = Get<void (*)(GLsizei, GLuint*)>("glGenTextures");
        bind_texture_ = Get<void (*)(GLenum, GLuint)>("glBindTexture");
        delete_textures_ = Get<void (*)(GLsizei, const GLuint*)>("glDeleteTextures");
        are_resident_ = Get<GLboolean (*)(GLsizei, const GLuint*, GLboolean*)>(
            "glAreTexturesResident");
        prioritize_ = Get<void (*)(GLsizei, const GLuint*, const GLfloat*)>(
            "glPrioritizeTextures");
        get_parameter_f_ = Get<void (*)(GLenum, GLenum, GLfloat*)>("glGetTexParameterfv");
        get_parameter_i_ = Get<void (*)(GLenum, GLenum, GLint*)>("glGetTexParameteriv");
        tex_parameter_f_ = Get<void (*)(GLenum, GLenum, GLfloat)>("glTexParameterf");
        tex_parameter_fv_ = Get<void (*)(GLenum, GLenum, const GLfloat*)>("glTexParameterfv");
        tex_parameter_i_ = Get<void (*)(GLenum, GLenum, GLint)>("glTexParameteri");
        tex_parameter_iv_ = Get<void (*)(GLenum, GLenum, const GLint*)>("glTexParameteriv");
        get_error_ = Get<GLenum (*)()>("glGetError");
    }

    void Drain() {
        while (get_error_() != GL_NO_ERROR_) {}
    }

    GLuint NewTexture() {
        GLuint texture = 0;
        gen_textures_(1, &texture);
        EXPECT_NE(texture, 0u);
        bind_texture_(GL_TEXTURE_2D_, texture);
        Drain();
        return texture;
    }

    GLfloat Priority() {
        GLfloat value = -1.0f;
        get_parameter_f_(GL_TEXTURE_2D_, GL_TEXTURE_PRIORITY_, &value);
        EXPECT_EQ(get_error_(), GL_NO_ERROR_);
        return value;
    }

    void (*gen_textures_)(GLsizei, GLuint*) = nullptr;
    void (*bind_texture_)(GLenum, GLuint) = nullptr;
    void (*delete_textures_)(GLsizei, const GLuint*) = nullptr;
    GLboolean (*are_resident_)(GLsizei, const GLuint*, GLboolean*) = nullptr;
    void (*prioritize_)(GLsizei, const GLuint*, const GLfloat*) = nullptr;
    void (*get_parameter_f_)(GLenum, GLenum, GLfloat*) = nullptr;
    void (*get_parameter_i_)(GLenum, GLenum, GLint*) = nullptr;
    void (*tex_parameter_f_)(GLenum, GLenum, GLfloat) = nullptr;
    void (*tex_parameter_fv_)(GLenum, GLenum, const GLfloat*) = nullptr;
    void (*tex_parameter_i_)(GLenum, GLenum, GLint) = nullptr;
    void (*tex_parameter_iv_)(GLenum, GLenum, const GLint*) = nullptr;
    GLenum (*get_error_)() = nullptr;
};

TEST_F(TextureResidencyTest, AllLiveTexturesAreResidentAndInvalidInputsError) {
    const GLuint first = NewTexture();
    GLuint second = NewTexture();
    const GLuint textures[2] = {first, second};
    GLboolean residences[2] = {0, 0};
    EXPECT_EQ(are_resident_(2, textures, residences), static_cast<GLboolean>(1));
    EXPECT_EQ(residences[0], static_cast<GLboolean>(1));
    EXPECT_EQ(residences[1], static_cast<GLboolean>(1));

    EXPECT_EQ(are_resident_(-1, textures, residences), static_cast<GLboolean>(0));
    EXPECT_EQ(get_error_(), GL_INVALID_VALUE_);
    const GLfloat unused_priority = 0.5f;
    prioritize_(-1, textures, &unused_priority);
    EXPECT_EQ(get_error_(), GL_INVALID_VALUE_);

    const GLuint invalid[2] = {first, 0};
    residences[0] = 0x7f;
    residences[1] = 0x7f;
    EXPECT_EQ(are_resident_(2, invalid, residences), static_cast<GLboolean>(0));
    EXPECT_EQ(get_error_(), GL_INVALID_VALUE_);
    EXPECT_EQ(residences[0], static_cast<GLboolean>(0x7f));
    EXPECT_EQ(residences[1], static_cast<GLboolean>(0x7f));

    delete_textures_(1, &second);
    const GLuint deleted[1] = {second};
    EXPECT_EQ(are_resident_(1, deleted, residences), static_cast<GLboolean>(0));
    EXPECT_EQ(get_error_(), GL_INVALID_VALUE_);
    delete_textures_(1, &first);
}

TEST_F(TextureResidencyTest, PrioritiesClampAndRoundTripThroughAllSetters) {
    const GLuint texture = NewTexture();
    EXPECT_FLOAT_EQ(Priority(), 1.0f);

    const GLfloat f = 0.25f;
    tex_parameter_f_(GL_TEXTURE_2D_, GL_TEXTURE_PRIORITY_, f);
    EXPECT_FLOAT_EQ(Priority(), f);
    const GLfloat fv = 0.75f;
    tex_parameter_fv_(GL_TEXTURE_2D_, GL_TEXTURE_PRIORITY_, &fv);
    EXPECT_FLOAT_EQ(Priority(), fv);
    tex_parameter_i_(GL_TEXTURE_2D_, GL_TEXTURE_PRIORITY_, 9);
    EXPECT_FLOAT_EQ(Priority(), 1.0f);
    const GLint iv = -3;
    tex_parameter_iv_(GL_TEXTURE_2D_, GL_TEXTURE_PRIORITY_, &iv);
    EXPECT_FLOAT_EQ(Priority(), 0.0f);

    const GLfloat priorities[1] = {-4.0f};
    prioritize_(1, &texture, priorities);
    EXPECT_FLOAT_EQ(Priority(), 0.0f);
    const GLfloat high[1] = {4.0f};
    prioritize_(1, &texture, high);
    EXPECT_FLOAT_EQ(Priority(), 1.0f);

    GLint integer = -1;
    get_parameter_i_(GL_TEXTURE_2D_, GL_TEXTURE_PRIORITY_, &integer);
    EXPECT_EQ(integer, 1);
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);

    GLint resident = 0;
    get_parameter_i_(GL_TEXTURE_2D_, GL_TEXTURE_RESIDENT_, &resident);
    EXPECT_EQ(resident, 1);
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
    delete_textures_(1, &texture);
}

TEST_F(TextureResidencyTest, PriorityCompilesButResidencyQueryExecutesImmediatelyInLists) {
    const GLuint texture = NewTexture();
    const GLuint list = Get<GLuint (*)(GLsizei)>("glGenLists")(1);
    ASSERT_NE(list, 0u);
    auto new_list = Get<void (*)(GLuint, GLenum)>("glNewList");
    auto end_list = Get<void (*)()>("glEndList");
    auto call_list = Get<void (*)(GLuint)>("glCallList");

    const GLfloat before = 0.2f;
    prioritize_(1, &texture, &before);
    new_list(list, GL_COMPILE_);
    GLfloat after = 0.8f;
    prioritize_(1, &texture, &after);

    // Queries do not compile: this must execute while the list is recording.
    GLboolean residence = 0;
    EXPECT_EQ(are_resident_(1, &texture, &residence), static_cast<GLboolean>(1));
    EXPECT_EQ(residence, static_cast<GLboolean>(1));
    end_list();
    EXPECT_FLOAT_EQ(Priority(), before);
    after = 0.4f; // replay must use the list's deep copy, not this caller storage
    call_list(list);
    EXPECT_FLOAT_EQ(Priority(), 0.8f);
    delete_textures_(1, &texture);
}

TEST_F(TextureResidencyTest, ExtensionAliasesResolve) {
    EXPECT_EQ(Get<void*>("glAreTexturesResidentEXT"), Get<void*>("glAreTexturesResident"));
    EXPECT_EQ(Get<void*>("glPrioritizeTexturesEXT"), Get<void*>("glPrioritizeTextures"));
}

} // namespace
