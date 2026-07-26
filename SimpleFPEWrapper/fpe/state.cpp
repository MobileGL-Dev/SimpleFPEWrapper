// SimpleFPEWrapper - SimpleFPEWrapper/fpe/state.cpp
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

#include "state.h"
#include "fpe.hpp"
#include "drawing1x.h"
#include <glm/gtc/type_ptr.hpp>
#include "list.h"
#include "pointer_utils.h"
#include "../init.h"

#include <algorithm>
#include <limits>
#include <type_traits>

#define DEBUG 0

#if GLOBAL_DEBUG || DEBUG
#pragma clang optimize off
#endif

namespace {

int active_texture_index() {
    // The glActiveTexture wrapper maintains a thread-local shadow; a
    // synchronous backend round-trip per call is unnecessary.
    const GLint active = (GLint)sfpewLogicalActiveTexture();
    return std::clamp(active - (GLint)GL_TEXTURE0, 0, MAX_TEX - 1);
}

int light_index(GLenum light) {
    if (light < GL_LIGHT0 || light >= GL_LIGHT0 + MAX_LIGHTS) return -1;
    return static_cast<int>(light - GL_LIGHT0);
}

template <typename T>
GLfloat normalized_component(T value) {
    if constexpr (std::is_floating_point_v<T>) {
        return (GLfloat)value;
    } else if constexpr (std::is_signed_v<T>) {
        const auto converted = (GLfloat)value / (GLfloat)std::numeric_limits<T>::max();
        return std::max(converted, -1.0f);
    } else {
        return (GLfloat)value / (GLfloat)std::numeric_limits<T>::max();
    }
}

int material_param_count(GLenum pname) {
    switch (pname) {
    case GL_SHININESS:
        return 1;
    case GL_COLOR_INDEXES:
        return 3;
    case GL_AMBIENT:
    case GL_DIFFUSE:
    case GL_SPECULAR:
    case GL_EMISSION:
    case GL_AMBIENT_AND_DIFFUSE:
        return 4;
    default:
        return 1;
    }
}

int tex_env_param_count(GLenum pname) {
    return pname == GL_TEXTURE_ENV_COLOR ? 4 : 1;
}

template <typename Func>
void for_each_material(GLenum face, Func&& func) {
    auto& materials = g_glstate.fpe_uniform.materials;
    if (face == GL_FRONT || face == GL_FRONT_AND_BACK) func(materials[0]);
    if (face == GL_BACK || face == GL_FRONT_AND_BACK) func(materials[1]);
}

texture_env_t& current_texture_env() {
    return g_glstate.fpe_uniform.texture_env[active_texture_index()];
}

} // namespace

bool hijack_fpe_states(GLenum cap, bool enable, fixed_function_bool_t* bools) {
    switch (cap) {
    case GL_FOG:
        bools->fog_enable = enable;
        return true;
    case GL_LIGHTING:
        bools->lighting_enable = enable;
        return true;
    case GL_ALPHA_TEST:
        bools->alpha_test_enable = enable;
        return true;
    case GL_COLOR_MATERIAL:
        bools->color_material_enable = enable;
        return true;
    case GL_LIGHT0:
    case GL_LIGHT1:
    case GL_LIGHT2:
    case GL_LIGHT3:
    case GL_LIGHT4:
    case GL_LIGHT5:
    case GL_LIGHT6:
    case GL_LIGHT7:
        bools->light_enable[cap - GL_LIGHT0] = enable;
        return true;
    case GL_TEXTURE_2D:
        bools->texture_2d_enable[active_texture_index()] = enable;
        return true;
    case GL_NORMALIZE:
        bools->normalize_enable = enable;
        return true;
    case GL_CLIP_PLANE0:
    case GL_CLIP_PLANE0 + 1:
    case GL_CLIP_PLANE0 + 2:
    case GL_CLIP_PLANE0 + 3:
    case GL_CLIP_PLANE0 + 4:
    case GL_CLIP_PLANE0 + 5:
        bools->clip_plane_enable[cap - GL_CLIP_PLANE0] = enable;
        return true;
    case GL_RESCALE_NORMAL:
        bools->rescale_normal_enable = enable;
        return true;
    default:
        break;
    }
    return false;
}

void glEnable(GLenum cap) {
    flushPendingImmediateDraws();
    // LOG()
    // LOG_D("glEnable, cap = %s", glEnumToString(cap));

    LIST_RECORD(glEnable, {}, cap)

    if (hijack_fpe_states(cap, true, &g_glstate.fpe_state.fpe_bools)) return;

    g_glFuncs.glEnable(cap);
}

void glDisable(GLenum cap) {
    flushPendingImmediateDraws();
    // LOG()
    // LOG_D("glDisable, cap = %s", glEnumToString(cap))

    LIST_RECORD(glDisable, {}, cap)

    if (hijack_fpe_states(cap, false, &g_glstate.fpe_state.fpe_bools)) return;

    g_glFuncs.glDisable(cap);
}

void glClientActiveTexture(GLenum texture) {
    flushPendingImmediateDraws();
    // LOG()
    // LOG_D("glClientActiveTexture(GL_TEXTURE%d)", texture - GL_TEXTURE0)

    // Todo: this function can be added to displayList when GL 1.3+ is disabled

    g_glstate.fpe_state.client_active_texture = texture;
}

void glAlphaFunc(GLenum func, GLclampf ref) {
    flushPendingImmediateDraws();
    // LOG()
    // LOG_D("glAlphaFunc(%s, %f)", glEnumToString(func), ref)

    // alpha_func feeds shader generation; an unvalidated enum used to end
    // up as error text inside the GLSL source. Erroring commands are not
    // recorded into display lists.
    if (func < GL_NEVER || func > GL_ALWAYS) {
        g_glstate.set_error(GL_INVALID_ENUM);
        return;
    }

    LIST_RECORD(glAlphaFunc, {}, func, ref)

    g_glstate.fpe_state.alpha_func = func;
    g_glstate.fpe_uniform.alpha_ref = ref;
}

