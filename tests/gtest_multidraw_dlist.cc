// SimpleFPEWrapper - tests/gtest_multidraw_dlist.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// glMultiDrawArrays inside glNewList (plans/15 15.1). GL 2.1 5.4 compiles a
// vertex-array draw into the list at COMPILE time, dereferencing its arrays
// then; a multi-draw is n such draws and every one of them has to end up in
// the list. The wrapper used to have no display-list awareness here at all:
// GL_COMPILE drew the geometry immediately and the list came out empty, so
// glCallList replayed nothing and glGetError stayed clean about it - the
// worst shape a bug can have.
//
// Each case therefore checks both halves: nothing on screen while GL_COMPILE
// records, and every sub-draw's own band on screen after glCallList. The
// source array is overwritten between glEndList and glCallList, so a replay
// that kept the caller's pointer instead of a snapshot draws nothing rather
// than accidentally passing.

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
using sfpew_test::GLuint;
using sfpew_test::PixelProbe;

constexpr GLbitfield GL_COLOR_BUFFER_BIT_ = 0x00004000;
constexpr GLenum GL_TRIANGLES_ = 0x0004;
constexpr GLenum GL_QUADS_ = 0x0007;
constexpr GLenum GL_FLOAT_ = 0x1406;
constexpr GLenum GL_VERTEX_ARRAY_ = 0x8074;
constexpr GLenum GL_COLOR_ARRAY_ = 0x8076;
constexpr GLenum GL_COMPILE_ = 0x1300;
constexpr GLenum GL_COMPILE_AND_EXECUTE_ = 0x1301;
constexpr GLenum GL_NO_ERROR_ = 0;

// Three vertical bands, each its own primary color, each its own sub-draw.
// A replay that loses a sub-draw leaves that band black; one that mixes up
// first[]/count[] paints a band the wrong color.
constexpr int kBands = 3;
constexpr int kFloatsPerVertex = 5; // x, y, r, g, b
constexpr GLsizei kVertexStride = kFloatsPerVertex * static_cast<GLsizei>(sizeof(GLfloat));

class MultiDrawDisplayListTest : public ContextTest {
protected:
    void SetUp() override {
        ContextTest::SetUp();
        if (::testing::Test::IsSkipped()) return;

        clear_color_ = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glClearColor");
        clear_ = Get<void (*)(GLbitfield)>("glClear");
        get_error_ = Get<GLenum (*)()>("glGetError");
        finish_ = Get<void (*)()>("glFinish");
        enable_client_state_ = Get<void (*)(GLenum)>("glEnableClientState");
        disable_client_state_ = Get<void (*)(GLenum)>("glDisableClientState");
        vertex_pointer_ = Get<void (*)(GLint, GLenum, GLsizei, const void*)>("glVertexPointer");
        color_pointer_ = Get<void (*)(GLint, GLenum, GLsizei, const void*)>("glColorPointer");
        multi_draw_arrays_ =
            Get<void (*)(GLenum, const GLint*, const GLsizei*, GLsizei)>("glMultiDrawArrays");
        gen_lists_ = Get<GLuint (*)(GLsizei)>("glGenLists");
        new_list_ = Get<void (*)(GLuint, GLenum)>("glNewList");
        end_list_ = Get<void (*)()>("glEndList");
        call_list_ = Get<void (*)(GLuint)>("glCallList");
        auto read_pixels =
            Get<void (*)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*)>("glReadPixels");
        ASSERT_NE(read_pixels, nullptr);
        probe_.emplace(read_pixels);

