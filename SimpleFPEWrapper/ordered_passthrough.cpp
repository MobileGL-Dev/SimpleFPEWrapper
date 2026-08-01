// SimpleFPEWrapper - SimpleFPEWrapper/ordered_passthrough.cpp
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

#include "init.h"
#include "fpe/drawing1x.h"
#include "fpe/list.h"
#include <vector>
#include <algorithm>
#include <cstdint>
#include "fpe/fpe.hpp"

namespace {

struct logical_program_state_t {
    EGLContext context = EGL_NO_CONTEXT;
    GLint program = 0;
    bool known = false;
};

thread_local logical_program_state_t logicalProgramState;

logical_program_state_t& getLogicalProgramState() {
    // Reconciles against the calling entry's strict-resolve snapshot
    // (docs/context-model.md); no eglGetCurrentContext of its own.
    const EGLContext context = (EGLContext)glstate_t::cached_context();
    if (logicalProgramState.context != context) {
        logicalProgramState = {};
        logicalProgramState.context = context;
    }
    return logicalProgramState;
}

} // namespace

GLint sfpewLogicalProgram() {
    auto& state = getLogicalProgramState();
    // Callers that bypass the glUseProgram wrapper (JNI direct dispatch,
    // layered wrappers) would otherwise desynchronize this shadow forever.
    // Re-read the truth every 256 queries - one cheap glGetIntegerv per ~256
    // draws keeps the FPE interception decision self-healing (plans/07).
    thread_local unsigned reconcile_counter = 0;
    const bool reconcile = (++reconcile_counter & 0xFFu) == 0u;
    // While a fixed-function draw is holding the app's state (plans/12), the
    // backend has the WRAPPER's program bound, so asking it would poison the
    // shadow with an internal program id - and every later draw would then
    // take the user-program path and pass GL_QUADS straight to GLES. The save
    // on the context is the app's truth here; the reconcile just waits for the
    // next unheld query.
    const bool held = g_glstate_c.deferred_draw.held;
    if (!state.known || (reconcile && !held)) {
        if (!sfpewEnsureBackend() || g_glFuncs.glGetIntegerv == nullptr) return 0;
        if (held) {
            state.program = g_glstate_c.deferred_draw.program;
        } else {
            g_glFuncs.glGetIntegerv(GL_CURRENT_PROGRAM, &state.program);
            // A backend readback can catch one of the WRAPPER's own programs:
            // with the deferred restore the FPE program stays bound between
            // fixed-function draws, and any window where this shadow seeds or
            // heals against that binding (context-poll jitter resetting the
            // thread-local state, a heal landing between a flush and its
            // restore) records the wrapper's program as if the app had bound
            // it. Every fixed-function draw then takes the user-program path:
            // no FPE program bind, no uniforms, attributes fed to a shader
            // whose inputs they do not match - on-device this is the
            // menu/world flicker with stacked glyphs and stale frames. The
            // app can never legitimately have one of our internal names
            // bound (they are created and used only inside wrapper draw
            // scopes), so a readback that returns one is always leftover
            // wrapper state, i.e. logically program 0.
            if (state.program != 0 &&
                g_glstate_c.internal_programs.count(state.program) != 0) {
                state.program = 0;
            }
        }
        state.known = true;
    }
    return state.program;
}

#define ORDERED_PASSTHROUGH(name, declaration, arguments)                                                             \
    void name declaration {                                                                                           \
        if (!sfpewEnsureBackend()) return;                                                                            \
        sfpewEntryBarrier();                                                                                 \
        if (g_glFuncs.name != nullptr) g_glFuncs.name arguments;                                                      \
    }

#define OP_ARGS(...) __VA_ARGS__
// Server-state commands are compiled into display lists per GL 2.1 5.4;
// their default tryMerge()==false also makes each one a batching barrier,
// so the display-list draw merger cannot fuse across them.
#define RECORDED_PASSTHROUGH(name, declaration, arguments)                                                            \
    void name declaration {                                                                                           \
        if (!sfpewEnsureBackend()) return;                                                                            \
        sfpewEntryBarrier();                                                                                 \
        LIST_RECORD(name, {}, OP_ARGS arguments)                                                                      \
        if (g_glFuncs.name != nullptr) g_glFuncs.name arguments;                                                      \
    }

