// SimpleFPEWrapper - SimpleFPEWrapper/init.h
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

#pragma once
#include <string>
#include <cstring>
#include <stdexcept>
#include "backend/loader.h"

#define SFPEW_APIENTRY extern "C" __attribute__((visibility("default")))
extern SFPEW::External::EGLFunctionsTable g_eglFuncs;
extern SFPEW::External::BackendGLFunctionsTable g_glFuncs;

// Lazily resolves the EGL/GL backend tables on first use. Returns false if
// the backend is unavailable; callers must degrade to a no-op then. Never
// throws and never issues GL calls by itself.
bool sfpewEnsureBackend() noexcept;

GLenum sfpewLogicalActiveTexture();
GLuint sfpewLogicalTextureBinding(GLenum target);
GLint sfpewLogicalProgram();
GLuint sfpewLogicalArrayBufferBinding();

SFPEW_APIENTRY const GLubyte* glGetString(GLenum name);
SFPEW_APIENTRY const GLubyte* glGetStringi(GLenum name, GLuint index);
SFPEW_APIENTRY void glGetIntegerv(GLenum pname, GLint* params);
SFPEW_APIENTRY void glDrawArrays(GLenum mode, GLint first, GLsizei count);
SFPEW_APIENTRY void glBindBuffer(GLenum target, GLuint buffer);
SFPEW_APIENTRY void glDeleteBuffers(GLsizei n, const GLuint* buffers);
SFPEW_APIENTRY void glActiveTexture(GLenum texture);
SFPEW_APIENTRY void glBindTexture(GLenum target, GLuint texture);
SFPEW_APIENTRY void glDeleteTextures(GLsizei n, const GLuint* textures);
SFPEW_APIENTRY void glBindFramebuffer(GLenum target, GLuint framebuffer);
SFPEW_APIENTRY void glUseProgram(GLuint program);
SFPEW_APIENTRY void glBlendEquationSeparate(GLenum modeRGB, GLenum modeAlpha);
SFPEW_APIENTRY void glBlendFuncSeparate(GLenum sfactorRGB, GLenum dfactorRGB, GLenum sfactorAlpha,
                                       GLenum dfactorAlpha);
SFPEW_APIENTRY void glGetFloatv(GLenum pname, GLfloat* params);
SFPEW_APIENTRY void glTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height,
                                GLint border, GLenum format, GLenum type, const GLvoid* pixels);
SFPEW_APIENTRY void glGetTexLevelParameteriv(GLenum target, GLint level, GLenum pname, GLint* params);
SFPEW_APIENTRY void glGetTexLevelParameterfv(GLenum target, GLint level, GLenum pname, GLfloat* params);
