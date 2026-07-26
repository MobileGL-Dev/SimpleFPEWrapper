// SimpleFPEWrapper - SimpleFPEWrapper/fpe/vertexpointer.cpp
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

#include "vertexpointer.h"
#include "fpe.hpp"
#include "drawing1x.h"

#define DEBUG 0

namespace {

GLuint clientArrayBufferBindings[VERTEX_POINTER_COUNT] = {};
struct logical_array_buffer_state_t {
    EGLContext context = EGL_NO_CONTEXT;
    GLuint binding = 0;
    bool known = false;
};

thread_local logical_array_buffer_state_t logicalArrayBufferState;

logical_array_buffer_state_t& getLogicalArrayBufferState() {
    const EGLContext context =
        g_eglFuncs.eglGetCurrentContext ? g_eglFuncs.eglGetCurrentContext() : EGL_NO_CONTEXT;
    if (logicalArrayBufferState.context != context) {
        logicalArrayBufferState = {};
        logicalArrayBufferState.context = context;
    }
    return logicalArrayBufferState;
}

GLuint getLogicalArrayBufferBinding() {
    auto& state = getLogicalArrayBufferState();
    if (state.known) return state.binding;

    if (!sfpewEnsureBackend() || g_glFuncs.glGetIntegerv == nullptr) return 0;
    GLint binding = 0;
    g_glFuncs.glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &binding);
    state.binding = static_cast<GLuint>(binding);
    state.known = true;
    return state.binding;
}

void rememberClientArrayBufferBinding(int index) {
    if (index < 0 || index >= VERTEX_POINTER_COUNT) return;
    clientArrayBufferBindings[index] = getLogicalArrayBufferBinding();
}

} // namespace

void glBindBuffer(GLenum target, GLuint buffer) {
    if (!sfpewEnsureBackend() || g_glFuncs.glBindBuffer == nullptr) return;
    flushPendingImmediateDraws();
    g_glFuncs.glBindBuffer(target, buffer);
    if (target == GL_ARRAY_BUFFER) {
        auto& state = getLogicalArrayBufferState();
        state.binding = buffer;
        state.known = true;
    }
}

void glDeleteBuffers(GLsizei n, const GLuint* buffers) {
    if (!sfpewEnsureBackend() || g_glFuncs.glDeleteBuffers == nullptr) return;
    flushPendingImmediateDraws();
    g_glFuncs.glDeleteBuffers(n, buffers);
    if (n <= 0 || buffers == nullptr) return;

    auto& state = getLogicalArrayBufferState();
    if (!state.known) return;
    for (GLsizei i = 0; i < n; ++i) {
        if (buffers[i] == state.binding) {
            state.binding = 0;
            break;
        }
    }
}

GLuint sfpewLogicalArrayBufferBinding() { return getLogicalArrayBufferBinding(); }

GLuint getClientArrayBufferBinding(int index) {
    if (index < 0 || index >= VERTEX_POINTER_COUNT) return 0;
    return clientArrayBufferBindings[index];
}

void glVertexPointer(GLint size, GLenum type, GLsizei stride, const void* pointer) {
    flushPendingImmediateDraws();
    // LOG_D("glVertexPointer, size = %d, type = %s, stride = %d, pointer = 0x%x", size, glEnumToString(type), stride,
    // pointer)
    auto& attr = g_glstate.fpe_state.vertexpointer_array.attributes[vp2idx(GL_VERTEX_ARRAY)];
    attr.size = size;
    attr.usage = GL_VERTEX_ARRAY;
    attr.type = type;
    attr.normalized = GL_FALSE;
    attr.stride = stride;
    attr.pointer = pointer;
    rememberClientArrayBufferBinding(vp2idx(GL_VERTEX_ARRAY));
    //    attr.varying = true;
    g_glstate.fpe_state.vertexpointer_array.dirty = true;
    g_glstate.fpe_state.vertexpointer_array.buffer_based = true;
}

