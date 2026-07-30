// SimpleFPEWrapper - SimpleFPEWrapper/drawing.cpp
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

#include "init.h"
#include "log.h"

#include "fpe/fpe.hpp"
#include "fpe/list.h"
#include "fpe/drawing1x.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <vector>

namespace {

void drawArraysNow(GLenum mode, GLint first, GLsizei count, bool forceFixedFunction,
                   GLint arrayBufferOverride = -1);

size_t componentSize(GLenum type) {
    switch (type) {
    case GL_BYTE:
    case GL_UNSIGNED_BYTE:
        return 1;
    case GL_SHORT:
    case GL_UNSIGNED_SHORT:
    case GL_HALF_FLOAT:
        return 2;
    case GL_INT:
    case GL_UNSIGNED_INT:
    case GL_FLOAT:
    case GL_FIXED:
        return 4;
    case GL_DOUBLE:
        return 8;
    default:
        return 0;
    }
}

bool checkedAdd(size_t lhs, size_t rhs, size_t* result) {
    if (lhs > std::numeric_limits<size_t>::max() - rhs) return false;
    *result = lhs + rhs;
    return true;
}

bool checkedMultiply(size_t lhs, size_t rhs, size_t* result) {
    if (lhs != 0 && rhs > std::numeric_limits<size_t>::max() / lhs) return false;
    *result = lhs * rhs;
    return true;
}

bool alignSize(size_t value, size_t alignment, size_t* result) {
    size_t withPadding = 0;
    if (alignment == 0 || !checkedAdd(value, alignment - 1, &withPadding)) return false;
    *result = (withPadding / alignment) * alignment;
    return true;
}

struct array_buffer_binding_guard_t {
    GLint binding = 0;

    array_buffer_binding_guard_t() { g_glFuncs.glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &binding); }

    ~array_buffer_binding_guard_t() { g_glFuncs.glBindBuffer(GL_ARRAY_BUFFER, binding); }

    array_buffer_binding_guard_t(const array_buffer_binding_guard_t&) = delete;
    array_buffer_binding_guard_t& operator=(const array_buffer_binding_guard_t&) = delete;
};

constexpr size_t kDisplayListArenaCapacity = 64u * 1024u * 1024u;

struct display_list_vertex_allocation_t {
    GLuint buffer = 0;
    size_t offset = 0;
    size_t size = 0;
    uint64_t generation = 0;
};

class display_list_vertex_arena_t {
    struct block_t {
        size_t offset = 0;
        size_t size = 0;
    };

    struct retired_block_t {
        block_t block;
        GLsync fence = nullptr;
    };

public:
    bool allocate(const void* data, size_t size, size_t stride,
                  display_list_vertex_allocation_t* allocation) {
        if (allocation == nullptr || data == nullptr || size == 0 || stride == 0) {
            return false;
        }

        // Runs under a recording/draw entry whose strict resolve refreshed
        // the snapshot (docs/context-model.md).
        const EGLContext currentContext = (EGLContext)glstate_t::cached_context();
        if (currentContext == EGL_NO_CONTEXT) return false;
        if (context != currentContext) resetForContext(currentContext);
        if (!ensureStorage()) return false;

        reapRetired(false);

        size_t offset = 0;
        if (!allocateFreeBlock(size, stride, &offset)) {
            if (!allocateTail(size, stride, &offset)) {
                // A full arena is exceptional. Finish once so regions retired
                // by rebuilt/deleted lists can be safely reused; active list
                // allocations remain untouched.
                reapRetired(true);
                if (!allocateFreeBlock(size, stride, &offset) &&
                    !allocateTail(size, stride, &offset)) {
                    return false;
                }
            }
        }

        // Upload through GL_COPY_WRITE_BUFFER: not part of vertex-array state,
        // so no binding guard is needed and no shadow can observe it.
        g_glFuncs.glBindBuffer(GL_COPY_WRITE_BUFFER, buffer);
        g_glFuncs.glBufferSubData(GL_COPY_WRITE_BUFFER, static_cast<GLintptr>(offset),
                                  static_cast<GLsizeiptr>(size), data);
        g_glFuncs.glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
        allocation->buffer = buffer;
        allocation->offset = offset;
        allocation->size = size;
        allocation->generation = generation;
        return true;
    }

    bool isCurrent(const display_list_vertex_allocation_t& allocation) const {
        return allocation.buffer != 0 && allocation.buffer == buffer &&
               allocation.generation == generation && storageReady;
    }

    void release(display_list_vertex_allocation_t* allocation) {
        if (allocation == nullptr || allocation->buffer == 0) return;

        const display_list_vertex_allocation_t old = *allocation;
        *allocation = {};
        if (!isCurrent(old) || (EGLContext)glstate_t::cached_context() != context) {
            return;
        }

        GLsync fence = nullptr;
        if (g_glFuncs.glFenceSync != nullptr && g_glFuncs.glClientWaitSync != nullptr &&
            g_glFuncs.glDeleteSync != nullptr) {
            fence = g_glFuncs.glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        }
        if (fence != nullptr) {
            retired.push_back({{old.offset, old.size}, fence});
        }
        // Without sync objects, conservatively leave the region unavailable.
        // glBufferSubData into a region an earlier draw still reads would be
        // implicitly synchronized by the driver, but a stall there is worse
        // than losing the bytes; the arena is large and lists are long-lived.
    }

private:
    static size_t alignTo(size_t value, size_t alignment) {
        const size_t remainder = value % alignment;
        return remainder == 0 ? value : value + alignment - remainder;
    }

    void resetForContext(EGLContext currentContext) {
        context = currentContext;
        buffer = 0;
        storageReady = false;
        tail = 0;
        storageAttempted = false;
        freeBlocks.clear();
        retired.clear();
        ++generation;
    }

    // Device-local storage, written once per allocation with glBufferSubData.
    // Display-list geometry is written once and replayed many times, so a
    // persistent-coherent CPU mapping is the WRONG residency for it: coherent
    // storage lives in host-visible memory, and every replay made the GPU pull
    // its vertices across the bus while the driver's per-submit bookkeeping
    // for the mapped buffer grew the ioctl cost ~4x (measured on NVIDIA:
    // 36.4 -> 5.1 us/chunk for the MC 1.12 chunk display-list shape just by
    // leaving the mapping out; same submission count, 185 -> 112 us/ioctl).
    bool ensureStorage() {
        if (storageReady) return true;
        if (storageAttempted) return false;
        storageAttempted = true;

        if (g_glFuncs.glGenBuffers == nullptr || g_glFuncs.glDeleteBuffers == nullptr ||
            g_glFuncs.glBufferData == nullptr || g_glFuncs.glBufferSubData == nullptr ||
            g_glFuncs.glBindBuffer == nullptr) {
            return false;
        }

        g_glFuncs.glGenBuffers(1, &buffer);
        sfpewNoteInternalBuffer(buffer);
        if (buffer == 0) return false;
        g_glFuncs.glBindBuffer(GL_COPY_WRITE_BUFFER, buffer);
        // Some drivers report GL_OUT_OF_MEMORY only via glGetError; a failed
        // allocation surfaces later as a failed SubData/draw, and the captured
        // draw's dedicated-buffer fallback covers that.
        g_glFuncs.glBufferData(GL_COPY_WRITE_BUFFER,
                               static_cast<GLsizeiptr>(kDisplayListArenaCapacity), nullptr,
                               GL_STATIC_DRAW);
        g_glFuncs.glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
        storageReady = true;
        return true;
    }

    bool allocateTail(size_t size, size_t alignment, size_t* offset) {
        const size_t aligned = alignTo(tail, alignment);
        if (aligned > kDisplayListArenaCapacity ||
            size > kDisplayListArenaCapacity - aligned) {
            return false;
        }
        *offset = aligned;
        tail = aligned + size;
        return true;
    }

    bool allocateFreeBlock(size_t size, size_t alignment, size_t* offset) {
        for (size_t i = 0; i < freeBlocks.size(); ++i) {
            const block_t block = freeBlocks[i];
            const size_t aligned = alignTo(block.offset, alignment);
            if (aligned < block.offset || aligned - block.offset > block.size ||
                size > block.size - (aligned - block.offset)) {
                continue;
            }

            freeBlocks.erase(freeBlocks.begin() + static_cast<ptrdiff_t>(i));
            if (aligned > block.offset) freeBlocks.push_back({block.offset, aligned - block.offset});
            const size_t end = aligned + size;
            const size_t blockEnd = block.offset + block.size;
            if (end < blockEnd) freeBlocks.push_back({end, blockEnd - end});
            coalesceFreeBlocks();
            *offset = aligned;
            return true;
        }
        return false;
    }

    void addFreeBlock(block_t block) {
        if (block.size == 0) return;
        freeBlocks.push_back(block);
        coalesceFreeBlocks();
    }

    void coalesceFreeBlocks() {
        std::sort(freeBlocks.begin(), freeBlocks.end(),
                  [](const block_t& lhs, const block_t& rhs) { return lhs.offset < rhs.offset; });
        size_t output = 0;
        for (const block_t& block : freeBlocks) {
            if (output != 0 &&
                freeBlocks[output - 1].offset + freeBlocks[output - 1].size >= block.offset) {
                auto& previous = freeBlocks[output - 1];
                previous.size = std::max(previous.offset + previous.size, block.offset + block.size) -
                                previous.offset;
            } else {
                freeBlocks[output++] = block;
            }
        }
        freeBlocks.resize(output);
    }

