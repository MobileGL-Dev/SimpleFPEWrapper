// SimpleFPEWrapper - SimpleFPEWrapper/perf_counters.cpp
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

#include "perf_counters.h"

#if SFPEW_PERF_COUNTERS
#include "init.h" // SFPEW_APIENTRY
#include "log.h"
#include "perf_backend.h"
#include "version.h" // SFPEW_GIT_COMMIT, for the report file's header

#include <algorithm>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

constexpr unsigned kPerfCounterCount = static_cast<unsigned>(sfpew_perf_t::count);
constexpr unsigned kCategoryCount = static_cast<unsigned>(sfpew_perf_category_t::count);

sfpew_perf_slot_t g_sfpew_perf[kPerfCounterCount];

namespace {

struct counter_info_t {
    const char* name;
    sfpew_perf_unit_t unit;
};

constexpr counter_info_t kCounters[] = {
#define SFPEW_PERF_INFO(id, unit, name) {name, sfpew_perf_unit_t::unit},
    SFPEW_PERF_COUNTER_LIST(SFPEW_PERF_INFO)
#undef SFPEW_PERF_INFO
};
static_assert(sizeof(kCounters) / sizeof(kCounters[0]) == kPerfCounterCount);

bool envSet(const char* name) {
    const char* v = getenv(name);
    return v != nullptr && v[0] != '\0' && !(v[0] == '0' && v[1] == '\0');
}

unsigned long envNumber(const char* name, unsigned long fallback) {
    const char* v = getenv(name);
    return v != nullptr && v[0] != '\0' ? std::strtoul(v, nullptr, 10) : fallback;
}

bool reportingEnabled() {
    static const bool enabled = envSet("SFPEW_PERF");
    return enabled;
}

// Frames between reports; 0 reports only at exit.
unsigned long reportInterval() {
    static const unsigned long interval = envNumber("SFPEW_PERF_INTERVAL", 0);
    return interval;
}

unsigned long topCount() {
    static const unsigned long top = envNumber("SFPEW_PERF_TOP", 20);
    return top;
}

FILE* openReportFile(const char* variable, const char* header) {
    if (!reportingEnabled()) return nullptr;
    const char* path = getenv(variable);
    if (path == nullptr || *path == '\0') return nullptr;
    FILE* opened = fopen(path, "w");
    if (opened == nullptr) {
        SFPEW_LOGW("PERF cannot write %s (%s)", path, strerror(errno));
        return nullptr;
    }
    setvbuf(opened, nullptr, _IOLBF, 0);
    fputs(header, opened);
    return opened;
}

// Truncated at open, so each file always holds the latest run.
FILE* reportFile() {
    static FILE* file = [] {
        char header[160];
        snprintf(header, sizeof header, "SFPEW performance counters, commit %s, built %s\n",
                 SFPEW_GIT_COMMIT, __DATE__ " " __TIME__);
        return openReportFile("SFPEW_PERF_FILE", header);
    }();
    return file;
}

FILE* csvFile() {
    static FILE* file =
        openReportFile("SFPEW_PERF_CSV", "report,frames,seconds,counter,unit,hits,sum,ns,max\n");
    return file;
}

void emit(const char* format, ...) {
    char line[320];
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof line, format, args);
    va_end(args);
    SFPEW_LOGI("%s", line);
    if (FILE* file = reportFile()) {
        fputs(line, file);
        fputc('\n', file);
    }
}

// ---- snapshots --------------------------------------------------------

struct snapshot_t {
    uint64_t hits[kPerfCounterCount] = {};
    uint64_t sum[kPerfCounterCount] = {};
    uint64_t ns[kPerfCounterCount] = {};
    uint64_t max[kPerfCounterCount] = {};
    uint64_t backend[kSfpewPerfBackendSlots] = {};
    uint64_t frames = 0;
    uint64_t time = 0;
};

// Frame boundaries arrive on the rendering thread; the totals are read at
// exit on whichever thread ends the process. Neither needs more than this.
uint64_t g_frames = 0;
bool g_saw_swap = false;
uint64_t g_last_frame_time = 0;
// Where the totals start: library load, or the last sfpewPerfReset.
uint64_t g_epoch = sfpewPerfNow();
// The previous interval report, and the largest single values of every
// interval already reported (max is reset per interval so each report
// shows its own spikes; the run total folds them back in).
snapshot_t g_last_report;
bool g_have_last_report = false;
uint64_t g_run_max[kPerfCounterCount] = {};

