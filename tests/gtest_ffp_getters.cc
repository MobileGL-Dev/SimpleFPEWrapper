// SimpleFPEWrapper - tests/gtest_ffp_getters.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// Every state variable GL 2.1 defines that neither an ES 3.0+ nor a GL 3.2+
// core backend can answer - the whole fixed-function surface - checked
// through all four entry points at once.
//
// The sweep looks for the failure mode that made this worth doing: a query
// that raises GL_INVALID_ENUM and leaves the caller's buffer exactly as it
// was. Nothing about that is visible to the caller, which is how Sodium's
// fog occlusion came to cull the world against a cutoff it never read - it
// asks for GL_FOG_MODE, then the matching fog distance, and does the rest on
// the CPU. That sequence is checked on its own below.
//
// It runs over both backends. They are the two floors this wrapper builds
// on and they disagree about exactly the part it forwards: core removed the
// queries ES kept (the *_BITS family) and never had the ones ES added (the
// aliased ranges), and GL_GENERATE_MIPMAP_HINT goes the other way. A
// compatibility context would answer everything and prove nothing.

#include "gl2_state_table.h"
#include "sfpew_gtest.h"

#include <cmath>

namespace {

using sfpew_test::ContextTest;
using sfpew_test::DesktopContextTest;
using sfpew_test::GLboolean;
using sfpew_test::GLdouble;
using sfpew_test::GLenum;
using sfpew_test::GLfloat;
using sfpew_test::GLint;
using sfpew_test::kGl2StateTable;

constexpr GLenum GL_NO_ERROR_ = 0;
constexpr GLenum GL_INVALID_ENUM_ = 0x0500;
constexpr GLenum GL_EXP_ = 0x0800;
constexpr GLenum GL_LINEAR_ = 0x2601;
constexpr GLenum GL_SMOOTH_ = 0x1D01;
constexpr GLenum GL_FLAT_ = 0x1D00;
constexpr GLenum GL_MODELVIEW_ = 0x1700;
constexpr GLenum GL_PROJECTION_ = 0x1701;
constexpr GLenum GL_FLOAT_ = 0x1406;
constexpr GLenum GL_TEXTURE0_ = 0x84C0;
constexpr GLenum GL_RENDER_ = 0x1C00;
constexpr GLenum GL_FILL_ = 0x1B02;
constexpr GLenum GL_NICEST_ = 0x1102;
constexpr GLenum GL_COLOR_ARRAY_ = 0x8076;
constexpr GLenum GL_MAP1_VERTEX_3_ = 0x0D97;
constexpr unsigned GL_ALL_ATTRIB_BITS_ = 0x000FFFFF;

// The state queries, bound once per case.
struct Queries {
    void (*get_integerv)(GLenum, GLint*) = nullptr;
    void (*get_floatv)(GLenum, GLfloat*) = nullptr;
    void (*get_booleanv)(GLenum, GLboolean*) = nullptr;
    void (*get_doublev)(GLenum, GLdouble*) = nullptr;
    GLboolean (*is_enabled)(GLenum) = nullptr;
    GLenum (*get_error)() = nullptr;