RECORDED_PASSTHROUGH(glClear, (GLbitfield mask), (mask))
// glDrawElements lives in drawing.cpp: it is FPE-converted, not passthrough.
void glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type,
                  GLvoid* pixels) {
    if (!sfpewEnsureBackend() || g_glFuncs.glReadPixels == nullptr) return;
    sfpewEntryBarrier();
    // Desktop apps read GL_BGRA, which GLES3 core does not offer: read RGBA
    // and swap in place (tight rows, the common screenshot/AWT case). A
    // desktop GL backend reads it directly.
    if (format == GL_BGRA && sfpewBackendIsES() && type == GL_UNSIGNED_BYTE && pixels != nullptr &&
        width > 0 && height > 0 && !sfpewPackPboBound()) {
        g_glFuncs.glReadPixels(x, y, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        auto* bytes = static_cast<uint8_t*>(pixels);
        const size_t count = (size_t)width * (size_t)height * 4u;
        for (size_t px = 0; px < count; px += 4) std::swap(bytes[px + 0], bytes[px + 2]);
        return;
    }
    g_glFuncs.glReadPixels(x, y, width, height, format, type, pixels);
}
ORDERED_PASSTHROUGH(glFlush, (), ())
ORDERED_PASSTHROUGH(glFinish, (), ())

ORDERED_PASSTHROUGH(glBindFramebuffer, (GLenum target, GLuint framebuffer), (target, framebuffer))
void glUseProgram(GLuint program) {
    if (!sfpewEnsureBackend()) return;
    (void)g_glstate; // entry strict resolve; the program shadow reads the snapshot
    sfpewEntryBarrier();
    if (g_glFuncs.glUseProgram == nullptr) return;
    sfpewInvalidateImmediateDrawState();
    g_glFuncs.glUseProgram(program);

    // Use the backend's post-call value so an invalid/unlinked program cannot
    // poison the logical shadow. This query happens only on glUseProgram,
    // replacing the per-draw GL_CURRENT_PROGRAM query on the hot path.
    auto& state = getLogicalProgramState();
    g_glFuncs.glGetIntegerv(GL_CURRENT_PROGRAM, &state.program);
    // Keep the invariant that the logical shadow never holds one of the
    // wrapper's internal programs (see sfpewLogicalProgram).
    if (state.program != 0 && g_glstate_c.internal_programs.count(state.program) != 0)
        state.program = 0;
    state.known = true;
}

// The blend/mask family keeps a shadow as well as passing through: only a
// shadow lets glPushAttrib(GL_COLOR_BUFFER_BIT) restore it (legacy
// Minecraft brackets GUI and item rendering that way, and a leaked blend
// function corrupts every later translucent draw).
// Blend, colour mask, depth range, hint, pixel store and texture parameters
// take the flush-only barrier: all of them are server or CPU state that
// neither reads nor writes the program, VAO or buffer bindings, and a
// renderer changes blending and texture filtering between draws constantly.
void glBlendColor(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha) {
    if (!sfpewEnsureBackend()) return;
    sfpewClientStateBarrier();
    LIST_RECORD(glBlendColor, {}, red, green, blue, alpha)
    auto& cb = g_glstate.fpe_state.color_buffer;
    cb.blend_color[0] = red;
    cb.blend_color[1] = green;
    cb.blend_color[2] = blue;
    cb.blend_color[3] = alpha;
    if (g_glFuncs.glBlendColor != nullptr) g_glFuncs.glBlendColor(red, green, blue, alpha);
}

void glBlendEquation(GLenum mode) {
    if (!sfpewEnsureBackend()) return;
    sfpewClientStateBarrier();
    LIST_RECORD(glBlendEquation, {}, mode)
    auto& cb = g_glstate.fpe_state.color_buffer;
    cb.equation_rgb = mode;
    cb.equation_alpha = mode;
    if (g_glFuncs.glBlendEquation != nullptr) g_glFuncs.glBlendEquation(mode);
}

void glBlendEquationSeparate(GLenum modeRGB, GLenum modeAlpha) {
    if (!sfpewEnsureBackend()) return;
    sfpewClientStateBarrier();
    LIST_RECORD(glBlendEquationSeparate, {}, modeRGB, modeAlpha)
    auto& cb = g_glstate.fpe_state.color_buffer;
    cb.equation_rgb = modeRGB;
    cb.equation_alpha = modeAlpha;
    if (g_glFuncs.glBlendEquationSeparate != nullptr)
        g_glFuncs.glBlendEquationSeparate(modeRGB, modeAlpha);
}

void glBlendFunc(GLenum sfactor, GLenum dfactor) {
    if (!sfpewEnsureBackend()) return;
    sfpewClientStateBarrier();
    LIST_RECORD(glBlendFunc, {}, sfactor, dfactor)
    auto& cb = g_glstate.fpe_state.color_buffer;
    cb.src_rgb = cb.src_alpha = sfactor;
    cb.dst_rgb = cb.dst_alpha = dfactor;
    if (g_glFuncs.glBlendFunc != nullptr) g_glFuncs.glBlendFunc(sfactor, dfactor);
}

void glBlendFuncSeparate(GLenum sfactorRGB, GLenum dfactorRGB, GLenum sfactorAlpha,
                         GLenum dfactorAlpha) {
    if (!sfpewEnsureBackend()) return;
    sfpewClientStateBarrier();
    LIST_RECORD(glBlendFuncSeparate, {}, sfactorRGB, dfactorRGB, sfactorAlpha, dfactorAlpha)
    auto& cb = g_glstate.fpe_state.color_buffer;
    cb.src_rgb = sfactorRGB;
    cb.dst_rgb = dfactorRGB;
    cb.src_alpha = sfactorAlpha;
    cb.dst_alpha = dfactorAlpha;
    if (g_glFuncs.glBlendFuncSeparate != nullptr)
        g_glFuncs.glBlendFuncSeparate(sfactorRGB, dfactorRGB, sfactorAlpha, dfactorAlpha);
}

void glColorMask(GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha) {
    if (!sfpewEnsureBackend()) return;
    sfpewClientStateBarrier();
    LIST_RECORD(glColorMask, {}, red, green, blue, alpha)
    auto& cb = g_glstate.fpe_state.color_buffer;
    cb.color_mask[0] = red;
    cb.color_mask[1] = green;
    cb.color_mask[2] = blue;
    cb.color_mask[3] = alpha;
    if (g_glFuncs.glColorMask != nullptr) g_glFuncs.glColorMask(red, green, blue, alpha);
}

// Plain per-fragment state. These entries neither read nor write the
// program, VAO or buffer bindings, so they take the flush-only barrier
// (drawing1x.h explains the bar): handing the app back bindings it cannot
// observe only makes the next fixed-function draw re-establish ours, and a
// legacy renderer puts a dozen of these between two draws.
//
// They are also filtered against a shadow of what the backend was last
// told. Nothing else in the wrapper writes this state, so a repeated value
// is unobservable - and re-asserting a whole pass's worth of state around
// every draw is exactly what legacy renderers do.
#define SHADOWED_STATE(name, declaration, arguments, shadow_test)                                  \
    void name declaration {                                                                        \
        if (!sfpewEnsureBackend()) return;                                                         \
        sfpewClientStateBarrier();                                                                 \
        LIST_RECORD(name, {}, OP_ARGS arguments)                                                   \
        auto& shadow = g_glstate.fpe_state.backend_state;                                                    \
        if (shadow.known && (shadow_test)) return;                                                 \
        shadow.known = true;                                                                       \
        if (g_glFuncs.name != nullptr) g_glFuncs.name arguments;                                   \
    }

SHADOWED_STATE(glDepthFunc, (GLenum func), (func),
               shadow.depth_func == func ? true : (shadow.depth_func = func, false))
SHADOWED_STATE(glDepthMask, (GLboolean flag), (flag),
               shadow.depth_mask == flag ? true : (shadow.depth_mask = flag, false))
SHADOWED_STATE(glCullFace, (GLenum mode), (mode),
               shadow.cull_face == mode ? true : (shadow.cull_face = mode, false))
SHADOWED_STATE(glFrontFace, (GLenum mode), (mode),
               shadow.front_face == mode ? true : (shadow.front_face = mode, false))
SHADOWED_STATE(glScissor, (GLint x, GLint y, GLsizei width, GLsizei height),
               (x, y, width, height),
               (shadow.scissor[0] == x && shadow.scissor[1] == y && shadow.scissor[2] == width &&
                shadow.scissor[3] == height)
                   ? true
                   : (shadow.scissor[0] = x, shadow.scissor[1] = y, shadow.scissor[2] = width,
                      shadow.scissor[3] = height, false))
SHADOWED_STATE(glPolygonOffset, (GLfloat factor, GLfloat units), (factor, units),
               (shadow.polygon_offset[0] == factor && shadow.polygon_offset[1] == units)
                   ? true
                   : (shadow.polygon_offset[0] = factor, shadow.polygon_offset[1] = units, false))
SHADOWED_STATE(glLineWidth, (GLfloat width), (width),
               shadow.line_width == width ? true : (shadow.line_width = width, false))
SHADOWED_STATE(glStencilFunc, (GLenum func, GLint ref, GLuint mask), (func, ref, mask),
               (shadow.stencil_func == func && shadow.stencil_ref == ref &&
                shadow.stencil_value_mask == mask)
                   ? true
                   : (shadow.stencil_func = func, shadow.stencil_ref = ref,
                      shadow.stencil_value_mask = mask, false))
SHADOWED_STATE(glStencilMask, (GLuint mask), (mask),
               shadow.stencil_mask == mask ? true : (shadow.stencil_mask = mask, false))
SHADOWED_STATE(glStencilOp, (GLenum fail, GLenum zfail, GLenum zpass), (fail, zfail, zpass),
               (shadow.stencil_fail == fail && shadow.stencil_zfail == zfail &&
                shadow.stencil_zpass == zpass)
                   ? true
                   : (shadow.stencil_fail = fail, shadow.stencil_zfail = zfail,
                      shadow.stencil_zpass = zpass, false))
#undef SHADOWED_STATE

// Viewport keeps the full barrier and no filtering: it is the one entry in
// this group the wrapper itself re-issues (the render-to-texture and blit
// paths), so a shadow here would have to be invalidated from those.
RECORDED_PASSTHROUGH(glViewport, (GLint x, GLint y, GLsizei width, GLsizei height),
                    (x, y, width, height))

namespace {

bool isTextureWrapParameter(GLenum pname) {
    return pname == GL_TEXTURE_WRAP_S || pname == GL_TEXTURE_WRAP_T || pname == GL_TEXTURE_WRAP_R;
}

GLint compatibleTextureParameter(GLenum pname, GLint param) {
    // GL_CLAMP was removed from the programmable/core and GLES profiles used
    // by SFPEW's backends.  Legacy games still use it for projected textures
    // such as Minecraft's entity shadows.  Passing 0x2900 through produces
    // GL_INVALID_ENUM and leaves the sampler at REPEAT, so out-of-range shadow
    // UVs tile across the ground.  An edge clamp is the compatible behavior
    // for these transparent-border textures.
    return isTextureWrapParameter(pname) && param == GL_CLAMP ? GL_CLAMP_TO_EDGE : param;
}

// GL_GENERATE_MIPMAP (GL 1.4): tracked per bound texture object; TexImage
// uploads regenerate the chain via the ES3 glGenerateMipmap.
bool sfpewHandleGenerateMipmapParam(GLenum target, GLenum pname, GLint param) {
    if (pname != 0x8191 /* GL_GENERATE_MIPMAP */) return false;
    if (target == GL_TEXTURE_1D) target = GL_TEXTURE_2D;
    sfpewSetGenerateMipmap(target, sfpewLogicalTextureBinding(target), param != 0);
    return true;
}

} // namespace

void glTexParameterf(GLenum target, GLenum pname, GLfloat param) {
    (void)g_glstate; // entry strict resolve; mipmap tracking reads the binding shadow
    sfpewClientStateBarrier();
    LIST_RECORD(glTexParameterf, {}, target, pname, param)
    if (sfpewHandleGenerateMipmapParam(target, pname, (GLint)param)) return;
    if (g_glFuncs.glTexParameterf == nullptr) return;
    if (isTextureWrapParameter(pname) && static_cast<GLint>(param) == GL_CLAMP)
        param = static_cast<GLfloat>(GL_CLAMP_TO_EDGE);
    g_glFuncs.glTexParameterf(target, pname, param);
}

void glTexParameterfv(GLenum target, GLenum pname, const GLfloat* params) {
    sfpewClientStateBarrier();
    if (g_glFuncs.glTexParameterfv == nullptr || params == nullptr) return;
    LIST_RECORD(glTexParameterfv,
                {{2, (pname == GL_TEXTURE_BORDER_COLOR ? 4u : 1u) * sizeof(GLfloat)}}, target, pname,
                params)
    if (isTextureWrapParameter(pname) && static_cast<GLint>(params[0]) == GL_CLAMP) {
        const GLfloat compatible = static_cast<GLfloat>(GL_CLAMP_TO_EDGE);
        g_glFuncs.glTexParameterfv(target, pname, &compatible);
    } else {
        g_glFuncs.glTexParameterfv(target, pname, params);
    }
}

void glTexParameteri(GLenum target, GLenum pname, GLint param) {
    (void)g_glstate; // entry strict resolve; mipmap tracking reads the binding shadow
    sfpewClientStateBarrier();
    LIST_RECORD(glTexParameteri, {}, target, pname, param)
    if (sfpewHandleGenerateMipmapParam(target, pname, param)) return;
    if (g_glFuncs.glTexParameteri != nullptr)
        g_glFuncs.glTexParameteri(target, pname, compatibleTextureParameter(pname, param));
}

void glTexParameteriv(GLenum target, GLenum pname, const GLint* params) {
    sfpewClientStateBarrier();
    if (g_glFuncs.glTexParameteriv == nullptr || params == nullptr) return;
    LIST_RECORD(glTexParameteriv,
                {{2, (pname == GL_TEXTURE_BORDER_COLOR ? 4u : 1u) * sizeof(GLint)}}, target, pname,
                params)
    if (isTextureWrapParameter(pname) && params[0] == GL_CLAMP) {
        const GLint compatible = GL_CLAMP_TO_EDGE;
        g_glFuncs.glTexParameteriv(target, pname, &compatible);
    } else {
        g_glFuncs.glTexParameteriv(target, pname, params);
    }
}
void glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width,
                     GLsizei height, GLenum format, GLenum type, const GLvoid* pixels) {
    if (!sfpewEnsureBackend() || g_glFuncs.glTexSubImage2D == nullptr) return;
    struct bgra_upload_guard_t {
        sfpew_bgra_upload_t state{};
        bool active = false;
        ~bgra_upload_guard_t() {
            if (active) sfpewFinishBgraUpload(state);
        }
    } bgraUpload;
    (void)g_glstate; // entry strict resolve; mipmap tracking reads the binding shadow
    sfpewEntryBarrier();
    // Legacy formats must match the RED/RG storage glTexImage2D allocated
    // for them; BGRA is swapped on the CPU (tight rows assumed, mirroring
    // the allocation path in getter.cpp).
    if (format == GL_ALPHA || format == GL_LUMINANCE) {
        format = GL_RED;
    } else if (format == GL_LUMINANCE_ALPHA) {
        format = GL_RG;
    } else if (format == GL_BGRA && sfpewBackendIsES()) {
        // Same conversion as glTexImage2D: reorders from client memory or a
        // mapped pixel unpack buffer, honouring row length and skips, into a
        // tight RGBA rectangle - which is what atlas stitching uploads and
        // the old tight-row-only swap silently corrupted.
        if (sfpewPrepareBgraUpload(width, height, type, pixels, &bgraUpload.state)) {
            bgraUpload.active = true;
            pixels = bgraUpload.state.pixels;
            format = GL_RGBA;
            type = GL_UNSIGNED_BYTE;
        } else {
            SFPEW_LOGW("glTexSubImage2D: unsupported GL_BGRA type 0x%x passed through", type);
        }
    }
    g_glFuncs.glTexSubImage2D(target, level, xoffset, yoffset, width, height, format, type, pixels);
    sfpewMaybeGenerateMipmap(target);
}

