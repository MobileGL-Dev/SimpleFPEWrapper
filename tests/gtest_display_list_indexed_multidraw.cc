// SimpleFPEWrapper - tests/gtest_display_list_indexed_multidraw.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// The three-way equivalence plans/06's exit criterion 1 asks for - immediate,
// GL_COMPILE + glCallList, GL_COMPILE_AND_EXECUTE - over the five entry points
// plans/15 taught to record: glDrawElements, glDrawRangeElements,
// glMultiDrawElements, glMultiDrawArrays and glArrayElement. All five used to
// execute at GL_COMPILE time and leave the list empty, so glCallList replayed
// nothing and glGetError stayed clean about it.
//
// The sibling suites (gtest_drawelements_dlist.cc, gtest_multidraw_dlist.cc,
// gtest_arrayelement_dlist.cc) check those entry points a band at a time. This
// one is the criterion's stricter form, and the reason it is worth a file of
// its own: whole-framebuffer equality against the immediate reference, plus a
// GL state snapshot either side of each path. A replay that paints the right
// picture but hands the application back a different array binding, current
// color or element-buffer binding than the immediate call left is a failure
// here even though every band probe would pass.
//
// GL 2.1 5.4: a vertex-array draw is compiled by dereferencing its arrays -
// and its indices - at COMPILE time. Every case therefore destroys both source
// arrays between glEndList and glCallList, so a list that kept the caller's
// pointers draws nothing rather than accidentally passing.

#include "sfpew_gtest.h"

#include <algorithm>
#include <cstdint>
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
using sfpew_test::GLsizeiptr;
using sfpew_test::GLubyte;
using sfpew_test::GLuint;

constexpr GLbitfield GL_COLOR_BUFFER_BIT_ = 0x00004000;
constexpr GLenum GL_TRIANGLES_ = 0x0004;
constexpr GLenum GL_QUADS_ = 0x0007;
constexpr GLenum GL_FLOAT_ = 0x1406;
constexpr GLenum GL_RGBA_ = 0x1908;
constexpr GLenum GL_UNSIGNED_BYTE_ = 0x1401;
constexpr GLenum GL_UNSIGNED_SHORT_ = 0x1403;
constexpr GLenum GL_UNSIGNED_INT_ = 0x1405;
constexpr GLenum GL_ELEMENT_ARRAY_BUFFER_ = 0x8893;
constexpr GLenum GL_STATIC_DRAW_ = 0x88E4;
constexpr GLenum GL_COMPILE_ = 0x1300;
constexpr GLenum GL_COMPILE_AND_EXECUTE_ = 0x1301;
constexpr GLenum GL_NO_ERROR_ = 0;

constexpr GLenum GL_VERTEX_ARRAY_ = 0x8074;
constexpr GLenum GL_NORMAL_ARRAY_ = 0x8075;
constexpr GLenum GL_COLOR_ARRAY_ = 0x8076;
constexpr GLenum GL_TEXTURE_COORD_ARRAY_ = 0x8078;
constexpr GLenum GL_VERTEX_ARRAY_SIZE_ = 0x807A;
constexpr GLenum GL_VERTEX_ARRAY_TYPE_ = 0x807B;
constexpr GLenum GL_VERTEX_ARRAY_STRIDE_ = 0x807C;
constexpr GLenum GL_COLOR_ARRAY_SIZE_ = 0x8081;
constexpr GLenum GL_COLOR_ARRAY_TYPE_ = 0x8082;
constexpr GLenum GL_COLOR_ARRAY_STRIDE_ = 0x8083;
constexpr GLenum GL_VERTEX_ARRAY_POINTER_ = 0x808E;
constexpr GLenum GL_COLOR_ARRAY_POINTER_ = 0x8090;
constexpr GLenum GL_ARRAY_BUFFER_BINDING_ = 0x8894;
constexpr GLenum GL_ELEMENT_ARRAY_BUFFER_BINDING_ = 0x8895;
constexpr GLenum GL_VERTEX_ARRAY_BUFFER_BINDING_ = 0x8896;
constexpr GLenum GL_COLOR_ARRAY_BUFFER_BINDING_ = 0x8898;
constexpr GLenum GL_VERTEX_ARRAY_BINDING_ = 0x85B5;
constexpr GLenum GL_CURRENT_PROGRAM_ = 0x8B8D;
constexpr GLenum GL_ACTIVE_TEXTURE_ = 0x84E0;
constexpr GLenum GL_CLIENT_ACTIVE_TEXTURE_ = 0x84E1;
constexpr GLenum GL_MATRIX_MODE_ = 0x0BA0;
constexpr GLenum GL_LIST_MODE_ = 0x0B30;
constexpr GLenum GL_LIST_BASE_ = 0x0B32;
constexpr GLenum GL_LIST_INDEX_ = 0x0B33;
constexpr GLenum GL_RENDER_MODE_ = 0x0C40;
constexpr GLenum GL_CURRENT_COLOR_ = 0x0B00;
constexpr GLenum GL_CURRENT_NORMAL_ = 0x0B02;
constexpr GLenum GL_CURRENT_TEXTURE_COORDS_ = 0x0B03;
constexpr GLenum GL_MODELVIEW_MATRIX_ = 0x0BA6;
constexpr GLenum GL_PROJECTION_MATRIX_ = 0x0BA7;

