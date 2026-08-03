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
// True when the backend is OpenGL ES rather than desktop GL. Cached after the
// first successful query, so it is safe on any hot path.
//
// It decides how BGRA pixel data is handled: desktop GL takes GL_BGRA
// directly, GLES has no such format in core and none of its BGRA extensions
// cover the paths that matter here, so the wrapper converts on the CPU.
bool sfpewBackendIsES();
// True when the backend takes GL_BGRA natively: desktop GL, excluding
// GLES-backed translation layers (MobileGlues) that only pretend to.
// Conservative: unknown answers "no", and the wrapper converts.
bool sfpewBackendTakesBgra();
// True when glMultiDrawArrays must not be called on this backend: MobileGlues
// up to V1.3.5 has no implementation and the call silently draws nothing.
bool sfpewBackendLacksMultiDrawArrays();

// A GL_BGRA upload converted for a GLES backend: `pixels` is tightly packed
// RGBA (thread-local scratch), and the unpack state / pixel unpack buffer
// that had to be neutralized for the tight result is recorded here so
// sfpewFinishBgraUpload can restore it after the backend call.
struct sfpew_bgra_upload_t {
    const void* pixels = nullptr;
    GLuint rebind_pbo = 0;
    GLint row_length = 0;
    GLint skip_rows = 0;
    GLint skip_pixels = 0;
};
bool sfpewPrepareBgraUpload(GLsizei width, GLsizei height, GLenum type, const void* pixels,
                            sfpew_bgra_upload_t* out);
void sfpewFinishBgraUpload(const sfpew_bgra_upload_t& upload);

// Display-list geometry accounting: a per-frame tally of every way chunk
// geometry can go missing, plus switches that take the batched replay apart
// one path at a time. It exists for device investigations and is compiled
// out of a normal build - cmake -DSFPEW_LIST_DEBUG=ON puts it in, and even
// then nothing happens until SFPEW_LISTLOG is set in the environment. What
// stays in every build is the handful of one-time notices at startup: the
// backend's BGRA and multi-draw decisions, and a refused arena reservation.
#ifndef SFPEW_LIST_DEBUG
#define SFPEW_LIST_DEBUG 0
#endif

#if SFPEW_LIST_DEBUG
void sfpewListLogFrame();
void sfpewListLogCompiled(GLuint list, size_t commands);
void sfpewListLogCalled(GLuint list, int found, size_t commands);
void sfpewListLogRequested(unsigned lists);
void sfpewListLogRequestedIds(const GLuint* ids, unsigned count);
void sfpewListLogGenerated(unsigned range);
void sfpewListLogDeleted(unsigned range);
void sfpewListLogRecording(GLuint list);
void sfpewListLogSingleCall();
void sfpewListLogDrawIssued();
void sfpewListLogDrewOne();
void sfpewListLogMatrixQuery(const char* which, unsigned slot, size_t stackDepth,
                             const GLfloat* matrix);
#else
inline void sfpewListLogFrame() {}
inline void sfpewListLogCompiled(GLuint, size_t) {}
inline void sfpewListLogCalled(GLuint, int, size_t) {}
inline void sfpewListLogRequested(unsigned) {}
inline void sfpewListLogRequestedIds(const GLuint*, unsigned) {}
inline void sfpewListLogGenerated(unsigned) {}
inline void sfpewListLogDeleted(unsigned) {}
inline void sfpewListLogRecording(GLuint) {}
inline void sfpewListLogSingleCall() {}
inline void sfpewListLogDrawIssued() {}
inline void sfpewListLogDrewOne() {}
inline void sfpewListLogMatrixQuery(const char*, unsigned, size_t, const GLfloat*) {}
#endif

// Fixed-function state that lives inside one translation unit, reachable
// only so the glGet family can answer for it. Each fills `values` in the
// order the spec lists and reports how many it wrote; false means the pname
// is not theirs.
bool sfpewEvaluatorStateQuery(GLenum pname, GLdouble* values, int* count);
void sfpewAccumClearValue(GLfloat* rgba);
void sfpewAttribStackDepths(int* server, int* client, int* maxDepth);
// Slot in glstate_t::legacy_hints for a hint target one of the two backend
// floors cannot take, or -1.
int sfpewLegacyHintSlot(GLenum target);
// True when the backend is GLES, reporting the desktop level the wrapper
// presents for it; false for a desktop backend, which needs no mapping.
// The two differ in which legacy queries they still accept, so anything
// that has to pick a spelling asks here.
bool sfpewDesktopGLVersion(int* major, int* minor);

