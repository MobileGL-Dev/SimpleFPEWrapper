// SimpleFPEWrapper - tests/gtest_interleaved_arrays.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// glInterleavedArrays is a lookup table (GL 2.1 table 2.5) turned into four
// gl*Pointer calls, so a single wrong number in it is invisible until it is
// the stride an application relies on: GL_T4F_C4F_N3F_V4F carried a tight
// stride of 14 floats where the spec says 15, which the row's own offsets
// already contradicted (the vertex block starts at 11f and is 4 wide). Every
// vertex past the first then read its components out of the previous one
// (plans/16 M3).
//
// The check is per format and stride-shaped on purpose: each row is drawn
// twice from the same tightly-packed bytes, once through glInterleavedArrays
// (stride 0, so the table's own `tight` decides) and once through the
// explicit pointer calls with the spec's stride and offsets spelled out. The
// two framebuffers must be identical, which no single-probe assertion could
// promise, and the reference render never consults the table.
//
// The drawn color is magenta - its own mirror image under an R/B swap - so
// llvmpipe's BGRA readback quirk cannot turn a correct row into a wrong one.

#include "sfpew_gtest.h"

#include <cstring>
#include <string>
#include <vector>

namespace {

using sfpew_test::ContextTest;
using sfpew_test::GLbitfield;
using sfpew_test::GLenum;
using sfpew_test::GLfloat;
using sfpew_test::GLint;
using sfpew_test::GLsizei;
using sfpew_test::GLubyte;

constexpr int kSize = 64;
constexpr GLbitfield GL_COLOR_BUFFER_BIT_ = 0x00004000;
constexpr GLenum GL_TRIANGLES_ = 0x0004;
constexpr GLenum GL_UNSIGNED_BYTE_ = 0x1401;
constexpr GLenum GL_FLOAT_ = 0x1406;
constexpr GLenum GL_RGBA_ = 0x1908;
constexpr GLenum GL_NO_ERROR_ = 0;
constexpr GLenum GL_VERTEX_ARRAY_ = 0x8074;
constexpr GLenum GL_NORMAL_ARRAY_ = 0x8075;
constexpr GLenum GL_COLOR_ARRAY_ = 0x8076;
constexpr GLenum GL_TEXTURE_COORD_ARRAY_ = 0x8078;

constexpr size_t F = sizeof(GLfloat);
// Four unsigned bytes, which table 2.5 already rounds up to a float boundary.
constexpr size_t C = 4;

// GL 2.1 table 2.5, transcribed offset by offset rather than derived, so a
// wrong number in the implementation cannot be reproduced by a wrong number
// here that shares its derivation.
struct Row {
    const char* name;
    GLenum format;
    int tex, color, normal, vertex; // component counts, 0 = array absent
    bool color_ubyte;
    size_t tex_off, color_off, normal_off, vertex_off, stride;
};

constexpr Row kRows[] = {
    {"V2F", 0x2A20, 0, 0, 0, 2, false, 0, 0, 0, 0, 2 * F},
    {"V3F", 0x2A21, 0, 0, 0, 3, false, 0, 0, 0, 0, 3 * F},
    {"C4UB_V2F", 0x2A22, 0, 4, 0, 2, true, 0, 0, 0, C, C + 2 * F},
    {"C4UB_V3F", 0x2A23, 0, 4, 0, 3, true, 0, 0, 0, C, C + 3 * F},
    {"C3F_V3F", 0x2A24, 0, 3, 0, 3, false, 0, 0, 0, 3 * F, 6 * F},
    {"N3F_V3F", 0x2A25, 0, 0, 3, 3, false, 0, 0, 0, 3 * F, 6 * F},
    {"C4F_N3F_V3F", 0x2A26, 0, 4, 3, 3, false, 0, 0, 4 * F, 7 * F, 10 * F},
    {"T2F_V3F", 0x2A27, 2, 0, 0, 3, false, 0, 0, 0, 2 * F, 5 * F},
    {"T4F_V4F", 0x2A28, 4, 0, 0, 4, false, 0, 0, 0, 4 * F, 8 * F},
    {"T2F_C4UB_V3F", 0x2A29, 2, 4, 0, 3, true, 0, 2 * F, 0, 2 * F + C, C + 5 * F},
    {"T2F_C3F_V3F", 0x2A2A, 2, 3, 0, 3, false, 0, 2 * F, 0, 5 * F, 8 * F},
    {"T2F_N3F_V3F", 0x2A2B, 2, 0, 3, 3, false, 0, 0, 2 * F, 5 * F, 8 * F},
    {"T2F_C4F_N3F_V3F", 0x2A2C, 2, 4, 3, 3, false, 0, 2 * F, 6 * F, 9 * F, 12 * F},
    {"T4F_C4F_N3F_V4F", 0x2A2D, 4, 4, 3, 4, false, 0, 4 * F, 8 * F, 11 * F, 15 * F},
};

// Two triangles covering the whole viewport, so any mis-strided vertex shows
// up as coverage that differs from the reference render.
constexpr GLfloat kQuad[6][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, -1}, {1, 1}, {-1, 1}};

