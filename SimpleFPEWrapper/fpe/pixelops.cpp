// SimpleFPEWrapper - SimpleFPEWrapper/fpe/pixelops.cpp
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// CPU pixel paths (plans/08, 8.2/8.4): glBitmap, glDrawPixels and
// glCopyPixels on a shared screen-aligned quad drawer. The drawer owns a
// tiny dedicated program (outside the FPE program cache), an empty VAO
// (corners derive from gl_VertexID) and one scratch texture; current
// blend/depth/scissor state applies to the emitted quad exactly as the
// spec asks.

#include "fpe.hpp"
#include "list.h"
#include "drawing1x.h"
#include "imaging.h"
#include "../log.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <new>
#include <type_traits>
#include <vector>

namespace {

constexpr char kQuadVS[] = "#version 300 es\n"
                           "precision highp float;\n"
                           "uniform vec4 uRect;  // NDC x0,y0,x1,y1\n"
                           "uniform float uDepth; // NDC z\n"
                           "out vec2 vUV;\n"
                           "void main() {\n"
                           "    vec2 corner = vec2(float(gl_VertexID & 1), float((gl_VertexID >> 1) & 1));\n"
                           "    vUV = corner;\n"
                           "    gl_Position = vec4(mix(uRect.xy, uRect.zw, corner), uDepth, 1.0);\n"
                           "}\n";

constexpr char kQuadFS[] = "#version 300 es\n"
                           "precision highp float;\n"
                           "uniform sampler2D uTex;\n"
                           "uniform vec4 uColor; // bitmap ink\n"
                           "uniform int uMode;   // 0 = color, 1 = bitmap, 2 = depth\n"
                           "in vec2 vUV;\n"
                           "out vec4 FragColor;\n"
                           "void main() {\n"
                           "    vec4 texel = texture(uTex, vUV);\n"
                           "    if (uMode == 1) {\n"
                           "        if (texel.r < 0.5) discard;\n"
                           "        FragColor = uColor;\n"
                           "    } else if (uMode == 2) {\n"
                           "        gl_FragDepth = clamp(texel.r, 0.0, 1.0);\n"
                           "        FragColor = vec4(0.0);\n"
                           "    } else {\n"
                           "        FragColor = texel * uColor;\n"
                           "    }\n"
                           "}\n";

enum class quad_mode_t : GLint { color = 0, bitmap = 1, depth = 2 };

struct quad_drawer_t {
    GLuint program = 0;
    GLuint vao = 0;
    GLuint texture = 0;
    GLint loc_rect = -1, loc_depth = -1, loc_color = -1, loc_mode = -1, loc_tex = -1;
    bool init_failed = false;
};

quad_drawer_t& drawer() {
    // Per-context: GL object names are context-scoped.
    static thread_local struct {
        EGLContext context = EGL_NO_CONTEXT;
        quad_drawer_t d{};
    } cache;
    const EGLContext current =
        sfpewCurrentContext();
    if (cache.context != current) {
        cache.context = current;
        cache.d = {};
    }
    return cache.d;
}

GLuint compileStage(GLenum type, const char* src) {
    const GLuint shader = g_glFuncs.glCreateShader(type);
    if (shader == 0) return 0;
    g_glFuncs.glShaderSource(shader, 1, &src, nullptr);
    g_glFuncs.glCompileShader(shader);
    GLint ok = GL_FALSE;
    g_glFuncs.glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok != GL_TRUE) {
        char info[512] = {};
        if (g_glFuncs.glGetShaderInfoLog != nullptr)
            g_glFuncs.glGetShaderInfoLog(shader, sizeof(info), nullptr, info);
        SFPEW_LOGE("pixel-path quad shader failed: %s", info);
        g_glFuncs.glDeleteShader(shader);
        return 0;
    }
    return shader;
}

bool ensureDrawer(quad_drawer_t& d) {
    if (d.program != 0) return true;
    if (d.init_failed) return false;
    if (g_glFuncs.glCreateShader == nullptr || g_glFuncs.glCreateProgram == nullptr ||
        g_glFuncs.glGenVertexArrays == nullptr || g_glFuncs.glGenTextures == nullptr) {
        d.init_failed = true;
        return false;
    }

    const GLuint vs = compileStage(GL_VERTEX_SHADER, kQuadVS);
    const GLuint fs = compileStage(GL_FRAGMENT_SHADER, kQuadFS);
    if (vs == 0 || fs == 0) {
        if (vs != 0) g_glFuncs.glDeleteShader(vs);
        if (fs != 0) g_glFuncs.glDeleteShader(fs);
        d.init_failed = true;
        return false;
    }
    const GLuint program = g_glFuncs.glCreateProgram();
    if (program != 0) g_glstate_c.internal_programs.insert((int)program);
    g_glFuncs.glAttachShader(program, vs);
    g_glFuncs.glAttachShader(program, fs);
    g_glFuncs.glLinkProgram(program);
    g_glFuncs.glDeleteShader(vs);
    g_glFuncs.glDeleteShader(fs);
    GLint linked = GL_FALSE;
    g_glFuncs.glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) {
        SFPEW_LOGE("pixel-path quad program failed to link");
        g_glFuncs.glDeleteProgram(program);
        d.init_failed = true;
        return false;
    }
    d.program = program;
    d.loc_rect = g_glFuncs.glGetUniformLocation(program, "uRect");
    d.loc_depth = g_glFuncs.glGetUniformLocation(program, "uDepth");
    d.loc_color = g_glFuncs.glGetUniformLocation(program, "uColor");
    d.loc_mode = g_glFuncs.glGetUniformLocation(program, "uMode");
    d.loc_tex = g_glFuncs.glGetUniformLocation(program, "uTex");
    g_glFuncs.glGenVertexArrays(1, &d.vao);
    g_glFuncs.glGenTextures(1, &d.texture);
    return d.vao != 0 && d.texture != 0;
}