    void reapRetired(bool waitForAll) {
        if (retired.empty() || g_glFuncs.glClientWaitSync == nullptr ||
            g_glFuncs.glDeleteSync == nullptr) {
            return;
        }
        if (waitForAll && g_glFuncs.glFinish != nullptr) g_glFuncs.glFinish();

        size_t output = 0;
        for (auto& entry : retired) {
            const GLenum result = g_glFuncs.glClientWaitSync(entry.fence, 0, 0);
            if (result == GL_ALREADY_SIGNALED || result == GL_CONDITION_SATISFIED) {
                g_glFuncs.glDeleteSync(entry.fence);
                addFreeBlock(entry.block);
            } else {
                retired[output++] = entry;
            }
        }
        retired.resize(output);
    }

    EGLContext context = EGL_NO_CONTEXT;
    GLuint buffer = 0;
    bool storageReady = false;
    size_t tail = 0;
    uint64_t generation = 0;
    bool storageAttempted = false;
    std::vector<block_t> freeBlocks;
    std::vector<retired_block_t> retired;
};

display_list_vertex_arena_t displayListVertexArena;

struct wrapper_client_state_guard_t {
    vertex_pointer_array_t vertexPointerArray = g_glstate_c.fpe_state.vertexpointer_array;
    vertex_pointer_array_t normalizedVertexPointerArray = g_glstate_c.fpe_state.normalized_vpa;
    GLenum clientActiveTexture = g_glstate_c.fpe_state.client_active_texture;

    wrapper_client_state_guard_t() = default;

    ~wrapper_client_state_guard_t() {
        g_glstate_c.fpe_state.vertexpointer_array = vertexPointerArray;
        g_glstate_c.fpe_state.normalized_vpa = normalizedVertexPointerArray;
        g_glstate_c.fpe_state.client_active_texture = clientActiveTexture;
    }

    wrapper_client_state_guard_t(const wrapper_client_state_guard_t&) = delete;
    wrapper_client_state_guard_t& operator=(const wrapper_client_state_guard_t&) = delete;
};

struct captured_attribute_t {
    bool enabled = false;
    size_t elementSize = 0;
    size_t sourceStride = 0;
    size_t packedOffset = 0;
    uintptr_t sourcePointer = 0;
    GLuint sourceBuffer = 0;
};

class captured_draw_arrays_cmd_t final : public GLCmd {
public:
    captured_draw_arrays_cmd_t(GLenum mode, GLint first, GLsizei count, const vertex_pointer_array_t& source,
                               GLenum clientActiveTexture)
        : mode(mode), count(count), clientActiveTexture(clientActiveTexture) {
        valid = capture(first, source);
    }

    ~captured_draw_arrays_cmd_t() override {
        // Cold path (list deletion/re-record): one strict resolve up front -
        // both the arena release and the cache scrub below compare against
        // the snapshot, and destruction can run from entries with no anchor.
        auto& gs = g_glstate;
        displayListVertexArena.release(&arenaAllocation);
        if (vertexBuffer == 0 || g_glFuncs.glDeleteBuffers == nullptr) return;
        if ((EGLContext)glstate_t::cached_context() == EGL_NO_CONTEXT) return;

        // Deleting a buffer clears backend VAO/binding references. Scrub the
        // matching wrapper cache as well so a subsequently recycled GL name
        // cannot make send_vertex_attributes incorrectly skip its rebind.
        if (gs.fpe_vertex_binding_valid &&
            gs.fpe_vertex_binding_buffer == vertexBuffer) {
            gs.fpe_vertex_binding_valid = false;
        }
        for (auto& attribute : gs.fpe_vertex_attributes) {
            if (!attribute.separate_binding && attribute.array_buffer == vertexBuffer) {
                attribute.pointer_valid = false;
            }
        }
        sfpewForgetInternalBuffer(vertexBuffer);
        g_glFuncs.glDeleteBuffers(1, &vertexBuffer);
    }

    bool isValid() const { return valid; }
    bool isCapturedDraw() const override { return true; }
    bool bakePositionTranslation(const glm::vec3& translation) override {
        if (!valid || arenaAllocation.buffer != 0 || vertexBuffer != 0 || vertexBufferUploaded) {
            return false;
        }

        const int positionIndex = vp2idx(GL_VERTEX_ARRAY);
        if (((layout.enabled_pointers >> positionIndex) & 1u) == 0) {
            return false;
        }

        const auto& position = layout.attributes[positionIndex];
        if (position.type != GL_FLOAT || position.size < 3 || layout.stride <= 0 ||
            packedOffsets[positionIndex] + sizeof(GLfloat) * 3u >
                static_cast<size_t>(layout.stride)) {
            return false;
        }

        for (GLsizei vertex = 0; vertex < count; ++vertex) {
            auto* components = reinterpret_cast<GLfloat*>(
                vertexData.data() + static_cast<size_t>(vertex) * layout.stride +
                packedOffsets[positionIndex]);
            components[0] += translation.x;
            components[1] += translation.y;
            components[2] += translation.z;
        }
        return true;
    }

    const GLCmd* capturedDrawForBatch(glm::mat4* transform) const override {
        if (transform != nullptr) *transform = glm::mat4(1.0f);
        return this;
    }

    bool tryMerge(const GLCmd& nextCommand) override {
        // A GLFuncCmd between two captured draws prevents this path. With no
        // intervening command, identical array layouts can share one packed
        // vertex block. Restrict merging to primitive modes whose topology
        // cannot connect vertices across the original draw boundary.
        const auto* next = dynamic_cast<const captured_draw_arrays_cmd_t*>(&nextCommand);
        if (next == nullptr || !valid || !next->valid || mode != next->mode ||
            clientActiveTexture != next->clientActiveTexture || !hasIndependentPrimitives() ||
            !next->hasIndependentPrimitives() ||
            count > std::numeric_limits<GLsizei>::max() - next->count ||
            arenaAllocation.buffer != 0 || next->arenaAllocation.buffer != 0 || vertexBuffer != 0 ||
            next->vertexBuffer != 0 || vertexBufferUploaded || next->vertexBufferUploaded ||
            layout.enabled_pointers != next->layout.enabled_pointers || layout.stride != next->layout.stride ||
            packedOffsets != next->packedOffsets) {
            return false;
        }

        for (int i = 0; i < VERTEX_POINTER_COUNT; ++i) {
            if (((layout.enabled_pointers >> i) & 1u) == 0) continue;
            const auto& left = layout.attributes[i];
            const auto& right = next->layout.attributes[i];
            if (left.size != right.size || left.usage != right.usage || left.type != right.type ||
                left.normalized != right.normalized || left.stride != right.stride ||
                left.pointer != right.pointer) {
                return false;
            }
        }

        size_t mergedSize = 0;
        if (!checkedAdd(vertexData.size(), next->vertexData.size(), &mergedSize)) return false;
        const size_t oldSize = vertexData.size();
        vertexData.resize(mergedSize);
        std::memcpy(vertexData.data() + oldSize, next->vertexData.data(), next->vertexData.size());
        count += next->count;
        return true;
    }

    void execute() const override {
        if (!valid) return;

        wrapper_client_state_guard_t wrapperState;

        auto replayState = layout;
        replayState.starting_pointer = nullptr;
        replayState.dirty = true;
        GLuint staticVertexBuffer = 0;
        GLint drawFirst = 0;
        const bool useStaticBuffer = bindStaticVertexBuffer(&staticVertexBuffer, &drawFirst);
        replayState.buffer_based = useStaticBuffer;

        for (int i = 0; i < VERTEX_POINTER_COUNT; ++i) {
            if (((replayState.enabled_pointers >> i) & 1u) == 0) continue;
            replayState.attributes[i].pointer = useStaticBuffer
                                                    ? reinterpret_cast<const void*>(packedOffsets[i])
                                                    : vertexData.data() + packedOffsets[i];
        }

        g_glstate_c.fpe_state.vertexpointer_array = replayState;
        g_glstate_c.fpe_state.normalized_vpa.reset();
        g_glstate_c.fpe_state.client_active_texture = clientActiveTexture;

        // Static display-list geometry is immutable after glEndList. Keep it
        // in its own backend VBO so glCallList does not re-upload the same
        // client-array bytes every frame. drawArraysNow binds the selected
        // buffer only after capturing the caller's backend state, avoiding a
        // second per-draw state guard. If allocation fails, override the
        // binding with zero to retain the previous CPU-backed upload path.
        drawArraysNow(mode, drawFirst, count, true,
                      useStaticBuffer ? static_cast<GLint>(staticVertexBuffer) : 0);
    }

private:
    bool hasIndependentPrimitives() const {
        switch (mode) {
        case GL_POINTS:
            return true;
        case GL_LINES:
            return count % 2 == 0;
        case GL_TRIANGLES:
            return count % 3 == 0;
        case GL_QUADS:
            return count % 4 == 0;
        default:
            return false;
        }
    }

