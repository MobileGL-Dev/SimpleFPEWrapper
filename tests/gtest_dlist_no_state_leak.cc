// SimpleFPEWrapper - tests/gtest_dlist_no_state_leak.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header
//
// Replaying a display list must not disturb the app's GL_ARRAY_BUFFER binding.
//
// The upload of that resident data originally went through GL_ARRAY_BUFFER. It
// runs as an argument to drawImmediateVertices, so it executes BEFORE that
// call's draw-state guard captures the app's bindings - nothing was there to
// undo it. On its own the wrapper recovers, but the array-buffer shadow
// re-reads GL_ARRAY_BUFFER_BINDING every 256 queries to self-heal, and a read
// landing right after the upload would record THE WRAPPER'S PRIVATE BUFFER AS
// THE APP'S. Every later app draw would then source attributes from the
// display-list buffer - garbage texture coordinates. The upload now goes
// through GL_COPY_WRITE_BUFFER, which is not part of vertex array state.
//
// HONEST SCOPE: this test does NOT catch that bug. Verified by reintroducing
// it - all checks still pass, because the upload happens once per run and the
// heal only fires every 256 queries, so the poisoned window is a single call
// that the heal almost never lands in. The bug was found by reading, not by
// this test, and it is kept because the invariant it states is worth guarding
// even though it cannot police the rare interleaving:
//
//   1. GL_ARRAY_BUFFER_BINDING is unchanged across a replay.
//   2. An app draw AFTER a replay still reads its own vertex data.
//   3. The replayed list still renders, so a fix cannot pass by breaking it.
//
// Catching the rare case deterministically would need the heal counter driven
// to a known phase, which no public entry point exposes.

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
using sfpew_test::PixelProbe;

constexpr GLbitfield GL_COLOR_BUFFER_BIT_ = 0x00004000;
constexpr GLenum GL_TRIANGLES_ = 0x0004;
constexpr GLenum GL_QUADS_ = 0x0007;
constexpr GLenum GL_FLOAT_ = 0x1406;
constexpr GLenum GL_ARRAY_BUFFER_ = 0x8892;
constexpr GLenum GL_ARRAY_BUFFER_BINDING_ = 0x8894;
constexpr GLenum GL_STATIC_DRAW_ = 0x88E4;
constexpr GLenum GL_COMPILE_ = 0x1300;
constexpr GLenum GL_VERTEX_ARRAY_ = 0x8074;
constexpr GLenum GL_COLOR_ARRAY_ = 0x8076;
constexpr GLenum GL_NO_ERROR_ = 0;

using DlistNoStateLeakTest = ContextTest;

