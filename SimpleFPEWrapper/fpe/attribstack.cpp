// SimpleFPEWrapper - SimpleFPEWrapper/fpe/attribstack.cpp
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// glPushAttrib/glPopAttrib over the state the wrapper tracks (plans/06,
// 6.3). GL_COLOR_BUFFER_BIT state that lives in the backend (blend enable,
// funcs, equations, blend color, color mask) is shadowed in
// color_buffer_state_t and replayed on pop. Depth/stencil/scissor
// pass-through state still has no shadow and is NOT captured; the manifest
// documents that deviation.

#include "fpe.hpp"
#include "list.h"
#include "drawing1x.h"

#include <vector>

namespace {

struct attrib_snapshot_t {
    GLbitfield mask = 0;
    fixed_function_bool_t bools{};
    // fog
    GLenum fog_mode;
    GLint fog_index;
    GLenum fog_coord_src;
    glm::vec4 fog_color;
    GLfloat fog_density, fog_start, fog_end;
    // lighting
    GLenum shade_model;
    GLenum light_model_color_ctrl;
    int light_model_local_viewer, light_model_two_side;
    glm::vec4 light_model_ambient;
    GLenum color_material_face, color_material_mode;
    light_t lights[MAX_LIGHTS];
    material_t materials[2];
    // color buffer
    GLenum alpha_func;
    GLclampf alpha_ref;
    color_buffer_state_t color_buffer;
    // texture
    GLenum texture_env_mode[MAX_TEX];
    texture_env_t texture_env[MAX_TEX];
    GLenum texture_gen_mode[MAX_TEX][4];
    glm::vec4 texgen_object_plane[MAX_TEX][4];
    glm::vec4 texgen_eye_plane[MAX_TEX][4];
    // transform
    GLenum matrix_mode;
    glm::dvec4 clip_planes[6];
    // point / polygon
    GLfloat point_size;
    GLenum polygon_mode_front, polygon_mode_back;
    // current
    fixed_function_draw_data_t current_data;
};

struct client_attrib_snapshot_t {
    GLbitfield mask = 0;
    vertex_pointer_array_t vertexpointer_array{};
    GLenum client_active_texture = GL_TEXTURE0;
    bool unpack_swap_bytes = false, unpack_lsb_first = false;
    bool pack_swap_bytes = false, pack_lsb_first = false;
};

constexpr size_t kMaxAttribStackDepth = 16;

std::vector<attrib_snapshot_t>& attribStack() {
    static std::vector<attrib_snapshot_t> stack; // context-scoped with glstate in plans/07
    return stack;
}

std::vector<client_attrib_snapshot_t>& clientAttribStack() {
    static std::vector<client_attrib_snapshot_t> stack;
    return stack;
}

template <typename T, size_t N>
void copy_array(T (&dst)[N], const T (&src)[N]) {
    for (size_t i = 0; i < N; ++i) dst[i] = src[i];
}

} // namespace

