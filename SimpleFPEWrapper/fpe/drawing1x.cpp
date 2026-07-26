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
constexpr size_t kImmediateGlyphBatchLimit = 256u;

} // namespace

GLintptr sfpewUploadImmediateVertexData(const void* data, size_t size) {
    auto& gs = g_glstate_c;
    auto& state = gs.fpe_state;
    const auto dropAllFences = [&]() {
        for (auto& fence : state.fpe_immediate_fences) {
            if (fence != nullptr && g_glFuncs.glDeleteSync != nullptr)
                g_glFuncs.glDeleteSync((GLsync)fence);
            fence = nullptr;
        }
    };
    const auto replaceImmediateBuffer = [&]() {
        dropAllFences();
        if (state.fpe_immediate_vbo_map != nullptr && g_glFuncs.glUnmapBuffer != nullptr) {
            g_glFuncs.glUnmapBuffer(GL_ARRAY_BUFFER);
        }
        g_glFuncs.glDeleteBuffers(1, &state.fpe_immediate_vbo);
        g_glFuncs.glGenBuffers(1, &state.fpe_immediate_vbo);
        g_glFuncs.glBindBuffer(GL_ARRAY_BUFFER, state.fpe_immediate_vbo);
        state.fpe_immediate_vbo_capacity = 0;
        state.fpe_immediate_vbo_offset = 0;
        state.fpe_immediate_vbo_map = nullptr;
        gs.fpe_vertex_binding_valid = false;
        for (auto& cached : gs.fpe_vertex_attributes) cached.pointer_valid = false;
    };

    const size_t required_capacity = std::max(kImmediateVboMinCapacity, std::bit_ceil(size));
    if (state.fpe_immediate_vbo_map != nullptr && required_capacity > state.fpe_immediate_vbo_capacity) {
        // Oversized immediate draws are rare, but an immutable persistent
        // store cannot grow in place. Finish users of the old store, replace
        // it, and retry with a power-of-two capacity large enough for this draw.
        if (g_glFuncs.glFinish != nullptr) g_glFuncs.glFinish();
        replaceImmediateBuffer();
        state.fpe_immediate_vbo_persistent_attempted = false;
        return sfpewUploadImmediateVertexData(data, size);
    }
    if (!state.fpe_immediate_vbo_persistent_attempted) {
        state.fpe_immediate_vbo_persistent_attempted = true;
        auto storage = g_glFuncs.glBufferStorage != nullptr ? g_glFuncs.glBufferStorage
                                                           : g_glFuncs.glBufferStorageEXT;
        if (storage != nullptr && g_glFuncs.glMapBufferRange != nullptr) {
            constexpr GLbitfield map_flags = GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;
            constexpr GLbitfield storage_flags = map_flags | GL_DYNAMIC_STORAGE_BIT;
            storage(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(required_capacity), nullptr, storage_flags);
            state.fpe_immediate_vbo_map =
                g_glFuncs.glMapBufferRange(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(required_capacity), map_flags);
            if (state.fpe_immediate_vbo_map != nullptr) {
                state.fpe_immediate_vbo_capacity = required_capacity;
                state.fpe_immediate_vbo_offset = 0;
            } else {
                // Immutable storage can't be respecified with BufferData. Replace
                // the failed persistent candidate with a fresh mutable fallback.
                replaceImmediateBuffer();
            }
        }
    }

    if (state.fpe_immediate_vbo_map == nullptr) {
        // The backend lacks coherent persistent storage. Keep the previous
        // orphaning behaviour, which is faster than synchronous SubData on
        // mobile drivers and preserves compatibility with other backends.
        g_glFuncs.glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(size), data, GL_STREAM_DRAW);
        return 0;
    }

    size_t offset = (state.fpe_immediate_vbo_offset + kImmediateVboAlignment - 1u) &
                    ~(kImmediateVboAlignment - 1u);
    if (offset + size > state.fpe_immediate_vbo_capacity) offset = 0;

    // Segmented synchronization: by the time an upload crosses into a new
    // quarter of the ring, every draw consuming the PREVIOUS quarter has
    // already been submitted (uploads and draws strictly alternate), so a
    // fence on the old quarter is a correct completion marker. Entering a
    // quarter waits on (and retires) its previous-lap fence. Without sync
    // objects this degrades to the old glFinish at wrap only.
    const bool have_sync = g_glFuncs.glFenceSync != nullptr &&
                           g_glFuncs.glClientWaitSync != nullptr && g_glFuncs.glDeleteSync != nullptr;
    const size_t segment_size = state.fpe_immediate_vbo_capacity / 4u;
    if (have_sync && segment_size != 0) {
        const size_t previous_segment =
            std::min<size_t>(state.fpe_immediate_vbo_offset / segment_size, 3u);
        const size_t first_segment = offset / segment_size;
        const size_t last_segment = std::min<size_t>((offset + size - 1u) / segment_size, 3u);
        if (first_segment != previous_segment || offset == 0) {
            auto& old_fence = state.fpe_immediate_fences[previous_segment];
            if (old_fence == nullptr)
                old_fence = (void*)g_glFuncs.glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
            for (size_t seg = first_segment; seg <= last_segment; ++seg) {
                auto& fence = state.fpe_immediate_fences[seg];
                if (fence == nullptr) continue;
                g_glFuncs.glClientWaitSync((GLsync)fence, GL_SYNC_FLUSH_COMMANDS_BIT,
                                           1000000000ull /* 1s */);
                g_glFuncs.glDeleteSync((GLsync)fence);
                fence = nullptr;
            }
        }
    } else if (offset == 0 && state.fpe_immediate_vbo_offset != 0) {
        if (g_glFuncs.glFinish != nullptr) g_glFuncs.glFinish();
    }

    if (size > 0) {
        std::memcpy(static_cast<uint8_t*>(state.fpe_immediate_vbo_map) + offset, data, size);
    }
    state.fpe_immediate_vbo_offset = offset + size;
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

