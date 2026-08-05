// SimpleFPEWrapper - tests/gtest_client_buffer_classification.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// plans/13 13.2/13.3: commit_fpe_state_on_draw used to decide whether a
// gl*Pointer's `pointer` argument was a real client-memory address or a byte
// offset into the bound GL_ARRAY_BUFFER by comparing its numeric magnitude
// against the stride - a byte offset that happened to exceed the stride (any
// sub-allocated/shared VBO where the app's geometry does not start at byte 0)
// was dereferenced as if it were a real pointer. classifyClientArrays()
// replaces the guess with client_array_buffer_bindings[], the binding
// actually captured at the gl*Pointer call.
//
// These reproduce the two crashes/miscompiles from the plans/13 audit that
// this phase (13.2/13.3) fixes:
//   1) glBindBuffer(vbo); glVertexPointer(2, GL_FLOAT, stride, (void*)offset)
//      with offset > stride -> gdb showed the offset value used as a pointer
//      and dereferenced (SIGSEGV).
//   4) the pointer is set while a VBO is bound, but the VBO is unbound again
//      before the draw - the data source is decided at the *Pointer call, not
//      at draw time, so the draw must still read the VBO, not fall back to
//      the wrapper's own empty internal buffer.
//
// A later pass extends the exact same fix, through the exact same
// classifyClientArrays(), to the two other call sites that had it:
// sfpewUserProgramFixedFunctionDrawArrays (glDrawArrays with a user program
// bound) and userProgramDrawElements (glDrawElements with a user program
// bound) - see ClientBufferClassificationUserProgramTest below - plus
// drawElementsNow's own, independent `pointer > 1MB` heuristic (13.5,
// DrawElementsNowVboVertexOffsetBeyondOldOneMegabyteThresholdRendersCorrectly),
// which could disagree with classifyClientArrays on the very same draw
// (audit finding #9).
//
// 13.4 (gather_mixed_client_arrays) fixes the remaining two crashes/miscompiles,
// both `mixed` classification (classifyClientArrays sees more than one
// distinct binding value, zero included, across the enabled attributes):
//   2) one attribute genuinely client memory, another VBO-backed, enabled
//      together - the client pointer got treated as a buffer offset (or vice
//      versa), crashing the driver or reading garbage/out-of-bounds.
//   3) two attributes each VBO-backed but from two DIFFERENT buffers - only
//      whichever buffer was bound at draw time got read, so one attribute's
//      data silently came from the wrong buffer.
// See MixedVboPositionAndClientMemoryColorRendersCorrectly (#2) and
// MixedTwoDifferentVbosPerAttributeRendersCorrectly (#3) below.

#include "sfpew_gtest.h"

#include <algorithm>
#include <optional>
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
using sfpew_test::GLushort;
using sfpew_test::PixelProbe;

constexpr GLbitfield GL_COLOR_BUFFER_BIT_ = 0x00004000;
constexpr GLenum GL_TRIANGLES_ = 0x0004;
constexpr GLenum GL_FLOAT_ = 0x1406;
constexpr GLenum GL_ARRAY_BUFFER_ = 0x8892;
constexpr GLenum GL_STATIC_DRAW_ = 0x88E4;
constexpr GLenum GL_VERTEX_ARRAY_ = 0x8074;
constexpr GLenum GL_COLOR_ARRAY_ = 0x8076;
constexpr GLenum GL_NO_ERROR_ = 0;
constexpr GLbitfield GL_CLIENT_VERTEX_ARRAY_BIT_ = 0x00000002;
constexpr GLenum GL_UNSIGNED_SHORT_ = 0x1403;

class ClientBufferClassificationTest : public ContextTest {
protected:
    void SetUp() override {
        ContextTest::SetUp();
        if (::testing::Test::IsSkipped()) return;

        clear_color_ = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glClearColor");
        clear_ = Get<void (*)(GLbitfield)>("glClear");
        draw_arrays_ = Get<void (*)(GLenum, GLint, GLsizei)>("glDrawArrays");
        draw_elements_ = Get<void (*)(GLenum, GLsizei, GLenum, const void*)>("glDrawElements");
        get_error_ = Get<GLenum (*)()>("glGetError");
        finish_ = Get<void (*)()>("glFinish");
        gen_buffers_ = Get<void (*)(GLsizei, GLuint*)>("glGenBuffers");
        bind_buffer_ = Get<void (*)(GLenum, GLuint)>("glBindBuffer");
        buffer_data_ = Get<void (*)(GLenum, GLsizeiptr, const void*, GLenum)>("glBufferData");
        enable_client_state_ = Get<void (*)(GLenum)>("glEnableClientState");
        vertex_pointer_ = Get<void (*)(GLint, GLenum, GLsizei, const void*)>("glVertexPointer");
        color_pointer_ = Get<void (*)(GLint, GLenum, GLsizei, const void*)>("glColorPointer");
        push_client_attrib_ = Get<void (*)(GLbitfield)>("glPushClientAttrib");
        pop_client_attrib_ = Get<void (*)()>("glPopClientAttrib");
        auto read_pixels =
            Get<void (*)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*)>("glReadPixels");
        ASSERT_NE(read_pixels, nullptr);
        probe_.emplace(read_pixels);