    bool bindStaticVertexBuffer(GLuint* selectedBuffer, GLint* drawFirst) const {
        if (!g_glstate_c.fpe_ready && init_fpe() != 0) return false;

        if (displayListVertexArena.isCurrent(arenaAllocation)) {
            *drawFirst = static_cast<GLint>(arenaAllocation.offset / static_cast<size_t>(layout.stride));
            *selectedBuffer = arenaAllocation.buffer;
            return true;
        }
        arenaAllocation = {};

        if (vertexBuffer == 0 &&
            displayListVertexArena.allocate(vertexData.data(), vertexData.size(),
                                            static_cast<size_t>(layout.stride), &arenaAllocation)) {
            *drawFirst = static_cast<GLint>(arenaAllocation.offset / static_cast<size_t>(layout.stride));
            *selectedBuffer = arenaAllocation.buffer;
            return true;
        }

        if (vertexBuffer == 0) {
            if ((!g_glstate_c.fpe_ready && init_fpe() != 0) || g_glFuncs.glGenBuffers == nullptr ||
                g_glFuncs.glBufferData == nullptr) {
                return false;
            }
            g_glFuncs.glGenBuffers(1, &vertexBuffer);
            sfpewNoteInternalBuffer(vertexBuffer);
            if (vertexBuffer == 0) return false;
        }

        if (!vertexBufferUploaded) {
            array_buffer_binding_guard_t bindingState;
            g_glFuncs.glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
            g_glFuncs.glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertexData.size()),
                                   vertexData.data(), GL_STATIC_DRAW);
            vertexBufferUploaded = true;
        }
        *selectedBuffer = vertexBuffer;
        *drawFirst = 0;
        return true;
    }

    bool capture(GLint first, const vertex_pointer_array_t& source) {
        if (first < 0 || count <= 0) {
            return false;
        }

        std::array<captured_attribute_t, VERTEX_POINTER_COUNT> attributes{};
        size_t packedStride = 0;
        size_t largestAlignment = 1;

        layout.reset();
        layout.enabled_pointers = source.enabled_pointers;
        layout.dirty = true;
        layout.buffer_based = false;

        for (int i = 0; i < VERTEX_POINTER_COUNT; ++i) {
            if (((source.enabled_pointers >> i) & 1u) == 0) continue;

            const auto& sourceAttribute = source.attributes[i];
            const size_t componentBytes = componentSize(sourceAttribute.type);
            if (sourceAttribute.size < 1 || sourceAttribute.size > 4 || componentBytes == 0 ||
                sourceAttribute.stride < 0) {
                return false;
            }

            size_t elementBytes = 0;
            if (!checkedMultiply(static_cast<size_t>(sourceAttribute.size), componentBytes, &elementBytes) ||
                !alignSize(packedStride, componentBytes, &packedStride)) {
                return false;
            }

            auto& captured = attributes[i];
            captured.enabled = true;
            captured.elementSize = elementBytes;
            captured.sourceStride = sourceAttribute.stride == 0 ? elementBytes
                                                                 : static_cast<size_t>(sourceAttribute.stride);
            captured.packedOffset = packedStride;
            captured.sourcePointer = reinterpret_cast<uintptr_t>(sourceAttribute.pointer);
            captured.sourceBuffer = getClientArrayBufferBinding(i);

            packedOffsets[i] = packedStride;
            if (!checkedAdd(packedStride, elementBytes, &packedStride)) {
                return false;
            }
            if (componentBytes > largestAlignment) largestAlignment = componentBytes;

            layout.attributes[i] = sourceAttribute;
        }

        if (!alignSize(packedStride, largestAlignment, &packedStride) || packedStride == 0 ||
            packedStride > static_cast<size_t>(std::numeric_limits<GLsizei>::max())) {
            return false;
        }

        size_t allocationSize = 0;
        if (!checkedMultiply(static_cast<size_t>(count), packedStride, &allocationSize)) {
            return false;
        }
        vertexData.resize(allocationSize);

        for (int i = 0; i < VERTEX_POINTER_COUNT; ++i) {
            if (!attributes[i].enabled) continue;
            layout.attributes[i].stride = static_cast<GLsizei>(packedStride);
            layout.attributes[i].pointer = reinterpret_cast<const void*>(packedOffsets[i]);
        }
        layout.stride = static_cast<GLsizei>(packedStride);

        array_buffer_binding_guard_t bindingState;
        for (int i = 0; i < VERTEX_POINTER_COUNT; ++i) {
            const auto& attribute = attributes[i];
            if (!attribute.enabled) continue;
            if (!copyAttribute(attribute, first, packedStride)) return false;
        }
        return true;
    }

    bool copyAttribute(const captured_attribute_t& attribute, GLint first, size_t packedStride) {
        size_t firstByte = 0;
        if (!checkedMultiply(static_cast<size_t>(first), attribute.sourceStride, &firstByte) ||
            !checkedAdd(firstByte, static_cast<size_t>(attribute.sourcePointer), &firstByte)) {
            return false;
        }

        size_t lastVertexOffset = 0;
        size_t sourceEnd = 0;
        if (!checkedMultiply(static_cast<size_t>(count - 1), attribute.sourceStride, &lastVertexOffset) ||
            !checkedAdd(firstByte, lastVertexOffset, &sourceEnd) ||
            !checkedAdd(sourceEnd, attribute.elementSize, &sourceEnd)) {
            return false;
        }

        const uint8_t* source = nullptr;
        void* mappedBuffer = nullptr;
        if (attribute.sourceBuffer == 0) {
            // Low values are buffer offsets, not dereferenceable client
            // addresses. They cannot be captured without the buffer binding
            // that was active at gl*Pointer time.
            if (attribute.sourcePointer < 4096) return false;
            source = reinterpret_cast<const uint8_t*>(firstByte);
        } else {
            if (g_glFuncs.glGetBufferParameteriv == nullptr || g_glFuncs.glMapBufferRange == nullptr ||
                g_glFuncs.glUnmapBuffer == nullptr) {
                return false;
            }

            g_glFuncs.glBindBuffer(GL_ARRAY_BUFFER, attribute.sourceBuffer);
            GLint bufferSize = 0;
            g_glFuncs.glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &bufferSize);
            if (bufferSize <= 0 || sourceEnd > static_cast<size_t>(bufferSize)) {
                return false;
            }

            mappedBuffer = g_glFuncs.glMapBufferRange(GL_ARRAY_BUFFER, 0, bufferSize, GL_MAP_READ_BIT);
            if (mappedBuffer == nullptr) {
                return false;
            }
            source = static_cast<const uint8_t*>(mappedBuffer) + firstByte;
        }

        for (GLsizei vertex = 0; vertex < count; ++vertex) {
            auto* destination = vertexData.data() + static_cast<size_t>(vertex) * packedStride +
                                attribute.packedOffset;
            std::memcpy(destination, source + static_cast<size_t>(vertex) * attribute.sourceStride,
                        attribute.elementSize);
        }

        if (mappedBuffer != nullptr && g_glFuncs.glUnmapBuffer(GL_ARRAY_BUFFER) == GL_FALSE) return false;
        return true;
    }

    GLenum mode;
    GLsizei count;
    GLenum clientActiveTexture;
    bool valid = false;
    vertex_pointer_array_t layout{};
    std::array<size_t, VERTEX_POINTER_COUNT> packedOffsets{};
    std::vector<uint8_t> vertexData;
    mutable display_list_vertex_allocation_t arenaAllocation{};
    mutable GLuint vertexBuffer = 0;
    mutable bool vertexBufferUploaded = false;

    friend bool ::tryExecuteCapturedDisplayLists(const GLuint* listIds, size_t listCount);
};

constexpr size_t kCapturedDisplayListBatchCacheSize = 4;

struct captured_display_list_batch_t {
    bool valid = false;
    const captured_draw_arrays_cmd_t* prototype = nullptr;
    glm::mat4 commonLinear{1.0f};
    GLuint commonBuffer = 0;
    GLsizei maxVertexCount = 0;
    std::vector<GLuint> listIds;
    std::vector<GLint> firsts;
    std::vector<GLsizei> vertexCounts;
    std::vector<GLsizei> elementCounts;
    std::vector<const void*> indexPointers;
};

struct captured_display_list_batch_cache_t {
    uint64_t generation = 0;
    size_t nextReplacement = 0;
    std::array<captured_display_list_batch_t, kCapturedDisplayListBatchCacheSize> entries;
};

// GL_QUADS/GL_QUAD_STRIP/GL_POLYGON for a draw that keeps the APP's vertex
// state. Sodium binds its own program and its own VAO with generic attributes,
// then issues glDrawArrays(GL_QUADS, ...) - legal in GL 2.1, but mode 7 does not
// exist in GLES, so passing it through raw is GL_INVALID_ENUM and the draw is
// dropped (RenderDoc, 1.16 sodium, EID 130..2311: 503 such draws).
//
// The app's attributes are already correct and must not be touched, so unlike
// the fixed-function paths this cannot move to fpe_vao. Only the element
// binding is needed, and that IS VAO state, so it is saved and restored around
// the draw. Returns true when the draw was issued here.
bool passthroughLegacyDrawArrays(GLenum mode, GLint first, GLsizei count) {
    if (mode == GL_QUAD_STRIP) {
        // Vertex order is identical; no index rewrite needed.
        g_glFuncs.glDrawArrays(GL_TRIANGLE_STRIP, first, count);
        return true;
    }
    if (mode == GL_POLYGON) {
        g_glFuncs.glDrawArrays(GL_TRIANGLE_FAN, first, count);
        return true;
    }
    if (mode != GL_QUADS) return false;
    if (count < 4 || first < 0) {
        // Nothing a quad can be made of; swallow it rather than handing the
        // backend an enum it will reject.
        return true;
    }
    if (g_glFuncs.glDrawElements == nullptr || g_glFuncs.glBindBuffer == nullptr ||
        g_glFuncs.glGetIntegerv == nullptr) {
        return false;
    }
    auto& st = g_glstate_c.fpe_state;
    if (st.fpe_ibo == 0) {
        if (g_glFuncs.glGenBuffers == nullptr) return false;
        g_glFuncs.glGenBuffers(1, &st.fpe_ibo);
        sfpewNoteInternalBuffer(st.fpe_ibo);
        if (st.fpe_ibo == 0) return false;
    }

    const bool base_vertex = first != 0 && g_glFuncs.glDrawElementsBaseVertex != nullptr;
    const GLuint index_first = base_vertex ? 0u : static_cast<GLuint>(first);
    const bool upload = prepare_quad_indices(count, index_first);
    const GLsizei index_count = (count / 4) * 6;

    // The app owns this VAO's element binding; put it back afterwards.
    GLint saved_element_buffer = 0;
    g_glFuncs.glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &saved_element_buffer);

    g_glFuncs.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, st.fpe_ibo);
    st.fpe_ibo_bound = false; // this bind landed in the app's VAO, not fpe_vao
    if (upload) {
        g_glFuncs.glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)quad_index_size_bytes(),
                               quad_index_data(), GL_DYNAMIC_DRAW);
    }

    if (base_vertex) {
        g_glFuncs.glDrawElementsBaseVertex(GL_TRIANGLES, index_count, quad_index_type(), (void*)0,
                                           first);
    } else {
        g_glFuncs.glDrawElements(GL_TRIANGLES, index_count, quad_index_type(), (void*)0);
    }

    g_glFuncs.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, (GLuint)saved_element_buffer);
    return true;
}

