// SimpleFPEWrapper - tests/gtest_map_buffer_barrier.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// plans/16 H5: glMapBuffer / glGetBufferSubData act through the CURRENTLY
// BOUND buffer, so they owe the same entry barrier every other member of the
// buffer surface takes (see the contract note above glBufferData in
// fpe/vertexpointer.cpp).
//
// The state that breaks them is the normal state after any fixed-function
// draw: the draw-state restore is deferred, so GL_ARRAY_BUFFER is still the
// wrapper's immediate ring and the wrapper's VAO still carries the element
// ring. Without the barrier both entry points address the ring instead of the
// app's buffer - NULL plus a latched GL_INVALID_OPERATION where the ring is
// persistently mapped, or a silent pointer INTO wrapper memory where it is
// not.
//
// Every case here binds its buffer BEFORE the fixed-function draw and never
// re-binds after: glBindBuffer is what would otherwise repair the binding
// (it updates the held snapshot for GL_ARRAY_BUFFER, barriers for the rest),
// and an app that maps a buffer it bound earlier - the LWJGL2 shape - never
// issues that repair.

#include "sfpew_gtest.h"

#include <cstring>

namespace {

using sfpew_test::ContextTest;
using sfpew_test::GLbitfield;
using sfpew_test::GLboolean;
using sfpew_test::GLenum;
using sfpew_test::GLfloat;
using sfpew_test::GLint;
using sfpew_test::GLintptr;
using sfpew_test::GLsizei;
using sfpew_test::GLsizeiptr;
using sfpew_test::GLuint;

constexpr GLenum GL_ARRAY_BUFFER_ = 0x8892;
constexpr GLenum GL_ELEMENT_ARRAY_BUFFER_ = 0x8893;
constexpr GLenum GL_STATIC_DRAW_ = 0x88E4;
constexpr GLenum GL_BUFFER_SIZE_ = 0x8764;
constexpr GLenum GL_READ_ONLY_ = 0x88B8;
constexpr GLenum GL_WRITE_ONLY_ = 0x88B9;
constexpr GLenum GL_READ_WRITE_ = 0x88BA;
constexpr GLbitfield GL_MAP_READ_BIT_ = 0x0001;
constexpr GLenum GL_QUADS_ = 0x0007;
constexpr GLenum GL_NO_ERROR_ = 0;

class MapBufferBarrierTest : public ContextTest {
protected:
    void SetUp() override {
        ContextTest::SetUp();
        if (::testing::Test::IsSkipped()) return;

        gen_buffers_ = Get<void (*)(GLsizei, GLuint*)>("glGenBuffers");
        bind_buffer_ = Get<void (*)(GLenum, GLuint)>("glBindBuffer");
        buffer_data_ = Get<void (*)(GLenum, GLsizeiptr, const void*, GLenum)>("glBufferData");
        get_buffer_parameteriv_ = Get<void (*)(GLenum, GLenum, GLint*)>("glGetBufferParameteriv");
        map_buffer_ = Get<void* (*)(GLenum, GLenum)>("glMapBuffer");
        map_buffer_range_ =
            Get<void* (*)(GLenum, GLintptr, GLsizeiptr, GLbitfield)>("glMapBufferRange");
        unmap_buffer_ = Get<GLboolean (*)(GLenum)>("glUnmapBuffer");
        get_buffer_sub_data_ =
            Get<void (*)(GLenum, GLintptr, GLsizeiptr, void*)>("glGetBufferSubData");
        begin_ = Get<void (*)(GLenum)>("glBegin");
        end_ = Get<void (*)()>("glEnd");
        color4f_ = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glColor4f");
        vertex2f_ = Get<void (*)(GLfloat, GLfloat)>("glVertex2f");
        get_error_ = Get<GLenum (*)()>("glGetError");
        ASSERT_NE(get_error_, nullptr);
        while (get_error_() != GL_NO_ERROR_) {
        }
    }