// --- Desktop-only entry points GLES lacks (plans/03, 3.4) ---------------

void glDepthRange(GLdouble nearVal, GLdouble farVal) {
    if (!sfpewEnsureBackend() || g_glFuncs.glDepthRangef == nullptr) return;
    sfpewClientStateBarrier();
    g_glFuncs.glDepthRangef(static_cast<GLfloat>(nearVal), static_cast<GLfloat>(farVal));
}

void glHint(GLenum target, GLenum mode) {
    if (mode != GL_FASTEST && mode != GL_NICEST && mode != GL_DONT_CARE) {
        g_glstate.set_error(GL_INVALID_ENUM);
        return;
    }
    switch (target) {
    // Backed by the GLES backend.
    case GL_GENERATE_MIPMAP_HINT:
    case GL_FRAGMENT_SHADER_DERIVATIVE_HINT:
        if (!sfpewEnsureBackend() || g_glFuncs.glHint == nullptr) return;
        sfpewClientStateBarrier();
        g_glFuncs.glHint(target, mode);
        return;
    // Legal GL 2.1 hints with no GLES equivalent: accepting them as a no-op
    // is a conforming implementation of a hint.
    case GL_FOG_HINT:
    case GL_LINE_SMOOTH_HINT:
    case GL_PERSPECTIVE_CORRECTION_HINT:
    case GL_POINT_SMOOTH_HINT:
    case GL_POLYGON_SMOOTH_HINT:
    case GL_TEXTURE_COMPRESSION_HINT:
        return;
    default:
        g_glstate.set_error(GL_INVALID_ENUM);
        return;
    }
}

