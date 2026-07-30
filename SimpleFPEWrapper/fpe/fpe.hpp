// SimpleFPEWrapper - SimpleFPEWrapper/fpe/fpe.hpp
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

#pragma once

#include <GL/gl.h>
#include "transformation.h"
#include "state.h"
#include "vertexpointer.h"
#include "../init.h"

void sfpewFlushDeferredDrawState();

#define GET_PREV_PROGRAM                                                                                               \
    GLint m_prev_program;                                                                                              \
    g_glFuncs.glGetIntegerv(GL_CURRENT_PROGRAM, &m_prev_program);
#define SET_PREV_PROGRAM                                                                                               \
    sfpewInvalidateImmediateDrawState();                                                                                \
    g_glFuncs.glUseProgram(m_prev_program);

// Strict resolve (one eglGetCurrentContext) - use for the FIRST context
// access of an exported entry point. Downstream code uses g_glstate_c.
#define g_glstate glstate_t::get_instance()
// Relaxed: thread-local snapshot refreshed by the entry's strict resolve.
#define g_glstate_c glstate_t::current()

int init_fpe();

// User-program compatibility uniforms (shader/userprogram.cpp, plans/09 9.3).
void sfpewFeedUserProgramUniforms(GLuint program);


// Replays a GL_COLOR_BUFFER_BIT / GL_ENABLE_BIT blend-state snapshot onto
// the backend (glPopAttrib). Only the differing calls are issued.
void restore_color_buffer(const color_buffer_state_t& current, const color_buffer_state_t& wanted);

// plans/09 S9 mixed pipeline: a bound USER program still consumes the
// fixed-function vertex arrays / immediate-mode vertices in GL 2.1. These
// resolve the program's fpe_* attribute locations (slot-indexed like
// vertex_pointer_array_t; false when the program has no fpe_Vertex) and
// submit the current arrays onto those locations inside fpe_user_vao.
bool sfpewUserProgramAttribLocations(GLuint program, GLint out_locations[VERTEX_POINTER_COUNT]);
bool gather_client_arrays(const vertex_pointer_array_t& raw, GLint first, GLsizei count,
                          vertex_pointer_array_t* out);
void sfpewSendUserProgramAttributes(const GLint locations[VERTEX_POINTER_COUNT],
                                    const vertex_pointer_array_t& va, GLintptr binding_offset);
// Full fixed-function-arrays draw through a user program. Returns true
// when the draw was submitted (or dropped on error); false = caller must
// fall back to the plain passthrough.
bool sfpewUserProgramFixedFunctionDrawArrays(GLuint program, GLenum mode, GLint first,
                                             GLsizei count);
void sfpewForgetUserProgram(GLuint program);

// Evaluator enables (fpe/evaluators.cpp, plans/10 10.2).
bool sfpewEvaluatorEnable(GLenum cap, bool enable);

// Evaluator enables (fpe/evaluators.cpp, plans/10 10.2).
bool sfpewEvaluatorEnable(GLenum cap, bool enable);

// Selection/feedback CPU pipeline (fpe/selection.cpp, plans/10 10.3).
void sfpewFlushSelectionHit();
void sfpewSelectionProcessVertices(GLenum mode, const GLfloat* positions, size_t stride_floats,
                                   GLint position_size, size_t vertex_count);
bool prepare_quad_indices(GLsizei count, GLuint first = 0);
const void* quad_index_data();
size_t quad_index_size_bytes();
GLenum quad_index_type();

// Route every wrapper-side backend VAO / element-array bind through these so
// the binding shadows on glstate_t stay exact (docs/context-model.md). All
// call sites run downstream of an entry's strict resolve.
inline void sfpewBackendBindVertexArray(GLuint vao) {
    g_glFuncs.glBindVertexArray(vao);
    auto& gs = g_glstate_c;
    gs.backend_vao_binding = static_cast<GLint>(vao);
    gs.backend_vao_known = true;
    // Any VAO change invalidates the immediate-draw fast path; the immediate
    // path re-arms it after its own binds. Putting the clear here means the
    // other FPE draw paths and the app's own glBindVertexArray get it for free.
    gs.immediate_live_program = -1;
    gs.immediate_live_buffer = 0;
}