// Three vertical bands, each its own primary color, four distinct corners
// each. A replay that loses a sub-draw leaves a band the clear color; one that
// rebases indices against the wrong base paints a band from another band's
// corners, which the per-band color probes catch.
constexpr int kBands = 3;
constexpr int kFloatsPerVertex = 5; // x, y, r, g, b
constexpr GLsizei kVertexStride = kFloatsPerVertex * static_cast<GLsizei>(sizeof(GLfloat));
constexpr GLfloat kBandColors[kBands][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};

// Not black: "nothing was drawn while GL_COMPILE recorded" is then an equality
// against the cleared framebuffer rather than a brightness threshold, so a
// draw that leaked through cannot hide in the dark end of one.
constexpr GLfloat kClearRgba[4] = {0.2f, 0.1f, 0.3f, 1.0f};

// Which recorded entry point a case drives.
enum class Entry { DrawElements, DrawRangeElements, MultiDrawElements, MultiDrawArrays, ArrayElement };

// The scalar state a capture/replay could plausibly disturb: the app's
// bindings, the client array descriptions the replay has to install and put
// back, and the display-list bookkeeping itself.
struct IntegerState {
    GLenum pname;
    const char* name;
};
constexpr IntegerState kIntegerState[] = {
    {GL_ARRAY_BUFFER_BINDING_, "GL_ARRAY_BUFFER_BINDING"},
    {GL_ELEMENT_ARRAY_BUFFER_BINDING_, "GL_ELEMENT_ARRAY_BUFFER_BINDING"},
    {GL_VERTEX_ARRAY_BINDING_, "GL_VERTEX_ARRAY_BINDING"},
    {GL_CURRENT_PROGRAM_, "GL_CURRENT_PROGRAM"},
    {GL_VERTEX_ARRAY_, "GL_VERTEX_ARRAY"},
    {GL_VERTEX_ARRAY_SIZE_, "GL_VERTEX_ARRAY_SIZE"},
    {GL_VERTEX_ARRAY_TYPE_, "GL_VERTEX_ARRAY_TYPE"},
    {GL_VERTEX_ARRAY_STRIDE_, "GL_VERTEX_ARRAY_STRIDE"},
    {GL_VERTEX_ARRAY_BUFFER_BINDING_, "GL_VERTEX_ARRAY_BUFFER_BINDING"},
    {GL_COLOR_ARRAY_, "GL_COLOR_ARRAY"},
    {GL_COLOR_ARRAY_SIZE_, "GL_COLOR_ARRAY_SIZE"},
    {GL_COLOR_ARRAY_TYPE_, "GL_COLOR_ARRAY_TYPE"},
    {GL_COLOR_ARRAY_STRIDE_, "GL_COLOR_ARRAY_STRIDE"},
    {GL_COLOR_ARRAY_BUFFER_BINDING_, "GL_COLOR_ARRAY_BUFFER_BINDING"},
    {GL_NORMAL_ARRAY_, "GL_NORMAL_ARRAY"},
    {GL_TEXTURE_COORD_ARRAY_, "GL_TEXTURE_COORD_ARRAY"},
    {GL_ACTIVE_TEXTURE_, "GL_ACTIVE_TEXTURE"},
    {GL_CLIENT_ACTIVE_TEXTURE_, "GL_CLIENT_ACTIVE_TEXTURE"},
    {GL_MATRIX_MODE_, "GL_MATRIX_MODE"},
    {GL_RENDER_MODE_, "GL_RENDER_MODE"},
    {GL_LIST_INDEX_, "GL_LIST_INDEX"},
    {GL_LIST_MODE_, "GL_LIST_MODE"},
    {GL_LIST_BASE_, "GL_LIST_BASE"},
};

