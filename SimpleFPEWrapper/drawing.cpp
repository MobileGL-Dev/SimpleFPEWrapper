// SimpleFPEWrapper - SimpleFPEWrapper/drawing.cpp
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

#include "init.h"

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
        if (allocation == nullptr || data == nullptr || size == 0 || stride == 0 ||
            g_eglFuncs.eglGetCurrentContext == nullptr) {
            return false;
        }

        const EGLContext currentContext = g_eglFuncs.eglGetCurrentContext();
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

        std::memcpy(mapped + offset, data, size);
        allocation->buffer = buffer;
        allocation->offset = offset;
        allocation->size = size;
        allocation->generation = generation;
        return true;
    }

    bool isCurrent(const display_list_vertex_allocation_t& allocation) const {
        return allocation.buffer != 0 && allocation.buffer == buffer &&
               allocation.generation == generation && mapped != nullptr;
    }

    void release(display_list_vertex_allocation_t* allocation) {
        if (allocation == nullptr || allocation->buffer == 0) return;

        const display_list_vertex_allocation_t old = *allocation;
        *allocation = {};
        if (!isCurrent(old) || g_eglFuncs.eglGetCurrentContext == nullptr ||
            g_eglFuncs.eglGetCurrentContext() != context) {
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
        // Reusing mapped bytes while an earlier draw is in flight is unsafe.
    }

private:
    static size_t alignTo(size_t value, size_t alignment) {
        const size_t remainder = value % alignment;
        return remainder == 0 ? value : value + alignment - remainder;
    }

    void resetForContext(EGLContext currentContext) {
        context = currentContext;
        buffer = 0;
        mapped = nullptr;
        tail = 0;
        storageAttempted = false;
        freeBlocks.clear();
        retired.clear();
        ++generation;
    }

    bool ensureStorage() {
        if (mapped != nullptr) return true;
        if (storageAttempted) return false;
        storageAttempted = true;

        auto storage = g_glFuncs.glBufferStorage != nullptr ? g_glFuncs.glBufferStorage
                                                            : g_glFuncs.glBufferStorageEXT;
        if (storage == nullptr || g_glFuncs.glMapBufferRange == nullptr ||
            g_glFuncs.glGenBuffers == nullptr || g_glFuncs.glDeleteBuffers == nullptr) {
            return false;
        }

        array_buffer_binding_guard_t bindingState;
        g_glFuncs.glGenBuffers(1, &buffer);
        if (buffer == 0) return false;
        g_glFuncs.glBindBuffer(GL_ARRAY_BUFFER, buffer);

        constexpr GLbitfield mapFlags =
            GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;
        constexpr GLbitfield storageFlags = mapFlags | GL_DYNAMIC_STORAGE_BIT;
        storage(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(kDisplayListArenaCapacity), nullptr,
                storageFlags);
        mapped = static_cast<uint8_t*>(g_glFuncs.glMapBufferRange(
            GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(kDisplayListArenaCapacity), mapFlags));
        if (mapped == nullptr) {
            g_glFuncs.glDeleteBuffers(1, &buffer);
            buffer = 0;
            return false;
        }
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
    uint8_t* mapped = nullptr;
    size_t tail = 0;
    uint64_t generation = 0;
    bool storageAttempted = false;
    std::vector<block_t> freeBlocks;
    std::vector<retired_block_t> retired;
};

display_list_vertex_arena_t displayListVertexArena;

struct wrapper_client_state_guard_t {
    vertex_pointer_array_t vertexPointerArray = g_glstate.fpe_state.vertexpointer_array;
    vertex_pointer_array_t normalizedVertexPointerArray = g_glstate.fpe_state.normalized_vpa;
    GLenum clientActiveTexture = g_glstate.fpe_state.client_active_texture;

    wrapper_client_state_guard_t() = default;

    ~wrapper_client_state_guard_t() {
        g_glstate.fpe_state.vertexpointer_array = vertexPointerArray;
        g_glstate.fpe_state.normalized_vpa = normalizedVertexPointerArray;
        g_glstate.fpe_state.client_active_texture = clientActiveTexture;
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
        displayListVertexArena.release(&arenaAllocation);
        if (vertexBuffer == 0 || g_glFuncs.glDeleteBuffers == nullptr ||
            g_eglFuncs.eglGetCurrentContext == nullptr ||
            g_eglFuncs.eglGetCurrentContext() == EGL_NO_CONTEXT) {
            return;
        }

        // Deleting a buffer clears backend VAO/binding references. Scrub the
        // matching wrapper cache as well so a subsequently recycled GL name
        // cannot make send_vertex_attributes incorrectly skip its rebind.
        if (g_glstate.fpe_vertex_binding_valid &&
            g_glstate.fpe_vertex_binding_buffer == vertexBuffer) {
            g_glstate.fpe_vertex_binding_valid = false;
        }
        for (auto& attribute : g_glstate.fpe_vertex_attributes) {
            if (!attribute.separate_binding && attribute.array_buffer == vertexBuffer) {
                attribute.pointer_valid = false;
            }
        }
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

        g_glstate.fpe_state.vertexpointer_array = replayState;
        g_glstate.fpe_state.normalized_vpa.reset();
        g_glstate.fpe_state.client_active_texture = clientActiveTexture;

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
        if (!fpe_inited && init_fpe() != 0) return false;

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
            if ((!fpe_inited && init_fpe() != 0) || g_glFuncs.glGenBuffers == nullptr ||
                g_glFuncs.glBufferData == nullptr) {
                return false;
            }
            g_glFuncs.glGenBuffers(1, &vertexBuffer);
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

    friend bool ::tryExecuteCapturedDisplayLists(const std::vector<GLuint>& listIds);
};

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
        if (doDrawElement > 0) {
            if (first != 0 && g_glFuncs.glDrawElementsBaseVertex != nullptr)
                g_glFuncs.glDrawElementsBaseVertex(mode, count, quad_index_type(), (void*)0, first);
            else
                g_glFuncs.glDrawElements(mode, count, quad_index_type(), (void*)0);
        } else {
            g_glFuncs.glDrawArrays(mode, first, count);
        }
        return;
    }

    GLint current_program = 0;
    g_glFuncs.glGetIntegerv(GL_CURRENT_PROGRAM, &current_program);

    const auto& vertex_array = g_glstate.fpe_state.vertexpointer_array;
    const uint32_t vertex_array_mask = 1u << vp2idx(GL_VERTEX_ARRAY);
    if ((!forceFixedFunction && current_program != 0) || first < 0 || count < 0 ||
        !(vertex_array.enabled_pointers & vertex_array_mask)) {
        g_glFuncs.glDrawArrays(mode, first, count);
        return;
    }

    fpe_backend_draw_state_guard_t backend_state(current_program);
    GLint attributeArrayBuffer = backend_state.array_buffer;
    if (arrayBufferOverride >= 0) {
        attributeArrayBuffer = arrayBufferOverride;
        g_glFuncs.glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(arrayBufferOverride));
    }
    int do_draw_element = commit_fpe_state_on_draw(&mode, &first, &count, attributeArrayBuffer);
    if (do_draw_element < 0) {
        return;
    } else if (do_draw_element > 0) {
        if (first != 0 && g_glFuncs.glDrawElementsBaseVertex != nullptr)
            g_glFuncs.glDrawElementsBaseVertex(mode, count, quad_index_type(), (void*)0, first);
        else
            g_glFuncs.glDrawElements(mode, count, quad_index_type(), (void*)0);
    } else {
        g_glFuncs.glDrawArrays(mode, first, count);
    }
}

} // namespace

