// SimpleFPEWrapper - tests/gtest_point_parameters.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

#include "sfpew_gtest.h"

namespace {

using sfpew_test::ContextTest;
using sfpew_test::GLbitfield;
using sfpew_test::GLboolean;
using sfpew_test::GLdouble;
using sfpew_test::GLenum;
using sfpew_test::GLfloat;
using sfpew_test::GLint;
using sfpew_test::GLsizei;
using sfpew_test::GLuint;
using sfpew_test::PixelProbe;

constexpr GLenum GL_NO_ERROR_ = 0;
constexpr GLenum GL_INVALID_ENUM_ = 0x0500;
constexpr GLenum GL_INVALID_VALUE_ = 0x0501;
constexpr GLenum GL_POINT_SIZE_ = 0x0B11;
constexpr GLenum GL_POINT_SIZE_RANGE_ = 0x0B12;
constexpr GLenum GL_POINT_SIZE_MIN_ = 0x8126;
constexpr GLenum GL_POINT_SIZE_MAX_ = 0x8127;
constexpr GLenum GL_POINT_FADE_THRESHOLD_SIZE_ = 0x8128;
constexpr GLenum GL_POINT_DISTANCE_ATTENUATION_ = 0x8129;
constexpr GLenum GL_POINT_SPRITE_COORD_ORIGIN_ = 0x8CA0;
constexpr GLenum GL_LOWER_LEFT_ = 0x8CA1;
constexpr GLenum GL_UPPER_LEFT_ = 0x8CA2;
constexpr GLbitfield GL_POINT_BIT_ = 0x00000002;
constexpr GLenum GL_COMPILE_ = 0x1300;
constexpr GLenum GL_POINTS_ = 0x0000;
constexpr GLenum GL_PROJECTION_ = 0x1701;
constexpr GLenum GL_MODELVIEW_ = 0x1700;
constexpr GLbitfield GL_COLOR_BUFFER_BIT_ = 0x00004000;

class PointParametersTest : public ContextTest {
protected:
    void SetUp() override {
        ContextTest::SetUp();
        if (::testing::Test::IsSkipped() || ::testing::Test::HasFatalFailure()) return;
        using MakeCurrentFn = EGLBoolean (*)(EGLDisplay, EGLSurface, EGLSurface, EGLContext);
        auto wrapper_make_current = Get<MakeCurrentFn>("eglMakeCurrent");
        ASSERT_TRUE(wrapper_make_current(display(), surface(), surface(), eglGetCurrentContext()));
        point_parameterf_ = Get<void (*)(GLenum, GLfloat)>("glPointParameterf");
        point_parameterfv_ = Get<void (*)(GLenum, const GLfloat*)>("glPointParameterfv");
        point_parameteri_ = Get<void (*)(GLenum, GLint)>("glPointParameteri");
        point_parameteriv_ = Get<void (*)(GLenum, const GLint*)>("glPointParameteriv");
        get_floatv_ = Get<void (*)(GLenum, GLfloat*)>("glGetFloatv");
        get_doublev_ = Get<void (*)(GLenum, GLdouble*)>("glGetDoublev");
        get_integerv_ = Get<void (*)(GLenum, GLint*)>("glGetIntegerv");
        get_booleanv_ = Get<void (*)(GLenum, GLboolean*)>("glGetBooleanv");
        get_error_ = Get<GLenum (*)()>("glGetError");
        get_error_();
    }

    void ExpectScalar(GLenum pname, GLfloat expected) {
        GLfloat as_float = -1.0f;
        GLdouble as_double = -1.0;
        GLint as_integer = -1;
        GLboolean as_boolean = 0;
        get_floatv_(pname, &as_float);
        get_doublev_(pname, &as_double);
        get_integerv_(pname, &as_integer);
        get_booleanv_(pname, &as_boolean);
        EXPECT_NEAR(as_float, expected, 1e-6f);
        EXPECT_DOUBLE_EQ(as_double, static_cast<GLdouble>(expected));
        EXPECT_EQ(as_integer, static_cast<GLint>(expected));
        EXPECT_EQ(as_boolean != 0, expected != 0.0f);
        EXPECT_EQ(get_error_(), GL_NO_ERROR_);
    }

