// SimpleFPEWrapper - tests/gtest_ring_segment_sync.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// The immediate-mode streaming ring is written with a persistent coherent
// mapping, so the CPU can overwrite bytes the GPU is still reading. Its only
// protection is per-quarter fences: a marker taken when a quarter is left
// behind, waited on before the next lap writes over it. That bookkeeping is
// invisible from outside - a missing wait is a data race, not an error - so
// this drives the decision function directly through the exported test hook
// and checks the property it exists for: no upload may overwrite bytes an
// earlier lap wrote without first waiting on a marker taken after those bytes
// were last drawn from.
//
// Needs no driver: the hook is pure arithmetic.

#include "sfpew_gtest.h"

#include <algorithm>
#include <random>

namespace {

constexpr int kSegments = 4;

using RingSyncPlanFn = void (*)(unsigned long long capacity, unsigned long long previous_end,
                                unsigned long long size, unsigned long long* offset,
                                unsigned* fence_mask, unsigned* wait_mask);

// A model of the ring as the GPU sees it: which lap last wrote each quarter,
// and which lap's contents the marker sitting in each fence slot covers.
class RingModel {
public:
    RingModel(RingSyncPlanFn plan, unsigned long long capacity) : plan_(plan), capacity_(capacity) {}

    struct Counters {
        int uploads = 0;
        int wraps = 0;
        int fences = 0;
        int waits = 0;
        int idle = 0; // uploads that needed no synchronization work at all
    };

    void Upload(unsigned long long size) {
        unsigned long long offset = 0;
        unsigned fence_mask = 0, wait_mask = 0;
        plan_(capacity_, end_, size, &offset, &fence_mask, &wait_mask);
        ++counters_.uploads;

        const unsigned long long segment_size = capacity_ / kSegments;
        ASSERT_GT(segment_size, 0u);
        ASSERT_LE(offset + size, capacity_) << "the upload does not fit where it was placed";
        ASSERT_TRUE(offset == 0 || offset >= end_) << "an upload moved backwards without a wrap";
        const bool wrapped = offset == 0 && end_ != 0;
        if (wrapped) ++lap_;

        const int first = static_cast<int>(offset / segment_size);
        const int last = size == 0 ? first
                                   : std::min<int>(static_cast<int>((offset + size - 1) / segment_size),
                                                   kSegments - 1);
        unsigned touched = 0;
        for (int s = first; s <= last; ++s) touched |= 1u << s;

        // A marker taken for a segment this upload is about to write over
        // would describe the OLD contents, so it must be consumed here and
        // never left behind for the next lap to trust.
        EXPECT_EQ(fence_mask & touched & ~wait_mask, 0u)
            << "a marker was left on a segment this upload overwrites (upload "
            << counters_.uploads << ")";

        // Markers first, then waits: the order the upload path uses.
        for (int s = 0; s < kSegments; ++s) {
            if ((fence_mask & (1u << s)) == 0 || fence_[s]) continue;
            fence_[s] = true;
            fence_covers_[s] = written_lap_[s];
            ++counters_.fences;
        }
        // A wait on an empty slot synchronizes nothing, so only the segments
        // that had a marker to wait on count as actually waited out.
        unsigned waited = 0;
        for (int s = 0; s < kSegments; ++s) {
            if ((wait_mask & (1u << s)) == 0 || !fence_[s]) continue;
            EXPECT_GE(fence_covers_[s], written_lap_[s])
                << "waited on a marker older than the data it is supposed to cover (segment " << s
                << ", upload " << counters_.uploads << ")";
            fence_[s] = false;
            waited |= 1u << s;
            ++counters_.waits;
        }

        // The property under test.
        for (int s = first; s <= last; ++s) {
            if (written_lap_[s] == 0 || written_lap_[s] == lap_) continue;
            EXPECT_TRUE((waited & (1u << s)) != 0)
                << "segment " << s << " holds lap " << written_lap_[s]
                << " data and is overwritten in lap " << lap_ << " with no wait (upload "
                << counters_.uploads << ", wait mask " << wait_mask << ")";
        }
        for (int s = first; s <= last; ++s) written_lap_[s] = lap_;

        if (wrapped) ++counters_.wraps;
        if (fence_mask == 0 && wait_mask == 0) ++counters_.idle;
        end_ = offset + size;
    }