void glFogf(GLenum pname, GLfloat param) {
    flushPendingImmediateDraws();
    // LOG()
    // LOG_D("glFogf(%s, %f)", glEnumToString(pname), param)

    LIST_RECORD(glFogf, {}, pname, param)

    switch (pname) {
    case GL_FOG_DENSITY:
        g_glstate.fpe_uniform.fog_density = param;
        return;
    case GL_FOG_START:
        g_glstate.fpe_uniform.fog_start = param;
        return;
    case GL_FOG_END:
        g_glstate.fpe_uniform.fog_end = param;
        return;

    // below should not be handled here
    case GL_FOG_MODE:
    case GL_FOG_INDEX:
    case GL_FOG_COORD_SRC:
        SELF_CALL(glFogi, pname, (GLint)param)
        return;

    default:
        g_glstate.set_error(GL_INVALID_ENUM);
        break;
    }
}

void glFogi(GLenum pname, GLint param) {
    flushPendingImmediateDraws();
    // LOG()
    // LOG_D("glFogi(%s, %s)", glEnumToString(pname), glEnumToString(param))

    LIST_RECORD(glFogi, {}, pname, param)

    switch (pname) {
    case GL_FOG_MODE:
        // fog_mode selects generated shader code; an invalid mode used to
        // produce GLSL referencing an undeclared fogFactor.
        if (param != GL_LINEAR && param != GL_EXP && param != GL_EXP2) {
            g_glstate.set_error(GL_INVALID_ENUM);
            break;
        }
        g_glstate.fpe_state.fog_mode = param;
        break;
    case GL_FOG_INDEX:
        g_glstate.fpe_state.fog_index = param;
        break;
    case GL_FOG_COORD_SRC:
        if (param != GL_FRAGMENT_DEPTH && param != GL_FOG_COORD) {
            g_glstate.set_error(GL_INVALID_ENUM);
            break;
        }
        g_glstate.fpe_state.fog_coord_src = param;
        break;

    // below should not be handled here
    case GL_FOG_DENSITY:
    case GL_FOG_START:
    case GL_FOG_END:
        SELF_CALL(glFogf, pname, (GLfloat)param)
        return;
    default:
        g_glstate.set_error(GL_INVALID_ENUM);
        break;
    }
}

void glFogfv(GLenum pname, const GLfloat* params) {
    flushPendingImmediateDraws();
    // LOG()
    // LOG_D("glFogfv(%s, [...])", glEnumToString(pname))

    if (params == nullptr) return; // LIST_RECORD deep-copies via pname_to_count; guard before it
    LIST_RECORD(glFogfv, {{1, PointerUtils::pname_to_count(pname) * sizeof(GLfloat)}}, pname, params)

    switch (pname) {
    case GL_FOG_MODE:
    case GL_FOG_INDEX:
    case GL_FOG_COORD_SRC:
        SELF_CALL(glFogi, pname, (GLint)params[0])
        break;
    case GL_FOG_DENSITY:
    case GL_FOG_START:
    case GL_FOG_END:
        SELF_CALL(glFogf, pname, params[0])
        break;
    case GL_FOG_COLOR: {
        auto& fcolor = g_glstate.fpe_uniform.fog_color;
        fcolor = glm::make_vec4(params);
        // LOG_D("[...] = [%.2f, %.2f, %.2f, %.2f]", params[0], params[1], params[2], params[3])
        // LOG_D("fcolor = [%.2f, %.2f, %.2f, %.2f]", fcolor[0], fcolor[1], fcolor[2], fcolor[3])
        break;
    }
    default:
        g_glstate.set_error(GL_INVALID_ENUM);
        break;
    }
}

void glFogiv(GLenum pname, const GLint* params) {
    flushPendingImmediateDraws();
    // LOG()
    // LOG_D("glFogiv(%s, [...])", glEnumToString(pname))

    if (params == nullptr) return;
    LIST_RECORD(glFogiv, {{1, PointerUtils::pname_to_count(pname) * sizeof(GLint)}}, pname, params)

    switch (pname) {
    case GL_FOG_COLOR: {
        auto& fcolor = g_glstate.fpe_uniform.fog_color;
        fcolor[0] = (GLfloat)params[0] / (GLfloat)INT32_MAX;
        fcolor[1] = (GLfloat)params[1] / (GLfloat)INT32_MAX;
        fcolor[2] = (GLfloat)params[2] / (GLfloat)INT32_MAX;
        fcolor[3] = (GLfloat)params[3] / (GLfloat)INT32_MAX;
        // LOG_D("[...] = [%d, %d, %d, %d]", params[0], params[1], params[2], params[3])
        break;
    }
    case GL_FOG_MODE:
    case GL_FOG_INDEX:
    case GL_FOG_COORD_SRC:
        SELF_CALL(glFogi, pname, params[0])
        break;
    case GL_FOG_DENSITY:
    case GL_FOG_START:
    case GL_FOG_END:
        SELF_CALL(glFogf, pname, (GLfloat)params[0])
        break;
    default:
        g_glstate.set_error(GL_INVALID_ENUM);
        break;
    }
}

void glShadeModel(GLenum mode) {
    flushPendingImmediateDraws();
    // LOG()
    // LOG_D("glShadeModel(%s)", glEnumToString(mode))

    LIST_RECORD(glShadeModel, {}, mode)

    g_glstate.fpe_state.shade_model = mode;
}

void glLightf(GLenum light, GLenum pname, GLfloat param) {
    flushPendingImmediateDraws();
    // LOG()
    // LOG_D("glLightf(%s, %s, %f)", glEnumToString(light), glEnumToString(pname), param)

    LIST_RECORD(glLightf, {}, light, pname, param)

    const int index = light_index(light);
    if (index < 0) return;
    auto& lightref = g_glstate.fpe_uniform.lights[index];

    switch (pname) {
    case GL_SPOT_EXPONENT:
        lightref.spot_exp = param;
        break;
    case GL_SPOT_CUTOFF:
        lightref.spot_cutoff = param;
        break;
    case GL_CONSTANT_ATTENUATION:
        lightref.constant_attenuation = param;
        break;
    case GL_LINEAR_ATTENUATION:
        lightref.linear_attenuation = param;
        break;
    case GL_QUADRATIC_ATTENUATION:
        lightref.quadratic_attenuation = param;
        break;
    default:
        g_glstate.set_error(GL_INVALID_ENUM);
        break;
    }
}