        enable_client_state_(GL_VERTEX_ARRAY_);
        enable_client_state_(GL_COLOR_ARRAY_);
        clear_color_(0.0f, 0.0f, 1.0f, 1.0f); // blue: anything un-drawn stays blue
        get_error_();
    }

    void ExpectGreen(int x, int y, const char* what) {
        const PixelProbe::Rgba p = probe_->At(x, y);
        EXPECT_TRUE(p.g > 200 && p.r <= 200 && p.b <= 200)
            << what << ": pixel(" << x << ',' << y << ") = (" << (int)p.r << ',' << (int)p.g
            << ',' << (int)p.b << ')';
    }

    void (*clear_color_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*clear_)(GLbitfield) = nullptr;
    void (*draw_arrays_)(GLenum, GLint, GLsizei) = nullptr;
    void (*draw_elements_)(GLenum, GLsizei, GLenum, const void*) = nullptr;
    GLenum (*get_error_)() = nullptr;
    void (*finish_)() = nullptr;
    void (*gen_buffers_)(GLsizei, GLuint*) = nullptr;
    void (*bind_buffer_)(GLenum, GLuint) = nullptr;
    void (*buffer_data_)(GLenum, GLsizeiptr, const void*, GLenum) = nullptr;
    void (*enable_client_state_)(GLenum) = nullptr;
    void (*vertex_pointer_)(GLint, GLenum, GLsizei, const void*) = nullptr;
    void (*color_pointer_)(GLint, GLenum, GLsizei, const void*) = nullptr;
    void (*push_client_attrib_)(GLbitfield) = nullptr;
    void (*pop_client_attrib_)() = nullptr;
    std::optional<PixelProbe> probe_;
};