snapshot_t take() {
    snapshot_t now;
    constexpr auto r = std::memory_order_relaxed;
    for (unsigned i = 0; i < kPerfCounterCount; ++i) {
        now.hits[i] = g_sfpew_perf[i].hits.load(r);
        now.sum[i] = g_sfpew_perf[i].sum.load(r);
        now.ns[i] = g_sfpew_perf[i].ns.load(r);
        now.max[i] = g_sfpew_perf[i].max.load(r);
    }
    for (unsigned i = 0; i < sfpewPerfBackendCount(); ++i)
        now.backend[i] = g_sfpew_perf_backend[i].hits.load(r);
    now.frames = g_frames;
    now.time = sfpewPerfNow();
    return now;
}

snapshot_t epochSnapshot() {
    snapshot_t start;
    start.time = g_epoch;
    return start;
}

// The difference between two snapshots, plus derived rates. max is not a
// difference: it is whatever the later snapshot holds for its interval.
struct interval_t {
    const snapshot_t& from;
    const snapshot_t& to;
    double frames;
    double seconds;

    uint64_t hits(sfpew_perf_t id) const {
        const auto i = static_cast<unsigned>(id);
        return to.hits[i] - from.hits[i];
    }
    uint64_t sum(sfpew_perf_t id) const {
        const auto i = static_cast<unsigned>(id);
        return to.sum[i] - from.sum[i];
    }
    uint64_t ns(sfpew_perf_t id) const {
        const auto i = static_cast<unsigned>(id);
        return to.ns[i] - from.ns[i];
    }
    uint64_t max(sfpew_perf_t id) const { return to.max[static_cast<unsigned>(id)]; }
    uint64_t backend(unsigned slot) const { return to.backend[slot] - from.backend[slot]; }
    double perSecond(double value) const { return seconds > 0.0 ? value / seconds : 0.0; }
    double perFrame(double value) const { return frames > 0.0 ? value / frames : 0.0; }
};

// ---- formatting -------------------------------------------------------

// A count, with a k/M suffix once it stops being readable digit by digit.
void formatCount(double value, char* out, size_t size) {
    if (value >= 1e7) snprintf(out, size, "%.2fM", value / 1e6);
    else if (value >= 1e5) snprintf(out, size, "%.1fk", value / 1e3);
    else if (value >= 100.0 || value == 0.0) snprintf(out, size, "%.0f", value);
    else snprintf(out, size, "%.2f", value);
}

void formatAmount(sfpew_perf_unit_t unit, double value, char* out, size_t size) {
    switch (unit) {
    case sfpew_perf_unit_t::ns:
        if (value >= 1e9) snprintf(out, size, "%.2f s", value / 1e9);
        else if (value >= 1e6) snprintf(out, size, "%.2f ms", value / 1e6);
        else if (value >= 1e3) snprintf(out, size, "%.2f us", value / 1e3);
        else snprintf(out, size, "%.0f ns", value);
        break;
    case sfpew_perf_unit_t::bytes:
        if (value >= 1073741824.0) snprintf(out, size, "%.2f GiB", value / 1073741824.0);
        else if (value >= 1048576.0) snprintf(out, size, "%.2f MiB", value / 1048576.0);
        else if (value >= 1024.0) snprintf(out, size, "%.2f KiB", value / 1024.0);
        else snprintf(out, size, "%.0f B", value);
        break;
    default:
        formatCount(value, out, size);
        break;
    }
}

// Bytes over the time spent moving them, as the rate the copy ran at.
void formatBandwidth(uint64_t bytes, uint64_t ns, char* out, size_t size) {
    if (ns == 0) {
        snprintf(out, size, "-");
        return;
    }
    const double per_second = (double)bytes * 1e9 / (double)ns;
    if (per_second >= 1073741824.0) snprintf(out, size, "%.2f GiB/s", per_second / 1073741824.0);
    else snprintf(out, size, "%.1f MiB/s", per_second / 1048576.0);
}