void glLighti(GLenum light, GLenum pname, GLint param) {
    flushPendingImmediateDraws();
    // LOG()
    // LOG_D("glLighti(%s, %s, %d)", glEnumToString(light), glEnumToString(pname), param)

    LIST_RECORD(glLighti, {}, light, pname, param)

    SELF_CALL(glLightf, light, pname, (GLfloat)param)
}

void glLightfv(GLenum light, GLenum pname, const GLfloat* params) {
    flushPendingImmediateDraws();
    // LOG()
    // LOG_D("glLightfv(%s, %s, [...])", glEnumToString(light), glEnumToString(pname))

    // Commands that would generate an error are not compiled into display
    // lists, and the record path deep-copies params before any check ran.
    const int index = light_index(light);
    if (index < 0 || params == nullptr) return;

    LIST_RECORD(glLightfv, {{2, PointerUtils::pname_to_count(pname) * sizeof(GLfloat)}}, light, pname, params)

    auto& lightref = g_glstate.fpe_uniform.lights[index];

    switch (pname) {
    case GL_SPOT_CUTOFF:
    case GL_SPOT_EXPONENT:
    case GL_CONSTANT_ATTENUATION:
    case GL_LINEAR_ATTENUATION:
    case GL_QUADRATIC_ATTENUATION:
        SELF_CALL(glLightf, light, pname, params[0])
        break;

    case GL_AMBIENT: {
        lightref.ambient = glm::make_vec4(params);
        break;
    }
    case GL_DIFFUSE: {
        lightref.diffuse = glm::make_vec4(params);
        break;
    }
    case GL_SPECULAR: {
        lightref.specular = glm::make_vec4(params);
        break;
    }
    case GL_POSITION: {
        // Desktop GL captures light positions in eye coordinates. In
        // particular, Minecraft rotates the ModelView matrix around the two
        // directional glLight(GL_POSITION) calls and restores it afterwards.
        const auto& model_view =
            g_glstate.fpe_uniform.transformation.matrices[matrix_idx(GL_MODELVIEW)];
        lightref.position = model_view * glm::make_vec4(params);
        break;
    }
    case GL_SPOT_DIRECTION: {
        lightref.spot_direction = glm::make_vec3(params);
        break;
    }
    default:
        g_glstate.set_error(GL_INVALID_ENUM);
        break;
    }
}

void glLightiv(GLenum light, GLenum pname, const GLint* params) {
    flushPendingImmediateDraws();
    // LOG()
    // LOG_D("glLightiv(%s, %s, [...])", glEnumToString(light), glEnumToString(pname))

    if (light_index(light) < 0 || params == nullptr) return;

    LIST_RECORD(glLightiv, {{2, PointerUtils::pname_to_count(pname) * sizeof(GLint)}}, light, pname, params)

    switch (pname) {
    case GL_SPOT_CUTOFF:
    case GL_SPOT_EXPONENT:
    case GL_CONSTANT_ATTENUATION:
    case GL_LINEAR_ATTENUATION:
    case GL_QUADRATIC_ATTENUATION:
        SELF_CALL(glLighti, light, pname, params[0]);
        break;

    case GL_AMBIENT:
    case GL_DIFFUSE:
    case GL_SPECULAR: {
        GLfloat converted[4];
        for (int i = 0; i < 4; ++i) converted[i] = normalized_component(params[i]);
        SELF_CALL(glLightfv, light, pname, converted)
        break;
    }
    case GL_POSITION: {
        const glm::vec4 vec = glm::make_vec4(params);
        SELF_CALL(glLightfv, light, pname, glm::value_ptr(vec))
        break;
    }
    case GL_SPOT_DIRECTION: {
        const glm::vec3 vec = glm::make_vec3(params);
        SELF_CALL(glLightfv, light, pname, glm::value_ptr(vec))
        break;
    }
    default:
        g_glstate.set_error(GL_INVALID_ENUM);
        break;
    }
}

void glLightModelf(GLenum pname, GLfloat param) {
    flushPendingImmediateDraws();
    // LOG()
    // LOG_D("glLightModelf(%s, %f)", glEnumToString(pname), param)

    LIST_RECORD(glLightModelf, {}, pname, param)

    switch (pname) {
    case GL_LIGHT_MODEL_LOCAL_VIEWER:
    case GL_LIGHT_MODEL_COLOR_CONTROL:
    case GL_LIGHT_MODEL_TWO_SIDE:
        SELF_CALL(glLightModeli, pname, (GLint)param)
        break; // previously fell through into the (then-empty) default
    default:
        g_glstate.set_error(GL_INVALID_ENUM);
        break;
    }
}

void glLightModeli(GLenum pname, GLint param) {
    flushPendingImmediateDraws();
    // LOG()
    // LOG_D("glLightModelf(%s, %d)", glEnumToString(pname), param)

    LIST_RECORD(glLightModeli, {}, pname, param)

    switch (pname) {
    case GL_LIGHT_MODEL_COLOR_CONTROL:
        g_glstate.fpe_state.light_model_color_ctrl = param;
        break;
    case GL_LIGHT_MODEL_LOCAL_VIEWER:
        g_glstate.fpe_state.light_model_local_viewer = param;
        break;
    case GL_LIGHT_MODEL_TWO_SIDE:
        g_glstate.fpe_state.light_model_two_side = param;
        break;
    default:
        g_glstate.set_error(GL_INVALID_ENUM);
        break;
    }
}

