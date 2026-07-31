// SimpleFPEWrapper - SimpleFPEWrapper/fpe/drawstate1x.cpp
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

#include "types.h"
#include <atomic>
#include <cstring>

#define DEBUG 0

uint64_t sfpewNextSizesEpoch() {
    // Relaxed is enough: the value only has to be distinct from every other
    // stamp, and it is always published together with the sizes it describes
    // through the same (per-context) state the reader already owns.
    static std::atomic<uint64_t> counter{0};
    return counter.fetch_add(1, std::memory_order_relaxed) + 1;
}

void fixed_function_draw_state_t::reset() {
    primitive = kNoPrimitive;
    vertex_count = 0;
    vb.clear();
    edge_flags.clear();
    repacked = false;
}

void fixed_function_draw_state_t::set_attribute_size(int slot, GLint requested) {
    GLint& stored = current_data.sizes.data[slot];
    if (primitive == kNoPrimitive || vertex_count == 0) {
        // Only a real change earns a new stamp: every vertex re-declares the
        // sizes of the attributes it sets, and stamping those no-op writes
        // would rebuild the packing layout for the first vertex of every
        // single Begin/End batch.
        if (stored != requested) {
            stored = requested;
            current_data.sizes_epoch = sfpewNextSizesEpoch();
        }
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

    repacked = true;
    std::vector<GLfloat> repacked_vb;
    repacked_vb.reserve((old_stride + (size_t)(requested - stored)) * vertex_count);
    for (size_t v = 0; v < vertex_count; ++v) {
        const GLfloat* src = vb.data() + v * old_stride;
        size_t consumed = 0;
        for (int s = 0; s < VERTEX_POINTER_COUNT; ++s) {
            const GLint sz = packed_size(s);
            if (s == slot) {
                for (GLint c = 0; c < requested; ++c) {
                    if (c < sz)
                        repacked_vb.push_back(src[consumed + c]);
                    else if (sz == 0)
                        repacked_vb.push_back(previous_value[c]);
                    else
                        repacked_vb.push_back(kComponentDefaults[c]);
                }
            } else {
                for (GLint c = 0; c < sz; ++c) repacked_vb.push_back(src[consumed + c]);
            }
            consumed += (size_t)sz;
        }
    }
    vb = std::move(repacked_vb);
    stored = requested;
    current_data.sizes_epoch = sfpewNextSizesEpoch();
}

void fixed_function_draw_state_t::rebuild_packed_layout() {
    packed_span_count = 0;
    packed_floats = 0;
    const auto* base = reinterpret_cast<const GLfloat*>(&current_data);
    const auto add = [&](const GLfloat* src, GLint count) {
        if (count <= 0) return;
        const auto offset = static_cast<uint16_t>(src - base);
        // Merge with the previous span when the source is contiguous; the
        // destination always is.
        if (packed_span_count > 0) {
            auto& last = packed_spans[packed_span_count - 1];
            if (last.src_offset + last.count == offset) {
                last.count = static_cast<uint16_t>(last.count + count);
                packed_floats += static_cast<size_t>(count);
                return;
            }
        }
        packed_spans[packed_span_count++] = {offset, static_cast<uint16_t>(count)};
        packed_floats += static_cast<size_t>(count);
    };

    const auto& sizes = current_data.sizes;
    add(glm::value_ptr(current_data.vertex), sizes.vertex_size);
    add(glm::value_ptr(current_data.normal), sizes.normal_size);
    add(glm::value_ptr(current_data.color), sizes.color_size);
    // Slots 5 and 6. compile_vertexattrib() sizes EVERY slot it sees, so a
    // slot that is sized but never packed here shifts every later attribute
    // (fog coord and secondary color both land before the texcoords).
    add(&current_data.fog_coord, sizes.fog_size > 0 ? 1 : 0);
    add(glm::value_ptr(current_data.secondary_color), sizes.secondary_color_size);
    for (GLint i = 0; i < MAX_TEX; ++i) {
        add(glm::value_ptr(current_data.texcoord[i]), sizes.texcoord_size[i]);
    }
    packed_layout_sizes = sizes;
    packed_layout_epoch = current_data.sizes_epoch;
}

void fixed_function_draw_state_t::advance() {
    ++vertex_count;

    // Edge flags, collected only once they can change the picture. While
    // every vertex so far has had the default GL_TRUE the vector stays
    // empty, which the wireframe expansion reads as "all boundary"; the
    // first cleared flag backfills the run's history and switches tracking
    // on. Cost until then is this one compare against a hot byte.
    if (!edge_flags.empty()) {
        edge_flags.push_back(current_data.edge_flag);
    } else if (current_data.edge_flag == GL_FALSE) {
        edge_flags.assign(vertex_count - 1u, 1u);
        edge_flags.push_back(0u);
    }

    // One integer compare per vertex. The stamp changes only when a write
    // actually changes the size block, so a steady layout - every vertex of
    // every batch in a draw loop - costs this compare and nothing else. It
    // also revalidates after wholesale sizes overwrites (attrib-stack
    // restore, immediate-draw guards), which take a fresh stamp.
    if (packed_span_count < 0 || packed_layout_epoch != current_data.sizes_epoch) {
        rebuild_packed_layout();
    }

    // resize() + a scalar copy loop, deliberately: an immediate-mode vertex
    // is a handful of floats, and vector::insert routes those few bytes
    // through memmove, whose call overhead measured worse than the zeroing
    // resize does (tinybatch 0.37 -> 0.42 us/batch when this used insert).
    const size_t old_size = vb.size();
    vb.resize(old_size + packed_floats);
    GLfloat* output = vb.data() + old_size;
    const auto* base = reinterpret_cast<const GLfloat*>(&current_data);
    for (int s = 0; s < packed_span_count; ++s) {
        const auto [src_offset, count] = packed_spans[s];
        const GLfloat* src = base + src_offset;
        for (uint16_t c = 0; c < count; ++c) output[c] = src[c];
        output += count;
    }
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

    // fog coord (slot 5) - declaration order must match advance()'s packing
    if (sizes.fog_size > 0) {
        va.enabled_pointers |= vp_mask(GL_FOG_COORD_ARRAY);
        va.attributes[vp2idx(GL_FOG_COORD_ARRAY)] = {
            .size = 1,
            .usage = GL_FOG_COORD_ARRAY,
            .type = GL_FLOAT,
            .normalized = GL_FALSE,
            .stride = 0,
            .pointer = (const void*)offset,
        };
        offset += sizeof(GLfloat);
    }

    // secondary color (slot 6)
    if (sizes.secondary_color_size > 0) {
        va.enabled_pointers |= vp_mask(GL_SECONDARY_COLOR_ARRAY);
        va.attributes[vp2idx(GL_SECONDARY_COLOR_ARRAY)] = {
            .size = sizes.secondary_color_size,
            .usage = GL_SECONDARY_COLOR_ARRAY,
            .type = GL_FLOAT,
            .normalized = GL_FALSE,
            .stride = 0,
            .pointer = (const void*)offset,
        };
        offset += sizes.secondary_color_size * sizeof(GLfloat);
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
