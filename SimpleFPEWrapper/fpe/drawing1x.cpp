// SimpleFPEWrapper - SimpleFPEWrapper/fpe/drawing1x.cpp
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

#include <glm/gtc/type_ptr.hpp>
#include "drawing1x.h"
#include "list.h"
#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstring>

#define DEBUG 0

namespace {

constexpr size_t kImmediateVboMinCapacity = 16u * 1024u * 1024u;
constexpr size_t kImmediateVboAlignment = 256u;
// Index data is a fraction of vertex data, so the element ring is sized down.
constexpr size_t kElementRingMinCapacity = 4u * 1024u * 1024u;
// Merge window for tiny glBegin/glEnd runs. Runs longer than the vertex limit
// already amortize their per-draw fixed cost, so they draw directly. The run
// and total-vertex caps bound both the copy work and how long a draw can sit
// pending before some entry point flushes it.
constexpr size_t kImmediateMergeVertexLimit = 64u;
constexpr size_t kImmediateMergeRunLimit = 256u;
constexpr size_t kImmediateMergeVertexBudget = 8192u;

} // namespace

// One persistent-coherent streaming ring. The vertex and index rings differ
// only in their target and in what a buffer replacement has to invalidate, so
// the ring mechanics (capacity growth, segment fences, wrap) live here once.
struct stream_ring_t {
    GLenum target;
    GLuint& buffer;
    void* (&fences)[4];
    size_t& capacity;
    size_t& offset;
    void*& map;
    bool& persistent_attempted;
    size_t min_capacity;
    // Run after the ring's buffer object is replaced: cached attribute or
    // element bindings that referred to the old name are no longer valid.
    void (*on_replaced)();
};

static GLintptr sfpewUploadToRing(const stream_ring_t& ring, const void* data, size_t size);

GLintptr sfpewUploadImmediateVertexData(const void* data, size_t size) {
    auto& state = g_glstate_c.fpe_state;
    const stream_ring_t ring{GL_ARRAY_BUFFER,
                             state.fpe_immediate_vbo,
                             state.fpe_immediate_fences,
                             state.fpe_immediate_vbo_capacity,
                             state.fpe_immediate_vbo_offset,
                             state.fpe_immediate_vbo_map,
                             state.fpe_immediate_vbo_persistent_attempted,
                             kImmediateVboMinCapacity,
                             []() {
                                 auto& gs = g_glstate_c;
                                 gs.fpe_vertex_binding_valid = false;
                                 for (auto& cached : gs.fpe_vertex_attributes)
                                     cached.pointer_valid = false;
                             }};
    return sfpewUploadToRing(ring, data, size);
}

GLintptr sfpewUploadElementData(const void* data, size_t size) {
    auto& state = g_glstate_c.fpe_state;
    const stream_ring_t ring{GL_ELEMENT_ARRAY_BUFFER,
                             state.fpe_element_ring,
                             state.fpe_element_ring_fences,
                             state.fpe_element_ring_capacity,
                             state.fpe_element_ring_offset,
                             state.fpe_element_ring_map,
                             state.fpe_element_ring_persistent_attempted,
                             kElementRingMinCapacity,
                             []() { g_glstate_c.fpe_state.fpe_ibo_bound = false; }};
    return sfpewUploadToRing(ring, data, size);
}

static GLintptr sfpewUploadToRing(const stream_ring_t& ring, const void* data, size_t size) {
    auto& state = g_glstate_c.fpe_state;
    (void)state;
    const auto dropAllFences = [&]() {
        for (auto& fence : ring.fences) {
            if (fence != nullptr && g_glFuncs.glDeleteSync != nullptr)
                g_glFuncs.glDeleteSync((GLsync)fence);
            fence = nullptr;
        }
    };
    const auto replaceImmediateBuffer = [&]() {
        dropAllFences();
        if (ring.map != nullptr && g_glFuncs.glUnmapBuffer != nullptr) {
            g_glFuncs.glUnmapBuffer(ring.target);
        }
        sfpewForgetInternalBuffer(ring.buffer);
        g_glFuncs.glDeleteBuffers(1, &ring.buffer);
        g_glFuncs.glGenBuffers(1, &ring.buffer);
        sfpewNoteInternalBuffer(ring.buffer);
        g_glFuncs.glBindBuffer(ring.target, ring.buffer);
        ring.capacity = 0;
        ring.offset = 0;
        ring.map = nullptr;
        if (ring.on_replaced != nullptr) ring.on_replaced();
    };

    const size_t required_capacity = std::max(ring.min_capacity, std::bit_ceil(size));
    if (ring.map != nullptr && required_capacity > ring.capacity) {
        // Oversized immediate draws are rare, but an immutable persistent
        // store cannot grow in place. Finish users of the old store, replace
        // it, and retry with a power-of-two capacity large enough for this draw.
        if (g_glFuncs.glFinish != nullptr) g_glFuncs.glFinish();
        replaceImmediateBuffer();
        ring.persistent_attempted = false;
        return sfpewUploadToRing(ring, data, size);
    }
    if (!ring.persistent_attempted) {
        ring.persistent_attempted = true;
        auto storage = g_glFuncs.glBufferStorage != nullptr ? g_glFuncs.glBufferStorage
                                                           : g_glFuncs.glBufferStorageEXT;
        if (storage != nullptr && g_glFuncs.glMapBufferRange != nullptr) {
            constexpr GLbitfield map_flags = GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;
            constexpr GLbitfield storage_flags = map_flags | GL_DYNAMIC_STORAGE_BIT;
            storage(ring.target, static_cast<GLsizeiptr>(required_capacity), nullptr, storage_flags);
            ring.map = g_glFuncs.glMapBufferRange(ring.target, 0,
                                                  static_cast<GLsizeiptr>(required_capacity), map_flags);
            if (ring.map != nullptr) {
                ring.capacity = required_capacity;
                ring.offset = 0;
            } else {
                // Immutable storage can't be respecified with BufferData. Replace
                // the failed persistent candidate with a fresh mutable fallback.
                replaceImmediateBuffer();
            }
        }
    }

    if (ring.map == nullptr) {
        // The backend lacks coherent persistent storage. Keep the previous
        // orphaning behaviour, which is faster than synchronous SubData on
        // mobile drivers and preserves compatibility with other backends.
        g_glFuncs.glBufferData(ring.target, static_cast<GLsizeiptr>(size), data, GL_STREAM_DRAW);
        return 0;
    }

    size_t offset = (ring.offset + kImmediateVboAlignment - 1u) & ~(kImmediateVboAlignment - 1u);
    if (offset + size > ring.capacity) offset = 0;

    // Segmented synchronization: by the time an upload crosses into a new
    // quarter of the ring, every draw consuming the PREVIOUS quarter has
    // already been submitted (uploads and draws strictly alternate), so a
    // fence on the old quarter is a correct completion marker. Entering a
    // quarter waits on (and retires) its previous-lap fence. Without sync
    // objects this degrades to the old glFinish at wrap only.
    const bool have_sync = g_glFuncs.glFenceSync != nullptr &&
                           g_glFuncs.glClientWaitSync != nullptr && g_glFuncs.glDeleteSync != nullptr;
    const size_t segment_size = ring.capacity / 4u;
    if (have_sync && segment_size != 0) {
        // ring.offset is one-past-the-end of the previous upload: derive the
        // previous segment from its LAST byte, or an upload ending exactly on a
        // quarter boundary would make the next upload's segment compare equal
        // and skip the fence entirely.
        const size_t previous_segment =
            std::min<size_t>(ring.offset == 0 ? 0 : (ring.offset - 1u) / segment_size, 3u);
        const size_t first_segment = offset / segment_size;
        const size_t last_segment = std::min<size_t>((offset + size - 1u) / segment_size, 3u);
        if (first_segment != previous_segment || offset == 0) {
            auto& old_fence = ring.fences[previous_segment];
            if (old_fence == nullptr)
                old_fence = (void*)g_glFuncs.glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
            for (size_t seg = first_segment; seg <= last_segment; ++seg) {
                auto& fence = ring.fences[seg];
                if (fence == nullptr) continue;
                g_glFuncs.glClientWaitSync((GLsync)fence, GL_SYNC_FLUSH_COMMANDS_BIT,
                                           1000000000ull /* 1s */);
                g_glFuncs.glDeleteSync((GLsync)fence);
                fence = nullptr;
            }
        }
    } else if (offset == 0 && ring.offset != 0) {
        if (g_glFuncs.glFinish != nullptr) g_glFuncs.glFinish();
    }

    if (size > 0) {
        std::memcpy(static_cast<uint8_t*>(ring.map) + offset, data, size);
    }
    ring.offset = offset + size;
    return static_cast<GLintptr>(offset);
}

