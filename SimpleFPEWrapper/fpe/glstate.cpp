// SimpleFPEWrapper - SimpleFPEWrapper/fpe/glstate.cpp
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

#include <cmath>
#include "types.h"
#include "transformation.h"
#include "../init.h"
#include <cstring>
#include <format>
#include <glm/gtc/matrix_inverse.hpp>

#define DEBUG 0

void program_uniform_locations_t::initialize(GLuint program) {
    if (initialized || program == 0) return;
    initialized = true;

    const auto location = [program](const std::string& name) {
        return g_glFuncs.glGetUniformLocation(program, name.c_str());
    };

    model_view = location("ModelViewMat");
    model_view_projection = location("ModelViewProjMat");
    normal = location("NormalMat");
    light_model_ambient = location("LightModelAmbient");
    front_material_ambient = location("FrontMaterialAmbient");
    front_material_diffuse = location("FrontMaterialDiffuse");
    front_material_emission = location("FrontMaterialEmission");
    front_material_specular = location("FrontMaterialSpecular");
    front_material_shininess = location("FrontMaterialShininess");
    back_material_ambient = location("BackMaterialAmbient");
    back_material_diffuse = location("BackMaterialDiffuse");
    back_material_emission = location("BackMaterialEmission");
    back_material_specular = location("BackMaterialSpecular");
    back_material_shininess = location("BackMaterialShininess");
    fog_color = location("FogColor");
    fog_density = location("FogDensity");
    fog_start = location("FogStart");
    fog_end = location("FogEnd");
    alpha_ref = location("alpharef");
    point_size = location("PointSize");
    for (int i = 0; i < 6; ++i) clip_planes[i] = location(std::format("ClipPlane{}", i));
    polygon_stipple_rows = location("PolygonStipple");

    for (int i = 0; i < MAX_LIGHTS; ++i) {
        light_ambient[i] = location(std::format("Light{}Ambient", i));
        light_diffuse[i] = location(std::format("Light{}Diffuse", i));
        light_specular[i] = location(std::format("Light{}Specular", i));
        light_position[i] = location(std::format("Light{}Position", i));
        light_attenuation[i] = location(std::format("Light{}Attenuation", i));
        light_spot_direction[i] = location(std::format("Light{}SpotDirection", i));
        light_spot_params[i] = location(std::format("Light{}SpotParams", i));
    }
    for (int i = 0; i < MAX_TEX; ++i) {
        sampler[i] = location(std::format("Sampler{}", i));
        texture_matrix[i] = location(std::format("TexMat{}", i));
        texture_env_color[i] = location(std::format("TexEnvColor{}", i));
        texgen_obj_planes[i] = location(std::format("TexGen{}ObjPlanes", i));
        texgen_eye_planes[i] = location(std::format("TexGen{}EyePlanes", i));
    }
}