void drawArraysNow(GLenum mode, GLint first, GLsizei count, bool forceFixedFunction,
                   GLint arrayBufferOverride) {

    // A display-list API call owns one backend guard around the whole replay.
    // Captured draws always provide an explicit static VBO (or zero for the
    // allocation-failure fallback), so they can keep the FPE backend state
    // live between commands and avoid four synchronous state queries per
    // draw. The outer glCallList(s) guard restores all caller state once.
    if (forceFixedFunction && DisplayListManager::isCalling() && arrayBufferOverride >= 0) {
        g_glFuncs.glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(arrayBufferOverride));
        const int doDrawElement =
            commit_fpe_state_on_draw(&mode, &first, &count, arrayBufferOverride);
        if (doDrawElement < 0) return;
        if (doDrawElement == 2) {
            g_glFuncs.glDrawElements(mode, count, GL_UNSIGNED_INT, (void*)0);
        } else if (doDrawElement > 0) {
            if (first != 0 && g_glFuncs.glDrawElementsBaseVertex != nullptr)
                g_glFuncs.glDrawElementsBaseVertex(mode, count, quad_index_type(), (void*)0, first);
            else
                g_glFuncs.glDrawElements(mode, count, quad_index_type(), (void*)0);
        } else {
            g_glFuncs.glDrawArrays(mode, first, count);
        }
        return;
    }

    const GLint current_program = sfpewLogicalProgram();

    const auto& vertex_array = g_glstate_c.fpe_state.vertexpointer_array;
    const uint32_t vertex_array_mask = 1u << vp2idx(GL_VERTEX_ARRAY);
    if ((!forceFixedFunction && current_program != 0) || first < 0 || count < 0 ||
        !(vertex_array.enabled_pointers & vertex_array_mask)) {
        // These paths draw with the app's own program/VAO/element state; the
        // glDrawArrays entry no longer restores it for fixed-function draws,
        // so it must come back here before anything reaches the backend.
        sfpewFlushDeferredDrawState();
        if (current_program != 0) {
            // GL 2.1: a bound user program still consumes the fixed-function
            // vertex arrays (shader packs draw terrain via glVertexPointer).
            if ((vertex_array.enabled_pointers & vertex_array_mask) && first >= 0 && count > 0 &&
                sfpewUserProgramFixedFunctionDrawArrays((GLuint)current_program, mode, first,
                                                        count)) {
                return;
            }
            sfpewFeedUserProgramUniforms((GLuint)current_program);
        }
        if (passthroughLegacyDrawArrays(mode, first, count)) return;
        g_glFuncs.glDrawArrays(mode, first, count);
        return;
    }

    if (g_glstate_c.render_mode != GL_RENDER) {
        // Selection/feedback: transform on the CPU, never touch the GPU.
        const auto& attr = vertex_array.attributes[vp2idx(GL_VERTEX_ARRAY)];
        const bool client_ptr = getClientArrayBufferBinding(vp2idx(GL_VERTEX_ARRAY)) == 0;
        if (client_ptr && attr.pointer != nullptr && attr.type == GL_FLOAT && count > 0) {
            const GLsizei stride_bytes = attr.stride != 0 ? attr.stride
                                                          : attr.size * (GLsizei)sizeof(GLfloat);
            const auto* base = static_cast<const uint8_t*>(attr.pointer) +
                               (size_t)first * (size_t)stride_bytes;
            sfpewSelectionProcessVertices(mode, reinterpret_cast<const GLfloat*>(base),
                                          (size_t)stride_bytes / sizeof(GLfloat), attr.size,
                                          (size_t)count);
        } else {
            SFPEW_LOGW("selection: unsupported vertex source (VBO or non-float), draw skipped");
        }
        return;
    }

    const GLint logicalArrayBuffer = static_cast<GLint>(sfpewLogicalArrayBufferBinding());
    fpe_backend_draw_state_guard_t backend_state(current_program, logicalArrayBuffer);
    GLint attributeArrayBuffer = logicalArrayBuffer;
    if (arrayBufferOverride >= 0) {
        attributeArrayBuffer = arrayBufferOverride;
        g_glFuncs.glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(arrayBufferOverride));
    }
    int do_draw_element = commit_fpe_state_on_draw(&mode, &first, &count, attributeArrayBuffer);
    if (do_draw_element < 0) {
        return;
    } else if (do_draw_element == 2) {
        g_glFuncs.glDrawElements(mode, count, GL_UNSIGNED_INT, (void*)0);
    } else if (do_draw_element > 0) {
        if (first != 0 && g_glFuncs.glDrawElementsBaseVertex != nullptr)
            g_glFuncs.glDrawElementsBaseVertex(mode, count, quad_index_type(), (void*)0, first);
        else
            g_glFuncs.glDrawElements(mode, count, quad_index_type(), (void*)0);
    } else {
        g_glFuncs.glDrawArrays(mode, first, count);
    }
}

size_t indexTypeSize(GLenum type) {
    switch (type) {
    case GL_UNSIGNED_BYTE:
        return 1;
    case GL_UNSIGNED_SHORT:
        return 2;
    case GL_UNSIGNED_INT:
        return 4;
    default:
        return 0;
    }
}

template <typename T>
uint32_t maxIndexOf(const T* indices, size_t count) {
    uint32_t result = 0;
    for (size_t i = 0; i < count; ++i)
        result = std::max(result, static_cast<uint32_t>(indices[i]));
    return result;
}

template <typename T>
void expandQuadIndices(const T* src, size_t quadCount, std::vector<uint32_t>& out) {
    out.resize(quadCount * 6u);
    for (size_t q = 0; q < quadCount; ++q) {
        const uint32_t i0 = src[q * 4 + 0], i1 = src[q * 4 + 1];
        const uint32_t i2 = src[q * 4 + 2], i3 = src[q * 4 + 3];
        out[q * 6 + 0] = i0;
        out[q * 6 + 1] = i1;
        out[q * 6 + 2] = i2;
        out[q * 6 + 3] = i2;
        out[q * 6 + 4] = i3;
        out[q * 6 + 5] = i0;
    }
}

