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
#include <algorithm>

#define DEBUG 0

namespace {

struct logical_array_buffer_state_t {
    EGLContext context = EGL_NO_CONTEXT;
    GLuint binding = 0;
    bool known = false;
};

thread_local logical_array_buffer_state_t logicalArrayBufferState;

logical_array_buffer_state_t& getLogicalArrayBufferState() {
    // Reconciles against the calling entry's strict-resolve snapshot
    // (docs/context-model.md); no eglGetCurrentContext of its own.
    const EGLContext context = (EGLContext)glstate_t::cached_context();
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
    g_glstate_c.fpe_state.client_array_buffer_bindings[index] = getLogicalArrayBufferBinding();
}

} // namespace

void glBindBuffer(GLenum target, GLuint buffer) {
    if (!sfpewEnsureBackend() || g_glFuncs.glBindBuffer == nullptr) return;
    (void)g_glstate; // entry strict resolve; the array-buffer shadow reads the snapshot
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
    (void)g_glstate; // entry strict resolve; the array-buffer shadow reads the snapshot
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
    return g_glstate_c.fpe_state.client_array_buffer_bindings[index];
}

void glVertexPointer(GLint size, GLenum type, GLsizei stride, const void* pointer) {
    auto& gs = g_glstate;
    flushPendingImmediateDraws();
    // LOG_D("glVertexPointer, size = %d, type = %s, stride = %d, pointer = 0x%x", size, glEnumToString(type), stride,
    // pointer)
    auto& attr = gs.fpe_state.vertexpointer_array.attributes[vp2idx(GL_VERTEX_ARRAY)];
    attr.size = size;
    attr.usage = GL_VERTEX_ARRAY;
    attr.type = type;
    attr.normalized = GL_FALSE;
    attr.stride = stride;
    attr.pointer = pointer;
    rememberClientArrayBufferBinding(vp2idx(GL_VERTEX_ARRAY));
    //    attr.varying = true;
    gs.fpe_state.vertexpointer_array.dirty = true;
    gs.fpe_state.vertexpointer_array.buffer_based = true;
}

void glNormalPointer(GLenum type, GLsizei stride, const GLvoid* pointer) {
    auto& gs = g_glstate;
    flushPendingImmediateDraws();
    // LOG_D("glNormalPointer, type = %s, stride = %d, pointer = 0x%x", glEnumToString(type), stride, pointer)
    gs.fpe_state.vertexpointer_array.attributes[vp2idx(GL_NORMAL_ARRAY)] = {
        .size = 3,
        .usage = GL_NORMAL_ARRAY,
        .type = type,
        .normalized = static_cast<GLenum>((type == GL_FLOAT || type == GL_DOUBLE) ? GL_FALSE : GL_TRUE),
        .stride = stride,
        .pointer = pointer,
        //            .varying = true
    };
    rememberClientArrayBufferBinding(vp2idx(GL_NORMAL_ARRAY));
    gs.fpe_state.vertexpointer_array.dirty = true;
}

// Remaining GL 1.4/1.5 pointer trio. State is stored in the reserved
// vp slots (vp2idx already maps them); shader-side consumption arrives
// with plans/04 (secondary color), plans/05 (fog coord) and plans/08
// (edge flags for PolygonMode).
void glEdgeFlagPointer(GLsizei stride, const GLvoid* pointer) {
    auto& gs = g_glstate;
    flushPendingImmediateDraws();
    gs.fpe_state.vertexpointer_array.attributes[vp2idx(GL_EDGE_FLAG_ARRAY)] = {
        .size = 1,
        .usage = GL_EDGE_FLAG_ARRAY,
        .type = GL_UNSIGNED_BYTE, // GLboolean elements
        .normalized = GL_FALSE,
        .stride = stride,
        .pointer = pointer,
    };
    rememberClientArrayBufferBinding(vp2idx(GL_EDGE_FLAG_ARRAY));
    gs.fpe_state.vertexpointer_array.dirty = true;
}

void glSecondaryColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid* pointer) {
    auto& gs = g_glstate;
    if (size != 3) { // GL 2.1: secondary color arrays are strictly 3-component
        gs.set_error(GL_INVALID_VALUE);
        return;
    }
    flushPendingImmediateDraws();
    gs.fpe_state.vertexpointer_array.attributes[vp2idx(GL_SECONDARY_COLOR_ARRAY)] = {
        .size = size,
        .usage = GL_SECONDARY_COLOR_ARRAY,
        .type = type,
        .normalized = static_cast<GLenum>((type == GL_FLOAT || type == GL_DOUBLE) ? GL_FALSE : GL_TRUE),
        .stride = stride,
        .pointer = pointer,
    };
    rememberClientArrayBufferBinding(vp2idx(GL_SECONDARY_COLOR_ARRAY));
    gs.fpe_state.vertexpointer_array.dirty = true;
}