void glstate_t::send_uniforms(program_t& program) {
    // LOG()
    const int program_id = program.get_program();
    if (program_id <= 0) return;
    auto& locations = program.uniforms;
    locations.initialize(program_id);
    auto& values = program.uniform_values;
    const bool first_upload = !values.initialized;

    const auto& mv = fpe_uniform.transformation.matrices[matrix_idx(GL_MODELVIEW)];
    const auto& proj = fpe_uniform.transformation.matrices[matrix_idx(GL_PROJECTION)];

    // LOG_D("GL_MODELVIEW: ")
    print_matrix(mv);
    // LOG_D("GL_PROJECTION: ")
    print_matrix(proj);

    const auto differs = [first_upload](const auto& current, const auto& previous) {
        return first_upload || std::memcmp(&current, &previous, sizeof(current)) != 0;
    };
    const bool model_view_changed = differs(mv, values.model_view);
    const bool projection_changed = differs(proj, values.projection);

    if (model_view_changed && locations.model_view >= 0) {
        g_glFuncs.glUniformMatrix4fv(locations.model_view, 1, GL_FALSE,
                                     glm::value_ptr(mv));
    }

    if ((model_view_changed || projection_changed) && locations.model_view_projection >= 0) {
        const auto mat = proj * mv;
        g_glFuncs.glUniformMatrix4fv(locations.model_view_projection, 1, GL_FALSE, glm::value_ptr(mat));
    }
    if (model_view_changed) values.model_view = mv;
    if (projection_changed) values.projection = proj;

    if (fpe_state.fpe_bools.lighting_enable) {
        const auto send_vec4 = [&differs](GLint location, const glm::vec4& value, glm::vec4& previous) {
            if (!differs(value, previous)) return;
            if (location >= 0) g_glFuncs.glUniform4fv(location, 1, glm::value_ptr(value));
            previous = value;
        };

        if (model_view_changed && locations.normal >= 0) {
            const glm::mat3 normal_matrix = glm::inverseTranspose(glm::mat3(mv));
            g_glFuncs.glUniformMatrix3fv(locations.normal, 1, GL_FALSE,
                                         glm::value_ptr(normal_matrix));
        }

        send_vec4(locations.light_model_ambient, fpe_uniform.light_model_ambient,
                  values.light_model_ambient);

        const auto send_material = [&send_vec4, first_upload](int index, GLint ambient, GLint diffuse,
                                                              GLint emission, GLint specular, GLint shininess,
                                                              const material_t& material,
                                                              program_uniform_values_t& values) {
            send_vec4(ambient, material.ambient, values.material_ambient[index]);
            send_vec4(diffuse, material.diffuse, values.material_diffuse[index]);
            send_vec4(emission, material.emission, values.material_emission[index]);
            send_vec4(specular, material.specular, values.material_specular[index]);
            if ((first_upload || material.shininess != values.material_shininess[index]) && shininess >= 0) {
                g_glFuncs.glUniform1f(shininess, material.shininess);
                values.material_shininess[index] = material.shininess;
            }
        };
        send_material(0, locations.front_material_ambient, locations.front_material_diffuse,
                      locations.front_material_emission, locations.front_material_specular,
                      locations.front_material_shininess, fpe_uniform.materials[0], values);
        if (fpe_state.light_model_two_side) {
            send_material(1, locations.back_material_ambient, locations.back_material_diffuse,
                          locations.back_material_emission, locations.back_material_specular,
                          locations.back_material_shininess, fpe_uniform.materials[1], values);
        }

        for (int i = 0; i < MAX_LIGHTS; ++i) {
            if (!fpe_state.fpe_bools.light_enable[i]) continue;

            const auto& light = fpe_uniform.lights[i];
            send_vec4(locations.light_ambient[i], light.ambient, values.light_ambient[i]);
            send_vec4(locations.light_diffuse[i], light.diffuse, values.light_diffuse[i]);
            send_vec4(locations.light_specular[i], light.specular, values.light_specular[i]);
            send_vec4(locations.light_position[i], light.position, values.light_position[i]);

            const glm::vec4 attenuation(light.constant_attenuation, light.linear_attenuation,
                                        light.quadratic_attenuation, 0.0f);
            if (differs(attenuation, values.light_attenuation[i])) {
                if (locations.light_attenuation[i] >= 0)
                    g_glFuncs.glUniform3fv(locations.light_attenuation[i], 1, glm::value_ptr(attenuation));
                values.light_attenuation[i] = attenuation;
            }
            const glm::vec4 spot_direction(light.spot_direction, 0.0f);
            if (differs(spot_direction, values.light_spot_direction[i])) {
                if (locations.light_spot_direction[i] >= 0)
                    g_glFuncs.glUniform3fv(locations.light_spot_direction[i], 1, glm::value_ptr(spot_direction));
                values.light_spot_direction[i] = spot_direction;
            }
            // cutoff 180 means "not a spotlight"; encode it as -2 so the
            // shader's threshold test is immune to cos() rounding.
            const glm::vec4 spot_params(
                light.spot_cutoff >= 180.0f ? -2.0f : std::cos(glm::radians(light.spot_cutoff)),
                light.spot_exp, 0.0f, 0.0f);
            if (differs(spot_params, values.light_spot_params[i])) {
                if (locations.light_spot_params[i] >= 0)
                    g_glFuncs.glUniform2fv(locations.light_spot_params[i], 1, glm::value_ptr(spot_params));
                values.light_spot_params[i] = spot_params;
            }
        }
    }

    for (int i = 0; i < MAX_TEX; ++i) {
        if (!fpe_state.fpe_bools.texture_2d_enable[i]) continue;

        if (first_upload && locations.sampler[i] >= 0) g_glFuncs.glUniform1i(locations.sampler[i], i);

        const auto& texture_matrix = fpe_uniform.transformation.texture_matrices[i];
        if (differs(texture_matrix, values.texture_matrix[i])) {
            if (locations.texture_matrix[i] >= 0) {
                g_glFuncs.glUniformMatrix4fv(locations.texture_matrix[i], 1, GL_FALSE,
                                             glm::value_ptr(texture_matrix));
            }
            values.texture_matrix[i] = texture_matrix;
        }

        const auto& env_color = fpe_uniform.texture_env[i].color;
        if (differs(env_color, values.texture_env_color[i])) {
            if (locations.texture_env_color[i] >= 0)
                g_glFuncs.glUniform4fv(locations.texture_env_color[i], 1, glm::value_ptr(env_color));
            values.texture_env_color[i] = env_color;
        }

        if (locations.texgen_obj_planes[i] >= 0 &&
            (first_upload || std::memcmp(fpe_uniform.texgen_object_plane[i], values.texgen_obj_planes[i],
                                         sizeof(values.texgen_obj_planes[i])) != 0)) {
            g_glFuncs.glUniform4fv(locations.texgen_obj_planes[i], 4,
                                   glm::value_ptr(fpe_uniform.texgen_object_plane[i][0]));
            std::memcpy(values.texgen_obj_planes[i], fpe_uniform.texgen_object_plane[i],
                        sizeof(values.texgen_obj_planes[i]));
        }
        if (locations.texgen_eye_planes[i] >= 0 &&
            (first_upload || std::memcmp(fpe_uniform.texgen_eye_plane[i], values.texgen_eye_planes[i],
                                         sizeof(values.texgen_eye_planes[i])) != 0)) {
            g_glFuncs.glUniform4fv(locations.texgen_eye_planes[i], 4,
                                   glm::value_ptr(fpe_uniform.texgen_eye_plane[i][0]));
            std::memcpy(values.texgen_eye_planes[i], fpe_uniform.texgen_eye_plane[i],
                        sizeof(values.texgen_eye_planes[i]));
        }
    }

    if (fpe_state.fpe_bools.fog_enable) {
        const auto send_float = [first_upload](GLint location, GLfloat value, GLfloat& previous) {
            if (!first_upload && value == previous) return;
            if (location >= 0) g_glFuncs.glUniform1f(location, value);
            previous = value;
        };
        if (differs(fpe_uniform.fog_color, values.fog_color)) {
            if (locations.fog_color >= 0)
                g_glFuncs.glUniform4fv(locations.fog_color, 1, glm::value_ptr(fpe_uniform.fog_color));
            values.fog_color = fpe_uniform.fog_color;
        }
        send_float(locations.fog_density, fpe_uniform.fog_density, values.fog_density);
        send_float(locations.fog_start, fpe_uniform.fog_start, values.fog_start);
        send_float(locations.fog_end, fpe_uniform.fog_end, values.fog_end);
    }

    if (fpe_state.fpe_bools.alpha_test_enable &&
        (first_upload || fpe_uniform.alpha_ref != values.alpha_ref)) {
        if (locations.alpha_ref >= 0) g_glFuncs.glUniform1f(locations.alpha_ref, fpe_uniform.alpha_ref);
        values.alpha_ref = fpe_uniform.alpha_ref;
    }

    for (int i = 0; i < 6; ++i) {
        if (!fpe_state.fpe_bools.clip_plane_enable[i] || locations.clip_planes[i] < 0) continue;
        const glm::vec4 plane(fpe_uniform.clip_planes[i]);
        if (first_upload || std::memcmp(&plane, &values.clip_planes[i], sizeof(plane)) != 0) {
            g_glFuncs.glUniform4fv(locations.clip_planes[i], 1, glm::value_ptr(plane));
            values.clip_planes[i] = plane;
        }
    }

    if (fpe_state.fpe_bools.polygon_stipple_enable && locations.polygon_stipple_rows >= 0 &&
        (first_upload || std::memcmp(fpe_uniform.polygon_stipple_rows, values.polygon_stipple_rows,
                                     sizeof(values.polygon_stipple_rows)) != 0)) {
        g_glFuncs.glUniform1uiv(locations.polygon_stipple_rows, 32, fpe_uniform.polygon_stipple_rows);
        std::memcpy(values.polygon_stipple_rows, fpe_uniform.polygon_stipple_rows,
                    sizeof(values.polygon_stipple_rows));
    }

    if ((first_upload || fpe_uniform.point_size != values.point_size) && locations.point_size >= 0) {
        g_glFuncs.glUniform1f(locations.point_size, fpe_uniform.point_size);
        values.point_size = fpe_uniform.point_size;
    }

    values.initialized = true;
}

