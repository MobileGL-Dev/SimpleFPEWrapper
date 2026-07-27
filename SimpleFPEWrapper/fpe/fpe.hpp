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

#define GET_PREV_PROGRAM                                                                                               \
    GLint m_prev_program;                                                                                              \
    g_glFuncs.glGetIntegerv(GL_CURRENT_PROGRAM, &m_prev_program);
#define SET_PREV_PROGRAM                                                                                               \
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

    explicit fpe_backend_draw_state_guard_t(GLint known_program = -1,
                                            GLint known_array_buffer = -1) {
        if (g_glFuncs.glGetIntegerv == nullptr || g_glFuncs.glUseProgram == nullptr ||
            g_glFuncs.glBindVertexArray == nullptr || g_glFuncs.glBindBuffer == nullptr) {
            return;
        }
        active = true;
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
    }

    ~fpe_backend_draw_state_guard_t() {
        if (!active) return;
        g_glFuncs.glUseProgram(program);
        sfpewBackendBindVertexArray(static_cast<GLuint>(vertex_array));
        // A non-zero restored VAO already carries its element binding; only
        // VAO 0's must be re-bound explicitly.
        if (element_array_buffer >= 0)
            sfpewBackendBindElementBuffer(static_cast<GLuint>(element_array_buffer));
        g_glFuncs.glBindBuffer(GL_ARRAY_BUFFER, array_buffer);
    }

    fpe_backend_draw_state_guard_t(const fpe_backend_draw_state_guard_t&) = delete;
    fpe_backend_draw_state_guard_t& operator=(const fpe_backend_draw_state_guard_t&) = delete;
};

// -1 - FPE unavailable, 0 - keep DrawArrays, 1 - switch to DrawElements
int commit_fpe_state_on_draw(GLenum* mode, GLint* first, GLsizei* count, GLint previous_array_buffer);
