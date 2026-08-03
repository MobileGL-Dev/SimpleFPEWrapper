// SimpleFPEWrapper - tests/gtest_swap_flushes_batch.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// A frame must not end with geometry still buffered inside the wrapper.
//
// Small immediate-mode runs are accumulated so consecutive ones merge into one
// draw, and the batch is drained by sfpewEntryBarrier() from the next entry
// point that could observe it. Buffer swap was not one of those entry points -
// only eglMakeCurrent was wrapped - so a frame whose last drawing was
// immediate-mode left its batch pending ACROSS the swap. The next frame's first
// entry point then drained it into the new back buffer, where that frame's
// glClear erased it. Geometry drawn late in a frame vanished and the depth it
// wrote landed in the wrong frame: heavy flickering, the previous frame looking
// as though it was never cleared, wrong depth, and components rendering wrong.
//
// Two things are checked:
//
//   1. eglGetProcAddress("eglSwapBuffers") returns the WRAPPER's entry, not
//      libEGL's. If the routing regresses, the drain cannot happen at all.
//   2. Immediate geometry is pending before the swap and NOT pending after it.
//
// Check 2 asserts on sfpewImmediateBatchPendingForTest() rather than on pixels
// deliberately: a pbuffer performs no real buffer swap, so the frame-ordering
// error is invisible in the framebuffer here - with and without the fix the
// pixels come out identical. The batch predicate is the only thing that
// distinguishes them without a windowed, double-buffered surface.

#include "sfpew_gtest.h"

#include <optional>

namespace {

using sfpew_test::ContextTest;
using sfpew_test::GLbitfield;
using sfpew_test::GLenum;
using sfpew_test::GLfloat;
using sfpew_test::GLuint;

constexpr GLenum GL_QUADS_ = 0x0007;
constexpr GLbitfield GL_COLOR_BUFFER_BIT_ = 0x00004000;

using SwapFlushesBatchTest = ContextTest;

TEST_F(SwapFlushesBatchTest, BufferSwapLeavesNoGeometryBuffered) {
    auto pending_fn = Dlsym<int (*)(void)>("sfpewImmediateBatchPendingForTest");
    ASSERT_NE(pending_fn, nullptr) << "sfpewImmediateBatchPendingForTest not exported";

    // Route EGL through the wrapper the way a launcher does.
    using MakeCurrentFn = EGLBoolean (*)(EGLDisplay, EGLSurface, EGLSurface, EGLContext);
    using SwapFn = EGLBoolean (*)(EGLDisplay, EGLSurface);
    auto wrapper_make_current = Get<MakeCurrentFn>("eglMakeCurrent");
    auto wrapper_swap = Get<SwapFn>("eglSwapBuffers");
    ASSERT_NE(wrapper_make_current, nullptr) << "eglMakeCurrent not routed";
    ASSERT_NE(wrapper_swap, nullptr)
        << "eglGetProcAddress did not return the wrapper's eglSwapBuffers - "
           "the pending batch cannot be drained at swap";

    // The fixture already has the context current; re-assert it through the
    // wrapper so its own context tracking sees the call, the way a launcher
    // would have made it current in the first place.
    const EGLContext current = eglGetCurrentContext();
    ASSERT_TRUE(wrapper_make_current(display(), surface(), surface(), current))
        << "could not make the context current through the wrapper";

    auto begin = Get<void (*)(GLenum)>("glBegin");
    auto end = Get<void (*)()>("glEnd");
    auto vertex2f = Get<void (*)(GLfloat, GLfloat)>("glVertex2f");
    auto color4f = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glColor4f");
    auto clear_color = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glClearColor");
    auto clear = Get<void (*)(GLbitfield)>("glClear");
    ASSERT_NE(begin, nullptr);
    ASSERT_NE(end, nullptr);
    ASSERT_NE(vertex2f, nullptr);
    ASSERT_NE(color4f, nullptr);
    ASSERT_NE(clear_color, nullptr);
    ASSERT_NE(clear, nullptr);

    clear_color(0.0f, 0.0f, 1.0f, 1.0f);
    clear(GL_COLOR_BUFFER_BIT_);

    // End the frame with a small immediate-mode quad - the shape that goes
    // into the merge batch and is left pending for a later entry point to
    // drain.
    begin(GL_QUADS_);
    color4f(1.0f, 0.0f, 0.0f, 1.0f);
    vertex2f(-0.5f, -0.5f);
    vertex2f(0.5f, -0.5f);
    vertex2f(0.5f, 0.5f);
    vertex2f(-0.5f, 0.5f);
    end();

    if (!pending_fn()) {
        // Not a failure of the fix: if the batcher ever stops holding this
        // shape there is nothing for the swap to drain. Say so rather than
        // reporting a pass that proved nothing.
        GTEST_SKIP() << "this shape is no longer batched, so the swap has nothing to drain - "
                        "the test can no longer observe the behavior";
    }

    ASSERT_TRUE(wrapper_swap(display(), surface()))
        << "eglSwapBuffers through the wrapper failed";

    EXPECT_FALSE(pending_fn())
        << "geometry still buffered AFTER eglSwapBuffers - it will be drawn into the next "
           "frame and erased by its clear";

    // A second frame, to confirm the drain did not break ordinary drawing.
    clear(GL_COLOR_BUFFER_BIT_);
    begin(GL_QUADS_);
    color4f(0.0f, 1.0f, 0.0f, 1.0f);
    vertex2f(-0.5f, -0.5f);
    vertex2f(0.5f, -0.5f);
    vertex2f(0.5f, 0.5f);
    vertex2f(-0.5f, 0.5f);
    end();
    ASSERT_TRUE(wrapper_swap(display(), surface())) << "second swap failed";
    EXPECT_FALSE(pending_fn()) << "second frame left geometry buffered after the swap";
}

} // namespace
