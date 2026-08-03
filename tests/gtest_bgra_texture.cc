// SimpleFPEWrapper - tests/gtest_bgra_texture.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// GL_BGRA texture data on a GLES backend, through every source the wrapper
// has to take it from: client memory, a pixel unpack buffer (mapped for
// reading - the bytes never pass through the wrapper otherwise), and a
// sub-rectangle of a wider image described with GL_UNPACK_ROW_LENGTH and the
// skip offsets, which is how Minecraft stitches its atlases. Every route
// reorders into tight RGBA before the backend sees it, so a texture can mix
// BGRA and plain uploads freely.
//
// Probe colours keep R != B - a channel swap has to be visible - and the
// quad is drawn with the texture modulating a white vertex colour.

#include "sfpew_gtest.h"

#include <optional>

namespace {

using sfpew_test::ContextTest;
using sfpew_test::GLbitfield;
using sfpew_test::GLenum;
using sfpew_test::GLfloat;
using sfpew_test::GLint;
using sfpew_test::GLsizei;
using sfpew_test::GLsizeiptr;
using sfpew_test::GLubyte;
using sfpew_test::GLuint;
using sfpew_test::PixelProbe;

constexpr GLbitfield GL_COLOR_BUFFER_BIT_ = 0x00004000;
constexpr GLenum GL_TRIANGLES_ = 0x0004;
constexpr GLenum GL_FLOAT_ = 0x1406;
constexpr GLenum GL_RGBA_ = 0x1908;
constexpr GLenum GL_BGRA_ = 0x80E1;
constexpr GLenum GL_UNSIGNED_BYTE_ = 0x1401;
constexpr GLenum GL_TEXTURE_2D_ = 0x0DE1;
constexpr GLenum GL_TEXTURE_MIN_FILTER_ = 0x2801;
constexpr GLenum GL_TEXTURE_MAG_FILTER_ = 0x2800;
constexpr GLenum GL_NEAREST_ = 0x2600;
constexpr GLenum GL_VERTEX_ARRAY_ = 0x8074;
constexpr GLenum GL_TEXTURE_COORD_ARRAY_ = 0x8078;
constexpr GLenum GL_PIXEL_UNPACK_BUFFER_ = 0x88EC;
constexpr GLenum GL_UNPACK_ROW_LENGTH_ = 0x0CF2;
constexpr GLenum GL_UNPACK_SKIP_PIXELS_ = 0x0CF4;
constexpr GLenum GL_STATIC_DRAW_ = 0x88E4;
constexpr GLenum GL_NO_ERROR_ = 0;

using BgraTextureTest = ContextTest;

TEST_F(BgraTextureTest, EveryUploadRouteKeepsTheComponentOrder) {
    auto clear_color = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glClearColor");
    auto clear = Get<void (*)(GLbitfield)>("glClear");
    auto draw_arrays = Get<void (*)(GLenum, GLint, GLsizei)>("glDrawArrays");
    auto get_error = Get<GLenum (*)()>("glGetError");
    auto finish = Get<void (*)()>("glFinish");
    auto enable = Get<void (*)(GLenum)>("glEnable");
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
    auto gen_buffers = Get<void (*)(GLsizei, GLuint*)>("glGenBuffers");
    auto bind_buffer = Get<void (*)(GLenum, GLuint)>("glBindBuffer");
    auto buffer_data = Get<void (*)(GLenum, GLsizeiptr, const void*, GLenum)>("glBufferData");
    auto color4f = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glColor4f");
    auto tex_sub_image2d = Get<void (*)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum,
                                        GLenum, const void*)>("glTexSubImage2D");
    auto pixel_storei = Get<void (*)(GLenum, GLint)>("glPixelStorei");
    auto read_pixels =
        Get<void (*)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*)>("glReadPixels");
    ASSERT_NE(read_pixels, nullptr);
    PixelProbe probe(read_pixels);

    // 2x2 texels. Read as B,G,R,A this is red; read as R,G,B,A it is blue.
    static const GLubyte texels[] = {
        0, 0, 255, 255, 0, 0, 255, 255,
        0, 0, 255, 255, 0, 0, 255, 255,
    };
    static const GLfloat pos[] = {-1, -1, 1, -1, 1, 1, -1, -1, 1, 1, -1, 1};
    static const GLfloat uv[] = {0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1};
    GLuint tex = 0;
    gen_textures(1, &tex);
    bind_texture(GL_TEXTURE_2D_, tex);
    tex_parameteri(GL_TEXTURE_2D_, GL_TEXTURE_MIN_FILTER_, GL_NEAREST_);
    tex_parameteri(GL_TEXTURE_2D_, GL_TEXTURE_MAG_FILTER_, GL_NEAREST_);
    enable(GL_TEXTURE_2D_);
    vertex_pointer(2, GL_FLOAT_, 0, pos);
    tex_coord_pointer(2, GL_FLOAT_, 0, uv);
    enable_client_state(GL_VERTEX_ARRAY_);
    enable_client_state(GL_TEXTURE_COORD_ARRAY_);
    color4f(1.0f, 1.0f, 1.0f, 1.0f);
    clear_color(0.0f, 0.0f, 0.0f, 1.0f);

    const auto draw_and_expect = [&](int r, int g, int b, const char* what) {
        clear(GL_COLOR_BUFFER_BIT_);
        draw_arrays(GL_TRIANGLES_, 0, 6);
        finish();
        const PixelProbe::Rgba p = probe.At(32, 32);
        EXPECT_TRUE((p.r > 200) == (r > 0) && (p.g > 200) == (g > 0) && (p.b > 200) == (b > 0))
            << what << ": pixel = (" << (int)p.r << ',' << (int)p.g << ',' << (int)p.b
            << "), expected (" << r << ',' << g << ',' << b << ')';
    };

    // Route 1: the wrapper sees the bytes and reorders them.
    tex_image2d(GL_TEXTURE_2D_, 0, GL_RGBA_, 2, 2, 0, GL_BGRA_, GL_UNSIGNED_BYTE_, texels);
    draw_and_expect(1, 0, 0, "GL_BGRA texture from client memory samples as described");

    // Route 2: the same bytes through a pixel buffer object, which the
    // wrapper cannot reach into.
    GLuint pbo = 0;
    gen_buffers(1, &pbo);
    bind_buffer(GL_PIXEL_UNPACK_BUFFER_, pbo);
    buffer_data(GL_PIXEL_UNPACK_BUFFER_, static_cast<GLsizeiptr>(sizeof texels), texels,
               GL_STATIC_DRAW_);
    tex_image2d(GL_TEXTURE_2D_, 0, GL_RGBA_, 2, 2, 0, GL_BGRA_, GL_UNSIGNED_BYTE_, nullptr);
    bind_buffer(GL_PIXEL_UNPACK_BUFFER_, 0);
    draw_and_expect(1, 0, 0, "GL_BGRA texture from a pixel buffer samples as described");

    // The same bytes uploaded as ordinary RGBA must come out the other way
    // round - and this is the case a per-texture mode gets wrong if it is
    // never cleared, because the level before it was the swizzled one.
    tex_image2d(GL_TEXTURE_2D_, 0, GL_RGBA_, 2, 2, 0, GL_RGBA_, GL_UNSIGNED_BYTE_, texels);
    draw_and_expect(0, 0, 1, "an ordinary upload afterwards is not read swizzled");
    // ...and back again, so the mode is not one-way.
    tex_image2d(GL_TEXTURE_2D_, 0, GL_RGBA_, 2, 2, 0, GL_BGRA_, GL_UNSIGNED_BYTE_, texels);
    draw_and_expect(1, 0, 0, "switching back to GL_BGRA takes effect");

    // Route 3: allocate empty, then fill through a pixel-buffer sub-image -
    // the shape OptiFine's async uploads take, and one that used to fall
    // through to the driver as raw GL_BGRA (GL_INVALID_OPERATION, black).
    tex_image2d(GL_TEXTURE_2D_, 0, GL_RGBA_, 2, 2, 0, GL_BGRA_, GL_UNSIGNED_BYTE_, nullptr);
    bind_buffer(GL_PIXEL_UNPACK_BUFFER_, pbo);
    tex_sub_image2d(GL_TEXTURE_2D_, 0, 0, 0, 2, 2, GL_BGRA_, GL_UNSIGNED_BYTE_, nullptr);
    bind_buffer(GL_PIXEL_UNPACK_BUFFER_, 0);
    draw_and_expect(1, 0, 0, "pixel-buffer sub-image into an empty texture");

    // Route 4: a sub-rectangle out of a wider client image, described with
    // GL_UNPACK_ROW_LENGTH and the skip offsets - how Minecraft stitches its
    // atlases. The tight-row-only swap this replaced read the source as if
    // the rectangle were contiguous, corrupting every such upload.
    static const GLubyte wide[4 * 4 * 4] = {
        // row 0: blue blue red red   (as BGRA bytes)
        255, 0, 0, 255, 255, 0, 0, 255, 0, 0, 255, 255, 0, 0, 255, 255,
        // row 1: same
        255, 0, 0, 255, 255, 0, 0, 255, 0, 0, 255, 255, 0, 0, 255, 255,
        // rows 2-3: blue
        255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255,
        255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255,
    };
    tex_image2d(GL_TEXTURE_2D_, 0, GL_RGBA_, 2, 2, 0, GL_BGRA_, GL_UNSIGNED_BYTE_, nullptr);
    pixel_storei(GL_UNPACK_ROW_LENGTH_, 4);
    pixel_storei(GL_UNPACK_SKIP_PIXELS_, 2);
    tex_sub_image2d(GL_TEXTURE_2D_, 0, 0, 0, 2, 2, GL_BGRA_, GL_UNSIGNED_BYTE_, wide);
    pixel_storei(GL_UNPACK_ROW_LENGTH_, 0);
    pixel_storei(GL_UNPACK_SKIP_PIXELS_, 0);
    // Only the red 2x2 at columns 2-3, rows 0-1 was selected; the probe reads
    // the texture centre, so a correct upload shows red everywhere.
    draw_and_expect(1, 0, 0, "row-length + skip sub-rectangle keeps its bytes straight");

    disable_client_state(GL_TEXTURE_COORD_ARRAY_);
    disable_client_state(GL_VERTEX_ARRAY_);
    EXPECT_EQ(get_error(), GL_NO_ERROR_);
}

} // namespace