// FPE conversion for glDrawElements (plans/02 section B): mirrors the
// glDrawArrays interception - only program 0 with an enabled legacy vertex
// array is converted, everything else passes through untouched.
// User program + fixed-function arrays + glDrawElements (plans/09 S9).
// Everything goes through the CPU-simplest correct path: indices land in
// fpe_element_ibo (expanded for GL_QUADS), client vertices upload sized by
// the largest referenced index. Returns false to fall back to passthrough.
bool userProgramDrawElements(GLuint program, GLenum mode, GLsizei count, GLenum type,
                             const GLvoid* indices) {
    GLint locations[VERTEX_POINTER_COUNT];
    if (count <= 0 || !sfpewUserProgramAttribLocations(program, locations)) return false;
    const size_t index_size = indexTypeSize(type);
    if (index_size == 0) return false;
    if (!g_glstate.fpe_ready && init_fpe() != 0) return false;

    auto& st = g_glstate.fpe_state;
    if (st.fpe_user_vao == 0) {
        if (g_glFuncs.glGenVertexArrays == nullptr) return false;
        g_glFuncs.glGenVertexArrays(1, &st.fpe_user_vao);
        st.fpe_user_vao_enabled = 0;
        if (st.fpe_user_vao == 0) return false;
    }

    // Pull the indices to the CPU (client memory or mapped buffer).
    GLint element_buffer = 0;
    g_glFuncs.glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &element_buffer);
    thread_local std::vector<uint8_t> scratch;
    const uint8_t* cpu_indices = nullptr;
    if (element_buffer == 0) {
        if (indices == nullptr) return false;
        cpu_indices = static_cast<const uint8_t*>(indices);
    } else {
        if (g_glFuncs.glMapBufferRange == nullptr || g_glFuncs.glUnmapBuffer == nullptr)
            return false;
        const size_t bytes = (size_t)count * index_size;
        void* mapped = g_glFuncs.glMapBufferRange(
            GL_ELEMENT_ARRAY_BUFFER, (GLintptr)(uintptr_t)indices, (GLsizeiptr)bytes,
            GL_MAP_READ_BIT);
        if (mapped == nullptr) return false;
        scratch.assign((const uint8_t*)mapped, (const uint8_t*)mapped + bytes);
        g_glFuncs.glUnmapBuffer(GL_ELEMENT_ARRAY_BUFFER);
        cpu_indices = scratch.data();
    }
    const GLint logical_array_buffer = (GLint)sfpewLogicalArrayBufferBinding();
    fpe_backend_draw_state_guard_t backend_state((GLint)program, logical_array_buffer);

    const auto& raw_vpa = g_glstate.fpe_state.vertexpointer_array;

    // The largest referenced index has exactly two consumers: the vertex count
    // that sizes gather_client_arrays' copy, and the client-memory upload size.
    // A single VBO-backed enabled array makes the gather bail on that alone
    // (gather_client_arrays, fpe/fpe.cpp) and makes the draw source straight
    // from the app's buffer, so neither runs and the scan over every index of
    // every draw is dead work: 1.16-Optifine/1-frame19661.rdc walks 108630
    // indices a frame for a value nothing reads. Compute it on demand.
    bool any_vbo_backed = false;
    for (int i = 0; i < VERTEX_POINTER_COUNT; ++i) {
        if (!((raw_vpa.enabled_pointers >> i) & 1u)) continue;
        if (getClientArrayBufferBinding(i) != 0) {
            any_vbo_backed = true;
            break;
        }
    }
    uint32_t max_index = 0;
    bool max_index_known = false;
    const auto vertexCount = [&]() -> GLsizei {
        if (!max_index_known) {
            switch (index_size) {
            case 1: max_index = maxIndexOf((const uint8_t*)cpu_indices, (size_t)count); break;
            case 2: max_index = maxIndexOf((const uint16_t*)cpu_indices, (size_t)count); break;
            default: max_index = maxIndexOf((const uint32_t*)cpu_indices, (size_t)count); break;
            }
            max_index_known = true;
        }
        return (GLsizei)max_index + 1;
    };

    vertex_pointer_array_t vpa;
    if (!any_vbo_backed && gather_client_arrays(raw_vpa, 0, vertexCount(), &vpa)) {
        // gathered layout starts at vertex 0
    } else {
        vertex_pointer_array_t raw_copy = raw_vpa;
        vpa = raw_copy.normalize();
    }

    sfpewBackendBindVertexArray(st.fpe_user_vao);
    const bool client_memory_draw =
        reinterpret_cast<uintptr_t>(vpa.starting_pointer) > static_cast<uintptr_t>(vpa.stride);
    const GLuint attribute_buffer = (logical_array_buffer == 0 || client_memory_draw)
                                        ? st.fpe_vbo
                                        : (GLuint)logical_array_buffer;
    sfpewBackendBindAttributeBuffer(attribute_buffer, backend_state.holds_save);
    if (client_memory_draw) {
        const int64_t upload_size = (int64_t)vertexCount() * (int64_t)vpa.stride;
        if (upload_size <= 0 || upload_size > (int64_t)std::numeric_limits<GLsizei>::max()) {
            g_glstate.set_error(GL_INVALID_VALUE);
            return true;
        }
        g_glFuncs.glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)upload_size, vpa.starting_pointer,
                               GL_DYNAMIC_DRAW);
    }

    sfpewSendUserProgramAttributes(locations, vpa, 0);
    sfpewFeedUserProgramUniforms(program);

    GLenum draw_mode = mode;
    GLsizei draw_count = count;
    GLenum draw_type = type;
    const void* draw_offset = nullptr;
    thread_local std::vector<uint32_t> expanded;
    if (mode == GL_QUADS) {
        const size_t quads = (size_t)count / 4u;
        switch (index_size) {
        case 1: expandQuadIndices((const uint8_t*)cpu_indices, quads, expanded); break;
        case 2: expandQuadIndices((const uint16_t*)cpu_indices, quads, expanded); break;
        default: expandQuadIndices((const uint32_t*)cpu_indices, quads, expanded); break;
        }
        if (st.fpe_element_ring == 0) g_glFuncs.glGenBuffers(1, &st.fpe_element_ring); sfpewNoteInternalBuffer(st.fpe_element_ring);
        g_glFuncs.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, st.fpe_element_ring);
        st.fpe_ibo_bound = false;
        draw_offset = (const void*)(uintptr_t)sfpewUploadElementData(
            expanded.data(), expanded.size() * sizeof(uint32_t));
        draw_mode = GL_TRIANGLES;
        draw_count = (GLsizei)expanded.size();
        draw_type = GL_UNSIGNED_INT;
    } else {
        if (mode == GL_QUAD_STRIP) draw_mode = GL_TRIANGLE_STRIP;
        else if (mode == GL_POLYGON) draw_mode = GL_TRIANGLE_FAN;
        if (st.fpe_element_ring == 0) g_glFuncs.glGenBuffers(1, &st.fpe_element_ring); sfpewNoteInternalBuffer(st.fpe_element_ring);
        g_glFuncs.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, st.fpe_element_ring);
        st.fpe_ibo_bound = false;
        draw_offset = (const void*)(uintptr_t)sfpewUploadElementData(
            cpu_indices, (size_t)count * index_size);
    }
    g_glFuncs.glDrawElements(draw_mode, draw_count, draw_type, draw_offset);
    return true;
}

// Indexed counterpart of passthroughLegacyDrawArrays: the app owns the vertex
// state, only the legacy primitive mode has to go. QUAD_STRIP and POLYGON are
// vertex-order compatible so they are pure mode swaps. GL_QUADS needs its index
// data rewritten, which means reading the app's indices back - from client
// memory when no element buffer is bound, otherwise by mapping the app's buffer.
bool passthroughLegacyDrawElements(GLenum mode, GLsizei count, GLenum type,
                                   const GLvoid* indices) {
    if (g_glFuncs.glDrawElements == nullptr) return false;
    if (mode == GL_QUAD_STRIP) {
        g_glFuncs.glDrawElements(GL_TRIANGLE_STRIP, count, type, indices);
        return true;
    }
    if (mode == GL_POLYGON) {
        g_glFuncs.glDrawElements(GL_TRIANGLE_FAN, count, type, indices);
        return true;
    }
    if (mode != GL_QUADS) return false;
    if (count < 4) return true; // nothing a quad can be made of
    const size_t index_size = indexTypeSize(type);
    if (index_size == 0) {
        g_glstate_c.set_error(GL_INVALID_ENUM);
        return true;
    }
    if (g_glFuncs.glGetIntegerv == nullptr || g_glFuncs.glBindBuffer == nullptr) return false;

    GLint app_element_buffer = 0;
    g_glFuncs.glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &app_element_buffer);

    // Pull the app's indices to the CPU so the quads can be expanded.
    thread_local std::vector<uint8_t> scratch;
    const uint8_t* cpu_indices = nullptr;
    const size_t byte_count = (size_t)count * index_size;
    if (app_element_buffer == 0) {
        if (indices == nullptr) {
            g_glstate_c.set_error(GL_INVALID_VALUE);
            return true;
        }
        cpu_indices = static_cast<const uint8_t*>(indices);
    } else {
        if (g_glFuncs.glMapBufferRange == nullptr || g_glFuncs.glUnmapBuffer == nullptr) return false;
        void* mapped = g_glFuncs.glMapBufferRange(
            GL_ELEMENT_ARRAY_BUFFER, (GLintptr)reinterpret_cast<uintptr_t>(indices),
            (GLsizeiptr)byte_count, GL_MAP_READ_BIT);
        if (mapped == nullptr) return false;
        scratch.assign(static_cast<const uint8_t*>(mapped),
                       static_cast<const uint8_t*>(mapped) + byte_count);
        g_glFuncs.glUnmapBuffer(GL_ELEMENT_ARRAY_BUFFER);
        cpu_indices = scratch.data();
    }

    thread_local std::vector<uint32_t> expanded;
    const size_t quads = (size_t)count / 4u;
    switch (index_size) {
    case 1: expandQuadIndices(cpu_indices, quads, expanded); break;
    case 2: expandQuadIndices(reinterpret_cast<const uint16_t*>(cpu_indices), quads, expanded); break;
    default: expandQuadIndices(reinterpret_cast<const uint32_t*>(cpu_indices), quads, expanded); break;
    }
    if (expanded.empty()) return true;

    auto& st = g_glstate_c.fpe_state;
    if (st.fpe_element_ring == 0) {
        if (g_glFuncs.glGenBuffers == nullptr) return false;
        g_glFuncs.glGenBuffers(1, &st.fpe_element_ring); sfpewNoteInternalBuffer(st.fpe_element_ring);
        if (st.fpe_element_ring == 0) return false;
    }
    g_glFuncs.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, st.fpe_element_ring);
    st.fpe_ibo_bound = false; // this bind landed in the app's VAO
    const GLintptr offset =
        sfpewUploadElementData(expanded.data(), expanded.size() * sizeof(uint32_t));
    g_glFuncs.glDrawElements(GL_TRIANGLES, (GLsizei)expanded.size(), GL_UNSIGNED_INT,
                             (const void*)(uintptr_t)offset);

    // The element binding is the app's VAO state; hand it back.
    g_glFuncs.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, (GLuint)app_element_buffer);
    return true;
}