void glLightModelfv(GLenum pname, const GLfloat* params) {
    flushPendingImmediateDraws();
    // LOG()
    // LOG_D("glLightModelfv(%s, [...])", glEnumToString(pname))

    if (params == nullptr) return;
    LIST_RECORD(glLightModelfv, {{1, PointerUtils::pname_to_count(pname) * sizeof(GLfloat)}}, pname, params)

    switch (pname) {
    case GL_LIGHT_MODEL_AMBIENT:
        g_glstate.fpe_uniform.light_model_ambient = glm::make_vec4(params);
        break;
    case GL_LIGHT_MODEL_COLOR_CONTROL:
    case GL_LIGHT_MODEL_LOCAL_VIEWER:
    case GL_LIGHT_MODEL_TWO_SIDE:
        SELF_CALL(glLightModelf, pname, params[0]);
        break;
    default:
        g_glstate.set_error(GL_INVALID_ENUM);
        break;
    }
}

void glLightModeliv(GLenum pname, const GLint* params) {
    flushPendingImmediateDraws();
    // LOG()
    // LOG_D("glLightModeliv(%s, [...])", glEnumToString(pname))

    if (params == nullptr) return;
    LIST_RECORD(glLightModeliv, {{1, PointerUtils::pname_to_count(pname) * sizeof(GLint)}}, pname, params)

    switch (pname) {
    case GL_LIGHT_MODEL_AMBIENT: {
        GLfloat converted[4];
        for (int i = 0; i < 4; ++i) converted[i] = normalized_component(params[i]);
        SELF_CALL(glLightModelfv, pname, converted)
        break;
    }
    case GL_LIGHT_MODEL_COLOR_CONTROL:
    case GL_LIGHT_MODEL_LOCAL_VIEWER:
    case GL_LIGHT_MODEL_TWO_SIDE:
        SELF_CALL(glLightModeli, pname, params[0]);
        break;
    default:
        g_glstate.set_error(GL_INVALID_ENUM);
        break;
    }
}

void glColorMaterial(GLenum face, GLenum mode) {
    flushPendingImmediateDraws();
    LIST_RECORD(glColorMaterial, {}, face, mode)

    if (face != GL_FRONT && face != GL_BACK && face != GL_FRONT_AND_BACK) return;
    switch (mode) {
    case GL_AMBIENT:
    case GL_DIFFUSE:
    case GL_SPECULAR:
    case GL_EMISSION:
    case GL_AMBIENT_AND_DIFFUSE:
        g_glstate.fpe_state.color_material_face = face;
        g_glstate.fpe_state.color_material_mode = mode;
        break;
    default:
        break;
    }
}

void glMaterialf(GLenum face, GLenum pname, GLfloat param) {
    flushPendingImmediateDraws();
    LIST_RECORD(glMaterialf, {}, face, pname, param)

    if (pname != GL_SHININESS || param < 0.0f || param > 128.0f) return;
    for_each_material(face, [param](material_t& material) { material.shininess = param; });
}

void glMateriali(GLenum face, GLenum pname, GLint param) {
    flushPendingImmediateDraws();
    LIST_RECORD(glMateriali, {}, face, pname, param)

    SELF_CALL(glMaterialf, face, pname, (GLfloat)param)
}

void glMaterialfv(GLenum face, GLenum pname, const GLfloat* params) {
    flushPendingImmediateDraws();
    if (params == nullptr) return;
    LIST_RECORD(glMaterialfv, {{2, material_param_count(pname) * sizeof(GLfloat)}}, face, pname, params)

    if (!params) return;
    switch (pname) {
    case GL_SHININESS:
        SELF_CALL(glMaterialf, face, pname, params[0])
        break;
    case GL_AMBIENT:
        for_each_material(face, [params](material_t& material) { material.ambient = glm::make_vec4(params); });
        break;
    case GL_DIFFUSE:
        for_each_material(face, [params](material_t& material) { material.diffuse = glm::make_vec4(params); });
        break;
    case GL_SPECULAR:
        for_each_material(face, [params](material_t& material) { material.specular = glm::make_vec4(params); });
        break;
    case GL_EMISSION:
        for_each_material(face, [params](material_t& material) { material.emission = glm::make_vec4(params); });
        break;
    case GL_AMBIENT_AND_DIFFUSE:
        for_each_material(face, [params](material_t& material) {
            material.ambient = glm::make_vec4(params);
            material.diffuse = glm::make_vec4(params);
        });
        break;
    case GL_COLOR_INDEXES:
        for_each_material(face, [params](material_t& material) { material.color_indexes = glm::make_vec3(params); });
        break;
    default:
        break;
    }
}

void glMaterialiv(GLenum face, GLenum pname, const GLint* params) {
    flushPendingImmediateDraws();
    if (params == nullptr) return;
    LIST_RECORD(glMaterialiv, {{2, material_param_count(pname) * sizeof(GLint)}}, face, pname, params)

    if (!params) return;
    if (pname == GL_SHININESS) {
        SELF_CALL(glMateriali, face, pname, params[0])
        return;
    }

    GLfloat converted[4] = {};
    const int count = material_param_count(pname);
    for (int i = 0; i < count; ++i) {
        converted[i] = pname == GL_COLOR_INDEXES ? (GLfloat)params[i] : normalized_component(params[i]);
    }
    SELF_CALL(glMaterialfv, face, pname, converted)
}

void glTexEnvf(GLenum target, GLenum pname, GLfloat param) {
    flushPendingImmediateDraws();
    LIST_RECORD(glTexEnvf, {}, target, pname, param)

    auto& env = current_texture_env();
    if (target == GL_TEXTURE_FILTER_CONTROL && pname == GL_TEXTURE_LOD_BIAS) {
        env.lod_bias = param;
        return;
    }
    if (target != GL_TEXTURE_ENV) return;

    switch (pname) {
    case GL_RGB_SCALE:
        env.rgb_scale = param;
        break;
    case GL_ALPHA_SCALE:
        env.alpha_scale = param;
        break;
    default:
        SELF_CALL(glTexEnvi, target, pname, (GLint)param)
        break;
    }
}

