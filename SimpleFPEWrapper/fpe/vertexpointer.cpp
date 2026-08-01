// SimpleFPEWrapper - SimpleFPEWrapper/fpe/vertexpointer.cpp
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

#include "vertexpointer.h"
#include "../log.h"
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

    // Callers that bypass the glBindBuffer wrapper (JNI direct dispatch, a
    // layered wrapper) would otherwise desynchronize this shadow for the rest
    // of the process: unlike the program and VAO shadows it had no heal at all,
    // and once seeded it answered from itself forever. Re-read the truth every
    // 256 queries, the same self-healing reconciliation those two use
    // (sfpewLogicalProgram, plans/07) - one glGetIntegerv per ~256 queries.
    thread_local unsigned reconcile_counter = 0;
    const bool reconcile = (++reconcile_counter & 0xFFu) == 0u;

    // While a fixed-function draw holds the app's state the backend has the
    // WRAPPER's buffer bound, so asking it would poison the shadow with the
    // ring buffer as if the app had bound it (plans/12). The save on the
    // context is the app's truth here; the reconcile waits for the next
    // unheld query.
    const bool held = g_glstate_c.deferred_draw.held;
    if (state.known && !(reconcile && !held)) return state.binding;

    if (held) {
        state.binding = static_cast<GLuint>(g_glstate_c.deferred_draw.array_buffer);
        state.known = true;
        return state.binding;
    }

    if (!sfpewEnsureBackend() || g_glFuncs.glGetIntegerv == nullptr) {
        return state.known ? state.binding : 0;
    }
    GLint binding = 0;
    g_glFuncs.glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &binding);
    // Backend readbacks can catch one of the wrapper's own buffers (rings,
    // quad-index storage, the display-list arena): with the deferred restore
    // those legitimately stay bound between fixed-function draws. Adopting
    // one would make the guard restore the wrapper's buffer as if the app had
    // bound it, and later draws would source attributes from it (mirror of
    // the internal_programs rule in sfpewLogicalProgram; on-device forensics
    // caught "arraybuffer shadow heal 0 -> 4" with 4 being our arena).
    if (binding != 0 &&
        g_glstate_c.internal_buffers.count(static_cast<unsigned>(binding)) != 0) {
        binding = 0;
    }
    state.binding = static_cast<GLuint>(binding);
    state.known = true;
    return state.binding;
}

GLuint getLogicalVertexArrayBinding() {
    // The shadow itself lives on glstate_t next to the element-buffer one
    // (docs/context-model.md); the draw guard heals both every 256 draws, so
    // this only has to answer from it, or seed it once.
    auto& gs = g_glstate_c;
    // Held: backend_vao_binding is the wrapper's fpe_vao, not the app's.
    if (gs.deferred_draw.held) return static_cast<GLuint>(gs.deferred_draw.vertex_array);
    if (gs.backend_vao_known) return static_cast<GLuint>(gs.backend_vao_binding);

    if (!sfpewEnsureBackend() || g_glFuncs.glGetIntegerv == nullptr) return 0;
    GLint binding = 0;
    g_glFuncs.glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &binding);
    gs.backend_vao_binding = binding;
    gs.backend_vao_known = true;
    return static_cast<GLuint>(binding);
}

void rememberClientArrayBufferBinding(int index) {
    if (index < 0 || index >= VERTEX_POINTER_COUNT) return;
    g_glstate_c.fpe_state.client_array_buffer_bindings[index] = getLogicalArrayBufferBinding();
}

} // namespace

