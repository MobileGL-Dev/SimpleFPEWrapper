// SimpleFPEWrapper - tests/gtest_list_immediate.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// A glBegin/glEnd block inside a display list is compiled into a single
// command carrying the accumulated vertex block (plans/12-fpe-draw-cost.md)
// instead of one command per glVertex/glColor/glTexCoord. That rewrite must
// be invisible: replaying the list has to put the same pixels on screen as
// running the same calls immediately, GL_COMPILE must still not draw while
// recording, and GL_COMPILE_AND_EXECUTE must both draw and record.

#include "sfpew_gtest.h"

#include <cstring>
#include <optional>

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
constexpr GLenum GL_QUADS_ = 0x0007;
constexpr GLbitfield GL_COLOR_BUFFER_BIT_ = 0x00004000;
constexpr GLenum GL_COMPILE_ = 0x1300;
constexpr GLenum GL_COMPILE_AND_EXECUTE_ = 0x1301;
constexpr GLenum GL_NO_ERROR_ = 0;

class ListImmediateTest : public ContextTest {
protected:
    ListImmediateTest() : ContextTest(sfpew_test::Backend::GLES3, kWidth) {}

    void SetUp() override {
        ContextTest::SetUp();
        if (::testing::Test::IsSkipped()) return;

        clear_color_ = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glClearColor");
        clear_ = Get<void (*)(GLbitfield)>("glClear");
        finish_ = Get<void (*)()>("glFinish");
        viewport_ = Get<void (*)(GLint, GLint, GLsizei, GLsizei)>("glViewport");
        begin_ = Get<void (*)(GLenum)>("glBegin");
        end_ = Get<void (*)()>("glEnd");
        vertex3f_ = Get<void (*)(GLfloat, GLfloat, GLfloat)>("glVertex3f");
        color4f_ = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glColor4f");
        gen_lists_ = Get<GLuint (*)(GLsizei)>("glGenLists");
        new_list_ = Get<void (*)(GLuint, GLenum)>("glNewList");
        end_list_ = Get<void (*)()>("glEndList");
        call_list_ = Get<void (*)(GLuint)>("glCallList");
        get_error_ = Get<GLenum (*)()>("glGetError");
        read_pixels_ =
            Get<void (*)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*)>("glReadPixels");
        ASSERT_NE(read_pixels_, nullptr);
    }

    // A two-color shape: enough vertices and attributes that a mistake in
    // the interleaving or the vertex count shows up as wrong pixels.
    void DrawShape() {
        begin_(GL_QUADS_);
        color4f_(1.0f, 0.0f, 0.0f, 1.0f);
        vertex3f_(-0.9f, -0.9f, 0.0f);
        vertex3f_(-0.1f, -0.9f, 0.0f);
        vertex3f_(-0.1f, 0.9f, 0.0f);
        vertex3f_(-0.9f, 0.9f, 0.0f);
        color4f_(0.0f, 0.0f, 1.0f, 1.0f);
        vertex3f_(0.1f, -0.9f, 0.0f);
        vertex3f_(0.9f, -0.9f, 0.0f);
        vertex3f_(0.9f, 0.9f, 0.0f);
        vertex3f_(0.1f, 0.9f, 0.0f);
        end_();
    }

    void Capture(std::vector<GLubyte>* out) {
        out->resize(static_cast<size_t>(kWidth) * kHeight * 4);
        clear_color_(0.0f, 0.0f, 0.0f, 1.0f);
        clear_(GL_COLOR_BUFFER_BIT_);
        finish_();
        read_pixels_(0, 0, kWidth, kHeight, 0x1908 /* GL_RGBA */, 0x1401 /* GL_UNSIGNED_BYTE */,
                     out->data());
    }

    // A pixel is "lit" if it is not the clear color.
    static int LitCount(const std::vector<GLubyte>& p) {
        int n = 0;
        for (size_t i = 0; i < p.size() / 4; ++i)
            if (p[i * 4] != 0 || p[i * 4 + 1] != 0 || p[i * 4 + 2] != 0) ++n;
        return n;
    }

    void (*clear_color_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*clear_)(GLbitfield) = nullptr;
    void (*finish_)() = nullptr;
    void (*viewport_)(GLint, GLint, GLsizei, GLsizei) = nullptr;
    void (*begin_)(GLenum) = nullptr;
    void (*end_)() = nullptr;
    void (*vertex3f_)(GLfloat, GLfloat, GLfloat) = nullptr;
    void (*color4f_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    GLuint (*gen_lists_)(GLsizei) = nullptr;
    void (*new_list_)(GLuint, GLenum) = nullptr;
    void (*end_list_)() = nullptr;
    void (*call_list_)(GLuint) = nullptr;
    GLenum (*get_error_)() = nullptr;
    void (*read_pixels_)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*) = nullptr;
};

