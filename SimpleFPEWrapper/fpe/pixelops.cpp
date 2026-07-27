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
#include "../log.h"

#include <algorithm>
#include <cstring>
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
                           "uniform int uMode;   // 0 = pixels, 1 = bitmap\n"
                           "in vec2 vUV;\n"
                           "out vec4 FragColor;\n"
                           "void main() {\n"
                           "    vec4 texel = texture(uTex, vUV);\n"
                           "    if (uMode == 1) {\n"
                           "        if (texel.r < 0.5) discard;\n"
                           "        FragColor = uColor;\n"
                           "    } else {\n"
                           "        FragColor = texel * uColor;\n"
                           "    }\n"
                           "}\n";

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
void drawQuad(const void* pixels, GLsizei tex_w, GLsizei tex_h, GLenum tex_format, GLfloat x0,
              GLfloat y0, GLfloat wpx, GLfloat hpx, GLfloat depth, bool bitmap_mode,
              const glm::vec4& color) {
    auto& d = drawer();
    if (!ensureDrawer(d)) return;

    fpe_backend_draw_state_guard_t backend_state;

    GLint viewport[4] = {0, 0, 1, 1};
    g_glFuncs.glGetIntegerv(GL_VIEWPORT, viewport);
    if (viewport[2] <= 0 || viewport[3] <= 0) return;

    // Preserve unit-0 texture binding through the wrapper entry points so
    // the logical shadows stay coherent.
    const GLenum caller_active = sfpewLogicalActiveTexture();
    const GLuint caller_binding = sfpewLogicalTextureBinding(GL_TEXTURE_2D);
    glActiveTexture(GL_TEXTURE0);
    g_glFuncs.glBindTexture(GL_TEXTURE_2D, d.texture);
    g_glFuncs.glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    g_glFuncs.glTexImage2D(GL_TEXTURE_2D, 0, tex_format == GL_RED ? GL_R8 : GL_RGBA8, tex_w, tex_h, 0,
                           tex_format, GL_UNSIGNED_BYTE, pixels);
    g_glFuncs.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    g_glFuncs.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    g_glFuncs.glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

    const auto to_ndc_x = [&](GLfloat wx) { return (wx - viewport[0]) / viewport[2] * 2.0f - 1.0f; };
    const auto to_ndc_y = [&](GLfloat wy) { return (wy - viewport[1]) / viewport[3] * 2.0f - 1.0f; };

    sfpewInvalidateImmediateDrawState();
    g_glFuncs.glUseProgram(d.program);
    sfpewBackendBindVertexArray(d.vao);
    g_glFuncs.glUniform4f(d.loc_rect, to_ndc_x(x0), to_ndc_y(y0), to_ndc_x(x0 + wpx), to_ndc_y(y0 + hpx));
    g_glFuncs.glUniform1f(d.loc_depth, depth * 2.0f - 1.0f);
    g_glFuncs.glUniform4f(d.loc_color, color.r, color.g, color.b, color.a);
    g_glFuncs.glUniform1i(d.loc_mode, bitmap_mode ? 1 : 0);
    g_glFuncs.glUniform1i(d.loc_tex, 0);
    g_glFuncs.glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    // Restore unit-0 binding and the caller's active unit.
    g_glFuncs.glBindTexture(GL_TEXTURE_2D, caller_binding);
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
        drawQuad(texels.data(), width, height, GL_RED, raster.x - xorig, raster.y - yorig,
                 (GLfloat)width, (GLfloat)height, raster.z, true, g_glstate.fpe_uniform.raster_color);
    }

    // The raster position always advances, even for w/h 0 or null bitmaps.
    raster.x += xmove;
    raster.y += ymove;
}

