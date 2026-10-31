// SimpleFPEWrapper - SimpleFPEWrapper/perf_backend.cpp
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

#include "perf_backend.h"

#if SFPEW_PERF_COUNTERS
#include "backend/loader.h"

#include <cstring>
#include <initializer_list>

sfpew_perf_slot_t g_sfpew_perf_backend[kSfpewPerfBackendSlots];

namespace {

const char* g_backend_names[kSfpewPerfBackendSlots];
const void* g_backend_fields[kSfpewPerfBackendSlots];
sfpew_perf_category_t g_backend_categories[kSfpewPerfBackendSlots];
unsigned g_backend_count = 0;

bool startsWith(const char* name, const char* prefix) {
    return std::strncmp(name, prefix, std::strlen(prefix)) == 0;
}

bool isOneOf(const char* name, std::initializer_list<const char*> names) {
    for (const char* candidate : names)
        if (std::strcmp(name, candidate) == 0) return true;
    return false;
}

// What an entry point does, from its name. Order matters where prefixes
// overlap: glDrawBuffers is state, not a draw; glDeleteSync is sync, not
// object lifetime; glBindAttribLocation is program setup, not a bind.
sfpew_perf_category_t classify(const char* name) {
    using C = sfpew_perf_category_t;
    if (startsWith(name, "egl")) return C::egl;
    if (isOneOf(name, {"glDrawBuffers", "glReadBuffer"})) return C::state;
    if (isOneOf(name, {"glFinish", "glFlush", "glFenceSync", "glClientWaitSync", "glWaitSync",
                       "glDeleteSync", "glIsSync", "glGetSynciv"}))
        return C::sync;
    if (isOneOf(name, {"glBindAttribLocation", "glBindFragDataLocation", "glBindFragDataLocationEXT",
                       "glUniformBlockBinding", "glProgramBinary", "glGetProgramBinary",
                       "glProgramParameteri"}))
        return C::shader;
    if (startsWith(name, "glDraw") || startsWith(name, "glMultiDraw")) return C::draw;
    if (startsWith(name, "glClear")) return C::clear;
    if (startsWith(name, "glBind") || isOneOf(name, {"glUseProgram", "glActiveTexture"}))
        return C::bind;
    if (startsWith(name, "glVertexAttrib") ||
        isOneOf(name, {"glEnableVertexAttribArray", "glDisableVertexAttribArray"}))
        return C::vertex_setup;
    if (startsWith(name, "glBufferData") || startsWith(name, "glBufferSubData") ||
        startsWith(name, "glBufferStorage") || startsWith(name, "glMapBuffer") ||
        startsWith(name, "glUnmapBuffer") || startsWith(name, "glFlushMappedBufferRange") ||
        isOneOf(name, {"glCopyBufferSubData", "glInvalidateBufferData"}))
        return C::buffer;
    if (startsWith(name, "glTexImage") || startsWith(name, "glTexSubImage") ||
        startsWith(name, "glCompressedTex") || startsWith(name, "glCopyTex") ||
        startsWith(name, "glTexStorage") || startsWith(name, "glGenerateMipmap"))
        return C::texture;
    if (isOneOf(name, {"glReadPixels", "glGetTexImage", "glGetBufferSubData"})) return C::readback;
    if (startsWith(name, "glUniform") || startsWith(name, "glProgramUniform")) return C::uniform;
    if (isOneOf(name, {"glShaderSource", "glCompileShader", "glLinkProgram", "glAttachShader",
                       "glDetachShader", "glCreateShader", "glCreateProgram", "glDeleteShader",
                       "glDeleteProgram", "glValidateProgram", "glGetShaderInfoLog",
                       "glGetProgramInfoLog", "glGetShaderSource"}))
        return C::shader;
    if (startsWith(name, "glGen") || startsWith(name, "glDelete")) return C::object;
    if (startsWith(name, "glGet") || startsWith(name, "glIs") ||
        isOneOf(name, {"glCheckFramebufferStatus"}))
        return C::query;
    if (startsWith(name, "glEnable") || startsWith(name, "glDisable") || startsWith(name, "glBlend") ||
        startsWith(name, "glDepth") || startsWith(name, "glStencil") || startsWith(name, "glColorMask") ||
        startsWith(name, "glTexParameter") || startsWith(name, "glSampler") ||
        startsWith(name, "glPixelStore") || startsWith(name, "glFramebuffer") ||
        startsWith(name, "glRenderbuffer") || startsWith(name, "glPolygon") ||
        isOneOf(name, {"glCullFace", "glFrontFace", "glViewport", "glScissor", "glLineWidth",
                       "glHint", "glSampleCoverage", "glLogicOp", "glPointSize"}))
        return C::state;
    return C::other;
}

// ---- volume-carrying calls --------------------------------------------
// Each hook records what its call carries and forwards to the counting
// thunk it replaced, which keeps the per-function count.

uint64_t positive(GLsizei value) { return value > 0 ? static_cast<uint64_t>(value) : 0u; }
uint64_t instances(GLsizei value) { return value > 0 ? static_cast<uint64_t>(value) : 0u; }

// Bytes one pixel of client data occupies for a format/type pair. Payload
// size only: unpack row length and alignment padding are not counted.
uint64_t pixelBytes(GLenum format, GLenum type) {
    switch (type) {
    case GL_UNSIGNED_SHORT_5_6_5:
    case GL_UNSIGNED_SHORT_4_4_4_4:
    case GL_UNSIGNED_SHORT_5_5_5_1:
        return 2;
    case GL_UNSIGNED_INT_2_10_10_10_REV:
    case GL_UNSIGNED_INT_10F_11F_11F_REV:
    case GL_UNSIGNED_INT_5_9_9_9_REV:
    case GL_UNSIGNED_INT_24_8:
    case GL_UNSIGNED_INT_8_8_8_8:
    case GL_UNSIGNED_INT_8_8_8_8_REV:
        return 4;
    case GL_FLOAT_32_UNSIGNED_INT_24_8_REV:
        return 8;
    default:
        break;
    }
    uint64_t components = 4;
    switch (format) {
    case GL_RED:
    case GL_RED_INTEGER:
    case GL_ALPHA:
    case GL_LUMINANCE:
    case GL_DEPTH_COMPONENT:
    case GL_STENCIL_INDEX:
        components = 1;
        break;
    case GL_RG:
    case GL_RG_INTEGER:
    case GL_LUMINANCE_ALPHA:
    case GL_DEPTH_STENCIL:
        components = 2;
        break;
    case GL_RGB:
    case GL_BGR:
    case GL_RGB_INTEGER:
        components = 3;
        break;
    default:
        break;
    }
    uint64_t size = 1;
    switch (type) {
    case GL_SHORT:
    case GL_UNSIGNED_SHORT:
    case GL_HALF_FLOAT:
        size = 2;
        break;
    case GL_INT:
    case GL_UNSIGNED_INT:
    case GL_FLOAT:
        size = 4;
        break;
    default:
        break;
    }
    return components * size;
}

uint64_t imageBytes(GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLenum type) {
    return positive(width) * positive(height) * positive(depth) * pixelBytes(format, type);
}

using namespace SFPEW::External;

#define SFPEW_PERF_NEXT(name) BackendGL::name##_PTR next_##name = nullptr

// -- draws: vertices for array draws, indices for element draws, times the
// instance count. Multi-draws sum their parts.
SFPEW_PERF_NEXT(glDrawArrays);
void hook_glDrawArrays(GLenum mode, GLint first, GLsizei count) {
    SFPEW_PERF_ADD(gl_draw_vertices, positive(count));
    next_glDrawArrays(mode, first, count);
}
SFPEW_PERF_NEXT(glDrawArraysInstanced);
void hook_glDrawArraysInstanced(GLenum mode, GLint first, GLsizei count, GLsizei n) {
    SFPEW_PERF_ADD(gl_draw_vertices, positive(count) * instances(n));
    next_glDrawArraysInstanced(mode, first, count, n);
}
SFPEW_PERF_NEXT(glDrawElements);
void hook_glDrawElements(GLenum mode, GLsizei count, GLenum type, const void* indices) {
    SFPEW_PERF_ADD(gl_draw_indices, positive(count));
    next_glDrawElements(mode, count, type, indices);
}
SFPEW_PERF_NEXT(glDrawElementsInstanced);
void hook_glDrawElementsInstanced(GLenum mode, GLsizei count, GLenum type, const void* indices,
                                  GLsizei n) {
    SFPEW_PERF_ADD(gl_draw_indices, positive(count) * instances(n));
    next_glDrawElementsInstanced(mode, count, type, indices, n);
}
SFPEW_PERF_NEXT(glDrawRangeElements);
void hook_glDrawRangeElements(GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type,
                              const void* indices) {
    SFPEW_PERF_ADD(gl_draw_indices, positive(count));
    next_glDrawRangeElements(mode, start, end, count, type, indices);
}
SFPEW_PERF_NEXT(glDrawElementsBaseVertex);
void hook_glDrawElementsBaseVertex(GLenum mode, GLsizei count, GLenum type, const void* indices,
                                   GLint base) {
    SFPEW_PERF_ADD(gl_draw_indices, positive(count));
    next_glDrawElementsBaseVertex(mode, count, type, indices, base);
}
SFPEW_PERF_NEXT(glDrawRangeElementsBaseVertex);
void hook_glDrawRangeElementsBaseVertex(GLenum mode, GLuint start, GLuint end, GLsizei count,
                                        GLenum type, const void* indices, GLint base) {
    SFPEW_PERF_ADD(gl_draw_indices, positive(count));
    next_glDrawRangeElementsBaseVertex(mode, start, end, count, type, indices, base);
}
SFPEW_PERF_NEXT(glDrawElementsInstancedBaseVertex);
void hook_glDrawElementsInstancedBaseVertex(GLenum mode, GLsizei count, GLenum type,
                                            const void* indices, GLsizei n, GLint base) {
    SFPEW_PERF_ADD(gl_draw_indices, positive(count) * instances(n));
    next_glDrawElementsInstancedBaseVertex(mode, count, type, indices, n, base);
}

uint64_t sumCounts(const GLsizei* count, GLsizei drawcount) {
    uint64_t total = 0;
    if (count != nullptr)
        for (GLsizei i = 0; i < drawcount; ++i) total += positive(count[i]);
    return total;
}
SFPEW_PERF_NEXT(glMultiDrawArrays);
void hook_glMultiDrawArrays(GLenum mode, const GLint* first, const GLsizei* count, GLsizei n) {
    SFPEW_PERF_ADD(gl_draw_vertices, sumCounts(count, n));
    next_glMultiDrawArrays(mode, first, count, n);
}
SFPEW_PERF_NEXT(glMultiDrawArraysEXT);
void hook_glMultiDrawArraysEXT(GLenum mode, const GLint* first, const GLsizei* count, GLsizei n) {
    SFPEW_PERF_ADD(gl_draw_vertices, sumCounts(count, n));
    next_glMultiDrawArraysEXT(mode, first, count, n);
}
SFPEW_PERF_NEXT(glMultiDrawElements);
void hook_glMultiDrawElements(GLenum mode, const GLsizei* count, GLenum type,
                              const void* const* indices, GLsizei n) {
    SFPEW_PERF_ADD(gl_draw_indices, sumCounts(count, n));
    next_glMultiDrawElements(mode, count, type, indices, n);
}
SFPEW_PERF_NEXT(glMultiDrawElementsEXT);
void hook_glMultiDrawElementsEXT(GLenum mode, const GLsizei* count, GLenum type,
                                 const void* const* indices, GLsizei n) {
    SFPEW_PERF_ADD(gl_draw_indices, sumCounts(count, n));
    next_glMultiDrawElementsEXT(mode, count, type, indices, n);
}
SFPEW_PERF_NEXT(glMultiDrawElementsBaseVertex);
void hook_glMultiDrawElementsBaseVertex(GLenum mode, const GLsizei* count, GLenum type,
                                        const void* const* indices, GLsizei n, const GLint* base) {
    SFPEW_PERF_ADD(gl_draw_indices, sumCounts(count, n));
    next_glMultiDrawElementsBaseVertex(mode, count, type, indices, n, base);
}
SFPEW_PERF_NEXT(glMultiDrawElementsBaseVertexEXT);
void hook_glMultiDrawElementsBaseVertexEXT(GLenum mode, const GLsizei* count, GLenum type,
                                           const void* const* indices, GLsizei n,
                                           const GLint* base) {
    SFPEW_PERF_ADD(gl_draw_indices, sumCounts(count, n));
    next_glMultiDrawElementsBaseVertexEXT(mode, count, type, indices, n, base);
}

// -- buffers: a store with data is an upload, without it an allocation.
// Timed calls record the driver's CPU-side time for the call, which is what
// a frame pays even when the copy itself is deferred.
SFPEW_PERF_NEXT(glBufferData);
void hook_glBufferData(GLenum target, GLsizeiptr size, const void* data, GLenum usage) {
    if (data == nullptr) {
        SFPEW_PERF_ADD(gl_buffer_alloc, size > 0 ? size : 0);
        next_glBufferData(target, size, data, usage);
        return;
    }
    SFPEW_PERF_TIMED(gl_buffer_upload, size > 0 ? size : 0);
    next_glBufferData(target, size, data, usage);
}
SFPEW_PERF_NEXT(glBufferSubData);
void hook_glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const void* data) {
    SFPEW_PERF_TIMED(gl_buffer_upload, size > 0 ? size : 0);
    next_glBufferSubData(target, offset, size, data);
}
SFPEW_PERF_NEXT(glBufferStorage);
void hook_glBufferStorage(GLenum target, GLsizeiptr size, const void* data, GLbitfield flags) {
    if (data == nullptr) {
        SFPEW_PERF_ADD(gl_buffer_alloc, size > 0 ? size : 0);
        next_glBufferStorage(target, size, data, flags);
        return;
    }
    SFPEW_PERF_TIMED(gl_buffer_upload, size > 0 ? size : 0);
    next_glBufferStorage(target, size, data, flags);
}
SFPEW_PERF_NEXT(glBufferStorageEXT);
void hook_glBufferStorageEXT(GLenum target, GLsizeiptr size, const void* data, GLbitfield flags) {
    if (data == nullptr) {
        SFPEW_PERF_ADD(gl_buffer_alloc, size > 0 ? size : 0);
        next_glBufferStorageEXT(target, size, data, flags);
        return;
    }
    SFPEW_PERF_TIMED(gl_buffer_upload, size > 0 ? size : 0);
    next_glBufferStorageEXT(target, size, data, flags);
}
SFPEW_PERF_NEXT(glCopyBufferSubData);
void hook_glCopyBufferSubData(GLenum read, GLenum write, GLintptr read_offset, GLintptr write_offset,
                              GLsizeiptr size) {
    SFPEW_PERF_TIMED(gl_buffer_copy, size > 0 ? size : 0);
    next_glCopyBufferSubData(read, write, read_offset, write_offset, size);
}
// A mapping moves nothing by itself, so it records the bytes made visible
// and no time: a rate for it would be meaningless.
SFPEW_PERF_NEXT(glMapBufferRange);
void* hook_glMapBufferRange(GLenum target, GLintptr offset, GLsizeiptr length, GLbitfield access) {
    SFPEW_PERF_ADD(gl_buffer_map, length > 0 ? length : 0);
    return next_glMapBufferRange(target, offset, length, access);
}