void glPushAttrib(GLbitfield mask) {
    flushPendingImmediateDraws();
    LIST_RECORD(glPushAttrib, {}, mask)
    auto& stack = attribStack();
    if (stack.size() >= kMaxAttribStackDepth) {
        g_glstate.set_error(GL_STACK_OVERFLOW);
        return;
    }

    // Snapshot everything the wrapper tracks; the mask decides what PopAttrib
    // restores. Snapshot cost is small next to a 16-deep cap.
    attrib_snapshot_t snap;
    snap.mask = mask;
    const auto& st = g_glstate.fpe_state;
    const auto& un = g_glstate.fpe_uniform;
    snap.bools = st.fpe_bools;
    snap.fog_mode = st.fog_mode;
    snap.fog_index = st.fog_index;
    snap.fog_coord_src = st.fog_coord_src;
    snap.fog_color = un.fog_color;
    snap.fog_density = un.fog_density;
    snap.fog_start = un.fog_start;
    snap.fog_end = un.fog_end;
    snap.shade_model = st.shade_model;
    snap.light_model_color_ctrl = st.light_model_color_ctrl;
    snap.light_model_local_viewer = st.light_model_local_viewer;
    snap.light_model_two_side = st.light_model_two_side;
    snap.light_model_ambient = un.light_model_ambient;
    snap.color_material_face = st.color_material_face;
    snap.color_material_mode = st.color_material_mode;
    copy_array(snap.lights, un.lights);
    copy_array(snap.materials, un.materials);
    snap.alpha_func = st.alpha_func;
    snap.alpha_ref = un.alpha_ref;
    snap.color_buffer = st.color_buffer;
    copy_array(snap.texture_env_mode, st.texture_env_mode);
    copy_array(snap.texture_env, un.texture_env);
    for (int i = 0; i < MAX_TEX; ++i)
        for (int c = 0; c < 4; ++c) {
            snap.texture_gen_mode[i][c] = st.texture_gen_mode[i][c];
            snap.texgen_object_plane[i][c] = un.texgen_object_plane[i][c];
            snap.texgen_eye_plane[i][c] = un.texgen_eye_plane[i][c];
        }
    snap.matrix_mode = un.transformation.matrix_mode;
    copy_array(snap.clip_planes, un.clip_planes);
    snap.point_size = un.point_size;
    snap.polygon_mode_front = un.polygon_mode_front;
    snap.polygon_mode_back = un.polygon_mode_back;
    snap.current_data = st.fpe_draw.current_data;
    stack.push_back(std::move(snap));
}

void glPopAttrib() {
    flushPendingImmediateDraws();
    LIST_RECORD(glPopAttrib, {})
    auto& stack = attribStack();
    if (stack.empty()) {
        g_glstate.set_error(GL_STACK_UNDERFLOW);
        return;
    }
    const attrib_snapshot_t snap = std::move(stack.back());
    stack.pop_back();
    auto& st = g_glstate.fpe_state;
    auto& un = g_glstate.fpe_uniform;
    const GLbitfield mask = snap.mask;

    if (mask & GL_ENABLE_BIT) {
        st.fpe_bools = snap.bools;
    }
    if (mask & GL_FOG_BIT) {
        st.fpe_bools.fog_enable = snap.bools.fog_enable;
        st.fog_mode = snap.fog_mode;
        st.fog_index = snap.fog_index;
        st.fog_coord_src = snap.fog_coord_src;
        un.fog_color = snap.fog_color;
        un.fog_density = snap.fog_density;
        un.fog_start = snap.fog_start;
        un.fog_end = snap.fog_end;
    }
    if (mask & GL_LIGHTING_BIT) {
        st.fpe_bools.lighting_enable = snap.bools.lighting_enable;
        st.fpe_bools.color_material_enable = snap.bools.color_material_enable;
        copy_array(st.fpe_bools.light_enable, snap.bools.light_enable);
        st.shade_model = snap.shade_model;
        st.light_model_color_ctrl = snap.light_model_color_ctrl;
        st.light_model_local_viewer = snap.light_model_local_viewer;
        st.light_model_two_side = snap.light_model_two_side;
        un.light_model_ambient = snap.light_model_ambient;
        st.color_material_face = snap.color_material_face;
        st.color_material_mode = snap.color_material_mode;
        copy_array(un.lights, snap.lights);
        copy_array(un.materials, snap.materials);
    }
    if (mask & GL_COLOR_BUFFER_BIT) {
        st.fpe_bools.alpha_test_enable = snap.bools.alpha_test_enable;
        st.alpha_func = snap.alpha_func;
        un.alpha_ref = snap.alpha_ref;
        restore_color_buffer(st.color_buffer, snap.color_buffer);
        st.color_buffer = snap.color_buffer;
    }
    if (mask & GL_ENABLE_BIT) {
        // GL_ENABLE_BIT covers every enable flag, including the ones the
        // backend owns (GL_BLEND, GL_DITHER); the rest of that group is
        // restored above through fpe_bools.
        color_buffer_state_t enables = st.color_buffer;
        enables.blend_enable = snap.color_buffer.blend_enable;
        enables.dither_enable = snap.color_buffer.dither_enable;
        restore_color_buffer(st.color_buffer, enables);
        st.color_buffer = enables;
    }
    if (mask & GL_TEXTURE_BIT) {
        copy_array(st.fpe_bools.texture_2d_enable, snap.bools.texture_2d_enable);
        copy_array(st.texture_env_mode, snap.texture_env_mode);
        copy_array(un.texture_env, snap.texture_env);
        for (int i = 0; i < MAX_TEX; ++i)
            for (int c = 0; c < 4; ++c) {
                st.fpe_bools.texture_gen_enable[i][c] = snap.bools.texture_gen_enable[i][c];
                st.texture_gen_mode[i][c] = snap.texture_gen_mode[i][c];
                un.texgen_object_plane[i][c] = snap.texgen_object_plane[i][c];
                un.texgen_eye_plane[i][c] = snap.texgen_eye_plane[i][c];
            }
    }
    if (mask & GL_TRANSFORM_BIT) {
        un.transformation.matrix_mode = snap.matrix_mode;
        st.fpe_bools.normalize_enable = snap.bools.normalize_enable;
        st.fpe_bools.rescale_normal_enable = snap.bools.rescale_normal_enable;
        copy_array(st.fpe_bools.clip_plane_enable, snap.bools.clip_plane_enable);
        copy_array(un.clip_planes, snap.clip_planes);
    }
    if (mask & GL_POINT_BIT) un.point_size = snap.point_size;
    if (mask & GL_POLYGON_BIT) {
        un.polygon_mode_front = snap.polygon_mode_front;
        un.polygon_mode_back = snap.polygon_mode_back;
    }
    if (mask & GL_CURRENT_BIT) st.fpe_draw.current_data = snap.current_data;
}

