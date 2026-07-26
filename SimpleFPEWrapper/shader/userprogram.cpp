// SimpleFPEWrapper - SimpleFPEWrapper/shader/userprogram.cpp
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// plans/09 9.3 (first half): user programs whose translated shaders
// reference compatibility builtins receive the wrapper's fixed-function
// state through the injected fpe_* uniforms. Locations are resolved
// lazily per program and values are re-fed before every passthrough draw
// with cheap change detection.

#include "../init.h"
#include "../fpe/fpe.hpp"
#include "../fpe/transformation.h"

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cmath>
#include <cstring>
#include <mutex>
#include <unordered_map>

namespace {

struct user_program_uniforms_t {
    bool resolved = false;
    bool any = false;
    GLint model_view = -1, projection = -1, mvp = -1, normal = -1;
    GLint model_view_inverse = -1, projection_inverse = -1;
    GLint texture_matrix = -1; // array location (element 0)
    GLint front_ambient = -1, front_diffuse = -1, front_specular = -1, front_emission = -1,
          front_shininess = -1;
    GLint fog_color = -1, fog_density = -1, fog_start = -1, fog_end = -1, fog_scale = -1;
    GLint light_model_ambient = -1;
    GLint back_ambient = -1, back_diffuse = -1, back_specular = -1, back_emission = -1,
          back_shininess = -1;
    GLint light_ambient[MAX_LIGHTS], light_diffuse[MAX_LIGHTS], light_specular[MAX_LIGHTS],
        light_position[MAX_LIGHTS], light_half_vector[MAX_LIGHTS], light_spot_direction[MAX_LIGHTS],
        light_spot_exponent[MAX_LIGHTS], light_spot_cutoff[MAX_LIGHTS],
        light_spot_cos_cutoff[MAX_LIGHTS], light_const_atten[MAX_LIGHTS],
        light_linear_atten[MAX_LIGHTS], light_quadratic_atten[MAX_LIGHTS];
    // fpe_* attribute locations, slot-indexed like vertex_pointer_array_t
    // (plans/09 S9: fixed-function arrays feed the user program's inputs).
    bool attrs_resolved = false;
    GLint attr_locations[VERTEX_POINTER_COUNT];
    // change-detection mirrors
    glm::mat4 last_mv{0.0f}, last_proj{0.0f};
};

std::mutex g_user_program_mutex;
std::unordered_map<GLuint, user_program_uniforms_t>& userPrograms() {
    static std::unordered_map<GLuint, user_program_uniforms_t> programs;
    return programs;
}

void resolve(GLuint program, user_program_uniforms_t& u) {
    u.resolved = true;
    if (g_glFuncs.glGetUniformLocation == nullptr) return;
    const auto loc = [&](const char* name) { return g_glFuncs.glGetUniformLocation(program, name); };
    u.model_view = loc("fpe_ModelViewMatrix");
    u.projection = loc("fpe_ProjectionMatrix");
    u.mvp = loc("fpe_ModelViewProjectionMatrix");
    u.normal = loc("fpe_NormalMatrix");
    u.model_view_inverse = loc("fpe_ModelViewMatrixInverse");
    u.projection_inverse = loc("fpe_ProjectionMatrixInverse");
    u.texture_matrix = loc("fpe_TextureMatrix[0]");
    u.front_ambient = loc("fpe_FrontMaterial.ambient");
    u.front_diffuse = loc("fpe_FrontMaterial.diffuse");
    u.front_specular = loc("fpe_FrontMaterial.specular");
    u.front_emission = loc("fpe_FrontMaterial.emission");
    u.front_shininess = loc("fpe_FrontMaterial.shininess");
    u.fog_color = loc("fpe_Fog.color");
    u.fog_density = loc("fpe_Fog.density");
    u.fog_start = loc("fpe_Fog.start");
    u.fog_end = loc("fpe_Fog.end");
    u.fog_scale = loc("fpe_Fog.scale");
    u.light_model_ambient = loc("fpe_LightModel.ambient");
    u.back_ambient = loc("fpe_BackMaterial.ambient");
    u.back_diffuse = loc("fpe_BackMaterial.diffuse");
    u.back_specular = loc("fpe_BackMaterial.specular");
    u.back_emission = loc("fpe_BackMaterial.emission");
    u.back_shininess = loc("fpe_BackMaterial.shininess");
    char name[64];
    const auto light_loc = [&](int i, const char* field) {
        std::snprintf(name, sizeof(name), "fpe_LightSource[%d].%s", i, field);
        return loc(name);
    };
    for (int i = 0; i < MAX_LIGHTS; ++i) {
        u.light_ambient[i] = light_loc(i, "ambient");
        u.light_diffuse[i] = light_loc(i, "diffuse");
        u.light_specular[i] = light_loc(i, "specular");
        u.light_position[i] = light_loc(i, "position");
        u.light_half_vector[i] = light_loc(i, "halfVector");
        u.light_spot_direction[i] = light_loc(i, "spotDirection");
        u.light_spot_exponent[i] = light_loc(i, "spotExponent");
        u.light_spot_cutoff[i] = light_loc(i, "spotCutoff");
        u.light_spot_cos_cutoff[i] = light_loc(i, "spotCosCutoff");
        u.light_const_atten[i] = light_loc(i, "constantAttenuation");
        u.light_linear_atten[i] = light_loc(i, "linearAttenuation");
        u.light_quadratic_atten[i] = light_loc(i, "quadraticAttenuation");
    }
    u.any = u.model_view >= 0 || u.projection >= 0 || u.mvp >= 0 || u.normal >= 0 ||
            u.texture_matrix >= 0 || u.front_ambient >= 0 || u.back_ambient >= 0 ||
            u.fog_color >= 0 || u.light_model_ambient >= 0;
    for (int i = 0; i < MAX_LIGHTS && !u.any; ++i)
        u.any = u.light_ambient[i] >= 0 || u.light_diffuse[i] >= 0 || u.light_position[i] >= 0 ||
                u.light_const_atten[i] >= 0 || u.light_spot_direction[i] >= 0;
}

} // namespace

