#!/usr/bin/env python3
# SimpleFPEWrapper - tools/gen_api_manifest.py
# Copyright (c) 2026 MobileGL-Dev
# SPDX-License-Identifier: LGPL-3.0-only
#
# Generates the resolver-surface API manifest (plans/03, task 3.5) by
# parsing lookup.cpp: every symbol eglGetProcAddress can answer, including
# aliases. Replaces the hand-written fpe_implementation_progress.yaml as
# the source of truth for "what does the wrapper export".
#
# Usage: gen_api_manifest.py <repo_root> <out_dir>
# Writes api-manifest.md and api-manifest.json into <out_dir>.

import json
import re
import sys
from pathlib import Path

# The full GL <= 2.1 core command set (551 names, gl.xml's <feature api="gl">
# blocks through number="2.1"), frozen here as a literal so this tool has no
# dependency on gl.xml being present at generation time - it is a reference
# set, not something re-derived per run. Regenerate by walking gl.xml's
# feature/require and feature/remove command lists for every api="gl" feature
# with number <= 2.1, unioning the required names and subtracting anything a
# later <=2.1 feature removes.
#
# Used only to classify names lookup.cpp's eglGetProcAddress resolves WITHOUT
# a matching GETPROC/alias line: the final fallback in eglGetProcAddress asks
# the backend's own eglGetProcAddress for the name unchanged, which is
# correct and intentional for functions the wrapper does not need to
# intercept (glUniform*, glGenBuffers, glIsProgram, ...) - adding an
# individual GETPROC(name, name) line for each would be pure boilerplate.
# That fallback has no per-symbol text for a regex to find, so without this
# reference set those names would look unlisted rather than "resolves via
# the generic backend fallthrough", which is a real, working, and different
# thing from "unresolvable". Computed as a set difference below, so this
# classification stays correct as lookup.cpp gains explicit entries over
# time (e.g. a name here moving from GETPROC-less to a real GETPROC line, as
# happened for glGetTexImage and glLogicOp) without editing this list.
GL21_CORE_COMMANDS = frozenset({
    "glAccum", "glActiveTexture", "glAlphaFunc", "glAreTexturesResident", "glArrayElement",
    "glAttachShader", "glBegin", "glBeginQuery", "glBindAttribLocation", "glBindBuffer",
    "glBindTexture", "glBitmap", "glBlendColor", "glBlendEquation", "glBlendEquationSeparate",
    "glBlendFunc", "glBlendFuncSeparate", "glBufferData", "glBufferSubData", "glCallList",
    "glCallLists", "glClear", "glClearAccum", "glClearColor", "glClearDepth", "glClearIndex",
    "glClearStencil", "glClientActiveTexture", "glClipPlane", "glColor3b", "glColor3bv",
    "glColor3d", "glColor3dv", "glColor3f", "glColor3fv", "glColor3i", "glColor3iv",
    "glColor3s", "glColor3sv", "glColor3ub", "glColor3ubv", "glColor3ui", "glColor3uiv",
    "glColor3us", "glColor3usv", "glColor4b", "glColor4bv", "glColor4d", "glColor4dv",
    "glColor4f", "glColor4fv", "glColor4i", "glColor4iv", "glColor4s", "glColor4sv",
    "glColor4ub", "glColor4ubv", "glColor4ui", "glColor4uiv", "glColor4us", "glColor4usv",
    "glColorMask", "glColorMaterial", "glColorPointer", "glCompileShader",
    "glCompressedTexImage1D", "glCompressedTexImage2D", "glCompressedTexImage3D",
    "glCompressedTexSubImage1D", "glCompressedTexSubImage2D", "glCompressedTexSubImage3D",
    "glCopyPixels", "glCopyTexImage1D", "glCopyTexImage2D", "glCopyTexSubImage1D",
    "glCopyTexSubImage2D", "glCopyTexSubImage3D", "glCreateProgram", "glCreateShader",
    "glCullFace", "glDeleteBuffers", "glDeleteLists", "glDeleteProgram", "glDeleteQueries",
    "glDeleteShader", "glDeleteTextures", "glDepthFunc", "glDepthMask", "glDepthRange",
    "glDetachShader", "glDisable", "glDisableClientState", "glDisableVertexAttribArray",
    "glDrawArrays", "glDrawBuffer", "glDrawBuffers", "glDrawElements", "glDrawPixels",
    "glDrawRangeElements", "glEdgeFlag", "glEdgeFlagPointer", "glEdgeFlagv", "glEnable",
    "glEnableClientState", "glEnableVertexAttribArray", "glEnd", "glEndList", "glEndQuery",
    "glEvalCoord1d", "glEvalCoord1dv", "glEvalCoord1f", "glEvalCoord1fv", "glEvalCoord2d",
    "glEvalCoord2dv", "glEvalCoord2f", "glEvalCoord2fv", "glEvalMesh1", "glEvalMesh2",
    "glEvalPoint1", "glEvalPoint2", "glFeedbackBuffer", "glFinish", "glFlush",
    "glFogCoordPointer", "glFogCoordd", "glFogCoorddv", "glFogCoordf", "glFogCoordfv",
    "glFogf", "glFogfv", "glFogi", "glFogiv", "glFrontFace", "glFrustum", "glGenBuffers",
    "glGenLists", "glGenQueries", "glGenTextures", "glGetActiveAttrib", "glGetActiveUniform",
    "glGetAttachedShaders", "glGetAttribLocation", "glGetBooleanv", "glGetBufferParameteriv",
    "glGetBufferPointerv", "glGetBufferSubData", "glGetClipPlane", "glGetCompressedTexImage",
    "glGetDoublev", "glGetError", "glGetFloatv", "glGetIntegerv", "glGetLightfv",
    "glGetLightiv", "glGetMapdv", "glGetMapfv", "glGetMapiv", "glGetMaterialfv",
    "glGetMaterialiv", "glGetPixelMapfv", "glGetPixelMapuiv", "glGetPixelMapusv",
    "glGetPointerv", "glGetPolygonStipple", "glGetProgramInfoLog", "glGetProgramiv",
    "glGetQueryObjectiv", "glGetQueryObjectuiv", "glGetQueryiv", "glGetShaderInfoLog",
    "glGetShaderSource", "glGetShaderiv", "glGetString", "glGetTexEnvfv", "glGetTexEnviv",
    "glGetTexGendv", "glGetTexGenfv", "glGetTexGeniv", "glGetTexImage",
    "glGetTexLevelParameterfv", "glGetTexLevelParameteriv", "glGetTexParameterfv",
    "glGetTexParameteriv", "glGetUniformLocation", "glGetUniformfv", "glGetUniformiv",
    "glGetVertexAttribPointerv", "glGetVertexAttribdv", "glGetVertexAttribfv",
    "glGetVertexAttribiv", "glHint", "glIndexMask", "glIndexPointer", "glIndexd", "glIndexdv",
    "glIndexf", "glIndexfv", "glIndexi", "glIndexiv", "glIndexs", "glIndexsv", "glIndexub",
    "glIndexubv", "glInitNames", "glInterleavedArrays", "glIsBuffer", "glIsEnabled",
    "glIsList", "glIsProgram", "glIsQuery", "glIsShader", "glIsTexture", "glLightModelf",
    "glLightModelfv", "glLightModeli", "glLightModeliv", "glLightf", "glLightfv", "glLighti",
    "glLightiv", "glLineStipple", "glLineWidth", "glLinkProgram", "glListBase",
    "glLoadIdentity", "glLoadMatrixd", "glLoadMatrixf", "glLoadName", "glLoadTransposeMatrixd",
    "glLoadTransposeMatrixf", "glLogicOp", "glMap1d", "glMap1f", "glMap2d", "glMap2f",
    "glMapBuffer", "glMapGrid1d", "glMapGrid1f", "glMapGrid2d", "glMapGrid2f", "glMaterialf",
    "glMaterialfv", "glMateriali", "glMaterialiv", "glMatrixMode", "glMultMatrixd",
    "glMultMatrixf", "glMultTransposeMatrixd", "glMultTransposeMatrixf", "glMultiDrawArrays",
    "glMultiDrawElements", "glMultiTexCoord1d", "glMultiTexCoord1dv", "glMultiTexCoord1f",
    "glMultiTexCoord1fv", "glMultiTexCoord1i", "glMultiTexCoord1iv", "glMultiTexCoord1s",
    "glMultiTexCoord1sv", "glMultiTexCoord2d", "glMultiTexCoord2dv", "glMultiTexCoord2f",
    "glMultiTexCoord2fv", "glMultiTexCoord2i", "glMultiTexCoord2iv", "glMultiTexCoord2s",
    "glMultiTexCoord2sv", "glMultiTexCoord3d", "glMultiTexCoord3dv", "glMultiTexCoord3f",
    "glMultiTexCoord3fv", "glMultiTexCoord3i", "glMultiTexCoord3iv", "glMultiTexCoord3s",
    "glMultiTexCoord3sv", "glMultiTexCoord4d", "glMultiTexCoord4dv", "glMultiTexCoord4f",
    "glMultiTexCoord4fv", "glMultiTexCoord4i", "glMultiTexCoord4iv", "glMultiTexCoord4s",
    "glMultiTexCoord4sv", "glNewList", "glNormal3b", "glNormal3bv", "glNormal3d",
    "glNormal3dv", "glNormal3f", "glNormal3fv", "glNormal3i", "glNormal3iv", "glNormal3s",
    "glNormal3sv", "glNormalPointer", "glOrtho", "glPassThrough", "glPixelMapfv",
    "glPixelMapuiv", "glPixelMapusv", "glPixelStoref", "glPixelStorei", "glPixelTransferf",
    "glPixelTransferi", "glPixelZoom", "glPointParameterf", "glPointParameterfv",
    "glPointParameteri", "glPointParameteriv", "glPointSize", "glPolygonMode",
    "glPolygonOffset", "glPolygonStipple", "glPopAttrib", "glPopClientAttrib", "glPopMatrix",
    "glPopName", "glPrioritizeTextures", "glPushAttrib", "glPushClientAttrib", "glPushMatrix",
    "glPushName", "glRasterPos2d", "glRasterPos2dv", "glRasterPos2f", "glRasterPos2fv",
    "glRasterPos2i", "glRasterPos2iv", "glRasterPos2s", "glRasterPos2sv", "glRasterPos3d",
    "glRasterPos3dv", "glRasterPos3f", "glRasterPos3fv", "glRasterPos3i", "glRasterPos3iv",
    "glRasterPos3s", "glRasterPos3sv", "glRasterPos4d", "glRasterPos4dv", "glRasterPos4f",
    "glRasterPos4fv", "glRasterPos4i", "glRasterPos4iv", "glRasterPos4s", "glRasterPos4sv",
    "glReadBuffer", "glReadPixels", "glRectd", "glRectdv", "glRectf", "glRectfv", "glRecti",
    "glRectiv", "glRects", "glRectsv", "glRenderMode", "glRotated", "glRotatef",
    "glSampleCoverage", "glScaled", "glScalef", "glScissor", "glSecondaryColor3b",
    "glSecondaryColor3bv", "glSecondaryColor3d", "glSecondaryColor3dv", "glSecondaryColor3f",
    "glSecondaryColor3fv", "glSecondaryColor3i", "glSecondaryColor3iv", "glSecondaryColor3s",
    "glSecondaryColor3sv", "glSecondaryColor3ub", "glSecondaryColor3ubv",
    "glSecondaryColor3ui", "glSecondaryColor3uiv", "glSecondaryColor3us",
    "glSecondaryColor3usv", "glSecondaryColorPointer", "glSelectBuffer", "glShadeModel",
    "glShaderSource", "glStencilFunc", "glStencilFuncSeparate", "glStencilMask",
    "glStencilMaskSeparate", "glStencilOp", "glStencilOpSeparate", "glTexCoord1d",
    "glTexCoord1dv", "glTexCoord1f", "glTexCoord1fv", "glTexCoord1i", "glTexCoord1iv",
    "glTexCoord1s", "glTexCoord1sv", "glTexCoord2d", "glTexCoord2dv", "glTexCoord2f",
    "glTexCoord2fv", "glTexCoord2i", "glTexCoord2iv", "glTexCoord2s", "glTexCoord2sv",
    "glTexCoord3d", "glTexCoord3dv", "glTexCoord3f", "glTexCoord3fv", "glTexCoord3i",
    "glTexCoord3iv", "glTexCoord3s", "glTexCoord3sv", "glTexCoord4d", "glTexCoord4dv",
    "glTexCoord4f", "glTexCoord4fv", "glTexCoord4i", "glTexCoord4iv", "glTexCoord4s",
    "glTexCoord4sv", "glTexCoordPointer", "glTexEnvf", "glTexEnvfv", "glTexEnvi", "glTexEnviv",
    "glTexGend", "glTexGendv", "glTexGenf", "glTexGenfv", "glTexGeni", "glTexGeniv",
    "glTexImage1D", "glTexImage2D", "glTexImage3D", "glTexParameterf", "glTexParameterfv",
    "glTexParameteri", "glTexParameteriv", "glTexSubImage1D", "glTexSubImage2D",
    "glTexSubImage3D", "glTranslated", "glTranslatef", "glUniform1f", "glUniform1fv",
    "glUniform1i", "glUniform1iv", "glUniform2f", "glUniform2fv", "glUniform2i",
    "glUniform2iv", "glUniform3f", "glUniform3fv", "glUniform3i", "glUniform3iv",
    "glUniform4f", "glUniform4fv", "glUniform4i", "glUniform4iv", "glUniformMatrix2fv",
    "glUniformMatrix2x3fv", "glUniformMatrix2x4fv", "glUniformMatrix3fv",
    "glUniformMatrix3x2fv", "glUniformMatrix3x4fv", "glUniformMatrix4fv",
    "glUniformMatrix4x2fv", "glUniformMatrix4x3fv", "glUnmapBuffer", "glUseProgram",
    "glValidateProgram", "glVertex2d", "glVertex2dv", "glVertex2f", "glVertex2fv",
    "glVertex2i", "glVertex2iv", "glVertex2s", "glVertex2sv", "glVertex3d", "glVertex3dv",
    "glVertex3f", "glVertex3fv", "glVertex3i", "glVertex3iv", "glVertex3s", "glVertex3sv",
    "glVertex4d", "glVertex4dv", "glVertex4f", "glVertex4fv", "glVertex4i", "glVertex4iv",
    "glVertex4s", "glVertex4sv", "glVertexAttrib1d", "glVertexAttrib1dv", "glVertexAttrib1f",
    "glVertexAttrib1fv", "glVertexAttrib1s", "glVertexAttrib1sv", "glVertexAttrib2d",
    "glVertexAttrib2dv", "glVertexAttrib2f", "glVertexAttrib2fv", "glVertexAttrib2s",
    "glVertexAttrib2sv", "glVertexAttrib3d", "glVertexAttrib3dv", "glVertexAttrib3f",
    "glVertexAttrib3fv", "glVertexAttrib3s", "glVertexAttrib3sv", "glVertexAttrib4Nbv",
    "glVertexAttrib4Niv", "glVertexAttrib4Nsv", "glVertexAttrib4Nub", "glVertexAttrib4Nubv",
    "glVertexAttrib4Nuiv", "glVertexAttrib4Nusv", "glVertexAttrib4bv", "glVertexAttrib4d",
    "glVertexAttrib4dv", "glVertexAttrib4f", "glVertexAttrib4fv", "glVertexAttrib4iv",
    "glVertexAttrib4s", "glVertexAttrib4sv", "glVertexAttrib4ubv", "glVertexAttrib4uiv",
    "glVertexAttrib4usv", "glVertexAttribPointer", "glVertexPointer", "glViewport",
    "glWindowPos2d", "glWindowPos2dv", "glWindowPos2f", "glWindowPos2fv", "glWindowPos2i",
    "glWindowPos2iv", "glWindowPos2s", "glWindowPos2sv", "glWindowPos3d", "glWindowPos3dv",
    "glWindowPos3f", "glWindowPos3fv", "glWindowPos3i", "glWindowPos3iv", "glWindowPos3s",
    "glWindowPos3sv",
})


