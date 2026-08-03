// SimpleFPEWrapper - tests/gtest_display_list_chunks.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// Minecraft's terrain is display lists compiled from ONE shared client
// buffer: the tessellator fills its buffer with a chunk, points the vertex
// arrays at it, calls glDrawArrays inside glNewList, and then reuses the very
// same buffer for the next chunk. GL says a vertex-array draw dereferences
// its data when the command is COMPILED, so each list must keep its own
// snapshot - a wrapper that retained the pointer would replay every chunk
// with the last chunk's contents, and one that silently dropped a draw it
// could not snapshot would lose the chunk entirely.
//
// Each list here draws one quad in its own screen column with its own colour,
// so the readback names exactly which lists survived compilation and replay.
// The lists are then called three ways, because the wrapper has a separate
// path for each: one glCallLists batch (which it tries to merge into a single
// multi-draw), individual glCallList calls, and a batch after half of the
// lists have been re-recorded in place - the rebuild MC does whenever a chunk
// changes, which frees and reallocates inside the wrapper's vertex arena.
//
// The batched replay also has to work on a backend without
// glMultiDrawArrays: MobileGlues resolves the pointer but implements nothing
// behind it up to V1.3.5, so a group drawn that way vanishes without an
// error. SFPEW_NO_MULTIDRAWARRAYS forces the path taken there - identity
// indices through the indexed multi-draw - and the triangles case sets it,
// with GL_TRIANGLES so the group cannot take the quad index path instead.

#include "sfpew_gtest.h"

#include <cstring>
#include <optional>
#include <ostream>

namespace {

using sfpew_test::ContextTest;
using sfpew_test::GLbitfield;
using sfpew_test::GLenum;
using sfpew_test::GLfloat;
using sfpew_test::GLint;
using sfpew_test::GLsizei;
using sfpew_test::GLubyte;
using sfpew_test::GLuint;

constexpr GLbitfield GL_COLOR_BUFFER_BIT_ = 0x00004000;
constexpr GLenum GL_QUADS_ = 0x0007;
constexpr GLenum GL_TRIANGLES_ = 0x0004;
constexpr GLenum GL_FLOAT_ = 0x1406;
constexpr GLenum GL_UNSIGNED_BYTE_ = 0x1401;
constexpr GLenum GL_UNSIGNED_INT_ = 0x1405;
constexpr GLenum GL_VERTEX_ARRAY_ = 0x8074;
constexpr GLenum GL_COLOR_ARRAY_ = 0x8076;
constexpr GLenum GL_TEXTURE_COORD_ARRAY_ = 0x8078;
constexpr GLenum GL_COMPILE_ = 0x1300;
constexpr GLenum GL_NO_ERROR_ = 0;

// The wrapper's captured-list vertex arena is 64 MiB; CHUNKS * QUAD_BYTES
// stays far below that, so this exercises the arena path rather than the
// per-list fallback. The count is still high enough that the batch path has
// to stitch many lists together.
constexpr int kChunks = 64;
// A quad needs four vertices, the same band as triangles needs six; the
// buffer is sized for the larger so one shape serves both modes.
constexpr int kMaxVertsPerChunk = 6;
constexpr int kStride = 32; // MC's tessellator stride: xyz, uv, colour, padding

enum class Mode { Quads, Triangles };

// So gtest's parameter printer names cases by mode instead of a raw byte
// dump, which is what CMake's gtest_discover_tests then puts into the ctest
// test name.
void PrintTo(Mode mode, std::ostream* os) {
    *os << (mode == Mode::Quads ? "Quads" : "Triangles");
}

class DisplayListChunksTest : public ContextTest,
                              public ::testing::WithParamInterface<Mode> {
protected:
    void SetUp() override {
        // Set before any wrapper call so the getenv-based switch sees it.
        if (GetParam() == Mode::Triangles) {
            ASSERT_EQ(setenv("SFPEW_NO_MULTIDRAWARRAYS", "1", 1), 0);
        }
        ContextTest::SetUp();
        if (::testing::Test::IsSkipped()) return;

        clear_color_ = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glClearColor");
        clear_ = Get<void (*)(GLbitfield)>("glClear");
        read_pixels_ =
            Get<void (*)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*)>("glReadPixels");
        get_error_ = Get<GLenum (*)()>("glGetError");
        finish_ = Get<void (*)()>("glFinish");
        enable_client_state_ = Get<void (*)(GLenum)>("glEnableClientState");
        disable_client_state_ = Get<void (*)(GLenum)>("glDisableClientState");
        vertex_pointer_ = Get<void (*)(GLint, GLenum, GLsizei, const void*)>("glVertexPointer");
        color_pointer_ = Get<void (*)(GLint, GLenum, GLsizei, const void*)>("glColorPointer");
        tex_coord_pointer_ =
            Get<void (*)(GLint, GLenum, GLsizei, const void*)>("glTexCoordPointer");
        draw_arrays_ = Get<void (*)(GLenum, GLint, GLsizei)>("glDrawArrays");
        gen_lists_ = Get<GLuint (*)(GLsizei)>("glGenLists");
        new_list_ = Get<void (*)(GLuint, GLenum)>("glNewList");
        end_list_ = Get<void (*)()>("glEndList");
        call_list_ = Get<void (*)(GLuint)>("glCallList");
        call_lists_ = Get<void (*)(GLsizei, GLenum, const void*)>("glCallLists");

        clear_color_(0.0f, 0.0f, 0.0f, 1.0f);
    }

