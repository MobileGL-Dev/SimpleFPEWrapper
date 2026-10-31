// SimpleFPEWrapper - tests/gtest_texture_parameter_cache.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// glTexParameteri does not forward a 2D texture's min/mag filter or S/T wrap
// when the backend already holds that value (glstate_t::texture_sampling).
// Dropping a call is only ever safe when the value really is the backend's,
// so every case here changes the parameter some other way first - another
// setter, another texture, a deleted and reused name - and then checks, by
// asking the driver, that the next glTexParameteri still lands - including
// a change made from another context that shares the texture, which the
// wrapper cannot see: once a second context exists the cache steps aside.

#include "sfpew_gtest.h"

#include <functional>

namespace {

using sfpew_test::ContextTest;
using sfpew_test::GLenum;
using sfpew_test::GLfloat;
using sfpew_test::GLint;
using sfpew_test::GLsizei;
using sfpew_test::GLuint;

constexpr GLenum GL_TEXTURE_2D_ = 0x0DE1;
constexpr GLenum GL_TEXTURE_MIN_FILTER_ = 0x2801;
constexpr GLenum GL_TEXTURE_MAG_FILTER_ = 0x2800;
constexpr GLenum GL_TEXTURE_WRAP_S_ = 0x2802;
constexpr GLenum GL_NEAREST_ = 0x2600;
constexpr GLenum GL_LINEAR_ = 0x2601;
constexpr GLenum GL_REPEAT_ = 0x2901;
constexpr GLenum GL_CLAMP_ = 0x2900;
constexpr GLenum GL_CLAMP_TO_EDGE_ = 0x812F;
constexpr GLenum GL_MIRRORED_REPEAT_ = 0x8370;
constexpr GLenum GL_NO_ERROR_ = 0;
constexpr GLenum GL_INVALID_ENUM_ = 0x0500;

class TextureParameterCacheTest : public ContextTest {
protected:
    void SetUp() override {
        ContextTest::SetUp();
        if (IsSkipped() || HasFatalFailure()) return;
        gen_ = Get<void (*)(GLsizei, GLuint*)>("glGenTextures");
        delete_ = Get<void (*)(GLsizei, const GLuint*)>("glDeleteTextures");
        bind_ = Get<void (*)(GLenum, GLuint)>("glBindTexture");
        parameteri_ = Get<void (*)(GLenum, GLenum, GLint)>("glTexParameteri");
        parameterf_ = Get<void (*)(GLenum, GLenum, GLfloat)>("glTexParameterf");
        parameteriv_ = Get<void (*)(GLenum, GLenum, const GLint*)>("glTexParameteriv");
        parameterfv_ = Get<void (*)(GLenum, GLenum, const GLfloat*)>("glTexParameterfv");
        get_parameteriv_ = Get<void (*)(GLenum, GLenum, GLint*)>("glGetTexParameteriv");
        get_error_ = Get<GLenum (*)()>("glGetError");
        get_error_();
    }

    GLuint NewTexture() {
        GLuint texture = 0;
        gen_(1, &texture);
        bind_(GL_TEXTURE_2D_, texture);
        return texture;
    }

    GLint Parameter(GLenum pname) {
        GLint value = 0;
        get_parameteriv_(GL_TEXTURE_2D_, pname, &value);
        return value;
    }

