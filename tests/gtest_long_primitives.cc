// SimpleFPEWrapper - tests/gtest_long_primitives.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// Ported from piglit's tests/general/longprim.c (MIT-style license; see
// piglit's COPYING): one glBegin/glEnd pair holding an enormous number of
// vertices, for every primitive mode.
//
// This is the only test in the suite that pushes the immediate-mode
// collector far past its comfortable sizes, so it is the one that covers:
//
//   - fixed_function_draw_state_t::vb growing across many reallocations
//     while advance() keeps packing into it;
//   - the merge path correctly DECLINING these runs (they are far past
//     kImmediateMergeVertexLimit) and falling through to a direct draw;
//   - a single upload larger than the streaming ring's default capacity,
//     which forces sfpewUploadToRing to finish outstanding work, replace the
//     buffer object and retry at a bigger power-of-two size - the path that
//     also has to invalidate every cached attribute binding, since the
//     buffer NAME changes underneath them.
//
// Upstream goes to a million vertices; this port stops at 200k per run,
// which still crosses the ring-growth threshold for the widest vertex
// layouts while keeping the test a few seconds rather than a minute. Each
// run is checked for GL errors and the result is probed for actual
// geometry - a silently dropped draw would otherwise look like a pass.

#include "sfpew_gtest.h"

#include <optional>
#include <ostream>
#include <string>
#include <vector>

namespace {

using sfpew_test::ContextTest;
using sfpew_test::GLbitfield;
using sfpew_test::GLenum;
using sfpew_test::GLfloat;
using sfpew_test::GLint;
using sfpew_test::GLsizei;
using sfpew_test::PixelProbe;

constexpr int kWindow = 64;
constexpr GLbitfield GL_COLOR_BUFFER_BIT_ = 0x00004000;
constexpr GLenum GL_PROJECTION_ = 0x1701;
constexpr GLenum GL_MODELVIEW_ = 0x1700;
constexpr GLenum GL_NO_ERROR_ = 0;

constexpr GLenum kPrims[] = {0x0000 /* POINTS */,      0x0001 /* LINES */,
                             0x0002 /* LINE_LOOP */,   0x0003 /* LINE_STRIP */,
                             0x0004 /* TRIANGLES */,   0x0005 /* TRIANGLE_STRIP */,
                             0x0006 /* TRIANGLE_FAN */, 0x0007 /* QUADS */,
                             0x0008 /* QUAD_STRIP */,  0x0009 /* POLYGON */};

const char* PrimName(GLenum p) {
    switch (p) {
    case 0x0000: return "POINTS";
    case 0x0001: return "LINES";
    case 0x0002: return "LINE_LOOP";
    case 0x0003: return "LINE_STRIP";
    case 0x0004: return "TRIANGLES";
    case 0x0005: return "TRIANGLE_STRIP";
    case 0x0006: return "TRIANGLE_FAN";
    case 0x0007: return "QUADS";
    case 0x0008: return "QUAD_STRIP";
    case 0x0009: return "POLYGON";
    default: return "?";
    }
}

// Deterministic; the geometry only needs to be spread out.
class Rng {
public:
    float Next() {
        state_ = state_ * 1103515245u + 12345u;
        return static_cast<float>((state_ >> 16) & 0x7fff) / 32767.0f;
    }

private:
    unsigned state_ = 12345u;
};

class LongPrimitivesTest : public ContextTest {
protected:
    LongPrimitivesTest() : ContextTest(sfpew_test::Backend::GLES3, kWindow) {}