// Draws `pixels` (tightly packed) as a screen-aligned quad anchored at
// window coords (x0,y0) spanning (wpx,hpx) window pixels; bitmap mode
// discards zero texels and paints with `color`.
void drawQuad(const void* pixels, GLsizei tex_w, GLsizei tex_h, GLenum tex_format,
              GLenum tex_type, GLfloat x0, GLfloat y0, GLfloat wpx, GLfloat hpx,
              GLfloat depth, quad_mode_t mode, const glm::vec4& color) {
    auto& d = drawer();
    if (!ensureDrawer(d)) return;

    fpe_backend_draw_state_guard_t backend_state;

    GLint viewport[4] = {0, 0, 1, 1};
    g_glFuncs.glGetIntegerv(GL_VIEWPORT, viewport);
    if (viewport[2] <= 0 || viewport[3] <= 0) return;

    // Preserve unit-0 texture binding through the wrapper entry points so
    // the logical shadows stay coherent.
    const GLenum caller_active = sfpewLogicalActiveTexture();
    glActiveTexture(GL_TEXTURE0);
    const GLuint unit_zero_binding = sfpewLogicalTextureBinding(GL_TEXTURE_2D);
    g_glFuncs.glBindTexture(GL_TEXTURE_2D, d.texture);

    // Every pixel path hands this helper a tightly packed CPU buffer. Keep a
    // caller's PBO and desktop unpack layout from reinterpreting that pointer.
    GLint unpack_alignment = 4, unpack_row_length = 0, unpack_skip_rows = 0;
    GLint unpack_skip_pixels = 0, unpack_image_height = 0, unpack_skip_images = 0;
    GLint unpack_pbo = 0;
    g_glFuncs.glGetIntegerv(GL_UNPACK_ALIGNMENT, &unpack_alignment);
    g_glFuncs.glGetIntegerv(GL_UNPACK_ROW_LENGTH, &unpack_row_length);
    g_glFuncs.glGetIntegerv(GL_UNPACK_SKIP_ROWS, &unpack_skip_rows);
    g_glFuncs.glGetIntegerv(GL_UNPACK_SKIP_PIXELS, &unpack_skip_pixels);
    g_glFuncs.glGetIntegerv(GL_UNPACK_IMAGE_HEIGHT, &unpack_image_height);
    g_glFuncs.glGetIntegerv(GL_UNPACK_SKIP_IMAGES, &unpack_skip_images);
    g_glFuncs.glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &unpack_pbo);
    if (unpack_pbo != 0) g_glFuncs.glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    g_glFuncs.glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    g_glFuncs.glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    g_glFuncs.glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
    g_glFuncs.glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    g_glFuncs.glPixelStorei(GL_UNPACK_IMAGE_HEIGHT, 0);
    g_glFuncs.glPixelStorei(GL_UNPACK_SKIP_IMAGES, 0);
    const GLint internal_format = tex_format == GL_RED
                                      ? (tex_type == GL_FLOAT ? GL_R32F : GL_R8)
                                      : (tex_type == GL_FLOAT ? GL_RGBA32F : GL_RGBA8);
    g_glFuncs.glTexImage2D(GL_TEXTURE_2D, 0, internal_format, tex_w, tex_h, 0, tex_format,
                           tex_type, pixels);
    g_glFuncs.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    g_glFuncs.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    g_glFuncs.glPixelStorei(GL_UNPACK_ALIGNMENT, unpack_alignment);
    g_glFuncs.glPixelStorei(GL_UNPACK_ROW_LENGTH, unpack_row_length);
    g_glFuncs.glPixelStorei(GL_UNPACK_SKIP_ROWS, unpack_skip_rows);
    g_glFuncs.glPixelStorei(GL_UNPACK_SKIP_PIXELS, unpack_skip_pixels);
    g_glFuncs.glPixelStorei(GL_UNPACK_IMAGE_HEIGHT, unpack_image_height);
    g_glFuncs.glPixelStorei(GL_UNPACK_SKIP_IMAGES, unpack_skip_images);
    if (unpack_pbo != 0)
        g_glFuncs.glBindBuffer(GL_PIXEL_UNPACK_BUFFER, static_cast<GLuint>(unpack_pbo));

    const auto to_ndc_x = [&](GLfloat wx) { return (wx - viewport[0]) / viewport[2] * 2.0f - 1.0f; };
    const auto to_ndc_y = [&](GLfloat wy) { return (wy - viewport[1]) / viewport[3] * 2.0f - 1.0f; };

    sfpewInvalidateImmediateDrawState();
    g_glFuncs.glUseProgram(d.program);
    sfpewBackendBindVertexArray(d.vao);
    g_glFuncs.glUniform4f(d.loc_rect, to_ndc_x(x0), to_ndc_y(y0), to_ndc_x(x0 + wpx), to_ndc_y(y0 + hpx));
    g_glFuncs.glUniform1f(d.loc_depth, depth * 2.0f - 1.0f);
    g_glFuncs.glUniform4f(d.loc_color, color.r, color.g, color.b, color.a);
    g_glFuncs.glUniform1i(d.loc_mode, static_cast<GLint>(mode));
    g_glFuncs.glUniform1i(d.loc_tex, 0);

    const auto& caller_mask = g_glstate_c.fpe_state.color_buffer.color_mask;
    if (mode == quad_mode_t::depth && g_glFuncs.glColorMask != nullptr)
        g_glFuncs.glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    g_glFuncs.glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    if (mode == quad_mode_t::depth && g_glFuncs.glColorMask != nullptr)
        g_glFuncs.glColorMask(caller_mask[0], caller_mask[1], caller_mask[2], caller_mask[3]);

    // Restore unit-0 binding and the caller's active unit.
    g_glFuncs.glBindTexture(GL_TEXTURE_2D, unit_zero_binding);
    glActiveTexture(caller_active);
}

} // namespace

namespace {

// Full-screen pass over an EXISTING texture (0 selects a lazy 1x1 white),
// output scaled by `color`. Blend/FBO state is the caller's responsibility;
// program/VAO/texture bindings are restored like drawQuad does.
void drawFullscreenTexture(GLuint texture, const glm::vec4& color) {
    auto& d = drawer();
    if (!ensureDrawer(d)) return;

    static thread_local GLuint white_tex = 0;
    if (texture == 0) {
        if (white_tex == 0) {
            g_glFuncs.glGenTextures(1, &white_tex);
            const GLubyte white[4] = {255, 255, 255, 255};
            g_glFuncs.glBindTexture(GL_TEXTURE_2D, white_tex);
            g_glFuncs.glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                                   white);
            g_glFuncs.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            g_glFuncs.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        }
        texture = white_tex;
    }

    GLint prev_program = 0, prev_vao = 0, prev_tex = 0, prev_active = GL_TEXTURE0;
    g_glFuncs.glGetIntegerv(GL_CURRENT_PROGRAM, &prev_program);
    g_glFuncs.glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prev_vao);
    g_glFuncs.glGetIntegerv(GL_ACTIVE_TEXTURE, &prev_active);
    g_glFuncs.glActiveTexture(GL_TEXTURE0);
    g_glFuncs.glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev_tex);

    g_glFuncs.glBindTexture(GL_TEXTURE_2D, texture);
    sfpewInvalidateImmediateDrawState();
    g_glFuncs.glUseProgram(d.program);
    sfpewBackendBindVertexArray(d.vao);
    g_glFuncs.glUniform4f(d.loc_rect, -1.0f, -1.0f, 1.0f, 1.0f);
    g_glFuncs.glUniform1f(d.loc_depth, 0.0f);
    g_glFuncs.glUniform4f(d.loc_color, color.r, color.g, color.b, color.a);
    g_glFuncs.glUniform1i(d.loc_mode, 0);
    g_glFuncs.glUniform1i(d.loc_tex, 0);
    g_glFuncs.glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    g_glFuncs.glBindTexture(GL_TEXTURE_2D, (GLuint)prev_tex);
    g_glFuncs.glActiveTexture((GLenum)prev_active);
    sfpewInvalidateImmediateDrawState();
    g_glFuncs.glUseProgram(prev_program);
    sfpewBackendBindVertexArray(prev_vao);
}

} // namespace

#define sfpewDrawTextureQuad drawFullscreenTexture

// --- Accumulation buffer emulation (plans/10, 10.4) ---------------------
// An RGBA16F color attachment sized to the current viewport stands in for
// the accumulation buffer; ops render through the shared quad drawer with
// blend state providing the arithmetic. Sized-to-viewport is a documented
// approximation (the real accum buffer matches the full framebuffer).

namespace {

struct accum_state_t {
    GLuint fbo = 0;
    GLuint texture = 0;      // RGBA16F accumulation storage
    GLuint scratch_tex = 0;  // framebuffer snapshot for ACCUM/LOAD
    GLsizei width = 0, height = 0;
    glm::vec4 clear_value{0.0f};
};

accum_state_t& accumState() {
    static thread_local struct {
        EGLContext context = EGL_NO_CONTEXT;
        accum_state_t s{};
    } cache;
    const EGLContext current =
        sfpewCurrentContext();
    if (cache.context != current) {
        cache.context = current;
        cache.s = {};
    }
    return cache.s;
}

bool ensureAccum(accum_state_t& a, GLsizei width, GLsizei height) {
    if (g_glFuncs.glGenFramebuffers == nullptr || g_glFuncs.glFramebufferTexture2D == nullptr ||
        g_glFuncs.glGenTextures == nullptr) {
        return false;
    }
    if (a.fbo != 0 && a.width == width && a.height == height) return true;
    if (a.fbo == 0) g_glFuncs.glGenFramebuffers(1, &a.fbo);
    if (a.texture == 0) g_glFuncs.glGenTextures(1, &a.texture);
    if (a.scratch_tex == 0) g_glFuncs.glGenTextures(1, &a.scratch_tex);
    g_glFuncs.glBindTexture(GL_TEXTURE_2D, a.texture);
    g_glFuncs.glTexImage2D(GL_TEXTURE_2D, 0, 0x881A /* GL_RGBA16F */, width, height, 0, GL_RGBA,
                           0x140B /* GL_HALF_FLOAT */, nullptr);
    g_glFuncs.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    g_glFuncs.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    g_glFuncs.glBindFramebuffer(0x8D40 /* GL_FRAMEBUFFER */, a.fbo);
    g_glFuncs.glFramebufferTexture2D(0x8D40, 0x8CE0 /* GL_COLOR_ATTACHMENT0 */, GL_TEXTURE_2D,
                                     a.texture, 0);
    a.width = width;
    a.height = height;
    return true;
}

// Snapshot of blend/FBO state around accumulation passes.
struct accum_backend_guard_t {
    GLint draw_fbo = 0, read_fbo = 0;
    GLboolean blend = GL_FALSE;
    GLint src_rgb = GL_ONE, dst_rgb = GL_ZERO, src_a = GL_ONE, dst_a = GL_ZERO;

