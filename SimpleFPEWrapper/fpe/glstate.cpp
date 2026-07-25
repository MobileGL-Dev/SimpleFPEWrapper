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
    back_material_ambient = location("BackMaterialAmbient");
    back_material_diffuse = location("BackMaterialDiffuse");
    back_material_emission = location("BackMaterialEmission");
    fog_color = location("FogColor");
    fog_density = location("FogDensity");
    fog_start = location("FogStart");
    fog_end = location("FogEnd");
    alpha_ref = location("alpharef");

    for (int i = 0; i < MAX_LIGHTS; ++i) {
        light_ambient[i] = location(std::format("Light{}Ambient", i));
        light_diffuse[i] = location(std::format("Light{}Diffuse", i));
        light_position[i] = location(std::format("Light{}Position", i));
    }
    for (int i = 0; i < MAX_TEX; ++i) {
        sampler[i] = location(std::format("Sampler{}", i));
        texture_matrix[i] = location(std::format("TexMat{}", i));
        texture_env_color[i] = location(std::format("TexEnvColor{}", i));
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

        const auto send_material = [&send_vec4](int index, GLint ambient, GLint diffuse, GLint emission,
                                                const material_t& material, program_uniform_values_t& values) {
            send_vec4(ambient, material.ambient, values.material_ambient[index]);
            send_vec4(diffuse, material.diffuse, values.material_diffuse[index]);
            send_vec4(emission, material.emission, values.material_emission[index]);
        };
        send_material(0, locations.front_material_ambient, locations.front_material_diffuse,
                      locations.front_material_emission, fpe_uniform.materials[0], values);
        if (fpe_state.light_model_two_side) {
            send_material(1, locations.back_material_ambient, locations.back_material_diffuse,
                          locations.back_material_emission, fpe_uniform.materials[1], values);
        }

        for (int i = 0; i < MAX_LIGHTS; ++i) {
            if (!fpe_state.fpe_bools.light_enable[i]) continue;

            const auto& light = fpe_uniform.lights[i];
            send_vec4(locations.light_ambient[i], light.ambient, values.light_ambient[i]);
            send_vec4(locations.light_diffuse[i], light.diffuse, values.light_diffuse[i]);
            send_vec4(locations.light_position[i], light.position, values.light_position[i]);
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

    values.initialized = true;
}

uint64_t glstate_t::program_hash() {
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
        }
    }
    if (matches) return cache.hash;

    XXHash64 hash(s_hash_seed);

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
    cache.alpha_func = fpe_state.alpha_func;
    cache.fog_mode = fpe_state.fog_mode;
    cache.fog_index = fpe_state.fog_index;
    cache.fog_coord_src = fpe_state.fog_coord_src;
    cache.shade_model = fpe_state.shade_model;
    cache.light_model_color_ctrl = fpe_state.light_model_color_ctrl;
    cache.light_model_local_viewer = fpe_state.light_model_local_viewer;
    cache.light_model_two_side = fpe_state.light_model_two_side;
    cache.color_material_face = fpe_state.color_material_face;
    cache.color_material_mode = fpe_state.color_material_mode;
    cache.bools = fpe_state.fpe_bools;
    for (int i = 0; i < MAX_TEX; ++i) cache.texture_env_mode[i] = fpe_state.texture_env_mode[i];
    cache.hash = hash.hash();
    return cache.hash;
}

program_t& glstate_t::get_or_generate_program(const uint64_t key) {
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

    for (int i = 0; i < VERTEX_POINTER_COUNT; ++i) {
        bool enabled = ((va.enabled_pointers >> i) & 1);

        auto& vp = va.attributes[i];
        if (enabled) {
            const GLuint index = va.cidx(i);
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
                auto& cached = fpe_vertex_attributes[va.cidx(i)];
                if (!cached.enable_known || cached.enabled) {
                    g_glFuncs.glDisableVertexAttribArray(va.cidx(i));
                    cached.enable_known = true;
                    cached.enabled = false;
                }
            }
        }
    }

    return true;
}