void glBindBuffer(GLenum target, GLuint buffer) {
    if (!sfpewEnsureBackend() || g_glFuncs.glBindBuffer == nullptr) return;
    (void)g_glstate; // entry strict resolve; the array-buffer shadow reads the snapshot
    if (target == GL_ARRAY_BUFFER) {
        // GL_ARRAY_BUFFER is global (not VAO) state, so the bind can land
        // while a fixed-function draw still holds the app's draw state: the
        // held snapshot is updated so the eventual restore reproduces THIS
        // binding, and the immediate arm forgets its buffer so the next FPE
        // draw re-binds its own source. Handing the whole trio back here made
        // every bind-pointers-draw sequence (the MC chunk VBO shape) pay a
        // glUseProgram(0) -> glUseProgram(fpe) round trip per draw.
        flushPendingImmediateDraws();
        g_glFuncs.glBindBuffer(GL_ARRAY_BUFFER, buffer);
        auto& gs = g_glstate_c;
        // The arm tracks the backend ARRAY binding the wrapper knows about;
        // this bind just went straight through, so record the exact value.
        // The next FPE draw whose attribute source IS this buffer (the MC
        // chunk shape: bind, respecify pointers, draw) then skips its
        // re-bind entirely instead of re-issuing it for the driver to dedup.
        gs.immediate_live_buffer = buffer;
        if (gs.deferred_draw.held) {
            gs.deferred_draw.array_buffer = static_cast<GLint>(buffer);
        }
        auto& state = getLogicalArrayBufferState();
        state.binding = buffer;
        state.known = true;
        return;
    }
    sfpewEntryBarrier();
    g_glFuncs.glBindBuffer(target, buffer);
    if (target == GL_ELEMENT_ARRAY_BUFFER) {
        // Element bindings are VAO state; app calls run with the app's VAO
        // bound (the draw guard restored it). Track VAO 0's binding for the
        // guard; with an unknown or non-zero backend VAO, force a re-query.
        auto& gs = g_glstate_c;
        if (gs.backend_vao_known && gs.backend_vao_binding == 0) {
            gs.backend_vao0_element_binding = static_cast<GLint>(buffer);
            gs.backend_vao0_element_known = true;
        } else {
            gs.backend_vao0_element_known = false;
        }
    }
}

void glDeleteBuffers(GLsizei n, const GLuint* buffers) {
    if (!sfpewEnsureBackend() || g_glFuncs.glDeleteBuffers == nullptr) return;
    (void)g_glstate; // entry strict resolve; the array-buffer shadow reads the snapshot
    sfpewEntryBarrier();
    g_glFuncs.glDeleteBuffers(n, buffers);
    if (n <= 0 || buffers == nullptr) return;

    // A deleted name's "wrapper-internal" identity dies with it, whoever
    // deleted it - the pool will recycle the name (see
    // sfpewForgetInternalBuffer). Wrapper-side deletions call the helper at
    // their own sites; this covers names the app deletes.
    for (GLsizei i = 0; i < n; ++i) sfpewForgetInternalBuffer(buffers[i]);

    // Deleting the buffer the arm remembers un-binds it driver-side, and the
    // recycled NAME can come back as a different object: a later arm match
    // against the reused name would skip a bind the backend actually needs.
    for (GLsizei i = 0; i < n; ++i) {
        if (g_glstate_c.immediate_live_buffer == buffers[i]) {
            g_glstate_c.immediate_live_buffer = 0;
            break;
        }
    }

    auto& state = getLogicalArrayBufferState();
    auto& gs = g_glstate_c;
    for (GLsizei i = 0; i < n; ++i) {
        if (state.known && buffers[i] == state.binding) state.binding = 0;
        // Deleting the shadowed element binding: let the next draw guard
        // re-query instead of guessing what the driver unbound.
        if (gs.backend_vao0_element_known &&
            static_cast<GLint>(buffers[i]) == gs.backend_vao0_element_binding) {
            gs.backend_vao0_element_known = false;
        }
    }
}

GLuint sfpewLogicalArrayBufferBinding() { return getLogicalArrayBufferBinding(); }

// Test-only handles on the internal-buffer registry. Driver name-recycling
// policy is not controllable from a test, so smoke_buffer_name_reuse marks a
// buffer internal directly to prove the poisoning is real, then deletes it
// and asserts the identity is shed (the fix for the MC 1.12 "Use VBOs"
// random vertex corruption).
SFPEW_APIENTRY void sfpewMarkBufferInternalForTest(GLuint buffer) {
    g_glstate_c.internal_buffers.insert(buffer);
}
SFPEW_APIENTRY bool sfpewBufferIsInternalForTest(GLuint buffer) {
    return g_glstate_c.internal_buffers.count(buffer) != 0;
}
SFPEW_APIENTRY GLuint sfpewLogicalArrayBufferBindingForTest(void) {
    return getLogicalArrayBufferBinding();
}

