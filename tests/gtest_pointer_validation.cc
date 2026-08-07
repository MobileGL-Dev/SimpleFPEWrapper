// SimpleFPEWrapper - tests/gtest_pointer_validation.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// The gl*Pointer family used to validate nothing: whatever size, type and
// stride arrived was stored verbatim. GL 2.1 section 2.8 (and every one of
// the eight reference pages) asks for GL_INVALID_VALUE on an illegal size or
// a negative stride, GL_INVALID_ENUM on an illegal type - and, the part that
// matters past conformance, that a rejected call leave the array state
// UNCHANGED.
//
// The stride half is a memory-safety bug, not a nicety: gather_client_arrays
// (fpe.cpp) casts the stored stride to size_t, so a stored -20 became a step
// of ~2^64 and walked the source pointer out of its allocation on the second
// vertex (plans/16 M1, reproduced as a SIGSEGV). Rejecting the call before it
// reaches the array state is what keeps it out of every consumer at once.

#include "sfpew_gtest.h"

namespace {

using sfpew_test::ContextTest;
using sfpew_test::GLbitfield;
using sfpew_test::GLenum;
using sfpew_test::GLfloat;
using sfpew_test::GLint;
using sfpew_test::GLsizei;
using sfpew_test::GLubyte;
using sfpew_test::PixelProbe;

constexpr GLenum GL_NO_ERROR_ = 0;
constexpr GLenum GL_INVALID_ENUM_ = 0x0500;
constexpr GLenum GL_INVALID_VALUE_ = 0x0501;
constexpr GLbitfield GL_COLOR_BUFFER_BIT_ = 0x00004000;
constexpr GLenum GL_TRIANGLES_ = 0x0004;
constexpr GLenum GL_BYTE_ = 0x1400;
constexpr GLenum GL_UNSIGNED_BYTE_ = 0x1401;
constexpr GLenum GL_SHORT_ = 0x1402;
constexpr GLenum GL_INT_ = 0x1404;
constexpr GLenum GL_FLOAT_ = 0x1406;
constexpr GLenum GL_DOUBLE_ = 0x140A;
constexpr GLenum GL_BGRA_ = 0x80E1;
constexpr GLenum GL_VERTEX_ARRAY_ = 0x8074;
constexpr GLenum GL_COLOR_ARRAY_ = 0x8076;

// Neither a type nor a size any array accepts.
constexpr GLenum kBogusType = 0x9999;

class PointerValidationTest : public ContextTest {
protected:
    void SetUp() override {
        ContextTest::SetUp();
        if (::testing::Test::IsSkipped()) return;

        get_error_ = Get<GLenum (*)()>("glGetError");
        vertex_pointer_ = Get<void (*)(GLint, GLenum, GLsizei, const void*)>("glVertexPointer");
        color_pointer_ = Get<void (*)(GLint, GLenum, GLsizei, const void*)>("glColorPointer");
        normal_pointer_ = Get<void (*)(GLenum, GLsizei, const void*)>("glNormalPointer");
        tex_coord_pointer_ = Get<void (*)(GLint, GLenum, GLsizei, const void*)>("glTexCoordPointer");
        index_pointer_ = Get<void (*)(GLenum, GLsizei, const void*)>("glIndexPointer");
        edge_flag_pointer_ = Get<void (*)(GLsizei, const void*)>("glEdgeFlagPointer");
        secondary_color_pointer_ =
            Get<void (*)(GLint, GLenum, GLsizei, const void*)>("glSecondaryColorPointer");
        fog_coord_pointer_ = Get<void (*)(GLenum, GLsizei, const void*)>("glFogCoordPointer");
        ASSERT_NE(get_error_, nullptr);
        ASSERT_EQ(get_error_(), GL_NO_ERROR_) << "residual error before the case began";
    }

    GLenum (*get_error_)() = nullptr;
    void (*vertex_pointer_)(GLint, GLenum, GLsizei, const void*) = nullptr;
    void (*color_pointer_)(GLint, GLenum, GLsizei, const void*) = nullptr;
    void (*normal_pointer_)(GLenum, GLsizei, const void*) = nullptr;
    void (*tex_coord_pointer_)(GLint, GLenum, GLsizei, const void*) = nullptr;
    void (*index_pointer_)(GLenum, GLsizei, const void*) = nullptr;
    void (*edge_flag_pointer_)(GLsizei, const void*) = nullptr;
    void (*secondary_color_pointer_)(GLint, GLenum, GLsizei, const void*) = nullptr;
    void (*fog_coord_pointer_)(GLenum, GLsizei, const void*) = nullptr;