struct FloatState {
    GLenum pname;
    const char* name;
    int count;
};
constexpr FloatState kFloatState[] = {
    {GL_CURRENT_COLOR_, "GL_CURRENT_COLOR", 4},
    {GL_CURRENT_NORMAL_, "GL_CURRENT_NORMAL", 3},
    {GL_CURRENT_TEXTURE_COORDS_, "GL_CURRENT_TEXTURE_COORDS", 4},
    {GL_MODELVIEW_MATRIX_, "GL_MODELVIEW_MATRIX", 16},
    {GL_PROJECTION_MATRIX_, "GL_PROJECTION_MATRIX", 16},
};

constexpr IntegerState kPointerState[] = {
    {GL_VERTEX_ARRAY_POINTER_, "GL_VERTEX_ARRAY_POINTER"},
    {GL_COLOR_ARRAY_POINTER_, "GL_COLOR_ARRAY_POINTER"},
};

struct Snapshot {
    std::vector<GLint> integers;
    std::vector<GLfloat> floats;
    std::vector<const void*> pointers;
};

class DisplayListIndexedMultidrawTest : public ContextTest {
protected:
    void SetUp() override {
        ContextTest::SetUp();
        if (::testing::Test::IsSkipped()) return;

        clear_color_ = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glClearColor");
        clear_ = Get<void (*)(GLbitfield)>("glClear");
        finish_ = Get<void (*)()>("glFinish");
        get_error_ = Get<GLenum (*)()>("glGetError");
        get_integerv_ = Get<void (*)(GLenum, GLint*)>("glGetIntegerv");
        get_floatv_ = Get<void (*)(GLenum, GLfloat*)>("glGetFloatv");
        get_pointerv_ = Get<void (*)(GLenum, void**)>("glGetPointerv");
        read_pixels_ =
            Get<void (*)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*)>("glReadPixels");
        enable_client_state_ = Get<void (*)(GLenum)>("glEnableClientState");
        disable_client_state_ = Get<void (*)(GLenum)>("glDisableClientState");
        vertex_pointer_ = Get<void (*)(GLint, GLenum, GLsizei, const void*)>("glVertexPointer");
        color_pointer_ = Get<void (*)(GLint, GLenum, GLsizei, const void*)>("glColorPointer");
        color4f_ = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glColor4f");
        normal3f_ = Get<void (*)(GLfloat, GLfloat, GLfloat)>("glNormal3f");
        tex_coord4f_ = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glTexCoord4f");
        begin_ = Get<void (*)(GLenum)>("glBegin");
        end_ = Get<void (*)()>("glEnd");
        array_element_ = Get<void (*)(GLint)>("glArrayElement");
        draw_elements_ = Get<void (*)(GLenum, GLsizei, GLenum, const void*)>("glDrawElements");
        draw_range_elements_ = Get<void (*)(GLenum, GLuint, GLuint, GLsizei, GLenum, const void*)>(
            "glDrawRangeElements");
        multi_draw_arrays_ =
            Get<void (*)(GLenum, const GLint*, const GLsizei*, GLsizei)>("glMultiDrawArrays");
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
        ASSERT_NE(read_pixels_, nullptr);

