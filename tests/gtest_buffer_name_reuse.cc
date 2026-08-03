// SimpleFPEWrapper - tests/gtest_buffer_name_reuse.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// A buffer name the wrapper once owned must stop being treated as
// wrapper-internal the moment it is deleted.
//
// The self-adoption guards (internal_buffers) teach the array-buffer shadow
// to report a wrapper-owned binding as zero. Names, however, are recycled:
// when the wrapper deletes one of its buffers (an immediate-ring replacement,
// a captured static VBO dying with its display list), the GL name goes back
// to the pool and the app's next glGenBuffers can receive it. A stale
// internal_buffers entry then makes the shadow zero out the APP'S OWN VBO,
// and every fixed-function draw sourcing from it silently switches its
// attribute source to the wrapper's ring - garbage vertices.
//
// This is not hypothetical: it shipped in b3dd356 and was reported within
// hours as random vertex corruption in MC 1.12 with "Use VBOs" on (chunk VBO
// names recycle constantly there), captured in
// RDC/Minecraft/1.12-Optifine/vertex-bug.rdc.
//
// Driver name-recycling policy cannot be forced from a test (NVIDIA hands
// out fresh names), so the recycled-name collision is staged directly through
// test hooks instead of hoping for it:
//   1. an app VBO draws correctly (baseline)
//   2. sfpewMarkBufferInternalForTest() poisons its name, and after driving
//      the shadow past its heal interval the SAME draw must go wrong - this
//      is the bug mechanism made visible, and it doubles as the test's own
//      sensitivity proof (no hand-broken build needed)
//   3. deleting the buffer through the wrapper must shed the internal
//      identity (sfpewBufferIsInternalForTest() -> false) - the actual fix
//   4. a fresh buffer reusing that name draws correctly again

#include "sfpew_gtest.h"

#include <optional>

