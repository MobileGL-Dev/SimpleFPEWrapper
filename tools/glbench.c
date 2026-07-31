// SimpleFPEWrapper - tools/glbench.c
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// Fixed-function CPU-overhead benchmark for ANY library that exports the
// GL 1.x entry points: SimpleFPEWrapper, gl4es, a system libGL, Mesa, a
// vendor driver. Nothing here is specific to this project - the workload
// resolves every entry point with dlsym against the library under test and
// never links a system libGL, so the same binary measures all of them and
// the numbers are directly comparable.
//
// Deliberately standalone rather than a ctest: it takes a library path, and
// comparing two implementations means running it twice. tools/glbench-compare.sh
// drives that and prints the ratio table.
//
//   CMPBENCH_LIB=<path to .so>   library under test (required)
//   CMPBENCH_CTX=egl|glx         how to obtain the backing context
//   CMPBENCH_SCALE=<float>       iteration scale (default 1.0)
//   CMPBENCH_ONLY=<substring>    run only matching phases
//   CMPBENCH_VIEWPORT=<n>        square viewport; 1 (default) removes
//                                fragment cost so only CPU overhead shows
//   CMPBENCH_TSV=1               emit "phase<TAB>value<TAB>unit" for scripts
//
// Phases fall in three groups. The first nine isolate one geometry
// submission mechanism each (immediate, dlist, clientarrays, drawelements,
// tinybatch, progtoggle, matrixops, getter, texswitch). The mc* ones
// reproduce the GL 1.x call sequences Minecraft issues, so a library can be
// judged on the shape of work a real app produces rather than on
// microbenchmarks alone; see plans/12-fpe-draw-cost.md for what each
// reconstructs.
//
// The last five cover the rest of what a GL 2.1 implementation has to
// implement, which submitting geometry does not touch:
//   lighting     8 lights, normals, per-draw materials, shade/two-side
//                toggling - the largest generator of fixed-function shader
//                permutations
//   texstages    multitexture with per-unit texture environments (including
//                COMBINE), texture matrices and texgen
//   statechurn   the attribute stack plus blend/depth/stencil/scissor/cull
//                churn around a draw: the cost of tracking state, not of
//                drawing
//   shaderbuild  GLSL 1.10 compile+link throughput, unique sources so no
//                cache can answer
//   progdraw     a user program driven as an app drives it: generic
//                attributes from a VBO, a full uniform set per draw,
//                indexed draws, periodic render-to-texture
//
// The first two groups use only GL 1.x calls that any of these libraries
// implements, so a phase that prints nothing means an entry point was
// missing, not that the library was excused from the work. The last group
// resolves its entry points optionally and reports nothing when a library
// lacks them or cannot build the shader - "absent" must never read as
// "fast".

#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef unsigned int GLenum, GLuint, GLbitfield;
typedef unsigned char GLubyte, GLboolean;
typedef float GLfloat;
typedef double GLdouble;
typedef int GLint, GLsizei;

#define GL_QUADS 0x0007
#define GL_TRIANGLES 0x0004
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_DEPTH_BUFFER_BIT 0x00000100
#define GL_RGBA 0x1908
#define GL_UNSIGNED_BYTE 0x1401
#define GL_UNSIGNED_SHORT 0x1403
#define GL_FLOAT 0x1406
#define GL_MODELVIEW 0x1700
#define GL_PROJECTION 0x1701
#define GL_MODELVIEW_MATRIX 0x0BA6
#define GL_TEXTURE_2D 0x0DE1
#define GL_VERTEX_ARRAY 0x8074
#define GL_COLOR_ARRAY 0x8076
#define GL_TEXTURE_COORD_ARRAY 0x8078
#define GL_LIGHTING 0x0B50
#define GL_FOG 0x0B60
#define GL_ALPHA_TEST 0x0BC0
#define GL_COMPILE 0x1300
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_NEAREST 0x2600
#define GL_ARRAY_BUFFER 0x8892
#define GL_STATIC_DRAW 0x88E4
#define GL_UNSIGNED_BYTE_T 0x1401
#define GL_SHORT 0x1402
#define GL_TEXTURE0_C 0x84C0
#define GL_TEXTURE1_C 0x84C1
#define GL_FOG_MODE 0x0B65
#define GL_FOG_COLOR 0x0B66
#define GL_FOG_START 0x0B63
#define GL_FOG_END 0x0B64
#define GL_LINEAR_F 0x2601

/* lighting / material */
#define GL_LIGHT0 0x4000
#define GL_POSITION 0x1203
#define GL_AMBIENT 0x1200
#define GL_DIFFUSE 0x1201
#define GL_SPECULAR 0x1202
#define GL_SPOT_DIRECTION 0x1204
#define GL_SPOT_EXPONENT 0x1205
#define GL_SPOT_CUTOFF 0x1206
#define GL_CONSTANT_ATTENUATION 0x1207
#define GL_LINEAR_ATTENUATION 0x1208
#define GL_QUADRATIC_ATTENUATION 0x1209
#define GL_EMISSION 0x1600
#define GL_SHININESS 0x1601
#define GL_FRONT 0x0404
#define GL_BACK 0x0405
#define GL_FRONT_AND_BACK 0x0408
#define GL_AMBIENT_AND_DIFFUSE 0x1602
#define GL_LIGHT_MODEL_TWO_SIDE 0x0B52
#define GL_LIGHT_MODEL_AMBIENT 0x0B53
#define GL_COLOR_MATERIAL 0x0B57
#define GL_NORMALIZE 0x0BA1
#define GL_NORMAL_ARRAY 0x8075
#define GL_SMOOTH 0x1D01
#define GL_FLAT 0x1D00

/* texture stages */
#define GL_TEXTURE_ENV 0x2300
#define GL_TEXTURE_ENV_MODE 0x2200
#define GL_TEXTURE_ENV_COLOR 0x2201
#define GL_MODULATE 0x2100
#define GL_ADD 0x0104
#define GL_COMBINE 0x8570
#define GL_COMBINE_RGB 0x8571
#define GL_SRC0_RGB 0x8580
#define GL_SRC1_RGB 0x8581
#define GL_OPERAND0_RGB 0x8590
#define GL_INTERPOLATE 0x8575
#define GL_PREVIOUS 0x8578
#define GL_TEXTURE_C 0x1702
#define GL_SRC_COLOR 0x0300
#define GL_TEXTURE_GEN_S 0x0C60
#define GL_TEXTURE_GEN_T 0x0C61
#define GL_TEXTURE_GEN_MODE 0x2500
#define GL_SPHERE_MAP 0x2402
#define GL_OBJECT_LINEAR 0x2401
#define GL_S 0x2000
#define GL_T 0x2001

/* state churn */
#define GL_BLEND 0x0BE2
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_ONE 1
#define GL_ZERO 0
#define GL_DEPTH_TEST 0x0B71
#define GL_LESS 0x0201
#define GL_LEQUAL 0x0203
#define GL_GREATER 0x0204
#define GL_STENCIL_TEST 0x0B90
#define GL_KEEP 0x1E00
#define GL_REPLACE 0x1E01
#define GL_SCISSOR_TEST 0x0C11
#define GL_CULL_FACE 0x0B44
#define GL_CCW 0x0901
#define GL_CW 0x0900
#define GL_POLYGON_OFFSET_FILL 0x8037
#define GL_ALL_ATTRIB_BITS 0x000FFFFF
#define GL_ENABLE_BIT 0x00002000
#define GL_COLOR_BUFFER_BIT_A 0x00004000
#define GL_TRANSFORM_BIT 0x00001000
#define GL_ALWAYS 0x0207
#define GL_GEQUAL 0x0206

/* GL 2.x programmable pipeline */
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_INFO_LOG_LENGTH 0x8B84
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_FRAMEBUFFER 0x8D40
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_LINEAR_T 0x2601

static void* lib;
static void* backend_gles;
static void* (*lib_get_proc)(const char*);

// The resolution order a real app ends up with: the wrapper's own exported
// symbol, then its eglGetProcAddress (which is how a wrapper hands over
// entry points it only forwards), then the backend GLES library. gl4es
// exports everything directly and never needs the later steps.
static void* sym(const char* n) {
    void* p = dlsym(lib, n);
    if (p) return p;
    if (lib_get_proc) {
        p = lib_get_proc(n);
        if (p) return p;
    }
    if (backend_gles) {
        p = dlsym(backend_gles, n);
        if (p) return p;
    }
    return NULL;
}
#define LOAD(v, n)                                                                                 \
    do {                                                                                           \
        *(void**)(&v) = sym(n);                                                                     \
        if (!v) {                                                                                   \
            fprintf(stderr, "MISSING %s\n", n);                                                     \
            missing++;                                                                              \
        }                                                                                           \
    } while (0)

static int missing = 0;
static const char* only = NULL;
static int phase_on(const char* name) { return only == NULL || strstr(name, only) != NULL; }

// One result line. Human form by default; CMPBENCH_TSV=1 switches to
// "phase<TAB>value<TAB>unit" so a driver script can diff two runs without
// parsing column widths.
static int tsv_mode = 0;
static void report(const char* phase, double value, const char* unit) {
    if (tsv_mode) printf("%s\t%.4f\t%s\n", phase, value, unit);
    else printf("%-14s %10.2f %s\n", phase, value, unit);
    fflush(stdout);
}

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1.0e6;
}

/* GL entry points */
static void (*fClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
static void (*fClear)(GLbitfield);
static void (*fFinish)(void);
static void (*fViewport)(GLint, GLint, GLsizei, GLsizei);
static void (*fBegin)(GLenum);
static void (*fEnd)(void);
static void (*fVertex3f)(GLfloat, GLfloat, GLfloat);
static void (*fColor4f)(GLfloat, GLfloat, GLfloat, GLfloat);
static void (*fTexCoord2f)(GLfloat, GLfloat);
static void (*fPushMatrix)(void);
static void (*fPopMatrix)(void);
static void (*fTranslatef)(GLfloat, GLfloat, GLfloat);
static void (*fRotatef)(GLfloat, GLfloat, GLfloat, GLfloat);
static void (*fMatrixMode)(GLenum);
static void (*fLoadIdentity)(void);
static void (*fGetFloatv)(GLenum, GLfloat*);
static void (*fEnable)(GLenum);
static void (*fDisable)(GLenum);
static void (*fEnableClientState)(GLenum);
static void (*fDisableClientState)(GLenum);
static void (*fVertexPointer)(GLint, GLenum, GLsizei, const void*);
static void (*fColorPointer)(GLint, GLenum, GLsizei, const void*);
static void (*fTexCoordPointer)(GLint, GLenum, GLsizei, const void*);
static void (*fClientActiveTexture)(GLenum);
static void (*fMultMatrixf)(const GLfloat*);
static void (*fFogf)(GLenum, GLfloat);
static void (*fFogfv)(GLenum, const GLfloat*);
static void (*fGenBuffers)(GLsizei, GLuint*);
static void (*fBindBuffer)(GLenum, GLuint);
static void (*fBufferData)(GLenum, long, const void*, GLenum);
static void (*fDrawArrays)(GLenum, GLint, GLsizei);
static void (*fDrawElements)(GLenum, GLsizei, GLenum, const void*);
static GLuint (*fGenLists)(GLsizei);
static void (*fNewList)(GLuint, GLenum);
static void (*fEndList)(void);
static void (*fCallList)(GLuint);
static void (*fGenTextures)(GLsizei, GLuint*);
static void (*fBindTexture)(GLenum, GLuint);
static void (*fTexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum,
                           const void*);
static void (*fTexSubImage2D)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum,
                              const void*);
