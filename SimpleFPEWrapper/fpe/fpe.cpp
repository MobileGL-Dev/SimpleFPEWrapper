// SimpleFPEWrapper - SimpleFPEWrapper/fpe/fpe.cpp
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

#include "fpe.hpp"
#include "drawing1x.h"
#include <memory>
#include <mutex>
#include <glm/gtc/type_ptr.hpp>
#include <limits>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <algorithm>

#define DEBUG 0


// Set by the wrapper's own eglMakeCurrent when the app routes EGL through
// us. From then on this thread's current context is known exactly and even
// the strict resolve needs no EGL call at all - the difference between one
// plain pointer read and one libEGL entry per entry point, which on glvnd
// costs ~425ns (getpid fork check + dispatch mutex).
// Externally visible so the resolve's fast path can be inlined into every
// exported entry point (see sfpewResolveState in types.h): the call to
// get_instance was measured at 48% of a glGet, and every entry pays it.
thread_local SFPEW_TLS_HOT void* g_authoritative_context = nullptr;
thread_local SFPEW_TLS_HOT bool g_authoritative_context_known = false;
thread_local SFPEW_TLS_HOT unsigned g_context_reconcile_counter = 0;
// Starts tight so a bypassing app is caught early, then backs off.
thread_local SFPEW_TLS_HOT unsigned g_context_reconcile_interval = 64u;
thread_local SFPEW_TLS_HOT unsigned g_resolve_budget = 0u;
bool g_sfpew_relaxed_context = [] {
    const char* value = getenv("SFPEW_RELAXED_CONTEXT");
    return value != nullptr && value[0] != '\0' && value[0] != '0';
}();

void sfpewNoteCurrentContext(EGLContext context) {
    g_authoritative_context = context;
    g_authoritative_context_known = true;
    // The snapshot the resolve budget vouches for belongs to the OUTGOING
    // context. Spending the rest of it would run entry points against the
    // wrong per-context state, so the next one re-establishes.
    g_resolve_budget = 0u;
}

// The rare half of the resolve, kept out of line so the common half can be
// inlined into every entry point (sfpewResolveState in types.h).
//
// An app that routes SOME eglMakeCurrent calls through the wrapper and others
// straight to libEGL would leave the authoritative value stale, so the truth
// is re-read every 256 resolves - the same self-healing reconciliation the
// logical shadows use (plans/07). One libEGL query per ~256 GL calls keeps
// the fast path essentially free while bounding how long a bypassed switch
// can go unnoticed.
void* sfpewReconcileContext() {
    g_context_reconcile_counter = 0;
    if (g_eglFuncs.eglGetCurrentContext == nullptr)
        return g_authoritative_context_known ? g_authoritative_context : nullptr;
    void* const context = g_eglFuncs.eglGetCurrentContext();
    if (g_authoritative_context_known && context == g_authoritative_context) {
        // Confirmed again: trust it for longer. The cap keeps the bound well
        // inside a frame's worth of GL calls.
        if (g_context_reconcile_interval < 4096u) g_context_reconcile_interval *= 2u;
    } else {
        // Either the first query on this thread, or a switch the wrapper was
        // never told about. Take the new value and tighten back up.
        g_context_reconcile_interval = 64u;
        g_authoritative_context = context;
        g_authoritative_context_known = true;
    }
    return context;
}

EGLContext sfpewCurrentContext() { return sfpewCurrentContextInline(); }

// Thread-local snapshot of the last strict resolve. Shared by
// get_instance() / current() / current_vertex_data() / cached_context(), and
// by the inlined resolver in types.h. Declared there; see the note on the
// initial-exec model beside those declarations.
thread_local SFPEW_TLS_HOT void* tls_snapshot_context = (void*)(intptr_t)-1;
thread_local SFPEW_TLS_HOT glstate_t* tls_snapshot_state = nullptr;
namespace {

// SFPEW_RELAXED_CONTEXT=1: the app promises each thread uses at most one
// EGL context for the process lifetime (true for Minecraft-era launchers).
// Strict resolves then trust the snapshot after a thread's first resolve,
// removing the per-entry eglGetCurrentContext - which costs ~425ns per call
// on glvnd desktops (getpid fork check + dispatch mutex). Default: off,
// full lazy reconciliation per docs/context-model.md.
} // namespace

glstate_t& glstate_t::get_instance() {
    // Per-EGL-context FPE state (plans/07). The current context is resolved
    // lazily with a thread-local snapshot: this strict resolve runs once per
    // exported entry point and everything downstream uses current()
    // (docs/context-model.md). The resolve itself goes through
    // sfpewCurrentContext(), so an app that routes eglMakeCurrent through the
    // wrapper pays a TLS read rather than the ~425ns glvnd
    // eglGetCurrentContext (getpid fork check + dispatch mutex).
    //
    // Known limits (documented in plans/07): contexts cannot be observed
    // being destroyed, so their CPU-side state objects persist for the
    // process lifetime (the GL objects inside die with the context); and
    // share-group relationships are invisible, so display-list DEFINITIONS
    // stay process-global in DisplayListManager.
    static std::unordered_map<void*, std::unique_ptr<glstate_t>> instances;
    static std::mutex instances_mutex;
    static glstate_t no_context_state; // keeps backend-less calls crash-free

    // Refilling the budget is what lets the inlined fast path be a single
    // decrement; every return below therefore sets it.
    if (g_sfpew_relaxed_context && tls_snapshot_state != nullptr &&
        tls_snapshot_context != EGL_NO_CONTEXT) {
        // The app promised one context per thread for the process lifetime,
        // so the snapshot never needs re-establishing.
        g_resolve_budget = ~0u;
        return *tls_snapshot_state;
    }

    // Reconcile outright rather than through the counter-based query: the
    // budget refilled below already counts the entry points between two
    // reconciliations, and going through the counter as well would multiply
    // the two intervals together (64 x 64 entry points between libEGL
    // queries, and up to 16M once both had backed off).
    const EGLContext context = (EGLContext)sfpewReconcileContext();

    if (context == tls_snapshot_context && tls_snapshot_state != nullptr) {
        g_resolve_budget = g_context_reconcile_interval;
        return *tls_snapshot_state;
    }

    // The thread switched contexts. If the outgoing snapshot still holds an
    // open Begin/End batch, that batch was abandoned mid-collection: clear it
    // here so the vertex-data pin cannot route later calls (made on other
    // contexts) into the stale batch. This runs only on actual switches and
    // implements the documented drop-the-batch semantics (context-model.md).
    if (tls_snapshot_state != nullptr && tls_snapshot_context != EGL_NO_CONTEXT &&
        tls_snapshot_state->fpe_state.fpe_draw.primitive != kNoPrimitive) {
        tls_snapshot_state->fpe_state.fpe_draw.reset();
    }

    glstate_t* state = &no_context_state;
    if (context != EGL_NO_CONTEXT) {
        std::lock_guard<std::mutex> lock(instances_mutex);
        auto& slot = instances[context];
        if (!slot) slot = std::make_unique<glstate_t>();
        state = slot.get();
    }
    tls_snapshot_context = context;
    tls_snapshot_state = state;
    g_resolve_budget = g_context_reconcile_interval;
    return *state;
}