namespace {

struct immediate_client_state_guard_t {
    vertex_pointer_array_t vertexPointerArray = g_glstate_c.fpe_state.vertexpointer_array;
    vertex_pointer_array_t normalizedVertexPointerArray = g_glstate_c.fpe_state.normalized_vpa;

    immediate_client_state_guard_t() = default;

    ~immediate_client_state_guard_t() {
        g_glstate_c.fpe_state.vertexpointer_array = vertexPointerArray;
        g_glstate_c.fpe_state.normalized_vpa = normalizedVertexPointerArray;
    }

    immediate_client_state_guard_t(const immediate_client_state_guard_t&) = delete;
    immediate_client_state_guard_t& operator=(const immediate_client_state_guard_t&) = delete;
};

struct immediate_draw_sizes_guard_t {
    fixed_function_draw_size_t sizes = g_glstate_c.fpe_state.fpe_draw.current_data.sizes;

    ~immediate_draw_sizes_guard_t() { g_glstate_c.fpe_state.fpe_draw.current_data.sizes = sizes; }

    immediate_draw_sizes_guard_t() = default;
    immediate_draw_sizes_guard_t(const immediate_draw_sizes_guard_t&) = delete;
    immediate_draw_sizes_guard_t& operator=(const immediate_draw_sizes_guard_t&) = delete;
};

struct pending_immediate_batch_t {
    // Batches are keyed to the context they were collected on: replaying
    // vertex data or texture bindings on a different context would draw
    // garbage (plans/07).
    EGLContext context = EGL_NO_CONTEXT;
    bool active = false;
    // What the merged stream draws as. Runs only merge into a batch whose
    // target primitive matches, because a single draw call has one mode.
    GLenum primitive = GL_TRIANGLES;
    // A lone pending run is held in its ORIGINAL form: rewriting strips/fans/
    // quads into independent primitives inflates the stream (a quad grows 4
    // vertices to 6), which is a loss if no second run ever joins. The
    // expansion is paid only when one does.
    bool unexpanded = false;
    GLenum unexpandedPrimitive = GL_TRIANGLES;
    fixed_function_draw_size_t sizes{};
    std::vector<GLfloat> vertices;
    size_t vertexCount = 0;
    size_t runCount = 0;
    GLenum activeTexture = GL_TEXTURE0;
    GLuint texture2D = 0;
};

thread_local pending_immediate_batch_t pendingGlyphBatch;

// `sizes` describes the interleaved STREAM layout. `constant_sizes`, when
// non-null, additionally declares slots whose data is NOT in the stream but
// must still reach the shader as constant attributes (glVertexAttrib4fv from
// the live current values) - compiled display-list runs use this to inherit
// the caller's sticky current color/normal/texcoord exactly like a live
// replay would. The live glEnd path passes nullptr (stream == constants).
// static_source, when non-zero, is a buffer that ALREADY holds this run's
// vertex data at offset 0. Compiled display-list runs pass it: their data is
// immutable after glEndList, so re-streaming it through the ring on every
// glCallList was pure waste (measured: 59% of bench.mcentity sat in the driver,
// and the run's data was re-uploaded once per replay).
void drawImmediateVertices(GLenum primitive, const GLfloat* vertices, size_t floatCount,
                           size_t vertexCount, const fixed_function_draw_size_t& sizes,
                           const fixed_function_draw_size_t* constant_sizes = nullptr,
                           GLuint static_source = 0) {
    if (vertices == nullptr || floatCount == 0 || vertexCount == 0 ||
        vertexCount > static_cast<size_t>(std::numeric_limits<GLsizei>::max())) {
        return;
    }

    auto& gs = g_glstate_c;
    if (gs.render_mode != GL_RENDER) {
        // Selection/feedback: the interleaved buffer starts with the
        // position attribute; stride is the whole per-vertex float count.
        size_t stride_floats = 0;
        for (GLint component_count : sizes.data)
            if (component_count > 0) stride_floats += (size_t)component_count;
        if (stride_floats > 0)
            sfpewSelectionProcessVertices(primitive, vertices, stride_floats, sizes.vertex_size,
                                          vertexCount);
        return;
    }

    // Compiled display-list replay reaches this without ever passing
    // through glBegin (which used to be the only init_fpe caller on the
    // immediate path): a fresh context calling glCallList first would
    // otherwise draw with fpe_vao == 0 and no ring buffer.
    if (!gs.fpe_ready && init_fpe() != 0) return;

    fpe_backend_draw_state_guard_t backendState(
        sfpewLogicalProgram(), static_cast<GLint>(sfpewLogicalArrayBufferBinding()));
    // glBegin/glEnd uses temporary interleaved data. Preserve the caller's
    // client-array declarations so an immediate draw cannot invalidate the
    // following glDrawArrays call.
    immediate_client_state_guard_t clientState;
    immediate_draw_sizes_guard_t drawSizes;

    auto& state = gs.fpe_state;
    const fixed_function_draw_size_t& shader_sizes = constant_sizes ? *constant_sizes : sizes;
    state.fpe_draw.current_data.sizes = shader_sizes;

    fixed_function_draw_state_t layoutState;
    layoutState.current_data.sizes = sizes;
    layoutState.compile_vertexattrib(state.vertexpointer_array);

    auto& va = state.normalized_vpa;
    va = state.vertexpointer_array.normalize();
    // generate_compressed_index only reads the size array, but its legacy
    // declaration is not const-correct. It must see the shader's view
    // (stream + constant slots) so attribute indices line up.
    auto mutableSizes = shader_sizes;
    va.generate_compressed_index(mutableSizes.data);

    // GL 2.1: a bound USER program consumes immediate-mode vertices too
    // (OptiFine composite/final passes draw their fullscreen quad through
    // glBegin/glEnd with the pack's shader bound - substituting the FPE
    // program here silently blitted instead of compositing).
    const GLint user_program = sfpewLogicalProgram();
    GLint user_locations[VERTEX_POINTER_COUNT];
    if (user_program > 0 &&
        sfpewUserProgramAttribLocations((GLuint)user_program, user_locations)) {
        if (state.fpe_user_vao == 0 && g_glFuncs.glGenVertexArrays != nullptr) {
            g_glFuncs.glGenVertexArrays(1, &state.fpe_user_vao);
            state.fpe_user_vao_enabled = 0;
        }
        if (state.fpe_user_vao != 0) {
            sfpewBackendBindVertexArray(state.fpe_user_vao);
            const GLuint user_source =
                static_source != 0 ? static_source : state.fpe_immediate_vbo;
            g_glFuncs.glBindBuffer(GL_ARRAY_BUFFER, user_source);
            const GLintptr userVertexOffset =
                static_source != 0
                    ? 0
                    : sfpewUploadImmediateVertexData(vertices, floatCount * sizeof(GLfloat));
            sfpewSendUserProgramAttributes(user_locations, va, userVertexOffset);
            sfpewFeedUserProgramUniforms((GLuint)user_program);

            const GLsizei userDrawCount = static_cast<GLsizei>(vertexCount);
            if (primitive == GL_QUADS) {
                const GLsizei indexCount = (userDrawCount / 4) * 6;
                const bool uploadIndices = prepare_quad_indices(userDrawCount, 0);
                g_glFuncs.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, state.fpe_ibo);
                if (uploadIndices) {
                    g_glFuncs.glBufferData(GL_ELEMENT_ARRAY_BUFFER, quad_index_size_bytes(),
                                           quad_index_data(), GL_DYNAMIC_DRAW);
                }
                g_glFuncs.glDrawElements(GL_TRIANGLES, indexCount, quad_index_type(), (void*)0);
            } else {
                GLenum draw_mode = primitive;
                if (primitive == GL_QUAD_STRIP) draw_mode = GL_TRIANGLE_STRIP;
                else if (primitive == GL_POLYGON) draw_mode = GL_TRIANGLE_FAN;
                g_glFuncs.glDrawArrays(draw_mode, 0, userDrawCount);
            }
            return;
        }
    }

