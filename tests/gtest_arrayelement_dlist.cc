// SimpleFPEWrapper - tests/gtest_arrayelement_dlist.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// glArrayElement inside glNewList has to end up in the list. It used to write
// the current vertex state through the internal templates, which carry no
// LIST_RECORD: under GL_COMPILE nothing was recorded AND nothing was drawn
// (glBegin had returned early, so the vertices had no primitive to join), and
// glCallList replayed an empty list with GL_NO_ERROR. Dispatching through the
// public per-attribute entry points makes the list carry the dereferenced
// values instead (plans/15 15.2).
//
// The equivalence is checked three ways - immediate, GL_COMPILE + glCallList,
// GL_COMPILE_AND_EXECUTE - against the same framebuffer, so a driver's own
// channel order cannot flatter any of them; the explicit color probes use
// colors that survive an R/B swap for the same reason.

#include "sfpew_gtest.h"

#include <cstring>
#include <vector>

namespace {

using sfpew_test::ContextTest;
using sfpew_test::GLbitfield;
using sfpew_test::GLenum;
using sfpew_test::GLfloat;
using sfpew_test::GLint;
using sfpew_test::GLsizei;
using sfpew_test::GLubyte;
using sfpew_test::GLuint;
using sfpew_test::PixelProbe;

constexpr int kWidth = 64, kHeight = 64;
constexpr GLenum GL_TRIANGLES_ = 0x0004;
constexpr GLbitfield GL_COLOR_BUFFER_BIT_ = 0x00004000;
constexpr GLenum GL_COMPILE_ = 0x1300;
constexpr GLenum GL_COMPILE_AND_EXECUTE_ = 0x1301;
constexpr GLenum GL_NO_ERROR_ = 0;
constexpr GLenum GL_FLOAT_ = 0x1406;
constexpr GLenum GL_UNSIGNED_BYTE_ = 0x1401;
constexpr GLenum GL_VERTEX_ARRAY_ = 0x8074;
constexpr GLenum GL_COLOR_ARRAY_ = 0x8076;

class ArrayElementListTest : public ContextTest {
protected:
    ArrayElementListTest() : ContextTest(sfpew_test::Backend::GLES3, kWidth) {}

    void SetUp() override {
        ContextTest::SetUp();
        if (::testing::Test::IsSkipped()) return;

        clear_color_ = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glClearColor");
        clear_ = Get<void (*)(GLbitfield)>("glClear");
        finish_ = Get<void (*)()>("glFinish");
        begin_ = Get<void (*)(GLenum)>("glBegin");
        end_ = Get<void (*)()>("glEnd");
        array_element_ = Get<void (*)(GLint)>("glArrayElement");
        vertex_pointer_ = Get<void (*)(GLint, GLenum, GLsizei, const void*)>("glVertexPointer");
        color_pointer_ = Get<void (*)(GLint, GLenum, GLsizei, const void*)>("glColorPointer");
        enable_client_state_ = Get<void (*)(GLenum)>("glEnableClientState");
        disable_client_state_ = Get<void (*)(GLenum)>("glDisableClientState");
        gen_lists_ = Get<GLuint (*)(GLsizei)>("glGenLists");
        new_list_ = Get<void (*)(GLuint, GLenum)>("glNewList");
        end_list_ = Get<void (*)()>("glEndList");
        call_list_ = Get<void (*)(GLuint)>("glCallList");
        get_error_ = Get<GLenum (*)()>("glGetError");
        read_pixels_ =
            Get<void (*)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*)>("glReadPixels");
        ASSERT_NE(read_pixels_, nullptr);
        ASSERT_NE(array_element_, nullptr);

        vertex_pointer_(3, GL_FLOAT_, 0, positions_);
        color_pointer_(4, GL_UNSIGNED_BYTE_, 0, colors_);
        enable_client_state_(GL_VERTEX_ARRAY_);
        enable_client_state_(GL_COLOR_ARRAY_);
    }

    // Two triangles whose only per-vertex difference is the color array, so a
    // dropped or mis-indexed color shows up as the wrong half being wrong.
    void DrawShape() {
        begin_(GL_TRIANGLES_);
        for (GLint i = 0; i < 6; ++i) array_element_(i);
        end_();
    }

    void ClearFramebuffer() {
        clear_color_(0.0f, 0.0f, 0.0f, 1.0f);
        clear_(GL_COLOR_BUFFER_BIT_);
        finish_();
    }

    void Capture(std::vector<GLubyte>* out) {
        out->resize(static_cast<size_t>(kWidth) * kHeight * 4);
        finish_();
        read_pixels_(0, 0, kWidth, kHeight, 0x1908 /* GL_RGBA */, GL_UNSIGNED_BYTE_, out->data());
    }

    static int LitCount(const std::vector<GLubyte>& p) {
        int n = 0;
        for (size_t i = 0; i < p.size() / 4; ++i)
            if (p[i * 4] > 20 || p[i * 4 + 1] > 20 || p[i * 4 + 2] > 20) ++n;
        return n;
    }

    // Magenta and green: both are their own mirror image under an R/B swap,
    // so llvmpipe's BGRA readback quirk cannot turn one into the other.
    static constexpr int kLeftProbeX = 16, kLeftProbeY = 20;
    static constexpr int kRightProbeX = 47, kRightProbeY = 20;

    GLfloat positions_[18] = {
        -0.9f, -0.9f, 0.0f, -0.1f, -0.9f, 0.0f, -0.5f, 0.9f, 0.0f,
        0.1f,  -0.9f, 0.0f, 0.9f,  -0.9f, 0.0f, 0.5f,  0.9f, 0.0f,
    };
    GLubyte colors_[24] = {
        255, 0, 255, 255, 255, 0, 255, 255, 255, 0, 255, 255,
        0,   255, 0,   255, 0,   255, 0,   255, 0,   255, 0,   255,
    };