program_key_t glstate_t::program_hash() {
    // This executes for every fixed-function draw. The caller has already
    // normalized the vertex array, so avoid two heap allocations and a second
    // normalization just to build the shader-program key.
    const auto& va = fpe_state.normalized_vpa;
    const auto& sizes = fpe_state.fpe_draw.current_data.sizes;
    auto& cache = program_hash_cache;

    bool matches = cache.valid && cache.enabled_pointers == va.enabled_pointers &&
                   cache.client_active_texture == fpe_state.client_active_texture &&
                   cache.alpha_func == fpe_state.alpha_func && cache.fog_mode == fpe_state.fog_mode &&
                   cache.fog_index == fpe_state.fog_index &&
                   cache.fog_coord_src == fpe_state.fog_coord_src &&
                   cache.shade_model == fpe_state.shade_model &&
                   cache.light_model_color_ctrl == fpe_state.light_model_color_ctrl &&
                   cache.light_model_local_viewer == fpe_state.light_model_local_viewer &&
                   cache.light_model_two_side == fpe_state.light_model_two_side &&
                   cache.color_material_face == fpe_state.color_material_face &&
                   cache.color_material_mode == fpe_state.color_material_mode;

    if (matches) {
        for (int i = 0; i < VERTEX_POINTER_COUNT; ++i) {
            const bool enabled = ((va.enabled_pointers >> i) & 1u) != 0;
            if (cache.constant_sizes.data[i] != sizes.data[i]) {
                matches = false;
                break;
            }
            if (!enabled && sizes.data[i] <= 0) continue;

            const auto& attribute = va.attributes[i];
            const auto& previous = cache.vertices[i];
            if (previous.usage != attribute.usage ||
                (enabled && (previous.size != attribute.size || previous.type != attribute.type ||
                             previous.normalized != attribute.normalized))) {
                matches = false;
                break;
            }
        }
    }

    const auto& bools = fpe_state.fpe_bools;
    const auto& previous_bools = cache.bools;
    if (matches) {
        matches = previous_bools.fog_enable == bools.fog_enable &&
                  previous_bools.lighting_enable == bools.lighting_enable &&
                  previous_bools.alpha_test_enable == bools.alpha_test_enable &&
                  previous_bools.color_material_enable == bools.color_material_enable &&
                  previous_bools.normalize_enable == bools.normalize_enable &&
                  previous_bools.rescale_normal_enable == bools.rescale_normal_enable;
    }
    if (matches) {
        for (int i = 0; i < MAX_LIGHTS; ++i) {
            if (previous_bools.light_enable[i] != bools.light_enable[i]) {
                matches = false;
                break;
            }
        }
    }
    if (matches) {
        for (int i = 0; i < MAX_TEX; ++i) {
            if (previous_bools.texture_2d_enable[i] != bools.texture_2d_enable[i] ||
                cache.texture_env_mode[i] != fpe_state.texture_env_mode[i]) {
                matches = false;
                break;
            }
            // COMBINE bakes its combiner parameters into the generated
            // source; the cache does not mirror them, so never take the
            // fast path while a COMBINE unit is live.
            if (bools.texture_2d_enable[i] && fpe_state.texture_env_mode[i] == GL_COMBINE) {
                matches = false;
                break;
            }
            // texgen modes are likewise baked into the source but not
            // mirrored in this cache.
            if (bools.texture_2d_enable[i] &&
                (bools.texture_gen_enable[i][0] || bools.texture_gen_enable[i][1] ||
                 bools.texture_gen_enable[i][2] || bools.texture_gen_enable[i][3])) {
                matches = false;
                break;
            }
        }
    }
    if (matches) return cache.hash;

    // Two independent hash passes form the 128-bit key.
    struct dual_hash_t {
        XXHash64 lo{glstate_t::s_hash_seed};
        XXHash64 hi{glstate_t::s_hash_seed2};
        void add(const void* data, uint64_t length) {
            lo.add(data, length);
            hi.add(data, length);
        }
    } hash;

    for (int i = 0; i < VERTEX_POINTER_COUNT; ++i) {
        const bool enabled = ((va.enabled_pointers >> i) & 1u) != 0;
        if (!enabled && fpe_state.fpe_draw.current_data.sizes.data[i] <= 0) continue;

        hash.add(&i, sizeof(i));
        hash.add(&enabled, sizeof(enabled));
        const auto& attr = va.attributes[i];

        if (enabled)
            hash.add(&attr.size, sizeof(attr.size));
        else
            hash.add(&fpe_state.fpe_draw.current_data.sizes.data[i],
                     sizeof(fpe_state.fpe_draw.current_data.sizes.data[i]));

        hash.add(&attr.usage, sizeof(attr.usage));
        if (enabled) {
            hash.add(&attr.type, sizeof(attr.type));
            hash.add(&attr.normalized, sizeof(attr.normalized));
        } else {
            const GLenum type = GL_FLOAT;
            hash.add(&type, sizeof(type));
        }
    }

    hash.add(&fpe_state.client_active_texture, sizeof(fpe_state.client_active_texture));
    // Canonicalize: only hash state the generator reads under current enables.
    // An app calling glAlphaFunc() while alpha test is disabled, or glFogi()
    // with fog off, would mint a distinct key that compiles a byte-identical
    // shader and forces a spurious program switch. Minecraft 1.12/1.16 both
    // set alpha_func/fog_mode/lighting model constantly with the features off.
    if (bools.alpha_test_enable) {
        hash.add(&fpe_state.alpha_func, sizeof(fpe_state.alpha_func));
    }
    if (bools.fog_enable) {
        hash.add(&fpe_state.fog_mode, sizeof(fpe_state.fog_mode));
        hash.add(&fpe_state.fog_index, sizeof(fpe_state.fog_index));
        hash.add(&fpe_state.fog_coord_src, sizeof(fpe_state.fog_coord_src));
    }
    hash.add(&fpe_state.shade_model, sizeof(fpe_state.shade_model));
    if (bools.lighting_enable) {
        hash.add(&fpe_state.light_model_color_ctrl, sizeof(fpe_state.light_model_color_ctrl));
        hash.add(&fpe_state.light_model_local_viewer, sizeof(fpe_state.light_model_local_viewer));
        hash.add(&fpe_state.light_model_two_side, sizeof(fpe_state.light_model_two_side));
    }
    if (bools.color_material_enable) {
        hash.add(&fpe_state.color_material_face, sizeof(fpe_state.color_material_face));
        hash.add(&fpe_state.color_material_mode, sizeof(fpe_state.color_material_mode));
    }

    hash.add(&fpe_state.fpe_bools, sizeof(fpe_state.fpe_bools));
    hash.add(&fpe_state.texture_env_mode, sizeof(fpe_state.texture_env_mode));
    hash.add(&fpe_state.texture_gen_mode, sizeof(fpe_state.texture_gen_mode));

    for (int i = 0; i < MAX_TEX; ++i) {
        if (!fpe_state.fpe_bools.texture_2d_enable[i] || fpe_state.texture_env_mode[i] != GL_COMBINE)
            continue;
        const auto& env = fpe_uniform.texture_env[i];
        hash.add(&env.combine_rgb, sizeof(env.combine_rgb));
        hash.add(&env.combine_alpha, sizeof(env.combine_alpha));
        hash.add(&env.source_rgb, sizeof(env.source_rgb));
        hash.add(&env.source_alpha, sizeof(env.source_alpha));
        hash.add(&env.operand_rgb, sizeof(env.operand_rgb));
        hash.add(&env.operand_alpha, sizeof(env.operand_alpha));
        hash.add(&env.rgb_scale, sizeof(env.rgb_scale));
        hash.add(&env.alpha_scale, sizeof(env.alpha_scale));
    }

    cache.valid = true;
    cache.enabled_pointers = va.enabled_pointers;
    cache.constant_sizes = sizes;
    for (int i = 0; i < VERTEX_POINTER_COUNT; ++i) {
        cache.vertices[i].size = va.attributes[i].size;
        cache.vertices[i].usage = va.attributes[i].usage;
        cache.vertices[i].type = va.attributes[i].type;
        cache.vertices[i].normalized = va.attributes[i].normalized;
    }
    cache.client_active_texture = fpe_state.client_active_texture;
    cache.alpha_func = bools.alpha_test_enable ? fpe_state.alpha_func : 0;
    cache.fog_mode = bools.fog_enable ? fpe_state.fog_mode : 0;
    cache.fog_index = bools.fog_enable ? fpe_state.fog_index : 0;
    cache.fog_coord_src = bools.fog_enable ? fpe_state.fog_coord_src : 0;
    cache.shade_model = fpe_state.shade_model;
    cache.light_model_color_ctrl = bools.lighting_enable ? fpe_state.light_model_color_ctrl : 0;
    cache.light_model_local_viewer = bools.lighting_enable ? fpe_state.light_model_local_viewer : 0;
    cache.light_model_two_side = bools.lighting_enable ? fpe_state.light_model_two_side : 0;
    cache.color_material_face = fpe_state.color_material_face;
    cache.color_material_mode = fpe_state.color_material_mode;
    cache.bools = fpe_state.fpe_bools;
    for (int i = 0; i < MAX_TEX; ++i) cache.texture_env_mode[i] = fpe_state.texture_env_mode[i];
    cache.hash = {hash.lo.hash(), hash.hi.hash()};
    return cache.hash;
}