    accum_backend_guard_t() {
        g_glFuncs.glGetIntegerv(0x8CA6 /* GL_DRAW_FRAMEBUFFER_BINDING */, &draw_fbo);
        g_glFuncs.glGetIntegerv(0x8CAA /* GL_READ_FRAMEBUFFER_BINDING */, &read_fbo);
        blend = g_glFuncs.glIsEnabled != nullptr ? g_glFuncs.glIsEnabled(GL_BLEND) : GL_FALSE;
        g_glFuncs.glGetIntegerv(GL_BLEND_SRC_RGB, &src_rgb);
        g_glFuncs.glGetIntegerv(GL_BLEND_DST_RGB, &dst_rgb);
        g_glFuncs.glGetIntegerv(GL_BLEND_SRC_ALPHA, &src_a);
        g_glFuncs.glGetIntegerv(GL_BLEND_DST_ALPHA, &dst_a);
    }
    ~accum_backend_guard_t() {
        g_glFuncs.glBindFramebuffer(0x8CA9 /* GL_DRAW_FRAMEBUFFER */, draw_fbo);
        g_glFuncs.glBindFramebuffer(0x8CA8 /* GL_READ_FRAMEBUFFER */, read_fbo);
        if (blend)
            g_glFuncs.glEnable(GL_BLEND);
        else
            g_glFuncs.glDisable(GL_BLEND);
        g_glFuncs.glBlendFuncSeparate(src_rgb, dst_rgb, src_a, dst_a);
    }
};

} // namespace

void glClearAccum(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha) {
    accumState().clear_value = {red, green, blue, alpha};
}

namespace {

void drainFailedMapError();

bool pixelMapIndex(GLenum map, size_t* index = nullptr) {
    if (map < GL_PIXEL_MAP_I_TO_I || map > GL_PIXEL_MAP_A_TO_A) return false;
    if (index != nullptr) *index = static_cast<size_t>(map - GL_PIXEL_MAP_I_TO_I);
    return true;
}

bool pixelMapIsIndex(GLenum map) {
    return map == GL_PIXEL_MAP_I_TO_I || map == GL_PIXEL_MAP_S_TO_S;
}

bool pixelMapSizeIsValid(GLenum map, GLsizei mapsize) {
    if (mapsize < 1 || mapsize > SFPEW_MAX_PIXEL_MAP_TABLE) return false;
    // The six maps indexed by a color/stencil index use masking to select an
    // entry and therefore require a power-of-two size. Component-to-component
    // maps do not have that restriction.
    return map >= GL_PIXEL_MAP_R_TO_R || (mapsize & (mapsize - 1)) == 0;
}

template <typename T>
bool copyPixelMapSource(GLsizei mapsize, const T* values, std::vector<T>* copied) {
    try {
        copied->resize(static_cast<size_t>(mapsize));
    } catch (const std::bad_alloc&) {
        g_glstate.set_error(GL_OUT_OF_MEMORY);
        return false;
    }

    GLint unpack_buffer = 0;
    if (!DisplayListManager::isCalling() && sfpewEnsureBackend() &&
        g_glFuncs.glGetIntegerv != nullptr) {
        g_glFuncs.glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &unpack_buffer);
    }
    if (unpack_buffer == 0) {
        if (values == nullptr) return false;
        std::copy_n(values, mapsize, copied->data());
        return true;
    }

    if (g_glFuncs.glGetBufferParameteriv == nullptr || g_glFuncs.glMapBufferRange == nullptr ||
        g_glFuncs.glUnmapBuffer == nullptr) {
        g_glstate.set_error(GL_INVALID_OPERATION);
        return false;
    }
    const size_t offset = reinterpret_cast<uintptr_t>(values);
    const size_t bytes = static_cast<size_t>(mapsize) * sizeof(T);
    GLint buffer_size = 0, buffer_mapped = GL_FALSE;
    g_glFuncs.glGetBufferParameteriv(GL_PIXEL_UNPACK_BUFFER, GL_BUFFER_SIZE, &buffer_size);
    g_glFuncs.glGetBufferParameteriv(GL_PIXEL_UNPACK_BUFFER, GL_BUFFER_MAPPED, &buffer_mapped);
    if (offset % sizeof(T) != 0 || buffer_mapped == GL_TRUE || buffer_size < 0 ||
        offset > static_cast<size_t>(buffer_size) || bytes > static_cast<size_t>(buffer_size) - offset ||
        offset > static_cast<size_t>(std::numeric_limits<GLintptr>::max()) ||
        bytes > static_cast<size_t>(std::numeric_limits<GLsizeiptr>::max())) {
        g_glstate.set_error(GL_INVALID_OPERATION);
        return false;
    }

    const void* mapped = g_glFuncs.glMapBufferRange(
        GL_PIXEL_UNPACK_BUFFER, static_cast<GLintptr>(offset), static_cast<GLsizeiptr>(bytes),
        GL_MAP_READ_BIT);
    if (mapped == nullptr) {
        drainFailedMapError();
        g_glstate.set_error(GL_INVALID_OPERATION);
        return false;
    }
    std::memcpy(copied->data(), mapped, bytes);
    if (g_glFuncs.glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER) != GL_TRUE) {
        g_glstate.set_error(GL_INVALID_OPERATION);
        return false;
    }
    return true;
}

template <typename T>
void storePixelMap(GLenum map, const std::vector<T>& values) {
    size_t index = 0;
    pixelMapIndex(map, &index);
    const bool index_map = pixelMapIsIndex(map);
    auto& destination = g_glstate.pixel_maps[index];
    try {
        destination.resize(values.size());
    } catch (const std::bad_alloc&) {
        g_glstate.set_error(GL_OUT_OF_MEMORY);
        return;
    }
    for (size_t i = 0; i < values.size(); ++i) {
        if (index_map) {
            destination[i] = static_cast<GLfloat>(values[i]);
        } else if constexpr (std::is_floating_point_v<T>) {
            const GLfloat value = static_cast<GLfloat>(values[i]);
            destination[i] = std::isnan(value) ? 0.0f : std::clamp(value, 0.0f, 1.0f);
        } else {
            destination[i] = static_cast<GLfloat>(values[i]) /
                             static_cast<GLfloat>(std::numeric_limits<T>::max());
        }
    }
}

template <typename T>
T pixelMapResult(GLenum map, GLfloat value) {
    if constexpr (std::is_floating_point_v<T>) {
        return value;
    } else {
        if (std::isnan(value) || value <= 0.0f) return 0;
        const long double maximum = static_cast<long double>(std::numeric_limits<T>::max());
        const long double converted = pixelMapIsIndex(map)
                                          ? static_cast<long double>(value)
                                          : static_cast<long double>(std::min(value, 1.0f)) * maximum;
        if (converted >= maximum) return std::numeric_limits<T>::max();
        return static_cast<T>(std::llround(converted));
    }
}

template <typename T>
bool writePixelMapDestination(const std::vector<T>& converted, T* values) {
    GLint pack_buffer = 0;
    if (sfpewEnsureBackend() && g_glFuncs.glGetIntegerv != nullptr)
        g_glFuncs.glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &pack_buffer);
    if (pack_buffer == 0) {
        if (values == nullptr) return false;
        std::copy(converted.begin(), converted.end(), values);
        return true;
    }

    if (g_glFuncs.glGetBufferParameteriv == nullptr || g_glFuncs.glMapBufferRange == nullptr ||
        g_glFuncs.glUnmapBuffer == nullptr) {
        g_glstate.set_error(GL_INVALID_OPERATION);
        return false;
    }
    const size_t offset = reinterpret_cast<uintptr_t>(values);
    const size_t bytes = converted.size() * sizeof(T);
    GLint buffer_size = 0, buffer_mapped = GL_FALSE;
    g_glFuncs.glGetBufferParameteriv(GL_PIXEL_PACK_BUFFER, GL_BUFFER_SIZE, &buffer_size);
    g_glFuncs.glGetBufferParameteriv(GL_PIXEL_PACK_BUFFER, GL_BUFFER_MAPPED, &buffer_mapped);
    if (offset % sizeof(T) != 0 || buffer_mapped == GL_TRUE || buffer_size < 0 ||
        offset > static_cast<size_t>(buffer_size) || bytes > static_cast<size_t>(buffer_size) - offset ||
        offset > static_cast<size_t>(std::numeric_limits<GLintptr>::max()) ||
        bytes > static_cast<size_t>(std::numeric_limits<GLsizeiptr>::max())) {
        g_glstate.set_error(GL_INVALID_OPERATION);
        return false;
    }

    void* mapped = g_glFuncs.glMapBufferRange(
        GL_PIXEL_PACK_BUFFER, static_cast<GLintptr>(offset), static_cast<GLsizeiptr>(bytes),
        GL_MAP_WRITE_BIT);
    if (mapped == nullptr) {
        drainFailedMapError();
        g_glstate.set_error(GL_INVALID_OPERATION);
        return false;
    }
    std::memcpy(mapped, converted.data(), bytes);
    if (g_glFuncs.glUnmapBuffer(GL_PIXEL_PACK_BUFFER) != GL_TRUE) {
        g_glstate.set_error(GL_INVALID_OPERATION);
        return false;
    }
    return true;
}