void sfpewForgetUserProgram(GLuint program) {
    std::lock_guard<std::mutex> lock(g_user_program_mutex);
    userPrograms().erase(program);
}

// Slot layout mirrors vp2idx(): 0 vertex, 1 normal, 2 color, 3 index,
// 4 edge flag, 5 fog coord, 6 secondary color, 7+u texcoord unit u.
bool sfpewUserProgramAttribLocations(GLuint program, GLint out_locations[VERTEX_POINTER_COUNT]) {
    if (program == 0 || g_glFuncs.glGetAttribLocation == nullptr) return false;
    std::lock_guard<std::mutex> lock(g_user_program_mutex);
    auto& u = userPrograms()[program];
    if (!u.attrs_resolved) {
        for (int i = 0; i < VERTEX_POINTER_COUNT; ++i) u.attr_locations[i] = -1;
        u.attr_locations[0] = g_glFuncs.glGetAttribLocation(program, "fpe_Vertex");
        u.attr_locations[1] = g_glFuncs.glGetAttribLocation(program, "fpe_Normal");
        u.attr_locations[2] = g_glFuncs.glGetAttribLocation(program, "fpe_Color");
        u.attr_locations[5] = g_glFuncs.glGetAttribLocation(program, "fpe_FogCoord");
        u.attr_locations[6] = g_glFuncs.glGetAttribLocation(program, "fpe_SecondaryColor");
        char name[32];
        for (int unit = 0; unit + 7 < VERTEX_POINTER_COUNT; ++unit) {
            std::snprintf(name, sizeof(name), "fpe_MultiTexCoord%d", unit);
            u.attr_locations[7 + unit] = g_glFuncs.glGetAttribLocation(program, name);
        }
        u.attrs_resolved = true;
    }
    std::memcpy(out_locations, u.attr_locations, sizeof(u.attr_locations));
    // Without a position input the program does not consume fixed-function
    // vertex data at all (e.g. piglit-style tests feeding attribs directly).
    return u.attr_locations[0] >= 0;
}

