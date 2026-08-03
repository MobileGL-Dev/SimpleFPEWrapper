// SimpleFPEWrapper - tests/gtest_arraybuffer_heal.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// The wrapper shadows GL_ARRAY_BUFFER_BINDING so fixed-function draws can decide
// where to source attributes without a synchronous query per draw. An app that
// binds GL_ARRAY_BUFFER without going through the wrapper - JNI dispatching
// straight to the driver, or another wrapper layered underneath - desynchronizes
// that shadow.
//
// The program and VAO shadows already re-read the truth every 256 queries and
// self-heal. The array-buffer shadow did not: once seeded it answered from
// itself for the rest of the process, so a single bypassed bind stayed wrong
// forever and every later fixed-function draw sourced attributes from the wrong
// buffer.
//
// This test resolves glBindBuffer from the BACKEND (libGLESv2) rather than the
// wrapper and binds through it, so the wrapper never sees the call.
//
// SCOPE, stated precisely because it is narrower than the name suggests: this
// verifies that a bypassed bind does not corrupt the wrapper's own
// fixed-function draws. It does NOT isolate the 256-query heal - it passes with
// the heal disabled, because the explicit wrapper-side glBindBuffer after the
// bypass reseeds the shadow directly. Deterministically observing the heal needs
// the app to draw from the bypassed binding with no intervening wrapper bind,
// which means a user-program passthrough draw whose glVertexAttribPointer was
// baked before the bypass - a harness this file does not build. The heal itself
// is justified by parity with the program and VAO shadows (which already
// reconcile every 256 queries) and by the attribute-buffer bind elision now
// depending on this shadow being right; see plans/12.

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

using ArrayBufferHealTest = ContextTest;

