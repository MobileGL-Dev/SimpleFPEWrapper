// SimpleFPEWrapper - tests/gtest_texgen_toggle.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// Toggling GL_TEXTURE_GEN_S/T selects a different generated program, and the
// program-key cache's fast path has to notice the toggle in BOTH directions.
// It did not: its enable comparison was field-by-field and texture_gen_enable
// was not among the fields, so switching texgen OFF between two otherwise
// identical draws HIT the cache and kept drawing with the texgen program.
// Minecraft's enchantment glint is sphere-map texgen that comes and goes with
// the camera, which turned the miss into "lighting is broken from some view
// angles" under shader packs.
//
// The texture has a red left half and a blue right half. The vertex texcoords
// address the red half; the object-linear texgen planes address the blue
// half. Which color appears IS which program ran, so the off -> on -> off
// sequence checks the cache notices both edges.

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
using sfpew_test::GLuint;
using sfpew_test::PixelProbe;

constexpr GLbitfield GL_COLOR_BUFFER_BIT_ = 0x00004000;
constexpr GLenum GL_TRIANGLES_ = 0x0004;
constexpr GLenum GL_FLOAT_ = 0x1406;
constexpr GLenum GL_RGBA_ = 0x1908;
constexpr GLenum GL_UNSIGNED_BYTE_ = 0x1401;
constexpr GLenum GL_TEXTURE_2D_ = 0x0DE1;
constexpr GLenum GL_TEXTURE_MIN_FILTER_ = 0x2801;
constexpr GLenum GL_TEXTURE_MAG_FILTER_ = 0x2800;
constexpr GLenum GL_NEAREST_ = 0x2600;
constexpr GLenum GL_VERTEX_ARRAY_ = 0x8074;
constexpr GLenum GL_TEXTURE_COORD_ARRAY_ = 0x8078;
constexpr GLenum GL_TEXTURE_GEN_S_ = 0x0C60;
constexpr GLenum GL_TEXTURE_GEN_T_ = 0x0C61;
constexpr GLenum GL_TEXTURE_GEN_MODE_ = 0x2500;
constexpr GLenum GL_OBJECT_PLANE_ = 0x2501;
constexpr GLenum GL_OBJECT_LINEAR_ = 0x2401;
constexpr GLenum GL_S_ = 0x2000;
constexpr GLenum GL_T_ = 0x2001;
constexpr GLenum GL_NO_ERROR_ = 0;

using TexGenToggleTest = ContextTest;