    static const GLfloat* Payload() {
        static const GLfloat k_payload[16] = {1.5f,  -2.25f, 3.75f,  -4.5f,  5.125f, -6.5f,
                                              7.25f, -8.75f, 9.5f,   -10.25f, 11.75f, -12.5f,
                                              13.125f, -14.5f, 15.25f, -16.75f};
        return k_payload;
    }
    static constexpr GLsizeiptr kPayloadBytes = 16 * sizeof(GLfloat);

    GLuint MakeBuffer(GLenum target) {
        GLuint buffer = 0;
        gen_buffers_(1, &buffer);
        EXPECT_NE(buffer, 0u);
        bind_buffer_(target, buffer);
        buffer_data_(target, kPayloadBytes, Payload(), GL_STATIC_DRAW_);
        return buffer;
    }

    // Long enough that the small-run merger declines it, so it draws at glEnd
    // and leaves the deferred restore held with the wrapper's rings bound.
    // A short run would sit pending instead, and the map's own barrier would
    // then be the first thing to flush it - the case that cannot fail.
    void FpeDrawUnmerged() {
        begin_(GL_QUADS_);
        color4f_(1.0f, 0.0f, 0.0f, 1.0f);
        for (int i = 0; i < 20; ++i) { // 80 vertices, past the 64-vertex merge limit
            const GLfloat y0 = -0.5f + static_cast<GLfloat>(i) * 0.05f;
            const GLfloat y1 = y0 + 0.05f;
            vertex2f_(-0.5f, y0);
            vertex2f_(0.5f, y0);
            vertex2f_(0.5f, y1);
            vertex2f_(-0.5f, y1);
        }
        end_();
    }

