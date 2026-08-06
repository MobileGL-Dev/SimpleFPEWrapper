// SimpleFPEWrapper - SimpleFPEWrapper/fpe/list_capture.cpp
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

#include "../init.h"
#include "../log.h"

#include "fpe.hpp"
#include "list.h"
#include "list_diagnostics.h"
#include "drawing1x.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <vector>

namespace {

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

constexpr size_t kDisplayListArenaCapacity = 64u * 1024u * 1024u;

// Indices 0,1,2,... - the identity permutation. Drawing `count` of them with
// a base vertex reaches exactly the vertices glMultiDrawArrays would have
// drawn for that sub-draw, so a group whose backend implements only the
// indexed multi-draw still goes out in one call rather than one per list.
// Grown on demand and kept for the process.
GLuint identityIndexBuffer(GLsizei vertices, GLenum* type) {
    static thread_local GLuint buffer = 0;
    static thread_local GLsizei capacity = 0;
    static thread_local GLenum storedType = GL_UNSIGNED_SHORT;
    if (vertices <= 0 || g_glFuncs.glGenBuffers == nullptr || g_glFuncs.glBufferData == nullptr ||
        g_glFuncs.glBindBuffer == nullptr) {
        return 0;
    }
    const GLenum wanted = vertices > (GLsizei)std::numeric_limits<uint16_t>::max()
                              ? GL_UNSIGNED_INT
                              : GL_UNSIGNED_SHORT;
    if (buffer != 0 && capacity >= vertices && storedType == wanted) {
        *type = storedType;
        return buffer;
    }
    if (buffer == 0) {
        g_glFuncs.glGenBuffers(1, &buffer);
        sfpewNoteInternalBuffer(buffer);
        if (buffer == 0) return 0;
    }
    sfpewBackendBindElementBuffer(buffer);
    g_glstate_c.fpe_state.fpe_ibo_bound = false; // fpe_ibo is no longer the bound one
    if (wanted == GL_UNSIGNED_SHORT) {
        thread_local std::vector<uint16_t> indices;
        indices.resize((size_t)vertices);
        for (GLsizei i = 0; i < vertices; ++i) indices[(size_t)i] = (uint16_t)i;
        g_glFuncs.glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                               (GLsizeiptr)(indices.size() * sizeof(uint16_t)), indices.data(),
                               GL_STATIC_DRAW);
    } else {
        thread_local std::vector<uint32_t> indices;
        indices.resize((size_t)vertices);
        for (GLsizei i = 0; i < vertices; ++i) indices[(size_t)i] = (uint32_t)i;
        g_glFuncs.glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                               (GLsizeiptr)(indices.size() * sizeof(uint32_t)), indices.data(),
                               GL_STATIC_DRAW);
    }
    capacity = vertices;
    storedType = wanted;
    *type = storedType;
    return buffer;
}

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
        if (currentContext == EGL_NO_CONTEXT) return listLogArenaFail("no-current-context", size);
        if (context != currentContext) resetForContext(currentContext);
        if (!ensureStorage()) return listLogArenaFail("storage-unavailable", size);

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
                    return listLogArenaFail("arena-full", size);
                }
            }
        }

        // Upload through GL_COPY_WRITE_BUFFER: not part of vertex-array state,
        // so no binding guard is needed and no shadow can observe it.
        g_glFuncs.glBindBuffer(GL_COPY_WRITE_BUFFER, buffer);
#if SFPEW_LIST_DEBUG
        if (listLogEnabled() && g_glFuncs.glGetError != nullptr) {
            while (g_glFuncs.glGetError() != GL_NO_ERROR) {}
        }
#endif
        g_glFuncs.glBufferSubData(GL_COPY_WRITE_BUFFER, static_cast<GLintptr>(offset),
                                  static_cast<GLsizeiptr>(size), data);