struct pending_glyph_batch_t {
    // Batches are keyed to the context they were collected on: replaying
    // vertex data or texture bindings on a different context would draw
    // garbage (plans/07).
    EGLContext context = EGL_NO_CONTEXT;
    bool active = false;
    fixed_function_draw_size_t sizes{};
    std::vector<GLfloat> vertices;
    size_t vertexCount = 0;
    size_t glyphCount = 0;
    GLenum activeTexture = GL_TEXTURE0;
    GLuint texture2D = 0;
};

thread_local pending_glyph_batch_t pendingGlyphBatch;

void drawImmediateVertices(GLenum primitive, const GLfloat* vertices, size_t floatCount,
                           size_t vertexCount, const fixed_function_draw_size_t& sizes) {
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

    fpe_backend_draw_state_guard_t backendState(
        sfpewLogicalProgram(), static_cast<GLint>(sfpewLogicalArrayBufferBinding()));
    // glBegin/glEnd uses temporary interleaved data. Preserve the caller's
    // client-array declarations so an immediate draw cannot invalidate the
    // following glDrawArrays call.
    immediate_client_state_guard_t clientState;
    immediate_draw_sizes_guard_t drawSizes;

    auto& state = gs.fpe_state;
    state.fpe_draw.current_data.sizes = sizes;

    fixed_function_draw_state_t layoutState;
    layoutState.current_data.sizes = sizes;
    layoutState.compile_vertexattrib(state.vertexpointer_array);

    auto& va = state.normalized_vpa;
    va = state.vertexpointer_array.normalize();
    // generate_compressed_index only reads the size array, but its legacy
    // declaration is not const-correct.
    auto mutableSizes = sizes;
    va.generate_compressed_index(mutableSizes.data);

    auto key = gs.program_hash();
    auto& program = gs.get_or_generate_program(key);
    const int programId = program.get_program();
    if (programId <= 0) {
        gs.set_error(GL_INVALID_OPERATION);
        return;
    }

    g_glFuncs.glUseProgram(programId);
    sfpewBackendBindVertexArray(state.fpe_vao);
    g_glFuncs.glBindBuffer(GL_ARRAY_BUFFER, state.fpe_immediate_vbo);

    const GLintptr vertexOffset =
        sfpewUploadImmediateVertexData(vertices, floatCount * sizeof(GLfloat));
    gs.send_vertex_attributes(va, state.fpe_immediate_vbo, vertexOffset);
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

bool queueGlyphTriangleStrip(const fixed_function_draw_state_t& draw) {
    if (DisplayListManager::isCalling() || draw.primitive != GL_TRIANGLE_STRIP ||
        draw.vertex_count != 4 || draw.vb.empty() || draw.vb.size() % 4u != 0) {
        return false;
    }

    auto& batch = pendingGlyphBatch;
    const auto& sizes = draw.current_data.sizes;
    const GLenum activeTexture = sfpewLogicalActiveTexture();
    const GLuint texture2D = sfpewLogicalTextureBinding(GL_TEXTURE_2D);
    if (batch.active &&
        (std::memcmp(&batch.sizes, &sizes, sizeof(sizes)) != 0 ||
         batch.activeTexture != activeTexture || batch.texture2D != texture2D)) {
        flushPendingImmediateDraws();
    }
    if (!batch.active) {
        batch.active = true;
        // Called from glEnd, whose entry resolve refreshed the snapshot.
        batch.context = (EGLContext)glstate_t::cached_context();
        batch.sizes = sizes;
        batch.vertices.clear();
        batch.vertexCount = 0;
        batch.glyphCount = 0;
        batch.activeTexture = activeTexture;
        batch.texture2D = texture2D;
        batch.vertices.reserve(kImmediateGlyphBatchLimit * (draw.vb.size() / 4u) * 6u);
    }

    const size_t stride = draw.vb.size() / 4u;
    const size_t oldSize = batch.vertices.size();
    batch.vertices.resize(oldSize + stride * 6u);
    GLfloat* output = batch.vertices.data() + oldSize;
    const auto appendVertex = [&](size_t index) {
        std::memcpy(output, draw.vb.data() + index * stride, stride * sizeof(GLfloat));
        output += stride;
    };
    // Four vertices in a triangle strip form 0-1-2 and 2-1-3. Expand to
    // independent triangles so adjacent glyphs can share one draw call.
    appendVertex(0);
    appendVertex(1);
    appendVertex(2);
    appendVertex(2);
    appendVertex(1);
    appendVertex(3);
    batch.vertexCount += 6;
    ++batch.glyphCount;

    if (batch.glyphCount >= kImmediateGlyphBatchLimit) flushPendingImmediateDraws();
    return true;
}

} // namespace

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
        SFPEW_LOGW("dropping %zu pending glyphs collected on a different context", batch.glyphCount);
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

    drawImmediateVertices(GL_TRIANGLES, batch.vertices.data(), batch.vertices.size(),
                          batch.vertexCount, batch.sizes);

    if (callerTexture2D != batch.texture2D)
        g_glFuncs.glBindTexture(GL_TEXTURE_2D, callerTexture2D);
    if (callerActiveTexture != batch.activeTexture)
        g_glFuncs.glActiveTexture(callerActiveTexture);
    batch.active = false;
    batch.vertices.clear();
    batch.vertexCount = 0;
    batch.glyphCount = 0;
}