// --- Buffer and vertex-attribute surface (plans/12) ---------------------
//
// These are plain pass-throughs today. They exist because the wrapper must
// SEE them: each one reads or writes through the bound GL_ARRAY_BUFFER or the
// bound VAO, and unwrapped they fall through to the backend's own pointer
// where the wrapper cannot observe them at all. That leaves the wrapper
// shadowing the array-buffer binding while blind to writes through it, and it
// is the prerequisite for deferring the fixed-function draw-state restore
// (which would otherwise let an app's glBufferData land in the wrapper's
// immediate ring buffer).
//
// Each also orders against the pending glyph batch, like every other entry
// point that can change what a queued draw would observe.

void glBufferData(GLenum target, GLsizeiptr size, const void* data, GLenum usage) {
    if (!sfpewEnsureBackend() || g_glFuncs.glBufferData == nullptr) return;
    (void)g_glstate; // entry strict resolve
    sfpewEntryBarrier();
    g_glFuncs.glBufferData(target, size, data, usage);
}

void glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const void* data) {
    if (!sfpewEnsureBackend() || g_glFuncs.glBufferSubData == nullptr) return;
    (void)g_glstate;
    sfpewEntryBarrier();
    g_glFuncs.glBufferSubData(target, offset, size, data);
}

void* glMapBufferRange(GLenum target, GLintptr offset, GLsizeiptr length, GLbitfield access) {
    if (!sfpewEnsureBackend() || g_glFuncs.glMapBufferRange == nullptr) return nullptr;
    (void)g_glstate;
    sfpewEntryBarrier();
    return g_glFuncs.glMapBufferRange(target, offset, length, access);
}

GLboolean glUnmapBuffer(GLenum target) {
    if (!sfpewEnsureBackend() || g_glFuncs.glUnmapBuffer == nullptr) return GL_FALSE;
    (void)g_glstate;
    sfpewEntryBarrier();
    return g_glFuncs.glUnmapBuffer(target);
}

void glGetBufferParameteriv(GLenum target, GLenum pname, GLint* params) {
    if (!sfpewEnsureBackend() || g_glFuncs.glGetBufferParameteriv == nullptr) return;
    (void)g_glstate;
    sfpewEntryBarrier();
    g_glFuncs.glGetBufferParameteriv(target, pname, params);
}

namespace {
// GL 3.2 / ARB_vertex_array_bgra: size may be GL_BGRA, which means four
// components in B, G, R, A order. The spec allows it only with an unsigned
// byte or packed 10/10/10/2 type, and the values are always normalized.
bool isBgraArraySize(GLint size, GLenum type) {
    if (size != (GLint)GL_BGRA) return false;
    return type == GL_UNSIGNED_BYTE || type == GL_UNSIGNED_INT_2_10_10_10_REV ||
           type == GL_INT_2_10_10_10_REV;
}
} // namespace

void glVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized,
                           GLsizei stride, const void* pointer) {
    if (!sfpewEnsureBackend() || g_glFuncs.glVertexAttribPointer == nullptr) return;
    (void)g_glstate;
    sfpewEntryBarrier();
    // GL_BGRA reaches a USER program's attribute, whose shader the wrapper
    // does not get to rewrite - so unlike the fixed-function colour array
    // (glColorPointer) there is nowhere to put the component reordering.
    // Desktop GL performs it in the driver; on GLES the array is declared as
    // four plain components so the draw still happens, with the application's
    // own shader seeing them in its own order. Said once, because a renderer
    // that does this does it every frame.
    if (isBgraArraySize(size, type) && !sfpewBackendTakesBgra()) {
        static bool warned = false;
        if (!warned) {
            warned = true;
            SFPEW_LOGW("glVertexAttribPointer(index %u): GL_BGRA has no GLES equivalent and the "
                       "component order cannot be applied to an application's own shader; "
                       "declaring four components instead", index);
        }
        size = 4;
        normalized = GL_TRUE;
    }
    g_glFuncs.glVertexAttribPointer(index, size, type, normalized, stride, pointer);
}