// -- textures: payload bytes (a null pointer is an allocation, or an
// offset of 0 into a bound unpack buffer, which this cannot tell apart and
// leaves uncounted). Compressed uploads carry their own size.
SFPEW_PERF_NEXT(glTexImage2D);
void hook_glTexImage2D(GLenum target, GLint level, GLint internal, GLsizei w, GLsizei h, GLint border,
                       GLenum format, GLenum type, const void* pixels) {
    if (pixels == nullptr) {
        next_glTexImage2D(target, level, internal, w, h, border, format, type, pixels);
        return;
    }
    SFPEW_PERF_TIMED(gl_texture_upload, imageBytes(w, h, 1, format, type));
    next_glTexImage2D(target, level, internal, w, h, border, format, type, pixels);
}
SFPEW_PERF_NEXT(glTexImage3D);
void hook_glTexImage3D(GLenum target, GLint level, GLint internal, GLsizei w, GLsizei h, GLsizei d,
                       GLint border, GLenum format, GLenum type, const void* pixels) {
    if (pixels == nullptr) {
        next_glTexImage3D(target, level, internal, w, h, d, border, format, type, pixels);
        return;
    }
    SFPEW_PERF_TIMED(gl_texture_upload, imageBytes(w, h, d, format, type));
    next_glTexImage3D(target, level, internal, w, h, d, border, format, type, pixels);
}
SFPEW_PERF_NEXT(glTexSubImage2D);
void hook_glTexSubImage2D(GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h,
                          GLenum format, GLenum type, const void* pixels) {
    SFPEW_PERF_TIMED(gl_texture_upload, imageBytes(w, h, 1, format, type));
    next_glTexSubImage2D(target, level, x, y, w, h, format, type, pixels);
}
SFPEW_PERF_NEXT(glTexSubImage3D);
void hook_glTexSubImage3D(GLenum target, GLint level, GLint x, GLint y, GLint z, GLsizei w,
                          GLsizei h, GLsizei d, GLenum format, GLenum type, const void* pixels) {
    SFPEW_PERF_TIMED(gl_texture_upload, imageBytes(w, h, d, format, type));
    next_glTexSubImage3D(target, level, x, y, z, w, h, d, format, type, pixels);
}
SFPEW_PERF_NEXT(glCompressedTexImage2D);
void hook_glCompressedTexImage2D(GLenum target, GLint level, GLenum internal, GLsizei w, GLsizei h,
                                 GLint border, GLsizei size, const void* data) {
    SFPEW_PERF_TIMED(gl_texture_upload, positive(size));
    next_glCompressedTexImage2D(target, level, internal, w, h, border, size, data);
}
SFPEW_PERF_NEXT(glCompressedTexSubImage2D);
void hook_glCompressedTexSubImage2D(GLenum target, GLint level, GLint x, GLint y, GLsizei w,
                                    GLsizei h, GLenum format, GLsizei size, const void* data) {
    SFPEW_PERF_TIMED(gl_texture_upload, positive(size));
    next_glCompressedTexSubImage2D(target, level, x, y, w, h, format, size, data);
}
SFPEW_PERF_NEXT(glCompressedTexImage3D);
void hook_glCompressedTexImage3D(GLenum target, GLint level, GLenum internal, GLsizei w, GLsizei h,
                                 GLsizei d, GLint border, GLsizei size, const void* data) {
    SFPEW_PERF_TIMED(gl_texture_upload, positive(size));
    next_glCompressedTexImage3D(target, level, internal, w, h, d, border, size, data);
}
SFPEW_PERF_NEXT(glCompressedTexSubImage3D);
void hook_glCompressedTexSubImage3D(GLenum target, GLint level, GLint x, GLint y, GLint z,
                                    GLsizei w, GLsizei h, GLsizei d, GLenum format, GLsizei size,
                                    const void* data) {
    SFPEW_PERF_TIMED(gl_texture_upload, positive(size));
    next_glCompressedTexSubImage3D(target, level, x, y, z, w, h, d, format, size, data);
}