    auto key = gs.program_hash();
    auto& program = gs.get_or_generate_program(key);
    const int programId = program.get_program();
    if (programId <= 0) {
        gs.set_error(GL_INVALID_OPERATION);
        return;
    }

    // Resolve the attribute source first, so the arm below binds it directly
    // instead of binding the ring and then replacing it.
    const GLuint source_buffer = static_source != 0 ? static_source : state.fpe_immediate_vbo;

    // A preceding immediate draw may have left this state bound, in which
    // case re-issuing it is pure cost - half the per-batch driver traffic
    // (plans/12). Only valid while the app's state is still held; the
    // barrier clears the flag when it hands the state back. The program and
    // buffer arms are separate: consecutive resident display-list replays
    // (MC's entity model, one buffer per box) share the program and VAO but
    // switch buffers, and re-issuing glUseProgram + glBindVertexArray for a
    // buffer change is driver-validation cost for nothing.
    if (gs.immediate_live_program != programId || !gs.deferred_draw.held) {
        g_glFuncs.glUseProgram(programId);
        sfpewBackendBindVertexArray(state.fpe_vao); // clears both arms
        gs.immediate_live_program = programId;      // re-arm after the binds
        gs.immediate_live_buffer = 0;
    }
    if (gs.immediate_live_buffer != source_buffer) {
        g_glFuncs.glBindBuffer(GL_ARRAY_BUFFER, source_buffer);
        gs.immediate_live_buffer = source_buffer;
    }

    // A resident static source needs no copy; the ring path streams as before.
    const GLintptr vertexOffset =
        static_source != 0 ? 0
                           : sfpewUploadImmediateVertexData(vertices,
                                                            floatCount * sizeof(GLfloat));
    gs.send_vertex_attributes(va, source_buffer, vertexOffset);
    gs.send_uniforms(program);

    const GLsizei drawCount = static_cast<GLsizei>(vertexCount);
    if (primitive == GL_QUADS) {
        const GLsizei indexCount = (drawCount / 4) * 6;
        const bool uploadIndices = prepare_quad_indices(drawCount, 0);
        if (!state.fpe_ibo_bound) {
            sfpewBackendBindElementBuffer(state.fpe_ibo);
            state.fpe_ibo_bound = true;
        }
        if (uploadIndices) {
            g_glFuncs.glBufferData(GL_ELEMENT_ARRAY_BUFFER, quad_index_size_bytes(), quad_index_data(),
                                   GL_DYNAMIC_DRAW);
        }
        g_glFuncs.glDrawElements(GL_TRIANGLES, indexCount, quad_index_type(), (void*)0);
    } else {
        g_glFuncs.glDrawArrays(primitive, 0, drawCount);
    }
}