template <typename T>
void getPixelMap(GLenum map, T* values) {
    sfpewEntryBarrier();
    size_t index = 0;
    if (!pixelMapIndex(map, &index)) {
        g_glstate.set_error(GL_INVALID_ENUM);
        return;
    }
    const auto& source = g_glstate.pixel_maps[index];
    std::vector<T> converted;
    try {
        converted.resize(source.size());
    } catch (const std::bad_alloc&) {
        g_glstate.set_error(GL_OUT_OF_MEMORY);
        return;
    }
    for (size_t i = 0; i < source.size(); ++i)
        converted[i] = pixelMapResult<T>(map, source[i]);
    writePixelMapDestination(converted, values);
}

} // namespace

void glPixelMapfv(GLenum map, GLsizei mapsize, const GLfloat* values) {
    sfpewEntryBarrier();
    if (!pixelMapIndex(map)) {
        g_glstate.set_error(GL_INVALID_ENUM);
        return;
    }
    if (!pixelMapSizeIsValid(map, mapsize)) {
        g_glstate.set_error(GL_INVALID_VALUE);
        return;
    }
    std::vector<GLfloat> copied;
    if (!copyPixelMapSource(mapsize, values, &copied)) return;
    LIST_RECORD(glPixelMapfv, {{2, copied.size() * sizeof(GLfloat)}}, map, mapsize,
                static_cast<const GLfloat*>(copied.data()))
    storePixelMap(map, copied);
}

void glPixelMapuiv(GLenum map, GLsizei mapsize, const GLuint* values) {
    sfpewEntryBarrier();
    if (!pixelMapIndex(map)) {
        g_glstate.set_error(GL_INVALID_ENUM);
        return;
    }
    if (!pixelMapSizeIsValid(map, mapsize)) {
        g_glstate.set_error(GL_INVALID_VALUE);
        return;
    }
    std::vector<GLuint> copied;
    if (!copyPixelMapSource(mapsize, values, &copied)) return;
    LIST_RECORD(glPixelMapuiv, {{2, copied.size() * sizeof(GLuint)}}, map, mapsize,
                static_cast<const GLuint*>(copied.data()))
    storePixelMap(map, copied);
}

void glPixelMapusv(GLenum map, GLsizei mapsize, const GLushort* values) {
    sfpewEntryBarrier();
    if (!pixelMapIndex(map)) {
        g_glstate.set_error(GL_INVALID_ENUM);
        return;
    }
    if (!pixelMapSizeIsValid(map, mapsize)) {
        g_glstate.set_error(GL_INVALID_VALUE);
        return;
    }
    std::vector<GLushort> copied;
    if (!copyPixelMapSource(mapsize, values, &copied)) return;
    LIST_RECORD(glPixelMapusv, {{2, copied.size() * sizeof(GLushort)}}, map, mapsize,
                static_cast<const GLushort*>(copied.data()))
    storePixelMap(map, copied);
}

void glGetPixelMapfv(GLenum map, GLfloat* values) {
    getPixelMap(map, values);
}

void glGetPixelMapuiv(GLenum map, GLuint* values) {
    getPixelMap(map, values);
}

void glGetPixelMapusv(GLenum map, GLushort* values) {
    getPixelMap(map, values);
}

// GL_ACCUM_CLEAR_VALUE, for the glGet family: the accumulation state is
// private to this file, but its clear colour is queryable state.
void sfpewAccumClearValue(GLfloat* rgba) {
    const glm::vec4& value = accumState().clear_value;
    rgba[0] = value.r;
    rgba[1] = value.g;
    rgba[2] = value.b;
    rgba[3] = value.a;
}

void glAccum(GLenum op, GLfloat value) {
    sfpewEntryBarrier();
    if (op != GL_ACCUM && op != GL_LOAD && op != GL_ADD && op != GL_MULT && op != GL_RETURN) {
        g_glstate.set_error(GL_INVALID_ENUM);
        return;
    }
    if (!sfpewEnsureBackend() || g_glFuncs.glBlendFuncSeparate == nullptr ||
        g_glFuncs.glCopyTexImage2D == nullptr) {
        return;
    }
    GLint viewport[4] = {0, 0, 0, 0};
    g_glFuncs.glGetIntegerv(GL_VIEWPORT, viewport);
    if (viewport[2] <= 0 || viewport[3] <= 0) return;

    auto& a = accumState();
    if (!ensureAccum(a, viewport[2], viewport[3])) return;

    accum_backend_guard_t backend;
    const glm::vec4 scale(value, value, value, value);

    if (op == GL_ACCUM || op == GL_LOAD) {
        // Snapshot the caller's framebuffer region into the scratch texture.
        g_glFuncs.glBindFramebuffer(0x8CA8 /* GL_READ_FRAMEBUFFER */, backend.read_fbo);
        g_glFuncs.glBindTexture(GL_TEXTURE_2D, a.scratch_tex);
        g_glFuncs.glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, viewport[0], viewport[1], viewport[2],
                                   viewport[3], 0);
        g_glFuncs.glBindFramebuffer(0x8CA9 /* GL_DRAW_FRAMEBUFFER */, a.fbo);
        if (op == GL_ACCUM) {
            g_glFuncs.glEnable(GL_BLEND);
            g_glFuncs.glBlendFuncSeparate(GL_ONE, GL_ONE, GL_ONE, GL_ONE);
        } else {
            g_glFuncs.glDisable(GL_BLEND);
        }
        sfpewDrawTextureQuad(a.scratch_tex, scale);
    } else if (op == GL_ADD || op == GL_MULT) {
        g_glFuncs.glBindFramebuffer(0x8CA9, a.fbo);
        g_glFuncs.glEnable(GL_BLEND);
        if (op == GL_ADD)
            g_glFuncs.glBlendFuncSeparate(GL_ONE, GL_ONE, GL_ONE, GL_ONE);
        else
            g_glFuncs.glBlendFuncSeparate(GL_DST_COLOR, GL_ZERO, GL_DST_ALPHA, GL_ZERO);
        // ADD paints the constant; MULT multiplies the destination by it.
        sfpewDrawTextureQuad(0 /* white */, op == GL_ADD ? scale : scale);
    } else { // GL_RETURN
        g_glFuncs.glBindFramebuffer(0x8CA9, backend.draw_fbo);
        g_glFuncs.glDisable(GL_BLEND);
        sfpewDrawTextureQuad(a.texture, scale);
    }
}

void glBitmap(GLsizei width, GLsizei height, GLfloat xorig, GLfloat yorig, GLfloat xmove,
              GLfloat ymove, const GLubyte* bitmap) {
    sfpewEntryBarrier();
    if (width < 0 || height < 0) {
        g_glstate.set_error(GL_INVALID_VALUE);
        return;
    }
    if (!g_glstate.fpe_uniform.raster_position_valid) return;
    auto& raster = g_glstate.fpe_uniform.raster_position;

    if (width > 0 && height > 0 && bitmap != nullptr && sfpewEnsureBackend() &&
        g_glFuncs.glTexImage2D != nullptr) {
        LIST_RECORD(glBitmap, {{6, (size_t)((width + 7) / 8) * (size_t)height}}, width, height, xorig,
                    yorig, xmove, ymove, bitmap)
        // Unpack: bitmap rows are ceil(w/8) bytes, MSB-first unless
        // GL_UNPACK_LSB_FIRST; rows are not padded at alignment 1 (we
        // conservatively assume the common tight case).
        const bool lsb_first = g_glstate.pixel_store_unpack_lsb_first;
        const size_t row_bytes = (size_t)((width + 7) / 8);
        std::vector<uint8_t> texels((size_t)width * (size_t)height);
        for (GLsizei yy = 0; yy < height; ++yy) {
            for (GLsizei xx = 0; xx < width; ++xx) {
                const GLubyte byte = bitmap[yy * row_bytes + xx / 8];
                const int bit = lsb_first ? (xx % 8) : (7 - xx % 8);
                texels[(size_t)yy * width + xx] = ((byte >> bit) & 1u) ? 0xFF : 0x00;
            }
        }
        drawQuad(texels.data(), width, height, GL_RED, GL_UNSIGNED_BYTE, raster.x - xorig,
                 raster.y - yorig, (GLfloat)width, (GLfloat)height, raster.z,
                 quad_mode_t::bitmap, g_glstate.fpe_uniform.raster_color);
    }

    // The raster position always advances, even for w/h 0 or null bitmaps.
    raster.x += xmove;
    raster.y += ymove;
}