// -- readback and blits
SFPEW_PERF_NEXT(glReadPixels);
void hook_glReadPixels(GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type,
                       void* pixels) {
    SFPEW_PERF_TIMED(gl_readback, imageBytes(w, h, 1, format, type));
    next_glReadPixels(x, y, w, h, format, type, pixels);
}
SFPEW_PERF_NEXT(glBlitFramebuffer);
void hook_glBlitFramebuffer(GLint sx0, GLint sy0, GLint sx1, GLint sy1, GLint dx0, GLint dy0,
                            GLint dx1, GLint dy1, GLbitfield mask, GLenum filter) {
    const uint64_t w = static_cast<uint64_t>(dx1 > dx0 ? dx1 - dx0 : dx0 - dx1);
    const uint64_t h = static_cast<uint64_t>(dy1 > dy0 ? dy1 - dy0 : dy0 - dy1);
    SFPEW_PERF_TIMED(gl_blit, w * h);
    next_glBlitFramebuffer(sx0, sy0, sx1, sy1, dx0, dy0, dx1, dy1, mask, filter);
}

// -- syncs: time the CPU spends blocked in the driver.
SFPEW_PERF_NEXT(glFinish);
void hook_glFinish() {
    SFPEW_PERF_TIME(gl_sync_wait);
    next_glFinish();
}
SFPEW_PERF_NEXT(glClientWaitSync);
GLenum hook_glClientWaitSync(GLsync sync, GLbitfield flags, GLuint64 timeout) {
    SFPEW_PERF_TIME(gl_sync_wait);
    return next_glClientWaitSync(sync, flags, timeout);
}