double percent(double part, double whole) { return whole > 0.0 ? 100.0 * part / whole : 0.0; }

// ---- the report ---------------------------------------------------------

using P = sfpew_perf_t;

// One summary row: a per-second and per-frame count, plus a note.
void countRow(const interval_t& d, const char* label, double value, const char* note = "") {
    char per_second[24], per_frame[24];
    formatCount(d.perSecond(value), per_second, sizeof per_second);
    formatCount(d.perFrame(value), per_frame, sizeof per_frame);
    emit("    %-30s %12s %12s   %s", label, per_second, per_frame, note);
}

// One summary row for an amount of bytes, with the bandwidth it moved at
// when it was timed.
void bytesRow(const interval_t& d, const char* label, P id) {
    const uint64_t bytes = d.sum(id);
    if (d.hits(id) == 0) return;
    char per_second[24], per_frame[24], rate[24], note[96];
    formatAmount(sfpew_perf_unit_t::bytes, d.perSecond((double)bytes), per_second, sizeof per_second);
    formatAmount(sfpew_perf_unit_t::bytes, d.perFrame((double)bytes), per_frame, sizeof per_frame);
    formatBandwidth(bytes, d.ns(id), rate, sizeof rate);
    char calls[24];
    formatCount(d.perFrame((double)d.hits(id)), calls, sizeof calls);
    snprintf(note, sizeof note, "%s calls/frame%s%s", calls, d.ns(id) != 0 ? ", at " : "",
             d.ns(id) != 0 ? rate : "");
    emit("    %-30s %10s/s %10s/f   %s", label, per_second, per_frame, note);
}

void timerRow(const interval_t& d, const char* label, P id) {
    if (d.hits(id) == 0) return;
    char total[24], per_frame[24], avg[24], peak[24], note[96];
    formatAmount(sfpew_perf_unit_t::ns, (double)d.sum(id), total, sizeof total);
    formatAmount(sfpew_perf_unit_t::ns, d.perFrame((double)d.sum(id)), per_frame, sizeof per_frame);
    formatAmount(sfpew_perf_unit_t::ns, (double)d.sum(id) / (double)d.hits(id), avg, sizeof avg);
    formatAmount(sfpew_perf_unit_t::ns, (double)d.max(id), peak, sizeof peak);
    snprintf(note, sizeof note, "%llu x, avg %s, max %s", (unsigned long long)d.hits(id), avg, peak);
    emit("    %-30s %12s %10s/f   %s", label, total, per_frame, note);
}