        clear_color_(kClearRgba[0], kClearRgba[1], kClearRgba[2], kClearRgba[3]);
        get_error_();
    }

    void TearDown() override {
        if (index_buffer_ != 0 && delete_buffers_ != nullptr) {
            bind_buffer_(GL_ELEMENT_ARRAY_BUFFER_, 0);
            delete_buffers_(1, &index_buffer_);
        }
        ContextTest::TearDown();
    }

    // ---- scene ----------------------------------------------------------

    // `cornerOrder` picks which of the band's four corners each written vertex
    // is, so the indexed layouts get four distinct corners and the
    // glMultiDrawArrays layout gets the same corners already in draw order.
    // `leading` unreferenced vertices sit in front, to push the smallest index
    // (or first[]) a case uses well above zero.
    void BuildVertices(int leading, int perBand, const int* cornerOrder) {
        // Rebuilt in place when the size is unchanged: GL_VERTEX_ARRAY_POINTER
        // is part of the state snapshot, so a reallocation between the paths
        // would read as a state difference the draw never caused.
        const size_t floats =
            static_cast<size_t>(leading + kBands * perBand) * kFloatsPerVertex;
        if (vertices_.size() != floats)
            vertices_.assign(floats, 0.0f);
        else
            std::fill(vertices_.begin(), vertices_.end(), 0.0f);
        size_t w = static_cast<size_t>(leading) * kFloatsPerVertex;
        for (int band = 0; band < kBands; ++band) {
            const GLfloat x0 = -1.0f + 2.0f * static_cast<GLfloat>(band) / kBands;
            const GLfloat x1 = -1.0f + 2.0f * static_cast<GLfloat>(band + 1) / kBands;
            const GLfloat xs[4] = {x0, x1, x1, x0};
            const GLfloat ys[4] = {-1.0f, -1.0f, 1.0f, 1.0f};
            for (int v = 0; v < perBand; ++v) {
                vertices_[w++] = xs[cornerOrder[v]];
                vertices_[w++] = ys[cornerOrder[v]];
                vertices_[w++] = kBandColors[band][0];
                vertices_[w++] = kBandColors[band][1];
                vertices_[w++] = kBandColors[band][2];
            }
        }
    }

    template <typename T>
    static void AppendRaw(std::vector<GLubyte>& out, T value) {
        const GLubyte* bytes = reinterpret_cast<const GLubyte*>(&value);
        out.insert(out.end(), bytes, bytes + sizeof(T));
    }

    void AppendIndex(std::vector<GLubyte>& out, GLuint value) const {
        switch (index_type_) {
        case GL_UNSIGNED_BYTE_: AppendRaw(out, static_cast<uint8_t>(value)); break;
        case GL_UNSIGNED_INT_: AppendRaw(out, static_cast<uint32_t>(value)); break;
        default: AppendRaw(out, static_cast<uint16_t>(value)); break;
        }
    }

    GLuint IndexAt(int slot) const {
        const size_t offset = static_cast<size_t>(slot) * index_size_;
        switch (index_type_) {
        case GL_UNSIGNED_BYTE_: return indices_[offset];
        case GL_UNSIGNED_INT_: {
            uint32_t value = 0;
            std::memcpy(&value, indices_.data() + offset, sizeof value);
            return value;
        }
        default: {
            uint16_t value = 0;
            std::memcpy(&value, indices_.data() + offset, sizeof value);
            return value;
        }
        }
    }

    void BuildIndices(bool quads, int leading) {
        static const int quadCorners[4] = {0, 1, 2, 3};
        static const int triangleCorners[6] = {0, 1, 2, 0, 2, 3};
        const int* corners = quads ? quadCorners : triangleCorners;
        const int perBand = quads ? 4 : 6;
        indices_.clear();
        for (int band = 0; band < kBands; ++band)
            for (int i = 0; i < perBand; ++i)
                AppendIndex(indices_, static_cast<GLuint>(leading + band * 4 + corners[i]));
        index_count_ = kBands * perBand;
        sub_count_ = perBand;
    }

    void UploadIndices() {
        bind_buffer_(GL_ELEMENT_ARRAY_BUFFER_, index_buffer_);
        buffer_data_(GL_ELEMENT_ARRAY_BUFFER_, static_cast<GLsizeiptr>(indices_.size()),
                     indices_.data(), GL_STATIC_DRAW_);
    }

    const void* IndexArgument() const {
        return index_buffer_ != 0 ? nullptr : static_cast<const void*>(indices_.data());
    }

    void PrepareSubDraws() {
        for (int band = 0; band < kBands; ++band) {
            sub_counts_[band] = sub_count_;
            sub_indices_[band] = reinterpret_cast<const void*>(
                reinterpret_cast<uintptr_t>(IndexArgument()) +
                static_cast<uintptr_t>(band) * static_cast<uintptr_t>(sub_count_) * index_size_);
        }
    }

    void PointArrays() {
        vertex_pointer_(2, GL_FLOAT_, kVertexStride, vertices_.data());
        color_pointer_(3, GL_FLOAT_, kVertexStride, vertices_.data() + 2);
        enable_client_state_(GL_VERTEX_ARRAY_);
        enable_client_state_(GL_COLOR_ARRAY_);
    }

    // Everything the compiled list may not still be reading: the vertex data,
    // the indices, the bindings that named them and the enables that made them
    // count. What is left is the state a real application has by the time it
    // calls a chunk list built frames ago.
    void ScrambleSources() {
        std::memset(vertices_.data(), 0, vertices_.size() * sizeof(GLfloat));
        if (index_buffer_ != 0) {
            const std::vector<GLubyte> zeros(indices_.size(), 0);
            buffer_data_(GL_ELEMENT_ARRAY_BUFFER_, static_cast<GLsizeiptr>(zeros.size()),
                         zeros.data(), GL_STATIC_DRAW_);
            bind_buffer_(GL_ELEMENT_ARRAY_BUFFER_, 0);
        }
        if (!indices_.empty()) std::memset(indices_.data(), 0, indices_.size());
        disable_client_state_(GL_COLOR_ARRAY_);
        disable_client_state_(GL_VERTEX_ARRAY_);
    }

    void RestoreSources(int leading, int perBand, const int* cornerOrder, bool quads,
                        bool indexed) {
        BuildVertices(leading, perBand, cornerOrder);
        if (indexed) {
            BuildIndices(quads, leading);
            if (index_buffer_ != 0) UploadIndices();
            PrepareSubDraws();
        }
        PointArrays();
    }

    void Draw(Entry entry, GLenum mode) {
        switch (entry) {
        case Entry::DrawElements:
            draw_elements_(mode, index_count_, index_type_, IndexArgument());
            break;
        case Entry::DrawRangeElements:
            draw_range_elements_(mode, lowest_, highest_, index_count_, index_type_,
                                 IndexArgument());
            break;
        case Entry::MultiDrawElements:
            multi_draw_elements_(mode, sub_counts_, index_type_, sub_indices_, kBands);
            break;
        case Entry::MultiDrawArrays:
            multi_draw_arrays_(mode, sub_firsts_, sub_counts_, kBands);
            break;
        case Entry::ArrayElement:
            begin_(mode);
            for (int slot = 0; slot < index_count_; ++slot)
                array_element_(static_cast<GLint>(IndexAt(slot)));
            end_();
            break;
        }
    }

    // ---- framebuffer ----------------------------------------------------

    void Clear() { clear_(GL_COLOR_BUFFER_BIT_); }

    void Capture(std::vector<GLubyte>* out) {
        out->assign(static_cast<size_t>(size()) * size() * 4, 0);
        finish_();
        read_pixels_(0, 0, size(), size(), GL_RGBA_, GL_UNSIGNED_BYTE_, out->data());
    }

    static size_t DifferingPixels(const std::vector<GLubyte>& a, const std::vector<GLubyte>& b) {
        size_t differing = 0;
        for (size_t p = 0; p * 4 < a.size(); ++p)
            if (std::memcmp(a.data() + p * 4, b.data() + p * 4, 4) != 0) ++differing;
        return differing;
    }

    void ExpectSameImage(const std::vector<GLubyte>& got, const std::vector<GLubyte>& want,
                         const std::string& what) {
        const size_t differing = DifferingPixels(got, want);
        EXPECT_EQ(differing, 0u) << what << ": " << differing << " of "
                                 << (want.size() / 4) << " pixels differ from the reference";
    }

    // The immediate picture has to be RIGHT, not merely reproducible - three
    // identically broken framebuffers would compare equal all day.
    void CheckBandColors(const std::vector<GLubyte>& image, const std::string& what) {
        static const int xs[kBands] = {10, 32, 54};
        for (int band = 0; band < kBands; ++band) {
            const size_t p = (static_cast<size_t>(size() / 2) * size() +
                              static_cast<size_t>(xs[band])) * 4;
            const int r = image[p], g = image[p + 1], b = image[p + 2];
            EXPECT_TRUE((r > 200) == (kBandColors[band][0] > 0) &&
                        (g > 200) == (kBandColors[band][1] > 0) &&
                        (b > 200) == (kBandColors[band][2] > 0))
                << what << ": band " << band << " at (" << xs[band] << ',' << (size() / 2)
                << ") = (" << r << ',' << g << ',' << b << "), expected ("
                << kBandColors[band][0] << ',' << kBandColors[band][1] << ','
                << kBandColors[band][2] << ')';
        }
    }

    // ---- state ----------------------------------------------------------

    Snapshot TakeSnapshot() {
        Snapshot snapshot;
        for (const IntegerState& state : kIntegerState) {
            GLint value = -1;
            get_integerv_(state.pname, &value);
            snapshot.integers.push_back(value);
        }
        for (const FloatState& state : kFloatState) {
            GLfloat values[16] = {};
            get_floatv_(state.pname, values);
            for (int i = 0; i < state.count; ++i) snapshot.floats.push_back(values[i]);
        }
        for (const IntegerState& state : kPointerState) {
            void* value = nullptr;
            get_pointerv_(state.pname, &value);
            snapshot.pointers.push_back(value);
        }
        return snapshot;
    }

    void ExpectSameState(const Snapshot& got, const Snapshot& want, const std::string& what) {
        for (size_t i = 0; i < want.integers.size(); ++i)
            EXPECT_EQ(got.integers[i], want.integers[i])
                << what << ": " << kIntegerState[i].name << " is " << got.integers[i]
                << ", the immediate path left " << want.integers[i];
        size_t at = 0;
        for (const FloatState& state : kFloatState) {
            for (int i = 0; i < state.count; ++i, ++at)
                EXPECT_EQ(got.floats[at], want.floats[at])
                    << what << ": " << state.name << '[' << i << "] is " << got.floats[at]
                    << ", the immediate path left " << want.floats[at];
        }
        for (size_t i = 0; i < want.pointers.size(); ++i)
            EXPECT_EQ(got.pointers[i], want.pointers[i])
                << what << ": " << kPointerState[i].name << " differs from the immediate path";
    }

    // The current-vertex state each path is measured from. Without it a draw
    // that never touches the current color would compare equal to one that
    // sets it, because both would read back whatever the previous path left.
    void ResetCurrentState() {
        color4f_(0.25f, 0.5f, 0.75f, 0.5f);
        normal3f_(0.0f, 0.0f, 1.0f);
        tex_coord4f_(0.0f, 0.0f, 0.0f, 1.0f);
    }

    void ExpectNoError(const std::string& what) {
        EXPECT_EQ(get_error_(), GL_NO_ERROR_) << what << ": GL error raised";
    }

    // ---- the three-way equivalence --------------------------------------

    void RunEquivalence(const std::string& what, Entry entry, GLenum mode, GLenum indexType,
                        bool indicesInBuffer, int leading) {
        const bool quads = mode == GL_QUADS_;
        const bool indexed = entry != Entry::MultiDrawArrays;
        static const int quadCorners[4] = {0, 1, 2, 3};
        static const int triangleCorners[6] = {0, 1, 2, 0, 2, 3};
        // The indexed layouts address four distinct corners; the array layout
        // has to spell out the draw order it will hand to first[]/count[].
        const int* cornerOrder = (indexed || quads) ? quadCorners : triangleCorners;
        const int perBand = (indexed || quads) ? 4 : 6;

        index_type_ = indexType;
        index_size_ = indexType == GL_UNSIGNED_BYTE_ ? 1 : (indexType == GL_UNSIGNED_INT_ ? 4 : 2);
        lowest_ = static_cast<GLuint>(leading);
        highest_ = static_cast<GLuint>(leading + kBands * 4 - 1);
        if (indicesInBuffer) {
            gen_buffers_(1, &index_buffer_);
            ASSERT_NE(index_buffer_, 0u) << what << ": glGenBuffers returned 0";
        }
        BuildVertices(leading, perBand, cornerOrder);
        if (indexed) {
            BuildIndices(quads, leading);
            if (indicesInBuffer) UploadIndices();
            PrepareSubDraws();
        } else {
            sub_count_ = perBand;
            for (int band = 0; band < kBands; ++band) {
                sub_counts_[band] = perBand;
                sub_firsts_[band] = leading + band * perBand;
            }
        }
        PointArrays();

        std::vector<GLubyte> blank, immediate, actual;
        Clear();
        Capture(&blank);

        // (a) immediate execution: the reference every other path is measured
        // against, and the only one whose colors are checked outright.
        ResetCurrentState();
        Clear();
        Draw(entry, mode);
        Capture(&immediate);
        CheckBandColors(immediate, what + ": immediate");
        ASSERT_GT(DifferingPixels(immediate, blank), 0u)
            << what << ": the immediate draw painted nothing; nothing to compare against";
        const Snapshot immediateState = TakeSnapshot();
        ExpectNoError(what + ": immediate");

        // (b) GL_COMPILE: recording renders nothing at all.
        const GLuint compiled = gen_lists_(1);
        ASSERT_NE(compiled, 0u) << what << ": glGenLists(1) returned 0";
        ResetCurrentState();
        Clear();
        new_list_(compiled, GL_COMPILE_);
        Draw(entry, mode);
        end_list_();
        Capture(&actual);
        ExpectSameImage(actual, blank, what + ": GL_COMPILE rendered while recording");
        ExpectNoError(what + ": GL_COMPILE");

        // ... and replaying it reproduces the immediate picture and the state
        // the immediate call left behind.
        ResetCurrentState();
        Clear();
        call_list_(compiled);
        Capture(&actual);
        ExpectSameImage(actual, immediate, what + ": glCallList replay");
        ExpectSameState(TakeSnapshot(), immediateState, what + ": glCallList replay");
        ExpectNoError(what + ": glCallList");

        // GL 2.1 5.4: the list holds its own copy, so destroying both sources
        // must not change what it draws.
        ScrambleSources();
        ResetCurrentState();
        Clear();
        call_list_(compiled);
        Capture(&actual);
        ExpectSameImage(actual, immediate,
                        what + ": glCallList after the source arrays were destroyed");
        ExpectNoError(what + ": glCallList after scrambling");

        // (c) GL_COMPILE_AND_EXECUTE draws as it records ...
        RestoreSources(leading, perBand, cornerOrder, quads, indexed);
        const GLuint both = gen_lists_(1);
        ASSERT_NE(both, 0u) << what << ": glGenLists(1) returned 0";
        ResetCurrentState();
        Clear();
        new_list_(both, GL_COMPILE_AND_EXECUTE_);
        Draw(entry, mode);
        end_list_();
        Capture(&actual);
        ExpectSameImage(actual, immediate, what + ": GL_COMPILE_AND_EXECUTE did not draw");
        ExpectSameState(TakeSnapshot(), immediateState, what + ": GL_COMPILE_AND_EXECUTE");
        ExpectNoError(what + ": GL_COMPILE_AND_EXECUTE");

        // ... without having consumed what it recorded.
        ScrambleSources();
        ResetCurrentState();
        Clear();
        call_list_(both);
        Capture(&actual);
        ExpectSameImage(actual, immediate,
                        what + ": the GL_COMPILE_AND_EXECUTE list does not replay");
        ExpectNoError(what + ": GL_COMPILE_AND_EXECUTE replay");
    }

    std::vector<GLfloat> vertices_;
    std::vector<GLubyte> indices_;
    GLenum index_type_ = GL_UNSIGNED_SHORT_;
    size_t index_size_ = 2;
    int index_count_ = 0;
    int sub_count_ = 0;
    GLsizei sub_counts_[kBands] = {};
    GLint sub_firsts_[kBands] = {};
    const void* sub_indices_[kBands] = {};
    GLuint index_buffer_ = 0;
    GLuint lowest_ = 0, highest_ = 0;

    void (*clear_color_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*clear_)(GLbitfield) = nullptr;
    void (*finish_)() = nullptr;
    GLenum (*get_error_)() = nullptr;
    void (*get_integerv_)(GLenum, GLint*) = nullptr;
    void (*get_floatv_)(GLenum, GLfloat*) = nullptr;
    void (*get_pointerv_)(GLenum, void**) = nullptr;
    void (*read_pixels_)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*) = nullptr;
    void (*enable_client_state_)(GLenum) = nullptr;
    void (*disable_client_state_)(GLenum) = nullptr;
    void (*vertex_pointer_)(GLint, GLenum, GLsizei, const void*) = nullptr;
    void (*color_pointer_)(GLint, GLenum, GLsizei, const void*) = nullptr;
    void (*color4f_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*normal3f_)(GLfloat, GLfloat, GLfloat) = nullptr;
    void (*tex_coord4f_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*begin_)(GLenum) = nullptr;
    void (*end_)() = nullptr;
    void (*array_element_)(GLint) = nullptr;
    void (*draw_elements_)(GLenum, GLsizei, GLenum, const void*) = nullptr;
    void (*draw_range_elements_)(GLenum, GLuint, GLuint, GLsizei, GLenum, const void*) = nullptr;
    void (*multi_draw_arrays_)(GLenum, const GLint*, const GLsizei*, GLsizei) = nullptr;
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
};