namespace {

struct pixel_format_info_t {
    int components = 0;
    bool depth = false;
    bool stencil = false;
};

bool describePixelFormat(GLenum format, pixel_format_info_t* out) {
    switch (format) {
    case GL_RED:
    case GL_GREEN:
    case GL_BLUE:
    case GL_ALPHA:
    case GL_LUMINANCE:
        out->components = 1;
        return true;
    case GL_LUMINANCE_ALPHA:
        out->components = 2;
        return true;
    case GL_RGB:
    case GL_BGR:
        out->components = 3;
        return true;
    case GL_RGBA:
    case GL_BGRA:
        out->components = 4;
        return true;
    case GL_DEPTH_COMPONENT:
        out->components = 1;
        out->depth = true;
        return true;
    case GL_STENCIL_INDEX:
        out->components = 1;
        out->stencil = true;
        return true;
    default:
        return false;
    }
}

bool scalarPixelType(GLenum type, size_t* bytes) {
    switch (type) {
    case GL_BYTE:
    case GL_UNSIGNED_BYTE:
        *bytes = 1;
        return true;
    case GL_SHORT:
    case GL_UNSIGNED_SHORT:
    case GL_HALF_FLOAT:
        *bytes = 2;
        return true;
    case GL_INT:
    case GL_UNSIGNED_INT:
    case GL_FLOAT:
        *bytes = 4;
        return true;
    default:
        return false;
    }
}

bool packedPixelType(GLenum type, int widths[4], int* components, size_t* bytes,
                     bool* reversed) {
    *reversed = false;
    switch (type) {
    case GL_UNSIGNED_BYTE_3_3_2:
        widths[0] = 3; widths[1] = 3; widths[2] = 2;
        *components = 3; *bytes = 1;
        return true;
    case GL_UNSIGNED_BYTE_2_3_3_REV:
        widths[0] = 3; widths[1] = 3; widths[2] = 2;
        *components = 3; *bytes = 1; *reversed = true;
        return true;
    case GL_UNSIGNED_SHORT_5_6_5:
        widths[0] = 5; widths[1] = 6; widths[2] = 5;
        *components = 3; *bytes = 2;
        return true;
    case GL_UNSIGNED_SHORT_5_6_5_REV:
        widths[0] = 5; widths[1] = 6; widths[2] = 5;
        *components = 3; *bytes = 2; *reversed = true;
        return true;
    case GL_UNSIGNED_SHORT_4_4_4_4:
        widths[0] = 4; widths[1] = 4; widths[2] = 4; widths[3] = 4;
        *components = 4; *bytes = 2;
        return true;
    case GL_UNSIGNED_SHORT_4_4_4_4_REV:
        widths[0] = 4; widths[1] = 4; widths[2] = 4; widths[3] = 4;
        *components = 4; *bytes = 2; *reversed = true;
        return true;
    case GL_UNSIGNED_SHORT_5_5_5_1:
        widths[0] = 5; widths[1] = 5; widths[2] = 5; widths[3] = 1;
        *components = 4; *bytes = 2;
        return true;
    case GL_UNSIGNED_SHORT_1_5_5_5_REV:
        widths[0] = 5; widths[1] = 5; widths[2] = 5; widths[3] = 1;
        *components = 4; *bytes = 2; *reversed = true;
        return true;
    case GL_UNSIGNED_INT_8_8_8_8:
        widths[0] = 8; widths[1] = 8; widths[2] = 8; widths[3] = 8;
        *components = 4; *bytes = 4;
        return true;
    case GL_UNSIGNED_INT_8_8_8_8_REV:
        widths[0] = 8; widths[1] = 8; widths[2] = 8; widths[3] = 8;
        *components = 4; *bytes = 4; *reversed = true;
        return true;
    case GL_UNSIGNED_INT_10_10_10_2:
        widths[0] = 10; widths[1] = 10; widths[2] = 10; widths[3] = 2;
        *components = 4; *bytes = 4;
        return true;
    case GL_UNSIGNED_INT_2_10_10_10_REV:
        widths[0] = 10; widths[1] = 10; widths[2] = 10; widths[3] = 2;
        *components = 4; *bytes = 4; *reversed = true;
        return true;
    default:
        return false;
    }
}

bool describePixelType(GLenum format, const pixel_format_info_t& format_info, GLenum type,
                       size_t* pixel_bytes) {
    size_t scalar_bytes = 0;
    if (scalarPixelType(type, &scalar_bytes)) {
        *pixel_bytes = scalar_bytes * static_cast<size_t>(format_info.components);
        return true;
    }

    int widths[4] = {};
    int packed_components = 0;
    bool reversed = false;
    if (!packedPixelType(type, widths, &packed_components, pixel_bytes, &reversed)) return false;
    (void)widths;
    (void)reversed;
    if ((packed_components == 3 && format != GL_RGB && format != GL_BGR) ||
        (packed_components == 4 && format != GL_RGBA && format != GL_BGRA)) {
        g_glstate.set_error(GL_INVALID_OPERATION);
        return false;
    }
    return true;
}

template <typename T>
T loadPixelScalar(const uint8_t* source, bool swap_bytes) {
    T value{};
    if (swap_bytes && sizeof(T) > 1) {
        uint8_t bytes[sizeof(T)];
        std::memcpy(bytes, source, sizeof(T));
        std::reverse(bytes, bytes + sizeof(T));
        std::memcpy(&value, bytes, sizeof(T));
    } else {
        std::memcpy(&value, source, sizeof(T));
    }
    return value;
}

float halfToFloat(uint16_t bits) {
    const bool negative = (bits & 0x8000u) != 0;
    const uint16_t exponent = (bits >> 10u) & 0x1Fu;
    const uint16_t mantissa = bits & 0x03FFu;
    float value = 0.0f;
    if (exponent == 0) {
        value = std::ldexp(static_cast<float>(mantissa), -24);
    } else if (exponent == 0x1Fu) {
        value = mantissa == 0 ? std::numeric_limits<float>::infinity()
                              : std::numeric_limits<float>::quiet_NaN();
    } else {
        value = std::ldexp(1.0f + static_cast<float>(mantissa) / 1024.0f,
                           static_cast<int>(exponent) - 15);
    }
    return negative ? -value : value;
}

float readPixelComponent(const uint8_t* source, GLenum type, bool swap_bytes) {
    switch (type) {
    case GL_BYTE: return sfpewNormalizeColorComponent(loadPixelScalar<GLbyte>(source, false));
    case GL_UNSIGNED_BYTE:
        return sfpewNormalizeColorComponent(loadPixelScalar<GLubyte>(source, false));
    case GL_SHORT: return sfpewNormalizeColorComponent(loadPixelScalar<GLshort>(source, swap_bytes));
    case GL_UNSIGNED_SHORT:
        return sfpewNormalizeColorComponent(loadPixelScalar<GLushort>(source, swap_bytes));
    case GL_INT: return sfpewNormalizeColorComponent(loadPixelScalar<GLint>(source, swap_bytes));
    case GL_UNSIGNED_INT:
        return sfpewNormalizeColorComponent(loadPixelScalar<GLuint>(source, swap_bytes));
    case GL_FLOAT: return loadPixelScalar<GLfloat>(source, swap_bytes);
    case GL_HALF_FLOAT:
        return halfToFloat(loadPixelScalar<uint16_t>(source, swap_bytes));
    default: return 0.0f;
    }
}

void decodePackedPixel(const uint8_t* source, GLenum type, bool swap_bytes, float values[4],
                       int* components) {
    int widths[4] = {};
    size_t bytes = 0;
    bool reversed = false;
    packedPixelType(type, widths, components, &bytes, &reversed);
    uint32_t word = 0;
    if (bytes == 1)
        word = loadPixelScalar<uint8_t>(source, false);
    else if (bytes == 2)
        word = loadPixelScalar<uint16_t>(source, swap_bytes);
    else
        word = loadPixelScalar<uint32_t>(source, swap_bytes);

    int shift = reversed ? 0 : static_cast<int>(bytes * 8u);
    for (int component = 0; component < *components; ++component) {
        if (reversed)
            shift += component == 0 ? 0 : widths[component - 1];
        else
            shift -= widths[component];
        const uint32_t mask = (1u << widths[component]) - 1u;
        values[component] = static_cast<float>((word >> shift) & mask) /
                            static_cast<float>(mask);
    }
}

glm::vec4 expandPixel(GLenum format, const float values[4]) {
    switch (format) {
    case GL_RED: return {values[0], 0.0f, 0.0f, 1.0f};
    case GL_GREEN: return {0.0f, values[0], 0.0f, 1.0f};
    case GL_BLUE: return {0.0f, 0.0f, values[0], 1.0f};
    case GL_ALPHA: return {0.0f, 0.0f, 0.0f, values[0]};
    case GL_RGB: return {values[0], values[1], values[2], 1.0f};
    case GL_BGR: return {values[2], values[1], values[0], 1.0f};
    case GL_RGBA: return {values[0], values[1], values[2], values[3]};
    case GL_BGRA: return {values[2], values[1], values[0], values[3]};
    case GL_LUMINANCE: return {values[0], values[0], values[0], 1.0f};
    case GL_LUMINANCE_ALPHA: return {values[0], values[0], values[0], values[1]};
    case GL_DEPTH_COMPONENT:
    case GL_STENCIL_INDEX:
        return {values[0], 0.0f, 0.0f, 1.0f};
    default: return {0.0f};
    }
}

glm::vec4 decodePixel(const uint8_t* source, GLenum format, GLenum type,
                      const pixel_format_info_t& format_info, bool swap_bytes) {
    float values[4] = {};
    size_t scalar_bytes = 0;
    if (scalarPixelType(type, &scalar_bytes)) {
        for (int component = 0; component < format_info.components; ++component) {
            values[component] =
                readPixelComponent(source + static_cast<size_t>(component) * scalar_bytes, type,
                                   swap_bytes);
        }
    } else {
        int ignored_components = 0;
        decodePackedPixel(source, type, swap_bytes, values, &ignored_components);
    }
    return expandPixel(format, values);
}

bool checkedPixelAdd(size_t left, size_t right, size_t* out) {
    if (right > std::numeric_limits<size_t>::max() - left) return false;
    *out = left + right;
    return true;
}

bool checkedPixelMultiply(size_t left, size_t right, size_t* out) {
    if (left != 0 && right > std::numeric_limits<size_t>::max() / left) return false;
    *out = left * right;
    return true;
}

struct pixel_unpack_view_t {
    const uint8_t* pixels = nullptr;
    size_t row_stride = 0;
    GLenum mapped_target = GL_NONE;
    GLuint scratch = 0;
    GLint previous_copy_write = 0;

