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
    if (!state.known || reconcile) {
        if (!sfpewEnsureBackend() || g_glFuncs.glGetIntegerv == nullptr) return 0;
        g_glFuncs.glGetIntegerv(GL_CURRENT_PROGRAM, &state.program);
        state.known = true;
    }
    return state.program;
}

#define ORDERED_PASSTHROUGH(name, declaration, arguments)                                                             \
    void name declaration {                                                                                           \
        if (!sfpewEnsureBackend()) return;                                                                            \
        flushPendingImmediateDraws();                                                                                 \
        if (g_glFuncs.name != nullptr) g_glFuncs.name arguments;                                                      \
    }

#define OP_ARGS(...) __VA_ARGS__
// Server-state commands are compiled into display lists per GL 2.1 5.4;
// their default tryMerge()==false also makes each one a batching barrier,
// so the display-list draw merger cannot fuse across them.
#define RECORDED_PASSTHROUGH(name, declaration, arguments)                                                            \
    void name declaration {                                                                                           \
        if (!sfpewEnsureBackend()) return;                                                                            \
        flushPendingImmediateDraws();                                                                                 \
        LIST_RECORD(name, {}, OP_ARGS arguments)                                                                      \
        if (g_glFuncs.name != nullptr) g_glFuncs.name arguments;                                                      \
    }

RECORDED_PASSTHROUGH(glClear, (GLbitfield mask), (mask))
// glDrawElements lives in drawing.cpp: it is FPE-converted, not passthrough.
void glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type,
                  GLvoid* pixels) {
    if (!sfpewEnsureBackend() || g_glFuncs.glReadPixels == nullptr) return;
    flushPendingImmediateDraws();
    // Desktop apps read GL_BGRA, which GLES3 core does not offer: read RGBA
    // and swap in place (tight rows, the common screenshot/AWT case).
    if (format == GL_BGRA && type == GL_UNSIGNED_BYTE && pixels != nullptr && width > 0 && height > 0 &&
        !sfpewPackPboBound()) {
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
    flushPendingImmediateDraws();
    if (g_glFuncs.glUseProgram == nullptr) return;
    g_glFuncs.glUseProgram(program);

    // Use the backend's post-call value so an invalid/unlinked program cannot
    // poison the logical shadow. This query happens only on glUseProgram,
    // replacing the per-draw GL_CURRENT_PROGRAM query on the hot path.
    auto& state = getLogicalProgramState();
    g_glFuncs.glGetIntegerv(GL_CURRENT_PROGRAM, &state.program);
    state.known = true;
}

RECORDED_PASSTHROUGH(glBlendColor, (GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha),
                    (red, green, blue, alpha))
RECORDED_PASSTHROUGH(glBlendEquation, (GLenum mode), (mode))
RECORDED_PASSTHROUGH(glBlendEquationSeparate, (GLenum modeRGB, GLenum modeAlpha),
                    (modeRGB, modeAlpha))
RECORDED_PASSTHROUGH(glBlendFunc, (GLenum sfactor, GLenum dfactor), (sfactor, dfactor))
RECORDED_PASSTHROUGH(glBlendFuncSeparate,
                    (GLenum sfactorRGB, GLenum dfactorRGB, GLenum sfactorAlpha, GLenum dfactorAlpha),
                    (sfactorRGB, dfactorRGB, sfactorAlpha, dfactorAlpha))
RECORDED_PASSTHROUGH(glDepthFunc, (GLenum func), (func))
RECORDED_PASSTHROUGH(glDepthMask, (GLboolean flag), (flag))
RECORDED_PASSTHROUGH(glColorMask,
                    (GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha),
                    (red, green, blue, alpha))
RECORDED_PASSTHROUGH(glCullFace, (GLenum mode), (mode))
RECORDED_PASSTHROUGH(glFrontFace, (GLenum mode), (mode))
RECORDED_PASSTHROUGH(glViewport, (GLint x, GLint y, GLsizei width, GLsizei height),
                    (x, y, width, height))
RECORDED_PASSTHROUGH(glScissor, (GLint x, GLint y, GLsizei width, GLsizei height),
                    (x, y, width, height))
RECORDED_PASSTHROUGH(glPolygonOffset, (GLfloat factor, GLfloat units), (factor, units))
RECORDED_PASSTHROUGH(glLineWidth, (GLfloat width), (width))
RECORDED_PASSTHROUGH(glStencilFunc, (GLenum func, GLint ref, GLuint mask), (func, ref, mask))
RECORDED_PASSTHROUGH(glStencilMask, (GLuint mask), (mask))
RECORDED_PASSTHROUGH(glStencilOp, (GLenum fail, GLenum zfail, GLenum zpass),
                    (fail, zfail, zpass))

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
    flushPendingImmediateDraws();
    LIST_RECORD(glTexParameterf, {}, target, pname, param)
    if (sfpewHandleGenerateMipmapParam(target, pname, (GLint)param)) return;
    if (g_glFuncs.glTexParameterf == nullptr) return;
    if (isTextureWrapParameter(pname) && static_cast<GLint>(param) == GL_CLAMP)
        param = static_cast<GLfloat>(GL_CLAMP_TO_EDGE);
    g_glFuncs.glTexParameterf(target, pname, param);
}