void glVertexAttribIPointer(GLuint index, GLint size, GLenum type, GLsizei stride,
                            const void* pointer) {
    if (!sfpewEnsureBackend() || g_glFuncs.glVertexAttribIPointer == nullptr) return;
    (void)g_glstate;
    sfpewEntryBarrier();
    g_glFuncs.glVertexAttribIPointer(index, size, type, stride, pointer);
}

void glEnableVertexAttribArray(GLuint index) {
    if (!sfpewEnsureBackend() || g_glFuncs.glEnableVertexAttribArray == nullptr) return;
    (void)g_glstate;
    sfpewEntryBarrier();
    g_glFuncs.glEnableVertexAttribArray(index);
}

void glDisableVertexAttribArray(GLuint index) {
    if (!sfpewEnsureBackend() || g_glFuncs.glDisableVertexAttribArray == nullptr) return;
    (void)g_glstate;
    sfpewEntryBarrier();
    g_glFuncs.glDisableVertexAttribArray(index);
}

GLuint sfpewLogicalVertexArrayBinding() { return getLogicalVertexArrayBinding(); }

// Wrapped only to keep the shadow above current; the call itself is a
// straight pass-through. Without this the shadow would only converge on the
// draw guard's every-256-draws heal, so an app that binds its own VAO would
// have that many draws restore the wrong one.
void glBindVertexArray(GLuint array) {
    if (!sfpewEnsureBackend() || g_glFuncs.glBindVertexArray == nullptr) return;
    (void)g_glstate; // entry strict resolve; the shadows read the snapshot
    sfpewEntryBarrier();
    sfpewBackendBindVertexArray(array); // keeps the shadow and clears the fast path
    auto& gs = g_glstate_c;
    // VAO 0's element binding is only tracked while VAO 0 is the bound one;
    // any other VAO carries its own, so the cached value stops applying.
    if (array != 0) gs.backend_vao0_element_known = false;
}

void glDeleteVertexArrays(GLsizei n, const GLuint* arrays) {
    if (!sfpewEnsureBackend() || g_glFuncs.glDeleteVertexArrays == nullptr) return;
    (void)g_glstate; // entry strict resolve; the shadows read the snapshot
    sfpewEntryBarrier();
    g_glFuncs.glDeleteVertexArrays(n, arrays);
    if (n <= 0 || arrays == nullptr) return;

    // Deleting the bound VAO reverts the binding to zero, which brings VAO
    // 0's element binding back into scope - unknown until something re-reads.
    auto& gs = g_glstate_c;
    if (!gs.backend_vao_known) return;
    for (GLsizei i = 0; i < n; ++i) {
        if (static_cast<GLint>(arrays[i]) == gs.backend_vao_binding) {
            gs.backend_vao_binding = 0;
            gs.backend_vao0_element_known = false;
            break;
        }
    }
}

GLuint getClientArrayBufferBinding(int index) {
    if (index < 0 || index >= VERTEX_POINTER_COUNT) return 0;
    return g_glstate_c.fpe_state.client_array_buffer_bindings[index];
}


namespace {
// True when the attribute already holds exactly these values. MC's chunk
// loop respecifies identical pointers for every chunk with only the bound
// buffer changing; an identical respec must NOT mark the layout dirty, or
// every chunk pays a full normalize + program-key relayout for a
// declaration that did not change. The buffer binding is remembered
// separately (rememberClientArrayBufferBinding) either way.
bool samePointerSpec(const vertexattribute_t& a, GLint size, GLenum usage, GLenum type,
                     GLenum normalized, GLsizei stride, const void* pointer, bool bgra = false) {
    return a.size == size && a.usage == usage && a.type == type && a.normalized == normalized &&
           a.stride == stride && a.pointer == pointer && a.bgra == bgra;
}
} // namespace