    static const GLfloat* Scratch() {
        static const GLfloat data[64] = {};
        return data;
    }
};

TEST_F(PointerValidationTest, IllegalSizeIsInvalidValue) {
    const void* p = Scratch();

    vertex_pointer_(1, GL_FLOAT_, 0, p);
    EXPECT_EQ(get_error_(), GL_INVALID_VALUE_) << "glVertexPointer(size 1)";
    vertex_pointer_(5, GL_FLOAT_, 0, p);
    EXPECT_EQ(get_error_(), GL_INVALID_VALUE_) << "glVertexPointer(size 5)";

    color_pointer_(2, GL_FLOAT_, 0, p);
    EXPECT_EQ(get_error_(), GL_INVALID_VALUE_) << "glColorPointer(size 2)";
    color_pointer_(5, GL_FLOAT_, 0, p);
    EXPECT_EQ(get_error_(), GL_INVALID_VALUE_) << "glColorPointer(size 5)";

    tex_coord_pointer_(0, GL_FLOAT_, 0, p);
    EXPECT_EQ(get_error_(), GL_INVALID_VALUE_) << "glTexCoordPointer(size 0)";
    tex_coord_pointer_(5, GL_FLOAT_, 0, p);
    EXPECT_EQ(get_error_(), GL_INVALID_VALUE_) << "glTexCoordPointer(size 5)";

    secondary_color_pointer_(4, GL_FLOAT_, 0, p);
    EXPECT_EQ(get_error_(), GL_INVALID_VALUE_) << "glSecondaryColorPointer(size 4)";
}

TEST_F(PointerValidationTest, IllegalTypeIsInvalidEnum) {
    const void* p = Scratch();

    // GL_UNSIGNED_BYTE is a legal COLOR type and an illegal vertex/texcoord
    // one, which is exactly the sort of confusion a shared type check would
    // have hidden.
    vertex_pointer_(2, GL_UNSIGNED_BYTE_, 0, p);
    EXPECT_EQ(get_error_(), GL_INVALID_ENUM_) << "glVertexPointer(GL_UNSIGNED_BYTE)";
    tex_coord_pointer_(2, GL_UNSIGNED_BYTE_, 0, p);
    EXPECT_EQ(get_error_(), GL_INVALID_ENUM_) << "glTexCoordPointer(GL_UNSIGNED_BYTE)";
    normal_pointer_(GL_UNSIGNED_BYTE_, 0, p);
    EXPECT_EQ(get_error_(), GL_INVALID_ENUM_) << "glNormalPointer(GL_UNSIGNED_BYTE)";
    // glIndexPointer is the mirror image: unsigned byte yes, signed byte no.
    index_pointer_(GL_BYTE_, 0, p);
    EXPECT_EQ(get_error_(), GL_INVALID_ENUM_) << "glIndexPointer(GL_BYTE)";

    color_pointer_(4, kBogusType, 0, p);
    EXPECT_EQ(get_error_(), GL_INVALID_ENUM_) << "glColorPointer(bogus type)";
    secondary_color_pointer_(3, kBogusType, 0, p);
    EXPECT_EQ(get_error_(), GL_INVALID_ENUM_) << "glSecondaryColorPointer(bogus type)";
    fog_coord_pointer_(GL_INT_, 0, p);
    EXPECT_EQ(get_error_(), GL_INVALID_ENUM_) << "glFogCoordPointer(GL_INT)";
}

TEST_F(PointerValidationTest, LegalSizeAndTypeCombinationsStayLegal) {
    const void* p = Scratch();

    vertex_pointer_(3, GL_SHORT_, 0, p);
    vertex_pointer_(4, GL_DOUBLE_, 0, p);
    normal_pointer_(GL_BYTE_, 0, p);
    color_pointer_(3, GL_UNSIGNED_BYTE_, 0, p);
    color_pointer_(4, GL_INT_, 0, p);
    tex_coord_pointer_(1, GL_INT_, 0, p);
    index_pointer_(GL_UNSIGNED_BYTE_, 0, p);
    edge_flag_pointer_(0, p);
    secondary_color_pointer_(3, GL_UNSIGNED_BYTE_, 0, p);
    fog_coord_pointer_(GL_DOUBLE_, 0, p);
    EXPECT_EQ(get_error_(), GL_NO_ERROR_) << "a spec-legal declaration must not raise anything";

    // GL_ARB_vertex_array_bgra is in the reported extension string, so
    // size == GL_BGRA has to survive the new size check as well.
    color_pointer_(GL_BGRA_, GL_UNSIGNED_BYTE_, 0, p);
    EXPECT_EQ(get_error_(), GL_NO_ERROR_) << "glColorPointer(GL_BGRA, GL_UNSIGNED_BYTE)";
}

TEST_F(PointerValidationTest, NegativeStrideIsInvalidValue) {
    const void* p = Scratch();

    vertex_pointer_(2, GL_FLOAT_, -8, p);
    EXPECT_EQ(get_error_(), GL_INVALID_VALUE_) << "glVertexPointer(stride -8)";
    color_pointer_(4, GL_FLOAT_, -8, p);
    EXPECT_EQ(get_error_(), GL_INVALID_VALUE_) << "glColorPointer(stride -8)";
    normal_pointer_(GL_FLOAT_, -1, p);
    EXPECT_EQ(get_error_(), GL_INVALID_VALUE_) << "glNormalPointer(stride -1)";
    tex_coord_pointer_(2, GL_FLOAT_, -1, p);
    EXPECT_EQ(get_error_(), GL_INVALID_VALUE_) << "glTexCoordPointer(stride -1)";
    index_pointer_(GL_FLOAT_, -1, p);
    EXPECT_EQ(get_error_(), GL_INVALID_VALUE_) << "glIndexPointer(stride -1)";
    edge_flag_pointer_(-1, p);
    EXPECT_EQ(get_error_(), GL_INVALID_VALUE_) << "glEdgeFlagPointer(stride -1)";
    secondary_color_pointer_(3, GL_FLOAT_, -1, p);
    EXPECT_EQ(get_error_(), GL_INVALID_VALUE_) << "glSecondaryColorPointer(stride -1)";
    fog_coord_pointer_(GL_FLOAT_, -1, p);
    EXPECT_EQ(get_error_(), GL_INVALID_VALUE_) << "glFogCoordPointer(stride -1)";
}

// The conformance half above is only half the point: what the draw path needs
// is that none of those rejected values ever reach it. A working declaration
// is drawn, then broken every way at once, then drawn again - which on the
// unvalidated wrapper meant a -8 stride cast to size_t inside
// gather_client_arrays.
TEST_F(PointerValidationTest, RejectedCallLeavesTheArrayStateIntact) {
    auto clear_color = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glClearColor");
    auto clear = Get<void (*)(GLbitfield)>("glClear");
    auto draw_arrays = Get<void (*)(GLenum, GLint, GLsizei)>("glDrawArrays");
    auto finish = Get<void (*)()>("glFinish");
    auto enable_client_state = Get<void (*)(GLenum)>("glEnableClientState");
    auto disable_client_state = Get<void (*)(GLenum)>("glDisableClientState");
    auto read_pixels =
        Get<void (*)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*)>("glReadPixels");
    ASSERT_NE(read_pixels, nullptr);
    PixelProbe probe(read_pixels);