#if SFPEW_LIST_DEBUG
        if (listLogEnabled() && g_glFuncs.glGetError != nullptr) {
            const GLenum err = g_glFuncs.glGetError();
            if (err != GL_NO_ERROR && g_listLog.detailsArena++ < kListLogDetailLimit) {
                listLogLine(true, "LISTLOG arena SubData FAILED err=0x%x buffer=%u offset=%zu size=%zu "
                           "tail=%zu cap=%zu",
                           err, buffer, offset, size, tail, kDisplayListArenaCapacity);
            }
        }
#endif
        g_glFuncs.glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
        SFPEW_LISTLOG_TALLY(++g_listLog.arenaOk);
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
        if (g_glFuncs.glGetError != nullptr) {
            while (g_glFuncs.glGetError() != GL_NO_ERROR) {}
        }
        g_glFuncs.glBufferData(GL_COPY_WRITE_BUFFER,
                               static_cast<GLsizeiptr>(kDisplayListArenaCapacity), nullptr,
                               GL_STATIC_DRAW);
        const GLenum reserveError =
            g_glFuncs.glGetError != nullptr ? g_glFuncs.glGetError() : (GLenum)GL_NO_ERROR;
        if (reserveError != GL_NO_ERROR) {
            // Worth one line in any build: it changes how every captured list
            // is stored for the rest of the run.
            SFPEW_LOGW("display-list arena reservation of %zu bytes REFUSED (err=0x%x); captured "
                       "display lists fall back to one buffer each",
                       kDisplayListArenaCapacity, reserveError);
        }
#if SFPEW_LIST_DEBUG
        else if (listLogEnabled()) {
            listLogLine(false, "LISTLOG arena reserved %zu bytes as buffer %u",
                        kDisplayListArenaCapacity, buffer);
        }
#endif
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
    // Captured-draw replay overwrites vertexpointer_array's pointers with
    // arena/VBO byte offsets without going through gl*Pointer, so it must
    // also stand in for rememberClientArrayBufferBinding and update this
    // shadow itself (below) - otherwise commit_fpe_state_on_draw's
    // classifyClientArrays (plans/13) reads whatever the APP's own last
    // gl*Pointer binding happened to be, unrelated to what this replay is
    // actually drawing from. Saved/restored here so the app's own next draw
    // sees its own bindings again, not the replay's.
    GLuint clientArrayBufferBindings[VERTEX_POINTER_COUNT];

    wrapper_client_state_guard_t() {
        std::memcpy(clientArrayBufferBindings, g_glstate_c.fpe_state.client_array_buffer_bindings,
                    sizeof(clientArrayBufferBindings));
    }

    ~wrapper_client_state_guard_t() {
        g_glstate_c.fpe_state.vertexpointer_array = vertexPointerArray;
        g_glstate_c.fpe_state.normalized_vpa = normalizedVertexPointerArray;
        g_glstate_c.fpe_normalized_valid = false;
        g_glstate_c.fpe_state.client_active_texture = clientActiveTexture;
        std::memcpy(g_glstate_c.fpe_state.client_array_buffer_bindings, clientArrayBufferBindings,
                    sizeof(clientArrayBufferBindings));
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
    // Why capture() gave up, for SFPEW_LISTLOG. Never read on a hot path.
    const char* failReason = "unknown";
    int failAttribute = -1;
    GLint failSize = 0, failStride = 0, failBufferSize = 0;
    GLenum failType = 0;
    uintptr_t failPointer = 0;
    GLuint failBuffer = 0;
    size_t failSourceEnd = 0;
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
        SFPEW_LISTLOG_TALLY(++g_listLog.replayDraws);
        SFPEW_LISTLOG_TALLY(g_listLog.vertsDrawn += (unsigned long long)count);

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
            // Ground truth for classifyClientArrays (plans/13): this bypasses
            // gl*Pointer, so nothing else records which source this replay's
            // pointer values are relative to.
            g_glstate_c.fpe_state.client_array_buffer_bindings[i] =
                useStaticBuffer ? staticVertexBuffer : 0u;
        }

        g_glstate_c.fpe_state.vertexpointer_array = replayState;
        g_glstate_c.fpe_state.normalized_vpa.reset();
        g_glstate_c.fpe_normalized_valid = false;
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

        if (vertexBuffer == 0 && !listArenaDisabled() &&
            displayListVertexArena.allocate(vertexData.data(), vertexData.size(),
                                            static_cast<size_t>(layout.stride), &arenaAllocation)) {
            *drawFirst = static_cast<GLint>(arenaAllocation.offset / static_cast<size_t>(layout.stride));
            *selectedBuffer = arenaAllocation.buffer;
            return true;
        }

        if (vertexBuffer == 0) {
            if ((!g_glstate_c.fpe_ready && init_fpe() != 0) || g_glFuncs.glGenBuffers == nullptr ||
                g_glFuncs.glBufferData == nullptr) {
                SFPEW_LISTLOG_TALLY(++g_listLog.staticFail);
#if SFPEW_LIST_DEBUG
                if (listLogEnabled() && g_listLog.detailsArena++ < kListLogDetailLimit)
                    listLogLine(true, "LISTLOG static buffer unavailable (no glGenBuffers/glBufferData)");
#endif
                return false;
            }
            g_glFuncs.glGenBuffers(1, &vertexBuffer);
            sfpewNoteInternalBuffer(vertexBuffer);
            if (vertexBuffer == 0) {
                SFPEW_LISTLOG_TALLY(++g_listLog.staticFail);
#if SFPEW_LIST_DEBUG
                if (listLogEnabled() && g_listLog.detailsArena++ < kListLogDetailLimit)
                    listLogLine(true, "LISTLOG glGenBuffers returned 0 for a %zu byte list block",
                               vertexData.size());
#endif
                return false;
            }
            SFPEW_LISTLOG_TALLY(++g_listLog.dedicated);
        }

        if (!vertexBufferUploaded) {
            array_buffer_binding_guard_t bindingState;
            g_glFuncs.glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
#if SFPEW_LIST_DEBUG
            if (listLogEnabled() && g_glFuncs.glGetError != nullptr) {
                while (g_glFuncs.glGetError() != GL_NO_ERROR) {}
            }
#endif
            g_glFuncs.glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertexData.size()),
                                   vertexData.data(), GL_STATIC_DRAW);