void glNormalPointer(GLenum type, GLsizei stride, const GLvoid* pointer) {
    flushPendingImmediateDraws();
    // LOG_D("glNormalPointer, type = %s, stride = %d, pointer = 0x%x", glEnumToString(type), stride, pointer)
    g_glstate.fpe_state.vertexpointer_array.attributes[vp2idx(GL_NORMAL_ARRAY)] = {
        .size = 3,
        .usage = GL_NORMAL_ARRAY,
        .type = type,
        .normalized = static_cast<GLenum>((type == GL_FLOAT || type == GL_DOUBLE) ? GL_FALSE : GL_TRUE),
        .stride = stride,
        .pointer = pointer,
        //            .varying = true
    };
    rememberClientArrayBufferBinding(vp2idx(GL_NORMAL_ARRAY));
    g_glstate.fpe_state.vertexpointer_array.dirty = true;
}

void glColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid* pointer) {
    flushPendingImmediateDraws();
    // LOG_D("glColorPointer, size = %d, type = %s, stride = %d, pointer = 0x%x", size, glEnumToString(type), stride,
    // pointer)
    g_glstate.fpe_state.vertexpointer_array.attributes[vp2idx(GL_COLOR_ARRAY)] = {
        .size = size,
        .usage = GL_COLOR_ARRAY,
        .type = type,
        .normalized = GL_TRUE,
        .stride = stride,
        .pointer = pointer,
        //            .varying = true
    };
    rememberClientArrayBufferBinding(vp2idx(GL_COLOR_ARRAY));
    g_glstate.fpe_state.vertexpointer_array.dirty = true;
}

void glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid* pointer) {
    flushPendingImmediateDraws();
    // LOG_D("glTexCoordPointer, size = %d, type = %s, stride = %d, pointer = 0x%x", size, glEnumToString(type), stride,
    // pointer) LOG_D("Active texture: %s", glEnumToString(g_glstate.fpe_state.client_active_texture))
    const int index = vp2idx(GL_TEXTURE_COORD_ARRAY);
    g_glstate.fpe_state.vertexpointer_array.attributes[index] = {
        .size = size,
        .usage = GL_TEXTURE_COORD_ARRAY + (g_glstate.fpe_state.client_active_texture - GL_TEXTURE0),
        .type = type,
        .normalized = GL_FALSE,
        .stride = stride,
        .pointer = pointer,
        //            .varying = true
    };
    rememberClientArrayBufferBinding(index);
    g_glstate.fpe_state.vertexpointer_array.dirty = true;
}

void glIndexPointer(GLenum type, GLsizei stride, const GLvoid* pointer) {
    flushPendingImmediateDraws();
    // LOG_D("glIndexPointer, size = %d, type = %s, stride = %d, pointer = 0x%x", glEnumToString(type), stride, pointer)
    g_glstate.fpe_state.vertexpointer_array.attributes[vp2idx(GL_INDEX_ARRAY)] = {
        .size = 1,
        .usage = GL_INDEX_ARRAY,
        .type = type,
        .normalized = GL_FALSE,
        .stride = stride,
        .pointer = pointer,
        //            .varying = true
    };
    rememberClientArrayBufferBinding(vp2idx(GL_INDEX_ARRAY));
    g_glstate.fpe_state.vertexpointer_array.dirty = true;
}

void glEnableClientState(GLenum cap) {
    flushPendingImmediateDraws();
    // LOG_D("glEnableClientState, cap = %s", glEnumToString(cap))

    auto mask = vp_mask(cap);
    g_glstate.fpe_state.vertexpointer_array.enabled_pointers |= mask;
    // LOG_D("Enabled Ptr: 0x%x", g_glstate.fpe_state.vertexpointer_array.enabled_pointers)
    g_glstate.fpe_state.vertexpointer_array.dirty = true;
}

void glDisableClientState(GLenum cap) {
    flushPendingImmediateDraws();
    // LOG_D("glDisableClientState, cap = %s", glEnumToString(cap))
    auto mask = vp_mask(cap);

    g_glstate.fpe_state.vertexpointer_array.enabled_pointers &= (~mask);
    // LOG_D("Enabled Ptr: 0x%x", g_glstate.fpe_state.vertexpointer_array.enabled_pointers)
    g_glstate.fpe_state.vertexpointer_array.dirty = true;
}