glstate_t& glstate_t::current() {
    if (tls_snapshot_state != nullptr) return *tls_snapshot_state;
    return get_instance();
}

glstate_t& glstate_t::current_vertex_data() { return sfpewVertexDataState(); }

void* glstate_t::cached_context() {
    if (tls_snapshot_state == nullptr) get_instance();
    return tls_snapshot_context;
}

GLsizei type_size(GLenum type) {
    switch (type) {
    case GL_BYTE:
    case GL_UNSIGNED_BYTE:
        return 1;
    case GL_SHORT:
    case GL_UNSIGNED_SHORT:
    case GL_HALF_FLOAT:
        return 2;
    case GL_INT:
    case GL_UNSIGNED_INT:
    case GL_FLOAT:
    case GL_FIXED:
        return 4;
    case GL_DOUBLE:
        return 8;
    default:
        // LOG_D("%s: unknown type: %s", __FUNCTION__, glEnumToString(type))
        return 0;
    }
}

namespace {

template <typename T>
T loadUnaligned(const uint8_t* source) {
    T value{};
    std::memcpy(&value, source, sizeof(value));
    return value;
}

float halfToVertexFloat(uint16_t bits) {
    const bool negative = (bits & 0x8000u) != 0;
    const uint16_t exponent = (bits >> 10u) & 0x1Fu;
    const uint16_t mantissa = bits & 0x03FFu;
    float value = 0.0f;
    if (exponent == 0) {
        value = std::ldexp(static_cast<float>(mantissa), -24);
    } else if (exponent == 0x1Fu) {
        value = mantissa == 0 ? std::numeric_limits<float>::infinity()
                              : std::numeric_limits<float>::quiet_NaN();
    } else {
        value = std::ldexp(1.0f + static_cast<float>(mantissa) / 1024.0f,
                           static_cast<int>(exponent) - 15);
    }
    return negative ? -value : value;
}

GLfloat readVertexComponent(const uint8_t* source, GLenum type) {
    switch (type) {
    case GL_BYTE: return static_cast<GLfloat>(loadUnaligned<GLbyte>(source));
    case GL_UNSIGNED_BYTE: return static_cast<GLfloat>(loadUnaligned<GLubyte>(source));
    case GL_SHORT: return static_cast<GLfloat>(loadUnaligned<GLshort>(source));
    case GL_UNSIGNED_SHORT: return static_cast<GLfloat>(loadUnaligned<GLushort>(source));
    case GL_INT: return static_cast<GLfloat>(loadUnaligned<GLint>(source));
    case GL_UNSIGNED_INT: return static_cast<GLfloat>(loadUnaligned<GLuint>(source));
    case GL_FLOAT: return loadUnaligned<GLfloat>(source);
    case GL_DOUBLE: return static_cast<GLfloat>(loadUnaligned<GLdouble>(source));
    case GL_HALF_FLOAT: return halfToVertexFloat(loadUnaligned<uint16_t>(source));
    case GL_FIXED: return static_cast<GLfloat>(loadUnaligned<GLint>(source)) / 65536.0f;
    default: return 0.0f;
    }
}

bool copyAttributeElements(const vertexattribute_t& attribute, GLuint array_buffer,
                           GLint first, GLsizei count, GLint component_count,
                           std::vector<uint8_t>& out) {
    if (first < 0 || count < 0 || component_count <= 0 ||
        component_count > attribute.size) {
        return false;
    }
    if (count == 0) {
        out.clear();
        return true;
    }
    const GLsizei component_size = type_size(attribute.type);
    if (component_size <= 0 || attribute.stride < 0) return false;

    const uint64_t element_size = static_cast<uint64_t>(component_count) * component_size;
    const uint64_t stride = attribute.stride != 0
                                ? static_cast<uint64_t>(attribute.stride)
                                : static_cast<uint64_t>(attribute.size) * component_size;
    const uint64_t first_offset = static_cast<uint64_t>(first) * stride;
    const uint64_t pointer_value = reinterpret_cast<uintptr_t>(attribute.pointer);
    if (first_offset > std::numeric_limits<uint64_t>::max() - pointer_value) return false;
    const uint64_t source_offset = pointer_value + first_offset;
    const uint64_t source_bytes = static_cast<uint64_t>(count - 1) * stride + element_size;
    const uint64_t packed_bytes = static_cast<uint64_t>(count) * element_size;
    if (source_bytes > static_cast<uint64_t>(std::numeric_limits<GLsizeiptr>::max()) ||
        source_offset > static_cast<uint64_t>(std::numeric_limits<GLintptr>::max()) ||
        packed_bytes > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
        return false;
    }

    out.resize(static_cast<size_t>(packed_bytes));
    const uint8_t* source = nullptr;
    void* mapped = nullptr;
    if (array_buffer == 0) {
        if (attribute.pointer == nullptr) return false;
        source = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(source_offset));
    } else {
        if (g_glFuncs.glBindBuffer == nullptr || g_glFuncs.glMapBufferRange == nullptr ||
            g_glFuncs.glUnmapBuffer == nullptr) {
            return false;
        }
        g_glFuncs.glBindBuffer(GL_ARRAY_BUFFER, array_buffer);
        g_glstate_c.immediate_live_buffer = array_buffer;
        mapped = g_glFuncs.glMapBufferRange(GL_ARRAY_BUFFER, static_cast<GLintptr>(source_offset),
                                            static_cast<GLsizeiptr>(source_bytes), GL_MAP_READ_BIT);
        if (mapped == nullptr) return false;
        source = static_cast<const uint8_t*>(mapped);
    }

    for (GLsizei vertex = 0; vertex < count; ++vertex) {
        std::memcpy(out.data() + static_cast<size_t>(vertex) * element_size,
                    source + static_cast<size_t>(vertex) * stride,
                    static_cast<size_t>(element_size));
    }
    if (mapped != nullptr && g_glFuncs.glUnmapBuffer(GL_ARRAY_BUFFER) == GL_FALSE) return false;
    return true;
}

} // namespace