// ---- glDrawElements -----------------------------------------------------
// All three index widths from both sources: client memory and an element
// buffer are different code paths in the capture, and the narrowest type is
// the one a rebase can overflow.

TEST_F(DisplayListIndexedMultidrawTest, DrawElementsClientUnsignedByte) {
    RunEquivalence("glDrawElements client GL_UNSIGNED_BYTE", Entry::DrawElements, GL_TRIANGLES_,
                   GL_UNSIGNED_BYTE_, false, 0);
}

TEST_F(DisplayListIndexedMultidrawTest, DrawElementsClientUnsignedShort) {
    RunEquivalence("glDrawElements client GL_UNSIGNED_SHORT", Entry::DrawElements, GL_TRIANGLES_,
                   GL_UNSIGNED_SHORT_, false, 0);
}

TEST_F(DisplayListIndexedMultidrawTest, DrawElementsClientUnsignedInt) {
    RunEquivalence("glDrawElements client GL_UNSIGNED_INT", Entry::DrawElements, GL_TRIANGLES_,
                   GL_UNSIGNED_INT_, false, 0);
}

TEST_F(DisplayListIndexedMultidrawTest, DrawElementsBufferUnsignedByte) {
    RunEquivalence("glDrawElements VBO GL_UNSIGNED_BYTE", Entry::DrawElements, GL_TRIANGLES_,
                   GL_UNSIGNED_BYTE_, true, 0);
}