    ~pixel_unpack_view_t() {
        if (mapped_target != GL_NONE && g_glFuncs.glUnmapBuffer != nullptr)
            g_glFuncs.glUnmapBuffer(mapped_target);
        if (scratch != 0) {
            g_glFuncs.glBindBuffer(GL_COPY_WRITE_BUFFER, static_cast<GLuint>(previous_copy_write));
            g_glFuncs.glDeleteBuffers(1, &scratch);
        }
    }
};

void drainFailedMapError() {
    if (g_glFuncs.glGetError == nullptr) return;
    while (g_glFuncs.glGetError() != GL_NO_ERROR) {}
}

bool preparePixelUnpackView(GLsizei width, GLsizei height, size_t pixel_bytes,
                            const GLvoid* pixels, pixel_unpack_view_t* out) {
    if (g_glFuncs.glGetIntegerv == nullptr) return false;

    GLint alignment = 4, row_length = 0, skip_rows = 0, skip_pixels = 0, pbo = 0;
    g_glFuncs.glGetIntegerv(GL_UNPACK_ALIGNMENT, &alignment);
    g_glFuncs.glGetIntegerv(GL_UNPACK_ROW_LENGTH, &row_length);
    g_glFuncs.glGetIntegerv(GL_UNPACK_SKIP_ROWS, &skip_rows);
    g_glFuncs.glGetIntegerv(GL_UNPACK_SKIP_PIXELS, &skip_pixels);
    g_glFuncs.glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &pbo);
    if ((alignment != 1 && alignment != 2 && alignment != 4 && alignment != 8) ||
        row_length < 0 || skip_rows < 0 || skip_pixels < 0) {
        g_glstate.set_error(GL_INVALID_OPERATION);
        return false;
    }

    const size_t row_pixels = row_length > 0 ? static_cast<size_t>(row_length)
                                             : static_cast<size_t>(width);
    size_t row_bytes = 0, padded_row_bytes = 0;
    if (!checkedPixelMultiply(row_pixels, pixel_bytes, &row_bytes) ||
        !checkedPixelAdd(row_bytes, static_cast<size_t>(alignment - 1), &padded_row_bytes)) {
        g_glstate.set_error(GL_OUT_OF_MEMORY);
        return false;
    }
    out->row_stride = padded_row_bytes & ~static_cast<size_t>(alignment - 1);

    size_t start = 0, last_row = 0, final_row_bytes = 0, needed = 0;
    if (!checkedPixelMultiply(static_cast<size_t>(skip_rows), out->row_stride, &start) ||
        !checkedPixelMultiply(static_cast<size_t>(skip_pixels), pixel_bytes, &final_row_bytes) ||
        !checkedPixelAdd(start, final_row_bytes, &start) ||
        !checkedPixelMultiply(static_cast<size_t>(height - 1), out->row_stride, &last_row) ||
        !checkedPixelMultiply(static_cast<size_t>(width), pixel_bytes, &final_row_bytes) ||
        !checkedPixelAdd(start, last_row, &needed) ||
        !checkedPixelAdd(needed, final_row_bytes, &needed)) {
        g_glstate.set_error(GL_OUT_OF_MEMORY);
        return false;
    }

    if (pbo == 0) {
        if (pixels == nullptr) return false;
        out->pixels = static_cast<const uint8_t*>(pixels) + start;
        return true;
    }
    if (g_glFuncs.glGetBufferParameteriv == nullptr || g_glFuncs.glMapBufferRange == nullptr ||
        g_glFuncs.glUnmapBuffer == nullptr) {
        g_glstate.set_error(GL_INVALID_OPERATION);
        return false;
    }

    GLint buffer_size = 0, buffer_mapped = GL_FALSE;
    g_glFuncs.glGetBufferParameteriv(GL_PIXEL_UNPACK_BUFFER, GL_BUFFER_SIZE, &buffer_size);
    g_glFuncs.glGetBufferParameteriv(GL_PIXEL_UNPACK_BUFFER, GL_BUFFER_MAPPED, &buffer_mapped);
    const size_t offset = reinterpret_cast<uintptr_t>(pixels);
    if (buffer_mapped == GL_TRUE || buffer_size < 0 || offset > static_cast<size_t>(buffer_size) ||
        needed > static_cast<size_t>(buffer_size) - offset ||
        needed > static_cast<size_t>(std::numeric_limits<GLsizeiptr>::max()) ||
        offset > static_cast<size_t>(std::numeric_limits<GLintptr>::max())) {
        g_glstate.set_error(GL_INVALID_OPERATION);
        return false;
    }

    const auto* mapped = static_cast<const uint8_t*>(g_glFuncs.glMapBufferRange(
        GL_PIXEL_UNPACK_BUFFER, static_cast<GLintptr>(offset), static_cast<GLsizeiptr>(needed),
        GL_MAP_READ_BIT));
    if (mapped != nullptr) {
        out->mapped_target = GL_PIXEL_UNPACK_BUFFER;
        out->pixels = mapped + start;
        return true;
    }

    // Upload-oriented mobile buffers are often not directly READ-mappable.
    // Copy the exact source range into a STREAM_READ buffer before giving up.
    drainFailedMapError();
    if (g_glFuncs.glCopyBufferSubData == nullptr || g_glFuncs.glGenBuffers == nullptr ||
        g_glFuncs.glDeleteBuffers == nullptr || g_glFuncs.glBindBuffer == nullptr ||
        g_glFuncs.glBufferData == nullptr) {
        g_glstate.set_error(GL_INVALID_OPERATION);
        return false;
    }
    g_glFuncs.glGetIntegerv(GL_COPY_WRITE_BUFFER_BINDING, &out->previous_copy_write);
    g_glFuncs.glGenBuffers(1, &out->scratch);
    g_glFuncs.glBindBuffer(GL_COPY_WRITE_BUFFER, out->scratch);
    g_glFuncs.glBufferData(GL_COPY_WRITE_BUFFER, static_cast<GLsizeiptr>(needed), nullptr,
                           GL_STREAM_READ);
    g_glFuncs.glCopyBufferSubData(GL_PIXEL_UNPACK_BUFFER, GL_COPY_WRITE_BUFFER,
                                  static_cast<GLintptr>(offset), 0,
                                  static_cast<GLsizeiptr>(needed));
    mapped = static_cast<const uint8_t*>(g_glFuncs.glMapBufferRange(
        GL_COPY_WRITE_BUFFER, 0, static_cast<GLsizeiptr>(needed), GL_MAP_READ_BIT));
    if (mapped == nullptr) {
        drainFailedMapError();
        g_glFuncs.glBindBuffer(GL_COPY_WRITE_BUFFER,
                               static_cast<GLuint>(out->previous_copy_write));
        g_glFuncs.glDeleteBuffers(1, &out->scratch);
        out->scratch = 0;
        g_glstate.set_error(GL_INVALID_OPERATION);
        return false;
    }
    out->mapped_target = GL_COPY_WRITE_BUFFER;
    out->pixels = mapped + start;
    return true;
}

float clampPixel(float value) {
    if (std::isnan(value)) return 0.0f;
    return std::clamp(value, 0.0f, 1.0f);
}

} // namespace