void drawElementsNow(GLenum mode, GLsizei count, GLenum type, const GLvoid* indices) {
    const GLint current_program = sfpewLogicalProgram();
    const auto& vertex_array = g_glstate_c.fpe_state.vertexpointer_array;
    const uint32_t vertex_array_mask = 1u << vp2idx(GL_VERTEX_ARRAY);

    if (current_program != 0 || !(vertex_array.enabled_pointers & vertex_array_mask)) {
        // These paths draw with the app's own program/VAO/element state; the
        // glDrawElements entry no longer restores it for fixed-function
        // draws, so it must come back before anything reaches the backend.
        sfpewFlushDeferredDrawState();
        if (current_program != 0) {
            if ((vertex_array.enabled_pointers & vertex_array_mask) &&
                userProgramDrawElements((GLuint)current_program, mode, count, type, indices)) {
                return;
            }
            sfpewFeedUserProgramUniforms((GLuint)current_program);
        }
        if (passthroughLegacyDrawElements(mode, count, type, indices)) return;
        if (g_glFuncs.glDrawElements != nullptr) g_glFuncs.glDrawElements(mode, count, type, indices);
        return;
    }

    const size_t index_size = indexTypeSize(type);
    if (index_size == 0) {
        g_glstate_c.set_error(GL_INVALID_ENUM);
        return;
    }
    if (count < 0) {
        g_glstate_c.set_error(GL_INVALID_VALUE);
        return;
    }
    if (count == 0) return;

    if (!g_glstate_c.fpe_ready && init_fpe() != 0) {
        if (g_glFuncs.glDrawElements != nullptr) g_glFuncs.glDrawElements(mode, count, type, indices);
        return;
    }

    // The caller's element-array binding is VAO state. Resolve it from the
    // wrapper's shadows instead of a synchronous glGetIntegerv per draw:
    // while a fixed-function draw holds the app's state the held snapshot
    // has it, and outside that VAO 0's binding is shadowed (healed every
    // 256 draws by the guard). The leftover cases - app VAO unknown or
    // non-zero - restore and take the real query.
    GLint element_buffer = 0;
    {
        auto& gsc = g_glstate_c;
        if (gsc.deferred_draw.held && gsc.deferred_draw.vertex_array == 0 &&
            gsc.deferred_draw.element_array_buffer >= 0) {
            element_buffer = gsc.deferred_draw.element_array_buffer;
        } else if (!gsc.deferred_draw.held && gsc.backend_vao_known &&
                   gsc.backend_vao_binding == 0 && gsc.backend_vao0_element_known) {
            element_buffer = gsc.backend_vao0_element_binding;
        } else {
            sfpewFlushDeferredDrawState();
            g_glFuncs.glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &element_buffer);
        }
    }

    // Legacy modes: GL_QUADS needs index rewriting; the strip/fan quads
    // modes are vertex-order compatible with core modes.
    GLenum draw_mode = mode;
    const bool rewrite_quads = mode == GL_QUADS;
    if (mode == GL_QUAD_STRIP)
        draw_mode = GL_TRIANGLE_STRIP;
    else if (mode == GL_POLYGON)
        draw_mode = GL_TRIANGLE_FAN;

    // Client-memory vertex arrays force an upload sized by the largest
    // referenced index (same address-vs-offset heuristic as normalize()).
    const void* vertex_pointer = vertex_array.attributes[vp2idx(GL_VERTEX_ARRAY)].pointer;
    const bool client_vertices = reinterpret_cast<uintptr_t>(vertex_pointer) > (1u << 20);

    // Pull the index data to the CPU when we must scan or rewrite it.
    thread_local std::vector<uint8_t> index_scratch;
    const uint8_t* cpu_indices = nullptr;
    if (element_buffer == 0) {
        if (indices == nullptr) {
            g_glstate_c.set_error(GL_INVALID_VALUE);
            return;
        }
        cpu_indices = static_cast<const uint8_t*>(indices);
    } else if (client_vertices || rewrite_quads) {
        if (g_glFuncs.glMapBufferRange == nullptr || g_glFuncs.glUnmapBuffer == nullptr) {
            if (g_glFuncs.glDrawElements != nullptr) g_glFuncs.glDrawElements(mode, count, type, indices);
            return;
        }
        const size_t byte_count = static_cast<size_t>(count) * index_size;
        // Bind the caller's index VBO explicitly before mapping: with the
        // draw state held, the current VAO is the wrapper's and its element
        // binding is the wrapper's ring - mapping GL_ELEMENT_ARRAY_BUFFER
        // without this would read the wrong buffer.
        sfpewBackendBindElementBuffer(static_cast<GLuint>(element_buffer));
        g_glstate_c.fpe_state.fpe_ibo_bound = false;
        void* mapped = g_glFuncs.glMapBufferRange(GL_ELEMENT_ARRAY_BUFFER,
                                                  (GLintptr)reinterpret_cast<uintptr_t>(indices),
                                                  (GLsizeiptr)byte_count, GL_MAP_READ_BIT);
        if (mapped == nullptr) {
            if (g_glFuncs.glDrawElements != nullptr) g_glFuncs.glDrawElements(mode, count, type, indices);
            return;
        }
        index_scratch.assign(static_cast<const uint8_t*>(mapped),
                             static_cast<const uint8_t*>(mapped) + byte_count);
        g_glFuncs.glUnmapBuffer(GL_ELEMENT_ARRAY_BUFFER);
        cpu_indices = index_scratch.data();
    }

    uint32_t max_index = 0;
    if (client_vertices && cpu_indices != nullptr) {
        switch (type) {
        case GL_UNSIGNED_BYTE:
            max_index = maxIndexOf(cpu_indices, static_cast<size_t>(count));
            break;
        case GL_UNSIGNED_SHORT:
            max_index = maxIndexOf(reinterpret_cast<const uint16_t*>(cpu_indices), static_cast<size_t>(count));
            break;
        default:
            max_index = maxIndexOf(reinterpret_cast<const uint32_t*>(cpu_indices), static_cast<size_t>(count));
            break;
        }
        if (max_index >= static_cast<uint32_t>(std::numeric_limits<GLsizei>::max())) {
            g_glstate_c.set_error(GL_INVALID_VALUE);
            return;
        }
    }

    const GLint logical_array_buffer = static_cast<GLint>(sfpewLogicalArrayBufferBinding());
    fpe_backend_draw_state_guard_t backend_state(current_program, logical_array_buffer);

    // Reuse the arrays-path commit for program/uniform/attribute setup and
    // the client-memory vertex upload. GL_TRIANGLES bypasses its
    // QUADS-from-arrays conversion; first/count only size that upload.
    GLenum commit_mode = GL_TRIANGLES;
    GLint commit_first = 0;
    GLsizei commit_count = client_vertices ? static_cast<GLsizei>(max_index + 1u) : count;
    if (commit_fpe_state_on_draw(&commit_mode, &commit_first, &commit_count, logical_array_buffer) < 0) return;

    thread_local std::vector<uint32_t> expanded_indices;
    const void* draw_indices = indices;
    GLenum draw_type = type;
    GLsizei draw_count = count;

    if (rewrite_quads) {
        const size_t quad_count = static_cast<size_t>(count) / 4u; // partial quads are dropped per spec
        switch (type) {
        case GL_UNSIGNED_BYTE:
            expandQuadIndices(cpu_indices, quad_count, expanded_indices);
            break;
        case GL_UNSIGNED_SHORT:
            expandQuadIndices(reinterpret_cast<const uint16_t*>(cpu_indices), quad_count, expanded_indices);
            break;
        default:
            expandQuadIndices(reinterpret_cast<const uint32_t*>(cpu_indices), quad_count, expanded_indices);
            break;
        }
        draw_mode = GL_TRIANGLES;
        draw_type = GL_UNSIGNED_INT;
        draw_count = static_cast<GLsizei>(quad_count * 6u);
        if (draw_count == 0) return;
    }

    auto& state = g_glstate_c.fpe_state;
    if (rewrite_quads || element_buffer == 0) {
        const void* upload_data = rewrite_quads ? (const void*)expanded_indices.data()
                                                : (const void*)cpu_indices;
        const size_t upload_bytes = rewrite_quads
                                        ? expanded_indices.size() * sizeof(uint32_t)
                                        : static_cast<size_t>(count) * index_size;

        // Apps redraw the same client-memory index pattern every frame (GUI
        // widgets, glyph quads, chunk passes). The second consecutive draw
        // with byte-identical indices promotes them into a device-local
        // buffer; every later repeat pays a memcmp instead of a ring upload,
        // and the GPU reads its indices from device memory instead of the
        // coherent ring. Capped so a pathological app cannot pin memory.
        constexpr size_t kElementReuseLimit = 1u << 20;
        bool reused = false;
        if (upload_bytes <= kElementReuseLimit && upload_data != nullptr) {
            auto& hot = state.fpe_element_reuse_bytes;
            auto& pending = state.fpe_element_reuse_pending;
            if (state.fpe_element_reuse_buffer != 0 && hot.size() == upload_bytes &&
                std::memcmp(hot.data(), upload_data, upload_bytes) == 0) {
                sfpewBackendBindElementBuffer(state.fpe_element_reuse_buffer);
                state.fpe_ibo_bound = false; // fpe_vao's element binding changed
                draw_indices = (const void*)0;
                reused = true;
            } else if (pending.size() == upload_bytes &&
                       std::memcmp(pending.data(), upload_data, upload_bytes) == 0) {
                // Second sighting: promote. A miss keeps streaming through
                // the ring, so one-shot index sets never pay glBufferData.
                if (state.fpe_element_reuse_buffer == 0) {
                    g_glFuncs.glGenBuffers(1, &state.fpe_element_reuse_buffer);
                    sfpewNoteInternalBuffer(state.fpe_element_reuse_buffer);
                }
                if (state.fpe_element_reuse_buffer != 0) {
                    sfpewBackendBindElementBuffer(state.fpe_element_reuse_buffer);
                    state.fpe_ibo_bound = false;
                    g_glFuncs.glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                                           (GLsizeiptr)upload_bytes, upload_data,
                                           GL_STATIC_DRAW);
                    hot.assign(static_cast<const uint8_t*>(upload_data),
                               static_cast<const uint8_t*>(upload_data) + upload_bytes);
                    pending.clear();
                    draw_indices = (const void*)0;
                    reused = true;
                }
            } else {
                pending.assign(static_cast<const uint8_t*>(upload_data),
                               static_cast<const uint8_t*>(upload_data) + upload_bytes);
            }
        }

        if (!reused) {
            // CPU-side index data streams through the persistent-mapped
            // element ring. glBufferData here orphaned and reallocated a
            // buffer on every indexed client-array draw, which measured as
            // about half that draw's cost (plans/12).
            if (state.fpe_element_ring == 0) g_glFuncs.glGenBuffers(1, &state.fpe_element_ring); sfpewNoteInternalBuffer(state.fpe_element_ring);
            sfpewBackendBindElementBuffer(state.fpe_element_ring);
            state.fpe_ibo_bound = false; // fpe_vao's element binding changed
            draw_indices = (const void*)(uintptr_t)sfpewUploadElementData(upload_data, upload_bytes);
        }
    } else {
        // Indices stay in the caller's VBO; bind it inside fpe_vao.
        sfpewBackendBindElementBuffer(static_cast<GLuint>(element_buffer));
        state.fpe_ibo_bound = false;
    }

    g_glFuncs.glDrawElements(draw_mode, draw_count, draw_type, draw_indices);
}

} // namespace