// -- uniforms: bytes handed over. One generated hook per entry point, from
// the shape of its signature: a scalar form carries one value per argument
// after the location, a vector form `count` of Components values, a matrix
// form `count` matrices.
template <unsigned Tag, typename F>
struct uniform_scalar_hook_t;
template <unsigned Tag, typename... A>
struct uniform_scalar_hook_t<Tag, void (*)(GLint, A...)> {
    static inline void (*next)(GLint, A...) = nullptr;
    static void call(GLint location, A... values) {
        SFPEW_PERF_ADD(gl_uniform, (... + sizeof(A)));
        next(location, values...);
    }
};
template <unsigned Tag, unsigned Components, typename F>
struct uniform_vector_hook_t;
template <unsigned Tag, unsigned Components, typename T>
struct uniform_vector_hook_t<Tag, Components, void (*)(GLint, GLsizei, const T*)> {
    static inline void (*next)(GLint, GLsizei, const T*) = nullptr;
    static void call(GLint location, GLsizei count, const T* value) {
        SFPEW_PERF_ADD(gl_uniform, positive(count) * Components * sizeof(T));
        next(location, count, value);
    }
};
template <unsigned Tag, unsigned Components, typename F>
struct uniform_matrix_hook_t;
template <unsigned Tag, unsigned Components>
struct uniform_matrix_hook_t<Tag, Components, void (*)(GLint, GLsizei, GLboolean, const GLfloat*)> {
    static inline void (*next)(GLint, GLsizei, GLboolean, const GLfloat*) = nullptr;
    static void call(GLint location, GLsizei count, GLboolean transpose, const GLfloat* value) {
        SFPEW_PERF_ADD(gl_uniform, positive(count) * Components * sizeof(GLfloat));
        next(location, count, transpose, value);
    }
};

} // namespace

