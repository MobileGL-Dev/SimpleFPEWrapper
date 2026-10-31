// SimpleFPEWrapper - SimpleFPEWrapper/perf_backend.h
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

#pragma once

// The driver-side half of the performance counters (perf_counters.h): every
// pointer the backend loader resolves, GL and EGL alike, is replaced by a
// thunk that counts the call against that function and forwards it. Because
// every call the wrapper makes into the driver goes through those two tables,
// the per-function counts - and the per-category totals built from them - are
// complete by construction rather than by remembering to instrument a site.
//
// The thunk is generated from the pointer's own type, one instantiation per
// table entry, so it forwards with exactly the declared signature. Only a
// counters build includes this; the loader's resolve macros pick it up.

#include "perf_counters.h"

#if SFPEW_PERF_COUNTERS
namespace SFPEW::External {
struct BackendGLFunctionsTable;
}

// Room for every entry of both tables (about 470 today).
constexpr unsigned kSfpewPerfBackendSlots = 768;
extern sfpew_perf_slot_t g_sfpew_perf_backend[kSfpewPerfBackendSlots];

// A per-function slot, registered by name when the tables are filled.
// Returns the slot index; registering the same name again returns the same
// slot, so a table refilled later keeps counting where it was. A table
// larger than the slot array shares the last slot rather than overflowing.
// `field` is the table entry the pointer is stored in, so the resolver can
// hand applications the counted entry rather than the raw driver pointer.
unsigned sfpewPerfRegisterBackend(const char* name, const void* field);

// The current value of the table entry registered under `name` - its
// counting thunk, or the volume hook over it - or null if there is none.
// The resolver's fallback returns this instead of the driver's own pointer,
// so calls an application makes straight into the driver are counted too.
void* sfpewPerfBackendEntry(const char* name);

// What the reports read back.
unsigned sfpewPerfBackendCount();
const char* sfpewPerfBackendName(unsigned slot);
sfpew_perf_category_t sfpewPerfBackendCategory(unsigned slot);
const char* sfpewPerfCategoryName(sfpew_perf_category_t category);

inline void sfpewPerfBackendCall(unsigned slot) {
    auto& hits = g_sfpew_perf_backend[slot].hits;
    hits.store(hits.load(std::memory_order_relaxed) + 1u, std::memory_order_relaxed);
}

// The calls that carry volume - draws, transfers, uniforms, syncs - get a
// second, hand-written layer that also records vertices, bytes and time.
// Installed over the counting thunks once the GL table is filled.
void sfpewPerfHookBackend(SFPEW::External::BackendGLFunctionsTable& funcs);

template <unsigned Index, typename F>
struct sfpew_perf_thunk_t;

template <unsigned Index, typename R, typename... A>
struct sfpew_perf_thunk_t<Index, R (*)(A...)> {
    static inline R (*real)(A...) = nullptr;
    static inline unsigned slot = 0;
    static R call(A... args) {
        sfpewPerfBackendCall(slot);
        return real(args...);
    }
};

// Index only has to be unique per table entry; the loader passes __COUNTER__.
template <unsigned Index, typename F>
F sfpewPerfWrapBackend(F real, const char* name, const F* field) {
    if (real == nullptr) return nullptr;
    using thunk = sfpew_perf_thunk_t<Index, F>;
    thunk::real = real;
    thunk::slot = sfpewPerfRegisterBackend(name, field);
    return &thunk::call;
}
#endif