void glPixelStorei(GLenum pname, GLint param) {
    switch (pname) {
    // Desktop-only modes: shadowed for the CPU repack pipeline (plans/05/08).
    case GL_UNPACK_SWAP_BYTES:
        g_glstate.pixel_store_unpack_swap_bytes = param != 0;
        return;
    case GL_UNPACK_LSB_FIRST:
        g_glstate.pixel_store_unpack_lsb_first = param != 0;
        return;
    case GL_PACK_SWAP_BYTES:
        g_glstate.pixel_store_pack_swap_bytes = param != 0;
        return;
    case GL_PACK_LSB_FIRST:
        g_glstate.pixel_store_pack_lsb_first = param != 0;
        return;
    default:
        // Everything else (ALIGNMENT, ROW_LENGTH, SKIP_*, IMAGE_HEIGHT...)
        // is native ES 3.0 state; let the backend validate the value.
        if (!sfpewEnsureBackend() || g_glFuncs.glPixelStorei == nullptr) return;
        sfpewClientStateBarrier();
        g_glFuncs.glPixelStorei(pname, param);
        return;
    }
}

void glPixelStoref(GLenum pname, GLfloat param) { glPixelStorei(pname, static_cast<GLint>(param)); }

#undef ORDERED_PASSTHROUGH