void glFogCoordPointer(GLenum type, GLsizei stride, const GLvoid* pointer) {
    auto& gs = g_glstate;
    if (type != GL_FLOAT && type != GL_DOUBLE) {
        gs.set_error(GL_INVALID_ENUM);
        return;
    }
    flushPendingImmediateDraws();
    gs.fpe_state.vertexpointer_array.attributes[vp2idx(GL_FOG_COORD_ARRAY)] = {
        .size = 1,
        .usage = GL_FOG_COORD_ARRAY,
        .type = type,
        .normalized = GL_FALSE,
        .stride = stride,
        .pointer = pointer,
    };
    rememberClientArrayBufferBinding(vp2idx(GL_FOG_COORD_ARRAY));
    gs.fpe_state.vertexpointer_array.dirty = true;
}

void glColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid* pointer) {
    auto& gs = g_glstate;
    flushPendingImmediateDraws();
    // LOG_D("glColorPointer, size = %d, type = %s, stride = %d, pointer = 0x%x", size, glEnumToString(type), stride,
    // pointer)
    gs.fpe_state.vertexpointer_array.attributes[vp2idx(GL_COLOR_ARRAY)] = {
        .size = size,
        .usage = GL_COLOR_ARRAY,
        .type = type,
        .normalized = GL_TRUE,
        .stride = stride,
        .pointer = pointer,
        //            .varying = true
    };
    rememberClientArrayBufferBinding(vp2idx(GL_COLOR_ARRAY));
    gs.fpe_state.vertexpointer_array.dirty = true;
}

void glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid* pointer) {
    auto& gs = g_glstate;
    flushPendingImmediateDraws();
    // LOG_D("glTexCoordPointer, size = %d, type = %s, stride = %d, pointer = 0x%x", size, glEnumToString(type), stride,
    // pointer) LOG_D("Active texture: %s", glEnumToString(g_glstate.fpe_state.client_active_texture))
    const int index = vp2idx(GL_TEXTURE_COORD_ARRAY);
    gs.fpe_state.vertexpointer_array.attributes[index] = {
        .size = size,
        .usage = GL_TEXTURE_COORD_ARRAY + (gs.fpe_state.client_active_texture - GL_TEXTURE0),
        .type = type,
        .normalized = GL_FALSE,
        .stride = stride,
        .pointer = pointer,
        //            .varying = true
    };
    rememberClientArrayBufferBinding(index);
    gs.fpe_state.vertexpointer_array.dirty = true;
}

void glIndexPointer(GLenum type, GLsizei stride, const GLvoid* pointer) {
    auto& gs = g_glstate;
    flushPendingImmediateDraws();
    // LOG_D("glIndexPointer, size = %d, type = %s, stride = %d, pointer = 0x%x", glEnumToString(type), stride, pointer)
    gs.fpe_state.vertexpointer_array.attributes[vp2idx(GL_INDEX_ARRAY)] = {
        .size = 1,
        .usage = GL_INDEX_ARRAY,
        .type = type,
        .normalized = GL_FALSE,
        .stride = stride,
        .pointer = pointer,
        //            .varying = true
    };
    rememberClientArrayBufferBinding(vp2idx(GL_INDEX_ARRAY));
    gs.fpe_state.vertexpointer_array.dirty = true;
}

void glEnableClientState(GLenum cap) {
    auto& gs = g_glstate;
    flushPendingImmediateDraws();
    // LOG_D("glEnableClientState, cap = %s", glEnumToString(cap))

    auto mask = vp_mask(cap);
    gs.fpe_state.vertexpointer_array.enabled_pointers |= mask;
    // LOG_D("Enabled Ptr: 0x%x", g_glstate.fpe_state.vertexpointer_array.enabled_pointers)
    gs.fpe_state.vertexpointer_array.dirty = true;
}

void glDisableClientState(GLenum cap) {
    auto& gs = g_glstate;
    flushPendingImmediateDraws();
    // LOG_D("glDisableClientState, cap = %s", glEnumToString(cap))
    auto mask = vp_mask(cap);

    gs.fpe_state.vertexpointer_array.enabled_pointers &= (~mask);
    // LOG_D("Enabled Ptr: 0x%x", g_glstate.fpe_state.vertexpointer_array.enabled_pointers)
    gs.fpe_state.vertexpointer_array.dirty = true;
}