// Submits the normalized array layout onto the user program's attribute
// locations. Caller has fpe_user_vao + the source buffer bound; the mask
// in fpe_state tracks which locations stay enabled between draws.
void sfpewSendUserProgramAttributes(const GLint locations[VERTEX_POINTER_COUNT],
                                    const vertex_pointer_array_t& va, GLintptr binding_offset) {
    auto& st = g_glstate.fpe_state;
    const auto& cur = st.fpe_draw.current_data;
    uint64_t enabled_now = 0;
#ifdef SFPEW_DEBUG_USERATTRIBS
    fprintf(stderr, "[userattribs] enabled_pointers=0x%x stride=%d prev_mask=0x%llx\n",
            va.enabled_pointers, va.stride, (unsigned long long)st.fpe_user_vao_enabled);
#endif
    for (int i = 0; i < VERTEX_POINTER_COUNT; ++i) {
        const GLint loc = locations[i];
        if (loc < 0 || loc >= 64) continue;
        const bool array_on = ((va.enabled_pointers >> i) & 1u) != 0;
        if (array_on) {
            const auto& vp = va.attributes[i];
            const void* pointer = reinterpret_cast<const void*>(
                reinterpret_cast<uintptr_t>(vp.pointer) + static_cast<uintptr_t>(binding_offset));
            g_glFuncs.glVertexAttribPointer((GLuint)loc, vp.size, vp.type,
                                            (GLboolean)(vp.normalized != 0), va.stride, pointer);
            g_glFuncs.glEnableVertexAttribArray((GLuint)loc);
#ifdef SFPEW_DEBUG_USERATTRIBS
            fprintf(stderr, "[userattribs] slot %d -> loc %d size=%d type=0x%x stride=%d ptr=%p\n",
                    i, loc, vp.size, vp.type, va.stride, pointer);
#endif
            enabled_now |= 1ull << loc;
            continue;
        }
        // Array disabled but the program reads the input: legacy current-
        // value semantics (glColor4f/glNormal3f/glTexCoord2f between draws).
        if ((st.fpe_user_vao_enabled >> loc) & 1ull)
            g_glFuncs.glDisableVertexAttribArray((GLuint)loc);
        switch (i) {
        case 1:
            g_glFuncs.glVertexAttrib4f((GLuint)loc, cur.normal.x, cur.normal.y, cur.normal.z, 0.0f);
            break;
        case 2:
            g_glFuncs.glVertexAttrib4fv((GLuint)loc, glm::value_ptr(cur.color));
            break;
        default:
            if (i >= 7) {
                g_glFuncs.glVertexAttrib4fv((GLuint)loc, glm::value_ptr(cur.texcoord[i - 7]));
            } else {
                g_glFuncs.glVertexAttrib4f((GLuint)loc, 0.0f, 0.0f, 0.0f, 1.0f);
            }
            break;
        }
    }
    // Disable locations left over from a previous layout in this VAO.
    uint64_t stale = st.fpe_user_vao_enabled & ~enabled_now;
    for (int loc = 0; loc < 64; ++loc) {
        if ((stale >> loc) & 1ull) g_glFuncs.glDisableVertexAttribArray((GLuint)loc);
    }
    st.fpe_user_vao_enabled = enabled_now;
}

