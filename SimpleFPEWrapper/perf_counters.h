// SimpleFPEWrapper - SimpleFPEWrapper/perf_counters.h
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

#pragma once

// ---- Performance counters: a simpleperf for the wrapper ----------------
// Every performance question this wrapper has had came down to the same few
// numbers: how many draws per frame the application asked for and how many
// reached the driver, how many binds and state calls rode along, how many
// bytes moved and how fast, what the wrapper dropped as redundant, and which
// rare event (a compile, a fence wait) put a spike in a frame. Answering one
// used to mean a throwaway timing patch. This answers all of them in place.
//
// Three vantage points, so a number can always be traced to its cause:
//   app.*     what the application asked for, counted at the entry points
//   wrapper.* what the wrapper decided: batching, caches, redundancy drops,
//             and every CPU copy it makes on the application's behalf
//   gl.*      what the driver actually received. Every backend entry point
//             is wrapped (perf_backend.h), so the per-function call counts
//             and the per-category totals are complete by construction, and
//             the calls that carry volume - draws, transfers, uniforms,
//             syncs - also record vertices, bytes and time.
//
// Compiled out of a normal build: cmake -DSFPEW_PERF_COUNTERS=ON puts it in,
// and without it every macro below expands to nothing - no code, no storage,
// no symbols. Compiled in, counting is always on (it is cheaper than testing
// whether it should be) and reporting waits for the environment:
//
//   SFPEW_PERF=1                 report the run's totals at process exit
//   SFPEW_PERF_INTERVAL=<frames> also report every <frames> frames, covering
//                                that stretch only. Use this on Android,
//                                which kills processes rather than exiting
//   SFPEW_PERF_FILE=<path>       mirror the reports to a file (logcat wraps)
//   SFPEW_PERF_CSV=<path>        one machine-readable row per counter per
//                                report, for diffing runs
//   SFPEW_PERF_TOP=<n>           driver functions listed per report (20)
//   SFPEW_PERF_START=<frame>     zero everything once, at that frame: the
//                                exit report then covers only what follows,
//                                which is how a replay that rebuilds state
//                                before looping a frame profiles just the loop
//
// A report leads with the summary that matters - frame time, then draws,
// binds, state calls, uniform traffic and bytes moved, each per second and per
// frame, then the ratios between the vantage points - and follows with
// driver calls by category, the busiest driver functions, and every counter.
//
// A frame is a buffer swap through the wrapper, or a color clear when the
// application swaps through libEGL directly and the wrapper never sees one.
//
// Each counter keeps four numbers: hits (how often), sum (the amount each
// hit carried - items, bytes or nanoseconds by unit), ns (time spent, for
// the timed ones, which is what turns bytes into bandwidth) and max (the
// largest single amount, or for a pure timer the longest single hit - the
// spike an average hides).
//
// Increments are relaxed loads and stores, not atomic adds: a locked add per
// draw is exactly the kind of cost these counters exist to find (an atomic
// epoch at three per draw once measured 13% of a GUI pass), and a debug tally
// can afford to lose an increment to a cross-thread race where it cannot
// afford to perturb what it measures. Relaxed atomics keep the race defined.
//
// What a counters build costs (measured 2026-10-31, glbench + bench3, -O3
// LTO both sides): on the desktop (NVIDIA) a geometric mean of +2.4%, from
// +3-9% on the per-draw paths - each driver call now passes a hook and a
// thunk - down to nothing on fill, texture transfer and immediate mode; the
// worst case is the cheapest call, glGetFloatv, at +1.4ns on 5ns. On a
// phone (Mali-G77) every phase stayed within its noise. Compare runs of the
// same build; do not compare a counters build's timings with a normal one's.

#include <cstdint>

#ifndef SFPEW_PERF_COUNTERS
#define SFPEW_PERF_COUNTERS 0
#endif