// glInterleavedArrays: a GL 1.1 shortcut that declares up to four client
// arrays inside one interleaved block (spec table 2.5). Implemented on top
// of the public pointer/enable entry points so flushing, shadowing and
// future recording behave exactly as if the caller made those calls.
void glInterleavedArrays(GLenum format, GLsizei stride, const void* pointer) {
    auto& gs = g_glstate;
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
        gs.set_error(GL_INVALID_ENUM);
        return;
    }
    if (stride < 0) {
        gs.set_error(GL_INVALID_VALUE);
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

namespace {

// Reads component `c` of element `i` from a client array declaration.
GLfloat clientArrayComponent(const vertexattribute_t& a, GLint i, GLint c, bool normalized) {
    const GLsizei tsize = type_size(a.type);
    const GLsizei stride = a.stride != 0 ? a.stride : a.size * tsize;
    const uint8_t* p = static_cast<const uint8_t*>(a.pointer) + (size_t)i * stride + (size_t)c * tsize;
    switch (a.type) {
    case GL_BYTE: {
        auto v = *reinterpret_cast<const GLbyte*>(p);
        return normalized ? std::max((GLfloat)v / 127.0f, -1.0f) : (GLfloat)v;
    }
    case GL_UNSIGNED_BYTE: {
        auto v = *reinterpret_cast<const GLubyte*>(p);
        return normalized ? (GLfloat)v / 255.0f : (GLfloat)v;
    }
    case GL_SHORT: {
        auto v = *reinterpret_cast<const GLshort*>(p);
        return normalized ? std::max((GLfloat)v / 32767.0f, -1.0f) : (GLfloat)v;
    }
    case GL_UNSIGNED_SHORT: {
        auto v = *reinterpret_cast<const GLushort*>(p);
        return normalized ? (GLfloat)v / 65535.0f : (GLfloat)v;
    }
    case GL_INT: {
        auto v = *reinterpret_cast<const GLint*>(p);
        return normalized ? std::max((GLfloat)((GLdouble)v / 2147483647.0), -1.0f) : (GLfloat)v;
    }
    case GL_UNSIGNED_INT: {
        auto v = *reinterpret_cast<const GLuint*>(p);
        return normalized ? (GLfloat)((GLdouble)v / 4294967295.0) : (GLfloat)v;
    }
    case GL_DOUBLE:
        return (GLfloat)*reinterpret_cast<const GLdouble*>(p);
    case GL_FLOAT:
    default:
        return *reinterpret_cast<const GLfloat*>(p);
    }
}

} // namespace

// glArrayElement: feed element i of every enabled client array through the
// immediate-mode current-value path (vertex last: it commits the vertex).
// Arrays living in VBOs cannot be read from the CPU here; those elements
// are skipped (logged once per call site would be noise - manifest notes it).
void glArrayElement(GLint i) {
    auto& gs = g_glstate;
    if (i < 0) {
        gs.set_error(GL_INVALID_VALUE);
        return;
    }
    const auto& va = gs.fpe_state.vertexpointer_array;
    const auto enabled = [&](GLenum array) { return (va.enabled_pointers & vp_mask(array)) != 0; };
    const auto client_side = [&](int idx) { return getClientArrayBufferBinding(idx) == 0; };

    if (enabled(GL_NORMAL_ARRAY) && client_side(vp2idx(GL_NORMAL_ARRAY))) {
        const auto& a = va.attributes[vp2idx(GL_NORMAL_ARRAY)];
        if (a.pointer)
            mglNormal<GLfloat, 3>({clientArrayComponent(a, i, 0, true), clientArrayComponent(a, i, 1, true),
                                   clientArrayComponent(a, i, 2, true)});
    }
    if (enabled(GL_COLOR_ARRAY) && client_side(vp2idx(GL_COLOR_ARRAY))) {
        const auto& a = va.attributes[vp2idx(GL_COLOR_ARRAY)];
        if (a.pointer) {
            if (a.size == 3)
                mglColor<GLfloat, 3>({clientArrayComponent(a, i, 0, true), clientArrayComponent(a, i, 1, true),
                                      clientArrayComponent(a, i, 2, true)});
            else
                mglColor<GLfloat, 4>({clientArrayComponent(a, i, 0, true), clientArrayComponent(a, i, 1, true),
                                      clientArrayComponent(a, i, 2, true), clientArrayComponent(a, i, 3, true)});
        }
    }
    for (int unit = 0; unit < MAX_TEX; ++unit) {
        const int idx = 7 + unit;
        if (!((va.enabled_pointers >> idx) & 1u) || !client_side(idx)) continue;
        const auto& a = va.attributes[idx];
        if (!a.pointer) continue;
        GLfloat uv[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        for (GLint c = 0; c < a.size && c < 4; ++c) uv[c] = clientArrayComponent(a, i, c, false);
        mglTexCoord<GLfloat, 4>({uv[0], uv[1], uv[2], uv[3]}, unit);
    }
    if (enabled(GL_VERTEX_ARRAY) && client_side(vp2idx(GL_VERTEX_ARRAY))) {
        const auto& a = va.attributes[vp2idx(GL_VERTEX_ARRAY)];
        if (a.pointer) {
            GLfloat v[4] = {0.0f, 0.0f, 0.0f, 1.0f};
            for (GLint c = 0; c < a.size && c < 4; ++c) v[c] = clientArrayComponent(a, i, c, false);
            mglVertex<GLfloat, 4>({v[0], v[1], v[2], v[3]});
        }
    }
}

bool sfpewUnpackPboBound() {
    if (!sfpewEnsureBackend() || g_glFuncs.glGetIntegerv == nullptr) return false;
    GLint binding = 0;
    g_glFuncs.glGetIntegerv(0x88EF /* GL_PIXEL_UNPACK_BUFFER_BINDING */, &binding);
    return binding != 0;
}

bool sfpewPackPboBound() {
    if (!sfpewEnsureBackend() || g_glFuncs.glGetIntegerv == nullptr) return false;
    GLint binding = 0;
    g_glFuncs.glGetIntegerv(0x88ED /* GL_PIXEL_PACK_BUFFER_BINDING */, &binding);
    return binding != 0;
}