TEST_F(ArrayBufferHealTest, ShadowSurvivesABypassedBind) {
    auto clear_color = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glClearColor");
    auto clear = Get<void (*)(GLbitfield)>("glClear");
    auto draw_arrays = Get<void (*)(GLenum, GLint, GLsizei)>("glDrawArrays");
    auto get_error = Get<GLenum (*)()>("glGetError");
    auto finish = Get<void (*)()>("glFinish");
    auto gen_buffers = Get<void (*)(GLsizei, GLuint*)>("glGenBuffers");
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

    // The bypass: glBindBuffer straight from the backend, skipping the wrapper.
    void* backend = dlopen("libGLESv2.so.2", RTLD_NOW | RTLD_LOCAL);
    if (backend == nullptr) backend = dlopen("libGLESv2.so", RTLD_NOW | RTLD_LOCAL);
    void (*raw_bind_buffer)(GLenum, GLuint) = nullptr;
    if (backend != nullptr)
        *(void**)(&raw_bind_buffer) = dlsym(backend, "glBindBuffer");
    if (raw_bind_buffer == nullptr)
        GTEST_SKIP() << "cannot resolve the backend's glBindBuffer for the bypass";

    // Interleaved x,y,r,g,b,a in both buffers.
    static const GLfloat green_quad[] = {
        -1, -1, 0, 1, 0, 1, 1, -1, 0, 1, 0, 1, 1, 1, 0, 1, 0, 1,
        -1, -1, 0, 1, 0, 1, 1, 1,  0, 1, 0, 1, -1, 1, 0, 1, 0, 1,
    };
    static const GLfloat red_sliver[] = {
        -0.9f, -0.9f, 1, 0, 0, 1, -0.8f, -0.9f, 1, 0, 0, 1, -0.85f, -0.8f, 1, 0, 0, 1,
        -0.9f, -0.9f, 1, 0, 0, 1, -0.8f, -0.9f, 1, 0, 0, 1, -0.85f, -0.8f, 1, 0, 0, 1,
    };
    const GLsizei stride = 6 * static_cast<GLsizei>(sizeof(GLfloat));

    GLuint buf_green = 0, buf_red = 0;
    gen_buffers(1, &buf_green);
    bind_buffer(GL_ARRAY_BUFFER_, buf_green);
    buffer_data(GL_ARRAY_BUFFER_, static_cast<GLsizeiptr>(sizeof green_quad), green_quad,
               GL_STATIC_DRAW_);
    gen_buffers(1, &buf_red);
    bind_buffer(GL_ARRAY_BUFFER_, buf_red);
    buffer_data(GL_ARRAY_BUFFER_, static_cast<GLsizeiptr>(sizeof red_sliver), red_sliver,
               GL_STATIC_DRAW_);

    // Settle on the green buffer THROUGH the wrapper, so its shadow is seeded
    // and correct. Offsets, so the draw sources from the bound buffer.
    bind_buffer(GL_ARRAY_BUFFER_, buf_green);
    vertex_pointer(2, GL_FLOAT_, stride, nullptr);
    color_pointer(4, GL_FLOAT_, stride, reinterpret_cast<const void*>(2 * sizeof(GLfloat)));
    enable_client_state(GL_VERTEX_ARRAY_);
    enable_client_state(GL_COLOR_ARRAY_);
    clear_color(0.0f, 0.0f, 1.0f, 1.0f);

    const auto expect = [&](int r, int g, int b, const char* what) {
        const PixelProbe::Rgba p = probe.At(32, 32);
        EXPECT_TRUE((p.r > 200) == (r > 0) && (p.g > 200) == (g > 0) && (p.b > 200) == (b > 0))
            << what << ": pixel = (" << (int)p.r << ',' << (int)p.g << ',' << (int)p.b
            << "), expected (" << r << ',' << g << ',' << b << ')';
    };

    clear(GL_COLOR_BUFFER_BIT_);
    draw_arrays(GL_TRIANGLES_, 0, 6);
    finish();
    expect(0, 1, 0, "baseline: wrapper-bound buffer draws green");

    // Now bypass: rebind to the green buffer behind the wrapper's back. The
    // value matches what the shadow already holds, so this alone changes
    // nothing - it is the control for the step after it.
    raw_bind_buffer(GL_ARRAY_BUFFER_, buf_green);
    clear(GL_COLOR_BUFFER_BIT_);
    draw_arrays(GL_TRIANGLES_, 0, 6);
    finish();
    expect(0, 1, 0, "control: a bypassed bind to the same buffer is harmless");

    // The real bypass: the backend now has bufRed bound while the wrapper's
    // shadow still says bufGreen. Drive well past the 256-query heal interval
    // with calls that consult the shadow, then confirm the wrapper has
    // reconciled and draws agree with the backend's actual binding again.
    raw_bind_buffer(GL_ARRAY_BUFFER_, buf_red);
    for (int i = 0; i < 600; ++i) {
        // glVertexPointer records the array-buffer binding per pointer, which
        // is exactly the path that reads the shadow.
        vertex_pointer(2, GL_FLOAT_, stride, nullptr);
    }

    // Re-point through the wrapper so the layout is unambiguous, then draw.
    // Whatever the wrapper believes must now match the driver: binding bufRed
    // through the wrapper and drawing must produce the red sliver, which misses
    // the center and leaves the clear color there.
    bind_buffer(GL_ARRAY_BUFFER_, buf_red);
    vertex_pointer(2, GL_FLOAT_, stride, nullptr);
    color_pointer(4, GL_FLOAT_, stride, reinterpret_cast<const void*>(2 * sizeof(GLfloat)));
    clear(GL_COLOR_BUFFER_BIT_);
    draw_arrays(GL_TRIANGLES_, 0, 6);
    finish();
    expect(0, 0, 1, "post-bypass: the red buffer's sliver misses the center");

    // And back to green, to show the shadow tracks both directions.
    bind_buffer(GL_ARRAY_BUFFER_, buf_green);
    vertex_pointer(2, GL_FLOAT_, stride, nullptr);
    color_pointer(4, GL_FLOAT_, stride, reinterpret_cast<const void*>(2 * sizeof(GLfloat)));
    clear(GL_COLOR_BUFFER_BIT_);
    draw_arrays(GL_TRIANGLES_, 0, 6);
    finish();
    expect(0, 1, 0, "post-bypass: the green buffer draws green again");

    disable_client_state(GL_VERTEX_ARRAY_);
    disable_client_state(GL_COLOR_ARRAY_);
    bind_buffer(GL_ARRAY_BUFFER_, 0);
    EXPECT_EQ(get_error(), GL_NO_ERROR_);
}

} // namespace