TEST_F(ListImmediateTest, CompiledBlocksReplayIdenticallyToImmediateMode) {
    // The immediate reference.
    std::vector<GLubyte> immediate;
    Capture(&immediate);
    DrawShape();
    finish_();
    read_pixels_(0, 0, kWidth, kHeight, 0x1908, 0x1401, immediate.data());
    const int immediate_lit = LitCount(immediate);
    ASSERT_GT(immediate_lit, 0) << "immediate draw produced nothing; cannot compare";

    // GL_COMPILE must record without drawing.
    std::vector<GLubyte> replay;
    const GLuint list = gen_lists_(1);
    Capture(&replay);
    new_list_(list, GL_COMPILE_);
    DrawShape();
    end_list_();
    finish_();
    read_pixels_(0, 0, kWidth, kHeight, 0x1908, 0x1401, replay.data());
    EXPECT_EQ(LitCount(replay), 0) << "GL_COMPILE drew while recording ("
                                   << LitCount(replay) << " lit pixels)";

    // Replaying must match the immediate reference byte for byte.
    Capture(&replay);
    call_list_(list);
    finish_();
    read_pixels_(0, 0, kWidth, kHeight, 0x1908, 0x1401, replay.data());
    EXPECT_EQ(LitCount(replay), immediate_lit) << "replay lit a different pixel count";
    EXPECT_TRUE(std::memcmp(replay.data(), immediate.data(), immediate.size()) == 0)
        << "replayed pixels differ from the immediate reference";

    // Replaying twice must be idempotent: the compiled block keeps its own
    // copy of the vertices, so a second call cannot consume or move them.
    Capture(&replay);
    call_list_(list);
    call_list_(list);
    finish_();
    read_pixels_(0, 0, kWidth, kHeight, 0x1908, 0x1401, replay.data());
    EXPECT_TRUE(std::memcmp(replay.data(), immediate.data(), immediate.size()) == 0)
        << "replaying twice did not match a single draw";

    // GL_COMPILE_AND_EXECUTE must draw AND leave a replayable list.
    const GLuint both = gen_lists_(1);
    Capture(&replay);
    new_list_(both, GL_COMPILE_AND_EXECUTE_);
    DrawShape();
    end_list_();
    finish_();
    read_pixels_(0, 0, kWidth, kHeight, 0x1908, 0x1401, replay.data());
    EXPECT_TRUE(std::memcmp(replay.data(), immediate.data(), immediate.size()) == 0)
        << "GL_COMPILE_AND_EXECUTE did not draw as it recorded";
    Capture(&replay);
    call_list_(both);
    finish_();
    read_pixels_(0, 0, kWidth, kHeight, 0x1908, 0x1401, replay.data());
    EXPECT_TRUE(std::memcmp(replay.data(), immediate.data(), immediate.size()) == 0)
        << "GL_COMPILE_AND_EXECUTE list does not replay correctly";

    EXPECT_EQ(get_error_(), GL_NO_ERROR_) << "GL error raised during compiled-list rendering";
}

} // namespace