    void (*clear_color_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*clear_)(GLbitfield) = nullptr;
    void (*finish_)() = nullptr;
    void (*begin_)(GLenum) = nullptr;
    void (*end_)() = nullptr;
    void (*array_element_)(GLint) = nullptr;
    void (*vertex_pointer_)(GLint, GLenum, GLsizei, const void*) = nullptr;
    void (*color_pointer_)(GLint, GLenum, GLsizei, const void*) = nullptr;
    void (*enable_client_state_)(GLenum) = nullptr;
    void (*disable_client_state_)(GLenum) = nullptr;
    GLuint (*gen_lists_)(GLsizei) = nullptr;
    void (*new_list_)(GLuint, GLenum) = nullptr;
    void (*end_list_)() = nullptr;
    void (*call_list_)(GLuint) = nullptr;
    GLenum (*get_error_)() = nullptr;
    void (*read_pixels_)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*) = nullptr;
};

TEST_F(ArrayElementListTest, CompiledArrayElementsReplayIdenticallyToImmediateMode) {
    PixelProbe probe(read_pixels_);

    std::vector<GLubyte> immediate;
    ClearFramebuffer();
    DrawShape();
    Capture(&immediate);
    const int immediate_lit = LitCount(immediate);
    ASSERT_GT(immediate_lit, 0) << "immediate glArrayElement produced nothing; cannot compare";

    // Positions and colors both reached the immediate draw.
    const auto left = probe.At(kLeftProbeX, kLeftProbeY);
    const auto right = probe.At(kRightProbeX, kRightProbeY);
    EXPECT_GT(left.r, 150) << "left triangle is not magenta; the color array was not read";
    EXPECT_GT(left.b, 150) << "left triangle is not magenta; the color array was not read";
    EXPECT_LT(left.g, 100) << "left triangle is not magenta; the color array was not read";
    EXPECT_GT(right.g, 150) << "right triangle is not green; the color array was not read";
    EXPECT_LT(right.r, 100) << "right triangle is not green; the color array was not read";
    EXPECT_LT(right.b, 100) << "right triangle is not green; the color array was not read";

    // GL_COMPILE must record without drawing.
    std::vector<GLubyte> replay;
    const GLuint list = gen_lists_(1);
    ClearFramebuffer();
    new_list_(list, GL_COMPILE_);
    DrawShape();
    end_list_();
    Capture(&replay);
    EXPECT_EQ(LitCount(replay), 0)
        << "GL_COMPILE drew while recording (" << LitCount(replay) << " lit pixels)";

    // ... and replaying it must match the immediate reference byte for byte.
    ClearFramebuffer();
    call_list_(list);
    Capture(&replay);
    EXPECT_EQ(LitCount(replay), immediate_lit)
        << "glCallList lit a different pixel count; the recorded elements were dropped";
    EXPECT_TRUE(std::memcmp(replay.data(), immediate.data(), immediate.size()) == 0)
        << "replayed glArrayElement pixels differ from the immediate reference";

    // A second call cannot consume the recorded values.
    ClearFramebuffer();
    call_list_(list);
    call_list_(list);
    Capture(&replay);
    EXPECT_TRUE(std::memcmp(replay.data(), immediate.data(), immediate.size()) == 0)
        << "replaying twice did not match a single draw";

    // GL_COMPILE_AND_EXECUTE must draw AND leave a replayable list.
    const GLuint both = gen_lists_(1);
    ClearFramebuffer();
    new_list_(both, GL_COMPILE_AND_EXECUTE_);
    DrawShape();
    end_list_();
    Capture(&replay);
    EXPECT_TRUE(std::memcmp(replay.data(), immediate.data(), immediate.size()) == 0)
        << "GL_COMPILE_AND_EXECUTE did not draw as it recorded";
    ClearFramebuffer();
    call_list_(both);
    Capture(&replay);
    EXPECT_TRUE(std::memcmp(replay.data(), immediate.data(), immediate.size()) == 0)
        << "the GL_COMPILE_AND_EXECUTE list does not replay correctly";

    EXPECT_EQ(get_error_(), GL_NO_ERROR_) << "GL error raised around glArrayElement recording";
}

// GL 2.1 section 5.4: the array data a compiled glArrayElement refers to is
// dereferenced when the list is compiled, not when it is executed. Retaining
// the caller's pointer instead would replay whatever the application's buffer
// holds later - the same class of bug the captured-draw path exists to avoid.
TEST_F(ArrayElementListTest, RecordedElementsAreDereferencedAtCompileTime) {
    ClearFramebuffer();
    const GLuint list = gen_lists_(1);
    new_list_(list, GL_COMPILE_);
    DrawShape();
    end_list_();

    std::vector<GLubyte> compiled;
    ClearFramebuffer();
    call_list_(list);
    Capture(&compiled);
    ASSERT_GT(LitCount(compiled), 0) << "the compiled list drew nothing";

    // Swap the two triangles' colors and collapse the geometry to a degenerate
    // strip. Neither may reach the already-compiled list.
    for (int i = 0; i < 12; ++i) std::swap(colors_[i], colors_[i + 12]);
    for (float& position : positions_) position = 0.0f;
    disable_client_state_(GL_COLOR_ARRAY_);

    std::vector<GLubyte> after;
    ClearFramebuffer();
    call_list_(list);
    Capture(&after);
    EXPECT_TRUE(std::memcmp(after.data(), compiled.data(), compiled.size()) == 0)
        << "the list re-read the client arrays at glCallList time";

    EXPECT_EQ(get_error_(), GL_NO_ERROR_) << "GL error raised during compile-time dereference test";
}

} // namespace