    int VertsPerChunk() const { return GetParam() == Mode::Quads ? 4 : 6; }

    // One buffer for every chunk, exactly like the tessellator's.
    static GLubyte* SharedBuffer() {
        static GLubyte buffer[kMaxVertsPerChunk * kStride];
        return buffer;
    }

    // Chunk i owns the horizontal band [i/CHUNKS, (i+1)/CHUNKS] in clip space
    // and is tinted with a colour derived from i, so a readback in that band
    // proves which chunk's data the list replayed.
    void FillSharedBuffer(int chunk) {
        const float left = -1.0f + 2.0f * static_cast<float>(chunk) / kChunks;
        const float right = -1.0f + 2.0f * static_cast<float>(chunk + 1) / kChunks;
        const GLubyte r = static_cast<GLubyte>(40 + 3 * chunk);
        const GLubyte g = static_cast<GLubyte>(255 - 3 * chunk);
        const float xs[4] = {left, right, right, left};
        const float ys[4] = {-1.0f, -1.0f, 1.0f, 1.0f};
        // Corner order: a quad walks its four in turn, two triangles repeat
        // the diagonal. Both sweep the same band.
        static const int quad_corners[4] = {0, 1, 2, 3};
        static const int triangle_corners[6] = {0, 1, 2, 0, 2, 3};
        const int* corners = GetParam() == Mode::Quads ? quad_corners : triangle_corners;
        GLubyte* shared = SharedBuffer();
        std::memset(shared, 0, sizeof(GLubyte) * kMaxVertsPerChunk * kStride);
        for (int v = 0; v < VertsPerChunk(); ++v) {
            GLubyte* vertex = shared + v * kStride;
            auto* position = reinterpret_cast<GLfloat*>(vertex);
            position[0] = xs[corners[v]];
            position[1] = ys[corners[v]];
            position[2] = 0.0f;
            auto* uv = reinterpret_cast<GLfloat*>(vertex + 12);
            uv[0] = 0.0f;
            uv[1] = 0.0f;
            vertex[20] = r;
            vertex[21] = g;
            vertex[22] = 64;
            vertex[23] = 255;
        }
    }

    // Compile chunk `chunk` into `list`, from the shared buffer, the way the
    // tessellator does: point the arrays at the buffer that is about to be
    // overwritten, draw, and close the list.
    void RecordChunk(GLuint list, int chunk) {
        FillSharedBuffer(chunk);
        GLubyte* shared = SharedBuffer();
        new_list_(list, GL_COMPILE_);
        vertex_pointer_(3, GL_FLOAT_, kStride, shared);
        tex_coord_pointer_(2, GL_FLOAT_, kStride, shared + 12);
        color_pointer_(4, GL_UNSIGNED_BYTE_, kStride, shared + 20);
        enable_client_state_(GL_VERTEX_ARRAY_);
        enable_client_state_(GL_TEXTURE_COORD_ARRAY_);
        enable_client_state_(GL_COLOR_ARRAY_);
        draw_arrays_(GetParam() == Mode::Quads ? GL_QUADS_ : GL_TRIANGLES_, 0, VertsPerChunk());
        disable_client_state_(GL_COLOR_ARRAY_);
        disable_client_state_(GL_TEXTURE_COORD_ARRAY_);
        disable_client_state_(GL_VERTEX_ARRAY_);
        end_list_();
    }