TEST_F(DisplayListIndexedMultidrawTest, DrawElementsBufferUnsignedShort) {
    RunEquivalence("glDrawElements VBO GL_UNSIGNED_SHORT", Entry::DrawElements, GL_TRIANGLES_,
                   GL_UNSIGNED_SHORT_, true, 0);
}

TEST_F(DisplayListIndexedMultidrawTest, DrawElementsBufferUnsignedInt) {
    RunEquivalence("glDrawElements VBO GL_UNSIGNED_INT", Entry::DrawElements, GL_TRIANGLES_,
                   GL_UNSIGNED_INT_, true, 0);
}

// Nothing references vertices 0..11, so the capture packs from the smallest
// index it actually sees.
TEST_F(DisplayListIndexedMultidrawTest, DrawElementsIndicesAboveZero) {
    RunEquivalence("glDrawElements base 12", Entry::DrawElements, GL_TRIANGLES_,
                   GL_UNSIGNED_SHORT_, false, 12);
}

// GL_QUADS has no GLES equivalent: the immediate path rewrites the indices to
// triangles, and the replay has to reach the same picture.
TEST_F(DisplayListIndexedMultidrawTest, DrawElementsQuads) {
    RunEquivalence("glDrawElements GL_QUADS", Entry::DrawElements, GL_QUADS_, GL_UNSIGNED_SHORT_,
                   false, 0);
}