bool sfpewReadVertexPositions(const vertexattribute_t& attribute, GLuint array_buffer,
                              GLint first, GLsizei count, std::vector<glm::vec4>& out) {
    if (attribute.size < 2 || attribute.size > 4) return false;
    thread_local std::vector<uint8_t> packed;
    if (!copyAttributeElements(attribute, array_buffer, first, count, attribute.size, packed))
        return false;
    const size_t component_size = static_cast<size_t>(type_size(attribute.type));
    const size_t element_size = static_cast<size_t>(attribute.size) * component_size;
    out.assign(static_cast<size_t>(count), glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
    for (GLsizei vertex = 0; vertex < count; ++vertex) {
        const uint8_t* element = packed.data() + static_cast<size_t>(vertex) * element_size;
        for (GLint component = 0; component < attribute.size; ++component) {
            glm::value_ptr(out[static_cast<size_t>(vertex)])[component] =
                readVertexComponent(element + static_cast<size_t>(component) * component_size,
                                    attribute.type);
        }
    }
    return true;
}

bool sfpewReadEdgeFlags(const vertexattribute_t& attribute, GLuint array_buffer,
                        GLint first, GLsizei count, std::vector<uint8_t>& out) {
    thread_local std::vector<uint8_t> packed;
    if (!copyAttributeElements(attribute, array_buffer, first, count, 1, packed)) return false;
    const size_t component_size = static_cast<size_t>(type_size(attribute.type));
    out.resize(static_cast<size_t>(count));
    for (GLsizei vertex = 0; vertex < count; ++vertex) {
        out[static_cast<size_t>(vertex)] =
            readVertexComponent(packed.data() + static_cast<size_t>(vertex) * component_size,
                                attribute.type) != 0.0f
                ? 1u
                : 0u;
    }
    return true;
}

bool sfpewDrawMixedPolygonMode(GLenum primitive, const glm::vec4* positions,
                               size_t position_count, const uint32_t* indices,
                               size_t index_count, uint32_t output_base,
                               const uint8_t* edge_flags, size_t edge_flag_count) {
    if (!sfpewMixedPolygonMode(primitive)) return false;
    if (positions == nullptr || position_count == 0 || index_count == 0) return true;

    auto& gs = g_glstate_c;
    const auto& uniform = gs.fpe_uniform;
    const auto& backend = gs.fpe_state.backend_state;
    using shadow_t = fixed_function_state_t::backend_state_shadow_t;
    bool cull_enabled = backend.enable_known[shadow_t::kEnableCullFace]
                            ? backend.enable_value[shadow_t::kEnableCullFace]
                            : false;
    GLenum cull_face = backend.cull_face;
    GLenum front_face = backend.front_face;
    // Mixed mode is deliberately cold and already CPU-bound. Querying here
    // makes facing exact even for the first shadowed glFrontFace/glCullFace
    // call and for applications that reached those core entries directly.
    if (g_glFuncs.glIsEnabled != nullptr)
        cull_enabled = g_glFuncs.glIsEnabled(GL_CULL_FACE) == GL_TRUE;
    if (g_glFuncs.glGetIntegerv != nullptr) {
        GLint value = 0;
        g_glFuncs.glGetIntegerv(GL_CULL_FACE_MODE, &value);
        if (value == GL_FRONT || value == GL_BACK || value == GL_FRONT_AND_BACK)
            cull_face = static_cast<GLenum>(value);
        g_glFuncs.glGetIntegerv(GL_FRONT_FACE, &value);
        if (value == GL_CW || value == GL_CCW) front_face = static_cast<GLenum>(value);
    }

    thread_local std::vector<glm::dvec2> projected;
    thread_local std::vector<uint8_t> projectable;
    projected.resize(position_count);
    projectable.assign(position_count, 0u);
    const glm::mat4 mvp = uniform.transformation.matrices[matrix_idx(GL_PROJECTION)] *
                          uniform.transformation.matrices[matrix_idx(GL_MODELVIEW)];
    for (size_t i = 0; i < position_count; ++i) {
        const glm::vec4 clip = mvp * positions[i];
        if (clip.w == 0.0f || !std::isfinite(clip.x) || !std::isfinite(clip.y) ||
            !std::isfinite(clip.w)) {
            continue;
        }
        projected[i] = glm::dvec2(static_cast<double>(clip.x) / clip.w,
                                  static_cast<double>(clip.y) / clip.w);
        projectable[i] = 1u;
    }

    thread_local std::vector<uint32_t> fill_indices;
    thread_local std::vector<uint32_t> line_indices;
    thread_local std::vector<uint32_t> point_indices;
    fill_indices.clear();
    line_indices.clear();
    point_indices.clear();

    bool index_overflow = false;
    const auto outputIndex = [&](uint32_t index) {
        if (index > std::numeric_limits<uint32_t>::max() - output_base) {
            index_overflow = true;
            return 0u;
        }
        return output_base + index;
    };
    const auto edgeEnabled = [&](uint32_t index) {
        return edge_flags == nullptr || index >= edge_flag_count || edge_flags[index] != 0;
    };
    const auto isCulled = [&](bool front) {
        if (!cull_enabled) return false;
        if (cull_face == GL_FRONT_AND_BACK) return true;
        return cull_face == (front ? GL_FRONT : GL_BACK);
    };

    const auto emitPolygon = [&](const uint32_t* polygon, size_t count) {
        if (count < 3) return;
        double signed_area = 0.0;
        for (size_t i = 0; i < count; ++i) {
            const uint32_t a = polygon[i];
            const uint32_t b = polygon[(i + 1) % count];
            if (a >= position_count || b >= position_count || !projectable[a] ||
                !projectable[b]) {
                return;
            }
            signed_area += projected[a].x * projected[b].y -
                           projected[a].y * projected[b].x;
        }
        const bool ccw = signed_area > 0.0;
        const bool front = front_face == GL_CW ? !ccw : ccw;
        if (isCulled(front)) return;
        const GLenum polygon_mode =
            front ? uniform.polygon_mode_front : uniform.polygon_mode_back;

        if (polygon_mode == GL_POINT) {
            for (size_t i = 0; i < count; ++i)
                point_indices.push_back(outputIndex(polygon[i]));
            return;
        }
        if (polygon_mode == GL_LINE) {
            for (size_t i = 0; i < count; ++i) {
                const uint32_t a = polygon[i];
                if (!edgeEnabled(a)) continue;
                line_indices.push_back(outputIndex(a));
                line_indices.push_back(outputIndex(polygon[(i + 1) % count]));
            }
            return;
        }

        // GL_FILL. Preserve the wrapper's established flat-shaded quad rule:
        // both generated triangles end on the quad's provoking vertex.
        if (count == 4 && gs.fpe_state.shade_model == GL_FLAT) {
            fill_indices.insert(fill_indices.end(),
                                {outputIndex(polygon[0]), outputIndex(polygon[1]),
                                 outputIndex(polygon[3]), outputIndex(polygon[1]),
                                 outputIndex(polygon[2]), outputIndex(polygon[3])});
        } else {
            for (size_t i = 1; i + 1 < count; ++i) {
                fill_indices.push_back(outputIndex(polygon[0]));
                fill_indices.push_back(outputIndex(polygon[i]));
                fill_indices.push_back(outputIndex(polygon[i + 1]));
            }
        }
    };

    const auto sourceIndex = [&](size_t stream_index) -> uint32_t {
        return indices != nullptr ? indices[stream_index] : static_cast<uint32_t>(stream_index);
    };
    switch (primitive) {
    case GL_TRIANGLES:
        for (size_t i = 0; i + 2 < index_count; i += 3) {
            const uint32_t polygon[] = {sourceIndex(i), sourceIndex(i + 1), sourceIndex(i + 2)};
            emitPolygon(polygon, 3);
        }
        break;
    case GL_TRIANGLE_STRIP:
        for (size_t i = 0; i + 2 < index_count; ++i) {
            const uint32_t polygon[] = {
                sourceIndex(i + (i & 1u)), sourceIndex(i + ((i & 1u) ? 0u : 1u)),
                sourceIndex(i + 2)};
            emitPolygon(polygon, 3);
        }
        break;
    case GL_TRIANGLE_FAN:
        for (size_t i = 1; i + 1 < index_count; ++i) {
            const uint32_t polygon[] = {sourceIndex(0), sourceIndex(i), sourceIndex(i + 1)};
            emitPolygon(polygon, 3);
        }
        break;
    case GL_QUADS:
        for (size_t i = 0; i + 3 < index_count; i += 4) {
            const uint32_t polygon[] = {sourceIndex(i), sourceIndex(i + 1),
                                        sourceIndex(i + 2), sourceIndex(i + 3)};
            emitPolygon(polygon, 4);
        }
        break;
    case GL_QUAD_STRIP:
        for (size_t i = 0; i + 3 < index_count; i += 2) {
            const uint32_t polygon[] = {sourceIndex(i), sourceIndex(i + 1),
                                        sourceIndex(i + 3), sourceIndex(i + 2)};
            emitPolygon(polygon, 4);
        }
        break;
    case GL_POLYGON: {
        thread_local std::vector<uint32_t> polygon;
        polygon.resize(index_count);
        for (size_t i = 0; i < index_count; ++i) polygon[i] = sourceIndex(i);
        emitPolygon(polygon.data(), polygon.size());
        break;
    }
    default:
        return false;
    }

    if (index_overflow) {
        gs.set_error(GL_INVALID_VALUE);
        return true;
    }
    const auto draw = [&](GLenum mode, const std::vector<uint32_t>& draw_indices) {
        if (draw_indices.empty()) return true;
        if (g_glFuncs.glDrawElements == nullptr || !sfpewUploadWireframeIndices(draw_indices))
            return false;
        g_glFuncs.glDrawElements(mode, static_cast<GLsizei>(draw_indices.size()),
                                 GL_UNSIGNED_INT, (void*)0);
        return true;
    };
    if (!draw(GL_TRIANGLES, fill_indices) || !draw(GL_LINES, line_indices) ||
        !draw(GL_POINTS, point_indices)) {
        gs.set_error(GL_INVALID_OPERATION);
    }
    return true;
}

void sfpewBuildWireframeIndices(GLenum mode, uint32_t base, uint32_t n,
                                std::vector<uint32_t>& out, const uint8_t* edge_flags,
                                size_t flag_count) {
    out.clear();
    const auto edge = [&](uint32_t a, uint32_t b) {
        // The edge belongs to the vertex it leaves; a cleared flag there
        // makes it interior and it is not drawn. Vertices past the end of
        // the flag array keep the default, so a short array cannot drop
        // geometry.
        if (edge_flags != nullptr && a < flag_count && edge_flags[a] == 0) return;
        out.push_back(base + a);
        out.push_back(base + b);
    };
    // Each PRIMITIVE gets its own outline. For the triangle modes that means
    // per-triangle outlines, shared interior edges included - drawing a
    // triangle strip as a wireframe really does show every triangle. But
    // GL_POLYGON is ONE primitive and GL_QUAD_STRIP is a run of quads, so
    // outlining those must trace their boundaries only; decomposing them
    // first would draw diagonals the application never described.
    switch (mode) {
    case GL_TRIANGLES:
        for (uint32_t i = 0; i + 2 < n; i += 3) {
            edge(i, i + 1); edge(i + 1, i + 2); edge(i + 2, i);
        }
        break;
    case GL_QUADS:
        for (uint32_t i = 0; i + 3 < n; i += 4) {
            edge(i, i + 1); edge(i + 1, i + 2); edge(i + 2, i + 3); edge(i + 3, i);
        }
        break;
    case GL_QUAD_STRIP:
        // Quad k is (2k, 2k+1, 2k+3, 2k+2) - note the last pair swaps, which
        // is what makes a quad strip wind consistently.
        for (uint32_t i = 0; i + 3 < n; i += 2) {
            edge(i, i + 1); edge(i + 1, i + 3); edge(i + 3, i + 2); edge(i + 2, i);
        }
        break;
    case GL_TRIANGLE_STRIP:
        for (uint32_t i = 0; i + 2 < n; ++i) {
            edge(i, i + 1); edge(i + 1, i + 2); edge(i + 2, i);
        }
        break;
    case GL_POLYGON:
        // A single convex polygon: its boundary, closed.
        if (n >= 3) {
            for (uint32_t i = 0; i + 1 < n; ++i) edge(i, i + 1);
            edge(n - 1, 0);
        }
        break;
    default: // GL_TRIANGLE_FAN
        for (uint32_t i = 1; i + 1 < n; ++i) {
            edge(0, i); edge(i, i + 1); edge(i + 1, 0);
        }
        break;
    }
}

bool sfpewUploadWireframeIndices(const std::vector<uint32_t>& wire) {
    auto& st = g_glstate_c.fpe_state;
    if (st.fpe_element_ibo == 0) {
        if (g_glFuncs.glGenBuffers == nullptr) return false;
        g_glFuncs.glGenBuffers(1, &st.fpe_element_ibo);
        sfpewNoteInternalBuffer(st.fpe_element_ibo);
        if (st.fpe_element_ibo == 0) return false;
    }
    sfpewBackendBindElementBuffer(st.fpe_element_ibo);
    st.fpe_ibo_bound = false; // fpe_vao's element binding changed
    g_glFuncs.glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                           (GLsizeiptr)(wire.size() * sizeof(uint32_t)), wire.data(),
                           GL_DYNAMIC_DRAW);
    return true;
}

