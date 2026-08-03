// SimpleFPEWrapper - tests/gtest_empty_beginend.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// Ported from piglit's tests/spec/gl-1.0/empty-begin-end-clause.c (MIT-style
// license; see piglit's COPYING; fdo bug #23489): a Begin/End pair with zero
// vertices submitted must be a legal no-op, over every primitive mode, not
// just GL_LINES. Also covers what the beginend-coverage port's GL_NONE ==
// GL_POINTS sentinel fix was really guarding: GL_POINTS is mode 0, the same
// value the wrapper used to mean "no Begin/End open", so an empty
// glBegin(GL_POINTS)/glEnd() used to raise a spurious GL_INVALID_OPERATION
// and every vertex submitted inside one was silently dropped.

#include "sfpew_gtest.h"

namespace {

using sfpew_test::ContextTest;
using sfpew_test::GLbitfield;
using sfpew_test::GLenum;

constexpr GLenum GL_POINTS_ = 0x0000;
constexpr GLenum GL_LINES_ = 0x0001;
constexpr GLenum GL_LINE_LOOP_ = 0x0002;
constexpr GLenum GL_LINE_STRIP_ = 0x0003;
constexpr GLenum GL_TRIANGLES_ = 0x0004;
constexpr GLenum GL_TRIANGLE_STRIP_ = 0x0005;
constexpr GLenum GL_TRIANGLE_FAN_ = 0x0006;
constexpr GLenum GL_QUADS_ = 0x0007;
constexpr GLenum GL_QUAD_STRIP_ = 0x0008;
constexpr GLenum GL_POLYGON_ = 0x0009;
constexpr GLenum GL_NO_ERROR_ = 0;
constexpr GLbitfield GL_COLOR_BUFFER_BIT_ = 0x00004000;

using EmptyBeginEndTest = ContextTest;

TEST_F(EmptyBeginEndTest, IsANoOpAcrossEveryPrimitiveMode) {
    auto begin = Get<void (*)(GLenum)>("glBegin");
    auto end = Get<void (*)()>("glEnd");
    auto flush = Get<void (*)()>("glFlush");
    auto clear_color = Get<void (*)(float, float, float, float)>("glClearColor");
    auto clear = Get<void (*)(GLbitfield)>("glClear");
    auto get_error = Get<GLenum (*)()>("glGetError");
    ASSERT_NE(begin, nullptr);
    ASSERT_NE(end, nullptr);
    ASSERT_NE(flush, nullptr);
    ASSERT_NE(clear_color, nullptr);
    ASSERT_NE(clear, nullptr);
    ASSERT_NE(get_error, nullptr);

    const GLenum modes[] = {GL_POINTS_,    GL_LINES_,          GL_LINE_LOOP_,   GL_LINE_STRIP_,
                            GL_TRIANGLES_, GL_TRIANGLE_STRIP_, GL_TRIANGLE_FAN_, GL_QUADS_,
                            GL_QUAD_STRIP_, GL_POLYGON_};
    clear_color(0.0f, 0.0f, 0.0f, 0.0f);
    clear(GL_COLOR_BUFFER_BIT_);
    get_error();

    for (GLenum mode : modes) {
        for (int i = 0; i < 100; ++i) {
            begin(mode);
            end();
        }
        EXPECT_EQ(get_error(), GL_NO_ERROR_)
            << "empty Begin(0x" << std::hex << mode << ")/End() x100 latched an error";
    }
    flush();
    EXPECT_EQ(get_error(), GL_NO_ERROR_) << "glFlush() after empty Begin/End runs latched an error";
}

} // namespace