void glTexEnvi(GLenum target, GLenum pname, GLint param) {
    flushPendingImmediateDraws();
    LIST_RECORD(glTexEnvi, {}, target, pname, param)

    if (target == GL_TEXTURE_FILTER_CONTROL && pname == GL_TEXTURE_LOD_BIAS) {
        current_texture_env().lod_bias = (GLfloat)param;
        return;
    }
    if (target != GL_TEXTURE_ENV) return;

    auto& env = current_texture_env();
    switch (pname) {
    case GL_TEXTURE_ENV_MODE:
        env.mode = param;
        g_glstate.fpe_state.texture_env_mode[active_texture_index()] = param;
        break;
    case GL_COMBINE_RGB:
        env.combine_rgb = param;
        break;
    case GL_COMBINE_ALPHA:
        env.combine_alpha = param;
        break;
    case GL_SOURCE0_RGB:
    case GL_SOURCE1_RGB:
    case GL_SOURCE2_RGB:
        env.source_rgb[pname - GL_SOURCE0_RGB] = param;
        break;
    case GL_SOURCE0_ALPHA:
    case GL_SOURCE1_ALPHA:
    case GL_SOURCE2_ALPHA:
        env.source_alpha[pname - GL_SOURCE0_ALPHA] = param;
        break;
    case GL_OPERAND0_RGB:
    case GL_OPERAND1_RGB:
    case GL_OPERAND2_RGB:
        env.operand_rgb[pname - GL_OPERAND0_RGB] = param;
        break;
    case GL_OPERAND0_ALPHA:
    case GL_OPERAND1_ALPHA:
    case GL_OPERAND2_ALPHA:
        env.operand_alpha[pname - GL_OPERAND0_ALPHA] = param;
        break;
    case GL_RGB_SCALE:
        env.rgb_scale = (GLfloat)param;
        break;
    case GL_ALPHA_SCALE:
        env.alpha_scale = (GLfloat)param;
        break;
    default:
        break;
    }
}

void glTexEnvfv(GLenum target, GLenum pname, const GLfloat* params) {
    flushPendingImmediateDraws();
    if (params == nullptr) return;
    LIST_RECORD(glTexEnvfv, {{2, tex_env_param_count(pname) * sizeof(GLfloat)}}, target, pname, params)

    if (!params) return;
    if (target == GL_TEXTURE_ENV && pname == GL_TEXTURE_ENV_COLOR) {
        current_texture_env().color = glm::make_vec4(params);
        return;
    }
    SELF_CALL(glTexEnvf, target, pname, params[0])
}

void glTexEnviv(GLenum target, GLenum pname, const GLint* params) {
    flushPendingImmediateDraws();
    if (params == nullptr) return;
    LIST_RECORD(glTexEnviv, {{2, tex_env_param_count(pname) * sizeof(GLint)}}, target, pname, params)

    if (!params) return;
    if (target == GL_TEXTURE_ENV && pname == GL_TEXTURE_ENV_COLOR) {
        GLfloat converted[4];
        for (int i = 0; i < 4; ++i) converted[i] = normalized_component(params[i]);
        SELF_CALL(glTexEnvfv, target, pname, converted)
        return;
    }
    SELF_CALL(glTexEnvi, target, pname, params[0])
}

// The original implementation only defined a few GLfloat scalar immediate-mode
// entry points even though the public GL header declared the complete family.
// Keep one float-backed draw state, but expose the desktop aliases applications
// (notably LWJGL's legacy bindings) resolve independently.

#define DEFINE_VERTEX2(SUFFIX, TYPE)                                                                                  \
    void glVertex2##SUFFIX(TYPE x, TYPE y) {                                                                          \
        LIST_RECORD(glVertex2##SUFFIX, {}, x, y)                                                                      \
        mglVertex<TYPE, 2>({x, y});                                                                                   \
    }

#define DEFINE_VERTEX3(SUFFIX, TYPE)                                                                                  \
    void glVertex3##SUFFIX(TYPE x, TYPE y, TYPE z) {                                                                  \
        LIST_RECORD(glVertex3##SUFFIX, {}, x, y, z)                                                                   \
        mglVertex<TYPE, 3>({x, y, z});                                                                                \
    }

#define DEFINE_VERTEX4(SUFFIX, TYPE)                                                                                  \
    void glVertex4##SUFFIX(TYPE x, TYPE y, TYPE z, TYPE w) {                                                          \
        LIST_RECORD(glVertex4##SUFFIX, {}, x, y, z, w)                                                                \
        mglVertex<TYPE, 4>({x, y, z, w});                                                                             \
    }

#define DEFINE_VERTEX_VECTOR(N, SUFFIX, TYPE)                                                                         \
    void glVertex##N##SUFFIX##v(const TYPE* v) {                                                                      \
        LIST_RECORD(glVertex##N##SUFFIX##v, {{0, sizeof(TYPE) * N}}, v)                                                \
        if (!v) return;                                                                                                \
        if constexpr (N == 2)                                                                                         \
            mglVertex<TYPE, 2>({v[0], v[1]});                                                                         \
        else if constexpr (N == 3)                                                                                    \
            mglVertex<TYPE, 3>({v[0], v[1], v[2]});                                                                   \
        else                                                                                                           \
            mglVertex<TYPE, 4>({v[0], v[1], v[2], v[3]});                                                             \
    }

DEFINE_VERTEX2(d, GLdouble)
DEFINE_VERTEX2(f, GLfloat)
DEFINE_VERTEX2(i, GLint)
DEFINE_VERTEX2(s, GLshort)
DEFINE_VERTEX3(d, GLdouble)
DEFINE_VERTEX3(i, GLint)
DEFINE_VERTEX3(s, GLshort)
DEFINE_VERTEX4(d, GLdouble)
DEFINE_VERTEX4(i, GLint)
DEFINE_VERTEX4(s, GLshort)

DEFINE_VERTEX_VECTOR(2, d, GLdouble)
DEFINE_VERTEX_VECTOR(2, f, GLfloat)
DEFINE_VERTEX_VECTOR(2, i, GLint)
DEFINE_VERTEX_VECTOR(2, s, GLshort)
DEFINE_VERTEX_VECTOR(3, d, GLdouble)
DEFINE_VERTEX_VECTOR(3, f, GLfloat)
DEFINE_VERTEX_VECTOR(3, i, GLint)
DEFINE_VERTEX_VECTOR(3, s, GLshort)
DEFINE_VERTEX_VECTOR(4, d, GLdouble)
DEFINE_VERTEX_VECTOR(4, f, GLfloat)
DEFINE_VERTEX_VECTOR(4, i, GLint)
DEFINE_VERTEX_VECTOR(4, s, GLshort)

