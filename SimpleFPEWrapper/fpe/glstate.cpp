// SimpleFPEWrapper - SimpleFPEWrapper/fpe/glstate.cpp
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

#include "types.h"
#include "transformation.h"
#include "../init.h"
#include <format>
#include <glm/gtc/matrix_inverse.hpp>

#define DEBUG 0

void glstate_t::send_uniforms(int program) {
    // LOG()
    if (program <= 0) return;

    const auto& mv = fpe_uniform.transformation.matrices[matrix_idx(GL_MODELVIEW)];
    const auto& proj = fpe_uniform.transformation.matrices[matrix_idx(GL_PROJECTION)];

    // LOG_D("GL_MODELVIEW: ")
    print_matrix(mv);
    // LOG_D("GL_PROJECTION: ")
    print_matrix(proj);

    // TODO: detect change and only set dirty bits here
    g_glFuncs.glBindVertexArray(fpe_state.fpe_vao);

    GLint mvmat = g_glFuncs.glGetUniformLocation(program, "ModelViewMat");

    //    GLint projmat = g_glFuncs.glGetUniformLocation(program, "ProjMat");
    //
    GLint mat_id = g_glFuncs.glGetUniformLocation(program, "ModelViewProjMat");

    const auto mat = proj * mv;
    if (mvmat >= 0) {
        g_glFuncs.glUniformMatrix4fv(mvmat, 1, GL_FALSE,
                                     glm::value_ptr(fpe_uniform.transformation.matrices[matrix_idx(GL_MODELVIEW)]));
    }

    //    g_glFuncs.glUniformMatrix4fv(projmat, 1, GL_FALSE,
    //    glm::value_ptr(fpe_uniform.transformation.matrices[matrix_idx(GL_PROJECTION)]));
    if (mat_id >= 0) g_glFuncs.glUniformMatrix4fv(mat_id, 1, GL_FALSE, glm::value_ptr(mat));

    if (fpe_state.fpe_bools.lighting_enable) {
        const auto send_vec4 = [program](const std::string& name, const glm::vec4& value) {
            const GLint location = g_glFuncs.glGetUniformLocation(program, name.c_str());
            if (location >= 0) g_glFuncs.glUniform4fv(location, 1, glm::value_ptr(value));
        };

        const glm::mat3 normal_matrix = glm::inverseTranspose(glm::mat3(mv));
        const GLint normal_matrix_id = g_glFuncs.glGetUniformLocation(program, "NormalMat");
        if (normal_matrix_id >= 0) {
            g_glFuncs.glUniformMatrix3fv(normal_matrix_id, 1, GL_FALSE, glm::value_ptr(normal_matrix));
        }

        send_vec4("LightModelAmbient", fpe_uniform.light_model_ambient);

        const auto send_material = [&send_vec4](const char* prefix, const material_t& material) {
            send_vec4(std::format("{}Ambient", prefix), material.ambient);
            send_vec4(std::format("{}Diffuse", prefix), material.diffuse);
            send_vec4(std::format("{}Emission", prefix), material.emission);
        };
        send_material("FrontMaterial", fpe_uniform.materials[0]);
        if (fpe_state.light_model_two_side) {
            send_material("BackMaterial", fpe_uniform.materials[1]);
        }

        for (int i = 0; i < MAX_LIGHTS; ++i) {
            if (!fpe_state.fpe_bools.light_enable[i]) continue;

            const auto& light = fpe_uniform.lights[i];
            send_vec4(std::format("Light{}Ambient", i), light.ambient);
            send_vec4(std::format("Light{}Diffuse", i), light.diffuse);
            send_vec4(std::format("Light{}Position", i), light.position);
        }
    }

    for (int i = 0; i < MAX_TEX; ++i) {
        if (!fpe_state.fpe_bools.texture_2d_enable[i]) continue;

        const auto sampler_name = std::format("Sampler{}", i);
        const GLint sampler = g_glFuncs.glGetUniformLocation(program, sampler_name.c_str());
        if (sampler >= 0) g_glFuncs.glUniform1i(sampler, i);

        const auto matrix_name = std::format("TexMat{}", i);
        const GLint texture_matrix = g_glFuncs.glGetUniformLocation(program, matrix_name.c_str());
        if (texture_matrix >= 0) {
            g_glFuncs.glUniformMatrix4fv(texture_matrix, 1, GL_FALSE,
                                         glm::value_ptr(fpe_uniform.transformation.texture_matrices[i]));
        }

        const auto env_color_name = std::format("TexEnvColor{}", i);
        const GLint env_color = g_glFuncs.glGetUniformLocation(program, env_color_name.c_str());
        if (env_color >= 0) {
            g_glFuncs.glUniform4fv(env_color, 1, glm::value_ptr(fpe_uniform.texture_env[i].color));
        }
    }

    if (fpe_state.fpe_bools.fog_enable) {
        const GLint fogcolor_id = g_glFuncs.glGetUniformLocation(program, "FogColor");
        if (fogcolor_id >= 0)
            g_glFuncs.glUniform4fv(fogcolor_id, 1, glm::value_ptr(fpe_uniform.fog_color));

        const GLint fogdensity_id = g_glFuncs.glGetUniformLocation(program, "FogDensity");
        if (fogdensity_id >= 0) g_glFuncs.glUniform1f(fogdensity_id, fpe_uniform.fog_density);

        const GLint fogstart_id = g_glFuncs.glGetUniformLocation(program, "FogStart");
        if (fogstart_id >= 0) g_glFuncs.glUniform1f(fogstart_id, fpe_uniform.fog_start);

        const GLint fogend_id = g_glFuncs.glGetUniformLocation(program, "FogEnd");
        if (fogend_id >= 0) g_glFuncs.glUniform1f(fogend_id, fpe_uniform.fog_end);
    }

    if (fpe_state.fpe_bools.alpha_test_enable) {
        GLint alpharef_id = g_glFuncs.glGetUniformLocation(program, "alpharef");

        g_glFuncs.glUniform1f(alpharef_id, fpe_uniform.alpha_ref);
    }
}