    void ExpectVector3(GLenum pname, const GLfloat (&expected)[3]) {
        GLfloat as_float[3] = {};
        GLdouble as_double[3] = {};
        GLint as_integer[3] = {};
        GLboolean as_boolean[3] = {};
        get_floatv_(pname, as_float);
        get_doublev_(pname, as_double);
        get_integerv_(pname, as_integer);
        get_booleanv_(pname, as_boolean);
        for (int i = 0; i < 3; ++i) {
            EXPECT_FLOAT_EQ(as_float[i], expected[i]);
            EXPECT_DOUBLE_EQ(as_double[i], static_cast<GLdouble>(expected[i]));
            EXPECT_EQ(as_integer[i], static_cast<GLint>(expected[i]));
            EXPECT_EQ(as_boolean[i] != 0, expected[i] != 0.0f);
        }
        EXPECT_EQ(get_error_(), GL_NO_ERROR_);
    }

    void (*point_parameterf_)(GLenum, GLfloat) = nullptr;
    void (*point_parameterfv_)(GLenum, const GLfloat*) = nullptr;
    void (*point_parameteri_)(GLenum, GLint) = nullptr;
    void (*point_parameteriv_)(GLenum, const GLint*) = nullptr;
    void (*get_floatv_)(GLenum, GLfloat*) = nullptr;
    void (*get_doublev_)(GLenum, GLdouble*) = nullptr;
    void (*get_integerv_)(GLenum, GLint*) = nullptr;
    void (*get_booleanv_)(GLenum, GLboolean*) = nullptr;
    GLenum (*get_error_)() = nullptr;
};

TEST_F(PointParametersTest, StateRoundTripsThroughEverySetterAndGetterFamily) {
    GLfloat range[2] = {};
    get_floatv_(GL_POINT_SIZE_RANGE_, range);
    GLfloat initial_max = 0.0f;
    get_floatv_(GL_POINT_SIZE_MAX_, &initial_max);
    EXPECT_NEAR(initial_max, range[1], 1e-6f);
    EXPECT_GE(initial_max, 1.0f);

    point_parameterf_(GL_POINT_SIZE_MIN_, 2.0f);
    ExpectScalar(GL_POINT_SIZE_MIN_, 2.0f);
    const GLfloat min_fv = 3.0f;
    point_parameterfv_(GL_POINT_SIZE_MIN_, &min_fv);
    ExpectScalar(GL_POINT_SIZE_MIN_, 3.0f);
    point_parameteri_(GL_POINT_SIZE_MIN_, 4);
    ExpectScalar(GL_POINT_SIZE_MIN_, 4.0f);
    const GLint min_iv = 5;
    point_parameteriv_(GL_POINT_SIZE_MIN_, &min_iv);
    ExpectScalar(GL_POINT_SIZE_MIN_, 5.0f);

    point_parameterf_(GL_POINT_SIZE_MAX_, 24.0f);
    ExpectScalar(GL_POINT_SIZE_MAX_, 24.0f);
    const GLfloat max_fv = 25.0f;
    point_parameterfv_(GL_POINT_SIZE_MAX_, &max_fv);
    ExpectScalar(GL_POINT_SIZE_MAX_, 25.0f);
    point_parameteri_(GL_POINT_SIZE_MAX_, 26);
    ExpectScalar(GL_POINT_SIZE_MAX_, 26.0f);
    const GLint max_iv = 27;
    point_parameteriv_(GL_POINT_SIZE_MAX_, &max_iv);
    ExpectScalar(GL_POINT_SIZE_MAX_, 27.0f);

    point_parameterf_(GL_POINT_FADE_THRESHOLD_SIZE_, 6.0f);
    ExpectScalar(GL_POINT_FADE_THRESHOLD_SIZE_, 6.0f);
    const GLfloat fade_fv = 7.0f;
    point_parameterfv_(GL_POINT_FADE_THRESHOLD_SIZE_, &fade_fv);
    ExpectScalar(GL_POINT_FADE_THRESHOLD_SIZE_, 7.0f);
    point_parameteri_(GL_POINT_FADE_THRESHOLD_SIZE_, 8);
    ExpectScalar(GL_POINT_FADE_THRESHOLD_SIZE_, 8.0f);
    const GLint fade_iv = 9;
    point_parameteriv_(GL_POINT_FADE_THRESHOLD_SIZE_, &fade_iv);
    ExpectScalar(GL_POINT_FADE_THRESHOLD_SIZE_, 9.0f);

    point_parameterf_(GL_POINT_SPRITE_COORD_ORIGIN_, static_cast<GLfloat>(GL_LOWER_LEFT_));
    ExpectScalar(GL_POINT_SPRITE_COORD_ORIGIN_, static_cast<GLfloat>(GL_LOWER_LEFT_));
    const GLfloat upper_fv = static_cast<GLfloat>(GL_UPPER_LEFT_);
    point_parameterfv_(GL_POINT_SPRITE_COORD_ORIGIN_, &upper_fv);
    ExpectScalar(GL_POINT_SPRITE_COORD_ORIGIN_, static_cast<GLfloat>(GL_UPPER_LEFT_));
    point_parameteri_(GL_POINT_SPRITE_COORD_ORIGIN_, GL_LOWER_LEFT_);
    ExpectScalar(GL_POINT_SPRITE_COORD_ORIGIN_, static_cast<GLfloat>(GL_LOWER_LEFT_));
    const GLint upper_iv = GL_UPPER_LEFT_;
    point_parameteriv_(GL_POINT_SPRITE_COORD_ORIGIN_, &upper_iv);
    ExpectScalar(GL_POINT_SPRITE_COORD_ORIGIN_, static_cast<GLfloat>(GL_UPPER_LEFT_));

    const GLfloat attenuation_f[3] = {1.0f, 2.0f, 3.0f};
    point_parameterfv_(GL_POINT_DISTANCE_ATTENUATION_, attenuation_f);
    ExpectVector3(GL_POINT_DISTANCE_ATTENUATION_, attenuation_f);
    const GLint attenuation_i[3] = {4, 5, 6};
    point_parameteriv_(GL_POINT_DISTANCE_ATTENUATION_, attenuation_i);
    const GLfloat attenuation_i_as_float[3] = {4.0f, 5.0f, 6.0f};
    ExpectVector3(GL_POINT_DISTANCE_ATTENUATION_, attenuation_i_as_float);
}

TEST_F(PointParametersTest, ErrorsAndEveryAdvertisedAliasFollowTheContract) {
    point_parameterf_(GL_POINT_SIZE_MIN_, -1.0f);
    EXPECT_EQ(get_error_(), GL_INVALID_VALUE_);
    point_parameterf_(GL_POINT_SIZE_MAX_, -1.0f);
    EXPECT_EQ(get_error_(), GL_INVALID_VALUE_);
    point_parameterf_(GL_POINT_FADE_THRESHOLD_SIZE_, -1.0f);
    EXPECT_EQ(get_error_(), GL_INVALID_VALUE_);
    point_parameterf_(GL_POINT_DISTANCE_ATTENUATION_, 1.0f);
    EXPECT_EQ(get_error_(), GL_INVALID_ENUM_);
    point_parameteri_(GL_POINT_DISTANCE_ATTENUATION_, 1);
    EXPECT_EQ(get_error_(), GL_INVALID_ENUM_);
    point_parameteri_(GL_POINT_SPRITE_COORD_ORIGIN_, 0);
    EXPECT_EQ(get_error_(), GL_INVALID_VALUE_);
    point_parameterf_(0xDEADu, 1.0f);
    EXPECT_EQ(get_error_(), GL_INVALID_ENUM_);

    EXPECT_EQ(Get<void*>("glPointParameterf"), Get<void*>("glPointParameterfARB"));
    EXPECT_EQ(Get<void*>("glPointParameterfv"), Get<void*>("glPointParameterfvARB"));
    EXPECT_EQ(Get<void*>("glPointParameterf"), Get<void*>("glPointParameterfEXT"));
    EXPECT_EQ(Get<void*>("glPointParameterfv"), Get<void*>("glPointParameterfvEXT"));
    EXPECT_EQ(Get<void*>("glPointParameterf"), Get<void*>("glPointParameterfSGIS"));
    EXPECT_EQ(Get<void*>("glPointParameterfv"), Get<void*>("glPointParameterfvSGIS"));
    EXPECT_EQ(Get<void*>("glPointParameteri"), Get<void*>("glPointParameteriNV"));
    EXPECT_EQ(Get<void*>("glPointParameteriv"), Get<void*>("glPointParameterivNV"));
}

TEST_F(PointParametersTest, PointBitAndDisplayListsPreserveTheWholeState) {
    auto point_size = Get<void (*)(GLfloat)>("glPointSize");
    auto push_attrib = Get<void (*)(GLbitfield)>("glPushAttrib");
    auto pop_attrib = Get<void (*)()>("glPopAttrib");

    const GLfloat attenuation[3] = {0.0f, 0.0f, 1.0f};
    point_size(12.0f);
    point_parameterf_(GL_POINT_SIZE_MIN_, 2.0f);
    point_parameterf_(GL_POINT_SIZE_MAX_, 30.0f);
    point_parameterf_(GL_POINT_FADE_THRESHOLD_SIZE_, 4.0f);
    point_parameterfv_(GL_POINT_DISTANCE_ATTENUATION_, attenuation);
    point_parameteri_(GL_POINT_SPRITE_COORD_ORIGIN_, GL_LOWER_LEFT_);

    push_attrib(GL_POINT_BIT_);
    const GLfloat changed_attenuation[3] = {1.0f, 0.0f, 0.0f};
    point_size(3.0f);
    point_parameterf_(GL_POINT_SIZE_MIN_, 0.0f);
    point_parameterf_(GL_POINT_SIZE_MAX_, 3.0f);
    point_parameterf_(GL_POINT_FADE_THRESHOLD_SIZE_, 1.0f);
    point_parameterfv_(GL_POINT_DISTANCE_ATTENUATION_, changed_attenuation);
    point_parameteri_(GL_POINT_SPRITE_COORD_ORIGIN_, GL_UPPER_LEFT_);
    pop_attrib();

    ExpectScalar(GL_POINT_SIZE_, 12.0f);
    ExpectScalar(GL_POINT_SIZE_MIN_, 2.0f);
    ExpectScalar(GL_POINT_SIZE_MAX_, 30.0f);
    ExpectScalar(GL_POINT_FADE_THRESHOLD_SIZE_, 4.0f);
    ExpectScalar(GL_POINT_SPRITE_COORD_ORIGIN_, static_cast<GLfloat>(GL_LOWER_LEFT_));
    GLfloat restored[3] = {};
    get_floatv_(GL_POINT_DISTANCE_ATTENUATION_, restored);
    EXPECT_FLOAT_EQ(restored[0], 0.0f);
    EXPECT_FLOAT_EQ(restored[1], 0.0f);
    EXPECT_FLOAT_EQ(restored[2], 1.0f);

    const GLuint list = Get<GLuint (*)(GLsizei)>("glGenLists")(1);
    ASSERT_NE(list, 0u);
    auto new_list = Get<void (*)(GLuint, GLenum)>("glNewList");
    auto end_list = Get<void (*)()>("glEndList");
    auto call_list = Get<void (*)(GLuint)>("glCallList");
    GLfloat captured[3] = {1.0f, 2.0f, 3.0f};
    new_list(list, GL_COMPILE_);
    point_parameterfv_(GL_POINT_DISTANCE_ATTENUATION_, captured);
    end_list();
    captured[0] = captured[1] = captured[2] = 0.0f;
    call_list(list);
    get_floatv_(GL_POINT_DISTANCE_ATTENUATION_, restored);
    EXPECT_FLOAT_EQ(restored[0], 1.0f);
    EXPECT_FLOAT_EQ(restored[1], 2.0f);
    EXPECT_FLOAT_EQ(restored[2], 3.0f);
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

TEST_F(PointParametersTest, DistanceAttenuationChangesRenderedPointArea) {
    auto viewport = Get<void (*)(GLint, GLint, GLsizei, GLsizei)>("glViewport");
    auto matrix_mode = Get<void (*)(GLenum)>("glMatrixMode");
    auto load_identity = Get<void (*)()>("glLoadIdentity");
    auto ortho =
        Get<void (*)(GLdouble, GLdouble, GLdouble, GLdouble, GLdouble, GLdouble)>("glOrtho");
    auto point_size = Get<void (*)(GLfloat)>("glPointSize");
    auto clear_color = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glClearColor");
    auto clear = Get<void (*)(GLbitfield)>("glClear");
    auto begin = Get<void (*)(GLenum)>("glBegin");
    auto color = Get<void (*)(GLfloat, GLfloat, GLfloat)>("glColor3f");
    auto vertex = Get<void (*)(GLfloat, GLfloat, GLfloat)>("glVertex3f");
    auto end = Get<void (*)()>("glEnd");
    auto finish = Get<void (*)()>("glFinish");
    auto read_pixels =
        Get<void (*)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*)>("glReadPixels");
    PixelProbe probe(read_pixels);

    viewport(0, 0, size(), size());
    matrix_mode(GL_PROJECTION_);
    load_identity();
    ortho(-1.0, 1.0, -1.0, 1.0, 1.0, 10.0);
    matrix_mode(GL_MODELVIEW_);
    load_identity();
    point_size(24.0f);
    clear_color(0, 0, 0, 1);
    ASSERT_EQ(get_error_(), GL_NO_ERROR_) << "render setup";

    const auto draw_at = [&](GLfloat distance) {
        clear(GL_COLOR_BUFFER_BIT_);
        begin(GL_POINTS_);
        color(0.0f, 1.0f, 0.0f);
        vertex(0.0f, 0.0f, -distance);
        end();
        finish();
        EXPECT_EQ(get_error_(), GL_NO_ERROR_) << "draw at eye distance " << distance;
        const int count = probe.FindLit(0, 0, size(), size()).count;
        EXPECT_EQ(get_error_(), GL_NO_ERROR_) << "readback at eye distance " << distance;
        return count;
    };

    const GLfloat default_attenuation[3] = {1.0f, 0.0f, 0.0f};
    point_parameterfv_(GL_POINT_DISTANCE_ATTENUATION_, default_attenuation);
    ASSERT_EQ(get_error_(), GL_NO_ERROR_) << "default attenuation";
    const int default_near = draw_at(2.0f);
    const int default_far = draw_at(5.0f);
    EXPECT_EQ(default_near, default_far);
    EXPECT_GT(default_near, 0);

    const GLfloat distance_attenuation[3] = {0.0f, 0.0f, 1.0f};
    point_parameterfv_(GL_POINT_DISTANCE_ATTENUATION_, distance_attenuation);
    ASSERT_EQ(get_error_(), GL_NO_ERROR_) << "distance attenuation";
    const int attenuated_near = draw_at(2.0f);
    const int attenuated_far = draw_at(5.0f);
    EXPECT_GT(attenuated_near, attenuated_far)
        << "near=" << attenuated_near << ", far=" << attenuated_far;
    EXPECT_GT(attenuated_far, 0);
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

} // namespace