#undef DEFINE_VERTEX_VECTOR
#undef DEFINE_VERTEX4
#undef DEFINE_VERTEX3
#undef DEFINE_VERTEX2

template <typename T>
void set_normal(T x, T y, T z) {
    mglNormal<GLfloat, 3>({normalized_component(x), normalized_component(y), normalized_component(z)});
}

#define DEFINE_NORMAL_SCALAR(SUFFIX, TYPE)                                                                            \
    void glNormal3##SUFFIX(TYPE x, TYPE y, TYPE z) {                                                                  \
        LIST_RECORD(glNormal3##SUFFIX, {}, x, y, z)                                                                   \
        set_normal(x, y, z);                                                                                          \
    }

#define DEFINE_NORMAL_VECTOR(SUFFIX, TYPE)                                                                            \
    void glNormal3##SUFFIX##v(const TYPE* v) {                                                                        \
        LIST_RECORD(glNormal3##SUFFIX##v, {{0, sizeof(TYPE) * 3}}, v)                                                  \
        if (!v) return;                                                                                                \
        set_normal(v[0], v[1], v[2]);                                                                                 \
    }

DEFINE_NORMAL_SCALAR(b, GLbyte)
DEFINE_NORMAL_SCALAR(d, GLdouble)
DEFINE_NORMAL_SCALAR(i, GLint)
DEFINE_NORMAL_SCALAR(s, GLshort)
DEFINE_NORMAL_VECTOR(b, GLbyte)
DEFINE_NORMAL_VECTOR(d, GLdouble)
DEFINE_NORMAL_VECTOR(f, GLfloat)
DEFINE_NORMAL_VECTOR(i, GLint)
DEFINE_NORMAL_VECTOR(s, GLshort)

#undef DEFINE_NORMAL_VECTOR
#undef DEFINE_NORMAL_SCALAR

template <typename T>
void set_color3(T red, T green, T blue) {
    mglColor<GLfloat, 4>(
        {normalized_component(red), normalized_component(green), normalized_component(blue), 1.0f});
}

template <typename T>
void set_color4(T red, T green, T blue, T alpha) {
    mglColor<GLfloat, 4>({normalized_component(red), normalized_component(green), normalized_component(blue),
                          normalized_component(alpha)});
}

#define DEFINE_COLOR3_SCALAR(SUFFIX, TYPE)                                                                            \
    void glColor3##SUFFIX(TYPE red, TYPE green, TYPE blue) {                                                          \
        LIST_RECORD(glColor3##SUFFIX, {}, red, green, blue)                                                            \
        set_color3(red, green, blue);                                                                                  \
    }

#define DEFINE_COLOR4_SCALAR(SUFFIX, TYPE)                                                                            \
    void glColor4##SUFFIX(TYPE red, TYPE green, TYPE blue, TYPE alpha) {                                               \
        LIST_RECORD(glColor4##SUFFIX, {}, red, green, blue, alpha)                                                     \
        set_color4(red, green, blue, alpha);                                                                           \
    }

#define DEFINE_COLOR3_VECTOR(SUFFIX, TYPE)                                                                            \
    void glColor3##SUFFIX##v(const TYPE* v) {                                                                         \
        LIST_RECORD(glColor3##SUFFIX##v, {{0, sizeof(TYPE) * 3}}, v)                                                   \
        if (!v) return;                                                                                                \
        set_color3(v[0], v[1], v[2]);                                                                                 \
    }

#define DEFINE_COLOR4_VECTOR(SUFFIX, TYPE)                                                                            \
    void glColor4##SUFFIX##v(const TYPE* v) {                                                                         \
        LIST_RECORD(glColor4##SUFFIX##v, {{0, sizeof(TYPE) * 4}}, v)                                                   \
        if (!v) return;                                                                                                \
        set_color4(v[0], v[1], v[2], v[3]);                                                                           \
    }

DEFINE_COLOR3_SCALAR(b, GLbyte)
DEFINE_COLOR3_SCALAR(d, GLdouble)
DEFINE_COLOR3_SCALAR(i, GLint)
DEFINE_COLOR3_SCALAR(s, GLshort)
DEFINE_COLOR3_SCALAR(ub, GLubyte)
DEFINE_COLOR3_SCALAR(ui, GLuint)
DEFINE_COLOR3_SCALAR(us, GLushort)
DEFINE_COLOR4_SCALAR(b, GLbyte)
DEFINE_COLOR4_SCALAR(d, GLdouble)
DEFINE_COLOR4_SCALAR(i, GLint)
DEFINE_COLOR4_SCALAR(s, GLshort)
DEFINE_COLOR4_SCALAR(ub, GLubyte)
DEFINE_COLOR4_SCALAR(ui, GLuint)
DEFINE_COLOR4_SCALAR(us, GLushort)

DEFINE_COLOR3_VECTOR(b, GLbyte)
DEFINE_COLOR3_VECTOR(d, GLdouble)
DEFINE_COLOR3_VECTOR(f, GLfloat)
DEFINE_COLOR3_VECTOR(i, GLint)
DEFINE_COLOR3_VECTOR(s, GLshort)
DEFINE_COLOR3_VECTOR(ub, GLubyte)
DEFINE_COLOR3_VECTOR(ui, GLuint)
DEFINE_COLOR3_VECTOR(us, GLushort)
DEFINE_COLOR4_VECTOR(b, GLbyte)
DEFINE_COLOR4_VECTOR(d, GLdouble)
DEFINE_COLOR4_VECTOR(f, GLfloat)
DEFINE_COLOR4_VECTOR(i, GLint)
DEFINE_COLOR4_VECTOR(s, GLshort)
DEFINE_COLOR4_VECTOR(ub, GLubyte)
DEFINE_COLOR4_VECTOR(ui, GLuint)
DEFINE_COLOR4_VECTOR(us, GLushort)

#undef DEFINE_COLOR4_VECTOR
#undef DEFINE_COLOR3_VECTOR
#undef DEFINE_COLOR4_SCALAR
#undef DEFINE_COLOR3_SCALAR