bool prepare_quad_indices(GLsizei n, GLuint first) {
    auto& state = g_glstate_c.fpe_state;
    const size_t num_quads = n > 0 ? static_cast<size_t>(n) / 4u : 0u;
    const uint64_t max_index = num_quads == 0
                                   ? static_cast<uint64_t>(first)
                                   : static_cast<uint64_t>(first) + num_quads * 4u - 1u;
    const GLenum index_type = max_index <= std::numeric_limits<uint16_t>::max()
                                  ? GL_UNSIGNED_SHORT
                                  : GL_UNSIGNED_INT;
    // Which diagonal splits each quad. Flat shading takes a primitive's
    // color from its LAST vertex, and desktop GL_QUADS defines that to be
    // the quad's 4th vertex - but the usual 0-2 diagonal produces triangles
    // (0,1,2) and (2,3,0), neither of which even CONTAINS vertex 3, so a
    // flat-shaded quad came out in the wrong color entirely. The 1-3
    // diagonal gives (0,1,3) and (1,2,3): both end on vertex 3, which is
    // exactly the desktop rule.
    //
    // Only used when GL_FLAT is actually current. Both diagonals cover the
    // same area for the convex planar quads GL_QUADS requires, but they
    // interpolate a smooth-shaded quad's per-vertex colors differently, and
    // Minecraft's terrain is exactly that - so the smooth path keeps the
    // triangulation it has always used and its rendering is untouched.
    // Found by the piglit dlist-shademodel port.
    const bool flat = g_glstate_c.fpe_state.shade_model == GL_FLAT;
    if (state.fpe_ib_valid && state.fpe_ib_first == first && state.fpe_ib_type == index_type &&
        state.fpe_ib_flat == flat && state.fpe_ib_quad_count >= num_quads) {
        return false;
    }

    size_t first_quad_to_generate = 0;
    if (state.fpe_ib_valid && state.fpe_ib_first == first && state.fpe_ib_type == index_type &&
        state.fpe_ib_flat == flat) {
        first_quad_to_generate = state.fpe_ib_quad_count;
    }

    // (a,b,c) offsets of the two triangles, per diagonal.
    const unsigned t0[3] = {0u, 1u, flat ? 3u : 2u};
    const unsigned t1[3] = {flat ? 1u : 2u, flat ? 2u : 3u, flat ? 3u : 0u};

    if (index_type == GL_UNSIGNED_SHORT) {
        state.fpe_ib16.resize(num_quads * 6u);
        for (size_t i = first_quad_to_generate; i < num_quads; ++i) {
            const uint16_t base_index = static_cast<uint16_t>(first + i * 4u);
            for (unsigned k = 0; k < 3u; ++k) {
                state.fpe_ib16[i * 6u + k] = static_cast<uint16_t>(base_index + t0[k]);
                state.fpe_ib16[i * 6u + 3u + k] = static_cast<uint16_t>(base_index + t1[k]);
            }
        }
    } else {
        state.fpe_ib.resize(num_quads * 6u);
        for (size_t i = first_quad_to_generate; i < num_quads; ++i) {
            const uint32_t base_index = first + static_cast<uint32_t>(i * 4u);
            for (unsigned k = 0; k < 3u; ++k) {
                state.fpe_ib[i * 6u + k] = base_index + t0[k];
                state.fpe_ib[i * 6u + 3u + k] = base_index + t1[k];
            }
        }
    }
    state.fpe_ib_first = first;
    state.fpe_ib_quad_count = num_quads;
    state.fpe_ib_type = index_type;
    state.fpe_ib_flat = flat;
    state.fpe_ib_valid = true;
    return true;
}