void summary(const interval_t& d) {
    uint64_t category[kCategoryCount] = {};
    for (unsigned i = 0; i < sfpewPerfBackendCount(); ++i)
        category[static_cast<unsigned>(sfpewPerfBackendCategory(i))] += d.backend(i);
    const auto in = [&](sfpew_perf_category_t c) {
        return (double)category[static_cast<unsigned>(c)];
    };

    if (d.hits(P::frame_time) != 0) {
        char avg[24], peak[24];
        formatAmount(sfpew_perf_unit_t::ns, (double)d.sum(P::frame_time) / (double)d.hits(P::frame_time),
                     avg, sizeof avg);
        formatAmount(sfpew_perf_unit_t::ns, (double)d.max(P::frame_time), peak, sizeof peak);
        emit("  frame time: avg %s, max %s", avg, peak);
    }
    emit("  %-32s %12s %12s", "", "per second", "per frame");

    // Draws: what the application asked for, what reached the driver, and
    // the work each driver draw carried.
    const double app_immediate = (double)d.hits(P::app_draw_immediate);
    const double app_arrays = (double)d.hits(P::app_draw_arrays);
    const double app_elements = (double)d.hits(P::app_draw_elements);
    const double app_multi = (double)d.sum(P::app_draw_multi);
    const double app_draws = app_immediate + app_arrays + app_elements + app_multi;
    const double driver_draws = in(sfpew_perf_category_t::draw);
    char note[128];
    snprintf(note, sizeof note, "immediate %.0f%%, arrays %.0f%%, elements %.0f%%, multi %.0f%%",
             percent(app_immediate, app_draws), percent(app_arrays, app_draws),
             percent(app_elements, app_draws), percent(app_multi, app_draws));
    emit("  draws");
    countRow(d, "app draws", app_draws, note);
    countRow(d, "app display lists called", (double)d.sum(P::app_call_list));
    // A list call is an application request that replays as draws, so it
    // counts alongside direct draws when asking how much the batching saved.
    const double app_requests = app_draws + (double)d.sum(P::app_call_list);
    snprintf(note, sizeof note, "%.2f per app draw or list call",
             app_requests > 0.0 ? driver_draws / app_requests : 0.0);
    countRow(d, "driver draws", driver_draws, note);
    const double vertices = (double)d.sum(P::gl_draw_vertices) + (double)d.sum(P::gl_draw_indices);
    snprintf(note, sizeof note, "%.1f per driver draw", driver_draws > 0.0 ? vertices / driver_draws : 0.0);
    countRow(d, "driver vertices + indices", vertices, note);
    const double immediate_vertices = (double)d.sum(P::app_draw_immediate);
    if (app_immediate > 0.0) {
        snprintf(note, sizeof note, "%.1f per run, %.0f%% of runs merged", immediate_vertices / app_immediate,
                 percent((double)d.hits(P::immediate_merged_run), app_immediate));
        countRow(d, "immediate-mode vertices", immediate_vertices, note);
    }

    // Binds and state: what the application issued, what the wrapper dropped
    // as already in effect, and what the driver saw.
    const double app_binds = (double)(d.hits(P::app_bind_texture) + d.hits(P::app_active_texture) +
                                      d.hits(P::app_bind_buffer) + d.hits(P::app_bind_vertex_array) +
                                      d.hits(P::app_use_program));
    const double dropped_binds =
        (double)(d.hits(P::redundant_bind_texture) + d.hits(P::redundant_active_texture));
    emit("  binds and state");
    snprintf(note, sizeof note, "%.1f%% dropped as redundant", percent(dropped_binds, app_binds));
    countRow(d, "app binds", app_binds, note);
    snprintf(note, sizeof note, "%.2f per driver draw",
             driver_draws > 0.0 ? in(sfpew_perf_category_t::bind) / driver_draws : 0.0);
    countRow(d, "driver binds", in(sfpew_perf_category_t::bind), note);
    countRow(d, "driver vertex setup", in(sfpew_perf_category_t::vertex_setup));
    snprintf(note, sizeof note, "%.1f%% dropped as redundant",
             percent((double)d.hits(P::redundant_state), (double)d.hits(P::app_state)));
    countRow(d, "app state (shadowed)", (double)d.hits(P::app_state), note);
    countRow(d, "driver state", in(sfpew_perf_category_t::state));
    snprintf(note, sizeof note, "%.1f%% answered without the driver",
             percent((double)d.hits(P::app_get_wrapper),
                     (double)(d.hits(P::app_get_wrapper) + d.hits(P::app_get_backend))));
    countRow(d, "app queries", (double)(d.hits(P::app_get_wrapper) + d.hits(P::app_get_backend)), note);
    countRow(d, "driver queries", in(sfpew_perf_category_t::query));

    // Bytes moved: what the driver was handed, and what the wrapper copied on
    // the CPU first. Timed ones show the rate the copy ran at.
    emit("  bytes moved");
    bytesRow(d, "driver buffer upload", P::gl_buffer_upload);
    bytesRow(d, "driver buffer allocation", P::gl_buffer_alloc);
    bytesRow(d, "driver buffer copy", P::gl_buffer_copy);
    bytesRow(d, "driver buffer map", P::gl_buffer_map);
    bytesRow(d, "driver texture upload", P::gl_texture_upload);
    bytesRow(d, "driver readback", P::gl_readback);
    bytesRow(d, "driver uniforms", P::gl_uniform);
    bytesRow(d, "wrapper copy: ring upload", P::copy_ring_upload);
    bytesRow(d, "wrapper copy: client gather", P::copy_client_gather);
    bytesRow(d, "wrapper copy: list capture", P::copy_list_capture);
    bytesRow(d, "wrapper copy: BGRA convert", P::copy_bgra_convert);

    // Where time went that a frame did not ask for.
    emit("  time spent");
    timerRow(d, "driver sync waits", P::gl_sync_wait);
    timerRow(d, "ring fence waits", P::ring_fence_wait);
    timerRow(d, "program generation", P::program_generate);
    timerRow(d, "program compile + link", P::program_compile);
    timerRow(d, "program uniform setup", P::program_uniform_init);

    // Caches and the context: the rates that say whether the fast paths hold.
    emit("  fast paths");
    const double keys = (double)(d.hits(P::program_key_fast) + d.hits(P::program_key_hashed));
    snprintf(note, sizeof note, "%.2f%% took the fast path", percent((double)d.hits(P::program_key_fast), keys));
    countRow(d, "program key lookups", keys, note);
    snprintf(note, sizeof note, "%.2f%% reused the cached layout",
             percent((double)d.hits(P::list_replay_layout_hit), (double)d.hits(P::list_replay)));
    countRow(d, "display-list draw replays", (double)d.hits(P::list_replay), note);
    snprintf(note, sizeof note, "%.2f%% reused the normalized layout",
             percent((double)d.hits(P::draw_arrays_layout_reuse), (double)d.hits(P::draw_arrays)));
    countRow(d, "fixed-function array draws", (double)d.hits(P::draw_arrays), note);
    snprintf(note, sizeof note, "%llu context switches", (unsigned long long)d.hits(P::context_switch));
    countRow(d, "libEGL context queries", (double)d.hits(P::context_egl_query), note);
    countRow(d, "app state handed back", (double)d.hits(P::barrier_restore));
}