uint64_t glstate_t::program_hash(bool reset) {
    if (reset) {
        p_hash.reset();
        p_hash = std::make_unique<XXHash64>(s_hash_seed);
    }

    vertex_attrib_hash(true);

    auto& hash = *p_hash;

    hash.add(&fpe_state.client_active_texture, sizeof(fpe_state.client_active_texture));
    hash.add(&fpe_state.alpha_func, sizeof(fpe_state.alpha_func));
    hash.add(&fpe_state.fog_mode, sizeof(fpe_state.fog_mode));
    hash.add(&fpe_state.fog_index, sizeof(fpe_state.fog_index));
    hash.add(&fpe_state.fog_coord_src, sizeof(fpe_state.fog_coord_src));
    hash.add(&fpe_state.shade_model, sizeof(fpe_state.shade_model));
    hash.add(&fpe_state.light_model_color_ctrl, sizeof(fpe_state.light_model_color_ctrl));
    hash.add(&fpe_state.light_model_local_viewer, sizeof(fpe_state.light_model_local_viewer));
    hash.add(&fpe_state.light_model_two_side, sizeof(fpe_state.light_model_two_side));
    hash.add(&fpe_state.color_material_face, sizeof(fpe_state.color_material_face));
    hash.add(&fpe_state.color_material_mode, sizeof(fpe_state.color_material_mode));

    hash.add(&fpe_state.fpe_bools, sizeof(fpe_state.fpe_bools));
    hash.add(&fpe_state.texture_env_mode, sizeof(fpe_state.texture_env_mode));

    uint64_t key = hash.hash();

    return key;
}

uint64_t glstate_t::vertex_attrib_hash(bool reset) {
    if (reset) {
        p_hash.reset();
        p_hash = std::make_unique<XXHash64>(s_hash_seed);
    }

    auto& hash = *p_hash;

    auto va = fpe_state.vertexpointer_array.normalize();

    //    hash.add(&va.starting_pointer, sizeof(va.starting_pointer));
    for (int i = 0; i < VERTEX_POINTER_COUNT; ++i) {
        bool enabled = ((va.enabled_pointers >> i) & 1);
        if (enabled || fpe_state.fpe_draw.current_data.sizes.data[i] > 0) {
            hash.add(&i, sizeof(i));
            hash.add(&enabled, sizeof(enabled));
            auto& attr = va.attributes[i];

            if (enabled)
                hash.add(&attr.size, sizeof(attr.size));
            else
                hash.add(&fpe_state.fpe_draw.current_data.sizes.data[i],
                         sizeof(fpe_state.fpe_draw.current_data.sizes.data[i]));

            hash.add(&attr.usage, sizeof(attr.usage));

            if (enabled) {
                hash.add(&attr.type, sizeof(attr.type));
                hash.add(&attr.normalized, sizeof(attr.normalized));
                //                hash.add(&attr.stride, sizeof(attr.stride));
            } else {
                const GLenum t = GL_FLOAT;
                hash.add(&t, sizeof(t));
            }
        }
    }

    uint64_t result = hash.hash();
    return result;
}