TEST_F(TexGenToggleTest, ProgramCacheFollowsTexgenThroughBothEdges) {
    auto clear_color = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glClearColor");
    auto clear = Get<void (*)(GLbitfield)>("glClear");
    auto draw_arrays = Get<void (*)(GLenum, GLint, GLsizei)>("glDrawArrays");
    auto get_error = Get<GLenum (*)()>("glGetError");
    auto finish = Get<void (*)()>("glFinish");
    auto enable = Get<void (*)(GLenum)>("glEnable");
    auto disable = Get<void (*)(GLenum)>("glDisable");
    auto enable_client_state = Get<void (*)(GLenum)>("glEnableClientState");
    auto disable_client_state = Get<void (*)(GLenum)>("glDisableClientState");
    auto vertex_pointer = Get<void (*)(GLint, GLenum, GLsizei, const void*)>("glVertexPointer");
    auto tex_coord_pointer =
        Get<void (*)(GLint, GLenum, GLsizei, const void*)>("glTexCoordPointer");
    auto gen_textures = Get<void (*)(GLsizei, GLuint*)>("glGenTextures");
    auto bind_texture = Get<void (*)(GLenum, GLuint)>("glBindTexture");
    auto tex_image2d =
        Get<void (*)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*)>(
            "glTexImage2D");
    auto tex_parameteri = Get<void (*)(GLenum, GLenum, GLint)>("glTexParameteri");
    auto tex_geni = Get<void (*)(GLenum, GLenum, GLint)>("glTexGeni");
    auto tex_genfv = Get<void (*)(GLenum, GLenum, const GLfloat*)>("glTexGenfv");
    auto color4f = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glColor4f");
    auto read_pixels =
        Get<void (*)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*)>("glReadPixels");
    ASSERT_NE(read_pixels, nullptr);
    PixelProbe probe(read_pixels);

    // 2x1: texel 0 red, texel 1 blue.
    static const GLubyte texels[] = {255, 0, 0, 255, 0, 0, 255, 255};
    static const GLfloat pos[] = {-1, -1, 1, -1, 1, 1, -1, -1, 1, 1, -1, 1};
    // Vertex texcoords address the RED texel (s in [0, 0.5)).
    static const GLfloat uv[] = {0.25f, 0.5f, 0.25f, 0.5f, 0.25f, 0.5f,
                                 0.25f, 0.5f, 0.25f, 0.5f, 0.25f, 0.5f};
    // Object-linear planes address the BLUE texel: s = 0*x+0*y+0*z+0.75.
    static const GLfloat plane_s[4] = {0.0f, 0.0f, 0.0f, 0.75f};
    static const GLfloat plane_t[4] = {0.0f, 0.0f, 0.0f, 0.5f};

    GLuint tex = 0;
    gen_textures(1, &tex);
    bind_texture(GL_TEXTURE_2D_, tex);
    tex_image2d(GL_TEXTURE_2D_, 0, GL_RGBA_, 2, 1, 0, GL_RGBA_, GL_UNSIGNED_BYTE_, texels);
    tex_parameteri(GL_TEXTURE_2D_, GL_TEXTURE_MIN_FILTER_, GL_NEAREST_);
    tex_parameteri(GL_TEXTURE_2D_, GL_TEXTURE_MAG_FILTER_, GL_NEAREST_);
    enable(GL_TEXTURE_2D_);

    tex_geni(GL_S_, GL_TEXTURE_GEN_MODE_, GL_OBJECT_LINEAR_);
    tex_geni(GL_T_, GL_TEXTURE_GEN_MODE_, GL_OBJECT_LINEAR_);
    tex_genfv(GL_S_, GL_OBJECT_PLANE_, plane_s);
    tex_genfv(GL_T_, GL_OBJECT_PLANE_, plane_t);

    vertex_pointer(2, GL_FLOAT_, 0, pos);
    tex_coord_pointer(2, GL_FLOAT_, 0, uv);
    enable_client_state(GL_VERTEX_ARRAY_);
    enable_client_state(GL_TEXTURE_COORD_ARRAY_);
    color4f(1.0f, 1.0f, 1.0f, 1.0f);
    clear_color(0.0f, 0.0f, 0.0f, 1.0f);

    // The toggle sequence. Every draw is identical except for the texgen
    // enable, so a cache that overlooks that enable serves the wrong program
    // on one of the edges.
    const auto draw_and_expect = [&](int r, int g, int b, const char* what) {
        clear(GL_COLOR_BUFFER_BIT_);
        draw_arrays(GL_TRIANGLES_, 0, 6);
        finish();
        const PixelProbe::Rgba p = probe.At(32, 32);
        EXPECT_TRUE((p.r > 200) == (r > 0) && (p.g > 200) == (g > 0) && (p.b > 200) == (b > 0))
            << what << ": pixel = (" << (int)p.r << ',' << (int)p.g << ',' << (int)p.b
            << "), expected (" << r << ',' << g << ',' << b << ')';
    };

    draw_and_expect(1, 0, 0, "texgen off: vertex texcoords sample red");

    enable(GL_TEXTURE_GEN_S_);
    enable(GL_TEXTURE_GEN_T_);
    draw_and_expect(0, 0, 1, "texgen on: generated coordinates sample blue");

    disable(GL_TEXTURE_GEN_S_);
    disable(GL_TEXTURE_GEN_T_);
    draw_and_expect(1, 0, 0, "texgen off again: back to vertex texcoords");

    enable(GL_TEXTURE_GEN_S_);
    enable(GL_TEXTURE_GEN_T_);
    draw_and_expect(0, 0, 1, "texgen on again: the on-program is not stale either");

    disable(GL_TEXTURE_GEN_S_);
    disable(GL_TEXTURE_GEN_T_);
    disable_client_state(GL_TEXTURE_COORD_ARRAY_);
    disable_client_state(GL_VERTEX_ARRAY_);
    EXPECT_EQ(get_error(), GL_NO_ERROR_);
}

} // namespace