void glDrawPixels(GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid* pixels) {
    sfpewEntryBarrier();
    if (width < 0 || height < 0) {
        g_glstate.set_error(GL_INVALID_VALUE);
        return;
    }
    pixel_format_info_t format_info;
    if (!describePixelFormat(format, &format_info)) {
        g_glstate.set_error(GL_INVALID_ENUM);
        return;
    }
    size_t pixel_bytes = 0;
    if (!describePixelType(format, format_info, type, &pixel_bytes)) {
        if (g_glstate.first_error == GL_NO_ERROR)
            g_glstate.set_error(GL_INVALID_ENUM);
        return;
    }
    if (format_info.stencil) {
        // ES 3.0 cannot upload stencil values through a fragment shader, and
        // no portable framebuffer path exposes per-fragment stencil replace
        // values. Refuse the operation explicitly instead of drawing color.
        g_glstate.set_error(GL_INVALID_OPERATION);
        return;
    }
    if (!g_glstate.fpe_uniform.raster_position_valid || width == 0 || height == 0) return;
    if (!sfpewEnsureBackend() || g_glFuncs.glTexImage2D == nullptr) return;

    size_t pixel_count = 0, rgba_count = 0;
    if (!checkedPixelMultiply(static_cast<size_t>(width), static_cast<size_t>(height),
                              &pixel_count) ||
        (!format_info.depth && !checkedPixelMultiply(pixel_count, 4u, &rgba_count))) {
        g_glstate.set_error(GL_OUT_OF_MEMORY);
        return;
    }

    const auto& un = g_glstate.fpe_uniform;
    const bool imaging = !format_info.depth && sfpewImagingActive();
    const bool float_output = format_info.depth || type == GL_FLOAT || type == GL_HALF_FLOAT || imaging;
    std::vector<GLfloat> float_pixels;
    std::vector<GLubyte> byte_pixels;
    try {
        if (float_output)
            float_pixels.resize(format_info.depth ? pixel_count : rgba_count);
        else
            byte_pixels.resize(rgba_count);
    } catch (const std::bad_alloc&) {
        g_glstate.set_error(GL_OUT_OF_MEMORY);
        return;
    }

    {
        pixel_unpack_view_t source;
        if (!preparePixelUnpackView(width, height, pixel_bytes, pixels, &source)) return;
        const bool swap_bytes = g_glstate.pixel_store_unpack_swap_bytes;
        for (GLsizei row = 0; row < height; ++row) {
            const uint8_t* line = source.pixels + static_cast<size_t>(row) * source.row_stride;
            for (GLsizei column = 0; column < width; ++column) {
                const size_t pixel = static_cast<size_t>(row) * static_cast<size_t>(width) +
                                     static_cast<size_t>(column);
                glm::vec4 value = decodePixel(line + static_cast<size_t>(column) * pixel_bytes,
                                              format, type, format_info, swap_bytes);
                if (format_info.depth) {
                    float_pixels[pixel] =
                        clampPixel(value.r * un.pixel_scale[4] + un.pixel_bias[4]);
                    continue;
                }
                for (int channel = 0; channel < 4; ++channel) {
                    float transferred =
                        clampPixel(value[channel] * un.pixel_scale[channel] + un.pixel_bias[channel]);
                    if (un.pixel_map_color) {
                        const auto& map = g_glstate.pixel_maps[
                            static_cast<size_t>(GL_PIXEL_MAP_R_TO_R - GL_PIXEL_MAP_I_TO_I) +
                            static_cast<size_t>(channel)];
                        const size_t map_index = std::min(
                            static_cast<size_t>(std::floor(transferred * map.size())),
                            map.size() - 1u);
                        transferred = map[map_index];
                    }
                    if (float_output)
                        float_pixels[pixel * 4u + static_cast<size_t>(channel)] = transferred;
                    else
                        byte_pixels[pixel * 4u + static_cast<size_t>(channel)] =
                            static_cast<GLubyte>(transferred * 255.0f + 0.5f);
                }
            }
        }
    }

    GLsizei output_width = width, output_height = height;
    if (imaging) {
        sfpewImagingTransfer(float_pixels.data(), &output_width, &output_height);
        if (sfpewImagingSink()) return;
    }
    const auto& raster = un.raster_position;
    const void* upload = float_output ? static_cast<const void*>(float_pixels.data())
                                      : static_cast<const void*>(byte_pixels.data());
    const GLenum upload_format = format_info.depth ? GL_RED : GL_RGBA;
    const GLenum upload_type = float_output ? GL_FLOAT : GL_UNSIGNED_BYTE;
    drawQuad(upload, output_width, output_height, upload_format, upload_type, raster.x, raster.y,
             output_width * un.pixel_zoom_x, output_height * un.pixel_zoom_y, raster.z,
             format_info.depth ? quad_mode_t::depth : quad_mode_t::color, glm::vec4(1.0f));
}

namespace {

struct framebuffer_plane_t {
    bool present = false;
    GLint object_type = GL_NONE;
    GLint object_name = 0;
    GLint bits = 0;
    GLint component_type = 0;
};

GLenum framebufferPlaneAttachment(GLint framebuffer, GLenum type) {
    if (framebuffer == 0) return type == GL_DEPTH ? GL_DEPTH : GL_STENCIL;
    return type == GL_DEPTH ? GL_DEPTH_ATTACHMENT : GL_STENCIL_ATTACHMENT;
}

bool queryFramebufferPlane(GLenum target, GLint framebuffer, GLenum type,
                           framebuffer_plane_t* out) {
    if (g_glFuncs.glGetFramebufferAttachmentParameteriv == nullptr) return false;
    const GLenum attachment = framebufferPlaneAttachment(framebuffer, type);
    g_glFuncs.glGetFramebufferAttachmentParameteriv(
        target, attachment, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &out->object_type);
    if (out->object_type == GL_NONE) return true;
    if (framebuffer != 0) {
        g_glFuncs.glGetFramebufferAttachmentParameteriv(
            target, attachment, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &out->object_name);
    }
    g_glFuncs.glGetFramebufferAttachmentParameteriv(
        target, attachment,
        type == GL_DEPTH ? GL_FRAMEBUFFER_ATTACHMENT_DEPTH_SIZE
                         : GL_FRAMEBUFFER_ATTACHMENT_STENCIL_SIZE,
        &out->bits);
    g_glFuncs.glGetFramebufferAttachmentParameteriv(
        target, attachment, GL_FRAMEBUFFER_ATTACHMENT_COMPONENT_TYPE, &out->component_type);
    out->present = out->bits > 0;
    return true;
}

bool planesShareStorage(GLint framebuffer, const framebuffer_plane_t& depth,
                        const framebuffer_plane_t& stencil) {
    if (!depth.present || !stencil.present || depth.object_type != stencil.object_type) return false;
    if (framebuffer == 0) return true;
    return depth.object_name != 0 && depth.object_name == stencil.object_name;
}

GLenum scratchPlaneFormat(GLenum type, GLint framebuffer, const framebuffer_plane_t& requested,
                          const framebuffer_plane_t& other) {
    const framebuffer_plane_t& depth = type == GL_DEPTH ? requested : other;
    const framebuffer_plane_t& stencil = type == GL_STENCIL ? requested : other;
    if (planesShareStorage(framebuffer, depth, stencil)) {
        return depth.component_type == GL_FLOAT ? GL_DEPTH32F_STENCIL8
                                                : GL_DEPTH24_STENCIL8;
    }
    if (type == GL_STENCIL) return requested.bits <= 8 ? GL_STENCIL_INDEX8 : GL_NONE;
    if (requested.bits <= 16) return GL_DEPTH_COMPONENT16;
    if (requested.bits <= 24) return GL_DEPTH_COMPONENT24;
    return requested.component_type == GL_FLOAT ? GL_DEPTH_COMPONENT32F
                                                : 0x81A7 /* GL_DEPTH_COMPONENT32 */;
}

struct copy_pixels_scratch_t {
    GLuint fbo = 0;
    GLuint renderbuffer = 0;
    GLsizei width = 0;
    GLsizei height = 0;
    GLint samples = 0;
    GLenum internal_format = GL_NONE;
};

copy_pixels_scratch_t& copyPixelsScratch() {
    static thread_local struct {
        EGLContext context = EGL_NO_CONTEXT;
        copy_pixels_scratch_t scratch{};
    } cache;
    const EGLContext current = sfpewCurrentContext();
    if (cache.context != current) {
        cache.context = current;
        cache.scratch = {};
    }
    return cache.scratch;
}

bool ensureCopyPixelsScratch(copy_pixels_scratch_t& scratch, GLsizei width, GLsizei height,
                             GLint samples, GLenum internal_format) {
    if (g_glFuncs.glGenFramebuffers == nullptr || g_glFuncs.glGenRenderbuffers == nullptr ||
        g_glFuncs.glBindRenderbuffer == nullptr || g_glFuncs.glRenderbufferStorage == nullptr ||
        g_glFuncs.glFramebufferRenderbuffer == nullptr ||
        g_glFuncs.glCheckFramebufferStatus == nullptr || g_glFuncs.glDrawBuffers == nullptr ||
        g_glFuncs.glReadBuffer == nullptr) {
        return false;
    }
    if (scratch.fbo == 0) g_glFuncs.glGenFramebuffers(1, &scratch.fbo);
    if (scratch.renderbuffer == 0) g_glFuncs.glGenRenderbuffers(1, &scratch.renderbuffer);
    if (scratch.fbo == 0 || scratch.renderbuffer == 0) return false;

    const bool resized = scratch.width != width || scratch.height != height ||
                         scratch.samples != samples || scratch.internal_format != internal_format;
    g_glFuncs.glBindRenderbuffer(GL_RENDERBUFFER, scratch.renderbuffer);
    if (resized) {
        if (samples > 0) {
            if (g_glFuncs.glRenderbufferStorageMultisample == nullptr) return false;
            g_glFuncs.glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, internal_format,
                                                       width, height);
        } else {
            g_glFuncs.glRenderbufferStorage(GL_RENDERBUFFER, internal_format, width, height);
        }
    }