bool tryExecuteCapturedDisplayLists(const GLuint* listIds, size_t listCount) {
    if (listIds == nullptr || listCount < 2 || g_glFuncs.glMultiDrawArrays == nullptr ||
        g_glstate_c.fpe_uniform.transformation.matrix_mode != GL_MODELVIEW) {
        return false;
    }

    // Heap-backed: keeps the module's TLS block inside glibc's static-TLS
    // surplus so tls_model initial-exec stays usable (plans/12).
    thread_local std::unique_ptr<captured_display_list_batch_cache_t> cacheStorage;
    if (cacheStorage == nullptr)
        cacheStorage = std::make_unique<captured_display_list_batch_cache_t>();
    captured_display_list_batch_cache_t& cache = *cacheStorage;
    const uint64_t listGeneration = DisplayListManager::generation();
    if (cache.generation != listGeneration) {
        cache.generation = listGeneration;
        cache.nextReplacement = 0;
        for (auto& entry : cache.entries) entry.valid = false;
    }

    const auto compatible = [](const captured_draw_arrays_cmd_t& left,
                               const captured_draw_arrays_cmd_t& right) {
        if (!left.valid || !right.valid || left.mode != right.mode ||
            left.clientActiveTexture != right.clientActiveTexture ||
            left.layout.enabled_pointers != right.layout.enabled_pointers ||
            left.layout.stride != right.layout.stride ||
            left.packedOffsets != right.packedOffsets) {
            return false;
        }
        for (int i = 0; i < VERTEX_POINTER_COUNT; ++i) {
            if (((left.layout.enabled_pointers >> i) & 1u) == 0) continue;
            const auto& lhs = left.layout.attributes[i];
            const auto& rhs = right.layout.attributes[i];
            if (lhs.size != rhs.size || lhs.usage != rhs.usage || lhs.type != rhs.type ||
                lhs.normalized != rhs.normalized || lhs.stride != rhs.stride ||
                lhs.pointer != rhs.pointer) {
                return false;
            }
        }
        return true;
    };

    captured_display_list_batch_t* batch = nullptr;
    for (auto& entry : cache.entries) {
        if (!entry.valid || entry.listIds.size() != listCount ||
            std::memcmp(entry.listIds.data(), listIds,
                        listCount * sizeof(GLuint)) != 0) {
            continue;
        }

        // Every compatible draw comes from the same arena buffer and arena
        // generation. One surviving allocation therefore proves that all
        // cached offsets remain valid; display-list mutation separately
        // invalidates the command pointers above.
        if (entry.prototype == nullptr || entry.commonBuffer == 0 ||
            !displayListVertexArena.isCurrent(entry.prototype->arenaAllocation) ||
            entry.prototype->arenaAllocation.buffer != entry.commonBuffer) {
            entry.valid = false;
            continue;
        }
        batch = &entry;
        break;
    }

    if (batch == nullptr) {
        auto& candidate = cache.entries[cache.nextReplacement];
        cache.nextReplacement = (cache.nextReplacement + 1) % cache.entries.size();
        candidate.valid = false;
        candidate.prototype = nullptr;
        candidate.commonLinear = glm::mat4(1.0f);
        candidate.commonBuffer = 0;
        candidate.maxVertexCount = 0;
        candidate.listIds.assign(listIds, listIds + listCount);
        candidate.firsts.clear();
        candidate.vertexCounts.clear();
        candidate.elementCounts.clear();
        candidate.indexPointers.clear();
        candidate.firsts.reserve(listCount);
        candidate.vertexCounts.reserve(listCount);

        for (size_t listIndex = 0; listIndex < listCount; ++listIndex) {
            const GLuint listId = listIds[listIndex];
            const DisplayList* list = DisplayListManager::findList(listId);
            if (list == nullptr || list->size() != 1) return false;

            glm::mat4 linear(1.0f);
            const GLCmd* batchCommand = list->front()->capturedDrawForBatch(&linear);
            const auto* draw = dynamic_cast<const captured_draw_arrays_cmd_t*>(batchCommand);
            if (draw == nullptr) return false;

            if (candidate.prototype == nullptr) {
                candidate.prototype = draw;
                candidate.commonLinear = linear;
            } else if (std::memcmp(&candidate.commonLinear, &linear,
                                   sizeof(candidate.commonLinear)) != 0 ||
                       !compatible(*candidate.prototype, *draw)) {
                return false;
            }

            GLuint buffer = 0;
            GLint first = 0;
            if (!draw->bindStaticVertexBuffer(&buffer, &first) || buffer == 0) return false;
            if (candidate.commonBuffer == 0)
                candidate.commonBuffer = buffer;
            else if (candidate.commonBuffer != buffer)
                return false;

            candidate.firsts.push_back(first);
            candidate.vertexCounts.push_back(draw->count);
            candidate.maxVertexCount = std::max(candidate.maxVertexCount, draw->count);
        }

        if (candidate.prototype == nullptr || candidate.commonBuffer == 0) return false;
        if (candidate.prototype->mode == GL_QUADS) {
            if (g_glFuncs.glMultiDrawElementsBaseVertex == nullptr) return false;
            candidate.elementCounts.reserve(candidate.vertexCounts.size());
            for (const GLsizei count : candidate.vertexCounts) {
                candidate.elementCounts.push_back((count / 4) * 6);
            }
            candidate.indexPointers.assign(candidate.vertexCounts.size(), nullptr);
        }
        candidate.valid = true;
        batch = &candidate;
    }

    const auto* prototype = batch->prototype;
    const GLuint commonBuffer = batch->commonBuffer;

    wrapper_client_state_guard_t wrapperState;
    auto& modelView = g_glstate_c.fpe_uniform.transformation.matrices[matrix_idx(GL_MODELVIEW)];
    const glm::mat4 savedModelView = modelView;
    modelView *= batch->commonLinear;

    auto replayState = prototype->layout;
    replayState.starting_pointer = nullptr;
    replayState.dirty = true;
    replayState.buffer_based = true;
    for (int i = 0; i < VERTEX_POINTER_COUNT; ++i) {
        if (((replayState.enabled_pointers >> i) & 1u) == 0) continue;
        replayState.attributes[i].pointer =
            reinterpret_cast<const void*>(prototype->packedOffsets[i]);
    }
    g_glstate_c.fpe_state.vertexpointer_array = replayState;
    g_glstate_c.fpe_state.normalized_vpa.reset();
    g_glstate_c.fpe_state.client_active_texture = prototype->clientActiveTexture;

    g_glFuncs.glBindBuffer(GL_ARRAY_BUFFER, commonBuffer);
    GLenum mode = prototype->mode;
    GLint firstForCommit = batch->firsts.front();
    GLsizei countForCommit = batch->maxVertexCount;
    const int drawElements =
        commit_fpe_state_on_draw(&mode, &firstForCommit, &countForCommit,
                                 static_cast<GLint>(commonBuffer));

    bool executed = false;
    // Re-checked at the call rather than relying on the early-out 150 lines up:
    // glMultiDrawArrays is absent from GLES core (EXT_multi_draw_arrays), so on a
    // backend without it this pointer is null and the call would segfault. Keeping
    // the test adjacent to the call is what makes that safe to read.
    if (drawElements == 0 && prototype->mode != GL_QUADS &&
        g_glFuncs.glMultiDrawArrays != nullptr) {
        g_glFuncs.glMultiDrawArrays(mode, batch->firsts.data(), batch->vertexCounts.data(),
                                    static_cast<GLsizei>(batch->vertexCounts.size()));
        executed = true;
    } else if (drawElements > 0 && prototype->mode == GL_QUADS &&
               g_glFuncs.glMultiDrawElementsBaseVertex != nullptr) {
        g_glFuncs.glMultiDrawElementsBaseVertex(
            mode, batch->elementCounts.data(), quad_index_type(), batch->indexPointers.data(),
            static_cast<GLsizei>(batch->elementCounts.size()), batch->firsts.data());
        executed = true;
    }

    modelView = savedModelView;
    return executed;
}

