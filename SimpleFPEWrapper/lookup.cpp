// SimpleFPEWrapper - SimpleFPEWrapper/lookup.cpp
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

#include "EGL/egl.h"
#include "init.h"
#include "log.h"
#include "fpe/transformation.h"

#define GETPROC(name, var)                                                                                             \
    if (std::strcmp(#name, var) == 0) {                                                                                \
        return (__eglMustCastToProperFunctionPointerType)name;                                                         \
    }

SFPEW_APIENTRY __eglMustCastToProperFunctionPointerType eglGetProcAddress(const char* name) {
    if (!name) return nullptr;

    GETPROC(glGetError, name)
    GETPROC(glGetString, name)
    GETPROC(glGetStringi, name)
    GETPROC(glGetIntegerv, name)
    GETPROC(glGetBooleanv, name)
    GETPROC(glGetDoublev, name)
    GETPROC(glIsEnabled, name)
    GETPROC(glGetLightfv, name)
    GETPROC(glGetLightiv, name)
    GETPROC(glGetMaterialfv, name)
    GETPROC(glGetMaterialiv, name)
    GETPROC(glGetTexEnvfv, name)
    GETPROC(glGetTexEnviv, name)
    GETPROC(glGetTexGenfv, name)
    GETPROC(glGetTexGeniv, name)
    GETPROC(glGetTexGendv, name)
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
    GETPROC(glTexImage1D, name)
    GETPROC(glTexSubImage1D, name)
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
    GETPROC(glTexCoord1d, name)
    GETPROC(glTexCoord1f, name)
    GETPROC(glTexCoord1i, name)
    GETPROC(glTexCoord1s, name)
    GETPROC(glTexCoord2d, name)
    GETPROC(glTexCoord2i, name)
    GETPROC(glTexCoord2s, name)
    GETPROC(glTexCoord3d, name)
    GETPROC(glTexCoord3f, name)
    GETPROC(glTexCoord3i, name)
    GETPROC(glTexCoord3s, name)
    GETPROC(glTexCoord4d, name)
    GETPROC(glTexCoord4i, name)
    GETPROC(glTexCoord4s, name)
    GETPROC(glTexCoord1dv, name)
    GETPROC(glTexCoord1fv, name)
    GETPROC(glTexCoord1iv, name)
    GETPROC(glTexCoord1sv, name)
    GETPROC(glTexCoord2dv, name)
    GETPROC(glTexCoord2fv, name)
    GETPROC(glTexCoord2iv, name)
    GETPROC(glTexCoord2sv, name)
    GETPROC(glTexCoord3dv, name)
    GETPROC(glTexCoord3fv, name)
    GETPROC(glTexCoord3iv, name)
    GETPROC(glTexCoord3sv, name)
    GETPROC(glTexCoord4dv, name)
    GETPROC(glTexCoord4fv, name)
    GETPROC(glTexCoord4iv, name)
    GETPROC(glTexCoord4sv, name)
    GETPROC(glMultiTexCoord1d, name)
    if (std::strcmp("glMultiTexCoord1dARB", name) == 0) {
        return (__eglMustCastToProperFunctionPointerType)glMultiTexCoord1d;
    }
    GETPROC(glMultiTexCoord1f, name)
    if (std::strcmp("glMultiTexCoord1fARB", name) == 0) {
        return (__eglMustCastToProperFunctionPointerType)glMultiTexCoord1f;
    }
    GETPROC(glMultiTexCoord1i, name)
    if (std::strcmp("glMultiTexCoord1iARB", name) == 0) {
        return (__eglMustCastToProperFunctionPointerType)glMultiTexCoord1i;
    }
    GETPROC(glMultiTexCoord1s, name)
    if (std::strcmp("glMultiTexCoord1sARB", name) == 0) {
        return (__eglMustCastToProperFunctionPointerType)glMultiTexCoord1s;
    }
    GETPROC(glMultiTexCoord2d, name)
    if (std::strcmp("glMultiTexCoord2dARB", name) == 0) {
        return (__eglMustCastToProperFunctionPointerType)glMultiTexCoord2d;
    }
    if (std::strcmp("glMultiTexCoord2fARB", name) == 0) {
        return (__eglMustCastToProperFunctionPointerType)glMultiTexCoord2f;
    }
    GETPROC(glMultiTexCoord2i, name)
    if (std::strcmp("glMultiTexCoord2iARB", name) == 0) {
        return (__eglMustCastToProperFunctionPointerType)glMultiTexCoord2i;
    }
    GETPROC(glMultiTexCoord2s, name)
    if (std::strcmp("glMultiTexCoord2sARB", name) == 0) {
        return (__eglMustCastToProperFunctionPointerType)glMultiTexCoord2s;
    }
    GETPROC(glMultiTexCoord3d, name)
    if (std::strcmp("glMultiTexCoord3dARB", name) == 0) {
        return (__eglMustCastToProperFunctionPointerType)glMultiTexCoord3d;
    }
    GETPROC(glMultiTexCoord3f, name)
    if (std::strcmp("glMultiTexCoord3fARB", name) == 0) {
        return (__eglMustCastToProperFunctionPointerType)glMultiTexCoord3f;
    }
    GETPROC(glMultiTexCoord3i, name)
    if (std::strcmp("glMultiTexCoord3iARB", name) == 0) {
        return (__eglMustCastToProperFunctionPointerType)glMultiTexCoord3i;
    }
    GETPROC(glMultiTexCoord3s, name)
    if (std::strcmp("glMultiTexCoord3sARB", name) == 0) {
        return (__eglMustCastToProperFunctionPointerType)glMultiTexCoord3s;
    }
    GETPROC(glMultiTexCoord4d, name)
    if (std::strcmp("glMultiTexCoord4dARB", name) == 0) {
        return (__eglMustCastToProperFunctionPointerType)glMultiTexCoord4d;
    }
    if (std::strcmp("glMultiTexCoord4fARB", name) == 0) {
        return (__eglMustCastToProperFunctionPointerType)glMultiTexCoord4f;
    }
    GETPROC(glMultiTexCoord4i, name)
    if (std::strcmp("glMultiTexCoord4iARB", name) == 0) {
        return (__eglMustCastToProperFunctionPointerType)glMultiTexCoord4i;
    }
    GETPROC(glMultiTexCoord4s, name)
    if (std::strcmp("glMultiTexCoord4sARB", name) == 0) {
        return (__eglMustCastToProperFunctionPointerType)glMultiTexCoord4s;
    }
    GETPROC(glMultiTexCoord1dv, name)
    if (std::strcmp("glMultiTexCoord1dvARB", name) == 0) {
        return (__eglMustCastToProperFunctionPointerType)glMultiTexCoord1dv;
    }
    GETPROC(glMultiTexCoord1fv, name)
    if (std::strcmp("glMultiTexCoord1fvARB", name) == 0) {
        return (__eglMustCastToProperFunctionPointerType)glMultiTexCoord1fv;
    }
    GETPROC(glMultiTexCoord1iv, name)
    if (std::strcmp("glMultiTexCoord1ivARB", name) == 0) {
        return (__eglMustCastToProperFunctionPointerType)glMultiTexCoord1iv;
    }
    GETPROC(glMultiTexCoord1sv, name)
    if (std::strcmp("glMultiTexCoord1svARB", name) == 0) {
        return (__eglMustCastToProperFunctionPointerType)glMultiTexCoord1sv;
    }
    GETPROC(glMultiTexCoord2dv, name)
    if (std::strcmp("glMultiTexCoord2dvARB", name) == 0) {
        return (__eglMustCastToProperFunctionPointerType)glMultiTexCoord2dv;
    }
    GETPROC(glMultiTexCoord2fv, name)
    if (std::strcmp("glMultiTexCoord2fvARB", name) == 0) {
        return (__eglMustCastToProperFunctionPointerType)glMultiTexCoord2fv;
    }
    GETPROC(glMultiTexCoord2iv, name)
    if (std::strcmp("glMultiTexCoord2ivARB", name) == 0) {
        return (__eglMustCastToProperFunctionPointerType)glMultiTexCoord2iv;
    }
    GETPROC(glMultiTexCoord2sv, name)
    if (std::strcmp("glMultiTexCoord2svARB", name) == 0) {
        return (__eglMustCastToProperFunctionPointerType)glMultiTexCoord2sv;
    }
    GETPROC(glMultiTexCoord3dv, name)
    if (std::strcmp("glMultiTexCoord3dvARB", name) == 0) {
        return (__eglMustCastToProperFunctionPointerType)glMultiTexCoord3dv;
    }
    GETPROC(glMultiTexCoord3fv, name)
    if (std::strcmp("glMultiTexCoord3fvARB", name) == 0) {
        return (__eglMustCastToProperFunctionPointerType)glMultiTexCoord3fv;
    }
    GETPROC(glMultiTexCoord3iv, name)
    if (std::strcmp("glMultiTexCoord3ivARB", name) == 0) {
        return (__eglMustCastToProperFunctionPointerType)glMultiTexCoord3iv;
    }
    GETPROC(glMultiTexCoord3sv, name)
    if (std::strcmp("glMultiTexCoord3svARB", name) == 0) {
        return (__eglMustCastToProperFunctionPointerType)glMultiTexCoord3sv;
    }
    GETPROC(glMultiTexCoord4dv, name)
    if (std::strcmp("glMultiTexCoord4dvARB", name) == 0) {
        return (__eglMustCastToProperFunctionPointerType)glMultiTexCoord4dv;
    }
    GETPROC(glMultiTexCoord4fv, name)
    if (std::strcmp("glMultiTexCoord4fvARB", name) == 0) {
        return (__eglMustCastToProperFunctionPointerType)glMultiTexCoord4fv;
    }
    GETPROC(glMultiTexCoord4iv, name)
    if (std::strcmp("glMultiTexCoord4ivARB", name) == 0) {
        return (__eglMustCastToProperFunctionPointerType)glMultiTexCoord4iv;
    }
    GETPROC(glMultiTexCoord4sv, name)
    if (std::strcmp("glMultiTexCoord4svARB", name) == 0) {
        return (__eglMustCastToProperFunctionPointerType)glMultiTexCoord4sv;
    }
    GETPROC(glPointSize, name)
    GETPROC(glPolygonMode, name)
    GETPROC(glTexGeni, name)
    GETPROC(glTexGenf, name)
    GETPROC(glTexGend, name)
    GETPROC(glTexGeniv, name)
    GETPROC(glTexGenfv, name)
    GETPROC(glTexGendv, name)
    GETPROC(glClipPlane, name)
    GETPROC(glGetClipPlane, name)
    GETPROC(glRectd, name)
    GETPROC(glRectf, name)
    GETPROC(glRecti, name)
    GETPROC(glRects, name)
    GETPROC(glRectdv, name)
    GETPROC(glRectfv, name)
    GETPROC(glRectiv, name)
    GETPROC(glRectsv, name)
    GETPROC(glDepthRange, name)
    GETPROC(glHint, name)
    GETPROC(glPixelStorei, name)
    GETPROC(glPixelStoref, name)
    GETPROC(glPushAttrib, name)
    GETPROC(glPopAttrib, name)
    GETPROC(glPushClientAttrib, name)
    GETPROC(glPopClientAttrib, name)
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
    GETPROC(glInterleavedArrays, name)
    GETPROC(glEdgeFlagPointer, name)
    GETPROC(glSecondaryColorPointer, name)
    if (std::strcmp("glSecondaryColorPointerEXT", name) == 0) {
        return (__eglMustCastToProperFunctionPointerType)glSecondaryColorPointer;
    }
    GETPROC(glFogCoordPointer, name)
    if (std::strcmp("glFogCoordPointerEXT", name) == 0) {
        return (__eglMustCastToProperFunctionPointerType)glFogCoordPointer;
    }
    GETPROC(glArrayElement, name)
    GETPROC(glEnableClientState, name)
    GETPROC(glDisableClientState, name)
    GETPROC(glGetFloatv, name)

    if (!sfpewEnsureBackend() || g_eglFuncs.eglGetProcAddress == nullptr) {
        return nullptr;
    }
    __eglMustCastToProperFunctionPointerType ptr = g_eglFuncs.eglGetProcAddress(name);
    if (!ptr) {
        SFPEW_LOGW("eglGetProcAddress: backend also failed to find '%s'", name);
    }
    return ptr;
}

SFPEW_APIENTRY void* glXGetProcAddress(const char* name) {
    return (void*)eglGetProcAddress(name);
}

SFPEW_APIENTRY void* glXGetProcAddressARB(const char* name) {
    return glXGetProcAddress(name);
}