static void (*fTexParameteri)(GLenum, GLenum, GLint);
static void (*fReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*);
static const GLubyte* (*fGetString)(GLenum);

// Entry points the later phases need. Resolved OPTIONALLY: a library that
// lacks one skips that phase instead of failing the whole run, which keeps
// the comparison usable against implementations of differing completeness.
static void (*fLightf)(GLenum, GLenum, GLfloat);
static void (*fLightfv)(GLenum, GLenum, const GLfloat*);
static void (*fLightModelfv)(GLenum, const GLfloat*);
static void (*fLightModeli)(GLenum, GLint);
static void (*fMaterialf)(GLenum, GLenum, GLfloat);
static void (*fMaterialfv)(GLenum, GLenum, const GLfloat*);
static void (*fColorMaterial)(GLenum, GLenum);
static void (*fNormal3f)(GLfloat, GLfloat, GLfloat);
static void (*fNormalPointer)(GLenum, GLsizei, const void*);
static void (*fShadeModel)(GLenum);

static void (*fActiveTexture)(GLenum);
static void (*fMultiTexCoord2f)(GLenum, GLfloat, GLfloat);
static void (*fTexEnvi)(GLenum, GLenum, GLint);
static void (*fTexEnvfv)(GLenum, GLenum, const GLfloat*);
static void (*fTexGeni)(GLenum, GLenum, GLint);

static void (*fPushAttrib)(GLbitfield);
static void (*fPopAttrib)(void);
static void (*fBlendFunc)(GLenum, GLenum);
static void (*fDepthFunc)(GLenum);
static void (*fAlphaFunc)(GLenum, GLfloat);
static void (*fStencilFunc)(GLenum, GLint, GLuint);
static void (*fStencilOp)(GLenum, GLenum, GLenum);
static void (*fScissor)(GLint, GLint, GLsizei, GLsizei);
static void (*fColorMask)(GLboolean, GLboolean, GLboolean, GLboolean);
static void (*fDepthMask)(GLboolean);
static void (*fPolygonOffset)(GLfloat, GLfloat);
static void (*fLineWidth)(GLfloat);
static void (*fCullFace)(GLenum);
static void (*fFrontFace)(GLenum);

static GLuint (*fCreateShader)(GLenum);
static void (*fShaderSource)(GLuint, GLsizei, const char* const*, const GLint*);
static void (*fCompileShader)(GLuint);
static void (*fGetShaderiv)(GLuint, GLenum, GLint*);
static void (*fGetShaderInfoLog)(GLuint, GLsizei, GLsizei*, char*);
static void (*fDeleteShader)(GLuint);
static GLuint (*fCreateProgram)(void);
static void (*fAttachShader)(GLuint, GLuint);
static void (*fLinkProgram)(GLuint);
static void (*fGetProgramiv)(GLuint, GLenum, GLint*);
static void (*fGetProgramInfoLog)(GLuint, GLsizei, GLsizei*, char*);
static void (*fUseProgram)(GLuint);
static void (*fDeleteProgram)(GLuint);
static GLint (*fGetUniformLocation)(GLuint, const char*);
static GLint (*fGetAttribLocation)(GLuint, const char*);
static void (*fUniform1i)(GLint, GLint);
static void (*fUniform1f)(GLint, GLfloat);
static void (*fUniform3fv)(GLint, GLsizei, const GLfloat*);
static void (*fUniform4fv)(GLint, GLsizei, const GLfloat*);
static void (*fUniformMatrix4fv)(GLint, GLsizei, GLboolean, const GLfloat*);
static void (*fVertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*);
static void (*fEnableVertexAttribArray)(GLuint);
static void (*fDisableVertexAttribArray)(GLuint);
static void (*fBindAttribLocation)(GLuint, GLuint, const char*);

static void (*fGenFramebuffers)(GLsizei, GLuint*);
static void (*fBindFramebuffer)(GLenum, GLuint);
static void (*fFramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint);
static GLenum (*fCheckFramebufferStatus)(GLenum);

static void load_gl(void) {
    LOAD(fClearColor, "glClearColor");
    LOAD(fClear, "glClear");
    LOAD(fFinish, "glFinish");
    LOAD(fViewport, "glViewport");
    LOAD(fBegin, "glBegin");
    LOAD(fEnd, "glEnd");
    LOAD(fVertex3f, "glVertex3f");
    LOAD(fColor4f, "glColor4f");
    LOAD(fTexCoord2f, "glTexCoord2f");
    LOAD(fPushMatrix, "glPushMatrix");
    LOAD(fPopMatrix, "glPopMatrix");
    LOAD(fTranslatef, "glTranslatef");
    LOAD(fRotatef, "glRotatef");
    LOAD(fMatrixMode, "glMatrixMode");
    LOAD(fLoadIdentity, "glLoadIdentity");
    LOAD(fGetFloatv, "glGetFloatv");
    LOAD(fEnable, "glEnable");
    LOAD(fDisable, "glDisable");
    LOAD(fEnableClientState, "glEnableClientState");
    LOAD(fDisableClientState, "glDisableClientState");
    LOAD(fVertexPointer, "glVertexPointer");
    LOAD(fColorPointer, "glColorPointer");
    LOAD(fTexCoordPointer, "glTexCoordPointer");
    LOAD(fClientActiveTexture, "glClientActiveTexture");
    LOAD(fMultMatrixf, "glMultMatrixf");
    LOAD(fFogf, "glFogf");
    LOAD(fFogfv, "glFogfv");
    LOAD(fGenBuffers, "glGenBuffers");
    LOAD(fBindBuffer, "glBindBuffer");
    LOAD(fBufferData, "glBufferData");
    LOAD(fDrawArrays, "glDrawArrays");
    LOAD(fDrawElements, "glDrawElements");
    LOAD(fGenLists, "glGenLists");
    LOAD(fNewList, "glNewList");
    LOAD(fEndList, "glEndList");
    LOAD(fCallList, "glCallList");
    LOAD(fGenTextures, "glGenTextures");
    LOAD(fBindTexture, "glBindTexture");
    LOAD(fTexImage2D, "glTexImage2D");
    LOAD(fTexSubImage2D, "glTexSubImage2D");
    LOAD(fTexParameteri, "glTexParameteri");
    LOAD(fReadPixels, "glReadPixels");
    LOAD(fGetString, "glGetString");

    // Optional: missing ones disable their phase, not the run. The EXT
    // spellings are the FBO ones - GL 2.1 promoted them, but a library may
    // only ever have exported the ARB/EXT names.
#define LOAD_OPT(v, n) *(void**)(&v) = sym(n)
#define LOAD_OPT2(v, n, alt)                                                                       \
    do {                                                                                           \
        *(void**)(&v) = sym(n);                                                                    \
        if (!v) *(void**)(&v) = sym(alt);                                                          \
    } while (0)
    LOAD_OPT(fLightf, "glLightf");
    LOAD_OPT(fLightfv, "glLightfv");
    LOAD_OPT(fLightModelfv, "glLightModelfv");
    LOAD_OPT(fLightModeli, "glLightModeli");
    LOAD_OPT(fMaterialf, "glMaterialf");
    LOAD_OPT(fMaterialfv, "glMaterialfv");
    LOAD_OPT(fColorMaterial, "glColorMaterial");
    LOAD_OPT(fNormal3f, "glNormal3f");
    LOAD_OPT(fNormalPointer, "glNormalPointer");
    LOAD_OPT(fShadeModel, "glShadeModel");

    LOAD_OPT2(fActiveTexture, "glActiveTexture", "glActiveTextureARB");
    LOAD_OPT2(fMultiTexCoord2f, "glMultiTexCoord2f", "glMultiTexCoord2fARB");
    LOAD_OPT(fTexEnvi, "glTexEnvi");
    LOAD_OPT(fTexEnvfv, "glTexEnvfv");
    LOAD_OPT(fTexGeni, "glTexGeni");

    LOAD_OPT(fPushAttrib, "glPushAttrib");
    LOAD_OPT(fPopAttrib, "glPopAttrib");
    LOAD_OPT(fBlendFunc, "glBlendFunc");
    LOAD_OPT(fDepthFunc, "glDepthFunc");
    LOAD_OPT(fAlphaFunc, "glAlphaFunc");
    LOAD_OPT(fStencilFunc, "glStencilFunc");
    LOAD_OPT(fStencilOp, "glStencilOp");
    LOAD_OPT(fScissor, "glScissor");
    LOAD_OPT(fColorMask, "glColorMask");
    LOAD_OPT(fDepthMask, "glDepthMask");
    LOAD_OPT(fPolygonOffset, "glPolygonOffset");
    LOAD_OPT(fLineWidth, "glLineWidth");
    LOAD_OPT(fCullFace, "glCullFace");
    LOAD_OPT(fFrontFace, "glFrontFace");

    LOAD_OPT(fCreateShader, "glCreateShader");
    LOAD_OPT(fShaderSource, "glShaderSource");
    LOAD_OPT(fCompileShader, "glCompileShader");
    LOAD_OPT(fGetShaderiv, "glGetShaderiv");
    LOAD_OPT(fGetShaderInfoLog, "glGetShaderInfoLog");
    LOAD_OPT(fDeleteShader, "glDeleteShader");
    LOAD_OPT(fCreateProgram, "glCreateProgram");
    LOAD_OPT(fAttachShader, "glAttachShader");
    LOAD_OPT(fLinkProgram, "glLinkProgram");
    LOAD_OPT(fGetProgramiv, "glGetProgramiv");
    LOAD_OPT(fGetProgramInfoLog, "glGetProgramInfoLog");
    LOAD_OPT(fUseProgram, "glUseProgram");
    LOAD_OPT(fDeleteProgram, "glDeleteProgram");
    LOAD_OPT(fGetUniformLocation, "glGetUniformLocation");
    LOAD_OPT(fGetAttribLocation, "glGetAttribLocation");
    LOAD_OPT(fUniform1i, "glUniform1i");
    LOAD_OPT(fUniform1f, "glUniform1f");
    LOAD_OPT(fUniform3fv, "glUniform3fv");
    LOAD_OPT(fUniform4fv, "glUniform4fv");
    LOAD_OPT(fUniformMatrix4fv, "glUniformMatrix4fv");
    LOAD_OPT(fVertexAttribPointer, "glVertexAttribPointer");
    LOAD_OPT(fEnableVertexAttribArray, "glEnableVertexAttribArray");
    LOAD_OPT(fDisableVertexAttribArray, "glDisableVertexAttribArray");
    LOAD_OPT(fBindAttribLocation, "glBindAttribLocation");

    LOAD_OPT2(fGenFramebuffers, "glGenFramebuffers", "glGenFramebuffersEXT");
    LOAD_OPT2(fBindFramebuffer, "glBindFramebuffer", "glBindFramebufferEXT");
    LOAD_OPT2(fFramebufferTexture2D, "glFramebufferTexture2D", "glFramebufferTexture2DEXT");
    LOAD_OPT2(fCheckFramebufferStatus, "glCheckFramebufferStatus", "glCheckFramebufferStatusEXT");
#undef LOAD_OPT
#undef LOAD_OPT2
}