GLuint sfpewLogicalTextureBinding(GLenum target);
// Changes whenever the active texture unit or any texture binding does.
uint64_t sfpewTextureStateGeneration();
GLint sfpewLogicalProgram();
GLuint sfpewLogicalArrayBufferBinding();
GLuint sfpewLogicalVertexArrayBinding();
SFPEW_APIENTRY void glBindVertexArray(GLuint array);
SFPEW_APIENTRY void glDeleteVertexArrays(GLsizei n, const GLuint* arrays);
GLuint sfpewLogicalElementArrayBufferBinding();
GLuint sfpewLogicalVAOBinding();
// True when a pixel pack/unpack buffer is bound: CPU pixel conversions
// must pass through untouched then (plans/10 10.1).
bool sfpewUnpackPboBound();
bool sfpewPackPboBound();
void sfpewSetGenerateMipmap(GLenum target, GLuint texture, bool enable);
void sfpewMaybeGenerateMipmap(GLenum target);
void sfpewRememberTextureSize(GLuint texture, GLsizei width, GLsizei height);
SFPEW_APIENTRY void glGetTexImage(GLenum target, GLint level, GLenum format, GLenum type, GLvoid* pixels);
SFPEW_APIENTRY void glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLvoid* pixels);

SFPEW_APIENTRY GLenum glGetError();
SFPEW_APIENTRY GLuint glCreateShader(GLenum type);
SFPEW_APIENTRY void glDeleteShader(GLuint shader);
SFPEW_APIENTRY void glShaderSource(GLuint shader, GLsizei count, const GLchar* const* string, const GLint* length);
SFPEW_APIENTRY void glCompileShader(GLuint shader);
SFPEW_APIENTRY void glGetShaderSource(GLuint shader, GLsizei bufSize, GLsizei* length, GLchar* source);
SFPEW_APIENTRY void glGetShaderInfoLog(GLuint shader, GLsizei bufSize, GLsizei* length, GLchar* infoLog);
SFPEW_APIENTRY void glLinkProgram(GLuint program);
SFPEW_APIENTRY void glAttachShader(GLuint program, GLuint shader);
SFPEW_APIENTRY void glDetachShader(GLuint program, GLuint shader);
SFPEW_APIENTRY void glDeleteProgram(GLuint program);
SFPEW_APIENTRY void glGetShaderiv(GLuint shader, GLenum pname, GLint* params);
SFPEW_APIENTRY void glGetProgramiv(GLuint program, GLenum pname, GLint* params);
SFPEW_APIENTRY void glGetAttachedShaders(GLuint program, GLsizei maxCount, GLsizei* count, GLuint* shaders);
SFPEW_APIENTRY void glGetProgramInfoLog(GLuint program, GLsizei bufSize, GLsizei* length, GLchar* infoLog);
SFPEW_APIENTRY GLint glGetUniformLocation(GLuint program, const GLchar* name);
SFPEW_APIENTRY GLint glGetAttribLocation(GLuint program, const GLchar* name);
SFPEW_APIENTRY void glRenderbufferStorage(GLenum target, GLenum internalformat, GLsizei width, GLsizei height);
SFPEW_APIENTRY void glRenderbufferStorageMultisample(GLenum target, GLsizei samples, GLenum internalformat,
                                                     GLsizei width, GLsizei height);
SFPEW_APIENTRY void glFramebufferTexture1D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture,
                                           GLint level);
SFPEW_APIENTRY void glFramebufferTexture3D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture,
                                           GLint level, GLint zoffset);
SFPEW_APIENTRY void* glMapBuffer(GLenum target, GLenum access);
SFPEW_APIENTRY void glGetBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, void* data);
void sfpewForgetUserProgram(GLuint program);
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
SFPEW_APIENTRY void glFogCoordf(GLfloat coord);
SFPEW_APIENTRY void glFogCoordd(GLdouble coord);
SFPEW_APIENTRY void glFogCoordfv(const GLfloat* coord);
SFPEW_APIENTRY void glFogCoorddv(const GLdouble* coord);
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
SFPEW_APIENTRY void glDrawRangeElements(GLenum mode, GLuint start, GLuint end, GLsizei count,
                                        GLenum type, const GLvoid* indices);
