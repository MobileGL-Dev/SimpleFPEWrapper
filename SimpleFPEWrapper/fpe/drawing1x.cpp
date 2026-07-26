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

GLintptr uploadImmediateVertexData(const void* data, size_t size) {
    auto& state = g_glstate.fpe_state;
    const auto replaceImmediateBuffer = [&]() {
        if (state.fpe_immediate_vbo_map != nullptr && g_glFuncs.glUnmapBuffer != nullptr) {
            g_glFuncs.glUnmapBuffer(GL_ARRAY_BUFFER);
        }
        g_glFuncs.glDeleteBuffers(1, &state.fpe_immediate_vbo);
        g_glFuncs.glGenBuffers(1, &state.fpe_immediate_vbo);
        g_glFuncs.glBindBuffer(GL_ARRAY_BUFFER, state.fpe_immediate_vbo);
        state.fpe_immediate_vbo_capacity = 0;
        state.fpe_immediate_vbo_offset = 0;
        state.fpe_immediate_vbo_map = nullptr;
        g_glstate.fpe_vertex_binding_valid = false;
        for (auto& cached : g_glstate.fpe_vertex_attributes) cached.pointer_valid = false;
    };

    const size_t required_capacity = std::max(kImmediateVboMinCapacity, std::bit_ceil(size));
    if (state.fpe_immediate_vbo_map != nullptr && required_capacity > state.fpe_immediate_vbo_capacity) {
        // Oversized immediate draws are rare, but an immutable persistent
        // store cannot grow in place. Finish users of the old store, replace
        // it, and retry with a power-of-two capacity large enough for this draw.
        if (g_glFuncs.glFinish != nullptr) g_glFuncs.glFinish();
        replaceImmediateBuffer();
        state.fpe_immediate_vbo_persistent_attempted = false;
        return uploadImmediateVertexData(data, size);
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
    if (offset + size > state.fpe_immediate_vbo_capacity) {
        // The ring is deliberately large, so this is infrequent. Waiting at
        // wrap keeps persistent mapped writes from racing old GPU consumers.
        if (g_glFuncs.glFinish != nullptr) g_glFuncs.glFinish();
        offset = 0;
    }
    if (size > 0) {
        std::memcpy(static_cast<uint8_t*>(state.fpe_immediate_vbo_map) + offset, data, size);
    }
    state.fpe_immediate_vbo_offset = offset + size;
    return static_cast<GLintptr>(offset);
}

struct immediate_client_state_guard_t {
    vertex_pointer_array_t vertexPointerArray = g_glstate.fpe_state.vertexpointer_array;
    vertex_pointer_array_t normalizedVertexPointerArray = g_glstate.fpe_state.normalized_vpa;

    immediate_client_state_guard_t() = default;

    ~immediate_client_state_guard_t() {
        g_glstate.fpe_state.vertexpointer_array = vertexPointerArray;
        g_glstate.fpe_state.normalized_vpa = normalizedVertexPointerArray;
    }

    immediate_client_state_guard_t(const immediate_client_state_guard_t&) = delete;
    immediate_client_state_guard_t& operator=(const immediate_client_state_guard_t&) = delete;
};

struct immediate_draw_sizes_guard_t {
    fixed_function_draw_size_t sizes = g_glstate.fpe_state.fpe_draw.current_data.sizes;

    ~immediate_draw_sizes_guard_t() { g_glstate.fpe_state.fpe_draw.current_data.sizes = sizes; }

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

    fpe_backend_draw_state_guard_t backendState(
        sfpewLogicalProgram(), static_cast<GLint>(sfpewLogicalArrayBufferBinding()));
    // glBegin/glEnd uses temporary interleaved data. Preserve the caller's
    // client-array declarations so an immediate draw cannot invalidate the
    // following glDrawArrays call.
    immediate_client_state_guard_t clientState;
    immediate_draw_sizes_guard_t drawSizes;

    auto& state = g_glstate.fpe_state;
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

    auto key = g_glstate.program_hash();
    auto& program = g_glstate.get_or_generate_program(key);
    const int programId = program.get_program();
    if (programId <= 0) {
        g_glstate.set_error(GL_INVALID_OPERATION);
        return;
    }

    g_glFuncs.glUseProgram(programId);
    g_glFuncs.glBindVertexArray(state.fpe_vao);
    g_glFuncs.glBindBuffer(GL_ARRAY_BUFFER, state.fpe_immediate_vbo);

    const GLintptr vertexOffset =
        uploadImmediateVertexData(vertices, floatCount * sizeof(GLfloat));
    g_glstate.send_vertex_attributes(va, state.fpe_immediate_vbo, vertexOffset);
    g_glstate.send_uniforms(program);

    const GLsizei drawCount = static_cast<GLsizei>(vertexCount);
    if (primitive == GL_QUADS) {
        const GLsizei indexCount = (drawCount / 4) * 6;
        const bool uploadIndices = prepare_quad_indices(drawCount, 0);
        if (!state.fpe_ibo_bound) {
            g_glFuncs.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, state.fpe_ibo);
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
        batch.context =
            g_eglFuncs.eglGetCurrentContext ? g_eglFuncs.eglGetCurrentContext() : EGL_NO_CONTEXT;
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
    const EGLContext current =
        g_eglFuncs.eglGetCurrentContext ? g_eglFuncs.eglGetCurrentContext() : EGL_NO_CONTEXT;
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

void glBegin(GLenum mode) {
    // GL_POINTS(0) .. GL_POLYGON(9); invalid modes are errors and are not
    // recorded into display lists.
    if (mode > GL_POLYGON) {
        g_glstate.set_error(GL_INVALID_ENUM);
        return;
    }

    LIST_RECORD(glBegin, {}, mode)

    if (mode != GL_TRIANGLE_STRIP) flushPendingImmediateDraws();

    if (!g_glstate.fpe_ready) {
        if (init_fpe() != 0) return;
    }

    auto& s = g_glstate.fpe_state.fpe_draw;

    if (s.primitive != GL_NONE) {
        // Nested glBegin. Report it; keep collecting the outer primitive.
        g_glstate.set_error(GL_INVALID_OPERATION);
        return;
    }

    s.primitive = mode;
}

void glEnd() {
    LIST_RECORD(glEnd, {})

    auto& s = g_glstate.fpe_state.fpe_draw;
    if (s.primitive == GL_NONE) {
        // glEnd without a matching glBegin. Also drop any stray vertices
        // collected outside a Begin/End pair so they cannot leak into the
        // next primitive.
        g_glstate.set_error(GL_INVALID_OPERATION);
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