void glPushClientAttrib(GLbitfield mask) {
    flushPendingImmediateDraws();
    auto& stack = clientAttribStack();
    if (stack.size() >= kMaxAttribStackDepth) {
        g_glstate.set_error(GL_STACK_OVERFLOW);
        return;
    }
    client_attrib_snapshot_t snap;
    snap.mask = mask;
    snap.vertexpointer_array = g_glstate.fpe_state.vertexpointer_array;
    snap.client_active_texture = g_glstate.fpe_state.client_active_texture;
    snap.unpack_swap_bytes = g_glstate.pixel_store_unpack_swap_bytes;
    snap.unpack_lsb_first = g_glstate.pixel_store_unpack_lsb_first;
    snap.pack_swap_bytes = g_glstate.pixel_store_pack_swap_bytes;
    snap.pack_lsb_first = g_glstate.pixel_store_pack_lsb_first;
    stack.push_back(std::move(snap));
}

void glPopClientAttrib() {
    flushPendingImmediateDraws();
    auto& stack = clientAttribStack();
    if (stack.empty()) {
        g_glstate.set_error(GL_STACK_UNDERFLOW);
        return;
    }
    const client_attrib_snapshot_t snap = std::move(stack.back());
    stack.pop_back();
    if (snap.mask & GL_CLIENT_VERTEX_ARRAY_BIT) {
        g_glstate.fpe_state.vertexpointer_array = snap.vertexpointer_array;
        g_glstate.fpe_state.vertexpointer_array.dirty = true;
        g_glstate.fpe_state.client_active_texture = snap.client_active_texture;
    }
    if (snap.mask & GL_CLIENT_PIXEL_STORE_BIT) {
        g_glstate.pixel_store_unpack_swap_bytes = snap.unpack_swap_bytes;
        g_glstate.pixel_store_unpack_lsb_first = snap.unpack_lsb_first;
        g_glstate.pixel_store_pack_swap_bytes = snap.pack_swap_bytes;
        g_glstate.pixel_store_pack_lsb_first = snap.pack_lsb_first;
    }
}
