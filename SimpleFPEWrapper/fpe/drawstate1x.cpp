// SimpleFPEWrapper - SimpleFPEWrapper/fpe/drawstate1x.cpp
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

#include "types.h"
#include <cstring>

#define DEBUG 0

void fixed_function_draw_state_t::reset() {
    primitive = GL_NONE;
    vertex_count = 0;
    vb.clear();
}

void fixed_function_draw_state_t::set_attribute_size(int slot, GLint requested) {
    GLint& stored = current_data.sizes.data[slot];
    if (primitive == GL_NONE || vertex_count == 0) {
        stored = requested;
        return;
    }
    if (requested <= stored) return; // grow-only while collecting

    // Repack every collected vertex from the old layout to the new one.
    // advance() only ever packs slots 0-2 (vertex/normal/color) and 7+
    // (texcoords), in ascending slot order.
    const auto packed_size = [&](int s) -> GLint {
        const GLint sz = current_data.sizes.data[s];
        return (s <= 2 || s >= 7) && sz > 0 ? sz : 0;
    };
    size_t old_stride = 0;
    for (int s = 0; s < VERTEX_POINTER_COUNT; ++s) old_stride += (size_t)packed_size(s);
    if (old_stride == 0 || vb.size() < old_stride * vertex_count) {
        stored = requested;
        return;
    }

    // Backfill value for vertices collected before this attribute existed:
    // its current value right now (the caller has not overwritten it yet).
    const GLfloat* previous_value = nullptr;
    if (slot == 0)
        previous_value = glm::value_ptr(current_data.vertex);
    else if (slot == 1)
        previous_value = glm::value_ptr(current_data.normal);
    else if (slot == 2)
        previous_value = glm::value_ptr(current_data.color);
    else
        previous_value = glm::value_ptr(current_data.texcoord[slot - 7]);
    static constexpr GLfloat kComponentDefaults[4] = {0.0f, 0.0f, 0.0f, 1.0f};

    std::vector<GLfloat> repacked;
    repacked.reserve((old_stride + (size_t)(requested - stored)) * vertex_count);
    for (size_t v = 0; v < vertex_count; ++v) {
        const GLfloat* src = vb.data() + v * old_stride;
        size_t consumed = 0;
        for (int s = 0; s < VERTEX_POINTER_COUNT; ++s) {
            const GLint sz = packed_size(s);
            if (s == slot) {
                for (GLint c = 0; c < requested; ++c) {
                    if (c < sz)
                        repacked.push_back(src[consumed + c]);
                    else if (sz == 0)
                        repacked.push_back(previous_value[c]);
                    else
                        repacked.push_back(kComponentDefaults[c]);
                }
            } else {
                for (GLint c = 0; c < sz; ++c) repacked.push_back(src[consumed + c]);
            }
            consumed += (size_t)sz;
        }
    }
    vb = std::move(repacked);
    stored = requested;
}

void fixed_function_draw_state_t::advance() {
    ++vertex_count;

    const auto& sizes = current_data.sizes;
    size_t float_count = 0;
    for (GLint count : sizes.data) {
        if (count > 0) float_count += static_cast<size_t>(count);
    }

    const size_t old_size = vb.size();
    vb.resize(old_size + float_count);
    GLfloat* output = vb.data() + old_size;
    const auto append = [&output](const GLfloat* values, GLint count) {
        if (count <= 0) return;
        const size_t byte_count = static_cast<size_t>(count) * sizeof(GLfloat);
        std::memcpy(output, values, byte_count);
        output += count;
    };

    // vertex
    if (sizes.vertex_size > 0) {
        append(glm::value_ptr(current_data.vertex), sizes.vertex_size);
    }

    // normal
    if (sizes.normal_size > 0) {
        append(glm::value_ptr(current_data.normal), sizes.normal_size);
    }

    // color
    if (sizes.color_size > 0) {
        append(glm::value_ptr(current_data.color), sizes.color_size);
    }

    // texcoord
    for (GLint i = 0; i < MAX_TEX; ++i) {
        if (sizes.texcoord_size[i] > 0) {
            append(glm::value_ptr(current_data.texcoord[i]), sizes.texcoord_size[i]);
        }
    }

    // LOG_D("advance(): vertexcount = %d, vbsize = %d", vertex_count, vb.size() * sizeof(GLfloat))
}

void fixed_function_draw_state_t::compile_vertexattrib(vertex_pointer_array_t& va) const {
    va.reset();

    va.dirty = true;
    va.buffer_based = false;

    const auto& sizes = current_data.sizes;
    uintptr_t offset = 0;

    // vertex
    if (sizes.vertex_size > 0) {
        va.enabled_pointers |= vp_mask(GL_VERTEX_ARRAY);

        va.attributes[vp2idx(GL_VERTEX_ARRAY)] = {
            .size = sizes.vertex_size,
            .usage = GL_VERTEX_ARRAY,
            .type = GL_FLOAT,
            .normalized = GL_FALSE,
            .stride = 0,
            .pointer = (const void*)offset,
            //                .varying = true
        };
        offset += sizes.vertex_size * sizeof(GLfloat);
    }

    // normal
    if (sizes.normal_size > 0) {
        va.enabled_pointers |= vp_mask(GL_NORMAL_ARRAY);

        va.attributes[vp2idx(GL_NORMAL_ARRAY)] = {
            .size = sizes.normal_size,
            .usage = GL_NORMAL_ARRAY,
            .type = GL_FLOAT,
            .normalized = GL_FALSE,
            .stride = 0,
            .pointer = (const void*)offset,
            //                .varying = true
        };
        offset += sizes.normal_size * sizeof(GLfloat);
    }

    // color
    if (sizes.color_size > 0) {
        va.enabled_pointers |= vp_mask(GL_COLOR_ARRAY);
        va.attributes[vp2idx(GL_COLOR_ARRAY)] = {
            .size = sizes.color_size,
            .usage = GL_COLOR_ARRAY,
            .type = GL_FLOAT,
            .normalized = GL_FALSE,
            .stride = 0,
            .pointer = (const void*)offset,
            //                .varying = true
        };
        offset += sizes.color_size * sizeof(GLfloat);
    }

    // texcoord
    for (GLint i = 0; i < MAX_TEX; ++i) {
        if (sizes.texcoord_size[i] > 0) {
            // TODO: fix vp_mask()/vp2idx(), make it adapt to here
            va.enabled_pointers |= (1 << (7 + i));
            va.attributes[7 + i] = {
                .size = sizes.texcoord_size[i],
                .usage = GL_TEXTURE_COORD_ARRAY,
                .type = GL_FLOAT,
                .normalized = GL_FALSE,
                .stride = 0,
                .pointer = (const void*)offset,
                //                    .varying = true
            };
            offset += sizes.texcoord_size[i] * sizeof(GLfloat);
        }
    }

    va.stride = (GLsizei)offset;
}