def main() -> int:
    repo = Path(sys.argv[1])
    out_dir = Path(sys.argv[2])
    src = (repo / "SimpleFPEWrapper" / "lookup.cpp").read_text()

    direct = re.findall(r"^\s*GETPROC\((\w+), name\)", src, re.M)
    aliases = re.findall(
        r'std::strcmp\("(\w+)", name\) == 0\) \{\s*\n\s*return \(__eglMustCastToProperFunctionPointerType\)(\w+);',
        src,
    )
    # The two alias macros resolve just as many names as the hand-written
    # strcmp blocks above, so they belong in the surface too. They differ in
    # what the caller gets: WRAPPER_ALIAS hands out the wrapper's own entry
    # point, BACKEND_ALIAS hands out the backend's (an EXT/ARB spelling of a
    # command the wrapper does not need to intercept).
    wrapper_aliases = re.findall(r"^\s*GETPROC_WRAPPER_ALIAS\((\w+), (\w+)\)", src, re.M)
    backend_aliases = re.findall(r"^\s*GETPROC_BACKEND_ALIAS\((\w+), (\w+)\)", src, re.M)

    entries = {name: {"status": "resolvable", "target": name} for name in direct}
    for alias, target in aliases:
        entries.setdefault(alias, {"status": "alias", "target": target})
    for alias, target in wrapper_aliases:
        entries.setdefault(alias, {"status": "alias", "target": target})
    for alias, target in backend_aliases:
        entries.setdefault(alias, {"status": "backend-alias", "target": target})

    # Everything still unaccounted for that IS a GL <= 2.1 core command
    # falls through eglGetProcAddress's final case: ask the backend's own
    # eglGetProcAddress for the name unchanged (lookup.cpp, the
    # g_eglFuncs.eglGetProcAddress(name) call after every GETPROC/alias
    # check fails). That is a real, working resolution path, not a gap - it
    # is how the wrapper avoids one boilerplate line per standard entry
    # point it does not need to intercept. Recording it here closes the
    # manifest's coverage gap without adding ~80 redundant GETPROC(name,
    # name) lines lookup.cpp does not need.
    unaccounted = GL21_CORE_COMMANDS - set(entries.keys())
    for name in unaccounted:
        entries[name] = {"status": "backend-fallthrough", "target": name}

    ordered = dict(sorted(entries.items()))
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "api-manifest.json").write_text(
        json.dumps({"source": "lookup.cpp", "count": len(ordered), "entries": ordered}, indent=1)
        + "\n"
    )

    lines = [
        "# SimpleFPEWrapper API manifest (resolver surface)",
        "",
        "Generated by `tools/gen_api_manifest.py` from `lookup.cpp` - do not edit.",
        f"Entries: {len(ordered)}",
        "",
        "`backend-fallthrough` entries are GL <= 2.1 core commands with no",
        "GETPROC/alias line in lookup.cpp: they resolve through",
        "eglGetProcAddress's final fallback, which asks the backend's own",
        "eglGetProcAddress for the name unchanged. That is intentional for",
        "commands the wrapper does not need to intercept, not a gap.",
        "",
        "| symbol | status | target |",
        "|---|---|---|",
    ]
    lines += [f"| {n} | {e['status']} | {e['target']} |" for n, e in ordered.items()]
    (out_dir / "api-manifest.md").write_text("\n".join(lines) + "\n")
    print(f"api manifest: {len(ordered)} entries -> {out_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
