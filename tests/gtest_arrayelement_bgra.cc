// SimpleFPEWrapper - tests/gtest_arrayelement_bgra.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// glColorPointer(GL_BGRA, ...) records a component ORDER, not a component
// count: the wrapper normalizes it to size 4 plus a `bgra` flag, and applies
// the order where it can be expressed (the driver's own GL_BGRA on desktop
// GL, a swizzle in the generated shader on GLES). glArrayElement consulted
// neither flag and handed glColor4f the components in memory order, so the
// same array came out with red and blue exchanged depending on whether it was
// drawn indexed or with glDrawArrays (plans/16 L1).
//
// This case IS about component order, so it asserts channels rather than
// "something was drawn". The load-bearing assertions are relationships, not
// absolute triples, because llvmpipe's BGRA readback quirk can exchange R and
// B in the readback itself:
//   1. the indexed render must equal the glDrawArrays render of the same
//      array - that is the fix, and any readback quirk cancels out;
//   2. declaring the identical bytes as plain RGBA must give the indexed
//      render's R and B exchanged - which is what proves the order was
//      APPLIED rather than that both paths ignore it in the same way.
// The absolute check is kept as a third, weaker assertion.

#include "sfpew_gtest.h"

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
constexpr GLenum GL_BGRA_ = 0x80E1;
constexpr GLenum GL_VERTEX_ARRAY_ = 0x8074;
constexpr GLenum GL_COLOR_ARRAY_ = 0x8076;

constexpr GLfloat kQuad[12] = {-1, -1, 1, -1, 1, 1, -1, -1, 1, 1, -1, 1};
// Read as B,G,R,A this is red; read as R,G,B,A it is blue. Alpha is opaque
// either way, and green stays zero so the swap is the only difference.
constexpr GLubyte kPacked[24] = {
    0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255,
    0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255,
};

class ArrayElementBgraTest : public ContextTest {
protected:
    ArrayElementBgraTest() : ContextTest(sfpew_test::Backend::GLES3, kSize) {}

    void SetUp() override {
        ContextTest::SetUp();
        if (::testing::Test::IsSkipped()) return;

        clear_color_ = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glClearColor");
        clear_ = Get<void (*)(GLbitfield)>("glClear");
        finish_ = Get<void (*)()>("glFinish");
        get_error_ = Get<GLenum (*)()>("glGetError");
        begin_ = Get<void (*)(GLenum)>("glBegin");
        end_ = Get<void (*)()>("glEnd");
        array_element_ = Get<void (*)(GLint)>("glArrayElement");
        draw_arrays_ = Get<void (*)(GLenum, GLint, GLsizei)>("glDrawArrays");
        enable_client_state_ = Get<void (*)(GLenum)>("glEnableClientState");
        disable_client_state_ = Get<void (*)(GLenum)>("glDisableClientState");
        vertex_pointer_ = Get<void (*)(GLint, GLenum, GLsizei, const void*)>("glVertexPointer");
        color_pointer_ = Get<void (*)(GLint, GLenum, GLsizei, const void*)>("glColorPointer");
        read_pixels_ =
            Get<void (*)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*)>("glReadPixels");
        ASSERT_NE(array_element_, nullptr);
        ASSERT_NE(read_pixels_, nullptr);

        vertex_pointer_(2, GL_FLOAT_, 0, kQuad);
        enable_client_state_(GL_VERTEX_ARRAY_);
        enable_client_state_(GL_COLOR_ARRAY_);
        clear_color_(0.0f, 0.0f, 0.0f, 1.0f);
    }

    void TearDown() override {
        if (disable_client_state_ != nullptr) {
            disable_client_state_(GL_COLOR_ARRAY_);
            disable_client_state_(GL_VERTEX_ARRAY_);
        }
        ContextTest::TearDown();
    }

    void Capture(std::vector<GLubyte>* out) {
        out->resize(static_cast<size_t>(kSize) * kSize * 4);
        finish_();
        read_pixels_(0, 0, kSize, kSize, GL_RGBA_, GL_UNSIGNED_BYTE_, out->data());
    }

    void DrawIndexed() {
        clear_(GL_COLOR_BUFFER_BIT_);
        begin_(GL_TRIANGLES_);
        for (GLint i = 0; i < 6; ++i) array_element_(i);
        end_();
    }

    static const GLubyte* Center(const std::vector<GLubyte>& fb) {
        return fb.data() + (static_cast<size_t>(kSize / 2) * kSize + kSize / 2) * 4;
    }

    void (*clear_color_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*clear_)(GLbitfield) = nullptr;
    void (*finish_)() = nullptr;
    GLenum (*get_error_)() = nullptr;
    void (*begin_)(GLenum) = nullptr;
    void (*end_)() = nullptr;
    void (*array_element_)(GLint) = nullptr;
    void (*draw_arrays_)(GLenum, GLint, GLsizei) = nullptr;
    void (*enable_client_state_)(GLenum) = nullptr;
    void (*disable_client_state_)(GLenum) = nullptr;
    void (*vertex_pointer_)(GLint, GLenum, GLsizei, const void*) = nullptr;
    void (*color_pointer_)(GLint, GLenum, GLsizei, const void*) = nullptr;
    void (*read_pixels_)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*) = nullptr;
};

TEST_F(ArrayElementBgraTest, IndexedDrawAppliesTheDeclaredComponentOrder) {
    std::vector<GLubyte> bgra_direct, bgra_indexed, rgba_indexed;

    color_pointer_(GL_BGRA_, GL_UNSIGNED_BYTE_, 0, kPacked);
    clear_(GL_COLOR_BUFFER_BIT_);
    draw_arrays_(GL_TRIANGLES_, 0, 6);
    Capture(&bgra_direct);

    color_pointer_(GL_BGRA_, GL_UNSIGNED_BYTE_, 0, kPacked);
    DrawIndexed();
    Capture(&bgra_indexed);

    color_pointer_(4, GL_UNSIGNED_BYTE_, 0, kPacked);
    DrawIndexed();
    Capture(&rgba_indexed);

    EXPECT_EQ(bgra_indexed, bgra_direct)
        << "glArrayElement over a GL_BGRA color array must match glDrawArrays over it";

    const GLubyte* bgra = Center(bgra_indexed);
    const GLubyte* rgba = Center(rgba_indexed);
    EXPECT_EQ(bgra[0], rgba[2]) << "indexed BGRA must be the indexed RGBA render's R/B swap";
    EXPECT_EQ(bgra[2], rgba[0]);
    EXPECT_EQ(bgra[1], rgba[1]) << "only R and B move";

    // Weaker than the two relations above (a driver that swaps R and B in the
    // readback would defeat it), kept because it names the expected color.
    EXPECT_GT(bgra[0], 200) << "B,G,R,A = 0,0,255,255 is red";
    EXPECT_LE(bgra[1], 50);
    EXPECT_LE(bgra[2], 50);
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

} // namespace