    // Magenta: its own mirror image under an R/B swap, so llvmpipe's BGRA
    // readback quirk cannot turn a correct draw into a wrong one.
    static const GLfloat quad[] = {-1, -1, 1, -1, 1, 1, -1, -1, 1, 1, -1, 1};
    static const GLubyte magenta[] = {
        255, 0, 255, 255, 255, 0, 255, 255, 255, 0, 255, 255,
        255, 0, 255, 255, 255, 0, 255, 255, 255, 0, 255, 255,
    };

    vertex_pointer_(2, GL_FLOAT_, 0, quad);
    color_pointer_(4, GL_UNSIGNED_BYTE_, 0, magenta);
    enable_client_state(GL_VERTEX_ARRAY_);
    enable_client_state(GL_COLOR_ARRAY_);
    clear_color(0.0f, 0.0f, 0.0f, 1.0f);

    const auto draw = [&] {
        clear(GL_COLOR_BUFFER_BIT_);
        draw_arrays(GL_TRIANGLES_, 0, 6);
        finish();
    };
    const auto is_magenta = [&](int x, int y) {
        const PixelProbe::Rgba c = probe.At(x, y);
        return c.r > 200 && c.g <= 50 && c.b > 200;
    };

    draw();
    ASSERT_TRUE(is_magenta(32, 32)) << "baseline declaration must draw";

    // Every rejected form of the same two declarations. None may take.
    vertex_pointer_(2, GL_FLOAT_, -8, quad);
    (void)get_error_();
    vertex_pointer_(7, GL_FLOAT_, 0, quad);
    (void)get_error_();
    vertex_pointer_(2, GL_UNSIGNED_BYTE_, 0, quad);
    (void)get_error_();
    color_pointer_(4, GL_UNSIGNED_BYTE_, -12, magenta);
    (void)get_error_();
    color_pointer_(9, GL_UNSIGNED_BYTE_, 0, magenta);
    (void)get_error_();

    draw();
    EXPECT_TRUE(is_magenta(32, 32))
        << "a rejected gl*Pointer must leave the previous declaration in force";
    EXPECT_TRUE(is_magenta(4, 4) && is_magenta(59, 59))
        << "the whole quad, not just its middle, must still be drawn";

    disable_client_state(GL_COLOR_ARRAY_);
    disable_client_state(GL_VERTEX_ARRAY_);
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

} // namespace