// ---- glDrawRangeElements ------------------------------------------------

TEST_F(DisplayListIndexedMultidrawTest, DrawRangeElementsClient) {
    RunEquivalence("glDrawRangeElements client", Entry::DrawRangeElements, GL_TRIANGLES_,
                   GL_UNSIGNED_SHORT_, false, 12);
}

TEST_F(DisplayListIndexedMultidrawTest, DrawRangeElementsBuffer) {
    RunEquivalence("glDrawRangeElements VBO", Entry::DrawRangeElements, GL_TRIANGLES_,
                   GL_UNSIGNED_INT_, true, 0);
}

// ---- glMultiDrawElements ------------------------------------------------
// One captured command per sub-draw, so a gate that records only the call's
// first slice leaves two bands at the clear color.

TEST_F(DisplayListIndexedMultidrawTest, MultiDrawElementsClient) {
    RunEquivalence("glMultiDrawElements client", Entry::MultiDrawElements, GL_TRIANGLES_,
                   GL_UNSIGNED_SHORT_, false, 0);
}

TEST_F(DisplayListIndexedMultidrawTest, MultiDrawElementsBuffer) {
    RunEquivalence("glMultiDrawElements VBO", Entry::MultiDrawElements, GL_TRIANGLES_,
                   GL_UNSIGNED_SHORT_, true, 0);
}

