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

} // namespace

void glBegin(GLenum mode) {
    LIST_RECORD(glBegin, {}, mode)

    if (!fpe_inited) {
        if (init_fpe() != 0) return;
    }

    auto& s = g_glstate.fpe_state.fpe_draw;

    if (s.primitive != GL_NONE) {
        return;
    }

    s.primitive = mode;
}

void glEnd() {
    LIST_RECORD(glEnd, {})

    auto& s = g_glstate.fpe_state.fpe_draw;
    auto& raw_va = g_glstate.fpe_state.vertexpointer_array;
    //    auto& vb = g_glstate.fpe_state.fpe_vb;
    auto& vb = g_glstate.fpe_state.fpe_draw.vb;

    if (s.primitive == GL_NONE) {
        return;
    }

    fpe_backend_draw_state_guard_t backend_state;
    // glBegin/glEnd uses temporary interleaved data. Preserve the caller's
    // client-array declarations so an immediate draw cannot invalidate the
    // following glDrawArrays call.
    immediate_client_state_guard_t client_state;

    // actual assembly work, and draw!
    {
        // Vertex Pointer State Machine Update
        g_glstate.fpe_state.fpe_draw.compile_vertexattrib(raw_va);

        auto& va = g_glstate.fpe_state.normalized_vpa;
        va = raw_va.normalize();
        // Need to generate_compressed_index first (shadergen will use that)
        va.generate_compressed_index(g_glstate.fpe_state.fpe_draw.current_data.sizes.data);

        auto key = g_glstate.program_hash();

        // Program
        auto& prog = g_glstate.get_or_generate_program(key);

        int prog_id = prog.get_program();
        if (prog_id <= 0) {
            s.reset();
            return;
        }
        g_glFuncs.glUseProgram(prog_id);

        // VAO, VB
        g_glFuncs.glBindVertexArray(g_glstate.fpe_state.fpe_vao);

        g_glFuncs.glBindBuffer(GL_ARRAY_BUFFER, g_glstate.fpe_state.fpe_immediate_vbo);

        const GLintptr vertex_offset = uploadImmediateVertexData(vb.data(), vb.size() * sizeof(GLfloat));

        // Vertex Pointer to ES
        g_glstate.send_vertex_attributes(va, g_glstate.fpe_state.fpe_immediate_vbo, vertex_offset);

        // Uniform
        { g_glstate.send_uniforms(prog); }

        // Draw
        // LOG_D("glEnd: glDrawArrays(%s, %d, %d), vb = %d, vb size = %d", glEnumToString(s.primitive), 0,
        // s.vertex_count,
        //      g_glstate.fpe_state.fpe_vbo, vb.size() * sizeof(GLfloat))
        if (s.primitive == GL_QUADS) {
            const GLsizei vertex_count = static_cast<GLsizei>(s.vertex_count);
            const GLsizei index_count = (vertex_count / 4) * 6;
            const bool upload_indices = prepare_quad_indices(vertex_count, 0);
            if (!g_glstate.fpe_state.fpe_ibo_bound) {
                g_glFuncs.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_glstate.fpe_state.fpe_ibo);
                g_glstate.fpe_state.fpe_ibo_bound = true;
            }
            if (upload_indices) {
                g_glFuncs.glBufferData(GL_ELEMENT_ARRAY_BUFFER, quad_index_size_bytes(), quad_index_data(),
                                       GL_DYNAMIC_DRAW);
            }
            g_glFuncs.glDrawElements(GL_TRIANGLES, index_count, quad_index_type(), (void*)0);
        } else {
            g_glFuncs.glDrawArrays(s.primitive, 0, s.vertex_count);
        }
    }

    // resetting draw state
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

    LIST_RECORD(glMultiTexCoord2f, {}, target, s, t)

    assert(target - GL_TEXTURE0 < MAX_TEX);
    mglTexCoord<GLfloat, 2>({s, t}, target - GL_TEXTURE0);
}

void glMultiTexCoord4f(GLenum target, GLfloat s, GLfloat t, GLfloat r, GLfloat q) {
    // LOG()
    //  LOG_D("glMultiTexCoord4f(%s, %.2f, %.2f, %.2f, %.2f)", glEnumToString(target), s, t, r, q)

    LIST_RECORD(glMultiTexCoord4f, {}, target, s, t, r, q)

    assert(target - GL_TEXTURE0 < MAX_TEX);
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