class InterleavedArraysTest : public ContextTest {
protected:
    InterleavedArraysTest() : ContextTest(sfpew_test::Backend::GLES3, kSize) {}

    void SetUp() override {
        ContextTest::SetUp();
        if (::testing::Test::IsSkipped()) return;

        clear_color_ = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glClearColor");
        clear_ = Get<void (*)(GLbitfield)>("glClear");
        color4f_ = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glColor4f");
        draw_arrays_ = Get<void (*)(GLenum, GLint, GLsizei)>("glDrawArrays");
        finish_ = Get<void (*)()>("glFinish");
        get_error_ = Get<GLenum (*)()>("glGetError");
        enable_client_state_ = Get<void (*)(GLenum)>("glEnableClientState");
        disable_client_state_ = Get<void (*)(GLenum)>("glDisableClientState");
        interleaved_arrays_ = Get<void (*)(GLenum, GLsizei, const void*)>("glInterleavedArrays");
        vertex_pointer_ = Get<void (*)(GLint, GLenum, GLsizei, const void*)>("glVertexPointer");
        color_pointer_ = Get<void (*)(GLint, GLenum, GLsizei, const void*)>("glColorPointer");
        normal_pointer_ = Get<void (*)(GLenum, GLsizei, const void*)>("glNormalPointer");
        tex_coord_pointer_ = Get<void (*)(GLint, GLenum, GLsizei, const void*)>("glTexCoordPointer");
        read_pixels_ =
            Get<void (*)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*)>("glReadPixels");
        ASSERT_NE(interleaved_arrays_, nullptr);
        ASSERT_NE(read_pixels_, nullptr);
        clear_color_(0.0f, 0.0f, 0.0f, 1.0f);
    }

    // One vertex block per corner, laid out exactly where the spec row says.
    // Colors are magenta wherever the row carries a color array; where it does
    // not, the current color supplies the same magenta, so every row is
    // expected to paint the identical picture.
    static std::vector<GLubyte> BuildData(const Row& row) {
        std::vector<GLubyte> bytes(row.stride * 6, 0);
        for (int v = 0; v < 6; ++v) {
            GLubyte* base = bytes.data() + row.stride * static_cast<size_t>(v);
            GLfloat position[4] = {kQuad[v][0], kQuad[v][1], 0.0f, 1.0f};
            std::memcpy(base + row.vertex_off, position,
                        static_cast<size_t>(row.vertex) * sizeof(GLfloat));
            if (row.tex > 0) {
                const GLfloat texcoord[4] = {0.0f, 0.0f, 0.0f, 1.0f};
                std::memcpy(base + row.tex_off, texcoord,
                            static_cast<size_t>(row.tex) * sizeof(GLfloat));
            }
            if (row.normal > 0) {
                const GLfloat normal[3] = {0.0f, 0.0f, 1.0f};
                std::memcpy(base + row.normal_off, normal, sizeof normal);
            }
            if (row.color > 0 && row.color_ubyte) {
                const GLubyte magenta[4] = {255, 0, 255, 255};
                std::memcpy(base + row.color_off, magenta,
                            static_cast<size_t>(row.color));
            } else if (row.color > 0) {
                const GLfloat magenta[4] = {1.0f, 0.0f, 1.0f, 1.0f};
                std::memcpy(base + row.color_off, magenta,
                            static_cast<size_t>(row.color) * sizeof(GLfloat));
            }
        }
        return bytes;
    }

    void Capture(std::vector<GLubyte>* out) {
        out->resize(static_cast<size_t>(kSize) * kSize * 4);
        finish_();
        read_pixels_(0, 0, kSize, kSize, GL_RGBA_, GL_UNSIGNED_BYTE_, out->data());
    }

    void DisableAll() {
        disable_client_state_(GL_VERTEX_ARRAY_);
        disable_client_state_(GL_COLOR_ARRAY_);
        disable_client_state_(GL_NORMAL_ARRAY_);
        disable_client_state_(GL_TEXTURE_COORD_ARRAY_);
    }

    // The wrapper's own path: stride 0 hands the decision to the table.
    void RenderInterleaved(const Row& row, const std::vector<GLubyte>& data,
                           std::vector<GLubyte>* out) {
        DisableAll();
        color4f_(1.0f, 0.0f, 1.0f, 1.0f);
        clear_(GL_COLOR_BUFFER_BIT_);
        interleaved_arrays_(row.format, 0, data.data());
        draw_arrays_(GL_TRIANGLES_, 0, 6);
        Capture(out);
    }

    // The same declaration written out by hand from the spec row.
    void RenderExplicit(const Row& row, const std::vector<GLubyte>& data,
                        std::vector<GLubyte>* out) {
        DisableAll();
        color4f_(1.0f, 0.0f, 1.0f, 1.0f);
        clear_(GL_COLOR_BUFFER_BIT_);
        const GLsizei stride = static_cast<GLsizei>(row.stride);
        const GLubyte* base = data.data();
        if (row.tex > 0) {
            enable_client_state_(GL_TEXTURE_COORD_ARRAY_);
            tex_coord_pointer_(row.tex, GL_FLOAT_, stride, base + row.tex_off);
        }
        if (row.color > 0) {
            enable_client_state_(GL_COLOR_ARRAY_);
            color_pointer_(row.color, row.color_ubyte ? GL_UNSIGNED_BYTE_ : GL_FLOAT_, stride,
                           base + row.color_off);
        }
        if (row.normal > 0) {
            enable_client_state_(GL_NORMAL_ARRAY_);
            normal_pointer_(GL_FLOAT_, stride, base + row.normal_off);
        }
        enable_client_state_(GL_VERTEX_ARRAY_);
        vertex_pointer_(row.vertex, GL_FLOAT_, stride, base + row.vertex_off);
        draw_arrays_(GL_TRIANGLES_, 0, 6);
        Capture(out);
    }

    static bool IsMagenta(const std::vector<GLubyte>& fb, int x, int y) {
        const size_t i = (static_cast<size_t>(y) * kSize + static_cast<size_t>(x)) * 4;
        return fb[i] > 200 && fb[i + 1] <= 50 && fb[i + 2] > 200;
    }

    void (*clear_color_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*clear_)(GLbitfield) = nullptr;
    void (*color4f_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*draw_arrays_)(GLenum, GLint, GLsizei) = nullptr;
    void (*finish_)() = nullptr;
    GLenum (*get_error_)() = nullptr;
    void (*enable_client_state_)(GLenum) = nullptr;
    void (*disable_client_state_)(GLenum) = nullptr;
    void (*interleaved_arrays_)(GLenum, GLsizei, const void*) = nullptr;
    void (*vertex_pointer_)(GLint, GLenum, GLsizei, const void*) = nullptr;
    void (*color_pointer_)(GLint, GLenum, GLsizei, const void*) = nullptr;
    void (*normal_pointer_)(GLenum, GLsizei, const void*) = nullptr;
    void (*tex_coord_pointer_)(GLint, GLenum, GLsizei, const void*) = nullptr;
    void (*read_pixels_)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*) = nullptr;
};