namespace {

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
                                 std::vector<std::unique_ptr<GLCmd>>&& originals)
        : primitive(primitive), sizes(sizes), data(std::move(data)), vertexCount(vertexCount),
          hasColor(hasColor), originals(std::move(originals)) {}

    void execute() const override {
        if (hasColor && g_glstate_c.fpe_state.fpe_bools.color_material_enable) {
            for (const auto& command : originals) command->execute();
            return;
        }
        flushPendingImmediateDraws();
        drawImmediateVertices(primitive, data.data(), data.size(), vertexCount, sizes);
    }

    bool appendRun(GLenum otherPrimitive, const fixed_function_draw_size_t& otherSizes,
                   const std::vector<GLfloat>& otherData, size_t otherVertexCount,
                   bool otherHasColor, DisplayList::iterator originalsBegin,
                   DisplayList::iterator originalsEnd) {
        if (otherPrimitive != primitive ||
            std::memcmp(&otherSizes, &sizes, sizeof(sizes)) != 0) {
            return false;
        }
        if (primitive != GL_TRIANGLES && primitive != GL_QUADS && primitive != GL_LINES &&
            primitive != GL_POINTS) {
            return false;
        }
        data.insert(data.end(), otherData.begin(), otherData.end());
        vertexCount += otherVertexCount;
        hasColor = hasColor || otherHasColor;
        for (auto it = originalsBegin; it != originalsEnd; ++it)
            originals.emplace_back(std::move(*it));
        return true;
    }

private:
    GLenum primitive;
    fixed_function_draw_size_t sizes;
    std::vector<GLfloat> data;
    size_t vertexCount;
    bool hasColor;
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
        const GLenum mode = commands[i]->immediateBeginMode();
        if (!complete_run || mode == GL_NONE || mode > GL_POLYGON) {
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
        draw.reset();

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
                                         has_color, commands.begin() + (long)i,
                                         commands.begin() + (long)end_index + 1)) {
            i = end_index + 1;
            continue;
        }

        std::vector<std::unique_ptr<GLCmd>> originals;
        originals.reserve(end_index + 1 - i);
        for (size_t k = i; k <= end_index; ++k) originals.emplace_back(std::move(commands[k]));
        auto compiled = std::make_unique<compiled_immediate_run_cmd_t>(
            baked_mode, baked_sizes, std::move(baked), baked_vertices, has_color,
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

    if (mode != GL_TRIANGLE_STRIP) flushPendingImmediateDraws();

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

    if (queueGlyphTriangleStrip(s)) {
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