// What primitive a small glBegin/glEnd run can be rewritten into so that
// consecutive runs concatenate into one draw. GL_NONE means "not mergeable".
GLenum mergeTargetPrimitive(GLenum primitive, size_t vertexCount) {
    switch (primitive) {
    case GL_TRIANGLES:
        return vertexCount >= 3 && vertexCount % 3u == 0 ? GL_TRIANGLES : GL_NONE;
    case GL_TRIANGLE_STRIP:
    case GL_TRIANGLE_FAN:
    case GL_POLYGON:
        return vertexCount >= 3 ? GL_TRIANGLES : GL_NONE;
    case GL_QUADS:
        return vertexCount >= 4 && vertexCount % 4u == 0 ? GL_TRIANGLES : GL_NONE;
    case GL_QUAD_STRIP:
        return vertexCount >= 4 && vertexCount % 2u == 0 ? GL_TRIANGLES : GL_NONE;
    case GL_LINES:
        return vertexCount >= 2 && vertexCount % 2u == 0 ? GL_LINES : GL_NONE;
    case GL_LINE_STRIP:
    case GL_LINE_LOOP:
        return vertexCount >= 2 ? GL_LINES : GL_NONE;
    case GL_POINTS:
        return GL_POINTS;
    default:
        return GL_NONE;
    }
}

// Appends `draw`'s vertices to `out`, rewritten into independent primitives of
// the merge target. Strips/fans/loops lose their implicit connectivity here,
// which is exactly what makes concatenation legal.
void appendMergedRun(GLenum primitive, const GLfloat* src, size_t count, size_t stride,
                     std::vector<GLfloat>& out, size_t* appendedVertices) {
    size_t appended = 0;
    const auto emit = [&](size_t index) {
        const size_t offset = out.size();
        out.resize(offset + stride);
        std::memcpy(out.data() + offset, src + index * stride, stride * sizeof(GLfloat));
        ++appended;
    };
    const auto emitTriangle = [&](size_t a, size_t b, size_t c) {
        emit(a);
        emit(b);
        emit(c);
    };

    switch (primitive) {
    case GL_POINTS:
    case GL_LINES:
    case GL_TRIANGLES:
        // Already independent: one contiguous copy.
        {
            const size_t offset = out.size();
            out.resize(offset + count * stride);
            std::memcpy(out.data() + offset, src, count * stride * sizeof(GLfloat));
            appended = count;
        }
        break;
    case GL_LINE_STRIP:
        for (size_t i = 0; i + 1 < count; ++i) {
            emit(i);
            emit(i + 1);
        }
        break;
    case GL_LINE_LOOP:
        for (size_t i = 0; i + 1 < count; ++i) {
            emit(i);
            emit(i + 1);
        }
        if (count > 2) {
            emit(count - 1);
            emit(0);
        }
        break;
    case GL_TRIANGLE_STRIP:
        // Odd triangles swap their first two vertices to keep the winding.
        for (size_t i = 0; i + 2 < count; ++i) {
            if ((i & 1u) == 0u) emitTriangle(i, i + 1, i + 2);
            else emitTriangle(i + 1, i, i + 2);
        }
        break;
    case GL_TRIANGLE_FAN:
    case GL_POLYGON:
        for (size_t i = 1; i + 1 < count; ++i) emitTriangle(0, i, i + 1);
        break;
    case GL_QUADS:
        // Matches the index order the non-merged quad path uses.
        for (size_t q = 0; q + 3 < count; q += 4) {
            emitTriangle(q, q + 1, q + 2);
            emitTriangle(q + 2, q + 3, q);
        }
        break;
    case GL_QUAD_STRIP:
        // Spec vertex order: 2n, 2n+1, 2n+3, 2n+2 forms each quad.
        for (size_t i = 0; i + 3 < count; i += 2) {
            emitTriangle(i, i + 1, i + 3);
            emitTriangle(i + 3, i + 2, i);
        }
        break;
    default:
        break;
    }
    *appendedVertices = appended;
}

// Merges a small glBegin/glEnd run into the pending batch so a burst of tiny
// primitives costs one program hash, one attribute layout, one uniform push
// and one draw call instead of one of each per run (plans/12). Any entry point
// outside the vertex family flushes the batch first (sfpewEntryBarrier), so a
// merged run can never observe state that changed after it was collected.
bool queueImmediateRun(const fixed_function_draw_state_t& draw) {
    if (DisplayListManager::isCalling() || draw.vb.empty() || draw.vertex_count == 0 ||
        draw.vb.size() % draw.vertex_count != 0) {
        return false;
    }
    // Selection/feedback transforms on the CPU and must stay in draw order.
    if (g_glstate_c.render_mode != GL_RENDER) return false;
    // Large runs already amortize their own fixed cost, and rewriting them
    // would copy (and for quads inflate) a stream the GPU could have taken
    // as-is. Only the small-batch case is worth merging.
    if (draw.vertex_count > kImmediateMergeVertexLimit) return false;

    const GLenum target = mergeTargetPrimitive(draw.primitive, draw.vertex_count);
    if (target == GL_NONE) return false;

    auto& batch = pendingGlyphBatch;
    const auto& sizes = draw.current_data.sizes;
    const GLenum activeTexture = sfpewLogicalActiveTexture();
    const GLuint texture2D = sfpewLogicalTextureBinding(GL_TEXTURE_2D);
    if (batch.active &&
        (batch.primitive != target || std::memcmp(&batch.sizes, &sizes, sizeof(sizes)) != 0 ||
         batch.activeTexture != activeTexture || batch.texture2D != texture2D)) {
        flushPendingImmediateDraws();
    }
    const size_t stride = draw.vb.size() / draw.vertex_count;
    if (!batch.active) {
        // First run: keep it verbatim. If the next entry point is a state
        // change (the common case outside a tight draw loop) this batch flushes
        // as the original primitive and costs one stream copy over drawing
        // directly - no expansion, no inflated upload.
        batch.active = true;
        // Called from glEnd, whose entry resolve refreshed the snapshot.
        batch.context = (EGLContext)glstate_t::cached_context();
        batch.primitive = target;
        batch.unexpanded = true;
        batch.unexpandedPrimitive = draw.primitive;
        batch.sizes = sizes;
        batch.vertices.assign(draw.vb.begin(), draw.vb.end());
        batch.vertexCount = draw.vertex_count;
        batch.runCount = 1;
        batch.activeTexture = activeTexture;
        batch.texture2D = texture2D;
        return true;
    }

    if (batch.unexpanded) {
        // A second run joined, so the held one has to become independent
        // primitives before this one can be concatenated onto it.
        thread_local std::vector<GLfloat> expandScratch;
        expandScratch.swap(batch.vertices);
        batch.vertices.clear();
        batch.vertices.reserve(kImmediateMergeRunLimit * stride * 6u);
        size_t expanded = 0;
        appendMergedRun(batch.unexpandedPrimitive, expandScratch.data(), batch.vertexCount, stride,
                        batch.vertices, &expanded);
        batch.vertexCount = expanded;
        batch.unexpanded = false;
    }

    size_t appended = 0;
    appendMergedRun(draw.primitive, draw.vb.data(), draw.vertex_count, stride, batch.vertices,
                    &appended);
    if (appended == 0) {
        // Nothing to draw (degenerate run); the batch stays as it was.
        if (batch.vertexCount == 0) batch.active = false;
        return true;
    }
    batch.vertexCount += appended;
    ++batch.runCount;

    if (batch.runCount >= kImmediateMergeRunLimit ||
        batch.vertexCount >= kImmediateMergeVertexBudget) {
        flushPendingImmediateDraws();
    }
    return true;
}

} // namespace

