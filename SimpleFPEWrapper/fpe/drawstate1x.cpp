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