    // Every chunk must have drawn its own band in its own colour.
    void CheckAllChunks(const char* what) {
        GLubyte pixels[64 * 4];
        read_pixels_(0, 32, 64, 1, 0x1908 /* GL_RGBA */, GL_UNSIGNED_BYTE_, pixels);
        int missing = 0, wrong = 0, first_bad = -1;
        for (int chunk = 0; chunk < kChunks; ++chunk) {
            // Band centre for a 64-wide readback with CHUNKS == 64: one pixel
            // each.
            const int x = chunk;
            const GLubyte* pixel = pixels + x * 4;
            const GLubyte expected_r = static_cast<GLubyte>(40 + 3 * chunk);
            const GLubyte expected_g = static_cast<GLubyte>(255 - 3 * chunk);
            const bool blank = pixel[0] == 0 && pixel[1] == 0 && pixel[2] == 0;
            const bool near = pixel[0] + 6 >= expected_r && pixel[0] <= expected_r + 6 &&
                              pixel[1] + 6 >= expected_g && pixel[1] <= expected_g + 6;
            if (blank) {
                ++missing;
                if (first_bad < 0) first_bad = chunk;
            } else if (!near) {
                ++wrong;
                if (first_bad < 0) first_bad = chunk;
            }
        }
        if (missing != 0 || wrong != 0) {
            const int bad = first_bad < 0 ? 0 : first_bad;
            const GLubyte* pixel = pixels + bad * 4;
            FAIL() << what << ": " << missing << '/' << kChunks << " chunks missing, " << wrong
                   << " wrong; first bad chunk " << bad << " = (" << (int)pixel[0] << ','
                   << (int)pixel[1] << ',' << (int)pixel[2] << "), expected (" << (40 + 3 * bad)
                   << ',' << (255 - 3 * bad) << ",64)";
        }
    }

    void (*clear_color_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*clear_)(GLbitfield) = nullptr;
    void (*read_pixels_)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*) = nullptr;
    GLenum (*get_error_)() = nullptr;
    void (*finish_)() = nullptr;
    void (*enable_client_state_)(GLenum) = nullptr;
    void (*disable_client_state_)(GLenum) = nullptr;
    void (*vertex_pointer_)(GLint, GLenum, GLsizei, const void*) = nullptr;
    void (*color_pointer_)(GLint, GLenum, GLsizei, const void*) = nullptr;
    void (*tex_coord_pointer_)(GLint, GLenum, GLsizei, const void*) = nullptr;
    void (*draw_arrays_)(GLenum, GLint, GLsizei) = nullptr;
    GLuint (*gen_lists_)(GLsizei) = nullptr;
    void (*new_list_)(GLuint, GLenum) = nullptr;
    void (*end_list_)() = nullptr;
    void (*call_list_)(GLuint) = nullptr;
    void (*call_lists_)(GLsizei, GLenum, const void*) = nullptr;
};

TEST_P(DisplayListChunksTest, EveryChunkKeepsItsOwnVertexSnapshot) {
    const GLuint base = gen_lists_(kChunks);
    ASSERT_NE(base, 0u) << "glGenLists(" << kChunks << ") returned 0";

    GLuint ids[kChunks];
    for (int chunk = 0; chunk < kChunks; ++chunk) {
        ids[chunk] = base + static_cast<GLuint>(chunk);
        RecordChunk(ids[chunk], chunk);
    }

    // One batch, the shape MC uses every frame.
    clear_(GL_COLOR_BUFFER_BIT_);
    call_lists_(kChunks, GL_UNSIGNED_INT_, ids);
    finish_();
    CheckAllChunks("glCallLists batch replays every chunk's own snapshot");

    // The same lists one at a time: a different wrapper path.
    clear_(GL_COLOR_BUFFER_BIT_);
    for (int chunk = 0; chunk < kChunks; ++chunk) call_list_(ids[chunk]);
    finish_();
    CheckAllChunks("individual glCallList replays every chunk");

    // Rebuild half the lists in place - the chunk update MC does constantly -
    // and draw again. Re-recording frees the old snapshot inside the arena.
    for (int chunk = 0; chunk < kChunks; chunk += 2) RecordChunk(ids[chunk], chunk);
    clear_(GL_COLOR_BUFFER_BIT_);
    call_lists_(kChunks, GL_UNSIGNED_INT_, ids);
    finish_();
    CheckAllChunks("re-recorded lists still replay their own snapshot");

    // And once more without touching anything, to catch a batch cache that
    // went stale behind the rebuild.
    clear_(GL_COLOR_BUFFER_BIT_);
    call_lists_(kChunks, GL_UNSIGNED_INT_, ids);
    finish_();
    CheckAllChunks("the batch after a rebuild is still correct");

    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

INSTANTIATE_TEST_SUITE_P(Modes, DisplayListChunksTest,
                         ::testing::Values(Mode::Quads, Mode::Triangles),
                         [](const ::testing::TestParamInfo<Mode>& info) {
                             return info.param == Mode::Quads ? "Quads" : "TrianglesNoMultiDraw";
                         });

} // namespace