namespace {

using sfpew_test::ContextTest;
using sfpew_test::GLbitfield;
using sfpew_test::GLenum;
using sfpew_test::GLfloat;
using sfpew_test::GLint;
using sfpew_test::GLsizei;
using sfpew_test::GLsizeiptr;
using sfpew_test::GLuint;
using sfpew_test::GLubyte;
using sfpew_test::PixelProbe;

constexpr GLbitfield GL_COLOR_BUFFER_BIT_ = 0x00004000;
constexpr GLenum GL_TRIANGLES_ = 0x0004;
constexpr GLenum GL_FLOAT_ = 0x1406;
constexpr GLenum GL_ARRAY_BUFFER_ = 0x8892;
constexpr GLenum GL_STATIC_DRAW_ = 0x88E4;
constexpr GLenum GL_VERTEX_ARRAY_ = 0x8074;
constexpr GLenum GL_COLOR_ARRAY_ = 0x8076;
constexpr GLenum GL_NO_ERROR_ = 0;

using BufferNameReuseTest = ContextTest;

TEST_F(BufferNameReuseTest, RecycledNamesShedTheirWrapperInternalIdentity) {
    auto clear_color = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glClearColor");
    auto clear = Get<void (*)(GLbitfield)>("glClear");
    auto draw_arrays = Get<void (*)(GLenum, GLint, GLsizei)>("glDrawArrays");
    auto get_error = Get<GLenum (*)()>("glGetError");
    auto finish = Get<void (*)()>("glFinish");
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

    auto mark_internal = Dlsym<void (*)(GLuint)>("sfpewMarkBufferInternalForTest");
    auto is_internal = Dlsym<int (*)(GLuint)>("sfpewBufferIsInternalForTest");
    auto logical_binding = Dlsym<GLuint (*)(void)>("sfpewLogicalArrayBufferBindingForTest");
    ASSERT_NE(mark_internal, nullptr) << "sfpewMarkBufferInternalForTest not exported";
    ASSERT_NE(is_internal, nullptr) << "sfpewBufferIsInternalForTest not exported";
    ASSERT_NE(logical_binding, nullptr) << "sfpewLogicalArrayBufferBindingForTest not exported";

    clear_color(0.0f, 0.0f, 1.0f, 1.0f);

    static const GLfloat green_quad[] = {
        -1, -1, 0, 1, 0, 1, 1, -1, 0, 1, 0, 1, 1, 1, 0, 1, 0, 1,
        -1, -1, 0, 1, 0, 1, 1, 1,  0, 1, 0, 1, -1, 1, 0, 1, 0, 1,
    };
    const GLsizei stride = 6 * static_cast<GLsizei>(sizeof(GLfloat));
    enable_client_state(GL_VERTEX_ARRAY_);
    enable_client_state(GL_COLOR_ARRAY_);

    // Draws the currently configured VBO-backed layout after driving the
    // array-buffer shadow well past its ~256-query heal interval (each
    // gl*Pointer records the binding, i.e. queries the shadow).
    const auto draw_after_heal = [&] {
        for (int spin = 0; spin < 600; ++spin)
            vertex_pointer(2, GL_FLOAT_, stride, nullptr);
        color_pointer(4, GL_FLOAT_, stride, reinterpret_cast<const void*>(2 * sizeof(GLfloat)));
        clear(GL_COLOR_BUFFER_BIT_);
        draw_arrays(GL_TRIANGLES_, 0, 6);
        finish();
    };
    const auto centre_is_green = [&] {
        const PixelProbe::Rgba p = probe.At(32, 32);
        return p.g > 200 && p.r < 100 && p.b < 100;
    };

    GLuint vbo = 0;
    gen_buffers(1, &vbo);
    bind_buffer(GL_ARRAY_BUFFER_, vbo);
    buffer_data(GL_ARRAY_BUFFER_, static_cast<GLsizeiptr>(sizeof green_quad), green_quad,
               GL_STATIC_DRAW_);

    // 1. Baseline: the app's VBO draws its own vertices.
    draw_after_heal();
    EXPECT_TRUE(centre_is_green()) << "baseline VBO-backed draw must be green";

    // 2. Poison the name the way a stale registry entry would, and prove the
    //    mechanism at the shadow layer: after the heal interval the logical
    //    binding collapses to zero even though the app's VBO is bound. This
    //    is the test's built-in sensitivity check. (Drawing in this state
    //    dereferences buffer offsets as client pointers and crashes - the
    //    real-world severity - so the assertion stops at the shadow.)
    mark_internal(vbo);
    bind_buffer(GL_ARRAY_BUFFER_, vbo); // reseed the shadow, then let heal poison it
    for (int spin = 0; spin < 600; ++spin)
        vertex_pointer(2, GL_FLOAT_, stride, nullptr);
    EXPECT_EQ(logical_binding(), 0u)
        << "a stale internal mark must zero the app's binding after the heal - "
           "the test cannot observe the mechanism it exists to guard";

    // 3. The fix: deleting the buffer through the wrapper sheds the internal
    //    identity, because the name is about to be recycled.
    delete_buffers(1, &vbo);
    EXPECT_EQ(is_internal(vbo), 0) << "deleted name must shed the wrapper-internal identity";

    // 4. A recycled/fresh name must draw correctly again.
    GLuint again = 0;
    gen_buffers(1, &again);
    bind_buffer(GL_ARRAY_BUFFER_, again);
    buffer_data(GL_ARRAY_BUFFER_, static_cast<GLsizeiptr>(sizeof green_quad), green_quad,
               GL_STATIC_DRAW_);
    draw_after_heal();
    EXPECT_TRUE(centre_is_green()) << "a fresh buffer (name " << again
                                   << ") must draw correctly after the cycle";
    delete_buffers(1, &again);

    disable_client_state(GL_VERTEX_ARRAY_);
    disable_client_state(GL_COLOR_ARRAY_);
    bind_buffer(GL_ARRAY_BUFFER_, 0);
    EXPECT_EQ(get_error(), GL_NO_ERROR_);
}

} // namespace