#if SFPEW_LIST_DEBUG
            if (listLogEnabled() && g_glFuncs.glGetError != nullptr) {
                const GLenum err = g_glFuncs.glGetError();
                if (err != GL_NO_ERROR && g_listLog.detailsArena++ < kListLogDetailLimit) {
                    listLogLine(true, "LISTLOG dedicated buffer %u upload FAILED err=0x%x size=%zu",
                               vertexBuffer, err, vertexData.size());
                }
            }
#endif
            vertexBufferUploaded = true;
        }
        *selectedBuffer = vertexBuffer;
        *drawFirst = 0;
        return true;
    }

    bool capture(GLint first, const vertex_pointer_array_t& source) {
        if (first < 0 || count <= 0) {
            failReason = "first<0 or count<=0";
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
                failReason = "bad attribute size/type/stride";
                failAttribute = i;
                failSize = sourceAttribute.size;
                failType = sourceAttribute.type;
                failStride = sourceAttribute.stride;
                return false;
            }

            size_t elementBytes = 0;
            if (!checkedMultiply(static_cast<size_t>(sourceAttribute.size), componentBytes, &elementBytes) ||
                !alignSize(packedStride, componentBytes, &packedStride)) {
                failReason = "packed-stride overflow";
                failAttribute = i;
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
                failReason = "packed-offset overflow";
                failAttribute = i;
                return false;
            }
            if (componentBytes > largestAlignment) largestAlignment = componentBytes;

            layout.attributes[i] = sourceAttribute;
        }

        if (!alignSize(packedStride, largestAlignment, &packedStride) || packedStride == 0 ||
            packedStride > static_cast<size_t>(std::numeric_limits<GLsizei>::max())) {
            failReason = "final stride invalid";
            return false;
        }

        size_t allocationSize = 0;
        if (!checkedMultiply(static_cast<size_t>(count), packedStride, &allocationSize)) {
            failReason = "vertex block size overflow";
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
            if (!copyAttribute(attribute, first, packedStride)) {
                failAttribute = i;
                failSize = layout.attributes[i].size;
                failType = layout.attributes[i].type;
                failStride = layout.attributes[i].stride;
                failPointer = attribute.sourcePointer;
                failBuffer = attribute.sourceBuffer;
                return false;
            }
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
            if (attribute.sourcePointer < 4096) {
                failReason = "client pointer below 4096 (looks like a buffer offset)";
                return false;
            }
            source = reinterpret_cast<const uint8_t*>(firstByte);
        } else {
            if (g_glFuncs.glGetBufferParameteriv == nullptr || g_glFuncs.glMapBufferRange == nullptr ||
                g_glFuncs.glUnmapBuffer == nullptr) {
                failReason = "backend has no buffer mapping";
                return false;
            }

            g_glFuncs.glBindBuffer(GL_ARRAY_BUFFER, attribute.sourceBuffer);
            GLint bufferSize = 0;
            g_glFuncs.glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &bufferSize);
            if (bufferSize <= 0 || sourceEnd > static_cast<size_t>(bufferSize)) {
                failReason = "source VBO too small for the draw range";
                failBufferSize = bufferSize;
                failSourceEnd = sourceEnd;
                return false;
            }

            mappedBuffer = g_glFuncs.glMapBufferRange(GL_ARRAY_BUFFER, 0, bufferSize, GL_MAP_READ_BIT);
            if (mappedBuffer == nullptr) {
                failReason = "glMapBufferRange(READ) refused by the driver";
                failBufferSize = bufferSize;
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

        if (mappedBuffer != nullptr && g_glFuncs.glUnmapBuffer(GL_ARRAY_BUFFER) == GL_FALSE) {
            failReason = "glUnmapBuffer reported data loss";
            return false;
        }
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
    // Every draw in the group, so a cache hit can re-derive each one's
    // current vertex offset instead of trusting the offsets it recorded.
    std::vector<const captured_draw_arrays_cmd_t*> draws;
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

} // namespace