// Called before passthrough draws while a user program is current.
void sfpewFeedUserProgramUniforms(GLuint program) {
    if (program == 0 || g_glFuncs.glUniformMatrix4fv == nullptr || g_glFuncs.glUniform4fv == nullptr)
        return;

    user_program_uniforms_t* u;
    {
        std::lock_guard<std::mutex> lock(g_user_program_mutex);
        u = &userPrograms()[program];
        if (!u->resolved) resolve(program, *u);
    }
    if (!u->any) return;

    const auto& un = g_glstate.fpe_uniform;
    const auto& mv = un.transformation.matrices[matrix_idx(GL_MODELVIEW)];
    const auto& proj = un.transformation.matrices[matrix_idx(GL_PROJECTION)];
    const bool mv_changed = std::memcmp(&mv, &u->last_mv, sizeof(mv)) != 0;
    const bool proj_changed = std::memcmp(&proj, &u->last_proj, sizeof(proj)) != 0;

    if (mv_changed) {
        if (u->model_view >= 0) g_glFuncs.glUniformMatrix4fv(u->model_view, 1, GL_FALSE, glm::value_ptr(mv));
        if (u->normal >= 0) {
            const glm::mat3 normal_matrix = glm::inverseTranspose(glm::mat3(mv));
            g_glFuncs.glUniformMatrix3fv(u->normal, 1, GL_FALSE, glm::value_ptr(normal_matrix));
        }
        if (u->model_view_inverse >= 0) {
            const glm::mat4 inv = glm::inverse(mv);
            g_glFuncs.glUniformMatrix4fv(u->model_view_inverse, 1, GL_FALSE, glm::value_ptr(inv));
        }
        u->last_mv = mv;
    }
    if (proj_changed) {
        if (u->projection >= 0) g_glFuncs.glUniformMatrix4fv(u->projection, 1, GL_FALSE, glm::value_ptr(proj));
        if (u->projection_inverse >= 0) {
            const glm::mat4 inv = glm::inverse(proj);
            g_glFuncs.glUniformMatrix4fv(u->projection_inverse, 1, GL_FALSE, glm::value_ptr(inv));
        }
        u->last_proj = proj;
    }
    if ((mv_changed || proj_changed) && u->mvp >= 0) {
        const glm::mat4 mvp = proj * mv;
        g_glFuncs.glUniformMatrix4fv(u->mvp, 1, GL_FALSE, glm::value_ptr(mvp));
    }

    if (u->texture_matrix >= 0) {
        // 8 matrices in one call; drivers accept partial arrays.
        g_glFuncs.glUniformMatrix4fv(u->texture_matrix, 8, GL_FALSE,
                                     glm::value_ptr(un.transformation.texture_matrices[0]));
    }

    const auto& mat = un.materials[0];
    if (u->front_ambient >= 0) g_glFuncs.glUniform4fv(u->front_ambient, 1, glm::value_ptr(mat.ambient));
    if (u->front_diffuse >= 0) g_glFuncs.glUniform4fv(u->front_diffuse, 1, glm::value_ptr(mat.diffuse));
    if (u->front_specular >= 0) g_glFuncs.glUniform4fv(u->front_specular, 1, glm::value_ptr(mat.specular));
    if (u->front_emission >= 0) g_glFuncs.glUniform4fv(u->front_emission, 1, glm::value_ptr(mat.emission));
    if (u->front_shininess >= 0) g_glFuncs.glUniform1f(u->front_shininess, mat.shininess);

    if (u->fog_color >= 0) g_glFuncs.glUniform4fv(u->fog_color, 1, glm::value_ptr(un.fog_color));
    if (u->fog_density >= 0) g_glFuncs.glUniform1f(u->fog_density, un.fog_density);
    if (u->fog_start >= 0) g_glFuncs.glUniform1f(u->fog_start, un.fog_start);
    if (u->fog_end >= 0) g_glFuncs.glUniform1f(u->fog_end, un.fog_end);
    if (u->fog_scale >= 0) {
        const float span = un.fog_end - un.fog_start;
        g_glFuncs.glUniform1f(u->fog_scale, span != 0.0f ? 1.0f / span : 1.0f);
    }
    if (u->light_model_ambient >= 0)
        g_glFuncs.glUniform4fv(u->light_model_ambient, 1, glm::value_ptr(un.light_model_ambient));

    const auto& back = un.materials[1];
    if (u->back_ambient >= 0) g_glFuncs.glUniform4fv(u->back_ambient, 1, glm::value_ptr(back.ambient));
    if (u->back_diffuse >= 0) g_glFuncs.glUniform4fv(u->back_diffuse, 1, glm::value_ptr(back.diffuse));
    if (u->back_specular >= 0) g_glFuncs.glUniform4fv(u->back_specular, 1, glm::value_ptr(back.specular));
    if (u->back_emission >= 0) g_glFuncs.glUniform4fv(u->back_emission, 1, glm::value_ptr(back.emission));
    if (u->back_shininess >= 0) g_glFuncs.glUniform1f(u->back_shininess, back.shininess);

    for (int i = 0; i < MAX_LIGHTS; ++i) {
        const auto& light = un.lights[i];
        if (u->light_ambient[i] >= 0) g_glFuncs.glUniform4fv(u->light_ambient[i], 1, glm::value_ptr(light.ambient));
        if (u->light_diffuse[i] >= 0) g_glFuncs.glUniform4fv(u->light_diffuse[i], 1, glm::value_ptr(light.diffuse));
        if (u->light_specular[i] >= 0)
            g_glFuncs.glUniform4fv(u->light_specular[i], 1, glm::value_ptr(light.specular));
        if (u->light_position[i] >= 0)
            g_glFuncs.glUniform4fv(u->light_position[i], 1, glm::value_ptr(light.position));
        if (u->light_half_vector[i] >= 0) {
            // Infinite-viewer half vector of legacy lighting: only exact for
            // directional lights, which is the case gl_LightSource.halfVector
            // is specified for.
            const glm::vec3 to_light = glm::normalize(glm::vec3(light.position));
            const glm::vec3 half = glm::normalize(to_light + glm::vec3(0.0f, 0.0f, 1.0f));
            g_glFuncs.glUniform4fv(u->light_half_vector[i], 1,
                                   glm::value_ptr(glm::vec4(half, 1.0f)));
        }
        if (u->light_spot_direction[i] >= 0)
            g_glFuncs.glUniform3fv(u->light_spot_direction[i], 1, glm::value_ptr(light.spot_direction));
        if (u->light_spot_exponent[i] >= 0)
            g_glFuncs.glUniform1f(u->light_spot_exponent[i], light.spot_exp);
        if (u->light_spot_cutoff[i] >= 0)
            g_glFuncs.glUniform1f(u->light_spot_cutoff[i], light.spot_cutoff);
        if (u->light_spot_cos_cutoff[i] >= 0)
            g_glFuncs.glUniform1f(u->light_spot_cos_cutoff[i],
                                  light.spot_cutoff == 180.0f
                                      ? -1.0f
                                      : std::cos(glm::radians(light.spot_cutoff)));
        if (u->light_const_atten[i] >= 0)
            g_glFuncs.glUniform1f(u->light_const_atten[i], light.constant_attenuation);
        if (u->light_linear_atten[i] >= 0)
            g_glFuncs.glUniform1f(u->light_linear_atten[i], light.linear_attenuation);
        if (u->light_quadratic_atten[i] >= 0)
            g_glFuncs.glUniform1f(u->light_quadratic_atten[i], light.quadratic_attenuation);
    }
}