// Anything that binds a program, VAO or array buffer other than the
// immediate-draw trio must call this, or the next immediate draw skips a bind
// it actually needed (plans/12).
// GL_QUAD_STRIP and GL_POLYGON have no GLES equivalent but ARE vertex-order
// compatible with a core mode, so converting them is a mode swap plus a
// count truncation. Both halves matter:
//
//   - Without the swap the raw legacy enum reaches the backend and the draw
//     dies with GL_INVALID_ENUM, rendering nothing.
//   - Without the truncation an INCOMPLETE trailing group starts drawing.
//     GL_QUAD_STRIP needs vertices in pairs and at least four of them; a
//     3-vertex strip must draw nothing, but GL_TRIANGLE_STRIP happily draws
//     a triangle from those same three vertices. (GL_POLYGON needs no
//     truncation: a fan of fewer than three vertices already draws nothing.)
//
// Leaves every other mode, GL_QUADS included, untouched - quads convert to
// indexed triangles separately, and that path does its own (count / 4) * 6
// truncation.
inline void sfpewConvertLegacyDrawMode(GLenum* mode, GLsizei* count) {
    if (*mode == GL_QUAD_STRIP) {
        *mode = GL_TRIANGLE_STRIP;
        *count = *count >= 4 ? (*count & ~GLsizei{1}) : 0;
    } else if (*mode == GL_POLYGON) {
        *mode = GL_TRIANGLE_FAN;
    }
}

inline void sfpewInvalidateImmediateDrawState() {
    g_glstate_c.immediate_live_program = -1;
    g_glstate_c.immediate_live_buffer = 0;
}

// Registers a buffer object the WRAPPER created for its own draw plumbing.
// The logical array-buffer shadow treats a backend readback that returns one
// of these as logical zero - it is leftover wrapper state, not the app's
// binding (mirror of internal_programs; see sfpewLogicalProgram).
inline void sfpewNoteInternalBuffer(GLuint buffer) {
    if (buffer != 0) g_glstate_c.internal_buffers.insert(buffer);
}

// MUST be called wherever the wrapper deletes one of its own buffers. A
// deleted name goes back to the GL name pool and the app's next glGenBuffers
// can hand it out again; a stale entry here would then make the array-buffer
// shadow treat the APP'S OWN VBO as wrapper-internal and report it as zero -
// fixed-function draws silently switch their attribute source to the ring
// and read garbage vertices. Seen in the wild within hours of b3dd356: MC
// 1.12 with "Use VBOs" recycles chunk VBO names constantly, so random chunks
// rendered with corrupted vertices.
inline void sfpewForgetInternalBuffer(GLuint buffer) {
    if (buffer != 0) g_glstate_c.internal_buffers.erase(buffer);
}

// Binds the array buffer a fixed-function draw will source attributes from,
// skipping the call when the backend provably has it already.
//
// The guard below hands the app's ARRAY_BUFFER binding to the context and then
// deliberately leaves the backend alone until sfpewEntryBarrier() restores it,
// so while a save is held and nothing has rebound since, deferred_draw
// .array_buffer IS the live backend binding. A fixed-function draw that sources
// straight from the app's own VBO therefore asks for the buffer that is already
// bound. Measured on RDC/Minecraft/1.16-Optifine/1-frame19661.rdc: one
// redundant bind per draw, 107 of the frame's 549 glBindBuffer calls.
//
// guard_holds_save must be the guard's holds_save: false means an earlier
// fixed-function draw is holding the save and the backend carries THAT draw's
// attribute buffer, which this function cannot know - so it always binds.
inline void sfpewBackendBindAttributeBuffer(GLuint buffer, bool guard_holds_save) {
    const auto& held = g_glstate_c.deferred_draw;
    if (guard_holds_save && held.held && held.array_buffer == static_cast<GLint>(buffer)) return;
    g_glFuncs.glBindBuffer(GL_ARRAY_BUFFER, buffer);
}