void categories(const interval_t& d) {
    uint64_t category[kCategoryCount] = {};
    uint64_t total = 0;
    for (unsigned i = 0; i < sfpewPerfBackendCount(); ++i) {
        category[static_cast<unsigned>(sfpewPerfBackendCategory(i))] += d.backend(i);
        total += d.backend(i);
    }
    if (total == 0) return;
    char per_second[24], per_frame[24];
    formatCount(d.perSecond((double)total), per_second, sizeof per_second);
    formatCount(d.perFrame((double)total), per_frame, sizeof per_frame);
    emit("  driver calls by category: %s/s, %s/frame", per_second, per_frame);
    for (unsigned c = 0; c < kCategoryCount; ++c) {
        if (category[c] == 0) continue;
        formatCount(d.perSecond((double)category[c]), per_second, sizeof per_second);
        formatCount(d.perFrame((double)category[c]), per_frame, sizeof per_frame);
        emit("    %-30s %12s %12s   %5.1f%%",
             sfpewPerfCategoryName(static_cast<sfpew_perf_category_t>(c)), per_second, per_frame,
             percent((double)category[c], (double)total));
    }
}

void topFunctions(const interval_t& d) {
    std::vector<unsigned> order;
    for (unsigned i = 0; i < sfpewPerfBackendCount(); ++i)
        if (d.backend(i) != 0) order.push_back(i);
    if (order.empty() || topCount() == 0) return;
    std::sort(order.begin(), order.end(),
              [&](unsigned a, unsigned b) { return d.backend(a) > d.backend(b); });
    if (order.size() > topCount()) order.resize(topCount());
    emit("  busiest driver functions");
    for (unsigned slot : order) {
        char per_second[24], per_frame[24];
        formatCount(d.perSecond((double)d.backend(slot)), per_second, sizeof per_second);
        formatCount(d.perFrame((double)d.backend(slot)), per_frame, sizeof per_frame);
        emit("    %-30s %12s %12s   %s", sfpewPerfBackendName(slot), per_second, per_frame,
             sfpewPerfCategoryName(sfpewPerfBackendCategory(slot)));
    }
}

