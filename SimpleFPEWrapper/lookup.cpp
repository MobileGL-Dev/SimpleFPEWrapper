// SimpleFPEWrapper - SimpleFPEWrapper/lookup.cpp
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

#include "EGL/egl.h"
#include "init.h"
#include "fpe/transformation.h"

#define GETPROC(name, var)                                                                                             \
    if (std::strcmp(#name, var) == 0) {                                                                                \
        return (__eglMustCastToProperFunctionPointerType)name;                                                         \
    }

SFPEW_APIENTRY __eglMustCastToProperFunctionPointerType eglGetProcAddress(const char* name) {
    if (!name) return nullptr;

    GETPROC(glGetString, name)
    GETPROC(glGetStringi, name)
    GETPROC(glGetIntegerv, name)
    GETPROC(glDrawArrays, name)
    GETPROC(glDrawElements, name)
    GETPROC(glClear, name)
    GETPROC(glReadPixels, name)
    GETPROC(glFlush, name)
    GETPROC(glFinish, name)
    GETPROC(glBindFramebuffer, name)
    if (std::strcmp("glBindFramebufferEXT", name) == 0) {
        return (__eglMustCastToProperFunctionPointerType)glBindFramebuffer;
    }
    GETPROC(glUseProgram, name)
    GETPROC(glBlendColor, name)
    GETPROC(glBlendEquation, name)
    GETPROC(glBlendEquationSeparate, name)
    GETPROC(glBlendFunc, name)
    GETPROC(glBlendFuncSeparate, name)
    GETPROC(glDepthFunc, name)
    GETPROC(glDepthMask, name)
    GETPROC(glColorMask, name)
    GETPROC(glCullFace, name)
    GETPROC(glFrontFace, name)
    GETPROC(glViewport, name)
    GETPROC(glScissor, name)
    GETPROC(glPolygonOffset, name)
    GETPROC(glLineWidth, name)
    GETPROC(glStencilFunc, name)
    GETPROC(glStencilMask, name)
    GETPROC(glStencilOp, name)
    GETPROC(glTexParameterf, name)
    GETPROC(glTexParameterfv, name)
    GETPROC(glTexParameteri, name)
    GETPROC(glTexParameteriv, name)
    GETPROC(glTexSubImage2D, name)
    GETPROC(glBindBuffer, name)
    GETPROC(glDeleteBuffers, name)
    if (std::strcmp("glBindBufferARB", name) == 0) {
        return (__eglMustCastToProperFunctionPointerType)glBindBuffer;
    }
    if (std::strcmp("glDeleteBuffersARB", name) == 0) {
        return (__eglMustCastToProperFunctionPointerType)glDeleteBuffers;
    }
    GETPROC(glActiveTexture, name)
    GETPROC(glBindTexture, name)
    GETPROC(glDeleteTextures, name)
    GETPROC(glTexImage2D, name)
    GETPROC(glGetTexLevelParameteriv, name)
    GETPROC(glGetTexLevelParameterfv, name)

    GETPROC(glBegin, name)
    GETPROC(glEnd, name)
    GETPROC(glVertex2d, name)
    GETPROC(glVertex2f, name)
    GETPROC(glVertex2i, name)
    GETPROC(glVertex2s, name)
    GETPROC(glVertex3d, name)
    GETPROC(glVertex3f, name)
    GETPROC(glVertex3i, name)
    GETPROC(glVertex3s, name)
    GETPROC(glVertex4d, name)
    GETPROC(glVertex4f, name)
    GETPROC(glVertex4i, name)
    GETPROC(glVertex4s, name)
    GETPROC(glVertex2dv, name)
    GETPROC(glVertex2fv, name)
    GETPROC(glVertex2iv, name)
    GETPROC(glVertex2sv, name)
    GETPROC(glVertex3dv, name)
    GETPROC(glVertex3fv, name)
    GETPROC(glVertex3iv, name)
    GETPROC(glVertex3sv, name)
    GETPROC(glVertex4dv, name)
    GETPROC(glVertex4fv, name)
    GETPROC(glVertex4iv, name)
    GETPROC(glVertex4sv, name)
    GETPROC(glNormal3b, name)
    GETPROC(glNormal3d, name)
    GETPROC(glNormal3f, name)
    GETPROC(glNormal3i, name)
    GETPROC(glNormal3s, name)
    GETPROC(glNormal3bv, name)
    GETPROC(glNormal3dv, name)
    GETPROC(glNormal3fv, name)
    GETPROC(glNormal3iv, name)
    GETPROC(glNormal3sv, name)
    GETPROC(glColor3b, name)
    GETPROC(glColor3d, name)
    GETPROC(glColor3f, name)
    GETPROC(glColor3i, name)
    GETPROC(glColor3s, name)
    GETPROC(glColor3ub, name)
    GETPROC(glColor3ui, name)
    GETPROC(glColor3us, name)
    GETPROC(glColor4b, name)
    GETPROC(glColor4d, name)
    GETPROC(glColor4f, name)
    GETPROC(glColor4i, name)
    GETPROC(glColor4s, name)
    GETPROC(glColor4ub, name)
    GETPROC(glColor4ui, name)
    GETPROC(glColor4us, name)
    GETPROC(glColor3bv, name)
    GETPROC(glColor3dv, name)
    GETPROC(glColor3fv, name)
    GETPROC(glColor3iv, name)
    GETPROC(glColor3sv, name)
    GETPROC(glColor3ubv, name)
    GETPROC(glColor3uiv, name)
    GETPROC(glColor3usv, name)
    GETPROC(glColor4bv, name)
    GETPROC(glColor4dv, name)
    GETPROC(glColor4fv, name)
    GETPROC(glColor4iv, name)
    GETPROC(glColor4sv, name)
    GETPROC(glColor4ubv, name)
    GETPROC(glColor4uiv, name)
    GETPROC(glColor4usv, name)
    GETPROC(glTexCoord2f, name)
    GETPROC(glTexCoord4f, name)
    GETPROC(glMultiTexCoord2f, name)
    GETPROC(glMultiTexCoord4f, name)
    if (std::strcmp("glMultiTexCoord2fARB", name) == 0) {
        return (__eglMustCastToProperFunctionPointerType)glMultiTexCoord2f;
    }
    GETPROC(glGenLists, name)
    GETPROC(glDeleteLists, name)
    GETPROC(glIsList, name)
    GETPROC(glNewList, name)
    GETPROC(glEndList, name)
    GETPROC(glCallList, name)
    GETPROC(glCallLists, name)
    GETPROC(glListBase, name)
    GETPROC(glEnable, name)
    GETPROC(glDisable, name)
    GETPROC(glClientActiveTexture, name)
    if (std::strcmp("glClientActiveTextureARB", name) == 0) {
        return (__eglMustCastToProperFunctionPointerType)glClientActiveTexture;
    }
    if (std::strcmp("glActiveTextureARB", name) == 0) {
        return (__eglMustCastToProperFunctionPointerType)glActiveTexture;
    }
    GETPROC(glAlphaFunc, name)
    GETPROC(glFogf, name)
    GETPROC(glFogi, name)
    GETPROC(glFogfv, name)
    GETPROC(glFogiv, name)
    GETPROC(glShadeModel, name)
    GETPROC(glLightf, name)
    GETPROC(glLighti, name)
    GETPROC(glLightfv, name)
    GETPROC(glLightiv, name)
    GETPROC(glLightModelf, name)
    GETPROC(glLightModeli, name)
    GETPROC(glLightModelfv, name)
    GETPROC(glLightModeliv, name)
    GETPROC(glColorMaterial, name)
    GETPROC(glMaterialf, name)
    GETPROC(glMateriali, name)
    GETPROC(glMaterialfv, name)
    GETPROC(glMaterialiv, name)
    GETPROC(glTexEnvf, name)
    GETPROC(glTexEnvi, name)
    GETPROC(glTexEnvfv, name)
    GETPROC(glTexEnviv, name)
    GETPROC(glMatrixMode, name)
    GETPROC(glLoadIdentity, name)
    GETPROC(glLoadMatrixd, name)
    GETPROC(glLoadMatrixf, name)
    GETPROC(glOrtho, name)
    GETPROC(glOrthof, name)
    GETPROC(glFrustum, name)
    GETPROC(glFrustumf, name)
    GETPROC(glScalef, name)
    GETPROC(glTranslatef, name)
    GETPROC(glRotatef, name)
    GETPROC(glMultMatrixd, name)
    GETPROC(glMultMatrixf, name)
    GETPROC(glRotated, name)
    GETPROC(glScaled, name)
    GETPROC(glTranslated, name)
    GETPROC(glPushMatrix, name)
    GETPROC(glPopMatrix, name)
    GETPROC(glVertexPointer, name)
    GETPROC(glNormalPointer, name)
    GETPROC(glColorPointer, name)
    GETPROC(glTexCoordPointer, name)
    GETPROC(glIndexPointer, name)
    GETPROC(glEnableClientState, name)
    GETPROC(glDisableClientState, name)
    GETPROC(glGetFloatv, name)

    __eglMustCastToProperFunctionPointerType ptr = g_eglFuncs.eglGetProcAddress(name);
    if (!ptr) {
        printf("eglGetProcAddress: eglGetProcAddress also failed to find '%s'\n", name);
    }
    return ptr;
}

SFPEW_APIENTRY void* glXGetProcAddress(const char* name) {
    return (void*)eglGetProcAddress(name);
}

SFPEW_APIENTRY void* glXGetProcAddressARB(const char* name) {
    return glXGetProcAddress(name);
}