const void* quad_index_data() {
    const auto& state = g_glstate_c.fpe_state;
    return state.fpe_ib_type == GL_UNSIGNED_SHORT
               ? static_cast<const void*>(state.fpe_ib16.data())
               : static_cast<const void*>(state.fpe_ib.data());
}

size_t quad_index_size_bytes() {
    const auto& state = g_glstate_c.fpe_state;
    return state.fpe_ib_type == GL_UNSIGNED_SHORT ? state.fpe_ib16.size() * sizeof(uint16_t)
                                                  : state.fpe_ib.size() * sizeof(uint32_t);
}

GLenum quad_index_type() {
    return g_glstate_c.fpe_state.fpe_ib_type;
}

#if DEBUG || GLOBAL_DEBUG
void log_vtx_attrib_data(const void* ptr, GLenum type, int size, int stride, int offset, int idx) {
    const char* p = (const char*)ptr + idx * stride + offset;
    switch (type) {
    case GL_FLOAT: {
        // LOG_D_N("(GL_FLOAT): (")
        const GLfloat* p_data = (const GLfloat*)p;
        for (int i = 0; i < size; ++i) {
            // LOG_D_N("%.2f, ", p_data[i]);
        }
        // LOG_D_N(") ")
        break;
    }
    case GL_UNSIGNED_BYTE: {
        // LOG_D_N("(GL_UNSIGNED_BYTE): (")
        const GLubyte* p_data = (const GLubyte*)p;
        for (int i = 0; i < size; ++i) {
            // LOG_D_N("%hhu, ", p_data[i]);
        }
        // LOG_D_N(") ")
    }
    }
}
#endif

int init_fpe() {
    // LOG_I("Initializing fixed-function pipeline...")

    if (g_glstate_c.fpe_ready) return 0;

    if (g_eglFuncs.eglGetCurrentContext == nullptr || g_eglFuncs.eglGetCurrentContext() == EGL_NO_CONTEXT) {
        return -1;
    }

    if (g_glFuncs.glGenVertexArrays == nullptr || g_glFuncs.glDeleteVertexArrays == nullptr ||
        g_glFuncs.glGenBuffers == nullptr || g_glFuncs.glDeleteBuffers == nullptr) {
        SFPEW::Utils::BackendLoader::AcquireBackendGLFunctions(g_glFuncs, g_eglFuncs.eglGetProcAddress);
    }
    if (g_glFuncs.glGenVertexArrays == nullptr || g_glFuncs.glDeleteVertexArrays == nullptr ||
        g_glFuncs.glGenBuffers == nullptr || g_glFuncs.glDeleteBuffers == nullptr) {
        return -1;
    }

    g_glFuncs.glGenVertexArrays(1, &g_glstate_c.fpe_state.fpe_vao);

    g_glFuncs.glGenBuffers(1, &g_glstate_c.fpe_state.fpe_vbo);
    sfpewNoteInternalBuffer(g_glstate_c.fpe_state.fpe_vbo);

    g_glFuncs.glGenBuffers(1, &g_glstate_c.fpe_state.fpe_immediate_vbo);
    sfpewNoteInternalBuffer(g_glstate_c.fpe_state.fpe_immediate_vbo);
    g_glstate_c.fpe_state.fpe_immediate_vbo_capacity = 0;
    g_glstate_c.fpe_state.fpe_immediate_vbo_offset = 0;
    g_glstate_c.fpe_state.fpe_immediate_vbo_map = nullptr;
    g_glstate_c.fpe_state.fpe_immediate_vbo_persistent_attempted = false;

    g_glFuncs.glGenBuffers(1, &g_glstate_c.fpe_state.fpe_ibo);
    sfpewNoteInternalBuffer(g_glstate_c.fpe_state.fpe_ibo);

    // LOG_D("fpe_vao: %d", g_glstate_c.fpe_state.fpe_vao)
    // LOG_D("fpe_vbo: %d", g_glstate_c.fpe_state.fpe_vbo)
    // LOG_D("fpe_ibo: %d", g_glstate_c.fpe_state.fpe_ibo)

    if (g_glstate_c.fpe_state.fpe_vao == 0 || g_glstate_c.fpe_state.fpe_vbo == 0 ||
        g_glstate_c.fpe_state.fpe_immediate_vbo == 0 ||
        g_glstate_c.fpe_state.fpe_ibo == 0) {
        if (g_glstate_c.fpe_state.fpe_vao != 0)
            g_glFuncs.glDeleteVertexArrays(1, &g_glstate_c.fpe_state.fpe_vao);
        if (g_glstate_c.fpe_state.fpe_vbo != 0)
            sfpewForgetInternalBuffer(g_glstate_c.fpe_state.fpe_vbo);
            g_glFuncs.glDeleteBuffers(1, &g_glstate_c.fpe_state.fpe_vbo);
        if (g_glstate_c.fpe_state.fpe_immediate_vbo != 0)
            sfpewForgetInternalBuffer(g_glstate_c.fpe_state.fpe_immediate_vbo);
            g_glFuncs.glDeleteBuffers(1, &g_glstate_c.fpe_state.fpe_immediate_vbo);
        if (g_glstate_c.fpe_state.fpe_ibo != 0)
            sfpewForgetInternalBuffer(g_glstate_c.fpe_state.fpe_ibo);
            g_glFuncs.glDeleteBuffers(1, &g_glstate_c.fpe_state.fpe_ibo);
        g_glstate_c.fpe_state.fpe_vao = 0;
        g_glstate_c.fpe_state.fpe_vbo = 0;
        g_glstate_c.fpe_state.fpe_immediate_vbo = 0;
        g_glstate_c.fpe_state.fpe_immediate_vbo_capacity = 0;
        g_glstate_c.fpe_state.fpe_immediate_vbo_offset = 0;
        g_glstate_c.fpe_state.fpe_immediate_vbo_map = nullptr;
        g_glstate_c.fpe_state.fpe_immediate_vbo_persistent_attempted = false;
        g_glstate_c.fpe_state.fpe_ibo = 0;
        return -1;
    }

    g_glstate_c.fpe_ready = true;
    return 0;
}