// Every counter that moved, for the questions the summary does not ask.
void allCounters(const interval_t& d) {
    emit("  all counters                       hits/s     hits/f      sum/f        avg        max");
    for (unsigned i = 0; i < kPerfCounterCount; ++i) {
        const auto id = static_cast<P>(i);
        if (d.hits(id) == 0) continue;
        const auto unit = kCounters[i].unit;
        char per_second[24], per_frame[24], sum_frame[24] = "", avg[24] = "", peak[24] = "";
        formatCount(d.perSecond((double)d.hits(id)), per_second, sizeof per_second);
        formatCount(d.perFrame((double)d.hits(id)), per_frame, sizeof per_frame);
        if (unit != sfpew_perf_unit_t::events) {
            formatAmount(unit, d.perFrame((double)d.sum(id)), sum_frame, sizeof sum_frame);
            formatAmount(unit, (double)d.sum(id) / (double)d.hits(id), avg, sizeof avg);
            formatAmount(unit, (double)d.max(id), peak, sizeof peak);
        }
        emit("    %-30s %10s %10s %10s %10s %10s", kCounters[i].name, per_second, per_frame,
             sum_frame, avg, peak);
    }
}

void csv(const char* title, const interval_t& d) {
    FILE* file = csvFile();
    if (file == nullptr) return;
    static const char* const kUnits[] = {"events", "items", "bytes", "ns"};
    for (unsigned i = 0; i < kPerfCounterCount; ++i) {
        const auto id = static_cast<P>(i);
        if (d.hits(id) == 0) continue;
        fprintf(file, "\"%s\",%.0f,%.6f,%s,%s,%llu,%llu,%llu,%llu\n", title, d.frames, d.seconds,
                kCounters[i].name, kUnits[static_cast<int>(kCounters[i].unit)],
                (unsigned long long)d.hits(id), (unsigned long long)d.sum(id),
                (unsigned long long)d.ns(id), (unsigned long long)d.max(id));
    }
    for (unsigned slot = 0; slot < sfpewPerfBackendCount(); ++slot) {
        if (d.backend(slot) == 0) continue;
        fprintf(file, "\"%s\",%.0f,%.6f,call.%s,calls,%llu,0,0,0\n", title, d.frames, d.seconds,
                sfpewPerfBackendName(slot), (unsigned long long)d.backend(slot));
    }
}

void report(const char* title, const snapshot_t& from, const snapshot_t& to) {
    const interval_t d{from, to, (double)(to.frames - from.frames),
                       (double)(to.time - from.time) / 1e9};
    emit("SFPEW PERF %s: %.0f frames in %.2f s (%.1f fps)", title, d.frames, d.seconds,
         d.perSecond(d.frames));
    summary(d);
    categories(d);
    topFunctions(d);
    allCounters(d);
    csv(title, d);
}

// Starts the next interval's maxima from zero, keeping the run's.
void foldMaxima() {
    for (unsigned i = 0; i < kPerfCounterCount; ++i) {
        const uint64_t max = g_sfpew_perf[i].max.load(std::memory_order_relaxed);
        g_run_max[i] = std::max(g_run_max[i], max);
        g_sfpew_perf[i].max.store(0, std::memory_order_relaxed);
    }
}

snapshot_t runTotal() {
    snapshot_t now = take();
    for (unsigned i = 0; i < kPerfCounterCount; ++i) now.max[i] = std::max(now.max[i], g_run_max[i]);
    return now;
}

// The run's totals, once, as the library goes away (process exit or
// dlclose). Android usually kills a process instead of letting it exit, so
// there the interval reports are the ones to rely on.
struct exit_reporter_t {
    ~exit_reporter_t() {
        if (reportingEnabled()) report("run total", epochSnapshot(), runTotal());
    }
} g_exit_reporter;

} // namespace

SFPEW_APIENTRY void sfpewPerfReset();

void sfpewPerfFrame(bool swap) {
    if (swap) {
        g_saw_swap = true;
    } else if (g_saw_swap) {
        return; // swaps are the frame boundary once there are any
    }
    static const unsigned long start_frame = envNumber("SFPEW_PERF_START", 0);
    static bool started = false;
    if (start_frame != 0 && !started && g_frames + 1 >= start_frame) {
        started = true;
        sfpewPerfReset();
        g_last_frame_time = sfpewPerfNow();
        return;
    }
    const uint64_t now = sfpewPerfNow();
    if (g_last_frame_time != 0) {
        const uint64_t elapsed = now - g_last_frame_time;
        sfpewPerfRecord(g_sfpew_perf[static_cast<unsigned>(P::frame_time)], elapsed, elapsed, elapsed);
    }
    g_last_frame_time = now;
    ++g_frames;

    const unsigned long interval = reportInterval();
    if (!reportingEnabled() || interval == 0 || g_frames % interval != 0) return;
    const snapshot_t taken = take();
    char title[64];
    const snapshot_t start = g_have_last_report ? g_last_report : epochSnapshot();
    snprintf(title, sizeof title, "frames %llu-%llu", (unsigned long long)start.frames + 1,
             (unsigned long long)taken.frames);
    report(title, start, taken);
    foldMaxima();
    g_last_report = taken;
    g_have_last_report = true;
}

