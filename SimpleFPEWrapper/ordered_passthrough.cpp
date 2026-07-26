// SimpleFPEWrapper - SimpleFPEWrapper/ordered_passthrough.cpp
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

#include "init.h"
#include "fpe/drawing1x.h"

#define ORDERED_PASSTHROUGH(name, declaration, arguments)                                                             \
    void name declaration {                                                                                           \
        flushPendingImmediateDraws();                                                                                 \
        if (g_glFuncs.name != nullptr) g_glFuncs.name arguments;                                                      \
    }

ORDERED_PASSTHROUGH(glClear, (GLbitfield mask), (mask))
ORDERED_PASSTHROUGH(glDrawElements, (GLenum mode, GLsizei count, GLenum type, const GLvoid* indices),
                    (mode, count, type, indices))
ORDERED_PASSTHROUGH(glReadPixels,
                    (GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type,
                     GLvoid* pixels),
                    (x, y, width, height, format, type, pixels))
ORDERED_PASSTHROUGH(glFlush, (), ())
ORDERED_PASSTHROUGH(glFinish, (), ())

ORDERED_PASSTHROUGH(glBindFramebuffer, (GLenum target, GLuint framebuffer), (target, framebuffer))
ORDERED_PASSTHROUGH(glUseProgram, (GLuint program), (program))

ORDERED_PASSTHROUGH(glBlendColor, (GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha),
                    (red, green, blue, alpha))
ORDERED_PASSTHROUGH(glBlendEquation, (GLenum mode), (mode))
ORDERED_PASSTHROUGH(glBlendEquationSeparate, (GLenum modeRGB, GLenum modeAlpha),
                    (modeRGB, modeAlpha))
ORDERED_PASSTHROUGH(glBlendFunc, (GLenum sfactor, GLenum dfactor), (sfactor, dfactor))
ORDERED_PASSTHROUGH(glBlendFuncSeparate,
                    (GLenum sfactorRGB, GLenum dfactorRGB, GLenum sfactorAlpha, GLenum dfactorAlpha),
                    (sfactorRGB, dfactorRGB, sfactorAlpha, dfactorAlpha))
ORDERED_PASSTHROUGH(glDepthFunc, (GLenum func), (func))
ORDERED_PASSTHROUGH(glDepthMask, (GLboolean flag), (flag))
ORDERED_PASSTHROUGH(glColorMask,
                    (GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha),
                    (red, green, blue, alpha))
ORDERED_PASSTHROUGH(glCullFace, (GLenum mode), (mode))
ORDERED_PASSTHROUGH(glFrontFace, (GLenum mode), (mode))
ORDERED_PASSTHROUGH(glViewport, (GLint x, GLint y, GLsizei width, GLsizei height),
                    (x, y, width, height))
ORDERED_PASSTHROUGH(glScissor, (GLint x, GLint y, GLsizei width, GLsizei height),
                    (x, y, width, height))
ORDERED_PASSTHROUGH(glPolygonOffset, (GLfloat factor, GLfloat units), (factor, units))
ORDERED_PASSTHROUGH(glLineWidth, (GLfloat width), (width))
ORDERED_PASSTHROUGH(glStencilFunc, (GLenum func, GLint ref, GLuint mask), (func, ref, mask))
ORDERED_PASSTHROUGH(glStencilMask, (GLuint mask), (mask))
ORDERED_PASSTHROUGH(glStencilOp, (GLenum fail, GLenum zfail, GLenum zpass),
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

} // namespace

void glTexParameterf(GLenum target, GLenum pname, GLfloat param) {
    flushPendingImmediateDraws();
    if (g_glFuncs.glTexParameterf == nullptr) return;
    if (isTextureWrapParameter(pname) && static_cast<GLint>(param) == GL_CLAMP)
        param = static_cast<GLfloat>(GL_CLAMP_TO_EDGE);
    g_glFuncs.glTexParameterf(target, pname, param);
}

void glTexParameterfv(GLenum target, GLenum pname, const GLfloat* params) {
    flushPendingImmediateDraws();
    if (g_glFuncs.glTexParameterfv == nullptr || params == nullptr) return;
    if (isTextureWrapParameter(pname) && static_cast<GLint>(params[0]) == GL_CLAMP) {
        const GLfloat compatible = static_cast<GLfloat>(GL_CLAMP_TO_EDGE);
        g_glFuncs.glTexParameterfv(target, pname, &compatible);
    } else {
        g_glFuncs.glTexParameterfv(target, pname, params);
    }
}

void glTexParameteri(GLenum target, GLenum pname, GLint param) {
    flushPendingImmediateDraws();
    if (g_glFuncs.glTexParameteri != nullptr)
        g_glFuncs.glTexParameteri(target, pname, compatibleTextureParameter(pname, param));
}

void glTexParameteriv(GLenum target, GLenum pname, const GLint* params) {
    flushPendingImmediateDraws();
    if (g_glFuncs.glTexParameteriv == nullptr || params == nullptr) return;
    if (isTextureWrapParameter(pname) && params[0] == GL_CLAMP) {
        const GLint compatible = GL_CLAMP_TO_EDGE;
        g_glFuncs.glTexParameteriv(target, pname, &compatible);
    } else {
        g_glFuncs.glTexParameteriv(target, pname, params);
    }
}
ORDERED_PASSTHROUGH(glTexSubImage2D,
                    (GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width,
                     GLsizei height, GLenum format, GLenum type, const GLvoid* pixels),
                    (target, level, xoffset, yoffset, width, height, format, type, pixels))

#undef ORDERED_PASSTHROUGH
