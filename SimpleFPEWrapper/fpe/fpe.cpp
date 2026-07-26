// SimpleFPEWrapper - SimpleFPEWrapper/fpe/fpe.cpp
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

#include "fpe.hpp"
#include <memory>
#include <mutex>
#include <glm/gtc/type_ptr.hpp>
#include <limits>
#include <cstring>
#include <vector>

#define DEBUG 0

namespace {
// Thread-local snapshot of the last strict resolve. Shared by
// get_instance() / current() / current_vertex_data() / cached_context().
thread_local EGLContext tls_snapshot_context = (EGLContext)(intptr_t)-1;
thread_local glstate_t* tls_snapshot_state = nullptr;
} // namespace

glstate_t& glstate_t::get_instance() {
    // Per-EGL-context FPE state (plans/07). The wrapper cannot intercept
    // eglMakeCurrent (apps talk to libEGL directly), so the current context
    // is resolved lazily with a thread-local snapshot. On glvnd desktops
    // eglGetCurrentContext costs ~425ns (getpid + dispatch mutex in the
    // driver), so this strict resolve runs once per exported entry point;
    // everything downstream uses current() (docs/context-model.md).
    //
    // Known limits (documented in plans/07): contexts cannot be observed
    // being destroyed, so their CPU-side state objects persist for the
    // process lifetime (the GL objects inside die with the context); and
    // share-group relationships are invisible, so display-list DEFINITIONS
    // stay process-global in DisplayListManager.
    static std::unordered_map<void*, std::unique_ptr<glstate_t>> instances;
    static std::mutex instances_mutex;
    static glstate_t no_context_state; // keeps backend-less calls crash-free

    const EGLContext context =
        g_eglFuncs.eglGetCurrentContext ? g_eglFuncs.eglGetCurrentContext() : EGL_NO_CONTEXT;

    if (context == tls_snapshot_context && tls_snapshot_state != nullptr)
        return *tls_snapshot_state;

    // The thread switched contexts. If the outgoing snapshot still holds an
    // open Begin/End batch, that batch was abandoned mid-collection: clear it
    // here so the vertex-data pin cannot route later calls (made on other
    // contexts) into the stale batch. This runs only on actual switches and
    // implements the documented drop-the-batch semantics (context-model.md).
    if (tls_snapshot_state != nullptr && tls_snapshot_context != EGL_NO_CONTEXT &&
        tls_snapshot_state->fpe_state.fpe_draw.primitive != GL_NONE) {
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
    return *state;
}

glstate_t& glstate_t::current() {
    if (tls_snapshot_state != nullptr) return *tls_snapshot_state;
    return get_instance();
}

glstate_t& glstate_t::current_vertex_data() {
    // Pin only to a real context: a Begin issued with no current context
    // lands on no_context_state, and vertices arriving after the app then
    // makes a context current must resolve strictly (and be dropped by the
    // primitive==GL_NONE guard) exactly like the pre-snapshot behavior.
    glstate_t* state = tls_snapshot_state;
    if (state != nullptr && state->fpe_state.fpe_draw.primitive != GL_NONE &&
        tls_snapshot_context != EGL_NO_CONTEXT) {
        return *state;
    }
    return get_instance();
}

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

bool prepare_quad_indices(GLsizei n, GLuint first) {
    auto& state = g_glstate_c.fpe_state;
    const size_t num_quads = n > 0 ? static_cast<size_t>(n) / 4u : 0u;
    const uint64_t max_index = num_quads == 0
                                   ? static_cast<uint64_t>(first)
                                   : static_cast<uint64_t>(first) + num_quads * 4u - 1u;
    const GLenum index_type = max_index <= std::numeric_limits<uint16_t>::max()
                                  ? GL_UNSIGNED_SHORT
                                  : GL_UNSIGNED_INT;
    if (state.fpe_ib_valid && state.fpe_ib_first == first && state.fpe_ib_type == index_type &&
        state.fpe_ib_quad_count >= num_quads) {
        return false;
    }

    size_t first_quad_to_generate = 0;
    if (state.fpe_ib_valid && state.fpe_ib_first == first && state.fpe_ib_type == index_type) {
        first_quad_to_generate = state.fpe_ib_quad_count;
    }

    if (index_type == GL_UNSIGNED_SHORT) {
        state.fpe_ib16.resize(num_quads * 6u);
        for (size_t i = first_quad_to_generate; i < num_quads; ++i) {
            const uint16_t base_index = static_cast<uint16_t>(first + i * 4u);
            state.fpe_ib16[i * 6u + 0u] = static_cast<uint16_t>(base_index + 0u);
            state.fpe_ib16[i * 6u + 1u] = static_cast<uint16_t>(base_index + 1u);
            state.fpe_ib16[i * 6u + 2u] = static_cast<uint16_t>(base_index + 2u);
            state.fpe_ib16[i * 6u + 3u] = static_cast<uint16_t>(base_index + 2u);
            state.fpe_ib16[i * 6u + 4u] = static_cast<uint16_t>(base_index + 3u);
            state.fpe_ib16[i * 6u + 5u] = static_cast<uint16_t>(base_index + 0u);
        }
    } else {
        state.fpe_ib.resize(num_quads * 6u);
        for (size_t i = first_quad_to_generate; i < num_quads; ++i) {
            const uint32_t base_index = first + static_cast<uint32_t>(i * 4u);
            state.fpe_ib[i * 6u + 0u] = base_index + 0u;
            state.fpe_ib[i * 6u + 1u] = base_index + 1u;
            state.fpe_ib[i * 6u + 2u] = base_index + 2u;
            state.fpe_ib[i * 6u + 3u] = base_index + 2u;
            state.fpe_ib[i * 6u + 4u] = base_index + 3u;
            state.fpe_ib[i * 6u + 5u] = base_index + 0u;
        }
    }
    state.fpe_ib_first = first;
    state.fpe_ib_quad_count = num_quads;
    state.fpe_ib_type = index_type;
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

    g_glFuncs.glGenBuffers(1, &g_glstate_c.fpe_state.fpe_immediate_vbo);
    g_glstate_c.fpe_state.fpe_immediate_vbo_capacity = 0;
    g_glstate_c.fpe_state.fpe_immediate_vbo_offset = 0;
    g_glstate_c.fpe_state.fpe_immediate_vbo_map = nullptr;
    g_glstate_c.fpe_state.fpe_immediate_vbo_persistent_attempted = false;

    g_glFuncs.glGenBuffers(1, &g_glstate_c.fpe_state.fpe_ibo);

    // LOG_D("fpe_vao: %d", g_glstate_c.fpe_state.fpe_vao)
    // LOG_D("fpe_vbo: %d", g_glstate_c.fpe_state.fpe_vbo)
    // LOG_D("fpe_ibo: %d", g_glstate_c.fpe_state.fpe_ibo)

    if (g_glstate_c.fpe_state.fpe_vao == 0 || g_glstate_c.fpe_state.fpe_vbo == 0 ||
        g_glstate_c.fpe_state.fpe_immediate_vbo == 0 ||
        g_glstate_c.fpe_state.fpe_ibo == 0) {
        if (g_glstate_c.fpe_state.fpe_vao != 0)
            g_glFuncs.glDeleteVertexArrays(1, &g_glstate_c.fpe_state.fpe_vao);
        if (g_glstate_c.fpe_state.fpe_vbo != 0)
            g_glFuncs.glDeleteBuffers(1, &g_glstate_c.fpe_state.fpe_vbo);
        if (g_glstate_c.fpe_state.fpe_immediate_vbo != 0)
            g_glFuncs.glDeleteBuffers(1, &g_glstate_c.fpe_state.fpe_immediate_vbo);
        if (g_glstate_c.fpe_state.fpe_ibo != 0)
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

namespace {

// Multiple INDEPENDENT client-memory arrays (classic GL 1.1: separate
// glVertexPointer/glColorPointer allocations) cannot be expressed by
// normalize()'s single interleave stride. Gather them into one interleaved
// stream instead; correctness first, the copy is bounded by the draw size.
bool gather_client_arrays(const vertex_pointer_array_t& raw, GLint first, GLsizei count,
                          vertex_pointer_array_t* out) {
    if (count <= 0 || first < 0) return false;
    int enabled_count = 0;
    size_t element_bytes[VERTEX_POINTER_COUNT] = {};
    size_t total_stride = 0;
    for (int i = 0; i < VERTEX_POINTER_COUNT; ++i) {
        if (!((raw.enabled_pointers >> i) & 1u)) continue;
        const auto& attr = raw.attributes[i];
        if (getClientArrayBufferBinding(i) != 0) return false; // VBO mix: original path
        if (attr.pointer == nullptr || attr.size <= 0 || type_size(attr.type) == 0) return false;
        ++enabled_count;
        element_bytes[i] = (size_t)attr.size * (size_t)type_size(attr.type);
        total_stride += element_bytes[i];
    }
    if (enabled_count < 2 || total_stride == 0) return false;

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

} // namespace

int commit_fpe_state_on_draw(GLenum* mode, GLint* first, GLsizei* count, GLint previous_array_buffer) {
    // LOG()

    if (!g_glstate_c.fpe_ready) {
        if (init_fpe() != 0) return -1;
    }

    // Need to generate_compressed_index first (shadergen will use that)
    auto& raw_vpa = g_glstate_c.fpe_state.vertexpointer_array;
    auto& vpa = g_glstate_c.fpe_state.normalized_vpa;
    if (gather_client_arrays(raw_vpa, *first, *count, &vpa)) {
        *first = 0; // the gather already applied the base offset
    } else {
        vpa = raw_vpa.normalize();
    }
    vpa.generate_compressed_index(g_glstate_c.fpe_state.fpe_draw.current_data.sizes.data);
    // kinda cursed...
    raw_vpa.generate_compressed_index(g_glstate_c.fpe_state.fpe_draw.current_data.sizes.data);
    //    g_glFuncs.glGenVertexArrays(1, &vpa.fpe_vao);
    // LOG_D("fpe_vao: %d", g_glstate_c.fpe_state.fpe_vao)
    g_glFuncs.glBindVertexArray(g_glstate_c.fpe_state.fpe_vao);

    auto key = g_glstate_c.program_hash();
    // LOG_D("%s: key=0x%x", __func__, key)
    auto& prog = g_glstate_c.get_or_generate_program(key);
    int prog_id = prog.get_program();
    if (prog_id <= 0) {
        // Generated program failed to compile/link: the draw is dropped, and
        // per the error contract that must be observable, not silent.
        g_glstate_c.set_error(GL_INVALID_OPERATION);
        vpa.reset();
        return -1;
    }
    g_glFuncs.glUseProgram(prog_id);

    // Client-memory arrays are uploaded with glBufferData below. That upload
    // must NEVER target the caller's bound VBO: route it into fpe_vbo even
    // when the caller had a buffer bound, or their buffer contents would be
    // destroyed by the draw.
    const bool client_memory_draw =
        reinterpret_cast<uintptr_t>(vpa.starting_pointer) > static_cast<uintptr_t>(vpa.stride);

    // Ugh...Why binding vbo is required BEFORE calling VertexAttrib* functions?
    if (previous_array_buffer == 0 || client_memory_draw) {
        g_glFuncs.glBindBuffer(GL_ARRAY_BUFFER, g_glstate_c.fpe_state.fpe_vbo);
    }

    // LOG_D("starting_ptr = %p", vpa.starting_pointer)
    // LOG_D("stride = %d", vpa.stride)

    const GLuint attribute_array_buffer = (previous_array_buffer == 0 || client_memory_draw)
                                              ? g_glstate_c.fpe_state.fpe_vbo
                                              : static_cast<GLuint>(previous_array_buffer);
    g_glstate_c.send_vertex_attributes(vpa, attribute_array_buffer);
    vpa.dirty = false;

    int ret = 0;

    // Making sure it is a valid pointer rather than an offset into the buffer
    if (client_memory_draw) {
        // LOG_D("VB @ 0x%x, size = %d * %d = %d", vpa.starting_pointer, *count, vpa.stride, *count * vpa.stride)

#if DEBUG || GLOBAL_DEBUG
        //    for (int j = 0; j < *count; ++j) {
//        for (int i = 0; i < VERTEX_POINTER_COUNT; ++i) {
//            bool enabled = ((vpa.enabled_pointers >> i) & 1);
//
//            if (!enabled)
//                continue;
//
//            auto &vp = vpa.pointers[i];
//
//            // const void* ptr, GLenum type, int size, int stride, int offset, int i
//            log_vtx_attrib_data(vpa.starting_pointer, vp.type, vp.size, vp.stride,
//                                (const char*)vp.pointer - (const char*)vpa.starting_pointer, j);
//
//        }
//        // LOG_D("")
//    }
#endif

        // LOG_D("glBufferData: size = %d, data = 0x%x -> GL_ARRAY_BUFFER (%d)", *count * vpa.stride,
        // vpa.starting_pointer,
        //      g_glstate_c.fpe_state.fpe_vbo)

        // 64-bit size math: GLsizei * GLsizei overflowed for large draws,
        // handing glBufferData a negative or wrapped size.
        const int64_t upload_size = (int64_t)*count * (int64_t)vpa.stride;
        const int64_t skip = (int64_t)*first * (int64_t)vpa.stride;
        if (upload_size <= 0 || upload_size > (int64_t)std::numeric_limits<GLsizei>::max() || skip < 0) {
            g_glstate_c.set_error(GL_INVALID_VALUE);
            vpa.reset();
            return -1;
        }
        const auto* draw_start = static_cast<const uint8_t*>(vpa.starting_pointer) + skip;
        g_glFuncs.glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)upload_size, draw_start, GL_DYNAMIC_DRAW);
        *first = 0;

    } else {
        // LOG_D("Using already bound VB")
    }

    // plans/08 8.3: GL_LINE/GL_POINT polygon modes (uniform across faces -
    // per-face split needs CPU facing tests and stays a documented gap).
    const GLenum polygon_mode = g_glstate_c.fpe_uniform.polygon_mode_front;
    const bool filled_primitive = *mode == GL_TRIANGLES || *mode == GL_TRIANGLE_STRIP ||
                                  *mode == GL_TRIANGLE_FAN || *mode == GL_QUADS ||
                                  *mode == GL_QUAD_STRIP || *mode == GL_POLYGON;
    if (filled_primitive && polygon_mode == g_glstate_c.fpe_uniform.polygon_mode_back &&
        polygon_mode == GL_POINT) {
        // Vertices repeat across shared corners; visually identical to spec.
        *mode = GL_POINTS;
        g_glstate_c.send_uniforms(prog);
        vpa.reset();
        return 0;
    }
    if (filled_primitive && polygon_mode == g_glstate_c.fpe_uniform.polygon_mode_back &&
        polygon_mode == GL_LINE) {
        // Expand every triangle (or quad) into its outline edges. Shared
        // edges draw twice, which matches the visual result of wireframe.
        thread_local std::vector<uint32_t> wire;
        wire.clear();
        const uint32_t base = (uint32_t)*first;
        const uint32_t n = (uint32_t)*count;
        const auto edge = [&](uint32_t a, uint32_t b) {
            wire.push_back(base + a);
            wire.push_back(base + b);
        };
        if (*mode == GL_TRIANGLES) {
            for (uint32_t i = 0; i + 2 < n; i += 3) { edge(i, i + 1); edge(i + 1, i + 2); edge(i + 2, i); }
        } else if (*mode == GL_QUADS) {
            for (uint32_t i = 0; i + 3 < n; i += 4) {
                edge(i, i + 1); edge(i + 1, i + 2); edge(i + 2, i + 3); edge(i + 3, i);
            }
        } else if (*mode == GL_TRIANGLE_STRIP || *mode == GL_QUAD_STRIP) {
            for (uint32_t i = 0; i + 2 < n; ++i) { edge(i, i + 1); edge(i + 1, i + 2); edge(i + 2, i); }
        } else { // FAN / POLYGON
            for (uint32_t i = 1; i + 1 < n; ++i) { edge(0, i); edge(i, i + 1); edge(i + 1, 0); }
        }
        if (!wire.empty()) {
            auto& st = g_glstate_c.fpe_state;
            if (st.fpe_element_ibo == 0) g_glFuncs.glGenBuffers(1, &st.fpe_element_ibo);
            g_glFuncs.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, st.fpe_element_ibo);
            st.fpe_ibo_bound = false; // fpe_vao's element binding changed
            g_glFuncs.glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                                   (GLsizeiptr)(wire.size() * sizeof(uint32_t)), wire.data(),
                                   GL_DYNAMIC_DRAW);
            *mode = GL_LINES;
            *count = (GLsizei)wire.size();
            g_glstate_c.send_uniforms(prog);
            vpa.reset();
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
            g_glFuncs.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_glstate_c.fpe_state.fpe_ibo);
            g_glstate_c.fpe_state.fpe_ibo_bound = true;
        }

        if (upload_indices) {
            g_glFuncs.glBufferData(GL_ELEMENT_ARRAY_BUFFER, quad_index_size_bytes(), quad_index_data(),
                                   GL_DYNAMIC_DRAW);
        }

        *count = index_count;

        *mode = GL_TRIANGLES;
        ret = 1;
    }

    g_glstate_c.send_uniforms(prog);
    vpa.reset();
    //    vpa.starting_pointer = 0;
    //    vpa.stride = 0;
    return ret;
}