void glDrawPixels(GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid* pixels) {
    sfpewEntryBarrier();
    if (width < 0 || height < 0) {
        g_glstate.set_error(GL_INVALID_VALUE);
        return;
    }
    if (type != GL_UNSIGNED_BYTE) {
        SFPEW_LOGW("glDrawPixels: type 0x%x not implemented (UNSIGNED_BYTE only)", type);
        g_glstate.set_error(GL_INVALID_ENUM);
        return;
    }
    int components;
    switch (format) {
    case GL_RGBA:
    case GL_BGRA:
        components = 4;
        break;
    case GL_RGB:
        components = 3;
        break;
    case GL_LUMINANCE:
    case GL_ALPHA:
        components = 1;
        break;
    default:
        SFPEW_LOGW("glDrawPixels: format 0x%x not implemented", format);
        g_glstate.set_error(GL_INVALID_ENUM);
        return;
    }
    if (!g_glstate.fpe_uniform.raster_position_valid || width == 0 || height == 0 ||
        pixels == nullptr || !sfpewEnsureBackend() || g_glFuncs.glTexImage2D == nullptr) {
        return;
    }
    if (sfpewUnpackPboBound()) {
        SFPEW_LOGW("glDrawPixels from a pixel unpack buffer is not implemented; call skipped");
        return;
    }

    // Convert to RGBA8 applying the transfer scale/bias (identity skips the
    // per-channel math). Tightly packed rows assumed, as elsewhere.
    const auto& un = g_glstate.fpe_uniform;
    const bool transfer = un.pixel_scale[0] != 1.0f || un.pixel_scale[1] != 1.0f ||
                          un.pixel_scale[2] != 1.0f || un.pixel_scale[3] != 1.0f ||
                          un.pixel_bias[0] != 0.0f || un.pixel_bias[1] != 0.0f ||
                          un.pixel_bias[2] != 0.0f || un.pixel_bias[3] != 0.0f;
    const auto* src = static_cast<const uint8_t*>(pixels);
    std::vector<uint8_t> rgba((size_t)width * height * 4u);
    for (size_t px = 0; px < (size_t)width * height; ++px) {
        uint8_t r, g, b, a;
        const uint8_t* in = src + px * components;
        switch (format) {
        case GL_RGBA:
            r = in[0]; g = in[1]; b = in[2]; a = in[3];
            break;
        case GL_BGRA:
            r = in[2]; g = in[1]; b = in[0]; a = in[3];
            break;
        case GL_RGB:
            r = in[0]; g = in[1]; b = in[2]; a = 255;
            break;
        case GL_LUMINANCE:
            r = g = b = in[0]; a = 255;
            break;
        default: // GL_ALPHA
            r = g = b = 0; a = in[0];
            break;
        }
        if (transfer) {
            const auto apply = [&](uint8_t v, int ch) {
                const float scaled = (float)v / 255.0f * un.pixel_scale[ch] + un.pixel_bias[ch];
                return (uint8_t)(std::clamp(scaled, 0.0f, 1.0f) * 255.0f + 0.5f);
            };
            r = apply(r, 0); g = apply(g, 1); b = apply(b, 2); a = apply(a, 3);
        }
        rgba[px * 4 + 0] = r;
        rgba[px * 4 + 1] = g;
        rgba[px * 4 + 2] = b;
        rgba[px * 4 + 3] = a;
    }

    const auto& raster = un.raster_position;
    // Negative zoom mirrors by emitting a rect with negative span.
    drawQuad(rgba.data(), width, height, GL_RGBA, raster.x, raster.y, width * un.pixel_zoom_x,
             height * un.pixel_zoom_y, raster.z, false, glm::vec4(1.0f));
}

void glCopyPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum type) {
    sfpewEntryBarrier();
    if (width < 0 || height < 0) {
        g_glstate.set_error(GL_INVALID_VALUE);
        return;
    }
    if (type != GL_COLOR) {
        SFPEW_LOGW("glCopyPixels: only GL_COLOR is supported (0x%x requested)", type);
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
    // Same-framebuffer blit with overlapping regions is undefined per spec;
    // legacy callers use disjoint regions, matching desktop GL behavior.
    g_glFuncs.glBlitFramebuffer(x, y, x + width, y + height, dx0, dy0, dx1, dy1, GL_COLOR_BUFFER_BIT,
                                GL_NEAREST);
}