// ---- Debug API ------------------------------------------------------------
// Exported for tests and tools that want the numbers rather than the log.
// Not GL entry points: these answer dlsym, not eglGetProcAddress, and exist
// only in a build with the counters. Indices run over the named counters
// first, then one per driver function ("call.<name>", hits only).

SFPEW_APIENTRY unsigned sfpewPerfCounterCount() { return kPerfCounterCount + sfpewPerfBackendCount(); }

SFPEW_APIENTRY const char* sfpewPerfCounterName(unsigned index) {
    if (index < kPerfCounterCount) return kCounters[index].name;
    index -= kPerfCounterCount;
    if (index >= sfpewPerfBackendCount()) return nullptr;
    // Stable storage for the prefixed names, built on first use.
    static std::vector<std::string> names;
    if (names.size() < sfpewPerfBackendCount()) {
        names.resize(sfpewPerfBackendCount());
        for (unsigned i = 0; i < names.size(); ++i)
            if (names[i].empty()) names[i] = std::string("call.") + sfpewPerfBackendName(i);
    }
    return names[index].c_str();
}

// Returns the counter's unit (0 events, 1 items, 2 bytes, 3 ns; driver
// functions read as events), or -1 for an index out of range. Any output may
// be null.
SFPEW_APIENTRY int sfpewPerfCounterReadAll(unsigned index, uint64_t* hits, uint64_t* sum,
                                           uint64_t* ns, uint64_t* max) {
    constexpr auto r = std::memory_order_relaxed;
    if (index < kPerfCounterCount) {
        const auto& slot = g_sfpew_perf[index];
        if (hits != nullptr) *hits = slot.hits.load(r);
        if (sum != nullptr) *sum = slot.sum.load(r);
        if (ns != nullptr) *ns = slot.ns.load(r);
        if (max != nullptr) *max = std::max(slot.max.load(r), g_run_max[index]);
        return static_cast<int>(kCounters[index].unit);
    }
    index -= kPerfCounterCount;
    if (index >= sfpewPerfBackendCount()) return -1;
    const uint64_t calls = g_sfpew_perf_backend[index].hits.load(r);
    if (hits != nullptr) *hits = calls;
    if (sum != nullptr) *sum = calls;
    if (ns != nullptr) *ns = 0;
    if (max != nullptr) *max = 0;
    return static_cast<int>(sfpew_perf_unit_t::events);
}

SFPEW_APIENTRY int sfpewPerfCounterRead(unsigned index, uint64_t* hits, uint64_t* sum) {
    return sfpewPerfCounterReadAll(index, hits, sum, nullptr, nullptr);
}

SFPEW_APIENTRY uint64_t sfpewPerfFrameCount() { return g_frames; }

// Zeroes every counter, the driver call counts and the frame count; the next
// interval report and the exit report then cover only what follows.
SFPEW_APIENTRY void sfpewPerfReset() {
    constexpr auto r = std::memory_order_relaxed;
    for (auto& slot : g_sfpew_perf) {
        slot.hits.store(0, r);
        slot.sum.store(0, r);
        slot.ns.store(0, r);
        slot.max.store(0, r);
    }
    for (auto& slot : g_sfpew_perf_backend) slot.hits.store(0, r);
    std::fill(std::begin(g_run_max), std::end(g_run_max), 0);
    g_frames = 0;
    g_last_frame_time = 0;
    g_have_last_report = false;
    g_epoch = sfpewPerfNow();
}

// The totals so far, now, whatever SFPEW_PERF says.
SFPEW_APIENTRY void sfpewPerfReport() { report("on request", epochSnapshot(), runTotal()); }
#endif