bool tryExecuteCapturedDisplayLists(const std::vector<GLuint>& listIds) {
    if (listIds.size() < 2 || g_glFuncs.glMultiDrawArrays == nullptr ||
        g_glstate.fpe_uniform.transformation.matrix_mode != GL_MODELVIEW) {
        return false;
    }

    const captured_draw_arrays_cmd_t* prototype = nullptr;
    glm::mat4 commonLinear(1.0f);
    GLuint commonBuffer = 0;

    thread_local std::vector<GLint> firsts;
    thread_local std::vector<GLsizei> counts;
    thread_local std::vector<const void*> indexPointers;
    firsts.clear();
    counts.clear();
    firsts.reserve(listIds.size());
    counts.reserve(listIds.size());

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

    for (const GLuint listId : listIds) {
        const DisplayList* list = DisplayListManager::findList(listId);
        if (list == nullptr || list->size() != 1) return false;

        glm::mat4 linear(1.0f);
        const GLCmd* batchCommand = list->front()->capturedDrawForBatch(&linear);
        const auto* draw = dynamic_cast<const captured_draw_arrays_cmd_t*>(batchCommand);
        if (draw == nullptr) return false;

        if (prototype == nullptr) {
            prototype = draw;
            commonLinear = linear;
        } else if (std::memcmp(&commonLinear, &linear, sizeof(commonLinear)) != 0 ||
                   !compatible(*prototype, *draw)) {
            return false;
        }

        GLuint buffer = 0;
        GLint first = 0;
        if (!draw->bindStaticVertexBuffer(&buffer, &first) || buffer == 0) return false;
        if (commonBuffer == 0)
            commonBuffer = buffer;
        else if (commonBuffer != buffer)
            return false;

        firsts.push_back(first);
        counts.push_back(draw->count);
    }

    if (prototype == nullptr || commonBuffer == 0) return false;
    if (prototype->mode == GL_QUADS && g_glFuncs.glMultiDrawElementsBaseVertex == nullptr) {
        return false;
    }

    wrapper_client_state_guard_t wrapperState;
    auto& modelView = g_glstate.fpe_uniform.transformation.matrices[matrix_idx(GL_MODELVIEW)];
    const glm::mat4 savedModelView = modelView;
    modelView *= commonLinear;

    auto replayState = prototype->layout;
    replayState.starting_pointer = nullptr;
    replayState.dirty = true;
    replayState.buffer_based = true;
    for (int i = 0; i < VERTEX_POINTER_COUNT; ++i) {
        if (((replayState.enabled_pointers >> i) & 1u) == 0) continue;
        replayState.attributes[i].pointer =
            reinterpret_cast<const void*>(prototype->packedOffsets[i]);
    }
    g_glstate.fpe_state.vertexpointer_array = replayState;
    g_glstate.fpe_state.normalized_vpa.reset();
    g_glstate.fpe_state.client_active_texture = prototype->clientActiveTexture;

    g_glFuncs.glBindBuffer(GL_ARRAY_BUFFER, commonBuffer);
    GLenum mode = prototype->mode;
    GLint firstForCommit = firsts.front();
    const GLsizei maxVertexCount = *std::max_element(counts.begin(), counts.end());
    GLsizei countForCommit = maxVertexCount;
    const int drawElements =
        commit_fpe_state_on_draw(&mode, &firstForCommit, &countForCommit,
                                 static_cast<GLint>(commonBuffer));

    bool executed = false;
    if (drawElements == 0 && prototype->mode != GL_QUADS) {
        g_glFuncs.glMultiDrawArrays(mode, firsts.data(), counts.data(),
                                    static_cast<GLsizei>(counts.size()));
        executed = true;
    } else if (drawElements > 0 && prototype->mode == GL_QUADS) {
        for (auto& count : counts) count = (count / 4) * 6;
        indexPointers.assign(counts.size(), nullptr);
        g_glFuncs.glMultiDrawElementsBaseVertex(
            mode, counts.data(), quad_index_type(), indexPointers.data(),
            static_cast<GLsizei>(counts.size()), firsts.data());
        executed = true;
    }

    modelView = savedModelView;
    return executed;
}

void glDrawArrays(GLenum mode, GLint first, GLsizei count) {
    flushPendingImmediateDraws();
    if (!disableRecording && DisplayListManager::shouldRecord()) {
        std::unique_ptr<GLCmd> command;

        GLint currentProgram = 0;
        g_glFuncs.glGetIntegerv(GL_CURRENT_PROGRAM, &currentProgram);
        const auto& vertexArray = g_glstate.fpe_state.vertexpointer_array;
        const uint32_t vertexArrayMask = 1u << vp2idx(GL_VERTEX_ARRAY);

        if (currentProgram == 0 && first >= 0 && count > 0 &&
            (vertexArray.enabled_pointers & vertexArrayMask) != 0) {
            auto captured = std::make_unique<captured_draw_arrays_cmd_t>(
                mode, first, count, vertexArray, g_glstate.fpe_state.client_active_texture);
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