    void SetUp() override {
        ContextTest::SetUp();
        if (::testing::Test::IsSkipped()) return;

        vertex2f_ = Get<void (*)(GLfloat, GLfloat)>("glVertex2f");
        begin_ = Get<void (*)(GLenum)>("glBegin");
        end_ = Get<void (*)()>("glEnd");
        color3f_ = Get<void (*)(GLfloat, GLfloat, GLfloat)>("glColor3f");
        normal3f_ = Get<void (*)(GLfloat, GLfloat, GLfloat)>("glNormal3f");
        tex_coord2f_ = Get<void (*)(GLfloat, GLfloat)>("glTexCoord2f");
        clear_color_ = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glClearColor");
        clear_ = Get<void (*)(GLbitfield)>("glClear");
        ortho_ = Get<void (*)(double, double, double, double, double, double)>("glOrtho");
        matrix_mode_ = Get<void (*)(GLenum)>("glMatrixMode");
        load_identity_ = Get<void (*)()>("glLoadIdentity");
        finish_ = Get<void (*)()>("glFinish");
        get_error_ = Get<GLenum (*)()>("glGetError");
        auto read_pixels =
            Get<void (*)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*)>("glReadPixels");
        ASSERT_NE(read_pixels, nullptr);
        probe_.emplace(read_pixels);

        matrix_mode_(GL_PROJECTION_);
        load_identity_();
        ortho_(-1, 1, -1, 1, -1, 1);
        matrix_mode_(GL_MODELVIEW_);
        load_identity_();
        clear_color_(0, 0, 0, 0);
        color3f_(1, 1, 1);
        get_error_();
    }

    void (*vertex2f_)(GLfloat, GLfloat) = nullptr;
    void (*begin_)(GLenum) = nullptr;
    void (*end_)() = nullptr;
    void (*color3f_)(GLfloat, GLfloat, GLfloat) = nullptr;
    void (*normal3f_)(GLfloat, GLfloat, GLfloat) = nullptr;
    void (*tex_coord2f_)(GLfloat, GLfloat) = nullptr;
    void (*clear_color_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*clear_)(GLbitfield) = nullptr;
    void (*ortho_)(double, double, double, double, double, double) = nullptr;
    void (*matrix_mode_)(GLenum) = nullptr;
    void (*load_identity_)() = nullptr;
    void (*finish_)() = nullptr;
    GLenum (*get_error_)() = nullptr;
    std::optional<PixelProbe> probe_;
};

// (primitive mode, run length): every mode at 1k/20k/200k vertices, run as
// one enormous parameter space so a failing combination names itself in the
// test list instead of being one line in a loop's stdout.
struct Case {
    GLenum prim;
    int length;
};

void PrintTo(const Case& c, std::ostream* os) { *os << PrimName(c.prim) << "x" << c.length; }

std::vector<Case> AllCases() {
    std::vector<Case> cases;
    for (int length : {1000, 20000, 200000})
        for (GLenum prim : kPrims) cases.push_back({prim, length});
    return cases;
}

class LongPrimitivesCaseTest : public LongPrimitivesTest,
                               public ::testing::WithParamInterface<Case> {};

TEST_P(LongPrimitivesCaseTest, SurvivesAndDrawsSomething) {
    const Case& c = GetParam();
    clear_(GL_COLOR_BUFFER_BIT_);

    // A wide per-vertex layout (position + color + normal + texcoord) so the
    // collected stream is big enough to force the ring to grow at the
    // largest length.
    Rng rng;
    begin_(c.prim);
    for (int i = 0; i < c.length; ++i) {
        color3f_(1.0f, 1.0f, 1.0f);
        normal3f_(0.0f, 0.0f, 1.0f);
        tex_coord2f_(rng.Next(), rng.Next());
        vertex2f_(rng.Next() * 2.0f - 1.0f, rng.Next() * 2.0f - 1.0f);
    }
    end_();
    finish_();

    ASSERT_EQ(get_error_(), GL_NO_ERROR_);
    EXPECT_TRUE(probe_->AnyLit(0, 0, kWindow, kWindow)) << "drew nothing";
}

INSTANTIATE_TEST_SUITE_P(Runs, LongPrimitivesCaseTest, ::testing::ValuesIn(AllCases()),
                         [](const ::testing::TestParamInfo<Case>& info) {
                             return std::string(PrimName(info.param.prim)) + "x" +
                                   std::to_string(info.param.length);
                         });

} // namespace
