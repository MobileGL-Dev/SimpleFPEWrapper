// SimpleFPEWrapper - tests/gtest_delete_buffer_attrib_cache.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// send_vertex_attributes skips re-issuing a vertex binding or an attribute
// pointer when its cache says the backend already has exactly that
// declaration, and both halves of that cache name a BUFFER
// (fpe_vertex_binding_buffer, fpe_vertex_attributes[].array_buffer).
// Deleting a buffer drops the backend's references to it and returns the name
// to the pool, so a later object handed the same name would be matched by a
// cache entry that describes the dead one and the rebind would be skipped -
// the wrapper's VAO left pointing at whatever the driver made of it.
//
// list_capture.cpp performs exactly this scrub for the buffers the WRAPPER
// deletes, with a comment saying why; the application-facing glDeleteBuffers
// never got it (plans/16 H2). Same disease as the internal_buffers half fixed
// in gtest_buffer_name_reuse: "MC 1.12 with Use VBOs draws random garbage
// vertices".
//
// Driver name-recycling policy cannot be forced from a test, so the scrub is
// asserted through a test hook (deterministic on every driver) and the
// end-to-end recycled-name draw is checked only when the driver happens to
// hand the name back.

#include "sfpew_gtest.h"

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
constexpr GLenum GL_FLOAT_ = 0x1406;
constexpr GLenum GL_NO_ERROR_ = 0;
constexpr GLenum GL_ARRAY_BUFFER_ = 0x8892;
constexpr GLenum GL_STATIC_DRAW_ = 0x88E4;
constexpr GLenum GL_VERTEX_ARRAY_ = 0x8074;
constexpr GLenum GL_COLOR_ARRAY_ = 0x8076;

// Two triangles covering the viewport; the color travels with the vertices so
// a stale source shows up as the wrong color as well as the wrong shape.
constexpr GLfloat kMagentaQuad[6][6] = {
    {-1, -1, 1, 0, 1, 1}, {1, -1, 1, 0, 1, 1}, {1, 1, 1, 0, 1, 1},
    {-1, -1, 1, 0, 1, 1}, {1, 1, 1, 0, 1, 1},  {-1, 1, 1, 0, 1, 1},
};
constexpr GLfloat kGreenQuad[6][6] = {
    {-1, -1, 0, 1, 0, 1}, {1, -1, 0, 1, 0, 1}, {1, 1, 0, 1, 0, 1},
    {-1, -1, 0, 1, 0, 1}, {1, 1, 0, 1, 0, 1},  {-1, 1, 0, 1, 0, 1},
};

using DeleteBufferAttribCacheTest = ContextTest;

TEST_F(DeleteBufferAttribCacheTest, DeletingABufferScrubsTheAttributeCache) {
    auto clear_color = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glClearColor");
    auto clear = Get<void (*)(GLbitfield)>("glClear");
    auto draw_arrays = Get<void (*)(GLenum, GLint, GLsizei)>("glDrawArrays");
    auto finish = Get<void (*)()>("glFinish");
    auto get_error = Get<GLenum (*)()>("glGetError");
    auto gen_buffers = Get<void (*)(GLsizei, GLuint*)>("glGenBuffers");
    auto delete_buffers = Get<void (*)(GLsizei, const GLuint*)>("glDeleteBuffers");
    auto bind_buffer = Get<void (*)(GLenum, GLuint)>("glBindBuffer");
    auto buffer_data = Get<void (*)(GLenum, GLsizeiptr, const void*, GLenum)>("glBufferData");
    auto enable_client_state = Get<void (*)(GLenum)>("glEnableClientState");
    auto disable_client_state = Get<void (*)(GLenum)>("glDisableClientState");
    auto vertex_pointer = Get<void (*)(GLint, GLenum, GLsizei, const void*)>("glVertexPointer");
    auto color_pointer = Get<void (*)(GLint, GLenum, GLsizei, const void*)>("glColorPointer");
    auto read_pixels =
        Get<void (*)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*)>("glReadPixels");
    ASSERT_NE(read_pixels, nullptr);
    PixelProbe probe(read_pixels);

    auto cache_holds =
        Dlsym<int (*)(GLuint)>("sfpewVertexAttributeCacheHoldsBufferForTest");
    ASSERT_NE(cache_holds, nullptr)
        << "sfpewVertexAttributeCacheHoldsBufferForTest not exported";

    const GLsizei stride = 6 * static_cast<GLsizei>(sizeof(GLfloat));
    const auto declare = [&] {
        vertex_pointer(2, GL_FLOAT_, stride, nullptr);
        color_pointer(4, GL_FLOAT_, stride, reinterpret_cast<const void*>(2 * sizeof(GLfloat)));
    };
    const auto draw = [&] {
        clear(GL_COLOR_BUFFER_BIT_);
        draw_arrays(GL_TRIANGLES_, 0, 6);
        finish();
    };

    clear_color(0.0f, 0.0f, 0.0f, 1.0f);
    enable_client_state(GL_VERTEX_ARRAY_);
    enable_client_state(GL_COLOR_ARRAY_);

    GLuint vbo = 0;
    gen_buffers(1, &vbo);
    ASSERT_NE(vbo, 0u);
    bind_buffer(GL_ARRAY_BUFFER_, vbo);
    buffer_data(GL_ARRAY_BUFFER_, static_cast<GLsizeiptr>(sizeof kMagentaQuad), kMagentaQuad,
                GL_STATIC_DRAW_);
    declare();
    draw();

    const PixelProbe::Rgba magenta = probe.At(32, 32);
    ASSERT_TRUE(magenta.r > 200 && magenta.g <= 50 && magenta.b > 200)
        << "baseline VBO draw must be magenta";
    // Sensitivity check: without this the scrub assertion below could pass
    // simply because nothing ever cached the buffer.
    ASSERT_EQ(cache_holds(vbo), 1)
        << "the draw must have left the attribute cache naming this buffer - "
           "the test cannot observe the state it exists to guard";

    delete_buffers(1, &vbo);
    EXPECT_EQ(cache_holds(vbo), 0)
        << "glDeleteBuffers must scrub the attribute cache entries naming the deleted buffer";

    // If this driver recycles the name, the whole mechanism is observable in
    // pixels: the new object's contents must reach the screen.
    GLuint again = 0;
    gen_buffers(1, &again);
    bind_buffer(GL_ARRAY_BUFFER_, again);
    buffer_data(GL_ARRAY_BUFFER_, static_cast<GLsizeiptr>(sizeof kGreenQuad), kGreenQuad,
                GL_STATIC_DRAW_);
    declare();
    draw();
    const PixelProbe::Rgba green = probe.At(32, 32);
    EXPECT_TRUE(green.r <= 50 && green.g > 200 && green.b <= 50)
        << (again == vbo ? "the recycled name must source from the NEW object"
                         : "a fresh buffer must source from its own contents")
        << ": (" << (int)green.r << ',' << (int)green.g << ',' << (int)green.b << ')';

    delete_buffers(1, &again);
    disable_client_state(GL_COLOR_ARRAY_);
    disable_client_state(GL_VERTEX_ARRAY_);
    bind_buffer(GL_ARRAY_BUFFER_, 0);
    EXPECT_EQ(get_error(), GL_NO_ERROR_);
}

} // namespace