// Audit crash 1: a VBO offset numerically bigger than the stride.
TEST_F(ClientBufferClassificationTest, OffsetExceedingStrideReadsTheVboNotACrash) {
    // A 7-float (28-byte) header ahead of the actual geometry, as a
    // sub-allocated/shared VBO would have (unrelated data, or another
    // draw's vertices, occupying the front of the buffer). The per-vertex
    // stride is 24 bytes (2 floats position + 4 floats color), so the
    // position offset (28) is numerically greater than the stride - exactly
    // the shape the old `pointer > stride` heuristic mistook for a real
    // client-memory address instead of a buffer offset.
    static const GLfloat data[] = {
        // 7 junk floats nobody reads
        -9, -9, -9, -9, -9, -9, -9,
        // two full-screen triangles, interleaved x,y,r,g,b,a (all green)
        -1, -1, 0, 1, 0, 1, //
        1,  -1, 0, 1, 0, 1, //
        1,  1,  0, 1, 0, 1, //
        -1, -1, 0, 1, 0, 1, //
        1,  1,  0, 1, 0, 1, //
        -1, 1,  0, 1, 0, 1, //
    };
    constexpr GLsizei kStride = 6 * static_cast<GLsizei>(sizeof(GLfloat));
    constexpr size_t kHeaderBytes = 7 * sizeof(GLfloat);

    GLuint vbo = 0;
    gen_buffers_(1, &vbo);
    ASSERT_NE(vbo, 0u);
    bind_buffer_(GL_ARRAY_BUFFER_, vbo);
    buffer_data_(GL_ARRAY_BUFFER_, static_cast<GLsizeiptr>(sizeof data), data, GL_STATIC_DRAW_);

    vertex_pointer_(2, GL_FLOAT_, kStride, reinterpret_cast<const void*>(kHeaderBytes));
    color_pointer_(4, GL_FLOAT_, kStride,
                   reinterpret_cast<const void*>(kHeaderBytes + 2 * sizeof(GLfloat)));

    clear_(GL_COLOR_BUFFER_BIT_);
    draw_arrays_(GL_TRIANGLES_, 0, 6); // must not crash
    finish_();

    ExpectGreen(32, 32, "offset > stride: center");
    ExpectGreen(6, 6, "offset > stride: corner");
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

// Audit crash 4: the pointer is declared while a VBO is bound, but the VBO
// binding is gone (unbound) by the time the draw actually happens. The data
// source is fixed at the gl*Pointer call, not re-derived from whatever is
// bound at draw time.
TEST_F(ClientBufferClassificationTest, PointerSetWhileBoundSurvivesUnbindingBeforeTheDraw) {
    static const GLfloat verts[] = {
        -1, -1, 0, 1, 0, 1, 1, -1, 0, 1, 0, 1, 1, 1, 0, 1, 0, 1,
        -1, -1, 0, 1, 0, 1, 1, 1,  0, 1, 0, 1, -1, 1, 0, 1, 0, 1,
    };
    constexpr GLsizei kStride = 6 * static_cast<GLsizei>(sizeof(GLfloat));

    GLuint vbo = 0;
    gen_buffers_(1, &vbo);
    ASSERT_NE(vbo, 0u);
    bind_buffer_(GL_ARRAY_BUFFER_, vbo);
    buffer_data_(GL_ARRAY_BUFFER_, static_cast<GLsizeiptr>(sizeof verts), verts, GL_STATIC_DRAW_);

    vertex_pointer_(2, GL_FLOAT_, kStride, nullptr);
    color_pointer_(4, GL_FLOAT_, kStride, reinterpret_cast<const void*>(2 * sizeof(GLfloat)));

    // Unbind BEFORE the draw: GL_ARRAY_BUFFER_BINDING is 0 by the time
    // commit_fpe_state_on_draw runs, but the attribute data still has to
    // come from vbo, captured at the *Pointer calls above.
    bind_buffer_(GL_ARRAY_BUFFER_, 0);

    clear_(GL_COLOR_BUFFER_BIT_);
    draw_arrays_(GL_TRIANGLES_, 0, 6);
    finish_();

    ExpectGreen(32, 32, "pointer set then unbound: center");
    ExpectGreen(6, 6, "pointer set then unbound: corner");
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

// Audit finding #2 (plans/13 13.4): one attribute is real client memory,
// the other a VBO byte offset, both enabled in the same draw - legal GL 1.5.
// classifyClientArrays must resolve `mixed` (not all_client_memory, not
// single_buffer) and gather_mixed_client_arrays must read each attribute
// from its own real source: the client array by memcpy, the VBO-backed one
// via glMapBufferRange, not one guessed source applied to both.
TEST_F(ClientBufferClassificationTest, MixedVboPositionAndClientMemoryColorRendersCorrectly) {
    static const GLfloat positions[] = {
        -1, -1, 1, -1, 1, 1, -1, -1, 1, 1, -1, 1,
    };
    static const GLfloat colors[] = {
        0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1,
    };

    GLuint vboA = 0;
    gen_buffers_(1, &vboA);
    ASSERT_NE(vboA, 0u);
    bind_buffer_(GL_ARRAY_BUFFER_, vboA);
    buffer_data_(GL_ARRAY_BUFFER_, static_cast<GLsizeiptr>(sizeof positions), positions,
                 GL_STATIC_DRAW_);
    vertex_pointer_(2, GL_FLOAT_, 0, nullptr); // offset 0 into vboA

    bind_buffer_(GL_ARRAY_BUFFER_, 0); // colors below must NOT be read as a vboA offset
    color_pointer_(4, GL_FLOAT_, 0, colors); // real heap pointer

    clear_(GL_COLOR_BUFFER_BIT_);
    draw_arrays_(GL_TRIANGLES_, 0, 6); // must not crash
    finish_();

    ExpectGreen(32, 32, "mixed vbo position + client color: center");
    ExpectGreen(6, 6, "mixed vbo position + client color: corner");
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

// Audit finding #3 (plans/13 13.4): both attributes are VBO-backed, but from
// two DIFFERENT buffers - also legal GL 1.5. classifyClientArrays must
// resolve `mixed` (single_buffer requires every enabled attribute to share
// the SAME binding), and each attribute must be read from its own buffer -
// with the old "whichever buffer happens to be bound at draw time" guess,
// this reads (0,1) (the first two floats of vboB's color data) as every
// vertex's position, collapsing the quad to nothing visible at (32,32).
TEST_F(ClientBufferClassificationTest, MixedTwoDifferentVbosPerAttributeRendersCorrectly) {
    static const GLfloat positions[] = {
        -1, -1, 1, -1, 1, 1, -1, -1, 1, 1, -1, 1,
    };
    static const GLfloat colors[] = {
        0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1,
    };

    GLuint vboA = 0, vboB = 0;
    gen_buffers_(1, &vboA);
    gen_buffers_(1, &vboB);
    ASSERT_NE(vboA, 0u);
    ASSERT_NE(vboB, 0u);

    bind_buffer_(GL_ARRAY_BUFFER_, vboA);
    buffer_data_(GL_ARRAY_BUFFER_, static_cast<GLsizeiptr>(sizeof positions), positions,
                 GL_STATIC_DRAW_);
    vertex_pointer_(2, GL_FLOAT_, 0, nullptr); // offset 0 into vboA

    bind_buffer_(GL_ARRAY_BUFFER_, vboB);
    buffer_data_(GL_ARRAY_BUFFER_, static_cast<GLsizeiptr>(sizeof colors), colors,
                 GL_STATIC_DRAW_);
    color_pointer_(4, GL_FLOAT_, 0, nullptr); // offset 0 into vboB, NOT vboA

    clear_(GL_COLOR_BUFFER_BIT_);
    draw_arrays_(GL_TRIANGLES_, 0, 6); // must not crash
    finish_();

    ExpectGreen(32, 32, "mixed two VBOs: center");
    ExpectGreen(6, 6, "mixed two VBOs: corner");
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

// classifyClientArrays reads client_array_buffer_bindings[] as its ground
// truth (not the vertex_pointer_array_t pointer values themselves), so
// anything that restores vertex_pointer_array_t without also restoring that
// shadow can point an old, correctly-restored offset at whatever buffer
// happens to be bound in the shadow now, rather than the buffer it was
// actually declared against. glPushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT)/
// glPopClientAttrib() do exactly that restore.
TEST_F(ClientBufferClassificationTest, PopClientAttribRestoresTheBufferBindingNotJustThePointer) {
    ASSERT_NE(push_client_attrib_, nullptr);
    ASSERT_NE(pop_client_attrib_, nullptr);

    static const GLfloat greenData[] = {
        -9, -9, -9, -9, -9, -9, -9, // 7 junk floats, matching the offset used below
        -1, -1, 0, 1, 0, 1, //
        1,  -1, 0, 1, 0, 1, //
        1,  1,  0, 1, 0, 1, //
        -1, -1, 0, 1, 0, 1, //
        1,  1,  0, 1, 0, 1, //
        -1, 1,  0, 1, 0, 1, //
    };
    // Same byte layout/size as greenData so the same offset/stride pair is
    // valid against either buffer, but zero-filled: if the buffer BINDING
    // shadow is left pointing here after the pop below (the bug), the draw
    // reads zeroed (transparent black) color instead of green.
    static const GLfloat zeroData[sizeof(greenData) / sizeof(GLfloat)] = {};
    constexpr GLsizei kStride = 6 * static_cast<GLsizei>(sizeof(GLfloat));
    constexpr size_t kHeaderBytes = 7 * sizeof(GLfloat);

    GLuint vboA = 0, vboB = 0;
    gen_buffers_(1, &vboA);
    gen_buffers_(1, &vboB);
    ASSERT_NE(vboA, 0u);
    ASSERT_NE(vboB, 0u);

    bind_buffer_(GL_ARRAY_BUFFER_, vboA);
    buffer_data_(GL_ARRAY_BUFFER_, static_cast<GLsizeiptr>(sizeof greenData), greenData,
                 GL_STATIC_DRAW_);
    vertex_pointer_(2, GL_FLOAT_, kStride, reinterpret_cast<const void*>(kHeaderBytes));
    color_pointer_(4, GL_FLOAT_, kStride,
                   reinterpret_cast<const void*>(kHeaderBytes + 2 * sizeof(GLfloat)));

    push_client_attrib_(GL_CLIENT_VERTEX_ARRAY_BIT_);

    // Inside the pushed scope: switch to a different buffer and different
    // pointer values entirely - this is what a library/utility function
    // using push/pop client attrib to sandbox its own vertex setup would do.
    bind_buffer_(GL_ARRAY_BUFFER_, vboB);
    buffer_data_(GL_ARRAY_BUFFER_, static_cast<GLsizeiptr>(sizeof zeroData), zeroData,
                 GL_STATIC_DRAW_);
    vertex_pointer_(2, GL_FLOAT_, kStride, reinterpret_cast<const void*>(kHeaderBytes));
    color_pointer_(4, GL_FLOAT_, kStride,
                   reinterpret_cast<const void*>(kHeaderBytes + 2 * sizeof(GLfloat)));

    pop_client_attrib_();

    // The pop restores the pointer/stride values captured before the push
    // (offset kHeaderBytes into whatever was bound THEN) - they must be
    // read back against vboA, not vboB, which is still GL_ARRAY_BUFFER's
    // live binding after the pop (glPopClientAttrib does not touch bindings).
    clear_(GL_COLOR_BUFFER_BIT_);
    draw_arrays_(GL_TRIANGLES_, 0, 6);
    finish_();

    ExpectGreen(32, 32, "post-pop draw: center");
    ExpectGreen(6, 6, "post-pop draw: corner");
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

// Audit crash 1, reached through glDrawElements instead of glDrawArrays -
// drawElementsNow's own vertex-classification path (its `client_vertices`
// gate, plans/13 13.5) feeds commit_fpe_state_on_draw the same way, and both
// must agree the attribute data lives in vbo, not at a dereferenced offset.
TEST_F(ClientBufferClassificationTest, DrawElementsSingleBufferOffsetExceedingStrideDoesNotCrash) {
    ASSERT_NE(draw_elements_, nullptr);
    static const GLfloat data[] = {
        -9, -9, -9, -9, -9, -9, -9, // 7 junk floats nobody reads
        -1, -1, 0, 1, 0, 1, //
        1,  -1, 0, 1, 0, 1, //
        1,  1,  0, 1, 0, 1, //
        -1, -1, 0, 1, 0, 1, //
        1,  1,  0, 1, 0, 1, //
        -1, 1,  0, 1, 0, 1, //
    };
    constexpr GLsizei kStride = 6 * static_cast<GLsizei>(sizeof(GLfloat));
    constexpr size_t kHeaderBytes = 7 * sizeof(GLfloat);
    static const GLushort indices[6] = {0, 1, 2, 3, 4, 5};

    GLuint vbo = 0;
    gen_buffers_(1, &vbo);
    ASSERT_NE(vbo, 0u);
    bind_buffer_(GL_ARRAY_BUFFER_, vbo);
    buffer_data_(GL_ARRAY_BUFFER_, static_cast<GLsizeiptr>(sizeof data), data, GL_STATIC_DRAW_);

    vertex_pointer_(2, GL_FLOAT_, kStride, reinterpret_cast<const void*>(kHeaderBytes));
    color_pointer_(4, GL_FLOAT_, kStride,
                   reinterpret_cast<const void*>(kHeaderBytes + 2 * sizeof(GLfloat)));

    clear_(GL_COLOR_BUFFER_BIT_);
    draw_elements_(GL_TRIANGLES_, 6, GL_UNSIGNED_SHORT_, indices); // must not crash
    finish_();

    ExpectGreen(32, 32, "glDrawElements offset > stride: center");
    ExpectGreen(6, 6, "glDrawElements offset > stride: corner");
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

// Audit finding #2, reached through glDrawElements: drawElementsNow feeds
// the same vertex_pointer_array_t into commit_fpe_state_on_draw, so its
// mixed handling (gather_mixed_client_arrays) must agree with the
// glDrawArrays path above.
TEST_F(ClientBufferClassificationTest, MixedVboPositionAndClientMemoryColorDrawElementsRendersCorrectly) {
    ASSERT_NE(draw_elements_, nullptr);
    static const GLfloat positions[] = {
        -1, -1, 1, -1, 1, 1, -1, -1, 1, 1, -1, 1,
    };
    static const GLfloat colors[] = {
        0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1,
    };
    static const GLushort indices[6] = {0, 1, 2, 3, 4, 5};

    GLuint vboA = 0;
    gen_buffers_(1, &vboA);
    ASSERT_NE(vboA, 0u);
    bind_buffer_(GL_ARRAY_BUFFER_, vboA);
    buffer_data_(GL_ARRAY_BUFFER_, static_cast<GLsizeiptr>(sizeof positions), positions,
                 GL_STATIC_DRAW_);
    vertex_pointer_(2, GL_FLOAT_, 0, nullptr);

    bind_buffer_(GL_ARRAY_BUFFER_, 0);
    color_pointer_(4, GL_FLOAT_, 0, colors);

    clear_(GL_COLOR_BUFFER_BIT_);
    draw_elements_(GL_TRIANGLES_, 6, GL_UNSIGNED_SHORT_, indices); // must not crash
    finish_();

    ExpectGreen(32, 32, "glDrawElements mixed vbo position + client color: center");
    ExpectGreen(6, 6, "glDrawElements mixed vbo position + client color: corner");
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

// plans/13 13.5, audit finding #9: drawElementsNow used to classify a
// draw's vertex data as client memory purely by comparing the GL_VERTEX_ARRAY
// pointer's numeric magnitude against a flat 1MB threshold, independently of
// (and able to disagree with) commit_fpe_state_on_draw's own classification
// of the SAME draw. A byte offset into a real VBO can easily exceed 1MB (any
// sub-allocated/streamed/shared buffer past its first megabyte), which this
// reproduces directly rather than substituting a smaller offset: the ~1.2MB
// upload is a single glBufferData call and well within what a unit test can
// afford, so there is no need to settle for the offset-exceeds-stride stand-
// in the plan allows for cases where a genuine >1MB buffer is impractical.
// Before this fix the >1MB offset would have been misclassified as client
// memory here (forcing a needless glMapBufferRange sync on the index
// buffer, and a client_vertices=true fed into commit_fpe_state_on_draw's
// upload-size math for a draw that was never actually going to take the
// client-memory branch there) even though it renders correctly either way -
// the point of this test is that classifyClientArrays() now makes the same
// (correct) call here as it does everywhere else, not a distinct outcome.
TEST_F(ClientBufferClassificationTest,
       DrawElementsNowVboVertexOffsetBeyondOldOneMegabyteThresholdRendersCorrectly) {
    ASSERT_NE(draw_elements_, nullptr);
    constexpr size_t kHeaderFloats = 300000; // 1,200,000 bytes > 1u<<20 (1,048,576)
    constexpr GLsizei kStride = 6 * static_cast<GLsizei>(sizeof(GLfloat));
    constexpr size_t kHeaderBytes = kHeaderFloats * sizeof(GLfloat);
    static_assert(kHeaderBytes > (1u << 20), "offset must exceed the old 1MB threshold");

    std::vector<GLfloat> data(kHeaderFloats + 36, -9.0f);
    static const GLfloat quad[36] = {
        -1, -1, 0, 1, 0, 1, //
        1,  -1, 0, 1, 0, 1, //
        1,  1,  0, 1, 0, 1, //
        -1, -1, 0, 1, 0, 1, //
        1,  1,  0, 1, 0, 1, //
        -1, 1,  0, 1, 0, 1, //
    };
    std::copy(std::begin(quad), std::end(quad), data.begin() + static_cast<long>(kHeaderFloats));
    static const GLushort indices[6] = {0, 1, 2, 3, 4, 5};

    GLuint vbo = 0;
    gen_buffers_(1, &vbo);
    ASSERT_NE(vbo, 0u);
    bind_buffer_(GL_ARRAY_BUFFER_, vbo);
    buffer_data_(GL_ARRAY_BUFFER_, static_cast<GLsizeiptr>(data.size() * sizeof(GLfloat)),
                 data.data(), GL_STATIC_DRAW_);

    vertex_pointer_(2, GL_FLOAT_, kStride, reinterpret_cast<const void*>(kHeaderBytes));
    color_pointer_(4, GL_FLOAT_, kStride,
                   reinterpret_cast<const void*>(kHeaderBytes + 2 * sizeof(GLfloat)));

    clear_(GL_COLOR_BUFFER_BIT_);
    draw_elements_(GL_TRIANGLES_, 6, GL_UNSIGNED_SHORT_, indices);
    finish_();

    ExpectGreen(32, 32, "offset > 1MB: center");
    ExpectGreen(6, 6, "offset > 1MB: corner");
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

// plans/13, later pass: sfpewUserProgramFixedFunctionDrawArrays (glDrawArrays)
// and userProgramDrawElements (glDrawElements) are the two other call sites
// that shared commit_fpe_state_on_draw's pre-13.2 pointer-vs-offset guess -
// same classifyClientArrays(), same crash 1/crash 4 shapes, reached with a
// real user program bound and driving the fixed-function vertex arrays (the
// plans/09 S9 mixed pipeline).
class ClientBufferClassificationUserProgramTest : public ContextTest {
protected:
    void SetUp() override {
        ContextTest::SetUp();
        if (::testing::Test::IsSkipped()) return;

        clear_color_ = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glClearColor");
        clear_ = Get<void (*)(GLbitfield)>("glClear");
        draw_arrays_ = Get<void (*)(GLenum, GLint, GLsizei)>("glDrawArrays");
        draw_elements_ = Get<void (*)(GLenum, GLsizei, GLenum, const void*)>("glDrawElements");
        get_error_ = Get<GLenum (*)()>("glGetError");
        finish_ = Get<void (*)()>("glFinish");
        gen_buffers_ = Get<void (*)(GLsizei, GLuint*)>("glGenBuffers");
        bind_buffer_ = Get<void (*)(GLenum, GLuint)>("glBindBuffer");
        buffer_data_ = Get<void (*)(GLenum, GLsizeiptr, const void*, GLenum)>("glBufferData");
        enable_client_state_ = Get<void (*)(GLenum)>("glEnableClientState");
        vertex_pointer_ = Get<void (*)(GLint, GLenum, GLsizei, const void*)>("glVertexPointer");
        color_pointer_ = Get<void (*)(GLint, GLenum, GLsizei, const void*)>("glColorPointer");
        create_shader_ = Get<GLuint (*)(GLenum)>("glCreateShader");
        shader_source_ =
            Get<void (*)(GLuint, GLsizei, const char* const*, const GLint*)>("glShaderSource");
        compile_shader_ = Get<void (*)(GLuint)>("glCompileShader");
        get_shaderiv_ = Get<void (*)(GLuint, GLenum, GLint*)>("glGetShaderiv");
        get_shader_info_log_ =
            Get<void (*)(GLuint, GLsizei, GLsizei*, char*)>("glGetShaderInfoLog");
        create_program_ = Get<GLuint (*)()>("glCreateProgram");
        attach_shader_ = Get<void (*)(GLuint, GLuint)>("glAttachShader");
        link_program_ = Get<void (*)(GLuint)>("glLinkProgram");
        get_programiv_ = Get<void (*)(GLuint, GLenum, GLint*)>("glGetProgramiv");
        use_program_ = Get<void (*)(GLuint)>("glUseProgram");
        auto read_pixels =
            Get<void (*)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*)>("glReadPixels");
        ASSERT_NE(read_pixels, nullptr);
        probe_.emplace(read_pixels);

        enable_client_state_(GL_VERTEX_ARRAY_);
        enable_client_state_(GL_COLOR_ARRAY_);
        clear_color_(0.0f, 0.0f, 1.0f, 1.0f); // blue: anything un-drawn stays blue
        get_error_();

        // Plain pass-through: gl_Color reaches gl_FragColor unmodified, so
        // the SAME green-quad data used by the fixed-function tests above
        // still means success here - proof the user program actually
        // consumed the fixed-function arrays would be a color transform
        // (gtest_mixed_pipeline.cc's R<->B swap), which is not this test's
        // concern; this only needs the draw to not crash and to read the
        // right bytes.
        static const char* k_vs = "#version 120\n"
                                  "varying vec4 col;\n"
                                  "void main() { gl_Position = gl_Vertex; col = gl_Color; }\n";
        static const char* k_fs = "#version 120\n"
                                  "varying vec4 col;\n"
                                  "void main() { gl_FragColor = col; }\n";
        const GLuint vs = CompileStage(0x8B31 /* GL_VERTEX_SHADER */, k_vs, "vs");
        const GLuint fs = CompileStage(0x8B30 /* GL_FRAGMENT_SHADER */, k_fs, "fs");
        ASSERT_NE(vs, 0u);
        ASSERT_NE(fs, 0u);
        program_ = create_program_();
        attach_shader_(program_, vs);
        attach_shader_(program_, fs);
        link_program_(program_);
        GLint linked = 0;
        get_programiv_(program_, 0x8B82 /* GL_LINK_STATUS */, &linked);
        ASSERT_NE(linked, 0) << "passthrough program link";
        use_program_(program_);
    }

    GLuint CompileStage(GLenum stage, const char* src, const char* tag) {
        const GLuint shader = create_shader_(stage);
        shader_source_(shader, 1, &src, nullptr);
        compile_shader_(shader);
        GLint ok = 0;
        get_shaderiv_(shader, 0x8B81 /* GL_COMPILE_STATUS */, &ok);
        if (ok == 0) {
            char log[4096] = {};
            get_shader_info_log_(shader, sizeof log, nullptr, log);
            ADD_FAILURE() << tag << " compile:\n" << log;
            return 0;
        }
        return shader;
    }

    void ExpectGreen(int x, int y, const char* what) {
        const PixelProbe::Rgba p = probe_->At(x, y);
        EXPECT_TRUE(p.g > 200 && p.r <= 200 && p.b <= 200)
            << what << ": pixel(" << x << ',' << y << ") = (" << (int)p.r << ',' << (int)p.g
            << ',' << (int)p.b << ')';
    }

    void (*clear_color_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*clear_)(GLbitfield) = nullptr;
    void (*draw_arrays_)(GLenum, GLint, GLsizei) = nullptr;
    void (*draw_elements_)(GLenum, GLsizei, GLenum, const void*) = nullptr;
    GLenum (*get_error_)() = nullptr;
    void (*finish_)() = nullptr;
    void (*gen_buffers_)(GLsizei, GLuint*) = nullptr;
    void (*bind_buffer_)(GLenum, GLuint) = nullptr;
    void (*buffer_data_)(GLenum, GLsizeiptr, const void*, GLenum) = nullptr;
    void (*enable_client_state_)(GLenum) = nullptr;
    void (*vertex_pointer_)(GLint, GLenum, GLsizei, const void*) = nullptr;
    void (*color_pointer_)(GLint, GLenum, GLsizei, const void*) = nullptr;
    GLuint (*create_shader_)(GLenum) = nullptr;
    void (*shader_source_)(GLuint, GLsizei, const char* const*, const GLint*) = nullptr;
    void (*compile_shader_)(GLuint) = nullptr;
    void (*get_shaderiv_)(GLuint, GLenum, GLint*) = nullptr;
    void (*get_shader_info_log_)(GLuint, GLsizei, GLsizei*, char*) = nullptr;
    GLuint (*create_program_)() = nullptr;
    void (*attach_shader_)(GLuint, GLuint) = nullptr;
    void (*link_program_)(GLuint) = nullptr;
    void (*get_programiv_)(GLuint, GLenum, GLint*) = nullptr;
    void (*use_program_)(GLuint) = nullptr;
    GLuint program_ = 0;
    std::optional<PixelProbe> probe_;
};

// Audit crash 1 through sfpewUserProgramFixedFunctionDrawArrays.
TEST_F(ClientBufferClassificationUserProgramTest, DrawArraysOffsetExceedingStrideDoesNotCrash) {
    static const GLfloat data[] = {
        -9, -9, -9, -9, -9, -9, -9, //
        -1, -1, 0, 1, 0, 1, //
        1,  -1, 0, 1, 0, 1, //
        1,  1,  0, 1, 0, 1, //
        -1, -1, 0, 1, 0, 1, //
        1,  1,  0, 1, 0, 1, //
        -1, 1,  0, 1, 0, 1, //
    };
    constexpr GLsizei kStride = 6 * static_cast<GLsizei>(sizeof(GLfloat));
    constexpr size_t kHeaderBytes = 7 * sizeof(GLfloat);

    GLuint vbo = 0;
    gen_buffers_(1, &vbo);
    ASSERT_NE(vbo, 0u);
    bind_buffer_(GL_ARRAY_BUFFER_, vbo);
    buffer_data_(GL_ARRAY_BUFFER_, static_cast<GLsizeiptr>(sizeof data), data, GL_STATIC_DRAW_);
    vertex_pointer_(2, GL_FLOAT_, kStride, reinterpret_cast<const void*>(kHeaderBytes));
    color_pointer_(4, GL_FLOAT_, kStride,
                   reinterpret_cast<const void*>(kHeaderBytes + 2 * sizeof(GLfloat)));

    clear_(GL_COLOR_BUFFER_BIT_);
    draw_arrays_(GL_TRIANGLES_, 0, 6); // must not crash
    finish_();

    ExpectGreen(32, 32, "user program, offset > stride: center");
    ExpectGreen(6, 6, "user program, offset > stride: corner");
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

// Audit crash 4 through sfpewUserProgramFixedFunctionDrawArrays.
TEST_F(ClientBufferClassificationUserProgramTest,
       DrawArraysPointerSetWhileBoundSurvivesUnbindingBeforeTheDraw) {
    static const GLfloat verts[] = {
        -1, -1, 0, 1, 0, 1, 1, -1, 0, 1, 0, 1, 1, 1, 0, 1, 0, 1,
        -1, -1, 0, 1, 0, 1, 1, 1,  0, 1, 0, 1, -1, 1, 0, 1, 0, 1,
    };
    constexpr GLsizei kStride = 6 * static_cast<GLsizei>(sizeof(GLfloat));

    GLuint vbo = 0;
    gen_buffers_(1, &vbo);
    ASSERT_NE(vbo, 0u);
    bind_buffer_(GL_ARRAY_BUFFER_, vbo);
    buffer_data_(GL_ARRAY_BUFFER_, static_cast<GLsizeiptr>(sizeof verts), verts, GL_STATIC_DRAW_);
    vertex_pointer_(2, GL_FLOAT_, kStride, nullptr);
    color_pointer_(4, GL_FLOAT_, kStride, reinterpret_cast<const void*>(2 * sizeof(GLfloat)));
    bind_buffer_(GL_ARRAY_BUFFER_, 0); // gone by draw time; source is vbo regardless

    clear_(GL_COLOR_BUFFER_BIT_);
    draw_arrays_(GL_TRIANGLES_, 0, 6);
    finish_();

    ExpectGreen(32, 32, "user program, pointer set then unbound: center");
    ExpectGreen(6, 6, "user program, pointer set then unbound: corner");
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

// Audit finding #2 through sfpewUserProgramFixedFunctionDrawArrays.
TEST_F(ClientBufferClassificationUserProgramTest,
       DrawArraysMixedVboPositionAndClientMemoryColorRendersCorrectly) {
    static const GLfloat positions[] = {
        -1, -1, 1, -1, 1, 1, -1, -1, 1, 1, -1, 1,
    };
    static const GLfloat colors[] = {
        0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1,
    };

    GLuint vboA = 0;
    gen_buffers_(1, &vboA);
    ASSERT_NE(vboA, 0u);
    bind_buffer_(GL_ARRAY_BUFFER_, vboA);
    buffer_data_(GL_ARRAY_BUFFER_, static_cast<GLsizeiptr>(sizeof positions), positions,
                 GL_STATIC_DRAW_);
    vertex_pointer_(2, GL_FLOAT_, 0, nullptr);

    bind_buffer_(GL_ARRAY_BUFFER_, 0);
    color_pointer_(4, GL_FLOAT_, 0, colors);

    clear_(GL_COLOR_BUFFER_BIT_);
    draw_arrays_(GL_TRIANGLES_, 0, 6); // must not crash
    finish_();

    ExpectGreen(32, 32, "user program, mixed vbo position + client color: center");
    ExpectGreen(6, 6, "user program, mixed vbo position + client color: corner");
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

// Audit crash 1 through userProgramDrawElements.
TEST_F(ClientBufferClassificationUserProgramTest, DrawElementsOffsetExceedingStrideDoesNotCrash) {
    ASSERT_NE(draw_elements_, nullptr);
    static const GLfloat data[] = {
        -9, -9, -9, -9, -9, -9, -9, //
        -1, -1, 0, 1, 0, 1, //
        1,  -1, 0, 1, 0, 1, //
        1,  1,  0, 1, 0, 1, //
        -1, -1, 0, 1, 0, 1, //
        1,  1,  0, 1, 0, 1, //
        -1, 1,  0, 1, 0, 1, //
    };
    constexpr GLsizei kStride = 6 * static_cast<GLsizei>(sizeof(GLfloat));
    constexpr size_t kHeaderBytes = 7 * sizeof(GLfloat);
    static const GLushort indices[6] = {0, 1, 2, 3, 4, 5};

    GLuint vbo = 0;
    gen_buffers_(1, &vbo);
    ASSERT_NE(vbo, 0u);
    bind_buffer_(GL_ARRAY_BUFFER_, vbo);
    buffer_data_(GL_ARRAY_BUFFER_, static_cast<GLsizeiptr>(sizeof data), data, GL_STATIC_DRAW_);
    vertex_pointer_(2, GL_FLOAT_, kStride, reinterpret_cast<const void*>(kHeaderBytes));
    color_pointer_(4, GL_FLOAT_, kStride,
                   reinterpret_cast<const void*>(kHeaderBytes + 2 * sizeof(GLfloat)));

    clear_(GL_COLOR_BUFFER_BIT_);
    draw_elements_(GL_TRIANGLES_, 6, GL_UNSIGNED_SHORT_, indices); // must not crash
    finish_();

    ExpectGreen(32, 32, "user program glDrawElements, offset > stride: center");
    ExpectGreen(6, 6, "user program glDrawElements, offset > stride: corner");
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

// Audit crash 4 through userProgramDrawElements.
TEST_F(ClientBufferClassificationUserProgramTest,
       DrawElementsPointerSetWhileBoundSurvivesUnbindingBeforeTheDraw) {
    ASSERT_NE(draw_elements_, nullptr);
    static const GLfloat verts[] = {
        -1, -1, 0, 1, 0, 1, 1, -1, 0, 1, 0, 1, 1, 1, 0, 1, 0, 1,
        -1, -1, 0, 1, 0, 1, 1, 1,  0, 1, 0, 1, -1, 1, 0, 1, 0, 1,
    };
    constexpr GLsizei kStride = 6 * static_cast<GLsizei>(sizeof(GLfloat));
    static const GLushort indices[6] = {0, 1, 2, 3, 4, 5};

    GLuint vbo = 0;
    gen_buffers_(1, &vbo);
    ASSERT_NE(vbo, 0u);
    bind_buffer_(GL_ARRAY_BUFFER_, vbo);
    buffer_data_(GL_ARRAY_BUFFER_, static_cast<GLsizeiptr>(sizeof verts), verts, GL_STATIC_DRAW_);
    vertex_pointer_(2, GL_FLOAT_, kStride, nullptr);
    color_pointer_(4, GL_FLOAT_, kStride, reinterpret_cast<const void*>(2 * sizeof(GLfloat)));
    bind_buffer_(GL_ARRAY_BUFFER_, 0); // gone by draw time; source is vbo regardless

    clear_(GL_COLOR_BUFFER_BIT_);
    draw_elements_(GL_TRIANGLES_, 6, GL_UNSIGNED_SHORT_, indices);
    finish_();

    ExpectGreen(32, 32, "user program glDrawElements, pointer set then unbound: center");
    ExpectGreen(6, 6, "user program glDrawElements, pointer set then unbound: corner");
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

// Audit finding #2 through userProgramDrawElements.
TEST_F(ClientBufferClassificationUserProgramTest,
       DrawElementsMixedVboPositionAndClientMemoryColorRendersCorrectly) {
    ASSERT_NE(draw_elements_, nullptr);
    static const GLfloat positions[] = {
        -1, -1, 1, -1, 1, 1, -1, -1, 1, 1, -1, 1,
    };
    static const GLfloat colors[] = {
        0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1,
    };
    static const GLushort indices[6] = {0, 1, 2, 3, 4, 5};

    GLuint vboA = 0;
    gen_buffers_(1, &vboA);
    ASSERT_NE(vboA, 0u);
    bind_buffer_(GL_ARRAY_BUFFER_, vboA);
    buffer_data_(GL_ARRAY_BUFFER_, static_cast<GLsizeiptr>(sizeof positions), positions,
                 GL_STATIC_DRAW_);
    vertex_pointer_(2, GL_FLOAT_, 0, nullptr);

    bind_buffer_(GL_ARRAY_BUFFER_, 0);
    color_pointer_(4, GL_FLOAT_, 0, colors);

    clear_(GL_COLOR_BUFFER_BIT_);
    draw_elements_(GL_TRIANGLES_, 6, GL_UNSIGNED_SHORT_, indices); // must not crash
    finish_();

    ExpectGreen(32, 32, "user program glDrawElements, mixed vbo position + client color: center");
    ExpectGreen(6, 6, "user program glDrawElements, mixed vbo position + client color: corner");
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

} // namespace