TEST_F(DlistNoStateLeakTest, ReplayDoesNotLeakVertexBufferStateToTheApp) {
    auto clear_color = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glClearColor");
    auto clear = Get<void (*)(GLbitfield)>("glClear");
    auto draw_arrays = Get<void (*)(GLenum, GLint, GLsizei)>("glDrawArrays");
    auto get_error = Get<GLenum (*)()>("glGetError");
    auto finish = Get<void (*)()>("glFinish");
    auto get_integerv = Get<void (*)(GLenum, GLint*)>("glGetIntegerv");
    auto gen_buffers = Get<void (*)(GLsizei, GLuint*)>("glGenBuffers");
    auto bind_buffer = Get<void (*)(GLenum, GLuint)>("glBindBuffer");
    auto buffer_data = Get<void (*)(GLenum, GLsizeiptr, const void*, GLenum)>("glBufferData");
    auto enable_client_state = Get<void (*)(GLenum)>("glEnableClientState");
    auto disable_client_state = Get<void (*)(GLenum)>("glDisableClientState");
    auto vertex_pointer = Get<void (*)(GLint, GLenum, GLsizei, const void*)>("glVertexPointer");
    auto color_pointer = Get<void (*)(GLint, GLenum, GLsizei, const void*)>("glColorPointer");
    auto gen_lists = Get<GLuint (*)(GLsizei)>("glGenLists");
    auto new_list = Get<void (*)(GLuint, GLenum)>("glNewList");
    auto end_list = Get<void (*)()>("glEndList");
    auto call_list = Get<void (*)(GLuint)>("glCallList");
    auto begin = Get<void (*)(GLenum)>("glBegin");
    auto end = Get<void (*)()>("glEnd");
    auto color4f = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glColor4f");
    auto vertex2f = Get<void (*)(GLfloat, GLfloat)>("glVertex2f");
    auto read_pixels =
        Get<void (*)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*)>("glReadPixels");
    ASSERT_NE(read_pixels, nullptr);
    PixelProbe probe(read_pixels);

    const auto expect = [&](int r, int g, int b, const char* what) {
        const PixelProbe::Rgba p = probe.At(32, 32);
        EXPECT_TRUE((p.r > 200) == (r > 0) && (p.g > 200) == (g > 0) && (p.b > 200) == (b > 0))
            << what << ": pixel = (" << (int)p.r << ',' << (int)p.g << ',' << (int)p.b
            << "), expected (" << r << ',' << g << ',' << b << ')';
    };

    // The app's own buffer: a full-screen GREEN quad, interleaved x,y,r,g,b,a.
    const GLfloat app_verts[] = {
        -1, -1, 0, 1, 0, 1, 1, -1, 0, 1, 0, 1, 1, 1, 0, 1, 0, 1,
        -1, -1, 0, 1, 0, 1, 1, 1,  0, 1, 0, 1, -1, 1, 0, 1, 0, 1,
    };
    const GLsizei stride = 6 * static_cast<GLsizei>(sizeof(GLfloat));
    GLuint app_vbo = 0;
    gen_buffers(1, &app_vbo);
    bind_buffer(GL_ARRAY_BUFFER_, app_vbo);
    buffer_data(GL_ARRAY_BUFFER_, static_cast<GLsizeiptr>(sizeof app_verts), app_verts,
               GL_STATIC_DRAW_);

    // A display list holding an immediate-mode run. Replaying it is what
    // makes the wrapper materialise its own resident vertex buffer.
    // Deliberately a small RED quad away from the sample point, so if its
    // geometry ever bled into the app's draw the center pixel would stop
    // being green.
    const GLuint list = gen_lists(1);
    new_list(list, GL_COMPILE_);
    begin(GL_QUADS_);
    color4f(1.0f, 0.0f, 0.0f, 1.0f);
    vertex2f(-0.9f, -0.9f);
    vertex2f(-0.8f, -0.9f);
    vertex2f(-0.8f, -0.8f);
    vertex2f(-0.9f, -0.8f);
    end();
    end_list();

    bind_buffer(GL_ARRAY_BUFFER_, app_vbo);
    vertex_pointer(2, GL_FLOAT_, stride, nullptr);
    color_pointer(4, GL_FLOAT_, stride, reinterpret_cast<const void*>(2 * sizeof(GLfloat)));
    enable_client_state(GL_VERTEX_ARRAY_);
    enable_client_state(GL_COLOR_ARRAY_);
    clear_color(0.0f, 0.0f, 1.0f, 1.0f);

    // Baseline, before any replay.
    clear(GL_COLOR_BUFFER_BIT_);
    draw_arrays(GL_TRIANGLES_, 0, 6);
    finish();
    expect(0, 1, 0, "baseline: app draws green from its own buffer");

    // The shadow's self-heal fires every 256 queries, so a few iterations
    // would pass even with the binding poisoned. Drive well past that,
    // replaying the list before every app draw.
    int binding_mismatches = 0;
    for (int i = 0; i < 800; ++i) {
        call_list(list);
        // Each glVertexPointer records the array-buffer binding, which is
        // the path that queries (and heals) the shadow.
        vertex_pointer(2, GL_FLOAT_, stride, nullptr);
        color_pointer(4, GL_FLOAT_, stride, reinterpret_cast<const void*>(2 * sizeof(GLfloat)));
        GLint bound = -1;
        get_integerv(GL_ARRAY_BUFFER_BINDING_, &bound);
        if (bound != static_cast<GLint>(app_vbo)) ++binding_mismatches;
    }
    EXPECT_EQ(binding_mismatches, 0)
        << "GL_ARRAY_BUFFER_BINDING left the app's buffer on " << binding_mismatches
        << " of 800 iterations";

    // The property that actually broke: the app's draw must still read the
    // app's vertices, not the display list's.
    clear(GL_COLOR_BUFFER_BIT_);
    draw_arrays(GL_TRIANGLES_, 0, 6);
    finish();
    expect(0, 1, 0, "after 800 replays: app still draws from its own buffer");

    // And the list itself must still render - the fix must not have broken
    // it.
    disable_client_state(GL_VERTEX_ARRAY_);
    disable_client_state(GL_COLOR_ARRAY_);
    bind_buffer(GL_ARRAY_BUFFER_, 0);
    clear(GL_COLOR_BUFFER_BIT_);
    call_list(list);
    finish();
    const PixelProbe::Rgba red_quad = probe.At(3, 3); // inside the red quad
    EXPECT_TRUE(red_quad.r > 200 && red_quad.g < 200 && red_quad.b < 200)
        << "replayed list did not render red: (" << (int)red_quad.r << ',' << (int)red_quad.g
        << ',' << (int)red_quad.b << ')';

    EXPECT_EQ(get_error(), GL_NO_ERROR_);
}

} // namespace