TEST_F(DisplayListIndexedMultidrawTest, MultiDrawElementsQuadsAboveZero) {
    RunEquivalence("glMultiDrawElements GL_QUADS base 12", Entry::MultiDrawElements, GL_QUADS_,
                   GL_UNSIGNED_SHORT_, false, 12);
}

// ---- glMultiDrawArrays --------------------------------------------------

TEST_F(DisplayListIndexedMultidrawTest, MultiDrawArraysTriangles) {
    RunEquivalence("glMultiDrawArrays GL_TRIANGLES", Entry::MultiDrawArrays, GL_TRIANGLES_,
                   GL_UNSIGNED_SHORT_, false, 0);
}

// The mode glMultiDrawArrays can never hand to the backend: each sub-draw
// becomes its own indexed draw, so the capture meets the quad rewrite.
TEST_F(DisplayListIndexedMultidrawTest, MultiDrawArraysQuads) {
    RunEquivalence("glMultiDrawArrays GL_QUADS", Entry::MultiDrawArrays, GL_QUADS_,
                   GL_UNSIGNED_SHORT_, false, 5);
}

// ---- glArrayElement -----------------------------------------------------
// Recorded as the per-attribute entry points it dispatches through, so the
// state snapshot is the interesting half here: the current color the immediate
// run ends on has to survive into the replay too.

TEST_F(DisplayListIndexedMultidrawTest, ArrayElementTriangles) {
    RunEquivalence("glArrayElement GL_TRIANGLES", Entry::ArrayElement, GL_TRIANGLES_,
                   GL_UNSIGNED_SHORT_, false, 0);
}

TEST_F(DisplayListIndexedMultidrawTest, ArrayElementQuadsAboveZero) {
    RunEquivalence("glArrayElement GL_QUADS base 12", Entry::ArrayElement, GL_QUADS_,
                   GL_UNSIGNED_SHORT_, false, 12);
}

} // namespace