void glVertexPointer(GLint size, GLenum type, GLsizei stride, const void* pointer) {
    auto& gs = g_glstate;
    sfpewClientStateBarrier();
    // LOG_D("glVertexPointer, size = %d, type = %s, stride = %d, pointer = 0x%x", size, glEnumToString(type), stride,
    // pointer)
    auto& attr = gs.fpe_state.vertexpointer_array.attributes[vp2idx(GL_VERTEX_ARRAY)];
    if (samePointerSpec(attr, size, GL_VERTEX_ARRAY, type, GL_FALSE, stride, pointer)) {
        rememberClientArrayBufferBinding(vp2idx(GL_VERTEX_ARRAY));
        gs.fpe_state.vertexpointer_array.buffer_based = true;
        return;
    }
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
    sfpewClientStateBarrier();
    // LOG_D("glNormalPointer, type = %s, stride = %d, pointer = 0x%x", glEnumToString(type), stride, pointer)
    const auto& cur = gs.fpe_state.vertexpointer_array.attributes[vp2idx(GL_NORMAL_ARRAY)];
    if (samePointerSpec(cur, 3, GL_NORMAL_ARRAY, type, static_cast<GLenum>((type == GL_FLOAT || type == GL_DOUBLE) ? GL_FALSE : GL_TRUE), stride, pointer)) {
        rememberClientArrayBufferBinding(vp2idx(GL_NORMAL_ARRAY));
        return;
    }
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
    sfpewClientStateBarrier();
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
    sfpewClientStateBarrier();
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
    sfpewClientStateBarrier();
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
    sfpewClientStateBarrier();
    // LOG_D("glColorPointer, size = %d, type = %s, stride = %d, pointer = 0x%x", size, glEnumToString(type), stride,
    // pointer)
    // A GL_BGRA array is four components with a component ORDER, not a
    // component count: everything downstream works with size 4 and the order
    // is applied at the two places that can express it (see vertexattribute_t).
    const bool bgra = isBgraArraySize(size, type);
    if (bgra) size = 4;
    const auto& cur = gs.fpe_state.vertexpointer_array.attributes[vp2idx(GL_COLOR_ARRAY)];
    if (samePointerSpec(cur, size, GL_COLOR_ARRAY, type, GL_TRUE, stride, pointer, bgra)) {
        rememberClientArrayBufferBinding(vp2idx(GL_COLOR_ARRAY));
        return;
    }
    gs.fpe_state.vertexpointer_array.attributes[vp2idx(GL_COLOR_ARRAY)] = {
        .size = size,
        .usage = GL_COLOR_ARRAY,
        .type = type,
        .normalized = GL_TRUE,
        .stride = stride,
        .pointer = pointer,
        .bgra = bgra,
        //            .varying = true
    };
    rememberClientArrayBufferBinding(vp2idx(GL_COLOR_ARRAY));
    gs.fpe_state.vertexpointer_array.dirty = true;
}

void glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid* pointer) {
    auto& gs = g_glstate;
    sfpewClientStateBarrier();
    // LOG_D("glTexCoordPointer, size = %d, type = %s, stride = %d, pointer = 0x%x", size, glEnumToString(type), stride,
    // pointer) LOG_D("Active texture: %s", glEnumToString(g_glstate.fpe_state.client_active_texture))
    const int index = vp2idx(GL_TEXTURE_COORD_ARRAY);
    const auto& cur = gs.fpe_state.vertexpointer_array.attributes[index];
    if (samePointerSpec(cur, size, GL_TEXTURE_COORD_ARRAY + (gs.fpe_state.client_active_texture - GL_TEXTURE0), type, GL_FALSE, stride, pointer)) {
        rememberClientArrayBufferBinding(index);
        return;
    }
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
    sfpewClientStateBarrier();
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
    sfpewClientStateBarrier();
    // LOG_D("glEnableClientState, cap = %s", glEnumToString(cap))

    auto mask = vp_mask(cap);
    gs.fpe_state.vertexpointer_array.enabled_pointers |= mask;
    // LOG_D("Enabled Ptr: 0x%x", g_glstate.fpe_state.vertexpointer_array.enabled_pointers)
    gs.fpe_state.vertexpointer_array.dirty = true;
}

void glDisableClientState(GLenum cap) {
    auto& gs = g_glstate;
    sfpewClientStateBarrier();
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