bool tryExecuteCapturedDisplayLists(const GLuint* listIds, size_t listCount) {
    if (listBatchDisabled()) return false;
    SFPEW_LISTLOG_TALLY(++g_listLog.batchTry);
    if (listIds == nullptr || listCount < 2 ||
        (g_glFuncs.glMultiDrawArrays == nullptr &&
         g_glFuncs.glMultiDrawElementsBaseVertex == nullptr) ||
        g_glstate_c.fpe_uniform.transformation.matrix_mode != GL_MODELVIEW) {
#if SFPEW_LIST_DEBUG
        if (listLogEnabled() && g_listLog.detailsBatch++ < kListLogDetailLimit) {
            listLogLine(false, "LISTLOG batch declined: count=%zu multiDraw=%s matrixMode=0x%x",
                       listCount, g_glFuncs.glMultiDrawArrays ? "yes" : "NO",
                       g_glstate_c.fpe_uniform.transformation.matrix_mode);
        }
#endif
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
        candidate.draws.clear();
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

            candidate.draws.push_back(draw);
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
        }
        // Needed by both indexed forms: the quad indices and the identity
        // indices are each drawn from offset zero.
        candidate.indexPointers.assign(candidate.vertexCounts.size(), nullptr);
        candidate.valid = true;
        batch = &candidate;
    }

    // A cached group only ever re-checked the FIRST list's arena allocation
    // and reused the recorded offset of every other one, on the reasoning
    // that a shared arena generation makes one allocation prove the rest.
    // It does not: an individual list can be re-uploaded to a different
    // slice while the group is still cached, and the group then draws that
    // list from wherever its old slice now points - the chunk lands as
    // another chunk's geometry or outside the buffer entirely. Re-deriving
    // the offsets costs one check per list against a rebuild's map lookups.
    for (size_t i = 0; i < batch->draws.size(); ++i) {
        GLuint buffer = 0;
        GLint first = 0;
        if (!batch->draws[i]->bindStaticVertexBuffer(&buffer, &first) ||
            buffer != batch->commonBuffer) {
#if SFPEW_LIST_DEBUG
            if (listLogEnabled() && g_listLog.detailsBatch++ < kListLogDetailLimit) {
                listLogLine(true,
                            "LISTLOG batch list %u no longer shares the group's buffer "
                            "(buffer=%u want=%u); replaying the group one list at a time",
                            batch->listIds[i], buffer, batch->commonBuffer);
            }
#endif
            batch->valid = false;
            return false;
        }
        if (first != batch->firsts[i]) {
#if SFPEW_LIST_DEBUG
            if (listLogEnabled() && g_listLog.detailsBatch++ < kListLogDetailLimit) {
                listLogLine(true,
                            "LISTLOG batch list %u moved: cached first=%d now %d (stride=%d) - "
                            "the cached group would have drawn the wrong vertices",
                            batch->listIds[i], batch->firsts[i], first, batch->prototype->layout.stride);
            }
#endif
            batch->firsts[i] = first;
        }
    }

    SFPEW_LISTLOG_TALLY(++g_listLog.batchOk);
    SFPEW_LISTLOG_TALLY(g_listLog.drawnLists += (unsigned)listCount);