void glTexParameterfv(GLenum target, GLenum pname, const GLfloat* params) {
    flushPendingImmediateDraws();
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
    flushPendingImmediateDraws();
    LIST_RECORD(glTexParameteri, {}, target, pname, param)
    if (sfpewHandleGenerateMipmapParam(target, pname, param)) return;
    if (g_glFuncs.glTexParameteri != nullptr)
        g_glFuncs.glTexParameteri(target, pname, compatibleTextureParameter(pname, param));
}

void glTexParameteriv(GLenum target, GLenum pname, const GLint* params) {
    flushPendingImmediateDraws();
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
    (void)g_glstate; // entry strict resolve; mipmap tracking reads the binding shadow
    flushPendingImmediateDraws();
    // Legacy formats must match the RED/RG storage glTexImage2D allocated
    // for them; BGRA is swapped on the CPU (tight rows assumed, mirroring
    // the allocation path in getter.cpp).
    if (format == GL_ALPHA || format == GL_LUMINANCE) {
        format = GL_RED;
    } else if (format == GL_LUMINANCE_ALPHA) {
        format = GL_RG;
    } else if (format == GL_BGRA && pixels != nullptr && !sfpewUnpackPboBound() &&
               (type == GL_UNSIGNED_BYTE || type == GL_UNSIGNED_INT_8_8_8_8 ||
                type == GL_UNSIGNED_INT_8_8_8_8_REV)) {
        thread_local std::vector<uint8_t> scratch;
        const size_t count = (size_t)width * (size_t)height * 4u;
        scratch.resize(count);
        const auto* src = static_cast<const uint8_t*>(pixels);
        for (size_t px = 0; px < count; px += 4) {
            scratch[px + 0] = src[px + 2];
            scratch[px + 1] = src[px + 1];
            scratch[px + 2] = src[px + 0];
            scratch[px + 3] = src[px + 3];
        }
        pixels = scratch.data();
        format = GL_RGBA;
        type = GL_UNSIGNED_BYTE;
    }
    g_glFuncs.glTexSubImage2D(target, level, xoffset, yoffset, width, height, format, type, pixels);
    sfpewMaybeGenerateMipmap(target);
}

// --- Desktop-only entry points GLES lacks (plans/03, 3.4) ---------------

void glDepthRange(GLdouble nearVal, GLdouble farVal) {
    if (!sfpewEnsureBackend() || g_glFuncs.glDepthRangef == nullptr) return;
    flushPendingImmediateDraws();
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
        flushPendingImmediateDraws();
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
        flushPendingImmediateDraws();
        g_glFuncs.glPixelStorei(pname, param);
        return;
    }
}

void glPixelStoref(GLenum pname, GLfloat param) { glPixelStorei(pname, static_cast<GLint>(param)); }

#undef ORDERED_PASSTHROUGH