#define SFPEW_PERF_HOOK(name)                                                                      \
    do {                                                                                           \
        if (funcs.name != nullptr && funcs.name != hook_##name) {                                  \
            next_##name = funcs.name;                                                              \
            funcs.name = hook_##name;                                                              \
        }                                                                                          \
    } while (0)
#define SFPEW_PERF_HOOK_WITH(name, ...)                                                            \
    do {                                                                                           \
        using hook_t = __VA_ARGS__;                                                                \
        if (funcs.name != nullptr && funcs.name != &hook_t::call) {                                \
            hook_t::next = funcs.name;                                                             \
            funcs.name = &hook_t::call;                                                            \
        }                                                                                          \
    } while (0)
#define SFPEW_PERF_UNIFORM_SCALAR(name)                                                            \
    SFPEW_PERF_HOOK_WITH(name, uniform_scalar_hook_t<__COUNTER__, BackendGL::name##_PTR>)
#define SFPEW_PERF_UNIFORM_VECTOR(name, n)                                                         \
    SFPEW_PERF_HOOK_WITH(name, uniform_vector_hook_t<__COUNTER__, n, BackendGL::name##_PTR>)
#define SFPEW_PERF_UNIFORM_MATRIX(name, n)                                                         \
    SFPEW_PERF_HOOK_WITH(name, uniform_matrix_hook_t<__COUNTER__, n, BackendGL::name##_PTR>)

void sfpewPerfHookBackend(BackendGLFunctionsTable& funcs) {
    SFPEW_PERF_HOOK(glDrawArrays);
    SFPEW_PERF_HOOK(glDrawArraysInstanced);
    SFPEW_PERF_HOOK(glDrawElements);
    SFPEW_PERF_HOOK(glDrawElementsInstanced);
    SFPEW_PERF_HOOK(glDrawRangeElements);
    SFPEW_PERF_HOOK(glDrawElementsBaseVertex);
    SFPEW_PERF_HOOK(glDrawRangeElementsBaseVertex);
    SFPEW_PERF_HOOK(glDrawElementsInstancedBaseVertex);
    SFPEW_PERF_HOOK(glMultiDrawArrays);
    SFPEW_PERF_HOOK(glMultiDrawArraysEXT);
    SFPEW_PERF_HOOK(glMultiDrawElements);
    SFPEW_PERF_HOOK(glMultiDrawElementsEXT);
    SFPEW_PERF_HOOK(glMultiDrawElementsBaseVertex);
    SFPEW_PERF_HOOK(glMultiDrawElementsBaseVertexEXT);
    SFPEW_PERF_HOOK(glBufferData);
    SFPEW_PERF_HOOK(glBufferSubData);
    SFPEW_PERF_HOOK(glBufferStorage);
    SFPEW_PERF_HOOK(glBufferStorageEXT);
    SFPEW_PERF_HOOK(glCopyBufferSubData);
    SFPEW_PERF_HOOK(glMapBufferRange);
    SFPEW_PERF_HOOK(glTexImage2D);
    SFPEW_PERF_HOOK(glTexImage3D);
    SFPEW_PERF_HOOK(glTexSubImage2D);
    SFPEW_PERF_HOOK(glTexSubImage3D);
    SFPEW_PERF_HOOK(glCompressedTexImage2D);
    SFPEW_PERF_HOOK(glCompressedTexSubImage2D);
    SFPEW_PERF_HOOK(glCompressedTexImage3D);
    SFPEW_PERF_HOOK(glCompressedTexSubImage3D);
    SFPEW_PERF_HOOK(glReadPixels);
    SFPEW_PERF_HOOK(glBlitFramebuffer);
    SFPEW_PERF_HOOK(glFinish);
    SFPEW_PERF_HOOK(glClientWaitSync);

    SFPEW_PERF_UNIFORM_SCALAR(glUniform1f);
    SFPEW_PERF_UNIFORM_SCALAR(glUniform2f);
    SFPEW_PERF_UNIFORM_SCALAR(glUniform3f);
    SFPEW_PERF_UNIFORM_SCALAR(glUniform4f);
    SFPEW_PERF_UNIFORM_SCALAR(glUniform1i);
    SFPEW_PERF_UNIFORM_SCALAR(glUniform2i);
    SFPEW_PERF_UNIFORM_SCALAR(glUniform3i);
    SFPEW_PERF_UNIFORM_SCALAR(glUniform4i);
    SFPEW_PERF_UNIFORM_SCALAR(glUniform1ui);
    SFPEW_PERF_UNIFORM_SCALAR(glUniform2ui);
    SFPEW_PERF_UNIFORM_SCALAR(glUniform3ui);
    SFPEW_PERF_UNIFORM_SCALAR(glUniform4ui);
    SFPEW_PERF_UNIFORM_VECTOR(glUniform1fv, 1);
    SFPEW_PERF_UNIFORM_VECTOR(glUniform2fv, 2);
    SFPEW_PERF_UNIFORM_VECTOR(glUniform3fv, 3);
    SFPEW_PERF_UNIFORM_VECTOR(glUniform4fv, 4);
    SFPEW_PERF_UNIFORM_VECTOR(glUniform1iv, 1);
    SFPEW_PERF_UNIFORM_VECTOR(glUniform2iv, 2);
    SFPEW_PERF_UNIFORM_VECTOR(glUniform3iv, 3);
    SFPEW_PERF_UNIFORM_VECTOR(glUniform4iv, 4);
    SFPEW_PERF_UNIFORM_VECTOR(glUniform1uiv, 1);
    SFPEW_PERF_UNIFORM_VECTOR(glUniform2uiv, 2);
    SFPEW_PERF_UNIFORM_VECTOR(glUniform3uiv, 3);
    SFPEW_PERF_UNIFORM_VECTOR(glUniform4uiv, 4);
    SFPEW_PERF_UNIFORM_MATRIX(glUniformMatrix2fv, 4);
    SFPEW_PERF_UNIFORM_MATRIX(glUniformMatrix3fv, 9);
    SFPEW_PERF_UNIFORM_MATRIX(glUniformMatrix4fv, 16);
    SFPEW_PERF_UNIFORM_MATRIX(glUniformMatrix2x3fv, 6);
    SFPEW_PERF_UNIFORM_MATRIX(glUniformMatrix3x2fv, 6);
    SFPEW_PERF_UNIFORM_MATRIX(glUniformMatrix2x4fv, 8);
    SFPEW_PERF_UNIFORM_MATRIX(glUniformMatrix4x2fv, 8);
    SFPEW_PERF_UNIFORM_MATRIX(glUniformMatrix3x4fv, 12);
    SFPEW_PERF_UNIFORM_MATRIX(glUniformMatrix4x3fv, 12);
}

unsigned sfpewPerfRegisterBackend(const char* name, const void* field) {
    for (unsigned i = 0; i < g_backend_count; ++i) {
        if (std::strcmp(g_backend_names[i], name) == 0) {
            g_backend_fields[i] = field;
            return i;
        }
    }
    if (g_backend_count == kSfpewPerfBackendSlots) return kSfpewPerfBackendSlots - 1;
    g_backend_names[g_backend_count] = name;
    g_backend_fields[g_backend_count] = field;
    g_backend_categories[g_backend_count] = classify(name);
    return g_backend_count++;
}

void* sfpewPerfBackendEntry(const char* name) {
    for (unsigned i = 0; i < g_backend_count; ++i) {
        if (std::strcmp(g_backend_names[i], name) != 0) continue;
        // Every table entry is a plain function pointer; read it as bytes
        // rather than through an aliasing cast.
        void (*entry)() = nullptr;
        std::memcpy(&entry, g_backend_fields[i], sizeof entry);
        return reinterpret_cast<void*>(entry);
    }
    return nullptr;
}

unsigned sfpewPerfBackendCount() { return g_backend_count; }

const char* sfpewPerfBackendName(unsigned slot) {
    return slot < g_backend_count ? g_backend_names[slot] : nullptr;
}

sfpew_perf_category_t sfpewPerfBackendCategory(unsigned slot) {
    return slot < g_backend_count ? g_backend_categories[slot] : sfpew_perf_category_t::other;
}

const char* sfpewPerfCategoryName(sfpew_perf_category_t category) {
    switch (category) {
    case sfpew_perf_category_t::draw: return "draw";
    case sfpew_perf_category_t::bind: return "bind";
    case sfpew_perf_category_t::vertex_setup: return "vertex setup";
    case sfpew_perf_category_t::buffer: return "buffer";
    case sfpew_perf_category_t::texture: return "texture";
    case sfpew_perf_category_t::readback: return "readback";
    case sfpew_perf_category_t::uniform: return "uniform";
    case sfpew_perf_category_t::state: return "state";
    case sfpew_perf_category_t::clear: return "clear";
    case sfpew_perf_category_t::shader: return "shader";
    case sfpew_perf_category_t::object: return "object";
    case sfpew_perf_category_t::query: return "query";
    case sfpew_perf_category_t::sync: return "sync";
    case sfpew_perf_category_t::egl: return "egl";
    default: return "other";
    }
}
#endif