program_t& glstate_t::get_or_generate_program(const uint64_t key) {
    // LOG()
    auto it = fpe_programs.find(key);
    if (it == fpe_programs.end()) {
        // LOG_D("Generating new shader: 0x%x", key)
        fpe_shader_generator gen(fpe_state);
        program_t program = gen.generate_program();
        program.get_program();
        it = fpe_programs.emplace(key, std::move(program)).first;
    } else {
        // LOG_D("Using existing shader: 0x%x", key)
    }

    return it->second;
}

bool glstate_t::get_vao(const uint64_t key, GLuint* vao) {
    // LOG()
    if (fpe_vaos.find(key) == fpe_vaos.end()) {
        return false;
    }

    if (vao) *vao = fpe_vaos[key];
    return true;
}

void glstate_t::save_vao(const uint64_t key, const GLuint vao) {
    // LOG()
    fpe_vaos[key] = vao;
}

bool glstate_t::send_vertex_attributes(const vertex_pointer_array_t& va) const {
    // LOG()

    //    auto& va = fpe_state.vertexpointer_array;
    if (!va.dirty) return false;

    for (int i = 0; i < VERTEX_POINTER_COUNT; ++i) {
        bool enabled = ((va.enabled_pointers >> i) & 1);

        auto& vp = va.attributes[i];
        if (enabled) {
            g_glFuncs.glVertexAttribPointer(va.cidx(i), vp.size, vp.type, vp.normalized, vp.stride, vp.pointer);

            g_glFuncs.glEnableVertexAttribArray(va.cidx(i));

            // LOG_D("attrib #%d, cidx #%u: type = %s, size = %d, stride = %d, usage = %s, ptr = %p", i, va.cidx(i),
            //      glEnumToString(vp.type), vp.size, vp.stride, glEnumToString(vp.usage), vp.pointer)
        } else {
            switch (idx2vp(i)) {
            case GL_COLOR_ARRAY:
                if (fpe_state.fpe_draw.current_data.sizes.color_size > 0) {
                    const auto& v = fpe_state.fpe_draw.current_data.color;
                    // LOG_D("attrib #%d, cidx #%u: type = %s, usage = %s, value = (%.2f, %.2f, %.2f, %.2f) (disabled)",
                    // i,
                    //      va.cidx(i), glEnumToString(vp.type), glEnumToString(vp.usage), v[0], v[1], v[2], v[3])

                    g_glFuncs.glVertexAttrib4fv(va.cidx(i), glm::value_ptr(v));
                }
                break;
            case GL_NORMAL_ARRAY:
                if (fpe_state.fpe_draw.current_data.sizes.normal_size > 0) {
                    const auto& v = fpe_state.fpe_draw.current_data.normal;
                    // LOG_D("attrib #%d, cidx #%u: type = %s, usage = %s, value = (%.2f, %.2f, %.2f) (disabled)", i,
                    //      va.cidx(i), glEnumToString(vp.type), glEnumToString(vp.usage), v[0], v[1], v[2])

                    g_glFuncs.glVertexAttrib3fv(va.cidx(i), glm::value_ptr(v));
                }
                break;
            default:
                // A disabled texture-coordinate array consumes the current
                // coordinate for that unit. Minecraft uses this path for the
                // entity lightmap via glMultiTexCoord* rather than a second
                // client array.
                if (i >= 7 && i < 7 + MAX_TEX) {
                    const int textureUnit = i - 7;
                    if (fpe_state.fpe_draw.current_data.sizes.texcoord_size[textureUnit] > 0) {
                        const auto& v = fpe_state.fpe_draw.current_data.texcoord[textureUnit];
                        g_glFuncs.glVertexAttrib4fv(va.cidx(i), glm::value_ptr(v));
                    }
                }
                break;
            }

            if (va.cidx(i) != ~0u) g_glFuncs.glDisableVertexAttribArray(va.cidx(i));
        }
    }

    return true;
}