    void (*gen_buffers_)(GLsizei, GLuint*) = nullptr;
    void (*bind_buffer_)(GLenum, GLuint) = nullptr;
    void (*buffer_data_)(GLenum, GLsizeiptr, const void*, GLenum) = nullptr;
    void (*get_buffer_parameteriv_)(GLenum, GLenum, GLint*) = nullptr;
    void* (*map_buffer_)(GLenum, GLenum) = nullptr;
    void* (*map_buffer_range_)(GLenum, GLintptr, GLsizeiptr, GLbitfield) = nullptr;
    GLboolean (*unmap_buffer_)(GLenum) = nullptr;
    void (*get_buffer_sub_data_)(GLenum, GLintptr, GLsizeiptr, void*) = nullptr;
    void (*begin_)(GLenum) = nullptr;
    void (*end_)() = nullptr;
    void (*color4f_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*vertex2f_)(GLfloat, GLfloat) = nullptr;
    GLenum (*get_error_)() = nullptr;
};

TEST_F(MapBufferBarrierTest, MapBufferAfterFixedFunctionDrawAddressesTheAppsArrayBuffer) {
    MakeBuffer(GL_ARRAY_BUFFER_);
    FpeDrawUnmerged();

    const GLfloat* mapped = static_cast<const GLfloat*>(map_buffer_(GL_ARRAY_BUFFER_,
                                                                   GL_READ_ONLY_));
    ASSERT_NE(mapped, nullptr) << "glMapBuffer(GL_ARRAY_BUFFER) after a fixed-function draw "
                                  "returned NULL, error 0x"
                               << std::hex << get_error_();
    EXPECT_EQ(std::memcmp(mapped, Payload(), static_cast<size_t>(kPayloadBytes)), 0)
        << "the mapping is not the app's buffer";
    EXPECT_TRUE(unmap_buffer_(GL_ARRAY_BUFFER_));
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

// The same omission on a backend whose ring falls back to non-persistent
// mapping hands the app a live pointer into wrapper memory instead of failing,
// so prove the write lands in the app's buffer and nowhere else.
TEST_F(MapBufferBarrierTest, MapBufferWriteAfterFixedFunctionDrawLandsInTheAppsBuffer) {
    MakeBuffer(GL_ARRAY_BUFFER_);
    FpeDrawUnmerged();

    GLfloat* mapped = static_cast<GLfloat*>(map_buffer_(GL_ARRAY_BUFFER_, GL_WRITE_ONLY_));
    ASSERT_NE(mapped, nullptr) << "glMapBuffer(GL_WRITE_ONLY) returned NULL, error 0x" << std::hex
                               << get_error_();
    GLfloat written[16];
    for (int i = 0; i < 16; ++i) written[i] = Payload()[i] * -2.0f;
    std::memcpy(mapped, written, sizeof written);
    EXPECT_TRUE(unmap_buffer_(GL_ARRAY_BUFFER_));

    const GLfloat* read_back = static_cast<const GLfloat*>(
        map_buffer_range_(GL_ARRAY_BUFFER_, 0, kPayloadBytes, GL_MAP_READ_BIT_));
    ASSERT_NE(read_back, nullptr);
    EXPECT_EQ(std::memcmp(read_back, written, sizeof written), 0)
        << "the write went somewhere other than the app's buffer";
    EXPECT_TRUE(unmap_buffer_(GL_ARRAY_BUFFER_));
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

// GL_QUADS is index-rewritten, so the draw above also leaves the wrapper's
// element ring bound through the wrapper's own VAO.
TEST_F(MapBufferBarrierTest, MapBufferAfterFixedFunctionDrawAddressesTheAppsElementBuffer) {
    MakeBuffer(GL_ELEMENT_ARRAY_BUFFER_);
    FpeDrawUnmerged();

    const GLfloat* mapped = static_cast<const GLfloat*>(
        map_buffer_(GL_ELEMENT_ARRAY_BUFFER_, GL_READ_ONLY_));
    ASSERT_NE(mapped, nullptr) << "glMapBuffer(GL_ELEMENT_ARRAY_BUFFER) after a fixed-function "
                                  "draw returned NULL, error 0x"
                               << std::hex << get_error_();
    EXPECT_EQ(std::memcmp(mapped, Payload(), static_cast<size_t>(kPayloadBytes)), 0)
        << "the mapping is not the app's element buffer";
    EXPECT_TRUE(unmap_buffer_(GL_ELEMENT_ARRAY_BUFFER_));
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

TEST_F(MapBufferBarrierTest, GetBufferSubDataAfterFixedFunctionDrawReadsTheAppsBuffer) {
    MakeBuffer(GL_ARRAY_BUFFER_);
    FpeDrawUnmerged();

    GLfloat out[16];
    std::memset(out, 0, sizeof out);
    get_buffer_sub_data_(GL_ARRAY_BUFFER_, 0, kPayloadBytes, out);
    EXPECT_EQ(std::memcmp(out, Payload(), sizeof out), 0)
        << "glGetBufferSubData read something other than the app's buffer";
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

// glMapBuffer sizes its range from GL_BUFFER_SIZE, which unbarriered answers
// for the ring. Map GL_READ_WRITE and touch the last element to show the range
// is the app's buffer end to end, not a prefix of something else's.
TEST_F(MapBufferBarrierTest, ReadWriteMapAfterFixedFunctionDrawSpansTheWholeAppBuffer) {
    MakeBuffer(GL_ARRAY_BUFFER_);
    FpeDrawUnmerged();

    GLint size = 0;
    get_buffer_parameteriv_(GL_ARRAY_BUFFER_, GL_BUFFER_SIZE_, &size);
    ASSERT_EQ(size, static_cast<GLint>(kPayloadBytes));

    GLfloat* mapped = static_cast<GLfloat*>(map_buffer_(GL_ARRAY_BUFFER_, GL_READ_WRITE_));
    ASSERT_NE(mapped, nullptr) << "glMapBuffer(GL_READ_WRITE) returned NULL, error 0x" << std::hex
                               << get_error_();
    EXPECT_EQ(mapped[15], Payload()[15]);
    mapped[15] = 42.5f;
    EXPECT_TRUE(unmap_buffer_(GL_ARRAY_BUFFER_));

    GLfloat out[16];
    std::memset(out, 0, sizeof out);
    get_buffer_sub_data_(GL_ARRAY_BUFFER_, 0, kPayloadBytes, out);
    EXPECT_EQ(out[15], 42.5f);
    EXPECT_EQ(std::memcmp(out, Payload(), 15 * sizeof(GLfloat)), 0);
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

} // namespace