void glDrawArrays(GLenum mode, GLint first, GLsizei count) {
    if (!sfpewEnsureBackend()) return;
    (void)g_glstate; // entry strict resolve; commit/capture path reads the snapshot
    // A fixed-function draw re-establishes the wrapper's program/VAO/buffer
    // trio itself in commit_fpe_state_on_draw, so handing the app's state
    // back first would be paid twice per draw. Only a user-program draw
    // consumes the app's real bindings, and recording captures client arrays
    // under guards that expect the app's state - both take the full barrier.
    flushPendingImmediateDraws();
    if (sfpewLogicalProgram() != 0 || DisplayListManager::shouldRecord())
        sfpewFlushDeferredDrawState();
    if (!disableRecording && DisplayListManager::shouldRecord()) {
        std::unique_ptr<GLCmd> command;

        const GLint currentProgram = sfpewLogicalProgram();
        const auto& vertexArray = g_glstate_c.fpe_state.vertexpointer_array;
        const uint32_t vertexArrayMask = 1u << vp2idx(GL_VERTEX_ARRAY);

        if (currentProgram == 0 && first >= 0 && count > 0 &&
            (vertexArray.enabled_pointers & vertexArrayMask) != 0) {
            auto captured = std::make_unique<captured_draw_arrays_cmd_t>(
                mode, first, count, vertexArray, g_glstate_c.fpe_state.client_active_texture);
            if (captured->isValid()) command = std::move(captured);
        }

        // Never retain the caller's raw client-array pointers in a display
        // list. If a fixed-function draw cannot be snapshotted, omitting the
        // command is safer than replaying stale Java/LWJGL memory later.
        if (command != nullptr) displayListManager.recordCommand(std::move(command));

        if (DisplayListManager::shouldFinish()) return;
    }

    drawArraysNow(mode, first, count, false);
}

void glDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid* indices) {
    if (!sfpewEnsureBackend()) return;
    (void)g_glstate; // entry strict resolve; commit path reads the snapshot
    // Same contract as glDrawArrays: a fixed-function draw re-establishes
    // the wrapper's state itself; only user-program draws consume the app's.
    flushPendingImmediateDraws();
    if (sfpewLogicalProgram() != 0) sfpewFlushDeferredDrawState();
    // Display-list capture of indexed draws lands with plans/06; while
    // recording, execution matches the previous passthrough behavior.
    drawElementsNow(mode, count, type, indices);
}

// GL 1.2 core. start/end are a promise about the index range, not state, so
// the wrapper can honour it by simply forwarding to the glDrawElements
// logic: legacy modes get converted, fixed-function arrays get wired and
// the emulated alpha test uniforms get fed. Passing this through raw (the
// previous behavior) meant GL_QUADS died on GLES and cutout foliage drawn
// this way kept a stale alpha-test state.
void glDrawRangeElements(GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type,
                         const GLvoid* indices) {
    (void)start;
    (void)end;
    if (!sfpewEnsureBackend()) return;
    (void)g_glstate; // entry strict resolve, matching glDrawElements
    flushPendingImmediateDraws();
    if (sfpewLogicalProgram() != 0) sfpewFlushDeferredDrawState();
    drawElementsNow(mode, count, type, indices);
}

namespace {

// Multi-draw exists under two spellings and the backend may have either, both,
// or neither: desktop GL has the unsuffixed pair in 1.4 core, GLES has only the
// EXT pair from EXT_multi_draw_arrays. Prefer unsuffixed, fall back to EXT, and
// report null when the backend has no multi-draw at all (the caller then loops).
// True once the backend extension string has been checked.
// Result: nullptr = use loop; otherwise = pointer to call.
// Resolves once per context by caching on the first call after a backend is
// present. The result covers both the backend-type test (ES vs desktop) and the
// extension-string test, so callers never need to re-probe.
static auto pickMultiDrawArrays() {
    using Fn = decltype(g_glFuncs.glMultiDrawArrays);
    static Fn cached = nullptr;
    static bool probed = false;
    if (probed) return cached;
    if (g_glFuncs.glGetString == nullptr) return cached; // too early; re-probe next call
    probed = true;

    const char* ver = reinterpret_cast<const char*>(g_glFuncs.glGetString(0x1F02 /*GL_VERSION*/));
    if (ver == nullptr) return cached;
    const bool is_es = std::strstr(ver, "OpenGL ES") != nullptr;

    if (!is_es) {
        // Desktop GL: glMultiDrawArrays is 1.4 core, pointer is valid.
        cached = g_glFuncs.glMultiDrawArrays;
        return cached;
    }
    // GLES: only usable if the backend advertises GL_EXT_multi_draw_arrays.
    const char* ext = reinterpret_cast<const char*>(g_glFuncs.glGetString(0x1F03 /*GL_EXTENSIONS*/));
    if (ext != nullptr && std::strstr(ext, "GL_EXT_multi_draw_arrays") != nullptr) {
        cached = g_glFuncs.glMultiDrawArraysEXT;
    }
    return cached;
}

static auto pickMultiDrawElements() {
    using Fn = decltype(g_glFuncs.glMultiDrawElements);
    static Fn cached = nullptr;
    static bool probed = false;
    if (probed) return cached;
    if (g_glFuncs.glGetString == nullptr) return cached;
    probed = true;

    const char* ver = reinterpret_cast<const char*>(g_glFuncs.glGetString(0x1F02));
    if (ver == nullptr) return cached;
    const bool is_es = std::strstr(ver, "OpenGL ES") != nullptr;

    if (!is_es) {
        cached = g_glFuncs.glMultiDrawElements;
        return cached;
    }
    const char* ext = reinterpret_cast<const char*>(g_glFuncs.glGetString(0x1F03));
    if (ext != nullptr && std::strstr(ext, "GL_EXT_multi_draw_arrays") != nullptr) {
        cached = g_glFuncs.glMultiDrawElementsEXT;
    }
    return cached;
}

decltype(g_glFuncs.glMultiDrawArrays) nativeMultiDrawArrays() {
    return pickMultiDrawArrays();
}

decltype(g_glFuncs.glMultiDrawElements) nativeMultiDrawElements() {
    return pickMultiDrawElements();
}

// One native multi-draw replaces the loop only when no sub-draw needs individual
// work. Two conditions, both invariant across the sub-draws of a single call:
//
//  - the app owns the vertex state, so there is nothing to commit or upload
//    per sub-draw. This is the same predicate drawArraysNow uses to reach its
//    passthrough branch, minus the per-draw first/count tests.
//  - the primitive mode survives to the backend unchanged.
//
// The fixed-function path deliberately keeps looping: commit_fpe_state_on_draw
// rewrites first/count per draw (GL_QUADS expands to a different index count)
// and uploads only the range that draw covers, so the sub-draws are not
// interchangeable.
bool appOwnsVertexStateForPassthrough() {
    if (g_glstate_c.render_mode != GL_RENDER) return false;
    const GLint current_program = sfpewLogicalProgram();
    if (current_program == 0) return false;
    const auto& vertex_array = g_glstate_c.fpe_state.vertexpointer_array;
    const uint32_t vertex_array_mask = 1u << vp2idx(GL_VERTEX_ARRAY);
    // With fixed-function arrays live the user-program path wires them per draw.
    return (vertex_array.enabled_pointers & vertex_array_mask) == 0;
}

// The mode the backend can be handed directly, or GL_NONE when it needs a
// per-draw rewrite. QUAD_STRIP/POLYGON are vertex-order compatible so the swap
// applies uniformly to every sub-draw; GL_QUADS is not, since it becomes an
// indexed draw whose index count differs per sub-draw.
GLenum nativeMultiDrawMode(GLenum mode) {
    switch (mode) {
    case GL_QUADS:
        return GL_NONE;
    case GL_QUAD_STRIP:
        return GL_TRIANGLE_STRIP;
    case GL_POLYGON:
        return GL_TRIANGLE_FAN;
    default:
        return mode;
    }
}

} // namespace

// GL 1.4 core. Forwards to the backend's multi-draw when nothing needs doing per
// sub-draw, otherwise loops over the single-draw path so legacy modes and the
// fixed-function/user-program plumbing still apply to each one.
void glMultiDrawArrays(GLenum mode, const GLint* first, const GLsizei* count, GLsizei drawcount) {
    if (!sfpewEnsureBackend()) return;
    if (drawcount < 0) {
        g_glstate.set_error(GL_INVALID_VALUE);
        return;
    }
    if (first == nullptr || count == nullptr) return;
    sfpewEntryBarrier();

    const auto native = nativeMultiDrawArrays();
    const GLenum native_mode = nativeMultiDrawMode(mode);
    if (native != nullptr && native_mode != GL_NONE && appOwnsVertexStateForPassthrough()) {
        sfpewFeedUserProgramUniforms((GLuint)sfpewLogicalProgram());
        native(native_mode, first, count, drawcount);
        return;
    }

    for (GLsizei i = 0; i < drawcount; ++i) {
        if (count[i] <= 0) continue;
        drawArraysNow(mode, first[i], count[i], false);
    }
}

void glMultiDrawElements(GLenum mode, const GLsizei* count, GLenum type,
                         const GLvoid* const* indices, GLsizei drawcount) {
    if (!sfpewEnsureBackend()) return;
    if (drawcount < 0) {
        g_glstate.set_error(GL_INVALID_VALUE);
        return;
    }
    if (count == nullptr || indices == nullptr) return;
    sfpewEntryBarrier();

    const auto native = nativeMultiDrawElements();
    const GLenum native_mode = nativeMultiDrawMode(mode);
    if (native != nullptr && native_mode != GL_NONE && appOwnsVertexStateForPassthrough()) {
        sfpewFeedUserProgramUniforms((GLuint)sfpewLogicalProgram());
        native(native_mode, count, type, indices, drawcount);
        return;
    }

    for (GLsizei i = 0; i < drawcount; ++i) {
        if (count[i] <= 0) continue;
        drawElementsNow(mode, count[i], type, indices[i]);
    }
}

