// SimpleFPEWrapper - tests/gtest_perf_counters.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// The debug performance counters (SimpleFPEWrapper/perf_counters.h) count
// what they say they count. They exist only in a -DSFPEW_PERF_COUNTERS=ON
// build; in any other build their API is not exported and every case here
// skips, saying so, rather than passing having proved nothing.

#include "sfpew_gtest.h"

#include <cstdint>
#include <set>
#include <string>

namespace {

using sfpew_test::ContextTest;
using sfpew_test::GLbitfield;
using sfpew_test::GLenum;
using sfpew_test::GLfloat;
using sfpew_test::GLsizei;
using sfpew_test::GLsizeiptr;
using sfpew_test::GLuint;

constexpr GLenum GL_QUADS_ = 0x0007;
constexpr GLenum GL_MODELVIEW_MATRIX_ = 0x0BA6;
constexpr GLenum GL_TEXTURE_2D_ = 0x0DE1;
constexpr GLenum GL_ARRAY_BUFFER_ = 0x8892;
constexpr GLenum GL_STATIC_DRAW_ = 0x88E4;
constexpr GLbitfield GL_COLOR_BUFFER_BIT_ = 0x00004000;
constexpr GLbitfield GL_DEPTH_BUFFER_BIT_ = 0x00000100;

class PerfCountersTest : public ContextTest {
protected:
    void SetUp() override {
        ContextTest::SetUp();
        if (IsSkipped() || HasFatalFailure()) return;
        count_ = Dlsym<unsigned (*)()>("sfpewPerfCounterCount");
        name_ = Dlsym<const char* (*)(unsigned)>("sfpewPerfCounterName");
        read_ = Dlsym<int (*)(unsigned, uint64_t*, uint64_t*)>("sfpewPerfCounterRead");
        frames_ = Dlsym<uint64_t (*)()>("sfpewPerfFrameCount");
        reset_ = Dlsym<void (*)()>("sfpewPerfReset");
        if (count_ == nullptr || name_ == nullptr || read_ == nullptr || frames_ == nullptr ||
            reset_ == nullptr)
            GTEST_SKIP() << "built without SFPEW_PERF_COUNTERS: no counters to test";
        reset_();
    }

    // hits and sum of the counter with this name; fails the test if there
    // is no such counter.
    void Read(const char* name, uint64_t* hits, uint64_t* sum = nullptr) {
        for (unsigned i = 0; i < count_(); ++i) {
            if (std::string(name_(i)) == name) {
                read_(i, hits, sum);
                return;
            }
        }
        ADD_FAILURE() << "no counter named " << name;
        *hits = 0;
        if (sum != nullptr) *sum = 0;
    }
    uint64_t Hits(const char* name) {
        uint64_t hits = 0;
        Read(name, &hits);
        return hits;
    }

    void Quad() {
        auto begin = Get<void (*)(GLenum)>("glBegin");
        auto end = Get<void (*)()>("glEnd");
        auto vertex2f = Get<void (*)(GLfloat, GLfloat)>("glVertex2f");
        begin(GL_QUADS_);
        vertex2f(-0.5f, -0.5f);
        vertex2f(0.5f, -0.5f);
        vertex2f(0.5f, 0.5f);
        vertex2f(-0.5f, 0.5f);
        end();
    }