TEST_F(InterleavedArraysTest, EveryFormatsTightStrideMatchesTheSpecTable) {
    for (const Row& row : kRows) {
        SCOPED_TRACE(std::string("GL_") + row.name);
        const std::vector<GLubyte> data = BuildData(row);

        std::vector<GLubyte> from_table, from_spec;
        RenderInterleaved(row, data, &from_table);
        RenderExplicit(row, data, &from_spec);

        EXPECT_TRUE(IsMagenta(from_spec, kSize / 2, kSize / 2))
            << "the reference declaration must cover the viewport";
        EXPECT_EQ(from_table, from_spec)
            << "glInterleavedArrays must lay the block out exactly as table 2.5 says";
        EXPECT_EQ(get_error_(), GL_NO_ERROR_);
    }
}

// Table 2.5 also says which arrays a format switches OFF - a format with no
// normal block must leave GL_NORMAL_ARRAY disabled, and so on - so a leftover
// enable from a previous declaration cannot feed the draw stale pointers.
TEST_F(InterleavedArraysTest, FormatsDisableTheArraysTheyDoNotCarry) {
    const Row& with_color = kRows[4];  // C3F_V3F
    const Row& without_color = kRows[1]; // V3F
    const std::vector<GLubyte> colored = BuildData(with_color);
    const std::vector<GLubyte> plain = BuildData(without_color);

    std::vector<GLubyte> fb;
    RenderInterleaved(with_color, colored, &fb);
    ASSERT_TRUE(IsMagenta(fb, kSize / 2, kSize / 2)) << "baseline C3F_V3F must draw magenta";

    // The color array is still declared over `colored`; V3F must disable it,
    // so the current color decides - and it is deliberately a different one.
    color4f_(0.0f, 1.0f, 0.0f, 1.0f);
    clear_(GL_COLOR_BUFFER_BIT_);
    interleaved_arrays_(without_color.format, 0, plain.data());
    draw_arrays_(GL_TRIANGLES_, 0, 6);
    Capture(&fb);
    const size_t center = (static_cast<size_t>(kSize / 2) * kSize + kSize / 2) * 4;
    EXPECT_GT(fb[center + 1], 200) << "GL_V3F must disable the color array and use glColor4f";
    EXPECT_LE(fb[center], 50) << "a format without a color block must not keep the old one";
    EXPECT_LE(fb[center + 2], 50);
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

} // namespace
