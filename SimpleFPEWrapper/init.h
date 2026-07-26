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

SFPEW_APIENTRY GLenum glGetError();
SFPEW_APIENTRY GLuint glCreateShader(GLenum type);
SFPEW_APIENTRY void glDeleteShader(GLuint shader);
SFPEW_APIENTRY void glShaderSource(GLuint shader, GLsizei count, const GLchar* const* string, const GLint* length);
SFPEW_APIENTRY void glCompileShader(GLuint shader);
SFPEW_APIENTRY void glGetShaderSource(GLuint shader, GLsizei bufSize, GLsizei* length, GLchar* source);
SFPEW_APIENTRY void glGetShaderInfoLog(GLuint shader, GLsizei bufSize, GLsizei* length, GLchar* infoLog);
extern "C" {
GLuint sfpewCreateShaderObjectARB(GLenum type);
GLuint sfpewCreateProgramObjectARB(void);
void sfpewDeleteObjectARB(GLuint object);
void sfpewAttachObjectARB(GLuint program, GLuint shader);
void sfpewDetachObjectARB(GLuint program, GLuint shader);
void sfpewGetObjectParameterivARB(GLuint object, GLenum pname, GLint* params);
void sfpewGetInfoLogARB(GLuint object, GLsizei maxLength, GLsizei* length, GLchar* infoLog);
GLuint sfpewGetHandleARB(GLenum pname);
}
SFPEW_APIENTRY void glWindowPos2d(GLdouble x, GLdouble y);
SFPEW_APIENTRY void glWindowPos2f(GLfloat x, GLfloat y);
SFPEW_APIENTRY void glWindowPos2i(GLint x, GLint y);
SFPEW_APIENTRY void glWindowPos2s(GLshort x, GLshort y);
SFPEW_APIENTRY void glWindowPos3d(GLdouble x, GLdouble y, GLdouble z);
SFPEW_APIENTRY void glWindowPos3f(GLfloat x, GLfloat y, GLfloat z);
SFPEW_APIENTRY void glWindowPos3i(GLint x, GLint y, GLint z);
SFPEW_APIENTRY void glWindowPos3s(GLshort x, GLshort y, GLshort z);
SFPEW_APIENTRY void glWindowPos2dv(const GLdouble* v);
SFPEW_APIENTRY void glWindowPos2fv(const GLfloat* v);
SFPEW_APIENTRY void glWindowPos2iv(const GLint* v);
SFPEW_APIENTRY void glWindowPos2sv(const GLshort* v);
SFPEW_APIENTRY void glWindowPos3dv(const GLdouble* v);
SFPEW_APIENTRY void glWindowPos3fv(const GLfloat* v);
SFPEW_APIENTRY void glWindowPos3iv(const GLint* v);
SFPEW_APIENTRY void glWindowPos3sv(const GLshort* v);

SFPEW_APIENTRY void glSecondaryColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid* pointer);
SFPEW_APIENTRY void glFogCoordPointer(GLenum type, GLsizei stride, const GLvoid* pointer);
SFPEW_APIENTRY GLboolean glIsEnabled(GLenum cap);
SFPEW_APIENTRY void glGetBooleanv(GLenum pname, GLboolean* params);
SFPEW_APIENTRY void glGetDoublev(GLenum pname, GLdouble* params);
SFPEW_APIENTRY void glGetLightfv(GLenum light, GLenum pname, GLfloat* params);
SFPEW_APIENTRY void glGetLightiv(GLenum light, GLenum pname, GLint* params);
SFPEW_APIENTRY void glGetMaterialfv(GLenum face, GLenum pname, GLfloat* params);
SFPEW_APIENTRY void glGetMaterialiv(GLenum face, GLenum pname, GLint* params);
SFPEW_APIENTRY void glGetTexEnvfv(GLenum target, GLenum pname, GLfloat* params);
SFPEW_APIENTRY void glGetTexEnviv(GLenum target, GLenum pname, GLint* params);
SFPEW_APIENTRY void glGetTexGenfv(GLenum coord, GLenum pname, GLfloat* params);
SFPEW_APIENTRY void glGetTexGeniv(GLenum coord, GLenum pname, GLint* params);
SFPEW_APIENTRY void glGetTexGendv(GLenum coord, GLenum pname, GLdouble* params);
SFPEW_APIENTRY void glTexImage1D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLint border,
                                 GLenum format, GLenum type, const GLvoid* pixels);
SFPEW_APIENTRY void glTexSubImage1D(GLenum target, GLint level, GLint xoffset, GLsizei width, GLenum format,
                                    GLenum type, const GLvoid* pixels);
SFPEW_APIENTRY void glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width,
                                    GLsizei height, GLenum format, GLenum type, const GLvoid* pixels);
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