#if SFPEW_LIST_DEBUG
    for (const GLsizei vertices : batch->vertexCounts)
        g_listLog.vertsDrawn += (unsigned long long)vertices;
#endif
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
        // Ground truth for classifyClientArrays (plans/13): this bypasses
        // gl*Pointer, so nothing else records that these offsets are
        // relative to commonBuffer.
        g_glstate_c.fpe_state.client_array_buffer_bindings[i] = commonBuffer;
    }
    g_glstate_c.fpe_state.vertexpointer_array = replayState;
    g_glstate_c.fpe_state.normalized_vpa.reset();
    g_glstate_c.fpe_normalized_valid = false;
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
    // MobileGlues resolves a glMultiDrawArrays pointer but has no
    // implementation behind it up to V1.3.5: the call returns having drawn
    // nothing, which is why batched chunks vanished while every list was
    // submitted and every offset was right. Its indexed relatives work, so
    // only the array form needs the loop.
    // MobileGlues resolves a glMultiDrawArrays pointer but has nothing behind
    // it up to V1.3.5: the call returns having drawn nothing, which is why
    // batched chunks vanished while every list was submitted and every offset
    // was right. Its INDEXED multi-draws are implemented, and identity indices
    // with a base vertex reach the same vertices the array form would, so the
    // group still leaves in a single call there.
    const bool avoidMultiArrays = listBatchLoopOnly() || sfpewBackendLacksMultiDrawArrays();
    if (drawElements == 0 && prototype->mode != GL_QUADS) {
        GLenum identityType = GL_UNSIGNED_SHORT;
        GLuint identityBuffer = 0;
        if (!avoidMultiArrays && g_glFuncs.glMultiDrawArrays != nullptr) {
            g_glFuncs.glMultiDrawArrays(mode, batch->firsts.data(), batch->vertexCounts.data(),
                                        static_cast<GLsizei>(batch->vertexCounts.size()));
            executed = true;
        } else if (!listBatchLoopOnly() && g_glFuncs.glMultiDrawElementsBaseVertex != nullptr &&
                   (identityBuffer = identityIndexBuffer(batch->maxVertexCount, &identityType)) !=
                       0) {
            sfpewBackendBindElementBuffer(identityBuffer);
            g_glstate_c.fpe_state.fpe_ibo_bound = false;
            g_glFuncs.glMultiDrawElementsBaseVertex(
                mode, batch->vertexCounts.data(), identityType, batch->indexPointers.data(),
                static_cast<GLsizei>(batch->vertexCounts.size()), batch->firsts.data());
            executed = true;
        } else if (g_glFuncs.glDrawArrays != nullptr) {
            // Neither multi-draw usable: still one state commit for the whole
            // group, one draw per list.
            for (size_t i = 0; i < batch->firsts.size(); ++i)
                g_glFuncs.glDrawArrays(mode, batch->firsts[i], batch->vertexCounts[i]);
            executed = true;
        }
    } else if (drawElements > 0 && prototype->mode == GL_QUADS &&
               g_glFuncs.glMultiDrawElementsBaseVertex != nullptr) {
        g_glFuncs.glMultiDrawElementsBaseVertex(
            mode, batch->elementCounts.data(), quad_index_type(), batch->indexPointers.data(),
            static_cast<GLsizei>(batch->elementCounts.size()), batch->firsts.data());
        executed = true;
    }

