// SimpleFPEWrapper - SimpleFPEWrapper/fpe/vertexpointerarray.cpp
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

#include "types.h"

void vertex_pointer_array_t::reset() {
    starting_pointer = NULL;
    stride = 0;
    enabled_pointers = 0;
    dirty = false;
    buffer_based = false;
    memset(&attributes, 0, sizeof(attributes));
}

// Split into starting pointer & offset into buffer per pointer
vertex_pointer_array_t vertex_pointer_array_t::normalize() {
    vertex_pointer_array_t that = *this;
    int first_va_idx = -1;

    // Find starting pointer
    for (int i = 0; i < VERTEX_POINTER_COUNT; ++i) {
        bool enabled = ((enabled_pointers >> i) & 1);
        if (!enabled) continue;

        first_va_idx = i;
        break;
    }

    if (first_va_idx < 0) {
        that.starting_pointer = nullptr;
        that.stride = 0;
        return that;
    }

    if (stride == 0) that.stride = attributes[first_va_idx].stride;

    // if not valid starting pointer
    if (!(that.stride != 0 && that.starting_pointer != 0 &&
          (uintptr_t)that.starting_pointer > (uintptr_t)that.stride)) {
        that.starting_pointer = attributes[first_va_idx].pointer;
    }

    // stride==0 && stride in pointer == 0
    // => tightly packed, infer stride from offset below
    bool do_calc_stride = (that.stride == 0);

    // Adjust pointer offsets according to starting pointer
    // Getting actual stride if stride==0
    for (int i = 0; i < VERTEX_POINTER_COUNT; ++i) {
        bool enabled = ((enabled_pointers >> i) & 1);
        if (!enabled) continue;

        auto& vp = that.attributes[i];

        // check if pointer is a pointer rather than an offset
        if (that.stride > 0 && (uint64_t)vp.pointer > (uint64_t)that.stride)
            vp.pointer = (const void*)((uintptr_t)vp.pointer - (uintptr_t)that.starting_pointer);

        if (do_calc_stride)
            that.stride = std::max((uint64_t)stride, (uint64_t)vp.pointer + vp.size * type_size(vp.type));
    }

    // Overwrite `stride` in pointers
    for (int i = 0; i < VERTEX_POINTER_COUNT; ++i) {
        bool enabled = ((enabled_pointers >> i) & 1);
        if (!enabled) continue;

        auto& vp = that.attributes[i];
        vp.stride = that.stride;
    }
    return that;
}

void vertex_pointer_array_t::generate_compressed_index(GLint constant_sizes[VERTEX_POINTER_COUNT]) {
    // Should set the array to -1, or ~0u
    memset(compressed_index, -1, sizeof(compressed_index));

    GLuint index_count = 0;
    // Populate `compressed_index`
    // Save attribute index space for some devices
    for (int i = 0; i < VERTEX_POINTER_COUNT; ++i) {
        bool enabled = ((enabled_pointers >> i) & 1);
        if (enabled || constant_sizes[i] > 0) {
            // An attribute is in use,
            // should generate an index entry
            compressed_index[i] = index_count;
            ++index_count;
        }
    }
}