    g_glFuncs.glBindFramebuffer(GL_FRAMEBUFFER, scratch.fbo);
    g_glFuncs.glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, 0);
    g_glFuncs.glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, 0);
    g_glFuncs.glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER,
                                        0);
    if (internal_format == GL_STENCIL_INDEX8) {
        g_glFuncs.glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER,
                                            scratch.renderbuffer);
    } else if (internal_format == GL_DEPTH24_STENCIL8 ||
               internal_format == GL_DEPTH32F_STENCIL8) {
        g_glFuncs.glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                                            GL_RENDERBUFFER, scratch.renderbuffer);
    } else {
        g_glFuncs.glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER,
                                            scratch.renderbuffer);
    }
    const GLenum none = GL_NONE;
    g_glFuncs.glDrawBuffers(1, &none);
    g_glFuncs.glReadBuffer(GL_NONE);
    if (g_glFuncs.glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) return false;
    if (resized) {
        scratch.width = width;
        scratch.height = height;
        scratch.samples = samples;
        scratch.internal_format = internal_format;
    }
    return true;
}

struct copy_pixels_backend_guard_t {
    GLint read_fbo = 0;
    GLint draw_fbo = 0;
    GLint renderbuffer = 0;
    bool scissor = false;

    copy_pixels_backend_guard_t() {
        g_glFuncs.glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read_fbo);
        g_glFuncs.glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw_fbo);
        g_glFuncs.glGetIntegerv(GL_RENDERBUFFER_BINDING, &renderbuffer);
        scissor = g_glFuncs.glIsEnabled != nullptr &&
                  g_glFuncs.glIsEnabled(GL_SCISSOR_TEST) == GL_TRUE;
    }

    ~copy_pixels_backend_guard_t() {
        g_glFuncs.glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(read_fbo));
        g_glFuncs.glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(draw_fbo));
        g_glFuncs.glBindRenderbuffer(GL_RENDERBUFFER, static_cast<GLuint>(renderbuffer));
        if (scissor)
            g_glFuncs.glEnable(GL_SCISSOR_TEST);
        else
            g_glFuncs.glDisable(GL_SCISSOR_TEST);
    }
};

bool rectanglesOverlap(GLint ax0, GLint ay0, GLint ax1, GLint ay1, GLint bx0, GLint by0,
                       GLint bx1, GLint by1) {
    const GLint amin_x = std::min(ax0, ax1), amax_x = std::max(ax0, ax1);
    const GLint amin_y = std::min(ay0, ay1), amax_y = std::max(ay0, ay1);
    const GLint bmin_x = std::min(bx0, bx1), bmax_x = std::max(bx0, bx1);
    const GLint bmin_y = std::min(by0, by1), bmax_y = std::max(by0, by1);
    return std::max(amin_x, bmin_x) < std::min(amax_x, bmax_x) &&
           std::max(amin_y, bmin_y) < std::min(amax_y, bmax_y);
}

} // namespace

void glCopyPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum type) {
    sfpewEntryBarrier();
    if (width < 0 || height < 0) {
        g_glstate.set_error(GL_INVALID_VALUE);
        return;
    }
    if (type != GL_COLOR && type != GL_DEPTH && type != GL_STENCIL) {
        g_glstate.set_error(GL_INVALID_ENUM);
        return;
    }
    if (!g_glstate.fpe_uniform.raster_position_valid || width == 0 || height == 0 ||
        !sfpewEnsureBackend() || g_glFuncs.glBlitFramebuffer == nullptr) {
        return;
    }
    const auto& un = g_glstate.fpe_uniform;
    const GLint dx0 = (GLint)un.raster_position.x;
    const GLint dy0 = (GLint)un.raster_position.y;
    const GLint dx1 = dx0 + (GLint)(width * un.pixel_zoom_x);
    const GLint dy1 = dy0 + (GLint)(height * un.pixel_zoom_y);
    const GLbitfield mask = type == GL_COLOR     ? GL_COLOR_BUFFER_BIT
                            : type == GL_DEPTH   ? GL_DEPTH_BUFFER_BIT
                                                : GL_STENCIL_BUFFER_BIT;

    if (type == GL_COLOR && sfpewImagingActive()) {
        std::vector<GLfloat> rgba;
        GLsizei output_width = width, output_height = height;
        if (!sfpewImagingReadRgba(x, y, width, height, &rgba, &output_width, &output_height))
            return;
        if (sfpewImagingSink() || rgba.empty()) return;
        drawQuad(rgba.data(), output_width, output_height, GL_RGBA, GL_FLOAT,
                 un.raster_position.x, un.raster_position.y,
                 output_width * un.pixel_zoom_x, output_height * un.pixel_zoom_y,
                 un.raster_position.z, quad_mode_t::color, glm::vec4(1.0f));
        return;
    }

    copy_pixels_backend_guard_t backend;
    if (type != GL_COLOR) {
        framebuffer_plane_t source_plane, source_other, destination_plane;
        const GLenum other_type = type == GL_DEPTH ? GL_STENCIL : GL_DEPTH;
        if (!queryFramebufferPlane(GL_READ_FRAMEBUFFER, backend.read_fbo, type, &source_plane) ||
            !queryFramebufferPlane(GL_READ_FRAMEBUFFER, backend.read_fbo, other_type,
                                   &source_other) ||
            !queryFramebufferPlane(GL_DRAW_FRAMEBUFFER, backend.draw_fbo, type,
                                   &destination_plane) ||
            !source_plane.present || !destination_plane.present) {
            g_glstate.set_error(GL_INVALID_OPERATION);
            return;
        }

        const bool overlap = backend.read_fbo == backend.draw_fbo &&
                             rectanglesOverlap(x, y, x + width, y + height, dx0, dy0, dx1, dy1);
        if (overlap) {
            GLint samples = 0;
            g_glFuncs.glGetIntegerv(GL_SAMPLES, &samples);
            const GLenum internal_format =
                scratchPlaneFormat(type, backend.read_fbo, source_plane, source_other);
            auto& scratch = copyPixelsScratch();
            if (internal_format == GL_NONE ||
                !ensureCopyPixelsScratch(scratch, width, height, samples, internal_format)) {
                g_glstate.set_error(GL_INVALID_OPERATION);
                return;
            }

            // The caller's scissor belongs to the destination pixel
            // operation. It must not clip the intermediate source snapshot.
            if (backend.scissor) g_glFuncs.glDisable(GL_SCISSOR_TEST);
            g_glFuncs.glBindFramebuffer(GL_READ_FRAMEBUFFER,
                                        static_cast<GLuint>(backend.read_fbo));
            g_glFuncs.glBindFramebuffer(GL_DRAW_FRAMEBUFFER, scratch.fbo);
            g_glFuncs.glBlitFramebuffer(x, y, x + width, y + height, 0, 0, width, height, mask,
                                        GL_NEAREST);

            g_glFuncs.glBindFramebuffer(GL_READ_FRAMEBUFFER, scratch.fbo);
            g_glFuncs.glBindFramebuffer(GL_DRAW_FRAMEBUFFER,
                                        static_cast<GLuint>(backend.draw_fbo));
            if (backend.scissor) g_glFuncs.glEnable(GL_SCISSOR_TEST);
            g_glFuncs.glBlitFramebuffer(0, 0, width, height, dx0, dy0, dx1, dy1, mask,
                                        GL_NEAREST);
            return;
        }
    }

    g_glFuncs.glBlitFramebuffer(x, y, x + width, y + height, dx0, dy0, dx1, dy1, mask,
                                GL_NEAREST);
}