#if SFPEW_LIST_DEBUG
    if (listLogEnabled() && executed && g_listLog.detailsBatch++ < kListLogDetailLimit) {
        listLogLine(false,
                    "LISTLOG batch %s mode=0x%x n=%zu maxVerts=%d buffer=%u stride=%d "
                    "first[0..2]=%d,%d,%d count[0..2]=%d,%d,%d",
                    drawElements > 0 ? "multiElements(quads)"
                                     : (avoidMultiArrays ? "multiElements(identity)" : "multiArrays"),
                    mode, batch->firsts.size(), batch->maxVertexCount, commonBuffer,
                    prototype->layout.stride, batch->firsts[0],
                    batch->firsts.size() > 1 ? batch->firsts[1] : -1,
                    batch->firsts.size() > 2 ? batch->firsts[2] : -1, batch->vertexCounts[0],
                    batch->vertexCounts.size() > 1 ? batch->vertexCounts[1] : -1,
                    batch->vertexCounts.size() > 2 ? batch->vertexCounts[2] : -1);
    }
#endif
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
            if (captured->isValid()) {
                SFPEW_LISTLOG_TALLY(++g_listLog.captureOk);
                SFPEW_LISTLOG_TALLY(g_listLog.vertsCompiled += (unsigned long long)count);
                command = std::move(captured);
            } else {
                SFPEW_LISTLOG_TALLY(++g_listLog.captureFail);
#if SFPEW_LIST_DEBUG
                if (listLogEnabled() && g_listLog.detailsCapture++ < kListLogDetailLimit) {
                    const auto& vp = vertexArray.attributes[captured->failAttribute >= 0
                                                                ? captured->failAttribute
                                                                : 0];
                    listLogLine(true, "LISTLOG DROP capture failed: list=%u mode=0x%x first=%d count=%d "
                               "enabled=0x%x stride=%d | reason=%s attr=%d size=%d type=0x%x "
                               "attrStride=%d ptr=%p buf=%u bufSize=%d needEnd=%zu",
                               DisplayListManager::currentList(), mode, first, count,
                               vertexArray.enabled_pointers, vertexArray.stride,
                               captured->failReason, captured->failAttribute, captured->failSize,
                               captured->failType, captured->failStride,
                               captured->failPointer != 0 ? (const void*)captured->failPointer
                                                          : vp.pointer,
                               captured->failBuffer, captured->failBufferSize,
                               captured->failSourceEnd);
                }
#endif
            }
        } else {
            SFPEW_LISTLOG_TALLY(++g_listLog.preconditionSkip);
#if SFPEW_LIST_DEBUG
            if (listLogEnabled() && g_listLog.detailsPrecondition++ < kListLogDetailLimit) {
                listLogLine(true, "LISTLOG DROP not capturable: list=%u mode=0x%x first=%d count=%d "
                           "program=%d enabled=0x%x (GL_VERTEX_ARRAY %s)",
                           DisplayListManager::currentList(), mode, first, count, currentProgram,
                           vertexArray.enabled_pointers,
                           (vertexArray.enabled_pointers & vertexArrayMask) ? "on" : "OFF");
            }
#endif
        }

        // Never retain the caller's raw client-array pointers in a display
        // list. If a fixed-function draw cannot be snapshotted, omitting the
        // command is safer than replaying stale Java/LWJGL memory later.
        if (command != nullptr) displayListManager.recordCommand(std::move(command));

        if (DisplayListManager::shouldFinish()) return;
    }

    drawArraysNow(mode, first, count, false);
}