// X(id, unit, "name"). Names group by prefix in reports.
#define SFPEW_PERF_COUNTER_LIST(X)                                                                 \
    X(frame_time, ns, "frame.time")                                                                \
    /* what the application asked for */                                                           \
    X(app_draw_immediate, items, "app.draw.immediate")                                             \
    X(app_draw_arrays, items, "app.draw.arrays")                                                   \
    X(app_draw_elements, items, "app.draw.elements")                                               \
    X(app_draw_multi, items, "app.draw.multi")                                                     \
    X(app_call_list, items, "app.call_list")                                                       \
    X(app_bind_texture, events, "app.bind.texture")                                                \
    X(app_active_texture, events, "app.bind.active_texture")                                       \
    X(app_bind_buffer, events, "app.bind.buffer")                                                  \
    X(app_bind_vertex_array, events, "app.bind.vertex_array")                                      \
    X(app_use_program, events, "app.bind.program")                                                 \
    X(app_state, events, "app.state")                                                              \
    X(app_get_wrapper, events, "app.get.answered")                                                 \
    X(app_get_backend, events, "app.get.forwarded")                                                \
    /* what the wrapper decided */                                                                 \
    X(context_strict_resolve, events, "wrapper.context.strict_resolve")                            \
    X(context_egl_query, events, "wrapper.context.egl_query")                                      \
    X(context_switch, events, "wrapper.context.switch")                                            \
    X(redundant_bind_texture, events, "wrapper.redundant.bind_texture")                            \
    X(redundant_active_texture, events, "wrapper.redundant.active_texture")                        \
    X(redundant_state, events, "wrapper.redundant.state")                                          \
    X(redundant_tex_parameter, events, "wrapper.redundant.tex_parameter")                          \
    X(barrier_restore, events, "wrapper.barrier.restore")                                          \
    X(immediate_merged_run, events, "wrapper.immediate.merged_run")                                \
    X(immediate_batch_draw, items, "wrapper.immediate.batch_draw")                                 \
    X(immediate_direct_draw, events, "wrapper.immediate.direct_draw")                              \
    X(immediate_idle_kick, events, "wrapper.immediate.idle_kick")                                  \
    X(draw_immediate, events, "wrapper.draw.immediate")                                            \
    X(draw_immediate_layout_miss, events, "wrapper.draw.immediate_layout_miss")                    \
    X(draw_arrays, events, "wrapper.draw.arrays")                                                  \
    X(draw_arrays_layout_reuse, events, "wrapper.draw.arrays_layout_reuse")                        \
    X(list_replay, events, "wrapper.list.replay")                                                  \
    X(list_replay_layout_hit, events, "wrapper.list.replay_layout_hit")                            \
    X(program_key_fast, events, "wrapper.program.key_fast")                                        \
    X(program_key_hashed, events, "wrapper.program.key_hashed")                                    \
    X(program_generate, ns, "wrapper.program.generate")                                            \
    X(program_compile, ns, "wrapper.program.compile")                                              \
    X(program_uniform_init, ns, "wrapper.program.uniform_init")                                    \
    X(copy_ring_upload, bytes, "wrapper.copy.ring_upload")                                         \
    X(copy_client_gather, bytes, "wrapper.copy.client_gather")                                     \
    X(copy_list_capture, bytes, "wrapper.copy.list_capture")                                       \
    X(copy_bgra_convert, bytes, "wrapper.copy.bgra_convert")                                       \
    X(ring_fence_wait, ns, "wrapper.ring.fence_wait")                                              \
    X(ring_wrap_finish, events, "wrapper.ring.wrap_finish")                                        \
    /* what the driver received, beyond the per-function call counts */                            \
    X(gl_draw_vertices, items, "gl.draw.vertices")                                                 \
    X(gl_draw_indices, items, "gl.draw.indices")                                                   \
    X(gl_buffer_upload, bytes, "gl.buffer.upload")                                                 \
    X(gl_buffer_alloc, bytes, "gl.buffer.alloc")                                                   \
    X(gl_buffer_copy, bytes, "gl.buffer.copy")                                                     \
    X(gl_buffer_map, bytes, "gl.buffer.map")                                                       \
    X(gl_texture_upload, bytes, "gl.texture.upload")                                               \
    X(gl_readback, bytes, "gl.readback")                                                           \
    X(gl_blit, items, "gl.blit.pixels")                                                            \
    X(gl_uniform, bytes, "gl.uniform")                                                             \
    X(gl_sync_wait, ns, "gl.sync.wait")

enum class sfpew_perf_unit_t : int { events, items, bytes, ns };

enum class sfpew_perf_t : unsigned {
#define SFPEW_PERF_ENUM(id, unit, name) id,
    SFPEW_PERF_COUNTER_LIST(SFPEW_PERF_ENUM)
#undef SFPEW_PERF_ENUM
        count
};

// What a driver entry point does, for the per-category totals. Assigned from
// its name when the backend table is wrapped.
enum class sfpew_perf_category_t : int {
    draw,
    bind,
    vertex_setup,
    buffer,
    texture,
    readback,
    uniform,
    state,
    clear,
    shader,
    object,
    query,
    sync,
    egl,
    other,
    count
};

