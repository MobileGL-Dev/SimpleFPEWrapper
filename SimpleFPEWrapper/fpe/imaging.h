// SimpleFPEWrapper - SimpleFPEWrapper/fpe/imaging.h
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

#pragma once

#include "types.h"

// Runs the enabled GL_ARB_imaging stages over a tightly packed RGBA float
// image. Convolution may reduce the dimensions. False means every stage was
// disabled and the caller's existing fast path can remain untouched.
bool sfpewImagingTransfer(GLfloat* rgba, GLsizei* width, GLsizei* height);
bool sfpewImagingActive();
bool sfpewImagingSink();
bool sfpewImagingDecodePixels(GLsizei width, GLsizei height, GLenum format, GLenum type,
                              const GLvoid* pixels, std::vector<GLfloat>* rgba,
                              GLsizei* output_width, GLsizei* output_height);
bool sfpewImagingReadRgba(GLint x, GLint y, GLsizei width, GLsizei height,
                          std::vector<GLfloat>* rgba, GLsizei* output_width,
                          GLsizei* output_height);
bool sfpewImagingReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format,
                            GLenum type, GLvoid* pixels);

struct sfpew_imaging_upload_t {
    std::vector<GLfloat> rgba;
    GLsizei width = 0, height = 0;
    GLint alignment = 4, row_length = 0, skip_rows = 0, skip_pixels = 0, unpack_pbo = 0;
    bool unpack_swap_bytes = false;
    bool valid = false;
    bool sink = false;
    bool neutralized = false;
};

bool sfpewPrepareImagingUpload(GLsizei width, GLsizei height, GLenum format, GLenum type,
                               const GLvoid* pixels, sfpew_imaging_upload_t* upload);
bool sfpewBeginTightImagingUpload(sfpew_imaging_upload_t* upload);
void sfpewFinishImagingUpload(const sfpew_imaging_upload_t& upload);
void sfpewPushImagingBypass();
void sfpewPopImagingBypass();

#ifdef __cplusplus
extern "C" {
#endif

void glColorTable(GLenum target, GLenum internalformat, GLsizei width, GLenum format,
                  GLenum type, const GLvoid* table);
void glColorSubTable(GLenum target, GLsizei start, GLsizei count, GLenum format,
                     GLenum type, const GLvoid* data);
void glColorTableParameterfv(GLenum target, GLenum pname, const GLfloat* params);
void glColorTableParameteriv(GLenum target, GLenum pname, const GLint* params);
void glCopyColorTable(GLenum target, GLenum internalformat, GLint x, GLint y, GLsizei width);
void glCopyColorSubTable(GLenum target, GLsizei start, GLint x, GLint y, GLsizei width);
void glGetColorTable(GLenum target, GLenum format, GLenum type, GLvoid* table);
void glGetColorTableParameterfv(GLenum target, GLenum pname, GLfloat* params);
void glGetColorTableParameteriv(GLenum target, GLenum pname, GLint* params);

void glConvolutionFilter1D(GLenum target, GLenum internalformat, GLsizei width, GLenum format,
                           GLenum type, const GLvoid* image);
void glConvolutionFilter2D(GLenum target, GLenum internalformat, GLsizei width, GLsizei height,
                           GLenum format, GLenum type, const GLvoid* image);
void glConvolutionParameterf(GLenum target, GLenum pname, GLfloat param);
void glConvolutionParameterfv(GLenum target, GLenum pname, const GLfloat* params);
void glConvolutionParameteri(GLenum target, GLenum pname, GLint param);
void glConvolutionParameteriv(GLenum target, GLenum pname, const GLint* params);
void glCopyConvolutionFilter1D(GLenum target, GLenum internalformat, GLint x, GLint y,
                               GLsizei width);
void glCopyConvolutionFilter2D(GLenum target, GLenum internalformat, GLint x, GLint y,
                               GLsizei width, GLsizei height);
void glGetConvolutionFilter(GLenum target, GLenum format, GLenum type, GLvoid* image);
void glGetConvolutionParameterfv(GLenum target, GLenum pname, GLfloat* params);
void glGetConvolutionParameteriv(GLenum target, GLenum pname, GLint* params);
void glSeparableFilter2D(GLenum target, GLenum internalformat, GLsizei width, GLsizei height,
                         GLenum format, GLenum type, const GLvoid* row, const GLvoid* column);
void glGetSeparableFilter(GLenum target, GLenum format, GLenum type, GLvoid* row,
                          GLvoid* column, GLvoid* span);

void glHistogram(GLenum target, GLsizei width, GLenum internalformat, GLboolean sink);
void glGetHistogram(GLenum target, GLboolean reset, GLenum format, GLenum type, GLvoid* values);
void glGetHistogramParameterfv(GLenum target, GLenum pname, GLfloat* params);
void glGetHistogramParameteriv(GLenum target, GLenum pname, GLint* params);
void glResetHistogram(GLenum target);

void glMinmax(GLenum target, GLenum internalformat, GLboolean sink);
void glGetMinmax(GLenum target, GLboolean reset, GLenum format, GLenum type, GLvoid* values);
void glGetMinmaxParameterfv(GLenum target, GLenum pname, GLfloat* params);
void glGetMinmaxParameteriv(GLenum target, GLenum pname, GLint* params);
void glResetMinmax(GLenum target);

#ifdef __cplusplus
}
#endif
