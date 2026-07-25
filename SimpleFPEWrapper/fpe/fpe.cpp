// SimpleFPEWrapper - SimpleFPEWrapper/fpe/fpe.cpp
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

#include "fpe.hpp"
#include <glm/gtc/type_ptr.hpp>
#include <limits>

#define DEBUG 0

glstate_t& glstate_t::get_instance() {
    static glstate_t s_glstate;
    return s_glstate;
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
    auto& state = g_glstate.fpe_state;
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
    const auto& state = g_glstate.fpe_state;
    return state.fpe_ib_type == GL_UNSIGNED_SHORT
               ? static_cast<const void*>(state.fpe_ib16.data())
               : static_cast<const void*>(state.fpe_ib.data());
}

size_t quad_index_size_bytes() {
    const auto& state = g_glstate.fpe_state;
    return state.fpe_ib_type == GL_UNSIGNED_SHORT ? state.fpe_ib16.size() * sizeof(uint16_t)
                                                  : state.fpe_ib.size() * sizeof(uint32_t);
}

GLenum quad_index_type() {
    return g_glstate.fpe_state.fpe_ib_type;
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

bool fpe_inited = false;
int init_fpe() {
    // LOG_I("Initializing fixed-function pipeline...")

    if (fpe_inited) return 0;

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

    g_glFuncs.glGenVertexArrays(1, &g_glstate.fpe_state.fpe_vao);

    g_glFuncs.glGenBuffers(1, &g_glstate.fpe_state.fpe_vbo);

    g_glFuncs.glGenBuffers(1, &g_glstate.fpe_state.fpe_ibo);

    // LOG_D("fpe_vao: %d", g_glstate.fpe_state.fpe_vao)
    // LOG_D("fpe_vbo: %d", g_glstate.fpe_state.fpe_vbo)
    // LOG_D("fpe_ibo: %d", g_glstate.fpe_state.fpe_ibo)

    if (g_glstate.fpe_state.fpe_vao == 0 || g_glstate.fpe_state.fpe_vbo == 0 ||
        g_glstate.fpe_state.fpe_ibo == 0) {
        if (g_glstate.fpe_state.fpe_vao != 0)
            g_glFuncs.glDeleteVertexArrays(1, &g_glstate.fpe_state.fpe_vao);
        if (g_glstate.fpe_state.fpe_vbo != 0)
            g_glFuncs.glDeleteBuffers(1, &g_glstate.fpe_state.fpe_vbo);
        if (g_glstate.fpe_state.fpe_ibo != 0)
            g_glFuncs.glDeleteBuffers(1, &g_glstate.fpe_state.fpe_ibo);
        g_glstate.fpe_state.fpe_vao = 0;
        g_glstate.fpe_state.fpe_vbo = 0;
        g_glstate.fpe_state.fpe_ibo = 0;
        return -1;
    }

    fpe_inited = true;
    return 0;
}

int commit_fpe_state_on_draw(GLenum* mode, GLint* first, GLsizei* count, GLint previous_array_buffer) {
    // LOG()

    if (!fpe_inited) {
        if (init_fpe() != 0) return -1;
    }

    // Need to generate_compressed_index first (shadergen will use that)
    auto& raw_vpa = g_glstate.fpe_state.vertexpointer_array;
    auto& vpa = g_glstate.fpe_state.normalized_vpa;
    vpa = raw_vpa.normalize();
    vpa.generate_compressed_index(g_glstate.fpe_state.fpe_draw.current_data.sizes.data);
    // kinda cursed...
    raw_vpa.generate_compressed_index(g_glstate.fpe_state.fpe_draw.current_data.sizes.data);
    //    g_glFuncs.glGenVertexArrays(1, &vpa.fpe_vao);
    // LOG_D("fpe_vao: %d", g_glstate.fpe_state.fpe_vao)
    g_glFuncs.glBindVertexArray(g_glstate.fpe_state.fpe_vao);

    auto key = g_glstate.program_hash();
    // LOG_D("%s: key=0x%x", __func__, key)
    auto& prog = g_glstate.get_or_generate_program(key);
    int prog_id = prog.get_program();
    if (prog_id <= 0) {
        vpa.reset();
        return -1;
    }
    g_glFuncs.glUseProgram(prog_id);

    // Ugh...Why binding vbo is required BEFORE calling VertexAttrib* functions?
    if (previous_array_buffer == 0) {
        g_glFuncs.glBindBuffer(GL_ARRAY_BUFFER, g_glstate.fpe_state.fpe_vbo);
    }

    // LOG_D("starting_ptr = %p", vpa.starting_pointer)
    // LOG_D("stride = %d", vpa.stride)

    const GLuint attribute_array_buffer = previous_array_buffer == 0
                                              ? g_glstate.fpe_state.fpe_vbo
                                              : static_cast<GLuint>(previous_array_buffer);
    g_glstate.send_vertex_attributes(vpa, attribute_array_buffer);
    vpa.dirty = false;

    int ret = 0;

    // Making sure it is a valid pointer rather than an offset into the buffer
    if (reinterpret_cast<uintptr_t>(vpa.starting_pointer) > static_cast<uintptr_t>(vpa.stride)) {
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
        //      g_glstate.fpe_state.fpe_vbo)

        const auto* draw_start = static_cast<const uint8_t*>(vpa.starting_pointer) + *first * vpa.stride;
        g_glFuncs.glBufferData(GL_ARRAY_BUFFER, *count * vpa.stride, draw_start, GL_DYNAMIC_DRAW);
        *first = 0;

    } else {
        // LOG_D("Using already bound VB")
    }

    if (*mode == GL_QUADS) {
        const GLsizei index_count = (*count / 4) * 6;
        const bool upload_indices = prepare_quad_indices(*count, static_cast<uint32_t>(*first));

        // LOG_D("glBufferData: size = %d, data = 0x%x -> GL_ELEMENT_ARRAY_BUFFER (%d)",
        //      g_glstate.fpe_state.fpe_ib.size() * sizeof(uint32_t), g_glstate.fpe_state.fpe_ib.data(),
        //      g_glstate.fpe_state.fpe_ibo)

        if (!g_glstate.fpe_state.fpe_ibo_bound) {
            g_glFuncs.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_glstate.fpe_state.fpe_ibo);
            g_glstate.fpe_state.fpe_ibo_bound = true;
        }

        if (upload_indices) {
            g_glFuncs.glBufferData(GL_ELEMENT_ARRAY_BUFFER, quad_index_size_bytes(), quad_index_data(),
                                   GL_DYNAMIC_DRAW);
        }

        *count = index_count;

        *mode = GL_TRIANGLES;
        ret = 1;
    }

    g_glstate.send_uniforms(prog);
    vpa.reset();
    //    vpa.starting_pointer = 0;
    //    vpa.stride = 0;
    return ret;
}