#if SFPEW_PERF_COUNTERS
#include <atomic>
#include <chrono>

struct sfpew_perf_slot_t {
    std::atomic<uint64_t> hits{0};
    std::atomic<uint64_t> sum{0};
    std::atomic<uint64_t> ns{0};
    std::atomic<uint64_t> max{0};
};
extern sfpew_perf_slot_t g_sfpew_perf[static_cast<unsigned>(sfpew_perf_t::count)];

inline uint64_t sfpewPerfNow() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                     std::chrono::steady_clock::now().time_since_epoch())
                                     .count());
}

// `peak` is what max tracks: the amount, or for a timer the duration.
inline void sfpewPerfRecord(sfpew_perf_slot_t& slot, uint64_t amount, uint64_t ns, uint64_t peak) {
    constexpr auto r = std::memory_order_relaxed;
    slot.hits.store(slot.hits.load(r) + 1u, r);
    slot.sum.store(slot.sum.load(r) + amount, r);
    if (ns != 0) slot.ns.store(slot.ns.load(r) + ns, r);
    if (peak > slot.max.load(r)) slot.max.store(peak, r);
}

inline void sfpewPerfAdd(sfpew_perf_t id, uint64_t amount) {
    sfpewPerfRecord(g_sfpew_perf[static_cast<unsigned>(id)], amount, 0, amount);
}

// Times its scope. For a ns counter the duration is the amount; for any
// other the amount is given (bytes copied, say) and the duration is what a
// report divides it by. Reading the clock costs ~20ns, so timed scopes go
// around coarse work only: a compile, a fence wait, a copy of a buffer.
class sfpew_perf_scope_t {
public:
    sfpew_perf_scope_t(sfpew_perf_t id, uint64_t amount, bool amount_is_time)
        : id_(id), amount_(amount), amount_is_time_(amount_is_time), start_(sfpewPerfNow()) {}
    ~sfpew_perf_scope_t() {
        const uint64_t ns = sfpewPerfNow() - start_;
        auto& slot = g_sfpew_perf[static_cast<unsigned>(id_)];
        if (amount_is_time_) {
            sfpewPerfRecord(slot, ns, ns, ns);
        } else {
            sfpewPerfRecord(slot, amount_, ns, amount_);
        }
    }
    sfpew_perf_scope_t(const sfpew_perf_scope_t&) = delete;
    sfpew_perf_scope_t& operator=(const sfpew_perf_scope_t&) = delete;

private:
    sfpew_perf_t id_;
    uint64_t amount_;
    bool amount_is_time_;
    uint64_t start_;
};

// A frame boundary. `swap` is true for a buffer swap through the wrapper,
// false for a color clear; clears only count while no swap has been seen.
void sfpewPerfFrame(bool swap);

#define SFPEW_PERF_COUNT(id) sfpewPerfAdd(sfpew_perf_t::id, 1u)
#define SFPEW_PERF_ADD(id, amount) sfpewPerfAdd(sfpew_perf_t::id, static_cast<uint64_t>(amount))
#define SFPEW_PERF_CONCAT_(a, b) a##b
#define SFPEW_PERF_CONCAT(a, b) SFPEW_PERF_CONCAT_(a, b)
// The scope's duration, as one hit of a ns counter.
#define SFPEW_PERF_TIME(id)                                                                        \
    sfpew_perf_scope_t SFPEW_PERF_CONCAT(sfpew_perf_scope_, __LINE__)(sfpew_perf_t::id, 0, true)
// `amount` (bytes, items) as one hit, with the scope's duration alongside.
#define SFPEW_PERF_TIMED(id, amount)                                                               \
    sfpew_perf_scope_t SFPEW_PERF_CONCAT(sfpew_perf_scope_, __LINE__)(                             \
        sfpew_perf_t::id, static_cast<uint64_t>(amount), false)
#define SFPEW_PERF_FRAME(swap) sfpewPerfFrame(swap)
#else
// Arguments are not evaluated: a counter argument must never be the only
// place a side effect happens.
#define SFPEW_PERF_COUNT(id)                                                                       \
    do {                                                                                           \
    } while (0)
#define SFPEW_PERF_ADD(id, amount)                                                                 \
    do {                                                                                           \
    } while (0)
#define SFPEW_PERF_TIME(id)                                                                        \
    do {                                                                                           \
    } while (0)
#define SFPEW_PERF_TIMED(id, amount)                                                               \
    do {                                                                                           \
    } while (0)
#define SFPEW_PERF_FRAME(swap)                                                                     \
    do {                                                                                           \
    } while (0)
#endif