    void Drain() const {
        while (get_error() != GL_NO_ERROR_) {}
    }
    GLint Integer(GLenum pname) const {
        GLint value = -12345;
        get_integerv(pname, &value);
        return value;
    }
    GLfloat Float(GLenum pname) const {
        GLfloat value = -12345.0f;
        get_floatv(pname, &value);
        return value;
    }
};

Queries Bind(ContextTest* test) {
    Queries q;
    q.get_integerv = test->Get<void (*)(GLenum, GLint*)>("glGetIntegerv");
    q.get_floatv = test->Get<void (*)(GLenum, GLfloat*)>("glGetFloatv");
    q.get_booleanv = test->Get<void (*)(GLenum, GLboolean*)>("glGetBooleanv");
    q.get_doublev = test->Get<void (*)(GLenum, GLdouble*)>("glGetDoublev");
    q.is_enabled = test->Get<GLboolean (*)(GLenum)>("glIsEnabled");
    q.get_error = test->Get<GLenum (*)()>("glGetError");
    return q;
}

// The sweep, shared by both backends.
void SweepEveryStateVariable(const Queries& q) {
    q.Drain();
    for (const auto& row : kGl2StateTable) {
        GLint iv[16];
        GLfloat fv[16];
        GLdouble dv[16];
        GLboolean bv[16];
        for (int k = 0; k < 16; ++k) {
            iv[k] = -12345;
            fv[k] = -12345.0f;
            dv[k] = -12345.0;
            bv[k] = 42;
        }
        q.get_integerv(row.pname, iv);
        const GLenum ei = q.get_error();
        q.get_floatv(row.pname, fv);
        const GLenum ef = q.get_error();
        q.get_booleanv(row.pname, bv);
        const GLenum eb = q.get_error();
        q.get_doublev(row.pname, dv);
        const GLenum ed = q.get_error();

        if (ei != 0 || ef != 0 || eb != 0 || ed != 0) {
            ADD_FAILURE() << row.name << " raised " << std::hex << ei << '/' << ef << '/' << eb
                          << '/' << ed;
            continue;
        }
        // The failure that hides: no error, and the buffer never written.
        if (iv[0] == -12345 && fv[0] == -12345.0f && dv[0] == -12345.0) {
            ADD_FAILURE() << row.name << " answered nothing at all";
            continue;
        }
        // The four forms are one value in four types. Colours and normals
        // are the documented exception for the integer form, so compare the
        // ones that never rescale.
        for (int k = 0; k < row.count; ++k) {
            EXPECT_NEAR(dv[k], static_cast<GLdouble>(fv[k]), 1e-4 * (1.0 + std::fabs(dv[k])))
                << row.name << '[' << k << "]: the float and double forms disagree";
            EXPECT_EQ(bv[k], dv[k] != 0.0 ? 1 : 0)
                << row.name << '[' << k << "]: value " << dv[k] << " but boolean says "
                << static_cast<int>(bv[k]);
        }
    }
}

class Gl2StateTest : public ContextTest {};
class Gl2StateDesktopTest : public DesktopContextTest {};

TEST_F(Gl2StateTest, EveryStateVariableAnswersOnGles) {
    SweepEveryStateVariable(Bind(this));
}

TEST_F(Gl2StateDesktopTest, EveryStateVariableAnswersOnACoreProfile) {
    SweepEveryStateVariable(Bind(this));
}

TEST_F(Gl2StateTest, InitialValuesMatchTheManual) {
    const Queries q = Bind(this);
    q.Drain();

    EXPECT_EQ(q.Integer(0x0B65), static_cast<GLint>(GL_EXP_)) << "GL_FOG_MODE";
    EXPECT_FLOAT_EQ(q.Float(0x0B62), 1.0f) << "GL_FOG_DENSITY";
    EXPECT_FLOAT_EQ(q.Float(0x0B63), 0.0f) << "GL_FOG_START";
    EXPECT_FLOAT_EQ(q.Float(0x0B64), 1.0f) << "GL_FOG_END";
    EXPECT_EQ(q.Integer(0x0B54), static_cast<GLint>(GL_SMOOTH_)) << "GL_SHADE_MODEL";
    EXPECT_EQ(q.Integer(0x0BA0), static_cast<GLint>(GL_MODELVIEW_)) << "GL_MATRIX_MODE";
    EXPECT_EQ(q.Integer(0x0BA3), 1) << "GL_MODELVIEW_STACK_DEPTH";
    EXPECT_EQ(q.Integer(0x0D31), 8) << "GL_MAX_LIGHTS";
    EXPECT_EQ(q.Integer(0x0B32), 0) << "GL_LIST_BASE";
    EXPECT_EQ(q.Integer(0x0C40), static_cast<GLint>(GL_RENDER_)) << "GL_RENDER_MODE";
    EXPECT_EQ(q.Integer(0x807A), 4) << "GL_VERTEX_ARRAY_SIZE";
    EXPECT_EQ(q.Integer(0x807B), static_cast<GLint>(GL_FLOAT_)) << "GL_VERTEX_ARRAY_TYPE";
    EXPECT_EQ(q.Integer(0x84E1), static_cast<GLint>(GL_TEXTURE0_)) << "GL_CLIENT_ACTIVE_TEXTURE";
    EXPECT_EQ(q.Integer(0x0B40), static_cast<GLint>(GL_FILL_)) << "GL_POLYGON_MODE";
    EXPECT_EQ(q.Integer(0x0BB0), 0) << "GL_ATTRIB_STACK_DEPTH";
    EXPECT_EQ(q.Integer(0x0DD1), 1) << "GL_MAP1_GRID_SEGMENTS";
    EXPECT_FLOAT_EQ(q.Float(0x0D16), 1.0f) << "GL_ZOOM_X";
    EXPECT_FLOAT_EQ(q.Float(0x0BC2), 0.0f) << "GL_ALPHA_TEST_REF";
    EXPECT_FLOAT_EQ(q.Float(0x0D14), 1.0f) << "GL_RED_SCALE";
    EXPECT_FLOAT_EQ(q.Float(0x0D15), 0.0f) << "GL_RED_BIAS";

    GLfloat colour[4] = {0, 0, 0, 0};
    q.get_floatv(0x0B00, colour); // GL_CURRENT_COLOR
    EXPECT_FLOAT_EQ(colour[0], 1.0f);
    EXPECT_FLOAT_EQ(colour[3], 1.0f);

    GLfloat matrix[16] = {};
    q.get_floatv(0x0BA6, matrix); // GL_MODELVIEW_MATRIX
    for (int i = 0; i < 16; ++i)
        EXPECT_FLOAT_EQ(matrix[i], (i % 5) == 0 ? 1.0f : 0.0f)
            << "GL_MODELVIEW_MATRIX is not the identity at " << i;
    EXPECT_EQ(q.get_error(), GL_NO_ERROR_);
}

// Sodium reads GL_FOG_MODE, then whichever distance that mode uses, and
// culls chunks against the result without another GL call.
TEST_F(Gl2StateTest, FogOcclusionReadsBackWhatItSet) {
    const Queries q = Bind(this);
    auto fogi = Get<void (*)(GLenum, GLint)>("glFogi");
    auto fogf = Get<void (*)(GLenum, GLfloat)>("glFogf");
    ASSERT_NE(fogi, nullptr);
    ASSERT_NE(fogf, nullptr);
    q.Drain();

    fogi(0x0B65, static_cast<GLint>(GL_LINEAR_)); // GL_FOG_MODE
    fogf(0x0B64, 128.0f);                         // GL_FOG_END
    EXPECT_EQ(q.Integer(0x0B65), static_cast<GLint>(GL_LINEAR_));
    EXPECT_FLOAT_EQ(q.Float(0x0B64), 128.0f);

    fogi(0x0B65, static_cast<GLint>(GL_EXP_));
    fogf(0x0B62, 0.015f); // GL_FOG_DENSITY
    EXPECT_EQ(q.Integer(0x0B65), static_cast<GLint>(GL_EXP_));
    EXPECT_FLOAT_EQ(q.Float(0x0B62), 0.015f);
    EXPECT_EQ(q.get_error(), GL_NO_ERROR_);
}

TEST_F(Gl2StateTest, StateSetThroughTheApiComesBackOut) {
    const Queries q = Bind(this);
    auto shade_model = Get<void (*)(GLenum)>("glShadeModel");
    auto hint = Get<void (*)(GLenum, GLenum)>("glHint");
    auto clear_accum = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glClearAccum");
    auto map_grid1f = Get<void (*)(GLint, GLfloat, GLfloat)>("glMapGrid1f");
    auto matrix_mode = Get<void (*)(GLenum)>("glMatrixMode");
    auto push_matrix = Get<void (*)()>("glPushMatrix");
    auto pop_matrix = Get<void (*)()>("glPopMatrix");
    auto push_attrib = Get<void (*)(unsigned)>("glPushAttrib");
    auto pop_attrib = Get<void (*)()>("glPopAttrib");
    ASSERT_NE(shade_model, nullptr);
    q.Drain();

    shade_model(GL_FLAT_);
    EXPECT_EQ(q.Integer(0x0B54), static_cast<GLint>(GL_FLAT_)) << "glShadeModel round trip";

    // A hint the wrapper accepts as a no-op is still state, and has to read
    // back - on either backend, whichever one cannot take it.
    hint(0x0C54, GL_NICEST_); // GL_FOG_HINT
    EXPECT_EQ(q.Integer(0x0C54), static_cast<GLint>(GL_NICEST_)) << "glHint(GL_FOG_HINT)";
    hint(0x8192, GL_NICEST_); // GL_GENERATE_MIPMAP_HINT
    EXPECT_EQ(q.Integer(0x8192), static_cast<GLint>(GL_NICEST_))
        << "glHint(GL_GENERATE_MIPMAP_HINT)";

    clear_accum(0.25f, 0.5f, 0.75f, 1.0f);
    GLfloat accum[4] = {};
    q.get_floatv(0x0B80, accum); // GL_ACCUM_CLEAR_VALUE
    EXPECT_FLOAT_EQ(accum[0], 0.25f);
    EXPECT_FLOAT_EQ(accum[2], 0.75f);

    map_grid1f(7, 2.0f, 5.0f);
    EXPECT_EQ(q.Integer(0x0DD1), 7) << "GL_MAP1_GRID_SEGMENTS";
    GLfloat domain[2] = {};
    q.get_floatv(0x0DD0, domain); // GL_MAP1_GRID_DOMAIN
    EXPECT_FLOAT_EQ(domain[0], 2.0f);
    EXPECT_FLOAT_EQ(domain[1], 5.0f);

    matrix_mode(GL_PROJECTION_);
    push_matrix();
    EXPECT_EQ(q.Integer(0x0BA4), 2) << "GL_PROJECTION_STACK_DEPTH after push";
    pop_matrix();
    matrix_mode(GL_MODELVIEW_);

    push_attrib(GL_ALL_ATTRIB_BITS_);
    EXPECT_EQ(q.Integer(0x0BB0), 1) << "GL_ATTRIB_STACK_DEPTH after push";
    pop_attrib();
    EXPECT_EQ(q.Integer(0x0BB0), 0) << "GL_ATTRIB_STACK_DEPTH after pop";
    EXPECT_EQ(q.get_error(), GL_NO_ERROR_);
}

TEST_F(Gl2StateTest, EnablesAgreeBetweenIsEnabledAndTheGetters) {
    const Queries q = Bind(this);
    auto enable_client_state = Get<void (*)(GLenum)>("glEnableClientState");
    auto enable = Get<void (*)(GLenum)>("glEnable");
    ASSERT_NE(enable_client_state, nullptr);
    ASSERT_NE(enable, nullptr);
    q.Drain();

    enable_client_state(GL_COLOR_ARRAY_);
    GLboolean enabled = 0;
    q.get_booleanv(GL_COLOR_ARRAY_, &enabled);
    EXPECT_NE(enabled, 0) << "glGetBooleanv disagrees with glEnableClientState";
    EXPECT_NE(q.is_enabled(GL_COLOR_ARRAY_), 0) << "glIsEnabled disagrees with glGetBooleanv";

    enable(GL_MAP1_VERTEX_3_);
    EXPECT_NE(q.is_enabled(GL_MAP1_VERTEX_3_), 0) << "an evaluator map enable did not stick";
    q.Drain();

    // A state variable is not a capability: glIsEnabled has to reject it.
    q.is_enabled(0x0B65); // GL_FOG_MODE
    EXPECT_EQ(q.get_error(), GL_INVALID_ENUM_)
        << "glIsEnabled(GL_FOG_MODE) answered instead of rejecting";
}

// Pointers, evaluator control points and pixel maps cannot come back through
// glGet, so they have their own entry points. A null function pointer in the
// caller's hand is the failure mode here, which is why resolving them is
// half the test.
TEST_F(Gl2StateTest, GettersThatCannotGoThroughGlGet) {
    const Queries q = Bind(this);
    auto get_pointerv = Get<void (*)(GLenum, void**)>("glGetPointerv");
    auto vertex_pointer = Get<void (*)(GLint, GLenum, int, const void*)>("glVertexPointer");
    auto get_mapfv = Get<void (*)(GLenum, GLenum, GLfloat*)>("glGetMapfv");
    auto get_mapiv = Get<void (*)(GLenum, GLenum, GLint*)>("glGetMapiv");
    auto map1f = Get<void (*)(GLenum, GLfloat, GLfloat, GLint, GLint, const GLfloat*)>("glMap1f");
    auto get_pixel_mapfv = Get<void (*)(GLenum, GLfloat*)>("glGetPixelMapfv");
    ASSERT_NE(get_pointerv, nullptr);
    ASSERT_NE(get_mapfv, nullptr);
    ASSERT_NE(get_pixel_mapfv, nullptr);
    q.Drain();

    static const GLfloat verts[12] = {};
    vertex_pointer(3, GL_FLOAT_, 0, verts);
    void* pointer = nullptr;
    get_pointerv(0x808E, &pointer); // GL_VERTEX_ARRAY_POINTER
    EXPECT_EQ(pointer, static_cast<const void*>(verts));

    const GLfloat control[8] = {0, 0, 0, 1, 1, 1, 2, 4};
    map1f(GL_MAP1_VERTEX_3_, 0.0f, 4.0f, 3, 2, control);
    GLint order = 0;
    get_mapiv(GL_MAP1_VERTEX_3_, 0x0A01, &order); // GL_ORDER
    EXPECT_EQ(order, 2);
    GLfloat domain[2] = {};
    get_mapfv(GL_MAP1_VERTEX_3_, 0x0A02, domain); // GL_DOMAIN
    EXPECT_FLOAT_EQ(domain[0], 0.0f);
    EXPECT_FLOAT_EQ(domain[1], 4.0f);
    GLfloat coefficients[6] = {-1, -1, -1, -1, -1, -1};
    get_mapfv(GL_MAP1_VERTEX_3_, 0x0A00, coefficients); // GL_COEFF
    EXPECT_FLOAT_EQ(coefficients[3], 1.0f) << "GL_COEFF did not return the control points";
    EXPECT_FLOAT_EQ(coefficients[5], 1.0f);

    // Pixel maps are not implemented, so every table is still the identity
    // GL starts with - which is an answer, where a null pointer was not.
    GLfloat entry = -1.0f;
    get_pixel_mapfv(0x0C70, &entry); // GL_PIXEL_MAP_I_TO_I
    EXPECT_FLOAT_EQ(entry, 0.0f);
    EXPECT_EQ(q.get_error(), GL_NO_ERROR_);
}

} // namespace