inline void sfpewBackendBindElementBuffer(GLuint buffer) {
    g_glFuncs.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffer);
    auto& gs = g_glstate_c;
    if (gs.backend_vao_known && gs.backend_vao_binding == 0) {
        gs.backend_vao0_element_binding = static_cast<GLint>(buffer);
        gs.backend_vao0_element_known = true;
    }
}

struct fpe_backend_draw_state_guard_t {
    GLint program = 0;
    GLint vertex_array = 0;
    GLint array_buffer = 0;
    // -1: not captured; the restored VAO carries its own element binding.
    GLint element_array_buffer = -1;
    // Without a backend (no context yet / loader failure) the guard must be
    // inert: display-list replay reaches this even backend-less.
    bool active = false;
    // True when THIS guard captured the save (rather than finding one already
    // held by an earlier fixed-function draw).
    bool holds_save = false;

    explicit fpe_backend_draw_state_guard_t(GLint known_program = -1,
                                            GLint known_array_buffer = -1) {
        if (g_glFuncs.glGetIntegerv == nullptr || g_glFuncs.glUseProgram == nullptr ||
            g_glFuncs.glBindVertexArray == nullptr || g_glFuncs.glBindBuffer == nullptr) {
            return;
        }
        active = true;

        // A previous fixed-function draw may still be holding the app's state.
        // The save is already made and the backend is already in OUR state, so
        // capturing again here would record the wrapper's own bindings as if
        // they were the app's - the classic way a deferred scheme corrupts
        // state. Leave the existing save alone.
        if (g_glstate_c.deferred_draw.held) {
            holds_save = false;
            return;
        }
        holds_save = true;

        if (known_program >= 0)
            program = known_program;
        else
            g_glFuncs.glGetIntegerv(GL_CURRENT_PROGRAM, &program);
        if (known_array_buffer >= 0)
            array_buffer = known_array_buffer;
        else
            g_glFuncs.glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &array_buffer);

        // The two remaining bindings come from the backend shadows instead of
        // synchronous glGetIntegerv round-trips; a real query self-heals them
        // every 256 draws (JNI-direct callers cannot desync them for long).
        auto& gs = g_glstate_c;
        const bool heal = (++gs.backend_shadow_heal_counter & 0xFFu) == 0u;
        if (gs.backend_vao_known && !heal) {
            vertex_array = gs.backend_vao_binding;
        } else {
            g_glFuncs.glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vertex_array);
            gs.backend_vao_binding = vertex_array;
            gs.backend_vao_known = true;
        }
        if (vertex_array == 0) {
            if (gs.backend_vao0_element_known && !heal) {
                element_array_buffer = gs.backend_vao0_element_binding;
            } else {
                g_glFuncs.glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &element_array_buffer);
                gs.backend_vao0_element_binding = element_array_buffer;
                gs.backend_vao0_element_known = true;
            }
        }

        // Hand the save to the context so it outlives this guard. From here
        // the backend stays in the wrapper's draw state until
        // sfpewEntryBarrier(), so consecutive fixed-function draws neither
        // restore nor re-bind it.
        gs.deferred_draw = {true, program, vertex_array, array_buffer, element_array_buffer};
    }

    // Deliberately does not restore: the save lives on the context and is
    // replayed by sfpewEntryBarrier() at the next entry point that is not an
    // immediate-mode vertex call, so a run of fixed-function draws pays the
    // restore once instead of once per draw (plans/12).
    ~fpe_backend_draw_state_guard_t() = default;

    fpe_backend_draw_state_guard_t(const fpe_backend_draw_state_guard_t&) = delete;
    fpe_backend_draw_state_guard_t& operator=(const fpe_backend_draw_state_guard_t&) = delete;
};

// -1 - FPE unavailable, 0 - keep DrawArrays, 1 - switch to DrawElements
int commit_fpe_state_on_draw(GLenum* mode, GLint* first, GLsizei* count, GLint previous_array_buffer);
