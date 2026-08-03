// SimpleFPEWrapper - tests/gtest_rastercolor.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// Ported from piglit's tests/spec/gl-1.0/rastercolor.c (MIT-style license;
// see piglit's COPYING). glRasterPos SNAPSHOTS the current color along with
// the transformed position: a later glColor changes what primitives draw
// with, but every glBitmap issued from that raster position keeps the color
// that was current when glRasterPos ran.
//
// The sequence is: raster position set while green is current, then blue
// becomes current, then a bitmap (must be green), then an immediate-mode
// quad (must be blue), then a second bitmap advanced by the first one's
// xmove (must still be green). That last one also pins the raster-position
// ADVANCE: glBitmap moves the raster position by its xmove/ymove, and the
// snapshot has to survive the move.

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

constexpr int kWindow = 64;
constexpr GLenum GL_QUADS_ = 0x0007;
constexpr GLbitfield GL_COLOR_BUFFER_BIT_ = 0x00004000;
constexpr GLenum GL_PROJECTION_ = 0x1701;
constexpr GLenum GL_MODELVIEW_ = 0x1700;
constexpr GLenum GL_UNPACK_ALIGNMENT_ = 0x0CF5;
constexpr GLenum GL_NO_ERROR_ = 0;

using RasterColorTest = ContextTest;

TEST_F(RasterColorTest, RasterPosSnapshotsTheCurrentColorForBitmap) {
    auto vertex2f = Get<void (*)(GLfloat, GLfloat)>("glVertex2f");
    auto begin = Get<void (*)(GLenum)>("glBegin");
    auto end = Get<void (*)()>("glEnd");
    auto color3fv = Get<void (*)(const GLfloat*)>("glColor3fv");
    auto raster_pos2i = Get<void (*)(GLint, GLint)>("glRasterPos2i");
    auto bitmap = Get<void (*)(GLsizei, GLsizei, GLfloat, GLfloat, GLfloat, GLfloat,
                               const GLubyte*)>("glBitmap");
    auto clear_color = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glClearColor");
    auto clear = Get<void (*)(GLbitfield)>("glClear");
    auto ortho = Get<void (*)(double, double, double, double, double, double)>("glOrtho");
    auto matrix_mode = Get<void (*)(GLenum)>("glMatrixMode");
    auto load_identity = Get<void (*)()>("glLoadIdentity");
    auto viewport = Get<void (*)(GLint, GLint, GLsizei, GLsizei)>("glViewport");
    auto pixel_storei = Get<void (*)(GLenum, GLint)>("glPixelStorei");
    auto finish = Get<void (*)()>("glFinish");
    auto get_error = Get<GLenum (*)()>("glGetError");
    auto read_pixels =
        Get<void (*)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*)>("glReadPixels");
    ASSERT_NE(read_pixels, nullptr);
    PixelProbe probe(read_pixels);

    static const GLfloat green[3] = {0, 1, 0};
    static const GLfloat blue[3] = {0, 0, 1};
    static const GLubyte bitmap_data[8] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

    pixel_storei(GL_UNPACK_ALIGNMENT_, 1);
    clear_color(0, 0, 0, 0);
    clear(GL_COLOR_BUFFER_BIT_);

    viewport(0, 0, kWindow, kWindow);
    matrix_mode(GL_PROJECTION_);
    load_identity();
    ortho(0, kWindow, 0, kWindow, -1, 1);
    matrix_mode(GL_MODELVIEW_);
    load_identity();
    get_error();

    // Raster color becomes green; the raster position snapshots it.
    color3fv(green);
    raster_pos2i(8, 8);

    // Primitive color becomes blue - must NOT affect the snapshot.
    color3fv(blue);

    // Bitmap 1 at the raster position, advancing x by 32 for bitmap 2.
    bitmap(8, 8, 0, 0, 32, 0, bitmap_data);

    // A blue quad between the two bitmaps.
    begin(GL_QUADS_);
    vertex2f(24, 8);
    vertex2f(32, 8);
    vertex2f(32, 16);
    vertex2f(24, 16);
    end();

    // Bitmap 2 at the advanced raster position, no further advance.
    bitmap(8, 8, 0, 0, 0, 0, bitmap_data);
    finish();

    const auto expect = [&](int x, int y, int want_r, int want_g, int want_b, const char* what) {
        const PixelProbe::Rgba p = probe.At(x, y);
        const bool ok = (p.r > 150) == (want_r != 0) && (p.g > 150) == (want_g != 0) &&
                        (p.b > 150) == (want_b != 0);
        EXPECT_TRUE(ok) << what << " at (" << x << ',' << y << "): rgb=(" << (int)p.r << ','
                        << (int)p.g << ',' << (int)p.b << ')';
    };

    expect(12, 12, 0, 1, 0, "bitmap 1 keeps raster green");
    expect(28, 12, 0, 0, 1, "quad uses current blue");
    expect(44, 12, 0, 1, 0, "bitmap 2 still raster green");
    EXPECT_EQ(get_error(), GL_NO_ERROR_);
}

} // namespace
