// SimpleFPEWrapper - tests/gtest_drawelements_dlist.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// glDrawElements / glDrawRangeElements / glMultiDrawElements inside glNewList
// (plans/15 15.3 + 15.4). GL 2.1 5.4 compiles a vertex-array draw into the
// list at COMPILE time, dereferencing its arrays - and its indices - then. The
// wrapper had no display-list awareness on the indexed path at all: GL_COMPILE
// drew the geometry immediately, the list came out empty, glCallList replayed
// nothing and glGetError stayed clean about it.
//
// Every case checks both halves - nothing on screen while GL_COMPILE records,
// the same picture as the immediate call after glCallList - and overwrites
// both source arrays between glEndList and glCallList, so a replay that kept
// the caller's pointers instead of a snapshot draws nothing rather than
// accidentally passing.

#include "sfpew_gtest.h"

#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

namespace {

using sfpew_test::ContextTest;
using sfpew_test::GLbitfield;
using sfpew_test::GLenum;
using sfpew_test::GLfloat;
using sfpew_test::GLint;
using sfpew_test::GLsizei;
using sfpew_test::GLsizeiptr;
using sfpew_test::GLuint;
using sfpew_test::PixelProbe;

constexpr GLbitfield GL_COLOR_BUFFER_BIT_ = 0x00004000;
constexpr GLenum GL_TRIANGLES_ = 0x0004;
constexpr GLenum GL_QUADS_ = 0x0007;
constexpr GLenum GL_FLOAT_ = 0x1406;
constexpr GLenum GL_UNSIGNED_BYTE_ = 0x1401;
constexpr GLenum GL_UNSIGNED_SHORT_ = 0x1403;
constexpr GLenum GL_UNSIGNED_INT_ = 0x1405;
constexpr GLenum GL_VERTEX_ARRAY_ = 0x8074;
constexpr GLenum GL_COLOR_ARRAY_ = 0x8076;
constexpr GLenum GL_ELEMENT_ARRAY_BUFFER_ = 0x8893;
constexpr GLenum GL_STATIC_DRAW_ = 0x88E4;
constexpr GLenum GL_COMPILE_ = 0x1300;
constexpr GLenum GL_COMPILE_AND_EXECUTE_ = 0x1301;
constexpr GLenum GL_NO_ERROR_ = 0;

// Three vertical bands, each its own primary color, four corners each. A
// replay that loses indices leaves a band black; one that rebases them wrong
// paints a band with another band's corners, which shows up as a wrong color
// at that band's probe.
constexpr int kBands = 3;
constexpr int kCornersPerBand = 4;
constexpr int kFloatsPerVertex = 5; // x, y, r, g, b
constexpr GLsizei kVertexStride = kFloatsPerVertex * static_cast<GLsizei>(sizeof(GLfloat));

// Which of the three indexed entry points a case drives. Multi splits the
// bands into one sub-draw each, so a recording that captures only the first
// sub-draw leaves the other two bands black.
enum class Form { Plain, Range, Multi };

class DrawElementsDisplayListTest : public ContextTest {
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
        draw_elements_ = Get<void (*)(GLenum, GLsizei, GLenum, const void*)>("glDrawElements");
        draw_range_elements_ = Get<void (*)(GLenum, GLuint, GLuint, GLsizei, GLenum, const void*)>(
            "glDrawRangeElements");
        multi_draw_elements_ =
            Get<void (*)(GLenum, const GLsizei*, GLenum, const void* const*, GLsizei)>(
                "glMultiDrawElements");
        gen_buffers_ = Get<void (*)(GLsizei, GLuint*)>("glGenBuffers");
        bind_buffer_ = Get<void (*)(GLenum, GLuint)>("glBindBuffer");
        buffer_data_ = Get<void (*)(GLenum, GLsizeiptr, const void*, GLenum)>("glBufferData");
        delete_buffers_ = Get<void (*)(GLsizei, const GLuint*)>("glDeleteBuffers");
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