    const Counters& counters() const { return counters_; }

private:
    RingSyncPlanFn plan_;
    unsigned long long capacity_;
    unsigned long long end_ = 0;
    int lap_ = 1;
    bool fence_[kSegments] = {};
    int written_lap_[kSegments] = {};
    int fence_covers_[kSegments] = {};
    Counters counters_;
};

// The hook is not a GL entry point, so it answers dlsym and not the
// wrapper's eglGetProcAddress; and nothing here needs a context.
class RingSegmentSyncTest : public ::testing::Test {
protected:
    void SetUp() override {
        library_ = dlopen(WRAPPER_LIB_PATH, RTLD_NOW | RTLD_LOCAL);
        ASSERT_NE(library_, nullptr) << "dlopen(" << WRAPPER_LIB_PATH << "): " << dlerror();
        plan_ = reinterpret_cast<RingSyncPlanFn>(dlsym(library_, "sfpewRingSyncPlanForTest"));
        ASSERT_NE(plan_, nullptr) << "sfpewRingSyncPlanForTest not exported";
    }

    void TearDown() override {
        if (library_ != nullptr) dlclose(library_);
    }

    void* library_ = nullptr;
    RingSyncPlanFn plan_ = nullptr;
};

// The narrow case: an upload that starts in the quarter the previous one
// ended in and spills into the next. Nothing about it "enters" a new quarter
// by its start address, but it writes one all the same.
TEST_F(RingSegmentSyncTest, AnUploadSpillingOutOfItsStartingSegmentWaits) {
    constexpr unsigned long long kCapacity = 16ull * 1024 * 1024;
    constexpr unsigned long long kSegment = kCapacity / kSegments;

    unsigned long long offset = 0;
    unsigned fence_mask = 0, wait_mask = 0;
    // Previous upload ended three quarters of the way into segment 0; this
    // one starts there and runs into segment 1.
    plan_(kCapacity, kSegment * 3 / 4, kSegment * 3 / 4, &offset, &fence_mask, &wait_mask);

    EXPECT_EQ(offset, kSegment * 3 / 4);
    EXPECT_TRUE((wait_mask & (1u << 1)) != 0)
        << "segment 1 is written but not waited out (wait mask " << wait_mask << ")";
    EXPECT_EQ(wait_mask & (1u << 0), 0u) << "the segment being appended to must not be waited on";
}

// An upload that stays inside the previous one's quarter is the hot path and
// must cost nothing.
TEST_F(RingSegmentSyncTest, AnUploadInsideItsStartingSegmentSynchronizesNothing) {
    constexpr unsigned long long kCapacity = 16ull * 1024 * 1024;
    constexpr unsigned long long kSegment = kCapacity / kSegments;

    unsigned long long offset = 0;
    unsigned fence_mask = 0, wait_mask = 0;
    plan_(kCapacity, kSegment / 4, kSegment / 4, &offset, &fence_mask, &wait_mask);
    EXPECT_EQ(fence_mask, 0u);
    EXPECT_EQ(wait_mask, 0u);
}

TEST_F(RingSegmentSyncTest, EverySegmentAnUploadTouchesIsSynchronized) {
    constexpr unsigned long long kCapacity = 16ull * 1024 * 1024;
    RingModel model(plan_, kCapacity);
    // Immediate-mode uploads: mostly a few hundred bytes to a few tens of
    // kilobytes, with the occasional large one. Fixed seed, so a failure is
    // reproducible.
    std::mt19937 rng(0x5FEC0DEu);
    std::uniform_int_distribution<int> bucket(0, 99);
    std::uniform_int_distribution<unsigned long long> small(64, 4096);
    std::uniform_int_distribution<unsigned long long> medium(4096, 96 * 1024);
    std::uniform_int_distribution<unsigned long long> large(256 * 1024, 3 * 1024 * 1024);
    for (int i = 0; i < 200000; ++i) {
        const int roll = bucket(rng);
        const unsigned long long size =
            roll < 70 ? small(rng) : (roll < 98 ? medium(rng) : large(rng));
        model.Upload(size);
        if (::testing::Test::HasFailure()) break;
    }
    const auto& counters = model.counters();
    EXPECT_GT(counters.wraps, 10) << "the simulation never wrapped; it proves nothing";
    // The other half of the contract: the fix must not turn into a fence or a
    // wait on every upload.
    EXPECT_GT(counters.idle, counters.uploads * 9 / 10)
        << "synchronization work on " << (counters.uploads - counters.idle) << " of "
        << counters.uploads << " uploads";
}

// Half-ring uploads: each one crosses two quarter boundaries, which is what
// leaves a quarter behind without ever "entering" the next one from its start.
TEST_F(RingSegmentSyncTest, LargeUploadsSpanningSeveralSegmentsStaySynchronized) {
    constexpr unsigned long long kCapacity = 16ull * 1024 * 1024;
    RingModel model(plan_, kCapacity);
    std::mt19937 rng(7);
    std::uniform_int_distribution<unsigned long long> huge(kCapacity / 3, kCapacity * 2 / 3);
    for (int i = 0; i < 2000; ++i) {
        model.Upload(huge(rng));
        if (::testing::Test::HasFailure()) break;
    }
    EXPECT_GT(model.counters().wraps, 10);
}

} // namespace