void sfpewFlushDeferredDrawState() {
    // Reads the snapshot: callers have already done their entry's strict
    // resolve (docs/context-model.md).
    auto& held = g_glstate_c.deferred_draw;
    if (!held.held) return;
    held.held = false;

    if (g_glFuncs.glUseProgram == nullptr || g_glFuncs.glBindVertexArray == nullptr ||
        g_glFuncs.glBindBuffer == nullptr) {
        return;
    }
    sfpewInvalidateImmediateDrawState();
    g_glFuncs.glUseProgram(held.program);
    sfpewBackendBindVertexArray(static_cast<GLuint>(held.vertex_array));
    // A non-zero restored VAO already carries its element binding; only VAO 0's
    // must be re-bound explicitly.
    //
    // This bind is measurably redundant - the wrapper binds its own element
    // buffer while its OWN VAO is current, and an element binding is VAO state,
    // so VAO 0's binding is never disturbed (100% of restores: 20000/20000 in
    // bench.mcgui, 140000/140000 in bench.mcentity). Eliding it against the
    // shadow was tried and measured SLOWER on 6 of 12 phases and faster on
    // none: the driver already fast-paths a bind that does not change the
    // binding, so the shadow read and branch cost more than the call they
    // skip. Left unconditional deliberately; see plans/12.
    if (held.element_array_buffer >= 0)
        sfpewBackendBindElementBuffer(static_cast<GLuint>(held.element_array_buffer));
    g_glFuncs.glBindBuffer(GL_ARRAY_BUFFER, held.array_buffer);
}

// Test-only view of whether immediate geometry is still buffered. A pbuffer
// cannot show the frame-ordering bug in pixels (there is no real swap), so the
// regression test asserts on this instead.
SFPEW_APIENTRY bool sfpewImmediateBatchPendingForTest() { return pendingGlyphBatch.active; }

void flushPendingImmediateDraws() {
    auto& batch = pendingGlyphBatch;
    if (!batch.active) return;
    // Deliberately strict: flush is reached from passthrough entries that
    // never resolve the context themselves, and drawing a batch on the
    // wrong context must stay impossible. Amortized over <=256 glyphs.
    (void)g_glstate;
    const EGLContext current = (EGLContext)glstate_t::cached_context();
    if (batch.context != current) {
        // The collecting context is gone from this thread; its texture
        // bindings and buffer names are meaningless here. Drop, not draw.
        SFPEW_LOGW("dropping %zu pending immediate runs collected on a different context",
                   batch.runCount);
        batch = {};
        batch.context = current;
        return;
    }

    const GLenum callerActiveTexture = sfpewLogicalActiveTexture();
    if (callerActiveTexture != batch.activeTexture)
        g_glFuncs.glActiveTexture(batch.activeTexture);

    const GLuint callerTexture2D = sfpewLogicalTextureBinding(GL_TEXTURE_2D);
    if (callerTexture2D != batch.texture2D)
        g_glFuncs.glBindTexture(GL_TEXTURE_2D, batch.texture2D);

    // Clear before drawing: drawImmediateVertices reaches entry points that
    // call back into the barrier, and a batch still marked active would be
    // drawn twice.
    const GLenum primitive = batch.unexpanded ? batch.unexpandedPrimitive : batch.primitive;
    const size_t vertexCount = batch.vertexCount;
    batch.active = false;
    batch.vertexCount = 0;
    batch.runCount = 0;
    drawImmediateVertices(primitive, batch.vertices.data(), batch.vertices.size(), vertexCount,
                          batch.sizes);

    if (callerTexture2D != batch.texture2D)
        g_glFuncs.glBindTexture(GL_TEXTURE_2D, callerTexture2D);
    if (callerActiveTexture != batch.activeTexture)
        g_glFuncs.glActiveTexture(callerActiveTexture);
    batch.vertices.clear();
}

namespace {

// Retired vertex buffers from destroyed compiled runs, with the context that
// owned each. Destructors must not call GL (see dropResident), so names land
// here and are reused by the next residentBuffer() call, which runs under a
// real GL entry point with a live context.
struct retired_run_buffer_t {
    GLuint name;
    const void* context;
};
std::vector<retired_run_buffer_t>& retiredRunBuffers() {
    static std::vector<retired_run_buffer_t> retired;
    return retired;
}
void retireImmediateRunBuffer(GLuint name, const void* context) {
    if (name == 0) return;
    auto& retired = retiredRunBuffers();
    // A pathological app could destroy lists without ever replaying one again,
    // which would grow this without bound. Cap it: past the cap the name is
    // simply forgotten, and it dies with its context like any other GL object.
    if (retired.size() >= 4096u) return;
    retired.push_back({name, context});
}
// Hands back a buffer name owned by this context, dropping any that belonged
// to a different one (those died with their context; forgetting them is
// correct - their names were never handed to the app, so they cannot recycle
// into an app object the way deleted names can).
GLuint recycleImmediateRunBuffer(const void* context) {
    auto& retired = retiredRunBuffers();
    while (!retired.empty()) {
        const retired_run_buffer_t entry = retired.back();
        retired.pop_back();
        if (entry.context == context) return entry.name;
    }
    return 0;
}

// One compiled glBegin/glEnd run (or several merged ones): the vertex stream
// was baked at glEndList time by replaying the recorded vertex-data commands
// into a clean collector, so replay is one upload + one draw instead of
// re-executing thousands of per-vertex wrapper entries. Runs whose colors
// must feed glColorMaterial at replay time fall back to the retained
// original commands (material state mutates per glColor there).
class compiled_immediate_run_cmd_t final : public GLCmd {
public:
    compiled_immediate_run_cmd_t(GLenum primitive, const fixed_function_draw_size_t& sizes,
                                 std::vector<GLfloat>&& data, size_t vertexCount, bool hasColor,
                                 const fixed_function_draw_data_t& finalData,
                                 std::vector<std::unique_ptr<GLCmd>>&& originals)
        : primitive(primitive), sizes(sizes), data(std::move(data)), vertexCount(vertexCount),
          hasColor(hasColor), finalData(finalData), originals(std::move(originals)) {}