// Multiple INDEPENDENT client-memory arrays (classic GL 1.1: separate
// glVertexPointer/glColorPointer allocations) cannot be expressed by
// normalize()'s single interleave stride. Gather them into one interleaved
// stream instead; correctness first, the copy is bounded by the draw size.
// External linkage: the user-program draw paths share it (fpe.hpp).
bool gather_client_arrays(const vertex_pointer_array_t& raw, GLint first, GLsizei count,
                          vertex_pointer_array_t* out) {
    if (count <= 0 || first < 0) return false;
    int enabled_count = 0;
    size_t element_bytes[VERTEX_POINTER_COUNT] = {};
    size_t total_stride = 0;
    GLsizei shared_stride = -1;
    bool all_explicit_stride = true;
    uintptr_t window_begin = UINTPTR_MAX;
    uintptr_t window_end = 0;
    for (int i = 0; i < VERTEX_POINTER_COUNT; ++i) {
        if (!((raw.enabled_pointers >> i) & 1u)) continue;
        const auto& attr = raw.attributes[i];
        if (getClientArrayBufferBinding(i) != 0) return false; // VBO mix: original path
        if (attr.pointer == nullptr || attr.size <= 0 || type_size(attr.type) == 0) return false;
        ++enabled_count;
        element_bytes[i] = (size_t)attr.size * (size_t)type_size(attr.type);
        total_stride += element_bytes[i];
        if (attr.stride == 0) all_explicit_stride = false;
        const GLsizei effective_stride =
            attr.stride != 0 ? attr.stride : (GLsizei)element_bytes[i];
        if (shared_stride < 0) shared_stride = effective_stride;
        else if (effective_stride != shared_stride) shared_stride = 0;
        const auto ptr = reinterpret_cast<uintptr_t>(attr.pointer);
        window_begin = std::min(window_begin, ptr);
        window_end = std::max(window_end, ptr + element_bytes[i]);
    }
    if (enabled_count < 2 || total_stride == 0) return false;

    // Already-interleaved layout (the Minecraft chunk shape): every enabled
    // attribute declares the SAME explicit stride and lives inside a single
    // stride window, so normalize()'s single-block zero-copy path covers it.
    // Tight (stride 0) arrays stay on the gather - normalize's rebase logic
    // does not handle aliased or adjacent tight arrays.
    if (all_explicit_stride && shared_stride > 0 &&
        window_end - window_begin <= (uintptr_t)shared_stride) {
        return false;
    }

    static thread_local std::vector<uint8_t> gathered;
    const size_t total_size = (size_t)count * total_stride;
    if (total_size > (size_t)std::numeric_limits<GLsizei>::max()) return false;
    gathered.resize(total_size);

    *out = raw;
    size_t attribute_offset = 0;
    for (int i = 0; i < VERTEX_POINTER_COUNT; ++i) {
        if (!((raw.enabled_pointers >> i) & 1u)) continue;
        const auto& attr = raw.attributes[i];
        const size_t src_stride =
            attr.stride != 0 ? (size_t)attr.stride : element_bytes[i];
        const auto* src = static_cast<const uint8_t*>(attr.pointer) + (size_t)first * src_stride;
        uint8_t* dst = gathered.data() + attribute_offset;
        for (GLsizei v = 0; v < count; ++v)
            std::memcpy(dst + (size_t)v * total_stride, src + (size_t)v * src_stride,
                        element_bytes[i]);
        out->attributes[i].pointer = (const void*)attribute_offset;
        out->attributes[i].stride = (GLsizei)total_stride;
        attribute_offset += element_bytes[i];
    }
    out->starting_pointer = gathered.data();
    out->stride = (GLsizei)total_stride;
    return true;
}