SFPEW_APIENTRY void glMultiDrawArrays(GLenum mode, const GLint* first, const GLsizei* count,
                                      GLsizei drawcount);
SFPEW_APIENTRY void glMultiDrawElements(GLenum mode, const GLsizei* count, GLenum type,
                                        const GLvoid* const* indices, GLsizei drawcount);
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
SFPEW_APIENTRY void glGetTexParameterfv(GLenum target, GLenum pname, GLfloat* params);
SFPEW_APIENTRY void glGetTexParameteriv(GLenum target, GLenum pname, GLint* params);
SFPEW_APIENTRY GLboolean glAreTexturesResident(GLsizei n, const GLuint* textures,
                                                GLboolean* residences);
SFPEW_APIENTRY void glPrioritizeTextures(GLsizei n, const GLuint* textures,
                                         const GLclampf* priorities);
SFPEW_APIENTRY void glGetTexLevelParameteriv(GLenum target, GLint level, GLenum pname, GLint* params);
SFPEW_APIENTRY void glGenerateMipmap(GLenum target);
SFPEW_APIENTRY void glGetTexLevelParameterfv(GLenum target, GLint level, GLenum pname, GLfloat* params);
SFPEW_APIENTRY void glLoadTransposeMatrixf(const GLfloat* m);
SFPEW_APIENTRY void glLoadTransposeMatrixd(const GLdouble* m);
SFPEW_APIENTRY void glMultTransposeMatrixf(const GLfloat* m);
SFPEW_APIENTRY void glMultTransposeMatrixd(const GLdouble* m);
// The current EGL context for this thread. Exact (and free) once the app has
// called eglMakeCurrent through the wrapper; otherwise resolved by asking
// libEGL, which on some loaders costs a syscall per query.
// Buffer / vertex-attribute surface. Wrapped so the wrapper can observe reads
// and writes through the bound GL_ARRAY_BUFFER and VAO (plans/12); otherwise
// eglGetProcAddress hands the app the backend's own pointer and these become
// invisible.
SFPEW_APIENTRY void glBufferData(GLenum target, GLsizeiptr size, const void* data, GLenum usage);
SFPEW_APIENTRY void glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size,
                                    const void* data);
SFPEW_APIENTRY void* glMapBufferRange(GLenum target, GLintptr offset, GLsizeiptr length,
                                      GLbitfield access);
SFPEW_APIENTRY GLboolean glUnmapBuffer(GLenum target);
SFPEW_APIENTRY void glGetBufferParameteriv(GLenum target, GLenum pname, GLint* params);
SFPEW_APIENTRY void glVertexAttribPointer(GLuint index, GLint size, GLenum type,
                                          GLboolean normalized, GLsizei stride,
                                          const void* pointer);
SFPEW_APIENTRY void glVertexAttribIPointer(GLuint index, GLint size, GLenum type, GLsizei stride,
                                           const void* pointer);
SFPEW_APIENTRY void glEnableVertexAttribArray(GLuint index);
SFPEW_APIENTRY void glDisableVertexAttribArray(GLuint index);

EGLContext sfpewCurrentContext();
void sfpewNoteCurrentContext(EGLContext context);
EGLBoolean sfpewEglMakeCurrent(EGLDisplay dpy, EGLSurface draw, EGLSurface read, EGLContext ctx);
EGLBoolean sfpewEglSwapBuffers(EGLDisplay dpy, EGLSurface surface);
SFPEW_APIENTRY bool sfpewImmediateBatchPendingForTest();
SFPEW_APIENTRY void sfpewMarkBufferInternalForTest(GLuint buffer);
SFPEW_APIENTRY bool sfpewBufferIsInternalForTest(GLuint buffer);
SFPEW_APIENTRY GLuint sfpewLogicalArrayBufferBindingForTest(void);
EGLBoolean sfpewEglSwapBuffersWithDamageEXT(EGLDisplay dpy, EGLSurface surface, EGLint* rects,
                                            EGLint n_rects);
SFPEW_APIENTRY EGLBoolean eglMakeCurrent(EGLDisplay dpy, EGLSurface draw, EGLSurface read,
                                        EGLContext ctx);