    void execute() const override {
        auto& gs = g_glstate_c;
        if (hasColor && gs.fpe_state.fpe_bools.color_material_enable) {
            for (const auto& command : originals) command->execute();
            return;
        }
        flushPendingImmediateDraws();

        // Slots the run never set inherit the caller's sticky sizes and
        // reach the shader as constant attributes from the live current
        // values - exactly what a live replay of the recorded commands
        // produces (GL: current state at glCallList time applies).
        auto& live = gs.fpe_state.fpe_draw.current_data;
        fixed_function_draw_size_t shader_sizes = sizes;
        for (int i = 0; i < VERTEX_POINTER_COUNT; ++i) {
            if (shader_sizes.data[i] <= 0) shader_sizes.data[i] = live.sizes.data[i];
        }
        drawImmediateVertices(primitive, data.data(), data.size(), vertexCount, sizes,
                              &shader_sizes, residentBuffer());

        // GL: attribute setters executed FROM the list update the current
        // state, and that state persists after glCallList returns. Apply the
        // run's final values for every slot it set.
        for (int i = 0; i < VERTEX_POINTER_COUNT; ++i) {
            if (sizes.data[i] <= 0) continue;
            live.sizes.data[i] = sizes.data[i];
            if (i == 0) live.vertex = finalData.vertex;
            else if (i == 1) live.normal = finalData.normal;
            else if (i == 2) live.color = finalData.color;
            else if (i >= 7) live.texcoord[i - 7] = finalData.texcoord[i - 7];
        }
    }

    bool appendRun(GLenum otherPrimitive, const fixed_function_draw_size_t& otherSizes,
                   const std::vector<GLfloat>& otherData, size_t otherVertexCount,
                   bool otherHasColor, const fixed_function_draw_data_t& otherFinalData,
                   DisplayList::iterator originalsBegin, DisplayList::iterator originalsEnd) {
        if (otherPrimitive != primitive ||
            std::memcmp(&otherSizes, &sizes, sizeof(sizes)) != 0) {
            return false;
        }
        // Merge only complete primitive groups: GL drops an incomplete final
        // group per Begin/End, so concatenation must never weld leftovers
        // onto the next run's vertices.
        size_t group = 1;
        switch (primitive) {
        case GL_TRIANGLES: group = 3; break;
        case GL_QUADS: group = 4; break;
        case GL_LINES: group = 2; break;
        case GL_POINTS: group = 1; break;
        default: return false;
        }
        if (vertexCount % group != 0 || otherVertexCount % group != 0) return false;
        data.insert(data.end(), otherData.begin(), otherData.end());
        // Any upload of the old data is now stale. Merging happens at compile
        // time, before the first replay, so this normally has nothing to drop -
        // it is here so the invariant "resident matches data" cannot be broken
        // by a later change to when merging runs.
        dropResident();
        vertexCount += otherVertexCount;
        hasColor = hasColor || otherHasColor;
        finalData = otherFinalData;
        for (auto it = originalsBegin; it != originalsEnd; ++it)
            originals.emplace_back(std::move(*it));
        return true;
    }

    // A compiled run's vertex data is immutable after glEndList, so it belongs
    // in a buffer of its own rather than being re-streamed through the ring on
    // every glCallList. Uploaded once, lazily, on the first replay that has a
    // backend; every later replay just binds it.
    //
    // Returns 0 when the upload is not possible, which makes the caller fall
    // back to the streaming path - so a failure here costs performance, never
    // correctness.
    GLuint residentBuffer() const {
        // A context switch invalidates the name: buffers are per-context and
        // this command outlives them (contexts cannot be observed being
        // destroyed, plans/07), so re-upload rather than reuse a stale name.
        const void* context = glstate_t::cached_context();
        if (resident != 0 && resident_context == context) return resident;

        if (g_glFuncs.glGenBuffers == nullptr || g_glFuncs.glBindBuffer == nullptr ||
            g_glFuncs.glBufferData == nullptr || data.empty()) {
            return 0;
        }
        resident = recycleImmediateRunBuffer(context);
        if (resident == 0) g_glFuncs.glGenBuffers(1, &resident);
        if (resident == 0) return 0;
        // The shadows' periodic heal readbacks must never adopt this buffer
        // as the app's; registering it makes every heal filter it to 0.
        sfpewNoteInternalBuffer(resident);

        // Upload through GL_COPY_WRITE_BUFFER, never GL_ARRAY_BUFFER.
        //
        // This runs as an argument to drawImmediateVertices, so it executes
        // BEFORE that call's draw-state guard captures the app's bindings -
        // there is no guard around it to undo the damage. Binding
        // GL_ARRAY_BUFFER here would therefore leave the wrapper's private
        // buffer as the live array binding, and the array-buffer shadow's
        // periodic heal (getLogicalArrayBufferBinding) would re-read it and
        // record it AS THE APP'S. Every later app draw then sources its
        // attributes from this display-list buffer: texture coordinates come
        // out as garbage, which is the "all textures wrong" failure.
        //
        // GL_COPY_WRITE_BUFFER exists precisely for this - it is not part of
        // vertex array state, nothing shadows it, and it is core in both
        // GL 3.1 and ES 3.0, inside the profile floor in docs/backend-support.md.
        g_glFuncs.glBindBuffer(GL_COPY_WRITE_BUFFER, resident);
        g_glFuncs.glBufferData(GL_COPY_WRITE_BUFFER,
                               (GLsizeiptr)(data.size() * sizeof(GLfloat)), data.data(),
                               GL_STATIC_DRAW);
        g_glFuncs.glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
        resident_context = context;
        return resident;
    }

    // Retires the upload WITHOUT calling GL.
    //
    // This runs from the destructor, and a destructor can run at process
    // teardown after the driver is unloaded while eglGetCurrentContext still
    // reports the old context - so no context check makes a glDeleteBuffers
    // here safe (measured: it segfaults, and a strict resolve does not help).
    // The name goes on a retire list instead, and residentBuffer() recycles it
    // from a live entry point. That bounds the cost of re-recording a list to
    // reuse rather than leaking a buffer per rebuild.
    void dropResident() const {
        if (resident != 0) retireImmediateRunBuffer(resident, resident_context);
        resident = 0;
        resident_context = nullptr;
    }

    ~compiled_immediate_run_cmd_t() override { dropResident(); }

private:
    GLenum primitive;
    fixed_function_draw_size_t sizes;
    std::vector<GLfloat> data;
    // Lazily uploaded copy of `data`, and the context that owns it.
    mutable GLuint resident = 0;
    mutable const void* resident_context = nullptr;
    size_t vertexCount;
    bool hasColor;
    fixed_function_draw_data_t finalData;
    std::vector<std::unique_ptr<GLCmd>> originals;
};

} // namespace