        clear_color_(0.0f, 0.0f, 0.0f, 1.0f);
        get_error_();
    }

    // Band `band` spans its third of clip space; `verts_per_band` corners are
    // written in the order the mode wants them.
    void FillBands(GLfloat* out, int verts_per_band, bool quads) {
        static const GLfloat cols[kBands][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
        static const int quad_corners[4] = {0, 1, 2, 3};
        static const int triangle_corners[6] = {0, 1, 2, 0, 2, 3};
        const int* corners = quads ? quad_corners : triangle_corners;
        int w = 0;
        for (int band = 0; band < kBands; ++band) {
            const GLfloat x0 = -1.0f + 2.0f * static_cast<GLfloat>(band) / kBands;
            const GLfloat x1 = -1.0f + 2.0f * static_cast<GLfloat>(band + 1) / kBands;
            const GLfloat xs[4] = {x0, x1, x1, x0};
            const GLfloat ys[4] = {-1.0f, -1.0f, 1.0f, 1.0f};
            for (int v = 0; v < verts_per_band; ++v) {
                out[w++] = xs[corners[v]];
                out[w++] = ys[corners[v]];
                out[w++] = cols[band][0];
                out[w++] = cols[band][1];
                out[w++] = cols[band][2];
            }
        }
    }

    void PointArraysAt(const GLfloat* verts) {
        vertex_pointer_(2, GL_FLOAT_, kVertexStride, verts);
        color_pointer_(3, GL_FLOAT_, kVertexStride, verts + 2);
        enable_client_state_(GL_VERTEX_ARRAY_);
        enable_client_state_(GL_COLOR_ARRAY_);
    }

    void CheckBands(const char* what) {
        static const int xs[kBands] = {10, 32, 54};
        static const int wanted[kBands][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
        for (int band = 0; band < kBands; ++band) {
            const PixelProbe::Rgba p = probe_->At(xs[band], 32);
            EXPECT_TRUE((p.r > 200) == (wanted[band][0] > 0) &&
                        (p.g > 200) == (wanted[band][1] > 0) &&
                        (p.b > 200) == (wanted[band][2] > 0))
                << what << ": sub-draw " << band << " at (" << xs[band] << ",32) = (" << (int)p.r
                << ',' << (int)p.g << ',' << (int)p.b << "), expected (" << wanted[band][0] << ','
                << wanted[band][1] << ',' << wanted[band][2] << ')';
        }
    }

    void CheckNothingDrawn(const char* what) {
        const PixelProbe::LitPixel lit = probe_->FindLit(0, 0, size(), size());
        EXPECT_EQ(lit.count, 0) << what << ": " << lit.count << " pixels lit, first at ("
                                << lit.x << ',' << lit.y << ')';
    }

    void (*clear_color_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*clear_)(GLbitfield) = nullptr;
    GLenum (*get_error_)() = nullptr;
    void (*finish_)() = nullptr;
    void (*enable_client_state_)(GLenum) = nullptr;
    void (*disable_client_state_)(GLenum) = nullptr;
    void (*vertex_pointer_)(GLint, GLenum, GLsizei, const void*) = nullptr;
    void (*color_pointer_)(GLint, GLenum, GLsizei, const void*) = nullptr;
    void (*multi_draw_arrays_)(GLenum, const GLint*, const GLsizei*, GLsizei) = nullptr;
    GLuint (*gen_lists_)(GLsizei) = nullptr;
    void (*new_list_)(GLuint, GLenum) = nullptr;
    void (*end_list_)() = nullptr;
    void (*call_list_)(GLuint) = nullptr;
    std::optional<PixelProbe> probe_;
};

TEST_F(MultiDrawDisplayListTest, CompileRecordsEverySubDrawAndDrawsNothing) {
    constexpr int kVertsPerBand = 6;
    GLfloat verts[kBands * kVertsPerBand * kFloatsPerVertex];
    FillBands(verts, kVertsPerBand, /*quads=*/false);
    static const GLint firsts[kBands] = {0, 6, 12};
    static const GLsizei counts[kBands] = {6, 6, 6};

    PointArraysAt(verts);

    const GLuint list = gen_lists_(1);
    ASSERT_NE(list, 0u) << "glGenLists(1) returned 0";

    clear_(GL_COLOR_BUFFER_BIT_);
    new_list_(list, GL_COMPILE_);
    multi_draw_arrays_(GL_TRIANGLES_, firsts, counts, kBands);
    end_list_();
    finish_();
    CheckNothingDrawn("GL_COMPILE executed the glMultiDrawArrays it was supposed to record");

    // Whatever the list holds now must be its own copy: the array the draw
    // was compiled from is gone, exactly as MC's tessellator buffer is by the
    // time the chunk list is called.
    std::memset(verts, 0, sizeof verts);
    disable_client_state_(GL_COLOR_ARRAY_);
    disable_client_state_(GL_VERTEX_ARRAY_);

    clear_(GL_COLOR_BUFFER_BIT_);
    call_list_(list);
    finish_();
    CheckBands("glCallList replay of a recorded glMultiDrawArrays");

    // Equivalence: the same call executed immediately paints the same thing.
    FillBands(verts, kVertsPerBand, /*quads=*/false);
    PointArraysAt(verts);
    clear_(GL_COLOR_BUFFER_BIT_);
    multi_draw_arrays_(GL_TRIANGLES_, firsts, counts, kBands);
    finish_();
    CheckBands("immediate glMultiDrawArrays");

    disable_client_state_(GL_COLOR_ARRAY_);
    disable_client_state_(GL_VERTEX_ARRAY_);
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

// GL_QUADS is the mode glMultiDrawArrays can never hand to the backend
// (it becomes an indexed draw whose index count differs per sub-draw), so
// recording it exercises the capture path against a mode the immediate path
// only reaches through its own loop.
TEST_F(MultiDrawDisplayListTest, CompileRecordsQuadSubDraws) {
    constexpr int kVertsPerBand = 4;
    GLfloat verts[kBands * kVertsPerBand * kFloatsPerVertex];
    FillBands(verts, kVertsPerBand, /*quads=*/true);
    static const GLint firsts[kBands] = {0, 4, 8};
    static const GLsizei counts[kBands] = {4, 4, 4};

    PointArraysAt(verts);

    const GLuint list = gen_lists_(1);
    ASSERT_NE(list, 0u) << "glGenLists(1) returned 0";

    clear_(GL_COLOR_BUFFER_BIT_);
    new_list_(list, GL_COMPILE_);
    multi_draw_arrays_(GL_QUADS_, firsts, counts, kBands);
    end_list_();
    finish_();
    CheckNothingDrawn("GL_COMPILE executed the GL_QUADS glMultiDrawArrays");

    std::memset(verts, 0, sizeof verts);
    clear_(GL_COLOR_BUFFER_BIT_);
    call_list_(list);
    finish_();
    CheckBands("glCallList replay of a recorded GL_QUADS glMultiDrawArrays");

    disable_client_state_(GL_COLOR_ARRAY_);
    disable_client_state_(GL_VERTEX_ARRAY_);
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

// GL_COMPILE_AND_EXECUTE must do both, and the executed half must not be the
// thing that got recorded (or a second glCallList would draw nothing).
TEST_F(MultiDrawDisplayListTest, CompileAndExecuteDrawsAndStillRecords) {
    constexpr int kVertsPerBand = 6;
    GLfloat verts[kBands * kVertsPerBand * kFloatsPerVertex];
    FillBands(verts, kVertsPerBand, /*quads=*/false);
    static const GLint firsts[kBands] = {0, 6, 12};
    static const GLsizei counts[kBands] = {6, 6, 6};

    PointArraysAt(verts);

    const GLuint list = gen_lists_(1);
    ASSERT_NE(list, 0u) << "glGenLists(1) returned 0";

    clear_(GL_COLOR_BUFFER_BIT_);
    new_list_(list, GL_COMPILE_AND_EXECUTE_);
    multi_draw_arrays_(GL_TRIANGLES_, firsts, counts, kBands);
    end_list_();
    finish_();
    CheckBands("GL_COMPILE_AND_EXECUTE did not execute the glMultiDrawArrays");

    std::memset(verts, 0, sizeof verts);
    clear_(GL_COLOR_BUFFER_BIT_);
    call_list_(list);
    finish_();
    CheckBands("GL_COMPILE_AND_EXECUTE did not record the glMultiDrawArrays");

    disable_client_state_(GL_COLOR_ARRAY_);
    disable_client_state_(GL_VERTEX_ARRAY_);
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

} // namespace
