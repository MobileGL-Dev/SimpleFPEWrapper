// SimpleFPEWrapper - tests/gtest_attrib_retire.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// A draw whose enabled client-array set is SMALLER than the previous draw's
// must not leave the retired attribute enabled. cidx() only assigns a physical
// attribute index to a slot that is enabled or carries a constant value, so
// when the set shrinks the indices above the new high water mark belong to no
// slot and used to keep the previous layout's format. That stale index then
// reads at its old relative offset against the new, smaller stride - past the
// end of every vertex.
//
// Found in a RenderDoc capture of Minecraft 1.16 fabulous: attr 2 stayed
// enabled at relativeoffset 12 against a 12-byte position-only stride, so every
// texcoord fetched the following vertex's position.

#include "sfpew_gtest.h"

#include <optional>

namespace {

using sfpew_test::ContextTest;
using sfpew_test::GLbitfield;
using sfpew_test::GLenum;
using sfpew_test::GLfloat;
using sfpew_test::GLint;
using sfpew_test::GLsizei;
using sfpew_test::GLubyte;
using sfpew_test::PixelProbe;

constexpr GLbitfield GL_COLOR_BUFFER_BIT_ = 0x00004000;
constexpr GLenum GL_TRIANGLES_ = 0x0004;
constexpr GLenum GL_QUADS_ = 0x0007;
constexpr GLenum GL_FLOAT_ = 0x1406;
constexpr GLenum GL_VERTEX_ARRAY_ = 0x8074;
constexpr GLenum GL_COLOR_ARRAY_ = 0x8076;
constexpr GLenum GL_TEXTURE_COORD_ARRAY_ = 0x8078;
constexpr GLenum GL_NO_ERROR_ = 0;

using AttribRetireTest = ContextTest;

TEST_F(AttribRetireTest, ShrinkingTheEnabledSetRetiresStaleAttributeIndices) {
    auto clear_color = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glClearColor");
    auto clear = Get<void (*)(GLbitfield)>("glClear");
    auto enable_client_state = Get<void (*)(GLenum)>("glEnableClientState");
    auto disable_client_state = Get<void (*)(GLenum)>("glDisableClientState");
    auto vertex_pointer = Get<void (*)(GLint, GLenum, GLsizei, const void*)>("glVertexPointer");
    auto color_pointer = Get<void (*)(GLint, GLenum, GLsizei, const void*)>("glColorPointer");
    auto tex_coord_pointer =
        Get<void (*)(GLint, GLenum, GLsizei, const void*)>("glTexCoordPointer");
    auto draw_arrays = Get<void (*)(GLenum, GLint, GLsizei)>("glDrawArrays");
    auto get_error = Get<GLenum (*)()>("glGetError");
    auto color4f = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glColor4f");
    auto finish = Get<void (*)()>("glFinish");
    auto read_pixels =
        Get<void (*)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*)>("glReadPixels");
    ASSERT_NE(read_pixels, nullptr);
    PixelProbe probe(read_pixels);

    clear_color(0.0f, 0.0f, 1.0f, 1.0f);

    // Draw A: position + color + texcoord, interleaved, stride 36 bytes.
    // Claims physical attribute indices 0, 1 and 2.
    static const GLfloat wide[] = {
        -1, -1, 0, 1, 0, 0, 1, 0, 0,
        1,  -1, 0, 1, 0, 0, 1, 1, 0,
        1,  1,  0, 1, 0, 0, 1, 1, 1,
        -1, 1,  0, 1, 0, 0, 1, 0, 1,
    };
    clear(GL_COLOR_BUFFER_BIT_);
    enable_client_state(GL_VERTEX_ARRAY_);
    enable_client_state(GL_COLOR_ARRAY_);
    enable_client_state(GL_TEXTURE_COORD_ARRAY_);
    vertex_pointer(3, GL_FLOAT_, 36, wide);
    color_pointer(4, GL_FLOAT_, 36, wide + 3);
    tex_coord_pointer(2, GL_FLOAT_, 36, wide + 7);
    draw_arrays(GL_QUADS_, 0, 4);
    finish();
    {
        const PixelProbe::Rgba p = probe.At(32, 32);
        EXPECT_TRUE(p.r > 200 && p.g <= 50 && p.b <= 50)
            << "A: position+color+texcoord draw must render red: (" << (int)p.r << ',' << (int)p.g
            << ',' << (int)p.b << ')';
    }

    // Draw B: the set SHRINKS to position only, tightly packed (stride 12).
    // Physical index 2 is now claimed by no slot. Left enabled it keeps
    // relativeoffset 12 from draw A and reads past every 12-byte vertex, which
    // corrupts the shader's texcoord input; the wrapper must retire it.
    // The color comes from the sticky current value so the pixel test is
    // independent of the color array.
    static const GLfloat narrow[] = {
        -1, -1, 0,
        1,  -1, 0,
        1,  1,  0,
        -1, 1,  0,
    };
    clear(GL_COLOR_BUFFER_BIT_);
    disable_client_state(GL_COLOR_ARRAY_);
    disable_client_state(GL_TEXTURE_COORD_ARRAY_);
    color4f(0.0f, 1.0f, 0.0f, 1.0f);
    vertex_pointer(3, GL_FLOAT_, 0, narrow);
    draw_arrays(GL_QUADS_, 0, 4);
    finish();
    {
        const PixelProbe::Rgba p = probe.At(32, 32);
        EXPECT_TRUE(p.r <= 50 && p.g > 200 && p.b <= 50)
            << "B: shrunk-set draw must render green (retired attr not read): (" << (int)p.r << ','
            << (int)p.g << ',' << (int)p.b << ')';
    }

    disable_client_state(GL_VERTEX_ARRAY_);
    EXPECT_EQ(get_error(), GL_NO_ERROR_);
}

} // namespace