void sfpewCompileImmediateRuns(DisplayList& commands) {
    using ic = GLCmd::immediate_class_t;

    // Cheap scan first: nothing to do for lists without a Begin/End run.
    bool sawBegin = false;
    for (const auto& command : commands) {
        if (command->immediateClass() == ic::begin) { sawBegin = true; break; }
    }
    if (!sawBegin) return;

    auto& gs = g_glstate;
    auto& draw = gs.fpe_state.fpe_draw;
    // The compiler replays vertex-data commands through the live collector;
    // glNewList/glEndList inside Begin/End is undefined, so the collector is
    // idle here - but protect every piece of live state the replay touches:
    // the collection buffer itself, the current attribute values/sizes, and
    // color-material coupling (materials must not mutate at compile time).
    fixed_function_draw_state_t saved_draw;
    std::swap(saved_draw.primitive, draw.primitive);
    std::swap(saved_draw.current_data, draw.current_data);
    std::swap(saved_draw.vb, draw.vb);
    std::swap(saved_draw.vertex_count, draw.vertex_count);
    draw.current_data.sizes = {};
    const bool saved_color_material = gs.fpe_state.fpe_bools.color_material_enable;
    gs.fpe_state.fpe_bools.color_material_enable = false;
    const GLboolean saved_disable_recording = disableRecording;
    disableRecording = GL_TRUE;

    DisplayList output;
    output.reserve(commands.size());
    compiled_immediate_run_cmd_t* previous_compiled = nullptr;

    size_t i = 0;
    while (i < commands.size()) {
        if (commands[i]->immediateClass() != ic::begin) {
            // Only a directly adjacent compiled run may merge; any other
            // command is a potential state change and acts as a barrier.
            previous_compiled = nullptr;
            output.emplace_back(std::move(commands[i]));
            ++i;
            continue;
        }

        // Find the extent of the run: begin, vertex-data..., end. Any other
        // command inside the run (glMaterial etc. are legal there) makes it
        // uncompilable - leave those commands untouched.
        size_t end_index = i + 1;
        while (end_index < commands.size() &&
               commands[end_index]->immediateClass() == ic::vertex_data) {
            ++end_index;
        }
        const bool complete_run = end_index < commands.size() &&
                                  commands[end_index]->immediateClass() == ic::end;
        // commands[i] is a recorded glBegin, so the mode is the genuine enum;
        // GL_POINTS == 0 must not be confused with a missing-mode sentinel.
        const GLenum mode = commands[i]->immediateBeginMode();
        if (!complete_run || mode > GL_POLYGON) {
            previous_compiled = nullptr;
            for (size_t k = i; k < end_index; ++k) output.emplace_back(std::move(commands[k]));
            i = end_index;
            continue;
        }

        // Bake: replay the vertex-data commands into the clean collector.
        // Sizes reset per run: attributes the run never sets stay out of the
        // stream and resolve to runtime constants at draw, exactly like the
        // live replay's constant-attribute path.
        draw.reset();
        draw.current_data.sizes = {};
        draw.primitive = mode;
        for (size_t k = i + 1; k < end_index; ++k) commands[k]->execute();

        GLenum baked_mode = draw.primitive;
        std::vector<GLfloat> baked = std::move(draw.vb);
        size_t baked_vertices = draw.vertex_count;
        fixed_function_draw_size_t baked_sizes = draw.current_data.sizes;
        const bool has_color = baked_sizes.color_size > 0;
        const bool was_repacked = draw.repacked;
        const fixed_function_draw_data_t final_data = draw.current_data;
        draw.reset();

        if (was_repacked) {
            // An attribute was introduced after vertices were collected: the
            // repack backfilled with compile-time values where a live replay
            // backfills with replay-time current values. Keep the originals.
            previous_compiled = nullptr;
            for (size_t k = i; k <= end_index; ++k) output.emplace_back(std::move(commands[k]));
            i = end_index + 1;
            continue;
        }

        if (baked_vertices == 0) {
            // Empty Begin/End: keep the originals (they still flush pending
            // glyph batches in order at replay time).
            previous_compiled = nullptr;
            for (size_t k = i; k <= end_index; ++k) output.emplace_back(std::move(commands[k]));
            i = end_index + 1;
            continue;
        }

        // Expand a 4-vertex strip (the glyph pattern) into triangles so
        // adjacent glyph runs merge into one draw, mirroring the live path.
        if (baked_mode == GL_TRIANGLE_STRIP && baked_vertices == 4 && baked.size() % 4u == 0) {
            const size_t stride = baked.size() / 4u;
            std::vector<GLfloat> expanded;
            expanded.reserve(stride * 6u);
            for (const size_t v : {(size_t)0, (size_t)1, (size_t)2, (size_t)2, (size_t)1, (size_t)3})
                expanded.insert(expanded.end(), baked.begin() + v * stride,
                                baked.begin() + (v + 1u) * stride);
            baked = std::move(expanded);
            baked_vertices = 6;
            baked_mode = GL_TRIANGLES;
        }

        if (previous_compiled != nullptr &&
            previous_compiled->appendRun(baked_mode, baked_sizes, baked, baked_vertices,
                                         has_color, final_data, commands.begin() + (long)i,
                                         commands.begin() + (long)end_index + 1)) {
            i = end_index + 1;
            continue;
        }

        std::vector<std::unique_ptr<GLCmd>> originals;
        originals.reserve(end_index + 1 - i);
        for (size_t k = i; k <= end_index; ++k) originals.emplace_back(std::move(commands[k]));
        auto compiled = std::make_unique<compiled_immediate_run_cmd_t>(
            baked_mode, baked_sizes, std::move(baked), baked_vertices, has_color, final_data,
            std::move(originals));
        previous_compiled = compiled.get();
        output.emplace_back(std::move(compiled));
        i = end_index + 1;
    }

    commands = std::move(output);

    disableRecording = saved_disable_recording;
    gs.fpe_state.fpe_bools.color_material_enable = saved_color_material;
    std::swap(saved_draw.primitive, draw.primitive);
    std::swap(saved_draw.current_data, draw.current_data);
    std::swap(saved_draw.vb, draw.vb);
    std::swap(saved_draw.vertex_count, draw.vertex_count);
}