    unsigned (*count_)() = nullptr;
    const char* (*name_)(unsigned) = nullptr;
    int (*read_)(unsigned, uint64_t*, uint64_t*) = nullptr;
    uint64_t (*frames_)() = nullptr;
    void (*reset_)() = nullptr;
};

TEST_F(PerfCountersTest, EveryCounterHasAUniqueNameAndAUnit) {
    ASSERT_GT(count_(), 0u);
    std::set<std::string> names;
    for (unsigned i = 0; i < count_(); ++i) {
        const char* name = name_(i);
        ASSERT_NE(name, nullptr) << "counter " << i << " has no name";
        EXPECT_TRUE(names.insert(name).second) << "duplicate counter name " << name;
        uint64_t hits = 1, sum = 1;
        const int unit = read_(i, &hits, &sum);
        EXPECT_GE(unit, 0) << name;
        EXPECT_LE(unit, 3) << name;
    }
    EXPECT_EQ(name_(count_()), nullptr) << "a name past the end";
    EXPECT_EQ(read_(count_(), nullptr, nullptr), -1) << "a read past the end";
}

TEST_F(PerfCountersTest, ResetZeroesEverything) {
    Quad();
    Get<void (*)()>("glFinish")();
    reset_();
    for (unsigned i = 0; i < count_(); ++i) {
        uint64_t hits = 1, sum = 1;
        read_(i, &hits, &sum);
        EXPECT_EQ(hits, 0u) << name_(i);
        EXPECT_EQ(sum, 0u) << name_(i);
    }
    EXPECT_EQ(frames_(), 0u);
}

// One small run: counted once as an app draw carrying its four vertices,
// merged into the pending batch, drawn once when glFinish drains it - and
// that draw reaches the driver with the quad's six indices.
TEST_F(PerfCountersTest, ImmediateRunIsCountedThroughToTheDriver) {
    Quad();
    uint64_t runs = 0, vertices = 0;
    Read("app.draw.immediate", &runs, &vertices);
    EXPECT_EQ(runs, 1u);
    EXPECT_EQ(vertices, 4u);
    EXPECT_EQ(Hits("wrapper.immediate.merged_run"), 1u);
    EXPECT_EQ(Hits("wrapper.immediate.batch_draw"), 0u) << "drawn before anything drained the batch";

    Get<void (*)()>("glFinish")();
    uint64_t draws = 0, merged = 0;
    Read("wrapper.immediate.batch_draw", &draws, &merged);
    EXPECT_EQ(draws, 1u);
    EXPECT_EQ(merged, 1u) << "the batch should carry the one run it merged";
    EXPECT_EQ(Hits("wrapper.draw.immediate"), 1u);

    uint64_t vertex_draws = 0, driver_vertices = 0, index_draws = 0, driver_indices = 0;
    Read("gl.draw.vertices", &vertex_draws, &driver_vertices);
    Read("gl.draw.indices", &index_draws, &driver_indices);
    EXPECT_EQ(vertex_draws + index_draws, 1u) << "one driver draw for the one batch";
    EXPECT_EQ(driver_vertices + driver_indices, 6u) << "a quad reaches the driver as two triangles";
    EXPECT_GE(Hits("call.glFinish"), 1u) << "driver calls are not being counted per function";
}

TEST_F(PerfCountersTest, ModelviewQueryIsAnsweredByTheWrapper) {
    GLfloat matrix[16] = {};
    Get<void (*)(GLenum, GLfloat*)>("glGetFloatv")(GL_MODELVIEW_MATRIX_, matrix);
    EXPECT_EQ(Hits("app.get.answered"), 1u);
    EXPECT_EQ(Hits("app.get.forwarded"), 0u);
}

// A second bind of what is already bound never reaches the driver.
TEST_F(PerfCountersTest, RedundantTextureBindIsDropped) {
    auto gen = Get<void (*)(GLsizei, GLuint*)>("glGenTextures");
    auto bind = Get<void (*)(GLenum, GLuint)>("glBindTexture");
    GLuint texture = 0;
    gen(1, &texture);
    reset_();
    bind(GL_TEXTURE_2D_, texture);
    bind(GL_TEXTURE_2D_, texture);
    EXPECT_EQ(Hits("app.bind.texture"), 2u);
    EXPECT_EQ(Hits("wrapper.redundant.bind_texture"), 1u);
    EXPECT_EQ(Hits("call.glBindTexture"), 1u);
}

// An entry point the wrapper does not implement is still counted when the
// application calls it: the resolver hands out the counted table entry, and
// a data upload records its bytes.
TEST_F(PerfCountersTest, ApplicationBufferUploadIsCountedWithItsBytes) {
    auto gen = Get<void (*)(GLsizei, GLuint*)>("glGenBuffers");
    auto bind = Get<void (*)(GLenum, GLuint)>("glBindBuffer");
    auto data = Get<void (*)(GLenum, GLsizeiptr, const void*, GLenum)>("glBufferData");
    ASSERT_NE(data, nullptr);
    GLuint buffer = 0;
    gen(1, &buffer);
    bind(GL_ARRAY_BUFFER_, buffer);
    static const unsigned char bytes[1000] = {};
    reset_();
    data(GL_ARRAY_BUFFER_, sizeof bytes, bytes, GL_STATIC_DRAW_);
    data(GL_ARRAY_BUFFER_, 4096, nullptr, GL_STATIC_DRAW_);
    uint64_t uploads = 0, uploaded = 0, allocations = 0, allocated = 0;
    Read("gl.buffer.upload", &uploads, &uploaded);
    Read("gl.buffer.alloc", &allocations, &allocated);
    EXPECT_EQ(uploads, 1u);
    EXPECT_EQ(uploaded, sizeof bytes);
    EXPECT_EQ(allocations, 1u);
    EXPECT_EQ(allocated, 4096u);
    EXPECT_EQ(Hits("call.glBufferData"), 2u);
}

// Color clears stand in for frames until the wrapper sees a swap; from then
// on only swaps count.
TEST_F(PerfCountersTest, FramesAreColorClearsUntilTheFirstSwap) {
    auto clear = Get<void (*)(GLbitfield)>("glClear");
    clear(GL_COLOR_BUFFER_BIT_ | GL_DEPTH_BUFFER_BIT_);
    clear(GL_DEPTH_BUFFER_BIT_);
    clear(GL_COLOR_BUFFER_BIT_);
    EXPECT_EQ(frames_(), 2u) << "a depth-only clear is a pass, not a frame";
    EXPECT_EQ(Hits("frame.time"), 1u) << "two frame boundaries make one frame interval";

    using SwapFn = EGLBoolean (*)(EGLDisplay, EGLSurface);
    auto swap = Get<SwapFn>("eglSwapBuffers");
    ASSERT_NE(swap, nullptr);
    swap(display(), surface());
    EXPECT_EQ(frames_(), 3u);
    clear(GL_COLOR_BUFFER_BIT_);
    EXPECT_EQ(frames_(), 3u) << "clears still counted after a swap was seen";
}

} // namespace