program_t& glstate_t::get_or_generate_program(const program_key_t& key) {
    // LOG()
    if (last_program != nullptr && last_program_key == key) return *last_program;

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

    last_program_key = key;
    last_program = &it->second;
    return it->second;
}

bool glstate_t::get_vao(const program_key_t& key, GLuint* vao) {
    // LOG()
    if (fpe_vaos.find(key) == fpe_vaos.end()) {
        return false;
    }

    if (vao) *vao = fpe_vaos[key];
    return true;
}

void glstate_t::save_vao(const program_key_t& key, const GLuint vao) {
    // LOG()
    fpe_vaos[key] = vao;
}

bool glstate_t::send_vertex_attributes(const vertex_pointer_array_t& va, GLuint array_buffer,
                                       GLintptr binding_offset) {
    // LOG()

    //    auto& va = fpe_state.vertexpointer_array;
    if (!va.dirty) return false;

    bool use_separate_binding = g_glFuncs.glBindVertexBuffer != nullptr &&
                                g_glFuncs.glVertexAttribFormat != nullptr &&
                                g_glFuncs.glVertexAttribBinding != nullptr && va.stride > 0;
    if (use_separate_binding) {
        for (int i = 0; i < VERTEX_POINTER_COUNT; ++i) {
            if (((va.enabled_pointers >> i) & 1u) == 0) continue;
            const auto& attribute = va.attributes[i];
            const uintptr_t relative_offset = reinterpret_cast<uintptr_t>(attribute.pointer);
            if (attribute.size < 1 || attribute.size > 4 || attribute.type == GL_DOUBLE ||
                relative_offset > 2047u) {
                use_separate_binding = false;
                break;
            }
        }
    }

    if (use_separate_binding) {
        if (!fpe_vertex_binding_valid || fpe_vertex_binding_buffer != array_buffer ||
            fpe_vertex_binding_offset != binding_offset || fpe_vertex_binding_stride != va.stride) {
            g_glFuncs.glBindVertexBuffer(0, array_buffer, binding_offset, va.stride);
            fpe_vertex_binding_valid = true;
            fpe_vertex_binding_buffer = array_buffer;
            fpe_vertex_binding_offset = binding_offset;
            fpe_vertex_binding_stride = va.stride;
        }
    } else {
        // Legacy glVertexAttribPointer assigns its own binding state, including
        // binding zero for attribute zero. Re-establish the shared binding if a
        // later draw can use the separate-format fast path again.
        fpe_vertex_binding_valid = false;
    }

    // Physical attribute indices this draw actually accounts for. cidx() only
    // assigns an index to a slot that is enabled OR carries a constant size, so
    // when the enabled set SHRINKS between draws the indices above the new high
    // water mark belong to no slot at all - the loop below never visits them and
    // would leave them enabled with the previous layout's format. A stale index
    // then reads at its old relative offset against the new (smaller) stride,
    // i.e. past the end of every vertex. RenderDoc caught exactly this: attr 2
    // left enabled at relativeoffset 12 against a 12-byte position-only stride,
    // so every texcoord fetched the following vertex's position.
    uint64_t claimed_indices = 0;

    for (int i = 0; i < VERTEX_POINTER_COUNT; ++i) {
        bool enabled = ((va.enabled_pointers >> i) & 1);

        auto& vp = va.attributes[i];
        if (enabled) {
            const GLuint index = va.cidx(i);
            if (index < 64u) claimed_indices |= 1ull << index;
            auto& cached = fpe_vertex_attributes[index];
            if (use_separate_binding) {
                if (!cached.pointer_valid || !cached.separate_binding || cached.size != vp.size ||
                    cached.type != vp.type || cached.normalized != vp.normalized ||
                    cached.pointer != vp.pointer) {
                    const auto relative_offset = static_cast<GLuint>(reinterpret_cast<uintptr_t>(vp.pointer));
                    g_glFuncs.glVertexAttribFormat(index, vp.size, vp.type, vp.normalized, relative_offset);
                    g_glFuncs.glVertexAttribBinding(index, 0);
                    cached.pointer_valid = true;
                    cached.separate_binding = true;
                    cached.size = vp.size;
                    cached.type = vp.type;
                    cached.normalized = vp.normalized;
                    cached.stride = vp.stride;
                    cached.pointer = vp.pointer;
                }
            } else {
                const void* effective_pointer = reinterpret_cast<const void*>(
                    reinterpret_cast<uintptr_t>(vp.pointer) + static_cast<uintptr_t>(binding_offset));
                if (!cached.pointer_valid || cached.separate_binding || cached.array_buffer != array_buffer ||
                    cached.size != vp.size || cached.type != vp.type || cached.normalized != vp.normalized ||
                    cached.stride != vp.stride || cached.pointer != effective_pointer) {
                    g_glFuncs.glVertexAttribPointer(index, vp.size, vp.type, vp.normalized, vp.stride,
                                                    effective_pointer);
                    cached.pointer_valid = true;
                    cached.separate_binding = false;
                    cached.array_buffer = array_buffer;
                    cached.size = vp.size;
                    cached.type = vp.type;
                    cached.normalized = vp.normalized;
                    cached.stride = vp.stride;
                    cached.pointer = effective_pointer;
                }
            }

            if (!cached.enable_known || !cached.enabled) {
                g_glFuncs.glEnableVertexAttribArray(index);
                cached.enable_known = true;
                cached.enabled = true;
            }

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

            if (va.cidx(i) != ~0u) {
                if (va.cidx(i) < 64u) claimed_indices |= 1ull << va.cidx(i);
                auto& cached = fpe_vertex_attributes[va.cidx(i)];
                if (!cached.enable_known || cached.enabled) {
                    g_glFuncs.glDisableVertexAttribArray(va.cidx(i));
                    cached.enable_known = true;
                    cached.enabled = false;
                }
            }
        }
    }

    // Retire indices the previous layout enabled that no slot claims now. Only
    // indices we positively know are enabled are touched: an untracked index may
    // be past GL_MAX_VERTEX_ATTRIBS (VERTEX_POINTER_COUNT is 7 + MAX_TEX = 23,
    // above the usual 16) and disabling it would raise GL_INVALID_VALUE.
    for (GLuint index = 0; index < VERTEX_POINTER_COUNT; ++index) {
        if (index < 64u && ((claimed_indices >> index) & 1ull)) continue;
        auto& cached = fpe_vertex_attributes[index];
        if (!cached.enable_known || !cached.enabled) continue;
        g_glFuncs.glDisableVertexAttribArray(index);
        cached.enabled = false;
        // The format left behind describes the retired layout; force a
        // re-specify if this index is claimed again later.
        cached.pointer_valid = false;
    }

    return true;
}