    // `leadingVertices` unreferenced vertices in front of the real ones, so a
    // case can push the smallest index used well above zero.
    void FillBands(std::vector<GLfloat>& out, int leadingVertices) {
        static const GLfloat cols[kBands][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
        out.assign(static_cast<size_t>(leadingVertices + kBands * kCornersPerBand) *
                       kFloatsPerVertex,
                   0.0f);
        size_t w = static_cast<size_t>(leadingVertices) * kFloatsPerVertex;
        for (int band = 0; band < kBands; ++band) {
            const GLfloat x0 = -1.0f + 2.0f * static_cast<GLfloat>(band) / kBands;
            const GLfloat x1 = -1.0f + 2.0f * static_cast<GLfloat>(band + 1) / kBands;
            const GLfloat xs[4] = {x0, x1, x1, x0};
            const GLfloat ys[4] = {-1.0f, -1.0f, 1.0f, 1.0f};
            for (int v = 0; v < kCornersPerBand; ++v) {
                out[w++] = xs[v];
                out[w++] = ys[v];
                out[w++] = cols[band][0];
                out[w++] = cols[band][1];
                out[w++] = cols[band][2];
            }
        }
    }

    // Corner order per band, as the mode wants it, offset past the unused
    // leading vertices.
    template <typename T>
    void FillIndices(std::vector<T>& out, bool quads, int leadingVertices) {
        static const int quad_corners[4] = {0, 1, 2, 3};
        static const int triangle_corners[6] = {0, 1, 2, 0, 2, 3};
        const int* corners = quads ? quad_corners : triangle_corners;
        const int per_band = quads ? 4 : 6;
        out.clear();
        for (int band = 0; band < kBands; ++band) {
            for (int i = 0; i < per_band; ++i) {
                out.push_back(static_cast<T>(leadingVertices + band * kCornersPerBand +
                                             corners[i]));
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
                << what << ": band " << band << " at (" << xs[band] << ",32) = (" << (int)p.r
                << ',' << (int)p.g << ',' << (int)p.b << "), expected (" << wanted[band][0] << ','
                << wanted[band][1] << ',' << wanted[band][2] << ')';
        }
    }

    void CheckNothingDrawn(const char* what) {
        const PixelProbe::LitPixel lit = probe_->FindLit(0, 0, size(), size());
        EXPECT_EQ(lit.count, 0) << what << ": " << lit.count << " pixels lit, first at ("
                                << lit.x << ',' << lit.y << ')';
    }

    // The whole three-way equivalence for one (mode, index type, index
    // source, base vertex) combination: immediate, GL_COMPILE + glCallList,
    // and - for the plain glDrawElements shapes - GL_COMPILE_AND_EXECUTE.
    template <typename T>
    void RunEquivalence(const char* what, GLenum mode, GLenum type, bool indicesInBuffer,
                        int leadingVertices, Form form) {
        std::vector<GLfloat> verts;
        std::vector<T> indices;
        FillBands(verts, leadingVertices);
        FillIndices(indices, mode == GL_QUADS_, leadingVertices);
        const GLsizei count = static_cast<GLsizei>(indices.size());
        const GLuint lowest = static_cast<GLuint>(leadingVertices);
        const GLuint highest =
            static_cast<GLuint>(leadingVertices + kBands * kCornersPerBand - 1);

        GLuint indexBuffer = 0;
        if (indicesInBuffer) {
            gen_buffers_(1, &indexBuffer);
            ASSERT_NE(indexBuffer, 0u) << what << ": glGenBuffers returned 0";
            bind_buffer_(GL_ELEMENT_ARRAY_BUFFER_, indexBuffer);
            buffer_data_(GL_ELEMENT_ARRAY_BUFFER_,
                         static_cast<GLsizeiptr>(indices.size() * sizeof(T)), indices.data(),
                         GL_STATIC_DRAW_);
        }
        const void* indexArgument =
            indicesInBuffer ? nullptr : static_cast<const void*>(indices.data());

        // One sub-draw per band, each pointing at (or offset to) its own slice
        // of the same index list.
        const GLsizei perBand = count / kBands;
        GLsizei subCounts[kBands];
        const void* subIndices[kBands];
        for (int band = 0; band < kBands; ++band) {
            subCounts[band] = perBand;
            subIndices[band] = reinterpret_cast<const void*>(
                reinterpret_cast<uintptr_t>(indexArgument) +
                static_cast<uintptr_t>(band) * static_cast<uintptr_t>(perBand) * sizeof(T));
        }

        const auto draw = [&]() {
            switch (form) {
            case Form::Range:
                draw_range_elements_(mode, lowest, highest, count, type, indexArgument);
                break;
            case Form::Multi:
                multi_draw_elements_(mode, subCounts, type, subIndices, kBands);
                break;
            default:
                draw_elements_(mode, count, type, indexArgument);
                break;
            }
        };

        PointArraysAt(verts.data());

        clear_(GL_COLOR_BUFFER_BIT_);
        draw();
        finish_();
        CheckBands((std::string(what) + ": immediate").c_str());

        const GLuint list = gen_lists_(1);
        ASSERT_NE(list, 0u) << what << ": glGenLists(1) returned 0";

        clear_(GL_COLOR_BUFFER_BIT_);
        new_list_(list, GL_COMPILE_);
        draw();
        end_list_();
        finish_();
        CheckNothingDrawn((std::string(what) + ": GL_COMPILE executed the draw").c_str());

        // Both sources are gone by the time the list is called, exactly as
        // MC's tessellator buffer is by the time its chunk list is drawn.
        std::memset(verts.data(), 0, verts.size() * sizeof(GLfloat));
        if (indicesInBuffer) {
            std::vector<T> zeros(indices.size(), T{0});
            buffer_data_(GL_ELEMENT_ARRAY_BUFFER_,
                         static_cast<GLsizeiptr>(zeros.size() * sizeof(T)), zeros.data(),
                         GL_STATIC_DRAW_);
            bind_buffer_(GL_ELEMENT_ARRAY_BUFFER_, 0);
        } else {
            std::memset(indices.data(), 0, indices.size() * sizeof(T));
        }
        disable_client_state_(GL_COLOR_ARRAY_);
        disable_client_state_(GL_VERTEX_ARRAY_);

        clear_(GL_COLOR_BUFFER_BIT_);
        call_list_(list);
        finish_();
        CheckBands((std::string(what) + ": glCallList replay").c_str());

        if (indexBuffer != 0) delete_buffers_(1, &indexBuffer);
        EXPECT_EQ(get_error_(), GL_NO_ERROR_) << what;
    }

    void (*clear_color_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*clear_)(GLbitfield) = nullptr;
    GLenum (*get_error_)() = nullptr;
    void (*finish_)() = nullptr;
    void (*enable_client_state_)(GLenum) = nullptr;
    void (*disable_client_state_)(GLenum) = nullptr;
    void (*vertex_pointer_)(GLint, GLenum, GLsizei, const void*) = nullptr;
    void (*color_pointer_)(GLint, GLenum, GLsizei, const void*) = nullptr;
    void (*draw_elements_)(GLenum, GLsizei, GLenum, const void*) = nullptr;
    void (*draw_range_elements_)(GLenum, GLuint, GLuint, GLsizei, GLenum, const void*) = nullptr;
    void (*multi_draw_elements_)(GLenum, const GLsizei*, GLenum, const void* const*,
                                 GLsizei) = nullptr;
    void (*gen_buffers_)(GLsizei, GLuint*) = nullptr;
    void (*bind_buffer_)(GLenum, GLuint) = nullptr;
    void (*buffer_data_)(GLenum, GLsizeiptr, const void*, GLenum) = nullptr;
    void (*delete_buffers_)(GLsizei, const GLuint*) = nullptr;
    GLuint (*gen_lists_)(GLsizei) = nullptr;
    void (*new_list_)(GLuint, GLenum) = nullptr;
    void (*end_list_)() = nullptr;
    void (*call_list_)(GLuint) = nullptr;
    std::optional<PixelProbe> probe_;
};

TEST_F(DrawElementsDisplayListTest, ClientIndicesUnsignedByte) {
    RunEquivalence<uint8_t>("client GL_UNSIGNED_BYTE", GL_TRIANGLES_, GL_UNSIGNED_BYTE_, false, 0,
                            Form::Plain);
}

TEST_F(DrawElementsDisplayListTest, ClientIndicesUnsignedShort) {
    RunEquivalence<uint16_t>("client GL_UNSIGNED_SHORT", GL_TRIANGLES_, GL_UNSIGNED_SHORT_, false,
                             0, Form::Plain);
}

TEST_F(DrawElementsDisplayListTest, ClientIndicesUnsignedInt) {
    RunEquivalence<uint32_t>("client GL_UNSIGNED_INT", GL_TRIANGLES_, GL_UNSIGNED_INT_, false, 0,
                             Form::Plain);
}

// Indices in an element buffer: the capture has to read them back through the
// binding that was live at draw time, not treat the offset as a pointer.
TEST_F(DrawElementsDisplayListTest, BufferIndicesUnsignedShort) {
    RunEquivalence<uint16_t>("VBO GL_UNSIGNED_SHORT", GL_TRIANGLES_, GL_UNSIGNED_SHORT_, true, 0,
                             Form::Plain);
}

TEST_F(DrawElementsDisplayListTest, BufferIndicesUnsignedByte) {
    RunEquivalence<uint8_t>("VBO GL_UNSIGNED_BYTE", GL_TRIANGLES_, GL_UNSIGNED_BYTE_, true, 0,
                            Form::Plain);
}

TEST_F(DrawElementsDisplayListTest, BufferIndicesUnsignedInt) {
    RunEquivalence<uint32_t>("VBO GL_UNSIGNED_INT", GL_TRIANGLES_, GL_UNSIGNED_INT_, true, 0,
                             Form::Plain);
}

// Nothing references vertices 0..11, so the capture must pack the block from
// the smallest index it actually sees and rebase the indices onto it.
TEST_F(DrawElementsDisplayListTest, IndicesAboveZeroAreRebased) {
    RunEquivalence<uint16_t>("base vertex 12", GL_TRIANGLES_, GL_UNSIGNED_SHORT_, false, 12,
                             Form::Plain);
}

// GL_QUADS has no GLES equivalent: the immediate path rewrites its indices to
// triangles, and the capture has to reach the same picture.
TEST_F(DrawElementsDisplayListTest, QuadsClientIndices) {
    RunEquivalence<uint16_t>("client GL_QUADS", GL_QUADS_, GL_UNSIGNED_SHORT_, false, 0,
                             Form::Plain);
}

TEST_F(DrawElementsDisplayListTest, QuadsBufferIndices) {
    RunEquivalence<uint16_t>("VBO GL_QUADS", GL_QUADS_, GL_UNSIGNED_SHORT_, true, 0, Form::Plain);
}

// glDrawRangeElements records the same way; start/end are a promise about the
// index values, not something the list has to carry.
TEST_F(DrawElementsDisplayListTest, RangeElementsClientIndices) {
    RunEquivalence<uint16_t>("glDrawRangeElements", GL_TRIANGLES_, GL_UNSIGNED_SHORT_, false, 12,
                             Form::Range);
}

TEST_F(DrawElementsDisplayListTest, RangeElementsBufferIndices) {
    RunEquivalence<uint16_t>("glDrawRangeElements VBO", GL_TRIANGLES_, GL_UNSIGNED_SHORT_, true, 0,
                             Form::Range);
}

// glMultiDrawElements records one captured command per sub-draw (plans/15
// 15.4), so all three bands have to survive - a gate that records only the
// call's first slice passes every other check in this file.
TEST_F(DrawElementsDisplayListTest, MultiDrawElementsClientIndices) {
    RunEquivalence<uint16_t>("glMultiDrawElements client", GL_TRIANGLES_, GL_UNSIGNED_SHORT_,
                             false, 0, Form::Multi);
}

TEST_F(DrawElementsDisplayListTest, MultiDrawElementsBufferIndices) {
    RunEquivalence<uint16_t>("glMultiDrawElements VBO", GL_TRIANGLES_, GL_UNSIGNED_SHORT_, true, 0,
                             Form::Multi);
}

TEST_F(DrawElementsDisplayListTest, MultiDrawElementsUnsignedInt) {
    RunEquivalence<uint32_t>("glMultiDrawElements GL_UNSIGNED_INT", GL_TRIANGLES_,
                             GL_UNSIGNED_INT_, false, 0, Form::Multi);
}

// Each sub-draw's indices start at a different point in the list, so a capture
// that rebased them against the whole call's minimum rather than its own would
// draw two of the bands from the wrong corners.
TEST_F(DrawElementsDisplayListTest, MultiDrawElementsQuadsAboveZero) {
    RunEquivalence<uint16_t>("glMultiDrawElements GL_QUADS base 12", GL_QUADS_,
                             GL_UNSIGNED_SHORT_, false, 12, Form::Multi);
}

// GL_COMPILE_AND_EXECUTE must do both, and the executed half must not be the
// thing that got recorded (or a second glCallList would draw nothing).
TEST_F(DrawElementsDisplayListTest, CompileAndExecuteDrawsAndStillRecords) {
    std::vector<GLfloat> verts;
    std::vector<uint16_t> indices;
    FillBands(verts, 0);
    FillIndices(indices, /*quads=*/false, 0);

    PointArraysAt(verts.data());

    const GLuint list = gen_lists_(1);
    ASSERT_NE(list, 0u) << "glGenLists(1) returned 0";

    clear_(GL_COLOR_BUFFER_BIT_);
    new_list_(list, GL_COMPILE_AND_EXECUTE_);
    draw_elements_(GL_TRIANGLES_, static_cast<GLsizei>(indices.size()), GL_UNSIGNED_SHORT_,
                   indices.data());
    end_list_();
    finish_();
    CheckBands("GL_COMPILE_AND_EXECUTE did not execute the glDrawElements");

    std::memset(verts.data(), 0, verts.size() * sizeof(GLfloat));
    std::memset(indices.data(), 0, indices.size() * sizeof(uint16_t));
    clear_(GL_COLOR_BUFFER_BIT_);
    call_list_(list);
    finish_();
    CheckBands("GL_COMPILE_AND_EXECUTE did not record the glDrawElements");

    disable_client_state_(GL_COLOR_ARRAY_);
    disable_client_state_(GL_VERTEX_ARRAY_);
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

} // namespace