/* ---------------- context creation: EGL or GLX, via the library ------- */
typedef void* EGLDisplay;
typedef void* EGLSurface;
typedef void* EGLContext;
typedef void* EGLConfig;
typedef int EGLint;
typedef unsigned int EGLBoolean;

static int make_context_egl(void) {
    void* egl = dlopen("libEGL.so.1", RTLD_NOW | RTLD_GLOBAL);
    if (!egl) { fprintf(stderr, "no libEGL: %s\n", dlerror()); return 0; }
    EGLDisplay (*getDisplay)(void*) = dlsym(egl, "eglGetDisplay");
    EGLBoolean (*initialize)(EGLDisplay, EGLint*, EGLint*) = dlsym(egl, "eglInitialize");
    EGLBoolean (*chooseConfig)(EGLDisplay, const EGLint*, EGLConfig*, EGLint, EGLint*) =
        dlsym(egl, "eglChooseConfig");
    EGLSurface (*createPbuffer)(EGLDisplay, EGLConfig, const EGLint*) =
        dlsym(egl, "eglCreatePbufferSurface");
    EGLBoolean (*bindAPI)(EGLint) = dlsym(egl, "eglBindAPI");
    EGLContext (*createContext)(EGLDisplay, EGLConfig, EGLContext, const EGLint*) =
        dlsym(egl, "eglCreateContext");
    EGLBoolean (*makeCurrentReal)(EGLDisplay, EGLSurface, EGLSurface, EGLContext) =
        dlsym(egl, "eglMakeCurrent");
    if (!getDisplay || !initialize || !chooseConfig || !createPbuffer || !createContext ||
        !makeCurrentReal)
        return 0;

    // EGL_DEFAULT_DISPLAY resolves through the session's display server, so a
    // run without access to one (a different user, a headless box) either
    // fails outright or lands on a software rasterizer - and then the numbers
    // measure llvmpipe, not the GPU. GLBENCH_EGL_DEVICE=<index> selects a GPU
    // directly through EGL_EXT_platform_device, which needs no display server;
    // the index is the eglQueryDevicesEXT order.
    EGLDisplay d = (EGLDisplay)0;
    const char* device_sel = getenv("GLBENCH_EGL_DEVICE");
    if (device_sel != NULL && device_sel[0] != '\0') {
        void* (*getProc)(const char*) = dlsym(egl, "eglGetProcAddress");
        EGLBoolean (*queryDevices)(EGLint, void**, EGLint*) =
            getProc ? (EGLBoolean(*)(EGLint, void**, EGLint*))getProc("eglQueryDevicesEXT") : 0;
        EGLDisplay (*getPlatformDisplay)(unsigned int, void*, const EGLint*) =
            getProc ? (EGLDisplay(*)(unsigned int, void*, const EGLint*))getProc(
                          "eglGetPlatformDisplayEXT")
                    : 0;
        void* devices[16];
        EGLint count = 0;
        const int want = atoi(device_sel);
        if (queryDevices && getPlatformDisplay && queryDevices(16, devices, &count) && count > 0 &&
            want >= 0 && want < count) {
            d = getPlatformDisplay(0x313F /*PLATFORM_DEVICE_EXT*/, devices[want], NULL);
        }
        if (!d) {
            fprintf(stderr, "GLBENCH_EGL_DEVICE=%s: no such EGL device (found %d)\n", device_sel,
                    (int)count);
            return 0;
        }
    }
    if (!d) d = getDisplay((void*)0);
    if (!initialize(d, NULL, NULL)) return 0;
    const EGLint ca[] = {0x3033 /*SURFACE_TYPE*/, 0x0001 /*PBUFFER*/,
                         0x3040 /*RENDERABLE_TYPE*/, 0x0040 /*ES3*/,
                         0x3024, 8, 0x3023, 8, 0x3022, 8, 0x3021, 8, 0x3038};
    EGLConfig c;
    EGLint n = 0;
    if (!chooseConfig(d, ca, &c, 1, &n) || n == 0) return 0;
    const EGLint pa[] = {0x3057 /*WIDTH*/, 256, 0x3056 /*HEIGHT*/, 256, 0x3038};
    EGLSurface s = createPbuffer(d, c, pa);
    if (bindAPI) bindAPI(0x30A0 /*OPENGL_ES_API*/);
    const EGLint xa[] = {0x3098 /*CONTEXT_CLIENT_VERSION*/, 3, 0x3038};
    EGLContext x = createContext(d, c, NULL, xa);
    if (!s || !x) return 0;
    // Prefer the library's own eglMakeCurrent when it exports one: that is
    // how a launcher wires EGL through the wrapper.
    EGLBoolean (*makeCurrent)(EGLDisplay, EGLSurface, EGLSurface, EGLContext) =
        (EGLBoolean(*)(EGLDisplay, EGLSurface, EGLSurface, EGLContext))dlsym(lib,
                                                                            "eglMakeCurrent");
    const char* routed = "library";
    if (!makeCurrent) { makeCurrent = makeCurrentReal; routed = "libEGL directly"; }
    if (!makeCurrent(d, s, s, x)) return 0;
    if (!tsv_mode) printf("# context: EGL pbuffer 256x256, eglMakeCurrent via %s\n", routed);
    return 1;
}

static int make_context_glx(void) {
    void* xlib = dlopen("libX11.so.6", RTLD_NOW | RTLD_GLOBAL);
    if (!xlib) { fprintf(stderr, "no libX11\n"); return 0; }
    void* (*XOpenDisplay)(const char*) = dlsym(xlib, "XOpenDisplay");
    unsigned long (*XDefaultRootWindow)(void*) = dlsym(xlib, "XDefaultRootWindow");
    if (!XOpenDisplay) return 0;
    void* dpy = XOpenDisplay(NULL);
    if (!dpy) { fprintf(stderr, "cannot open X display\n"); return 0; }

    // GLX comes from the library under test (gl4es exports glX*).
    void* (*ChooseVisual)(void*, int, int*) = sym("glXChooseVisual");
    void* (*CreateContext)(void*, void*, void*, int) = sym("glXCreateContext");
    int (*MakeCurrent)(void*, unsigned long, void*) = sym("glXMakeCurrent");
    void* (*CreatePbuffer)(void*, void*, const int*) = sym("glXCreatePbuffer");
    if (!ChooseVisual || !CreateContext || !MakeCurrent) {
        fprintf(stderr, "library exports no usable GLX\n");
        return 0;
    }
    int attribs[] = {4 /*GLX_RGBA*/, 8 /*RED_SIZE*/, 8, 9 /*GREEN*/, 8, 10 /*BLUE*/, 8,
                     12 /*DEPTH*/, 16, 0 /*None*/};
    void* vi = ChooseVisual(dpy, 0, attribs);
    if (!vi) { fprintf(stderr, "glXChooseVisual failed\n"); return 0; }
    void* ctx = CreateContext(dpy, vi, NULL, 1);
    if (!ctx) { fprintf(stderr, "glXCreateContext failed\n"); return 0; }
    const unsigned long root = XDefaultRootWindow ? XDefaultRootWindow(dpy) : 0;
    if (!MakeCurrent(dpy, root, ctx)) {
        fprintf(stderr, "glXMakeCurrent failed\n");
        return 0;
    }
    if (!tsv_mode) printf("# context: GLX on root window, via the library's own GLX\n");
    return 1;
}

/* ------------------------------- workloads ---------------------------- */
static void draw_quads_immediate(int quads) {
    fBegin(GL_QUADS);
    for (int q = 0; q < quads; ++q) {
        const GLfloat x = (GLfloat)(q % 32) / 32.0f - 0.5f;
        const GLfloat y = (GLfloat)((q / 32) % 32) / 32.0f - 0.5f;
        for (int v = 0; v < 4; ++v) {
            fColor4f(0.5f, 0.6f, 0.7f, 1.0f);
            fTexCoord2f((GLfloat)(v & 1), (GLfloat)((v >> 1) & 1));
            fVertex3f(x + (v == 1 || v == 2 ? 0.02f : 0.0f),
                      y + (v >= 2 ? 0.02f : 0.0f), 0.0f);
        }
    }
    fEnd();
}

#define CA_QUADS 1000
#define CA_VERTS (CA_QUADS * 4)
static GLfloat interleaved[CA_VERTS * 9];
static unsigned short indices[CA_QUADS * 6];

// One model of the lighting phase. Split out so the phase can run it
// untimed first: fixed-function state maps to a generated shader, and
// letting every permutation this loop reaches compile once before the
// clock starts is what keeps the reported number a per-draw steady-state
// cost instead of a figure that shrinks as the iteration count grows.
static void lighting_draw_model(int m) {
    const GLfloat f = 0.001f * (GLfloat)(m & 255);
    const GLfloat emission[4] = {f, 0.0f, 0.0f, 1.0f};
    const GLfloat ambient[4] = {0.2f + f, 0.2f, 0.2f, 1.0f};
    const GLfloat diffuse[4] = {0.8f, 0.7f - f, 0.6f, 1.0f};
    const GLfloat specular[4] = {0.9f, 0.9f, 0.9f, 1.0f};
    fMaterialfv(GL_FRONT, GL_EMISSION, emission);
    fMaterialfv(GL_FRONT, GL_AMBIENT, ambient);
    fMaterialfv(GL_FRONT, GL_DIFFUSE, diffuse);
    fMaterialfv(GL_FRONT, GL_SPECULAR, specular);
    fMaterialf(GL_FRONT, GL_SHININESS, 16.0f + f);
    // Every 16th model flips the lighting state a fixed-function shader has
    // to be regenerated (or reselected) for; the rest only move uniforms.
    if ((m & 15) == 0) {
        fShadeModel((m & 16) ? GL_FLAT : GL_SMOOTH);
        fLightModeli(GL_LIGHT_MODEL_TWO_SIDE, (m & 32) ? 1 : 0);
        if ((m & 32)) fEnable(GL_COLOR_MATERIAL); else fDisable(GL_COLOR_MATERIAL);
    }
    fBegin(GL_QUADS);
    for (int q = 0; q < 8; ++q) {
        const GLfloat x = 0.01f * (GLfloat)q;
        fNormal3f(0.0f, 0.0f, 1.0f);
        fColor4f(0.5f, 0.6f, 0.7f, 1.0f);
        fVertex3f(x, 0.0f, 0.0f);
        fNormal3f(0.0f, 0.577f, 0.816f);
        fVertex3f(x + 0.01f, 0.0f, 0.0f);
        fNormal3f(0.577f, 0.577f, 0.577f);
        fVertex3f(x + 0.01f, 0.01f, 0.0f);
        fNormal3f(0.0f, 0.0f, 1.0f);
        fVertex3f(x, 0.01f, 0.0f);
    }
    fEnd();
}