void glBegin(GLenum mode) {
    // Entry strict resolve: pins the Begin/End batch (and every vertex-data
    // call inside it) to this context via the thread-local snapshot.
    auto& gs = g_glstate;

    // GL_POINTS(0) .. GL_POLYGON(9); invalid modes are errors and are not
    // recorded into display lists.
    if (mode > GL_POLYGON) {
        gs.set_error(GL_INVALID_ENUM);
        return;
    }

    LIST_RECORD(glBegin, {}, mode)

    // Deliberately does NOT flush the pending batch: glBegin is the entry a
    // mergeable run arrives through, and draining here would cap every batch
    // at one run. queueImmediateRun compares the merge key and flushes itself
    // when this run cannot join, and glEnd flushes before any direct draw, so
    // draw order is preserved either way.

    auto& s = gs.fpe_state.fpe_draw;

    if (s.primitive != GL_NONE) {
        // Nested glBegin. Report it; keep collecting the outer primitive.
        // Checked BEFORE backend init so the error contract holds even
        // without a current context.
        gs.set_error(GL_INVALID_OPERATION);
        return;
    }

    // Advance the Begin/End state machine BEFORE backend init: the pairing
    // contract must hold even without a context (draws bail out safely).
    s.primitive = mode;

    if (!gs.fpe_ready) {
        if (init_fpe() != 0) return;
    }
}

void glEnd() {
    LIST_RECORD(glEnd, {})

    // Entry strict resolve; the draw below is GPU-visible, so glEnd always
    // re-observes the real current context. A context switched mid-batch
    // resolves to a state whose primitive is GL_NONE (the pinned batch
    // stays on the Begin context), which lands in the error path below.
    auto& gs = g_glstate;
    auto& s = gs.fpe_state.fpe_draw;
    if (s.primitive == GL_NONE) {
        // glEnd without a matching glBegin. Also drop any stray vertices
        // collected outside a Begin/End pair so they cannot leak into the
        // next primitive.
        gs.set_error(GL_INVALID_OPERATION);
        s.reset();
        return;
    }

    if (queueImmediateRun(s)) {
        s.reset();
        return;
    }

    flushPendingImmediateDraws();
    drawImmediateVertices(s.primitive, s.vb.data(), s.vb.size(), s.vertex_count,
                          s.current_data.sizes);
    s.reset();
}

void glNormal3f(GLfloat nx, GLfloat ny, GLfloat nz) {
    // LOG()
    //  LOG_D("glNormal3f(%.2f, %.2f, %.2f)", nx, ny, nz)

    LIST_RECORD(glNormal3f, {}, nx, ny, nz)

    mglNormal<GLfloat, 3>({nx, ny, nz});
}

void glTexCoord2f(GLfloat s, GLfloat t) {
    // LOG()
    //  LOG_D("glTexCoord2f(%.2f, %.2f)", s, t)

    LIST_RECORD(glTexCoord2f, {}, s, t)

    mglTexCoord<GLfloat, 2>({s, t}, 0);
}

void glTexCoord4f(GLfloat s, GLfloat t, GLfloat r, GLfloat q) {
    // LOG()
    //  LOG_D("glTexCoord4f(%.2f, %.2f, %.2f, %.2f)", s, t, r, q)

    LIST_RECORD(glTexCoord4f, {}, s, t, r, q)

    mglTexCoord<GLfloat, 4>({s, t, r, q}, 0);
}

void glMultiTexCoord2f(GLenum target, GLfloat s, GLfloat t) {
    // LOG()
    //  LOG_D("glMultiTexCoord2f(%s, %.2f, %.2f)", glEnumToString(target), s, t)

    // Runtime bounds check (asserts are compiled out by NDEBUG): an
    // out-of-range unit would index past texcoord[MAX_TEX]. Invalid enums
    // are also not recorded into display lists.
    if (target < GL_TEXTURE0 || target - GL_TEXTURE0 >= MAX_TEX) {
        g_glstate.set_error(GL_INVALID_ENUM);
        return;
    }

    LIST_RECORD(glMultiTexCoord2f, {}, target, s, t)

    mglTexCoord<GLfloat, 2>({s, t}, target - GL_TEXTURE0);
}

void glMultiTexCoord4f(GLenum target, GLfloat s, GLfloat t, GLfloat r, GLfloat q) {
    // LOG()
    //  LOG_D("glMultiTexCoord4f(%s, %.2f, %.2f, %.2f, %.2f)", glEnumToString(target), s, t, r, q)

    if (target < GL_TEXTURE0 || target - GL_TEXTURE0 >= MAX_TEX) {
        g_glstate.set_error(GL_INVALID_ENUM);
        return;
    }

    LIST_RECORD(glMultiTexCoord4f, {}, target, s, t, r, q)

    mglTexCoord<GLfloat, 4>({s, t, r, q}, target - GL_TEXTURE0);
}

void glVertex3f(GLfloat x, GLfloat y, GLfloat z) {
    // LOG()
    //  LOG_D("glVertex3f(%.2f, %.2f, %.2f)", x, y, z)

    LIST_RECORD(glVertex3f, {}, x, y, z)

    mglVertex<GLfloat, 3>({x, y, z});
}

void glVertex4f(GLfloat x, GLfloat y, GLfloat z, GLfloat w) {
    // LOG()
    //  LOG_D("glVertex4f(%.2f, %.2f, %.2f, %.2f)", x, y, z, w)

    LIST_RECORD(glVertex4f, {}, x, y, z, w)

    mglVertex<GLfloat, 4>({x, y, z, w});
}

void glColor3f(GLfloat red, GLfloat green, GLfloat blue) {
    // LOG()
    //  LOG_D("glColor3f(%f, %f, %f)", red, green, blue)

    LIST_RECORD(glColor3f, {}, red, green, blue)

    mglColor<GLfloat, 3>({red, green, blue});
}

void glColor4f(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha) {
    // LOG()
    /* TODO: This can cause problem if per-vertex color is used,
     *  (using multiple glVertex*() calls in conjunction with
     *  multiple glColor4f() calls, rather than a single glColor4f()
     *  call and some gl*Pointer() call)
     *  Fix for that situation later.
     */
    // LOG_D("glColor4f(%f, %f, %f, %f)", red, green, blue, alpha)

    LIST_RECORD(glColor4f, {}, red, green, blue, alpha)

    //    auto& attr = g_glstate.fpe_state.vertexpointer_array.attributes[vp2idx(GL_COLOR_ARRAY)];
    //    auto& vpa = g_glstate.fpe_state.vertexpointer_array;
    //    if (vpa.buffer_based) {
    //        attr.size = 4;
    //        attr.usage = GL_COLOR_ARRAY;
    //        attr.type = GL_FLOAT;
    //        attr.normalized = GL_FALSE;
    //        attr.stride = 0;
    //        attr.pointer = 0;
    //        attr.value = glm::vec4(red, green, blue, alpha);
    //        attr.varying = false;
    //    }

    mglColor<GLfloat, 4>({red, green, blue, alpha});
}
