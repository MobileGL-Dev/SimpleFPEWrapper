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
    void execute() const override {
        if (!valid) return;

        wrapper_client_state_guard_t wrapperState;

        auto replayState = layout;
        replayState.starting_pointer = nullptr;
        replayState.dirty = true;
        const bool useStaticBuffer = bindStaticVertexBuffer();
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
        drawArraysNow(mode, 0, count, true,
                      useStaticBuffer ? static_cast<GLint>(vertexBuffer) : 0);
    }

private:
    bool bindStaticVertexBuffer() const {
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
    mutable GLuint vertexBuffer = 0;
    mutable bool vertexBufferUploaded = false;
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
        if (doDrawElement > 0)
            g_glFuncs.glDrawElements(mode, count, quad_index_type(), (void*)0);
        else
            g_glFuncs.glDrawArrays(mode, first, count);
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
        g_glFuncs.glDrawElements(mode, count, quad_index_type(), (void*)0);
    } else {
        g_glFuncs.glDrawArrays(mode, first, count);
    }
}

} // namespace

void glDrawArrays(GLenum mode, GLint first, GLsizei count) {
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