// glInterleavedArrays: a GL 1.1 shortcut that declares up to four client
// arrays inside one interleaved block (spec table 2.5). Implemented on top
// of the public pointer/enable entry points so flushing, shadowing and
// future recording behave exactly as if the caller made those calls.
void glInterleavedArrays(GLenum format, GLsizei stride, const void* pointer) {
    struct layout_t {
        GLint tex, color, normal, vertex;    // component counts, 0 = absent
        GLenum color_type;
        size_t tex_off, color_off, normal_off, vertex_off, tight;
    };
    constexpr size_t F = sizeof(GLfloat);
    layout_t l{};
    switch (format) {
    case GL_V2F:              l = {0, 0, 0, 2, 0, 0, 0, 0, 0, 2 * F}; break;
    case GL_V3F:              l = {0, 0, 0, 3, 0, 0, 0, 0, 0, 3 * F}; break;
    case GL_C4UB_V2F:         l = {0, 4, 0, 2, GL_UNSIGNED_BYTE, 0, 0, 0, 4, 4 + 2 * F}; break;
    case GL_C4UB_V3F:         l = {0, 4, 0, 3, GL_UNSIGNED_BYTE, 0, 0, 0, 4, 4 + 3 * F}; break;
    case GL_C3F_V3F:          l = {0, 3, 0, 3, GL_FLOAT, 0, 0, 0, 3 * F, 6 * F}; break;
    case GL_N3F_V3F:          l = {0, 0, 3, 3, 0, 0, 0, 0, 3 * F, 6 * F}; l.normal_off = 0; break;
    case GL_C4F_N3F_V3F:      l = {0, 4, 3, 3, GL_FLOAT, 0, 0, 4 * F, 7 * F, 10 * F}; break;
    case GL_T2F_V3F:          l = {2, 0, 0, 3, 0, 0, 0, 0, 2 * F, 5 * F}; break;
    case GL_T4F_V4F:          l = {4, 0, 0, 4, 0, 0, 0, 0, 4 * F, 8 * F}; break;
    case GL_T2F_C4UB_V3F:     l = {2, 4, 0, 3, GL_UNSIGNED_BYTE, 0, 2 * F, 0, 2 * F + 4, 2 * F + 4 + 3 * F}; break;
    case GL_T2F_C3F_V3F:      l = {2, 3, 0, 3, GL_FLOAT, 0, 2 * F, 0, 5 * F, 8 * F}; break;
    case GL_T2F_N3F_V3F:      l = {2, 0, 3, 3, 0, 0, 0, 2 * F, 5 * F, 8 * F}; break;
    case GL_T2F_C4F_N3F_V3F:  l = {2, 4, 3, 3, GL_FLOAT, 0, 2 * F, 6 * F, 9 * F, 12 * F}; break;
    case GL_T4F_C4F_N3F_V4F:  l = {4, 4, 3, 4, GL_FLOAT, 0, 4 * F, 8 * F, 11 * F, 14 * F}; break;
    default:
        g_glstate.set_error(GL_INVALID_ENUM);
        return;
    }
    if (stride < 0) {
        g_glstate.set_error(GL_INVALID_VALUE);
        return;
    }
    const GLsizei effective_stride = stride != 0 ? stride : static_cast<GLsizei>(l.tight);
    const auto* base = static_cast<const uint8_t*>(pointer);

    // Per spec, these four client arrays are unconditionally disabled.
    glDisableClientState(GL_EDGE_FLAG_ARRAY);
    glDisableClientState(GL_INDEX_ARRAY);
    glDisableClientState(GL_SECONDARY_COLOR_ARRAY);
    glDisableClientState(GL_FOG_COORD_ARRAY);

    if (l.tex > 0) {
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glTexCoordPointer(l.tex, GL_FLOAT, effective_stride, base + l.tex_off);
    } else {
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    }
    if (l.color > 0) {
        glEnableClientState(GL_COLOR_ARRAY);
        glColorPointer(l.color, l.color_type, effective_stride, base + l.color_off);
    } else {
        glDisableClientState(GL_COLOR_ARRAY);
    }
    if (l.normal > 0) {
        glEnableClientState(GL_NORMAL_ARRAY);
        glNormalPointer(GL_FLOAT, effective_stride, base + l.normal_off);
    } else {
        glDisableClientState(GL_NORMAL_ARRAY);
    }
    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(l.vertex, GL_FLOAT, effective_stride, base + l.vertex_off);
}