// Complete the glTexCoord/glMultiTexCoord families the same way as the
// Vertex/Normal/Color families above: one float-backed draw state behind
// desktop aliases. Per the GL spec texture coordinates are converted
// directly (NOT normalized) for integer variants, so the raw cast inside
// mglTexCoord is the correct conversion.

#define DEFINE_TEXCOORD(N, SUFFIX, TYPE, PARAMS, ARGS)                                                                 \
    void glTexCoord##N##SUFFIX PARAMS {                                                                                \
        LIST_RECORD(glTexCoord##N##SUFFIX, {}, ARGS_LIST ARGS)                                                         \
        mglTexCoord<TYPE, N>({ARGS_LIST ARGS}, 0);                                                                     \
    }

#define ARGS_LIST(...) __VA_ARGS__

#define DEFINE_TEXCOORD_ALL(SUFFIX, TYPE)                                                                              \
    DEFINE_TEXCOORD(1, SUFFIX, TYPE, (TYPE s), (s))                                                                    \
    DEFINE_TEXCOORD(2, SUFFIX, TYPE, (TYPE s, TYPE t), (s, t))                                                         \
    DEFINE_TEXCOORD(3, SUFFIX, TYPE, (TYPE s, TYPE t, TYPE r), (s, t, r))                                              \
    DEFINE_TEXCOORD(4, SUFFIX, TYPE, (TYPE s, TYPE t, TYPE r, TYPE q), (s, t, r, q))

DEFINE_TEXCOORD_ALL(d, GLdouble)
DEFINE_TEXCOORD_ALL(i, GLint)
DEFINE_TEXCOORD_ALL(s, GLshort)
DEFINE_TEXCOORD(1, f, GLfloat, (GLfloat s), (s))
DEFINE_TEXCOORD(3, f, GLfloat, (GLfloat s, GLfloat t, GLfloat r), (s, t, r))
// 2f/4f live in drawing1x.cpp since the original implementation.

#define DEFINE_TEXCOORD_VECTOR(N, SUFFIX, TYPE)                                                                        \
    void glTexCoord##N##SUFFIX##v(const TYPE* v) {                                                                     \
        if (!v) return;                                                                                                \
        LIST_RECORD(glTexCoord##N##SUFFIX##v, {{0, sizeof(TYPE) * N}}, v)                                              \
        if constexpr (N == 1)                                                                                          \
            mglTexCoord<TYPE, 1>({v[0]}, 0);                                                                           \
        else if constexpr (N == 2)                                                                                     \
            mglTexCoord<TYPE, 2>({v[0], v[1]}, 0);                                                                     \
        else if constexpr (N == 3)                                                                                     \
            mglTexCoord<TYPE, 3>({v[0], v[1], v[2]}, 0);                                                               \
        else                                                                                                           \
            mglTexCoord<TYPE, 4>({v[0], v[1], v[2], v[3]}, 0);                                                         \
    }

#define DEFINE_TEXCOORD_VECTOR_ALL(SUFFIX, TYPE)                                                                       \
    DEFINE_TEXCOORD_VECTOR(1, SUFFIX, TYPE)                                                                            \
    DEFINE_TEXCOORD_VECTOR(2, SUFFIX, TYPE)                                                                            \
    DEFINE_TEXCOORD_VECTOR(3, SUFFIX, TYPE)                                                                            \
    DEFINE_TEXCOORD_VECTOR(4, SUFFIX, TYPE)

DEFINE_TEXCOORD_VECTOR_ALL(d, GLdouble)
DEFINE_TEXCOORD_VECTOR_ALL(f, GLfloat)
DEFINE_TEXCOORD_VECTOR_ALL(i, GLint)
DEFINE_TEXCOORD_VECTOR_ALL(s, GLshort)

// MultiTexCoord: validate the unit before recording (invalid enums are not
// compiled into display lists) and before indexing texcoord[MAX_TEX].
#define DEFINE_MULTITEXCOORD(N, SUFFIX, TYPE, PARAMS, ARGS)                                                            \
    void glMultiTexCoord##N##SUFFIX PARAMS {                                                                           \
        if (target < GL_TEXTURE0 || target - GL_TEXTURE0 >= MAX_TEX) {                                                 \
            g_glstate.set_error(GL_INVALID_ENUM);                                                                      \
            return;                                                                                                    \
        }                                                                                                              \
        LIST_RECORD(glMultiTexCoord##N##SUFFIX, {}, target, ARGS_LIST ARGS)                                            \
        mglTexCoord<TYPE, N>({ARGS_LIST ARGS}, (GLint)(target - GL_TEXTURE0));                                         \
    }

#define DEFINE_MULTITEXCOORD_ALL(SUFFIX, TYPE)                                                                         \
    DEFINE_MULTITEXCOORD(1, SUFFIX, TYPE, (GLenum target, TYPE s), (s))                                                \
    DEFINE_MULTITEXCOORD(2, SUFFIX, TYPE, (GLenum target, TYPE s, TYPE t), (s, t))                                     \
    DEFINE_MULTITEXCOORD(3, SUFFIX, TYPE, (GLenum target, TYPE s, TYPE t, TYPE r), (s, t, r))                          \
    DEFINE_MULTITEXCOORD(4, SUFFIX, TYPE, (GLenum target, TYPE s, TYPE t, TYPE r, TYPE q), (s, t, r, q))

DEFINE_MULTITEXCOORD_ALL(d, GLdouble)
DEFINE_MULTITEXCOORD_ALL(i, GLint)
DEFINE_MULTITEXCOORD_ALL(s, GLshort)
DEFINE_MULTITEXCOORD(1, f, GLfloat, (GLenum target, GLfloat s), (s))
DEFINE_MULTITEXCOORD(3, f, GLfloat, (GLenum target, GLfloat s, GLfloat t, GLfloat r), (s, t, r))
// 2f/4f live in drawing1x.cpp since the original implementation.