// One draw of the texture-stage phase, split out for the same reason: the
// environment/texgen combinations it cycles through are shader inputs.
static void texstages_draw(int d, int units) {
    const GLfloat envColor[4] = {0.3f, 0.4f, 0.5f, 1.0f};
    for (int u = 0; u < units; ++u) {
        fActiveTexture(GL_TEXTURE0_C + u);
        // Rotate through the three environments a legacy renderer actually
        // mixes; COMBINE additionally carries source and operand state.
        const int mode = (d + u) % 3;
        if (mode == 0) {
            fTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        } else if (mode == 1) {
            fTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_ADD);
        } else {
            fTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
            fTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_INTERPOLATE);
            fTexEnvi(GL_TEXTURE_ENV, GL_SRC0_RGB, GL_TEXTURE_C);
            fTexEnvi(GL_TEXTURE_ENV, GL_SRC1_RGB, GL_PREVIOUS);
            fTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_RGB, GL_SRC_COLOR);
        }
        fTexEnvfv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR, envColor);
        // A per-unit texture matrix: another generated-shader input.
        fMatrixMode(GL_TEXTURE_C);
        fLoadIdentity();
        fTranslatef(0.001f * (GLfloat)d, 0.0f, 0.0f);
    }
    fMatrixMode(GL_MODELVIEW);
    if (fTexGeni && (d & 63) == 0) {
        fActiveTexture(GL_TEXTURE0_C + (units - 1));
        fTexGeni(GL_S, GL_TEXTURE_GEN_MODE, (d & 64) ? GL_SPHERE_MAP : GL_OBJECT_LINEAR);
        fTexGeni(GL_T, GL_TEXTURE_GEN_MODE, (d & 64) ? GL_SPHERE_MAP : GL_OBJECT_LINEAR);
        if ((d & 64)) { fEnable(GL_TEXTURE_GEN_S); fEnable(GL_TEXTURE_GEN_T); }
        else { fDisable(GL_TEXTURE_GEN_S); fDisable(GL_TEXTURE_GEN_T); }
    }
    fBegin(GL_QUADS);
    for (int v = 0; v < 4; ++v) {
        for (int u = 0; u < units; ++u)
            fMultiTexCoord2f(GL_TEXTURE0_C + u, (GLfloat)(v & 1), (GLfloat)((v >> 1) & 1));
        fColor4f(0.5f, 0.6f, 0.7f, 1.0f);
        fVertex3f((GLfloat)(v & 1) * 0.02f, (GLfloat)((v >> 1) & 1) * 0.02f, 0.0f);
    }
    fEnd();
}