int commit_fpe_state_on_draw(GLenum* mode, GLint* first, GLsizei* count, GLint previous_array_buffer) {
    // LOG()

    if (!g_glstate_c.fpe_ready) {
        if (init_fpe() != 0) return -1;
    }

    // Need to generate_compressed_index first (shadergen will use that)
    auto& raw_vpa = g_glstate_c.fpe_state.vertexpointer_array;
    auto& vpa = g_glstate_c.fpe_state.normalized_vpa;
    // The caller's original first, before the gather folds it into the
    // upload. The edge-flag array below is read through the RAW pointers,
    // which the gather does not touch, so it must be indexed with this.
    const GLint raw_first = *first;
    // Layout reuse: normalize() reads only the pointer declarations (sizes,
    // types, strides, client pointers) - NOT which buffer is bound, which
    // lives in client_array_buffer_bindings and feeds the attribute send
    // separately. MC's chunk loop respecifies BYTE-IDENTICAL pointers per
    // chunk with only the bound VBO changing (the gl*Pointer entries skip
    // the dirty mark for an identical respec), so the normalized layout,
    // its compressed index and the raw mirror are all still exact. A
    // gathered result is never reused: it bakes first/count into the copy.
    const auto& live_sizes = g_glstate_c.fpe_state.fpe_draw.current_data.sizes;
    const bool layout_reused =
        !raw_vpa.dirty && g_glstate_c.fpe_normalized_valid &&
        std::memcmp(&g_glstate_c.fpe_normalized_sizes, &live_sizes, sizeof(live_sizes)) == 0;
    if (!layout_reused) {
        if (gather_client_arrays(raw_vpa, *first, *count, &vpa)) {
            *first = 0; // the gather already applied the base offset
            g_glstate_c.fpe_normalized_valid = false;
        } else {
            vpa = raw_vpa.normalize();
            g_glstate_c.fpe_normalized_valid = true;
            g_glstate_c.fpe_normalized_sizes = live_sizes;
        }
        vpa.generate_compressed_index(g_glstate_c.fpe_state.fpe_draw.current_data.sizes.data);
        // kinda cursed...
        raw_vpa.generate_compressed_index(g_glstate_c.fpe_state.fpe_draw.current_data.sizes.data);
        // Consumed: an identical respec keeps it clear, a real layout change
        // sets it again and rebuilds here.
        raw_vpa.dirty = false;
    }

    auto key = g_glstate_c.program_hash();
    // LOG_D("%s: key=0x%x", __func__, key)
    auto& prog = g_glstate_c.get_or_generate_program(key);
    int prog_id = prog.get_program();
    if (prog_id <= 0) {
        // Generated program failed to compile/link: the draw is dropped, and
        // per the error contract that must be observable, not silent.
        g_glstate_c.set_error(GL_INVALID_OPERATION);
        return -1;
    }
    // The same program/VAO arm the immediate path uses: while the app's draw
    // state is still held, a preceding FPE draw left exactly this pair bound,
    // and re-issuing glUseProgram + glBindVertexArray per draw is pure
    // driver-validation cost. Any other program/VAO bind clears the arm
    // (sfpewBackendBindVertexArray / sfpewInvalidateImmediateDrawState), so a
    // valid arm proves the backend still has this exact pair. The ARRAY
    // binding is NOT armed here: each branch below re-binds its own source.
    auto& gsc = g_glstate_c;
    if (gsc.immediate_live_program != prog_id || !gsc.deferred_draw.held) {
        g_glFuncs.glUseProgram(prog_id);
        sfpewBackendBindVertexArray(g_glstate_c.fpe_state.fpe_vao); // clears the arm
        gsc.immediate_live_program = prog_id; // re-arm after the binds
        gsc.immediate_live_buffer = 0;
    }

    // Client-memory arrays stream through the persistent-coherent immediate
    // ring (no per-draw buffer orphan). The upload must NEVER target the
    // caller's bound VBO, or their buffer contents would be destroyed.
    const bool client_memory_draw =
        reinterpret_cast<uintptr_t>(vpa.starting_pointer) > static_cast<uintptr_t>(vpa.stride);

    int ret = 0;

    if (client_memory_draw) {
        // 64-bit size math: GLsizei * GLsizei overflowed for large draws,
        // handing the upload a negative or wrapped size. The final row is
        // trimmed to its last attribute byte: reading a full stride past the
        // last vertex would over-read the client allocation when the row has
        // tail padding (interleaved layouts with window < stride).
        int64_t row_tail = 0;
        for (int i = 0; i < VERTEX_POINTER_COUNT; ++i) {
            if (!((vpa.enabled_pointers >> i) & 1u)) continue;
            const auto& attr = vpa.attributes[i];
            const int64_t tail = (int64_t)(uintptr_t)attr.pointer +
                                 (int64_t)attr.size * (int64_t)type_size(attr.type);
            row_tail = std::max(row_tail, tail);
        }
        if (row_tail <= 0 || row_tail > (int64_t)vpa.stride) row_tail = (int64_t)vpa.stride;
        const int64_t upload_size = (int64_t)(*count - 1) * (int64_t)vpa.stride + row_tail;
        const int64_t skip = (int64_t)*first * (int64_t)vpa.stride;
        if (*count <= 0 || upload_size <= 0 ||
            upload_size > (int64_t)std::numeric_limits<GLsizei>::max() || skip < 0) {
            g_glstate_c.set_error(GL_INVALID_VALUE);
            return -1;
        }
        const auto* draw_start = static_cast<const uint8_t*>(vpa.starting_pointer) + skip;
        auto& st = g_glstate_c.fpe_state;
        g_glFuncs.glBindBuffer(GL_ARRAY_BUFFER, st.fpe_immediate_vbo);
        const GLintptr ring_offset =
            sfpewUploadImmediateVertexData(draw_start, (size_t)upload_size);
        // Fresh read: the upload can replace the ring buffer object.
        gsc.immediate_live_buffer = st.fpe_immediate_vbo;
        g_glstate_c.send_vertex_attributes(vpa, st.fpe_immediate_vbo, ring_offset);
        *first = 0;
    } else {
        // Buffer-based arrays: attributes reference the caller's VBO (or
        // fpe_vbo when nothing is bound, matching the legacy layout). Bind
        // unconditionally: with the draw state held across draws, a previous
        // FPE draw may have left the ring bound over the app's own binding,
        // so "the app bound it already" is not a backend fact. A same-value
        // rebind is driver-deduplicated.
        const GLuint attribute_array_buffer = previous_array_buffer == 0
                                                  ? g_glstate_c.fpe_state.fpe_vbo
                                                  : static_cast<GLuint>(previous_array_buffer);
        // The arm records the backend ARRAY binding the wrapper last saw
        // established (its own binds and the app's direct passthrough both
        // update it; everything else clears it). A match means the backend
        // verifiably has this buffer bound - the MC chunk shape hits this
        // every draw, since the app itself just bound the chunk VBO.
        if (gsc.immediate_live_buffer != attribute_array_buffer) {
            g_glFuncs.glBindBuffer(GL_ARRAY_BUFFER, attribute_array_buffer);
            gsc.immediate_live_buffer = attribute_array_buffer;
        }
        g_glstate_c.send_vertex_attributes(vpa, attribute_array_buffer);
    }

    // plans/08 8.3: GL_LINE/GL_POINT polygon modes. drawImmediateVertices
    // applies the same two conversions to the glBegin/glEnd path using the
    // shared helpers below.
    const GLenum polygon_mode = sfpewUniformPolygonMode();
    const bool filled_primitive = sfpewIsFilledPrimitive(*mode);
    if (filled_primitive && polygon_mode == GL_POINT) {
        // Vertices repeat across shared corners; visually identical to spec.
        *mode = GL_POINTS;
        g_glstate_c.send_uniforms(prog);
        return 0;
    }
    if (filled_primitive && polygon_mode == GL_LINE) {
        thread_local std::vector<uint32_t> wire;
        const auto& edge_array = raw_vpa.attributes[vp2idx(GL_EDGE_FLAG_ARRAY)];
        const bool edge_array_live =
            ((raw_vpa.enabled_pointers >> vp2idx(GL_EDGE_FLAG_ARRAY)) & 1u) != 0 &&
            edge_array.pointer != nullptr &&
            getClientArrayBufferBinding(vp2idx(GL_EDGE_FLAG_ARRAY)) == 0;
        // Client-memory edge flags can be read where they lie; a VBO-backed
        // array would have to be mapped, which is not worth a stall on a
        // path this cold - those keep every edge, the pre-existing behavior.
        thread_local std::vector<uint8_t> client_flags;
        const uint8_t* flags = nullptr;
        size_t flag_count = 0;
        if (edge_array_live && *count > 0) {
            const size_t stride = edge_array.stride != 0 ? (size_t)edge_array.stride
                                                         : sizeof(GLboolean);
            const auto* bytes = static_cast<const uint8_t*>(edge_array.pointer);
            client_flags.resize((size_t)*count);
            // raw_first, not *first: the gather zeroes *first after folding
            // it into the vertex upload, but this array is read through the
            // raw client pointer where the caller's base still applies.
            for (size_t i = 0; i < (size_t)*count; ++i)
                client_flags[i] = bytes[((size_t)raw_first + i) * stride] != 0 ? 1u : 0u;
            flags = client_flags.data();
            flag_count = client_flags.size();
        }
        sfpewBuildWireframeIndices(*mode, (uint32_t)*first, (uint32_t)*count, wire, flags,
                                   flag_count);
        // Empty means every edge was suppressed - draw nothing rather than
        // falling through to the filled path (see drawImmediateVertices).
        if (wire.empty()) return -1;
        if (sfpewUploadWireframeIndices(wire)) {
            *mode = GL_LINES;
            *count = (GLsizei)wire.size();
            g_glstate_c.send_uniforms(prog);
            return 2; // wireframe: GL_UNSIGNED_INT indices at offset 0
        }
    }

    if (*mode == GL_QUADS) {
        const GLsizei index_count = (*count / 4) * 6;
        // A base-vertex draw lets display lists share one large immutable VBO
        // without regenerating and uploading quad indices for every list.
        // Retain baked indices as the compatibility fallback when the backend
        // doesn't expose the GLES 3.2/core entry point.
        const GLuint index_first =
            *first != 0 && g_glFuncs.glDrawElementsBaseVertex != nullptr
                ? 0u
                : static_cast<uint32_t>(*first);
        const bool upload_indices = prepare_quad_indices(*count, index_first);

        // LOG_D("glBufferData: size = %d, data = 0x%x -> GL_ELEMENT_ARRAY_BUFFER (%d)",
        //      g_glstate_c.fpe_state.fpe_ib.size() * sizeof(uint32_t), g_glstate_c.fpe_state.fpe_ib.data(),
        //      g_glstate_c.fpe_state.fpe_ibo)

        if (!g_glstate_c.fpe_state.fpe_ibo_bound) {
            sfpewBackendBindElementBuffer(g_glstate_c.fpe_state.fpe_ibo);
            g_glstate_c.fpe_state.fpe_ibo_bound = true;
        }

        if (upload_indices) {
            g_glFuncs.glBufferData(GL_ELEMENT_ARRAY_BUFFER, quad_index_size_bytes(), quad_index_data(),
                                   GL_DYNAMIC_DRAW);
        }

        *count = index_count;

        *mode = GL_TRIANGLES;
        ret = 1;
    } else {
        // The other two legacy modes passed through raw and died with
        // GL_INVALID_ENUM on the backend. Only glDrawArrays callers reached
        // here with them (glDrawElements converts in drawElementsNow), which
        // is why it went unnoticed - Minecraft draws quads, not quad strips.
        // Found by the piglit degenerate-prims port.
        sfpewConvertLegacyDrawMode(mode, count);
    }

    g_glstate_c.send_uniforms(prog);
    //    vpa.starting_pointer = 0;
    //    vpa.stride = 0;
    return ret;
}