    void (*gen_)(GLsizei, GLuint*) = nullptr;
    void (*delete_)(GLsizei, const GLuint*) = nullptr;
    void (*bind_)(GLenum, GLuint) = nullptr;
    void (*parameteri_)(GLenum, GLenum, GLint) = nullptr;
    void (*parameterf_)(GLenum, GLenum, GLfloat) = nullptr;
    void (*parameteriv_)(GLenum, GLenum, const GLint*) = nullptr;
    void (*parameterfv_)(GLenum, GLenum, const GLfloat*) = nullptr;
    void (*get_parameteriv_)(GLenum, GLenum, GLint*) = nullptr;
    GLenum (*get_error_)() = nullptr;
};

TEST_F(TextureParameterCacheTest, RepeatedValueStaysSet) {
    NewTexture();
    parameteri_(GL_TEXTURE_2D_, GL_TEXTURE_MIN_FILTER_, GL_NEAREST_);
    parameteri_(GL_TEXTURE_2D_, GL_TEXTURE_MIN_FILTER_, GL_NEAREST_);
    EXPECT_EQ(Parameter(GL_TEXTURE_MIN_FILTER_), static_cast<GLint>(GL_NEAREST_));
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

// Each of the other setters changes the value behind glTexParameteri's back;
// setting the old value again afterwards must reach the driver.
TEST_F(TextureParameterCacheTest, OtherSettersDoNotLeaveAStaleValue) {
    NewTexture();
    const GLint linear = GL_LINEAR_;
    const GLfloat linear_f = static_cast<GLfloat>(GL_LINEAR_);
    const struct {
        const char* setter;
        std::function<void()> set_linear;
    } setters[] = {
        {"glTexParameterf", [&] { parameterf_(GL_TEXTURE_2D_, GL_TEXTURE_MAG_FILTER_, linear_f); }},
        {"glTexParameterfv", [&] { parameterfv_(GL_TEXTURE_2D_, GL_TEXTURE_MAG_FILTER_, &linear_f); }},
        {"glTexParameteriv", [&] { parameteriv_(GL_TEXTURE_2D_, GL_TEXTURE_MAG_FILTER_, &linear); }},
    };
    for (const auto& setter : setters) {
        parameteri_(GL_TEXTURE_2D_, GL_TEXTURE_MAG_FILTER_, GL_NEAREST_);
        setter.set_linear();
        ASSERT_EQ(Parameter(GL_TEXTURE_MAG_FILTER_), static_cast<GLint>(GL_LINEAR_)) << setter.setter;
        parameteri_(GL_TEXTURE_2D_, GL_TEXTURE_MAG_FILTER_, GL_NEAREST_);
        EXPECT_EQ(Parameter(GL_TEXTURE_MAG_FILTER_), static_cast<GLint>(GL_NEAREST_))
            << "glTexParameteri after " << setter.setter << " was dropped as redundant";
    }
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

TEST_F(TextureParameterCacheTest, EachTextureKeepsItsOwnValue) {
    const GLuint first = NewTexture();
    parameteri_(GL_TEXTURE_2D_, GL_TEXTURE_WRAP_S_, GL_CLAMP_TO_EDGE_);
    NewTexture();
    parameteri_(GL_TEXTURE_2D_, GL_TEXTURE_WRAP_S_, GL_MIRRORED_REPEAT_);
    bind_(GL_TEXTURE_2D_, first);
    parameteri_(GL_TEXTURE_2D_, GL_TEXTURE_WRAP_S_, GL_MIRRORED_REPEAT_);
    EXPECT_EQ(Parameter(GL_TEXTURE_WRAP_S_), static_cast<GLint>(GL_MIRRORED_REPEAT_))
        << "a value set on another texture was taken for this one's";
}

// A deleted name comes back from glGenTextures as a new object with default
// parameters; what the old object held must not be assumed of it.
TEST_F(TextureParameterCacheTest, ReusedNameStartsUnknown) {
    GLuint texture = NewTexture();
    parameteri_(GL_TEXTURE_2D_, GL_TEXTURE_WRAP_S_, GL_CLAMP_TO_EDGE_);
    delete_(1, &texture);
    GLuint again = 0;
    gen_(1, &again);
    bind_(GL_TEXTURE_2D_, again);
    ASSERT_EQ(Parameter(GL_TEXTURE_WRAP_S_), static_cast<GLint>(GL_REPEAT_)) << "a new object starts at REPEAT";
    parameteri_(GL_TEXTURE_2D_, GL_TEXTURE_WRAP_S_, GL_CLAMP_TO_EDGE_);
    EXPECT_EQ(Parameter(GL_TEXTURE_WRAP_S_), static_cast<GLint>(GL_CLAMP_TO_EDGE_))
        << (again == texture ? "the reused name kept the deleted object's value" : "");
}

// GL_CLAMP reaches GLES as GL_CLAMP_TO_EDGE, and the cache holds what the
// backend got: the two are the same value to it.
TEST_F(TextureParameterCacheTest, ClampAndClampToEdgeAreOneValue) {
    NewTexture();
    parameteri_(GL_TEXTURE_2D_, GL_TEXTURE_WRAP_S_, GL_CLAMP_);
    parameteri_(GL_TEXTURE_2D_, GL_TEXTURE_WRAP_S_, GL_CLAMP_TO_EDGE_);
    EXPECT_EQ(Parameter(GL_TEXTURE_WRAP_S_), static_cast<GLint>(GL_CLAMP_TO_EDGE_));
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

// A value the backend rejects changes nothing, so it is not remembered and
// raises its error every time.
TEST_F(TextureParameterCacheTest, RejectedValueIsRejectedEveryTime) {
    NewTexture();
    parameteri_(GL_TEXTURE_2D_, GL_TEXTURE_MAG_FILTER_, GL_REPEAT_);
    EXPECT_EQ(get_error_(), GL_INVALID_ENUM_);
    parameteri_(GL_TEXTURE_2D_, GL_TEXTURE_MAG_FILTER_, GL_REPEAT_);
    EXPECT_EQ(get_error_(), GL_INVALID_ENUM_) << "the repeat of an invalid value raised nothing";
    EXPECT_EQ(Parameter(GL_TEXTURE_MAG_FILTER_), static_cast<GLint>(GL_LINEAR_));
}

// A second context sharing the texture changes the filter; the first then
// sets its old value again, which must reach the driver.
TEST_F(TextureParameterCacheTest, ChangeFromASharedContextIsNotLost) {
    using CreateFn = EGLContext (*)(EGLDisplay, EGLConfig, EGLContext, const EGLint*);
    using MakeCurrentFn = EGLBoolean (*)(EGLDisplay, EGLSurface, EGLSurface, EGLContext);
    using DestroyFn = EGLBoolean (*)(EGLDisplay, EGLContext);
    auto create = Get<CreateFn>("eglCreateContext");
    auto make_current = Get<MakeCurrentFn>("eglMakeCurrent");
    auto destroy = Get<DestroyFn>("eglDestroyContext");
    ASSERT_NE(create, nullptr);
    ASSERT_NE(make_current, nullptr);

    const GLuint texture = NewTexture();
    parameteri_(GL_TEXTURE_2D_, GL_TEXTURE_MIN_FILTER_, GL_NEAREST_);
    parameteri_(GL_TEXTURE_2D_, GL_TEXTURE_MIN_FILTER_, GL_NEAREST_);

    const EGLint es3[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    const EGLContext other = create(display(), config(), context(), es3);
    if (other == EGL_NO_CONTEXT) GTEST_SKIP() << "no second, shared ES3 context";
    ASSERT_TRUE(make_current(display(), surface(), surface(), other));
    bind_(GL_TEXTURE_2D_, texture);
    parameteri_(GL_TEXTURE_2D_, GL_TEXTURE_MIN_FILTER_, GL_LINEAR_);
    ASSERT_EQ(Parameter(GL_TEXTURE_MIN_FILTER_), static_cast<GLint>(GL_LINEAR_)) << "set in the shared context";

    ASSERT_TRUE(make_current(display(), surface(), surface(), context()));
    bind_(GL_TEXTURE_2D_, texture);
    parameteri_(GL_TEXTURE_2D_, GL_TEXTURE_MIN_FILTER_, GL_NEAREST_);
    EXPECT_EQ(Parameter(GL_TEXTURE_MIN_FILTER_), static_cast<GLint>(GL_NEAREST_))
        << "dropped as redundant, though the shared context had changed it";
    destroy(display(), other);
}

} // namespace