int main(void) {
    const char* path = getenv("CMPBENCH_LIB");
    if (!path) { fprintf(stderr, "set CMPBENCH_LIB\n"); return 2; }
    only = getenv("CMPBENCH_ONLY");
    tsv_mode = getenv("CMPBENCH_TSV") != NULL;
    double scale = 1.0;
    if (getenv("CMPBENCH_SCALE")) scale = atof(getenv("CMPBENCH_SCALE"));
    if (scale <= 0.0) scale = 1.0;

    lib = dlopen(path, RTLD_NOW | RTLD_GLOBAL);
    if (!lib) { fprintf(stderr, "dlopen %s: %s\n", path, dlerror()); return 2; }
    *(void**)(&lib_get_proc) = dlsym(lib, "eglGetProcAddress");
    backend_gles = dlopen("libGLESv2.so.2", RTLD_NOW | RTLD_GLOBAL);

    const char* ctx = getenv("CMPBENCH_CTX");
    const int ok = (ctx && strcmp(ctx, "glx") == 0) ? make_context_glx() : make_context_egl();
    if (!ok) { fprintf(stderr, "SKIP: no context\n"); return 77; }

    load_gl();
    if (missing) { fprintf(stderr, "SKIP: %d entry points missing\n", missing); return 77; }
    const GLubyte* ver = fGetString ? fGetString(0x1F02) : NULL;
    if (!tsv_mode) printf("# GL_VERSION: %s\n", ver ? (const char*)ver : "(null)");

    // A wrapper comparison is about CPU overhead per GL call, so the
    // default viewport is 1x1: rasterization then costs ~nothing and the
    // two libraries' different render targets stop mattering. Set
    // CMPBENCH_VIEWPORT=256 to include fragment work.
    int vp = 1;
    if (getenv("CMPBENCH_VIEWPORT")) vp = atoi(getenv("CMPBENCH_VIEWPORT"));
    if (vp <= 0) vp = 1;
    if (!tsv_mode) printf("# GL_RENDERER: %s\n", fGetString ? (const char*)fGetString(0x1F01) : "(null)");
    if (!tsv_mode) printf("# viewport: %dx%d\n", vp, vp);
    fViewport(0, 0, vp, vp);
    fClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    fClear(GL_COLOR_BUFFER_BIT);
    fMatrixMode(GL_PROJECTION);
    fLoadIdentity();
    fMatrixMode(GL_MODELVIEW);
    fLoadIdentity();

    double t0, ms;

    /* immediate mode */
    if (phase_on("immediate")) {
        const int frames = (int)(200 * scale) > 0 ? (int)(200 * scale) : 1;
        draw_quads_immediate(1000);
        fFinish();
        t0 = now_ms();
        for (int f = 0; f < frames; ++f) draw_quads_immediate(1000);
        fFinish();
        ms = now_ms() - t0;
        report("immediate", ms * 1.0e6 / ((double)frames * 4000.0), "ns/vert");
    }

    /* display list replay */
    if (phase_on("dlist")) {
        const int frames = (int)(200 * scale) > 0 ? (int)(200 * scale) : 1;
        GLuint list = fGenLists(1);
        fNewList(list, GL_COMPILE);
        draw_quads_immediate(1000);
        fEndList();
        fCallList(list);
        fFinish();
        t0 = now_ms();
        for (int f = 0; f < frames; ++f) fCallList(list);
        fFinish();
        ms = now_ms() - t0;
        report("dlist", ms * 1.0e6 / ((double)frames * 4000.0), "ns/vert");
    }

    /* client arrays */
    if (phase_on("clientarrays") || phase_on("drawelements")) {
        for (int v = 0; v < CA_VERTS; ++v) {
            GLfloat* p = interleaved + v * 9;
            p[0] = (GLfloat)(v % 64) / 64.0f - 0.5f;
            p[1] = (GLfloat)((v / 64) % 64) / 64.0f - 0.5f;
            p[2] = 0.0f;
            p[3] = 0.5f; p[4] = 0.6f; p[5] = 0.7f; p[6] = 1.0f;
            p[7] = (GLfloat)(v & 1); p[8] = (GLfloat)((v >> 1) & 1);
        }
        for (int q = 0; q < CA_QUADS; ++q) {
            const unsigned short b = (unsigned short)(q * 4);
            indices[q * 6 + 0] = b; indices[q * 6 + 1] = (unsigned short)(b + 1);
            indices[q * 6 + 2] = (unsigned short)(b + 2);
            indices[q * 6 + 3] = (unsigned short)(b + 2);
            indices[q * 6 + 4] = (unsigned short)(b + 3); indices[q * 6 + 5] = b;
        }
        fEnableClientState(GL_VERTEX_ARRAY);
        fEnableClientState(GL_COLOR_ARRAY);
        fEnableClientState(GL_TEXTURE_COORD_ARRAY);
        fVertexPointer(3, GL_FLOAT, 9 * sizeof(GLfloat), interleaved);
        fColorPointer(4, GL_FLOAT, 9 * sizeof(GLfloat), interleaved + 3);
        fTexCoordPointer(2, GL_FLOAT, 9 * sizeof(GLfloat), interleaved + 7);
        const int frames = (int)(200 * scale) > 0 ? (int)(200 * scale) : 1;

        if (phase_on("clientarrays")) {
            fDrawArrays(GL_QUADS, 0, CA_VERTS);
            fFinish();
            t0 = now_ms();
            for (int f = 0; f < frames; ++f) fDrawArrays(GL_QUADS, 0, CA_VERTS);
            fFinish();
            ms = now_ms() - t0;
            report("clientarrays", ms * 1.0e6 / ((double)frames * CA_VERTS), "ns/vert");
        }
        if (phase_on("drawelements")) {
            fDrawElements(GL_TRIANGLES, CA_QUADS * 6, GL_UNSIGNED_SHORT, indices);
            fFinish();
            t0 = now_ms();
            for (int f = 0; f < frames; ++f)
                fDrawElements(GL_TRIANGLES, CA_QUADS * 6, GL_UNSIGNED_SHORT, indices);
            fFinish();
            ms = now_ms() - t0;
            report("drawelements", ms * 1.0e6 / ((double)frames * CA_QUADS * 6), "ns/idx");
        }
        fDisableClientState(GL_VERTEX_ARRAY);
        fDisableClientState(GL_COLOR_ARRAY);
        fDisableClientState(GL_TEXTURE_COORD_ARRAY);
    }

    /* tiny batches: per-draw fixed cost */
    if (phase_on("tinybatch")) {
        const int batches = (int)(20000 * scale) > 0 ? (int)(20000 * scale) : 1;
        draw_quads_immediate(1);
        fFinish();
        t0 = now_ms();
        for (int i = 0; i < batches; ++i) draw_quads_immediate(1);
        fFinish();
        ms = now_ms() - t0;
        report("tinybatch", ms * 1000.0 / batches, "us/batch");
    }

    /* state toggling: shader/program cache pressure */
    if (phase_on("progtoggle")) {
        const int draws = (int)(10000 * scale) > 0 ? (int)(10000 * scale) : 1;
        t0 = now_ms();
        for (int i = 0; i < draws; ++i) {
            if (i & 1) fEnable(GL_LIGHTING); else fDisable(GL_LIGHTING);
            draw_quads_immediate(1);
        }
        fFinish();
        ms = now_ms() - t0;
        fDisable(GL_LIGHTING);
        report("progtoggle", ms * 1000.0 / draws, "us/draw");
    }

    /* pure CPU: matrix stack */
    if (phase_on("matrixops")) {
        const int ops = (int)(200000 * scale) > 0 ? (int)(200000 * scale) : 1;
        t0 = now_ms();
        for (int i = 0; i < ops; ++i) {
            fPushMatrix();
            fTranslatef(0.1f, 0.2f, 0.3f);
            fRotatef(17.0f, 0.0f, 1.0f, 0.0f);
            fPopMatrix();
        }
        ms = now_ms() - t0;
        report("matrixops", ms * 1.0e6 / ops, "ns/group");
    }

    /* pure CPU: getter */
    if (phase_on("getter")) {
        const int gets = (int)(500000 * scale) > 0 ? (int)(500000 * scale) : 1;
        GLfloat m[16];
        t0 = now_ms();
        for (int i = 0; i < gets; ++i) fGetFloatv(GL_MODELVIEW_MATRIX, m);
        ms = now_ms() - t0;
        report("getter", ms * 1.0e6 / gets, "ns/call");
    }

    /* texture switching */
    if (phase_on("texswitch")) {
        static GLubyte texels[64 * 64 * 4];
        for (int i = 0; i < 64 * 64 * 4; ++i) texels[i] = (GLubyte)(i * 7);
        GLuint tex[2];
        fGenTextures(2, tex);
        for (int i = 0; i < 2; ++i) {
            fBindTexture(GL_TEXTURE_2D, tex[i]);
            fTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 64, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, texels);
            fTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            fTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        }
        fEnable(GL_TEXTURE_2D);
        const int draws = (int)(10000 * scale) > 0 ? (int)(10000 * scale) : 1;
        t0 = now_ms();
        for (int i = 0; i < draws; ++i) {
            fBindTexture(GL_TEXTURE_2D, tex[i & 1]);
            draw_quads_immediate(1);
        }
        fFinish();
        ms = now_ms() - t0;
        fDisable(GL_TEXTURE_2D);
        report("texswitch", ms * 1000.0 / draws, "us/draw");
    }

    // --- Minecraft-shaped phases ------------------------------------------
    // Reconstructed GL 1.x call sequences from the 1.12/1.16 RenderDoc
    // captures plus the known vanilla vertex formats; see
    // plans/12-fpe-draw-cost.md. Only GL 1.x calls, so any library here runs
    // exactly the same workload.
    if (phase_on("mcchunk") || phase_on("mcchunkmulti")) {
        // DefaultVertexFormats.BLOCK: 3f position, color, 2f uv, lightmap.
        // Declared all-float so one array serves every library; the 28-byte
        // stride is what per-draw cost turns on.
        enum { MC_STRIDE_F = 7, MC_CHUNK_V = 400, MC_CHUNKS = 16 };
        static GLfloat mc[MC_CHUNK_V * MC_CHUNKS * MC_STRIDE_F];
        for (int v = 0; v < MC_CHUNK_V * MC_CHUNKS; ++v) {
            const int corner = v % 4;
            const GLfloat cell = 0.01f * (GLfloat)((v / 4) % 100) - 0.5f;
            GLfloat* d = &mc[v * MC_STRIDE_F];
            d[0] = cell + ((corner == 1 || corner == 2) ? 0.01f : 0.0f);
            d[1] = cell + ((corner >= 2) ? 0.01f : 0.0f);
            d[2] = 0.0f;
            d[3] = 0.8f;
            d[4] = (GLfloat)(corner & 1);
            d[5] = (GLfloat)(corner >> 1);
            d[6] = 0.5f;
        }
        const GLsizei mcstride = MC_STRIDE_F * (GLsizei)sizeof(GLfloat);
        fEnableClientState(GL_VERTEX_ARRAY);
        fEnableClientState(GL_COLOR_ARRAY);
        fEnableClientState(GL_TEXTURE_COORD_ARRAY);
        fVertexPointer(3, GL_FLOAT, mcstride, mc);
        fColorPointer(3, GL_FLOAT, mcstride, mc + 3);
        fTexCoordPointer(2, GL_FLOAT, mcstride, mc + 4);
        const int mcframes = (int)(120 * scale) > 0 ? (int)(120 * scale) : 1;
        const double mcverts = (double)mcframes * (MC_CHUNK_V * MC_CHUNKS);

        if (phase_on("mcchunk") && !phase_on("mcchunkmulti")) {
            fDrawArrays(GL_QUADS, 0, MC_CHUNK_V * MC_CHUNKS);
            fFinish();
            t0 = now_ms();
            for (int f = 0; f < mcframes; ++f) {
                fPushMatrix();
                fTranslatef(0.001f * (GLfloat)f, 0.0f, 0.0f);
                fDrawArrays(GL_QUADS, 0, MC_CHUNK_V * MC_CHUNKS);
                fPopMatrix();
            }
            fFinish();
            ms = now_ms() - t0;
            report("mcchunk", ms * 1.0e6 / mcverts, "ns/vert");
        }
        if (phase_on("mcchunkmulti")) {
            // The real frame shape: many small chunk draws, each with its own
            // matrix, so per-draw fixed cost dominates over vertex rate.
            fDrawArrays(GL_QUADS, 0, MC_CHUNK_V);
            fFinish();
            t0 = now_ms();
            for (int f = 0; f < mcframes; ++f) {
                for (int c = 0; c < MC_CHUNKS; ++c) {
                    fPushMatrix();
                    fTranslatef(0.001f * (GLfloat)c, 0.0f, 0.0f);
                    fDrawArrays(GL_QUADS, c * MC_CHUNK_V, MC_CHUNK_V);
                    fPopMatrix();
                }
            }
            fFinish();
            ms = now_ms() - t0;
            report("mcchunkmulti", ms * 1000.0 / ((double)mcframes * MC_CHUNKS), "us/chunk");
        }
        fDisableClientState(GL_VERTEX_ARRAY);
        fDisableClientState(GL_COLOR_ARRAY);
        fDisableClientState(GL_TEXTURE_COORD_ARRAY);
    }

    // --- MC 1.12 chunk pipelines, reconstructed from
    // RDC/Minecraft/1.12-Optifine/chunk.rdc (VBOs on) and
    // chunk-without-vbo.rdc (VBOs off). Both captures show ~950 draws/frame,
    // median 416 vertices per chunk pass, the 28-byte BLOCK vertex (3f pos,
    // 4ub color, 2f uv, 2s lightmap) and a per-chunk matrix. The difference
    // is purely how the vertices reach GL:
    //   VBOs on : per chunk - bind ITS OWN VBO, respecify all four pointers
    //             as buffer offsets (lightmap via glClientActiveTexture),
    //             glDrawArrays(GL_QUADS).
    //   VBOs off: per chunk - glCallList of a list baked at chunk-build time
    //             (client-array pointers + the draw live inside the list).
    if (phase_on("mcchunkvbo") || phase_on("mcchunkdlist")) {
        enum { CK_VERTS = 416, CK_CHUNKS = 16, CK_STRIDE = 28 };
        static unsigned char ck[CK_VERTS * CK_STRIDE];
        for (int v = 0; v < CK_VERTS; ++v) {
            unsigned char* d = ck + v * CK_STRIDE;
            float* pos = (float*)d;
            const int corner = v % 4;
            const float cell = 0.004f * (float)((v / 4) % 100) - 0.5f;
            pos[0] = cell + ((corner == 1 || corner == 2) ? 0.004f : 0.0f);
            pos[1] = cell + ((corner >= 2) ? 0.004f : 0.0f);
            pos[2] = 0.0f;
            d[12] = 200; d[13] = 200; d[14] = 200; d[15] = 255;   // color 4ub
            float* uv = (float*)(d + 16);
            uv[0] = (float)(corner & 1); uv[1] = (float)(corner >> 1);
            short* lm = (short*)(d + 24);
            lm[0] = 240; lm[1] = 240;
        }
        const int ckframes = (int)(120 * scale) > 0 ? (int)(120 * scale) : 1;
        const double ckdraws = (double)ckframes * CK_CHUNKS;

        if (phase_on("mcchunkvbo") && fGenBuffers && fBindBuffer && fBufferData &&
            fClientActiveTexture) {
            GLuint vbos[CK_CHUNKS];
            fGenBuffers(CK_CHUNKS, vbos);
            for (int c = 0; c < CK_CHUNKS; ++c) {
                fBindBuffer(GL_ARRAY_BUFFER, vbos[c]);
                fBufferData(GL_ARRAY_BUFFER, (long)sizeof ck, ck, GL_STATIC_DRAW);
            }
            fEnableClientState(GL_VERTEX_ARRAY);
            fEnableClientState(GL_COLOR_ARRAY);
            fEnableClientState(GL_TEXTURE_COORD_ARRAY);
            fClientActiveTexture(GL_TEXTURE1_C);
            fEnableClientState(GL_TEXTURE_COORD_ARRAY);
            fClientActiveTexture(GL_TEXTURE0_C);
            fFinish();
            t0 = now_ms();
            for (int f = 0; f < ckframes; ++f) {
                for (int c = 0; c < CK_CHUNKS; ++c) {
                    fPushMatrix();
                    fTranslatef(0.001f * (GLfloat)c, 0.0f, 0.0f);
                    fBindBuffer(GL_ARRAY_BUFFER, vbos[c]);
                    fVertexPointer(3, GL_FLOAT, CK_STRIDE, (const void*)0);
                    fColorPointer(4, GL_UNSIGNED_BYTE_T, CK_STRIDE, (const void*)12);
                    fTexCoordPointer(2, GL_FLOAT, CK_STRIDE, (const void*)16);
                    fClientActiveTexture(GL_TEXTURE1_C);
                    fTexCoordPointer(2, GL_SHORT, CK_STRIDE, (const void*)24);
                    fClientActiveTexture(GL_TEXTURE0_C);
                    fDrawArrays(GL_QUADS, 0, CK_VERTS);
                    fPopMatrix();
                }
            }
            fFinish();
            ms = now_ms() - t0;
            report("mcchunkvbo", ms * 1000.0 / ckdraws, "us/chunk");
            fBindBuffer(GL_ARRAY_BUFFER, 0);
            fClientActiveTexture(GL_TEXTURE1_C);
            fDisableClientState(GL_TEXTURE_COORD_ARRAY);
            fClientActiveTexture(GL_TEXTURE0_C);
            fDisableClientState(GL_VERTEX_ARRAY);
            fDisableClientState(GL_COLOR_ARRAY);
            fDisableClientState(GL_TEXTURE_COORD_ARRAY);
        }

        if (phase_on("mcchunkdlist")) {
            GLuint lists[CK_CHUNKS];
            for (int c = 0; c < CK_CHUNKS; ++c) {
                lists[c] = fGenLists(1);
                fNewList(lists[c], GL_COMPILE);
                fEnableClientState(GL_VERTEX_ARRAY);
                fEnableClientState(GL_COLOR_ARRAY);
                fEnableClientState(GL_TEXTURE_COORD_ARRAY);
                fVertexPointer(3, GL_FLOAT, CK_STRIDE, ck);
                fColorPointer(4, GL_UNSIGNED_BYTE_T, CK_STRIDE, ck + 12);
                fTexCoordPointer(2, GL_FLOAT, CK_STRIDE, ck + 16);
                if (fClientActiveTexture) {
                    fClientActiveTexture(GL_TEXTURE1_C);
                    fEnableClientState(GL_TEXTURE_COORD_ARRAY);
                    fTexCoordPointer(2, GL_SHORT, CK_STRIDE, ck + 24);
                    fClientActiveTexture(GL_TEXTURE0_C);
                }
                fDrawArrays(GL_QUADS, 0, CK_VERTS);
                fDisableClientState(GL_VERTEX_ARRAY);
                fDisableClientState(GL_COLOR_ARRAY);
                fDisableClientState(GL_TEXTURE_COORD_ARRAY);
                if (fClientActiveTexture) {
                    fClientActiveTexture(GL_TEXTURE1_C);
                    fDisableClientState(GL_TEXTURE_COORD_ARRAY);
                    fClientActiveTexture(GL_TEXTURE0_C);
                }
                fEndList();
            }
            fFinish();
            t0 = now_ms();
            for (int f = 0; f < ckframes; ++f) {
                for (int c = 0; c < CK_CHUNKS; ++c) {
                    fPushMatrix();
                    fTranslatef(0.001f * (GLfloat)c, 0.0f, 0.0f);
                    fCallList(lists[c]);
                    fPopMatrix();
                }
            }
            fFinish();
            ms = now_ms() - t0;
            report("mcchunkdlist", ms * 1000.0 / ckdraws, "us/chunk");
        }
    }

    // --- MC 1.16 chunk pipeline, reconstructed from
    // RDC/Minecraft/1.16-Optifine/2-12chunks.rdc: 583 draws/frame at render
    // distance 12, median 608 vertices per chunk pass, the same 28-byte
    // BLOCK vertex as 1.12 - but 1.16's VertexBuffer.draw() replaces the
    // 1.12 glTranslatef with a FULL matrix reload per chunk:
    //   bind chunk VBO, setupBufferState (4 pointers, lightmap on unit 1),
    //   glPushMatrix + glLoadIdentity + glMultMatrixf(chunk pose),
    //   glDrawArrays(GL_QUADS), glPopMatrix.
    // The reload invalidates every matrix-derived uniform each draw, which
    // is what separates this shape from mcchunkvbo.
    if (phase_on("mcchunk116") && fGenBuffers && fBindBuffer && fBufferData &&
        fClientActiveTexture && fMultMatrixf) {
        enum { CK116_VERTS = 608, CK116_CHUNKS = 16, CK116_STRIDE = 28 };
        static unsigned char ck[CK116_VERTS * CK116_STRIDE];
        for (int v = 0; v < CK116_VERTS; ++v) {
            unsigned char* d = ck + v * CK116_STRIDE;
            float* pos = (float*)d;
            const int corner = v % 4;
            const float cell = 0.004f * (float)((v / 4) % 100) - 0.5f;
            pos[0] = cell + ((corner == 1 || corner == 2) ? 0.004f : 0.0f);
            pos[1] = cell + ((corner >= 2) ? 0.004f : 0.0f);
            pos[2] = 0.0f;
            d[12] = 200; d[13] = 200; d[14] = 200; d[15] = 255;
            float* uv = (float*)(d + 16);
            uv[0] = (float)(corner & 1); uv[1] = (float)(corner >> 1);
            short* lm = (short*)(d + 24);
            lm[0] = 240; lm[1] = 240;
        }
        GLuint vbos[CK116_CHUNKS];
        fGenBuffers(CK116_CHUNKS, vbos);
        for (int c = 0; c < CK116_CHUNKS; ++c) {
            fBindBuffer(GL_ARRAY_BUFFER, vbos[c]);
            fBufferData(GL_ARRAY_BUFFER, (long)sizeof ck, ck, GL_STATIC_DRAW);
        }
        fEnableClientState(GL_VERTEX_ARRAY);
        fEnableClientState(GL_COLOR_ARRAY);
        fEnableClientState(GL_TEXTURE_COORD_ARRAY);
        fClientActiveTexture(GL_TEXTURE1_C);
        fEnableClientState(GL_TEXTURE_COORD_ARRAY);
        fClientActiveTexture(GL_TEXTURE0_C);

        // Fog on, exactly like the capture: MC's world passes render with
        // linear fog enabled, which is what makes the generated shader carry
        // ModelViewMat as well as ModelViewProjMat - so every per-chunk
        // matrix reload uploads TWO mat4s, not one. A fogless variant of
        // this phase would miss half the per-draw uniform traffic.
        if (fFogf && fFogfv) {
            static const GLfloat fog_color[4] = {0.7f, 0.8f, 0.9f, 1.0f};
            fFogf(GL_FOG_MODE, (GLfloat)GL_LINEAR_F);
            fFogf(GL_FOG_START, 64.0f);
            fFogf(GL_FOG_END, 192.0f);
            fFogfv(GL_FOG_COLOR, fog_color);
            fEnable(GL_FOG);
        }

        // Per-chunk pose: identity with a small translation, like the
        // camera-relative chunk origin MC bakes into the matrix.
        GLfloat pose[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

        const int ckframes = (int)(120 * scale) > 0 ? (int)(120 * scale) : 1;
        const double ckdraws = (double)ckframes * CK116_CHUNKS;
        fFinish();
        t0 = now_ms();
        for (int f = 0; f < ckframes; ++f) {
            for (int c = 0; c < CK116_CHUNKS; ++c) {
                fBindBuffer(GL_ARRAY_BUFFER, vbos[c]);
                fVertexPointer(3, GL_FLOAT, CK116_STRIDE, (const void*)0);
                fColorPointer(4, GL_UNSIGNED_BYTE_T, CK116_STRIDE, (const void*)12);
                fTexCoordPointer(2, GL_FLOAT, CK116_STRIDE, (const void*)16);
                fClientActiveTexture(GL_TEXTURE1_C);
                fTexCoordPointer(2, GL_SHORT, CK116_STRIDE, (const void*)24);
                fClientActiveTexture(GL_TEXTURE0_C);
                fPushMatrix();
                fLoadIdentity();
                pose[12] = 0.001f * (GLfloat)c;
                pose[13] = 0.0005f * (GLfloat)(f & 7);
                fMultMatrixf(pose);
                fDrawArrays(GL_QUADS, 0, CK116_VERTS);
                fPopMatrix();
            }
        }
        fFinish();
        ms = now_ms() - t0;
        report("mcchunk116", ms * 1000.0 / ckdraws, "us/chunk");
        if (fFogf && fFogfv) fDisable(GL_FOG);
        fBindBuffer(GL_ARRAY_BUFFER, 0);
        fClientActiveTexture(GL_TEXTURE1_C);
        fDisableClientState(GL_TEXTURE_COORD_ARRAY);
        fClientActiveTexture(GL_TEXTURE0_C);
        fDisableClientState(GL_VERTEX_ARRAY);
        fDisableClientState(GL_COLOR_ARRAY);
        fDisableClientState(GL_TEXTURE_COORD_ARRAY);
    }

    if (phase_on("mcgui")) {
        // Gui/GuiIngame: one immediate quad per widget, bracketed by the
        // texture bind and alpha-test toggle the vanilla helpers push and pop.
        GLuint guitex[2] = {0, 0};
        fGenTextures(2, guitex);
        static unsigned char px[16 * 16 * 4];
        for (int i = 0; i < 16 * 16 * 4; ++i) px[i] = (unsigned char)(i * 7);
        for (int i = 0; i < 2; ++i) {
            fBindTexture(GL_TEXTURE_2D, guitex[i]);
            fTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 16, 16, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
            fTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            fTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        }
        fEnable(GL_TEXTURE_2D);
        const int widgets = (int)(4000 * scale) > 0 ? (int)(4000 * scale) : 1;
        fFinish();
        t0 = now_ms();
        for (int w = 0; w < widgets; ++w) {
            fBindTexture(GL_TEXTURE_2D, guitex[w & 1]);
            fDisable(GL_ALPHA_TEST);
            fBegin(GL_QUADS);
            fColor4f(1.0f, 1.0f, 1.0f, 1.0f);
            fTexCoord2f(0.0f, 0.0f); fVertex3f(-0.5f, -0.5f, 0.0f);
            fTexCoord2f(1.0f, 0.0f); fVertex3f(-0.4f, -0.5f, 0.0f);
            fTexCoord2f(1.0f, 1.0f); fVertex3f(-0.4f, -0.4f, 0.0f);
            fTexCoord2f(0.0f, 1.0f); fVertex3f(-0.5f, -0.4f, 0.0f);
            fEnd();
            fEnable(GL_ALPHA_TEST);
        }
        fFinish();
        ms = now_ms() - t0;
        fDisable(GL_ALPHA_TEST);
        fDisable(GL_TEXTURE_2D);
        report("mcgui", ms * 1000.0 / widgets, "us/widget");
    }

    if (phase_on("mcfont")) {
        // FontRenderer: one quad per glyph with a per-glyph color, all inside
        // a single Begin/End for the string.
        const int strings = (int)(2000 * scale) > 0 ? (int)(2000 * scale) : 1;
        enum { GLYPHS = 24 };
        fFinish();
        t0 = now_ms();
        for (int st2 = 0; st2 < strings; ++st2) {
            fBegin(GL_QUADS);
            for (int g = 0; g < GLYPHS; ++g) {
                const GLfloat x = -0.5f + 0.01f * (GLfloat)g;
                fColor4f(1.0f, 1.0f, 1.0f, 0.25f + 0.03f * (GLfloat)(g & 7));
                fTexCoord2f(0.0f, 0.0f); fVertex3f(x, 0.0f, 0.0f);
                fTexCoord2f(1.0f, 0.0f); fVertex3f(x + 0.008f, 0.0f, 0.0f);
                fTexCoord2f(1.0f, 1.0f); fVertex3f(x + 0.008f, 0.01f, 0.0f);
                fTexCoord2f(0.0f, 1.0f); fVertex3f(x, 0.01f, 0.0f);
            }
            fEnd();
        }
        fFinish();
        ms = now_ms() - t0;
        report("mcfont", ms * 1000.0 / strings, "us/string");
    }

    if (phase_on("mcentity")) {
        // ModelRenderer: each box is a compiled display list replayed under
        // its own push/rotate/pop.
        enum { BOXES = 12 };
        GLuint lists[BOXES];
        for (int b = 0; b < BOXES; ++b) {
            lists[b] = fGenLists(1);
            fNewList(lists[b], GL_COMPILE);
            fBegin(GL_QUADS);
            for (int q = 0; q < 6; ++q) {
                const GLfloat c = 0.01f * (GLfloat)q - 0.2f;
                fColor4f(0.5f, 0.6f, 0.7f, 1.0f);
                fTexCoord2f(0.0f, 0.0f); fVertex3f(c, c, 0.0f);
                fTexCoord2f(1.0f, 0.0f); fVertex3f(c + 0.01f, c, 0.0f);
                fTexCoord2f(1.0f, 1.0f); fVertex3f(c + 0.01f, c + 0.01f, 0.0f);
                fTexCoord2f(0.0f, 1.0f); fVertex3f(c, c + 0.01f, 0.0f);
            }
            fEnd();
            fEndList();
        }
        const int models = (int)(2000 * scale) > 0 ? (int)(2000 * scale) : 1;
        fFinish();
        t0 = now_ms();
        for (int m = 0; m < models; ++m) {
            for (int b = 0; b < BOXES; ++b) {
                fPushMatrix();
                fTranslatef(0.0f, 0.01f * (GLfloat)b, 0.0f);
                fRotatef(1.0f * (GLfloat)b, 0.0f, 1.0f, 0.0f);
                fCallList(lists[b]);
                fPopMatrix();
            }
        }
        fFinish();
        ms = now_ms() - t0;
        report("mcentity", ms * 1000.0 / models, "us/model");
    }

    // --- coverage phases ---------------------------------------------------
    // The phases above exercise geometry submission. These five cover the
    // rest of what a GL 2.1 implementation has to implement: the two halves
    // of the fixed-function pipeline that shape shader generation (lighting,
    // texture stages), the per-frame state management around draws, and the
    // programmable pipeline both when building programs and when driving
    // them. Each is guarded on its own entry points, so a library missing
    // one simply has no line for that phase.

    /* FFP lighting: the largest generator of fixed-function shader
     * permutations. Eight lights with full parameter sets, per-vertex
     * normals, per-draw material changes, and shade-model/two-sided
     * toggling - the state a legacy engine changes between model draws. */
    if (phase_on("lighting") && fLightfv && fLightf && fMaterialfv && fMaterialf && fNormal3f &&
        fShadeModel && fColorMaterial && fLightModeli) {
        enum { LIGHTS = 8 };
        for (int l = 0; l < LIGHTS; ++l) {
            const GLfloat f = 0.1f * (GLfloat)l;
            const GLfloat position[4] = {f, 1.0f - f, 2.0f + f, 1.0f};
            const GLfloat ambient[4] = {0.05f + f * 0.01f, 0.05f, 0.05f, 1.0f};
            const GLfloat diffuse[4] = {0.8f - f * 0.05f, 0.7f, 0.6f + f * 0.02f, 1.0f};
            const GLfloat specular[4] = {1.0f, 1.0f, 1.0f, 1.0f};
            const GLfloat direction[3] = {0.0f, -1.0f, f};
            fLightfv(GL_LIGHT0 + l, GL_POSITION, position);
            fLightfv(GL_LIGHT0 + l, GL_AMBIENT, ambient);
            fLightfv(GL_LIGHT0 + l, GL_DIFFUSE, diffuse);
            fLightfv(GL_LIGHT0 + l, GL_SPECULAR, specular);
            fLightfv(GL_LIGHT0 + l, GL_SPOT_DIRECTION, direction);
            fLightf(GL_LIGHT0 + l, GL_SPOT_EXPONENT, 2.0f + f);
            fLightf(GL_LIGHT0 + l, GL_SPOT_CUTOFF, 45.0f);
            fLightf(GL_LIGHT0 + l, GL_CONSTANT_ATTENUATION, 1.0f);
            fLightf(GL_LIGHT0 + l, GL_LINEAR_ATTENUATION, 0.01f);
            fLightf(GL_LIGHT0 + l, GL_QUADRATIC_ATTENUATION, 0.001f);
            fEnable(GL_LIGHT0 + l);
        }
        const GLfloat sceneAmbient[4] = {0.2f, 0.2f, 0.2f, 1.0f};
        if (fLightModelfv) fLightModelfv(GL_LIGHT_MODEL_AMBIENT, sceneAmbient);
        fEnable(GL_LIGHTING);
        fEnable(GL_NORMALIZE);
        fColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);

        const int models = (int)(4000 * scale) > 0 ? (int)(4000 * scale) : 1;
        // 64 models cover every permutation the loop reaches, so all the
        // generated shaders exist before the clock starts.
        for (int m = 0; m < 64; ++m) lighting_draw_model(m);
        fFinish();
        t0 = now_ms();
        for (int m = 0; m < models; ++m) lighting_draw_model(m);
        fFinish();
        ms = now_ms() - t0;
        for (int l = 0; l < LIGHTS; ++l) fDisable(GL_LIGHT0 + l);
        fDisable(GL_LIGHTING);
        fDisable(GL_NORMALIZE);
        fDisable(GL_COLOR_MATERIAL);
        if (fShadeModel) fShadeModel(GL_SMOOTH);
        report("lighting", ms * 1000.0 / models, "us/model");
    }

    /* FFP texture stages: multitexture with per-unit environments. The
     * combiner setup, the texture matrix and texgen are all inputs to the
     * generated fragment shader, and each unit adds a coordinate array. */
    if (phase_on("texstages") && fActiveTexture && fMultiTexCoord2f && fTexEnvi && fTexEnvfv) {
        enum { UNITS = 3 };
        static GLubyte stageTexels[32 * 32 * 4];
        for (int i = 0; i < 32 * 32 * 4; ++i) stageTexels[i] = (GLubyte)(i * 5);
        GLuint stageTex[UNITS];
        fGenTextures(UNITS, stageTex);
        for (int u = 0; u < UNITS; ++u) {
            fActiveTexture(GL_TEXTURE0_C + u);
            fBindTexture(GL_TEXTURE_2D, stageTex[u]);
            fTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 32, 32, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                        stageTexels);
            fTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            fTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            fEnable(GL_TEXTURE_2D);
        }

        const int draws = (int)(6000 * scale) > 0 ? (int)(6000 * scale) : 1;
        // 128 draws cover the environment rotation and both texgen states.
        for (int d = 0; d < 128; ++d) texstages_draw(d, UNITS);
        fFinish();
        t0 = now_ms();
        for (int d = 0; d < draws; ++d) texstages_draw(d, UNITS);
        fFinish();
        ms = now_ms() - t0;
        for (int u = UNITS - 1; u >= 0; --u) {
            fActiveTexture(GL_TEXTURE0_C + u);
            fTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
            if (fTexGeni) { fDisable(GL_TEXTURE_GEN_S); fDisable(GL_TEXTURE_GEN_T); }
            fMatrixMode(GL_TEXTURE_C);
            fLoadIdentity();
            fDisable(GL_TEXTURE_2D);
        }
        fMatrixMode(GL_MODELVIEW);
        report("texstages", ms * 1000.0 / draws, "us/draw");
    }

    /* Per-frame state management: the enable/disable and blend/depth/
     * stencil/scissor churn a GUI or particle pass produces, including the
     * attribute stack, which has to snapshot and restore whole state
     * groups. No geometry work beyond a single quad, so this is the cost of
     * tracking state rather than of drawing. */
    if (phase_on("statechurn") && fPushAttrib && fPopAttrib && fBlendFunc && fDepthFunc &&
        fScissor && fColorMask && fDepthMask && fCullFace && fFrontFace) {
        const int groups = (int)(8000 * scale) > 0 ? (int)(8000 * scale) : 1;
        // Warm-up: the alpha-test and shade state reached here can select a
        // different generated shader, and those builds are one-time.
        for (int w = 0; w < 128; ++w) {
            fPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT_A | GL_TRANSFORM_BIT);
            if (fAlphaFunc) {
                fEnable(GL_ALPHA_TEST);
                fAlphaFunc((w & 16) ? GL_GEQUAL : GL_ALWAYS, 0.1f);
            }
            draw_quads_immediate(1);
            fPopAttrib();
        }
        fFinish();
        t0 = now_ms();
        for (int g = 0; g < groups; ++g) {
            fPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT_A | GL_TRANSFORM_BIT);
            fEnable(GL_BLEND);
            fBlendFunc((g & 1) ? GL_SRC_ALPHA : GL_ONE,
                       (g & 1) ? GL_ONE_MINUS_SRC_ALPHA : GL_ZERO);
            fEnable(GL_DEPTH_TEST);
            fDepthFunc((g & 2) ? GL_LEQUAL : GL_LESS);
            fDepthMask((GLboolean)((g & 4) ? 1 : 0));
            fColorMask(1, 1, 1, (GLboolean)((g & 8) ? 1 : 0));
            if (fAlphaFunc) {
                fEnable(GL_ALPHA_TEST);
                fAlphaFunc((g & 16) ? GL_GEQUAL : GL_ALWAYS, 0.1f);
            }
            if (fStencilFunc && fStencilOp) {
                fEnable(GL_STENCIL_TEST);
                fStencilFunc((g & 32) ? GL_ALWAYS : GL_LEQUAL, 1, 0xFF);
                fStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
            }
            fEnable(GL_SCISSOR_TEST);
            fScissor(0, 0, 1, 1);
            fEnable(GL_CULL_FACE);
            fCullFace(GL_BACK);
            fFrontFace((g & 64) ? GL_CW : GL_CCW);
            if (fPolygonOffset) { fEnable(GL_POLYGON_OFFSET_FILL); fPolygonOffset(1.0f, 1.0f); }
            if (fLineWidth) fLineWidth(1.0f + (GLfloat)(g & 3));
            draw_quads_immediate(1);
            fPopAttrib();
        }
        fFinish();
        ms = now_ms() - t0;
        fDisable(GL_BLEND); fDisable(GL_DEPTH_TEST); fDisable(GL_SCISSOR_TEST);
        fDisable(GL_CULL_FACE); fDisable(GL_ALPHA_TEST); fDisable(GL_STENCIL_TEST);
        fDisable(GL_POLYGON_OFFSET_FILL);
        fDepthMask(1); fColorMask(1, 1, 1, 1);
        report("statechurn", ms * 1000.0 / groups, "us/group");
    }

    /* GL 2.1 programmable pipeline, build side: compiling and linking GLSL.
     * Each program's source is unique, so this measures real translation
     * and link throughput rather than a cache hit. Programs that fail to
     * build are not counted - an implementation must not look fast by
     * rejecting the shader. */
    if (phase_on("shaderbuild") && fCreateShader && fShaderSource && fCompileShader &&
        fGetShaderiv && fCreateProgram && fAttachShader && fLinkProgram && fGetProgramiv &&
        fDeleteShader && fDeleteProgram && fGetUniformLocation) {
        const int programs = (int)(60 * scale) > 0 ? (int)(60 * scale) : 1;
        int built = 0;
        char vsBuf[1024], fsBuf[1024];
        // One throwaway build first: a translating implementation pays its
        // toolchain's one-time initialization on the very first shader, and
        // that is a startup cost, not a per-program one.
        {
            static const char* warmVs = "void main() { gl_Position = ftransform(); }\n";
            static const char* warmFs = "void main() { gl_FragColor = vec4(1.0); }\n";
            const GLuint wv = fCreateShader(GL_VERTEX_SHADER);
            const GLuint wf = fCreateShader(GL_FRAGMENT_SHADER);
            fShaderSource(wv, 1, &warmVs, NULL);
            fShaderSource(wf, 1, &warmFs, NULL);
            fCompileShader(wv);
            fCompileShader(wf);
            const GLuint wp = fCreateProgram();
            fAttachShader(wp, wv);
            fAttachShader(wp, wf);
            fLinkProgram(wp);
            fDeleteShader(wv);
            fDeleteShader(wf);
            fDeleteProgram(wp);
        }
        fFinish();
        t0 = now_ms();
        for (int p = 0; p < programs; ++p) {
            // GLSL 1.10 with the compatibility built-ins a legacy app uses;
            // the uniqueness lives in a constant so every source differs.
            snprintf(vsBuf, sizeof vsBuf,
                     "uniform mat4 uXform;\n"
                     "uniform vec4 uTint;\n"
                     "attribute vec3 aPos;\n"
                     "attribute vec2 aUV;\n"
                     "varying vec2 vUV;\n"
                     "varying vec4 vColor;\n"
                     "void main() {\n"
                     "    vec4 p = uXform * vec4(aPos, 1.0);\n"
                     "    vUV = aUV + vec2(%d.0, 0.0) * 0.001;\n"
                     "    vColor = uTint * gl_Color + vec4(%d.0 * 0.001);\n"
                     "    gl_Position = gl_ModelViewProjectionMatrix * p;\n"
                     "}\n",
                     p, p);
            // Deliberately kept to constructs every implementation compared
            // here accepts. gl4es mistranslates clamp() (its output makes
            // the driver report bogus overloads), and a corpus one side
            // cannot build measures nothing.
            snprintf(fsBuf, sizeof fsBuf,
                     "uniform sampler2D uTex;\n"
                     "uniform float uFade;\n"
                     "varying vec2 vUV;\n"
                     "varying vec4 vColor;\n"
                     "void main() {\n"
                     "    vec4 t = texture2D(uTex, vUV);\n"
                     "    float f = uFade + %d.0 * 0.001;\n"
                     "    vec3 c = mix(t.rgb, vColor.rgb, f);\n"
                     "    gl_FragColor = vec4(c, t.a * vColor.a);\n"
                     "}\n",
                     p);
            const char* vsSrc = vsBuf;
            const char* fsSrc = fsBuf;
            const GLuint vs = fCreateShader(GL_VERTEX_SHADER);
            const GLuint fs = fCreateShader(GL_FRAGMENT_SHADER);
            fShaderSource(vs, 1, &vsSrc, NULL);
            fShaderSource(fs, 1, &fsSrc, NULL);
            fCompileShader(vs);
            fCompileShader(fs);
            GLint okv = 0, okf = 0;
            fGetShaderiv(vs, GL_COMPILE_STATUS, &okv);
            fGetShaderiv(fs, GL_COMPILE_STATUS, &okf);
            const GLuint prog = fCreateProgram();
            fAttachShader(prog, vs);
            fAttachShader(prog, fs);
            if (fBindAttribLocation) {
                fBindAttribLocation(prog, 0, "aPos");
                fBindAttribLocation(prog, 1, "aUV");
            }
            fLinkProgram(prog);
            GLint linked = 0;
            fGetProgramiv(prog, GL_LINK_STATUS, &linked);
            if (!(okv && okf && linked) && built == 0 && p == 0) {
                // Say why, once: a library that cannot build the shader has
                // no line in the table, and "missing" should not be
                // confused with "fast".
                char log[512];
                if (!okv && fGetShaderInfoLog) {
                    log[0] = '\0';
                    fGetShaderInfoLog(vs, (GLsizei)sizeof log, NULL, log);
                    fprintf(stderr, "shaderbuild: vertex compile failed: %s\n", log);
                }
                if (!okf && fGetShaderInfoLog) {
                    log[0] = '\0';
                    fGetShaderInfoLog(fs, (GLsizei)sizeof log, NULL, log);
                    fprintf(stderr, "shaderbuild: fragment compile failed: %s\n", log);
                }
                if (okv && okf && !linked && fGetProgramInfoLog) {
                    log[0] = '\0';
                    fGetProgramInfoLog(prog, (GLsizei)sizeof log, NULL, log);
                    fprintf(stderr, "shaderbuild: link failed: %s\n", log);
                }
            }
            if (okv && okf && linked) {
                // Resolving locations is part of what an app does with a
                // freshly linked program, and it is where a wrapper's
                // name-mapping cost shows up.
                (void)fGetUniformLocation(prog, "uXform");
                (void)fGetUniformLocation(prog, "uTint");
                (void)fGetUniformLocation(prog, "uTex");
                (void)fGetUniformLocation(prog, "uFade");
                ++built;
            }
            fDeleteShader(vs);
            fDeleteShader(fs);
            fDeleteProgram(prog);
        }
        fFinish();
        ms = now_ms() - t0;
        if (built == programs) report("shaderbuild", ms * 1000.0 / programs, "us/program");
        else fprintf(stderr, "shaderbuild: only %d/%d programs built; phase omitted\n", built,
                     programs);
    }

    /* GL 2.1 programmable pipeline, draw side: a user program driven the
     * way an app drives it - generic attributes out of a VBO, a full set of
     * uniform types re-sent per draw, indexed draws, and a periodic
     * render-to-texture switch so the framebuffer path is covered too. */
    if (phase_on("progdraw") && fCreateShader && fShaderSource && fCompileShader && fGetShaderiv &&
        fCreateProgram && fAttachShader && fLinkProgram && fGetProgramiv && fUseProgram &&
        fGetUniformLocation && fUniform1i && fUniform1f && fUniform4fv && fUniformMatrix4fv &&
        fVertexAttribPointer && fEnableVertexAttribArray && fGenBuffers && fBindBuffer &&
        fBufferData) {
        static const char* pdVs =
            "uniform mat4 uXform;\n"
            "uniform vec4 uTint;\n"
            "uniform float uTime;\n"
            "attribute vec3 aPos;\n"
            "attribute vec2 aUV;\n"
            "attribute vec4 aColor;\n"
            "varying vec2 vUV;\n"
            "varying vec4 vColor;\n"
            "void main() {\n"
            "    vUV = aUV;\n"
            "    vColor = aColor * uTint * (0.5 + 0.5 * uTime);\n"
            "    gl_Position = uXform * vec4(aPos, 1.0);\n"
            "}\n";
        static const char* pdFs =
            "uniform sampler2D uTex;\n"
            "uniform float uFade;\n"
            "varying vec2 vUV;\n"
            "varying vec4 vColor;\n"
            "void main() {\n"
            "    vec4 t = texture2D(uTex, vUV);\n"
            "    gl_FragColor = vec4(mix(t.rgb, vColor.rgb, uFade), t.a * vColor.a);\n"
            "}\n";
        const GLuint vs = fCreateShader(GL_VERTEX_SHADER);
        const GLuint fs = fCreateShader(GL_FRAGMENT_SHADER);
        fShaderSource(vs, 1, &pdVs, NULL);
        fShaderSource(fs, 1, &pdFs, NULL);
        fCompileShader(vs);
        fCompileShader(fs);
        GLint okv = 0, okf = 0;
        fGetShaderiv(vs, GL_COMPILE_STATUS, &okv);
        fGetShaderiv(fs, GL_COMPILE_STATUS, &okf);
        const GLuint prog = fCreateProgram();
        fAttachShader(prog, vs);
        fAttachShader(prog, fs);
        if (fBindAttribLocation) {
            fBindAttribLocation(prog, 0, "aPos");
            fBindAttribLocation(prog, 1, "aUV");
            fBindAttribLocation(prog, 2, "aColor");
        }
        fLinkProgram(prog);
        GLint linked = 0;
        fGetProgramiv(prog, GL_LINK_STATUS, &linked);
        if (!(okv && okf && linked)) {
            fprintf(stderr, "progdraw: program did not build; phase omitted\n");
        } else {
            GLuint pdBuf[2];
            fGenBuffers(2, pdBuf);
            fBindBuffer(GL_ARRAY_BUFFER, pdBuf[0]);
            fBufferData(GL_ARRAY_BUFFER, (long)sizeof(interleaved), interleaved, GL_STATIC_DRAW);
            fBindBuffer(GL_ELEMENT_ARRAY_BUFFER, pdBuf[1]);
            fBufferData(GL_ELEMENT_ARRAY_BUFFER, (long)sizeof(indices), indices, GL_STATIC_DRAW);

            GLint aPos = 0, aUV = 1, aColor = 2;
            if (fGetAttribLocation) {
                aPos = fGetAttribLocation(prog, "aPos");
                aUV = fGetAttribLocation(prog, "aUV");
                aColor = fGetAttribLocation(prog, "aColor");
            }
            fUseProgram(prog);
            const GLint uXform = fGetUniformLocation(prog, "uXform");
            const GLint uTint = fGetUniformLocation(prog, "uTint");
            const GLint uTime = fGetUniformLocation(prog, "uTime");
            const GLint uTex = fGetUniformLocation(prog, "uTex");
            const GLint uFade = fGetUniformLocation(prog, "uFade");

            // Render target for the periodic off-screen pass.
            GLuint fbo = 0, fboTex = 0;
            int fboReady = 0;
            if (fGenFramebuffers && fBindFramebuffer && fFramebufferTexture2D &&
                fCheckFramebufferStatus) {
                fGenTextures(1, &fboTex);
                fBindTexture(GL_TEXTURE_2D, fboTex);
                fTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 64, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
                fTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                fTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                fTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                fTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                fGenFramebuffers(1, &fbo);
                fBindFramebuffer(GL_FRAMEBUFFER, fbo);
                fFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fboTex,
                                      0);
                fboReady = fCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
                fBindFramebuffer(GL_FRAMEBUFFER, 0);
            }

            const int draws = (int)(20000 * scale) > 0 ? (int)(20000 * scale) : 1;
            GLfloat matrix[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
            const GLfloat tint[4] = {0.8f, 0.7f, 0.6f, 1.0f};
            // Warm-up: first use of the program, of each attribute array and
            // of the off-screen target all carry one-time setup.
            if (fboReady) {
                fBindFramebuffer(GL_FRAMEBUFFER, fbo);
                fViewport(0, 0, 64, 64);
            }
            fBindBuffer(GL_ARRAY_BUFFER, pdBuf[0]);
            if (aPos >= 0) {
                fVertexAttribPointer((GLuint)aPos, 3, GL_FLOAT, 0, 9 * (GLsizei)sizeof(GLfloat),
                                     (const void*)0);
                fEnableVertexAttribArray((GLuint)aPos);
            }
            if (aColor >= 0) {
                fVertexAttribPointer((GLuint)aColor, 4, GL_FLOAT, 0, 9 * (GLsizei)sizeof(GLfloat),
                                     (const void*)(3 * sizeof(GLfloat)));
                fEnableVertexAttribArray((GLuint)aColor);
            }
            if (aUV >= 0) {
                fVertexAttribPointer((GLuint)aUV, 2, GL_FLOAT, 0, 9 * (GLsizei)sizeof(GLfloat),
                                     (const void*)(7 * sizeof(GLfloat)));
                fEnableVertexAttribArray((GLuint)aUV);
            }
            fBindBuffer(GL_ELEMENT_ARRAY_BUFFER, pdBuf[1]);
            fDrawElements(GL_TRIANGLES, 24, GL_UNSIGNED_SHORT, (const void*)0);
            if (fboReady) {
                fBindFramebuffer(GL_FRAMEBUFFER, 0);
                fViewport(0, 0, vp, vp);
            }
            fDrawElements(GL_TRIANGLES, 24, GL_UNSIGNED_SHORT, (const void*)0);
            fFinish();
            t0 = now_ms();
            for (int d = 0; d < draws; ++d) {
                if (fboReady && (d & 255) == 0) {
                    // Off-screen pass: bind the FBO, draw, and come back -
                    // the shape of a shadow/reflection/post pass.
                    fBindFramebuffer(GL_FRAMEBUFFER, fbo);
                    fViewport(0, 0, 64, 64);
                }
                fBindBuffer(GL_ARRAY_BUFFER, pdBuf[0]);
                if (aPos >= 0) {
                    fVertexAttribPointer((GLuint)aPos, 3, GL_FLOAT, 0, 9 * (GLsizei)sizeof(GLfloat),
                                         (const void*)0);
                    fEnableVertexAttribArray((GLuint)aPos);
                }
                if (aColor >= 0) {
                    fVertexAttribPointer((GLuint)aColor, 4, GL_FLOAT, 0,
                                         9 * (GLsizei)sizeof(GLfloat),
                                         (const void*)(3 * sizeof(GLfloat)));
                    fEnableVertexAttribArray((GLuint)aColor);
                }
                if (aUV >= 0) {
                    fVertexAttribPointer((GLuint)aUV, 2, GL_FLOAT, 0, 9 * (GLsizei)sizeof(GLfloat),
                                         (const void*)(7 * sizeof(GLfloat)));
                    fEnableVertexAttribArray((GLuint)aUV);
                }
                matrix[12] = 0.0001f * (GLfloat)(d & 255);
                if (uXform >= 0) fUniformMatrix4fv(uXform, 1, 0, matrix);
                if (uTint >= 0) fUniform4fv(uTint, 1, tint);
                if (uTime >= 0) fUniform1f(uTime, 0.001f * (GLfloat)(d & 1023));
                if (uFade >= 0) fUniform1f(uFade, 0.5f);
                if (uTex >= 0) fUniform1i(uTex, 0);
                fBindBuffer(GL_ELEMENT_ARRAY_BUFFER, pdBuf[1]);
                fDrawElements(GL_TRIANGLES, 24, GL_UNSIGNED_SHORT, (const void*)0);
                if (fboReady && (d & 255) == 0) {
                    fBindFramebuffer(GL_FRAMEBUFFER, 0);
                    fViewport(0, 0, vp, vp);
                }
            }
            fFinish();
            ms = now_ms() - t0;
            if (aPos >= 0 && fDisableVertexAttribArray) fDisableVertexAttribArray((GLuint)aPos);
            if (aUV >= 0 && fDisableVertexAttribArray) fDisableVertexAttribArray((GLuint)aUV);
            if (aColor >= 0 && fDisableVertexAttribArray) fDisableVertexAttribArray((GLuint)aColor);
            fUseProgram(0);
            fBindBuffer(GL_ARRAY_BUFFER, 0);
            fBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
            report("progdraw", ms * 1000.0 / draws, "us/draw");
        }
    }

    return 0;
}
