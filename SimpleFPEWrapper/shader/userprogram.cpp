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
    GLint light_ambient[MAX_LIGHTS], light_diffuse[MAX_LIGHTS], light_specular[MAX_LIGHTS],
        light_position[MAX_LIGHTS];
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
    char name[64];
    for (int i = 0; i < MAX_LIGHTS; ++i) {
        std::snprintf(name, sizeof(name), "fpe_LightSource[%d].ambient", i);
        u.light_ambient[i] = loc(name);
        std::snprintf(name, sizeof(name), "fpe_LightSource[%d].diffuse", i);
        u.light_diffuse[i] = loc(name);
        std::snprintf(name, sizeof(name), "fpe_LightSource[%d].specular", i);
        u.light_specular[i] = loc(name);
        std::snprintf(name, sizeof(name), "fpe_LightSource[%d].position", i);
        u.light_position[i] = loc(name);
    }
    u.any = u.model_view >= 0 || u.projection >= 0 || u.mvp >= 0 || u.normal >= 0 ||
            u.texture_matrix >= 0 || u.front_ambient >= 0 || u.fog_color >= 0 ||
            u.light_model_ambient >= 0;
    for (int i = 0; i < MAX_LIGHTS && !u.any; ++i)
        u.any = u.light_ambient[i] >= 0 || u.light_diffuse[i] >= 0 || u.light_position[i] >= 0;
}

} // namespace

void sfpewForgetUserProgram(GLuint program) {
    std::lock_guard<std::mutex> lock(g_user_program_mutex);
    userPrograms().erase(program);
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

    for (int i = 0; i < MAX_LIGHTS; ++i) {
        const auto& light = un.lights[i];
        if (u->light_ambient[i] >= 0) g_glFuncs.glUniform4fv(u->light_ambient[i], 1, glm::value_ptr(light.ambient));
        if (u->light_diffuse[i] >= 0) g_glFuncs.glUniform4fv(u->light_diffuse[i], 1, glm::value_ptr(light.diffuse));
        if (u->light_specular[i] >= 0)
            g_glFuncs.glUniform4fv(u->light_specular[i], 1, glm::value_ptr(light.specular));
        if (u->light_position[i] >= 0)
            g_glFuncs.glUniform4fv(u->light_position[i], 1, glm::value_ptr(light.position));
    }
}