#define DEFINE_MULTITEXCOORD_VECTOR(N, SUFFIX, TYPE)                                                                   \
    void glMultiTexCoord##N##SUFFIX##v(GLenum target, const TYPE* v) {                                                 \
        if (target < GL_TEXTURE0 || target - GL_TEXTURE0 >= MAX_TEX) {                                                 \
            g_glstate.set_error(GL_INVALID_ENUM);                                                                      \
            return;                                                                                                    \
        }                                                                                                              \
        if (!v) return;                                                                                                \
        LIST_RECORD(glMultiTexCoord##N##SUFFIX##v, {{1, sizeof(TYPE) * N}}, target, v)                                 \
        const GLint unit = (GLint)(target - GL_TEXTURE0);                                                              \
        if constexpr (N == 1)                                                                                          \
            mglTexCoord<TYPE, 1>({v[0]}, unit);                                                                        \
        else if constexpr (N == 2)                                                                                     \
            mglTexCoord<TYPE, 2>({v[0], v[1]}, unit);                                                                  \
        else if constexpr (N == 3)                                                                                     \
            mglTexCoord<TYPE, 3>({v[0], v[1], v[2]}, unit);                                                            \
        else                                                                                                           \
            mglTexCoord<TYPE, 4>({v[0], v[1], v[2], v[3]}, unit);                                                      \
    }

#define DEFINE_MULTITEXCOORD_VECTOR_ALL(SUFFIX, TYPE)                                                                  \
    DEFINE_MULTITEXCOORD_VECTOR(1, SUFFIX, TYPE)                                                                       \
    DEFINE_MULTITEXCOORD_VECTOR(2, SUFFIX, TYPE)                                                                       \
    DEFINE_MULTITEXCOORD_VECTOR(3, SUFFIX, TYPE)                                                                       \
    DEFINE_MULTITEXCOORD_VECTOR(4, SUFFIX, TYPE)

DEFINE_MULTITEXCOORD_VECTOR_ALL(d, GLdouble)
DEFINE_MULTITEXCOORD_VECTOR_ALL(f, GLfloat)
DEFINE_MULTITEXCOORD_VECTOR_ALL(i, GLint)
DEFINE_MULTITEXCOORD_VECTOR_ALL(s, GLshort)

void glPolygonMode(GLenum face, GLenum mode) {
    flushPendingImmediateDraws();
    if (mode != GL_FILL && mode != GL_LINE && mode != GL_POINT) {
        g_glstate.set_error(GL_INVALID_ENUM);
        return;
    }
    if (face != GL_FRONT && face != GL_BACK && face != GL_FRONT_AND_BACK) {
        g_glstate.set_error(GL_INVALID_ENUM);
        return;
    }
    LIST_RECORD(glPolygonMode, {}, face, mode)
    if (face == GL_FRONT || face == GL_FRONT_AND_BACK) g_glstate.fpe_uniform.polygon_mode_front = mode;
    if (face == GL_BACK || face == GL_FRONT_AND_BACK) g_glstate.fpe_uniform.polygon_mode_back = mode;
    if (mode != GL_FILL)
        SFPEW_LOGW("glPolygonMode(0x%x): LINE/POINT emulation is not implemented yet (plans/08)", mode);
}

void glClipPlane(GLenum plane, const GLdouble* equation) {
    flushPendingImmediateDraws();
    if (plane < GL_CLIP_PLANE0 || plane >= GL_CLIP_PLANE0 + 6) {
        g_glstate.set_error(GL_INVALID_ENUM);
        return;
    }
    if (equation == nullptr) return;
    LIST_RECORD(glClipPlane, {{1, sizeof(GLdouble) * 4}}, plane, equation)
    // Spec: the stored plane is the given equation transformed by the
    // inverse of the model-view matrix current at call time.
    const auto& mv = g_glstate.fpe_uniform.transformation.matrices[matrix_idx(GL_MODELVIEW)];
    const glm::vec4 eye = glm::transpose(glm::inverse(mv)) *
                          glm::vec4((float)equation[0], (float)equation[1], (float)equation[2], (float)equation[3]);
    g_glstate.fpe_uniform.clip_planes[plane - GL_CLIP_PLANE0] = glm::dvec4(eye);
}

void glGetClipPlane(GLenum plane, GLdouble* equation) {
    if (equation == nullptr) return;
    if (plane < GL_CLIP_PLANE0 || plane >= GL_CLIP_PLANE0 + 6) {
        g_glstate.set_error(GL_INVALID_ENUM);
        return;
    }
    const auto& p = g_glstate.fpe_uniform.clip_planes[plane - GL_CLIP_PLANE0];
    for (int i = 0; i < 4; ++i) equation[i] = p[i];
}

void glPointSize(GLfloat size) {
    flushPendingImmediateDraws();
    if (size <= 0.0f) {
        g_glstate.set_error(GL_INVALID_VALUE);
        return;
    }
    LIST_RECORD(glPointSize, {}, size)
    g_glstate.fpe_uniform.point_size = size;
}

// glRect: GL 2.1 defines it as the equivalent Begin/Vertex2/End sequence.
// Delegating to the public entry points keeps display-list recording and
// glyph batching semantics identical to the expanded form.
#define DEFINE_RECT(SUFFIX, TYPE)                                                                                      \
    void glRect##SUFFIX(TYPE x1, TYPE y1, TYPE x2, TYPE y2) {                                                          \
        glBegin(GL_QUADS);                                                                                             \
        glVertex2##SUFFIX(x1, y1);                                                                                     \
        glVertex2##SUFFIX(x2, y1);                                                                                     \
        glVertex2##SUFFIX(x2, y2);                                                                                     \
        glVertex2##SUFFIX(x1, y2);                                                                                     \
        glEnd();                                                                                                       \
    }                                                                                                                  \
    void glRect##SUFFIX##v(const TYPE* v1, const TYPE* v2) {                                                           \
        if (!v1 || !v2) return;                                                                                        \
        glRect##SUFFIX(v1[0], v1[1], v2[0], v2[1]);                                                                    \
    }

DEFINE_RECT(d, GLdouble)
DEFINE_RECT(f, GLfloat)
DEFINE_RECT(i, GLint)
DEFINE_RECT(s, GLshort)