// plans/09 S9 mixed pipeline: GL 2.1 semantics feed the fixed-function
// vertex arrays into whatever program is bound. Uses a LOCAL
// normalization and the dedicated fpe_user_vao so the FPE path's
// normalized_vpa / attribute caches (which describe fpe_vao and
// FPE-generated attribute slots) stay untouched.
bool sfpewUserProgramFixedFunctionDrawArrays(GLuint program, GLenum mode, GLint first,
                                             GLsizei count) {
    if (count <= 0 || first < 0) return false;
    GLint locations[VERTEX_POINTER_COUNT];
    if (!sfpewUserProgramAttribLocations(program, locations)) return false;
    if (!g_glstate.fpe_ready && init_fpe() != 0) return false;

    auto& st = g_glstate.fpe_state;
    if (st.fpe_user_vao == 0) {
        if (g_glFuncs.glGenVertexArrays == nullptr) return false;
        g_glFuncs.glGenVertexArrays(1, &st.fpe_user_vao);
        st.fpe_user_vao_enabled = 0;
        if (st.fpe_user_vao == 0) return false;
    }

    const GLint logical_array_buffer = (GLint)sfpewLogicalArrayBufferBinding();
    fpe_backend_draw_state_guard_t backend_state((GLint)program, logical_array_buffer);

    const auto& raw_vpa = st.vertexpointer_array;
    const bool mixed_polygon_mode = sfpewMixedPolygonMode(mode);
    thread_local std::vector<glm::vec4> mixed_positions;
    thread_local std::vector<uint8_t> mixed_edge_flags;
    if (mixed_polygon_mode) {
        const int position_slot = vp2idx(GL_VERTEX_ARRAY);
        if (!sfpewReadVertexPositions(raw_vpa.attributes[position_slot],
                                      getClientArrayBufferBinding(position_slot), first, count,
                                      mixed_positions)) {
            g_glstate.set_error(GL_INVALID_OPERATION);
            return true;
        }
        mixed_edge_flags.clear();
        const int edge_slot = vp2idx(GL_EDGE_FLAG_ARRAY);
        if (((raw_vpa.enabled_pointers >> edge_slot) & 1u) != 0) {
            (void)sfpewReadEdgeFlags(raw_vpa.attributes[edge_slot],
                                     getClientArrayBufferBinding(edge_slot), first, count,
                                     mixed_edge_flags);
        }
    }
    vertex_pointer_array_t vpa;
    if (gather_client_arrays(raw_vpa, first, count, &vpa)) {
        first = 0; // the gather already applied the base offset
    } else {
        // normalize() is non-const only for legacy reasons; the copy keeps
        // the shared raw state and the FPE path's caches untouched.
        vertex_pointer_array_t raw_copy = raw_vpa;
        vpa = raw_copy.normalize();
    }

    sfpewBackendBindVertexArray(st.fpe_user_vao);

    const bool client_memory_draw =
        reinterpret_cast<uintptr_t>(vpa.starting_pointer) > static_cast<uintptr_t>(vpa.stride);
    const GLuint attribute_buffer = (logical_array_buffer == 0 || client_memory_draw)
                                        ? st.fpe_vbo
                                        : (GLuint)logical_array_buffer;
    sfpewBackendBindAttributeBuffer(attribute_buffer, backend_state.holds_save);
    if (client_memory_draw) {
        const int64_t upload_size = (int64_t)count * (int64_t)vpa.stride;
        const int64_t skip = (int64_t)first * (int64_t)vpa.stride;
        if (upload_size <= 0 || upload_size > (int64_t)std::numeric_limits<GLsizei>::max() ||
            skip < 0) {
            g_glstate.set_error(GL_INVALID_VALUE);
            return true; // handled: the draw is dropped, not passed through
        }
        const auto* draw_start = static_cast<const uint8_t*>(vpa.starting_pointer) + skip;
        g_glFuncs.glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)upload_size, draw_start,
                               GL_DYNAMIC_DRAW);
        first = 0;
    }

    sfpewSendUserProgramAttributes(locations, vpa, 0);
    sfpewFeedUserProgramUniforms(program);

    if (mixed_polygon_mode &&
        sfpewDrawMixedPolygonMode(mode, mixed_positions.data(), mixed_positions.size(), nullptr,
                                  static_cast<size_t>(count), static_cast<uint32_t>(first),
                                  mixed_edge_flags.empty() ? nullptr : mixed_edge_flags.data(),
                                  mixed_edge_flags.size())) {
        return true;
    }

    if (mode == GL_QUADS) {
        const GLsizei index_count = (count / 4) * 6;
        const GLuint index_first =
            first != 0 && g_glFuncs.glDrawElementsBaseVertex != nullptr ? 0u
                                                                        : (uint32_t)first;
        const bool upload_indices = prepare_quad_indices(count, index_first);
        // fpe_ibo_bound tracks fpe_vao's element binding; this VAO has its
        // own, so bind unconditionally.
        g_glFuncs.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, st.fpe_ibo);
        if (upload_indices) {
            g_glFuncs.glBufferData(GL_ELEMENT_ARRAY_BUFFER, quad_index_size_bytes(),
                                   quad_index_data(), GL_DYNAMIC_DRAW);
        }
        if (first != 0 && g_glFuncs.glDrawElementsBaseVertex != nullptr) {
            g_glFuncs.glDrawElementsBaseVertex(GL_TRIANGLES, index_count, quad_index_type(),
                                               (void*)0, first);
        } else {
            g_glFuncs.glDrawElements(GL_TRIANGLES, index_count, quad_index_type(), (void*)0);
        }
        return true;
    }

    GLenum draw_mode = mode;
    if (mode == GL_QUAD_STRIP) draw_mode = GL_TRIANGLE_STRIP;
    else if (mode == GL_POLYGON) draw_mode = GL_TRIANGLE_FAN;
    g_glFuncs.glDrawArrays(draw_mode, first, count);
    return true;
}
