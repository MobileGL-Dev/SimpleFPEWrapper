// SimpleFPEWrapper - tests/gtest_gather_stride_guard.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// gather_client_arrays casts each attribute's stride to size_t, so a
// negative one becomes a step of about 2^64 bytes and `src + v * stride`
// walks backwards out of the caller's allocation (plans/16 M1). The
// gl*Pointer entry points reject a negative stride at the door now, which is
// exactly why this is defence in depth and why it has to be driven through
// the wrapper's test hook: no public call can build the record any more.
// copyAttributeElements, the sibling that reads the same records, has always
// had the check - the gather is the one that did not.
//
// The guard page goes BEFORE the data here: that is the direction the bad
// stride steps in.

#include "sfpew_fault_trap.h"
#include "sfpew_gtest.h"

namespace {

using sfpew_test::ContextTest;
using sfpew_test::FaultProbe;
using sfpew_test::GLenum;
using sfpew_test::GuardedArray;

constexpr GLenum GL_NO_ERROR_ = 0;
// Three position floats then four colour bytes, the layout the hook builds.
constexpr int kRowExtent = 3 * 4 + 4;
constexpr int kVertices = 4;

class GatherStrideGuardTest : public ContextTest {
protected:
    void SetUp() override {
        ContextTest::SetUp();
        if (::testing::Test::IsSkipped()) return;
        // One wrapper entry point first, so the failure of a missing hook is
        // not confused with an unusable context.
        auto* get_error = Get<GLenum (*)()>("glGetError");
        ASSERT_NE(get_error, nullptr);
        EXPECT_EQ(get_error(), GL_NO_ERROR_);
        gather_ = Dlsym<int (*)(const void*, int, int, int)>("sfpewGatherClientArraysForTest");
        ASSERT_NE(gather_, nullptr) << "the wrapper exports no sfpewGatherClientArraysForTest";
    }

    int (*gather_)(const void*, int, int, int) = nullptr;
};

// The control: two tightly packed arrays are exactly what the gather exists
// for, so a rejection here would mean the hook is not reaching it at all.
TEST_F(GatherStrideGuardTest, TightlyPackedArraysAreGathered) {
    GuardedArray array(static_cast<size_t>(kVertices) * kRowExtent);
    ASSERT_NE(array.data(), nullptr) << "mmap";
    EXPECT_EQ(gather_(array.data(), 0, 0, kVertices), 1);
}

TEST_F(GatherStrideGuardTest, ANegativeStrideIsRejectedBeforeAnythingIsRead) {
    GuardedArray array(static_cast<size_t>(kVertices) * kRowExtent,
                       GuardedArray::Guard::kBefore);
    ASSERT_NE(array.data(), nullptr) << "mmap";

    int accepted = -1;
    FaultProbe probe;
    if (probe.Try()) accepted = gather_(array.data(), -32, 0, kVertices);
    ASSERT_FALSE(probe.faulted())
        << "the gather stepped backwards out of the array before rejecting the stride";
    EXPECT_EQ(accepted, 0) << "a negative stride was accepted";
}

} // namespace
