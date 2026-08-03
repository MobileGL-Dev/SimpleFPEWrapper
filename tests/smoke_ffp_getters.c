// SimpleFPEWrapper - tests/smoke_ffp_getters.c
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// Every state variable GL 2.1 defines that neither an ES 3.0+ nor a GL 3.2+
// core backend can answer - the whole fixed-function surface, taken from the
// glGet page of the GL 2.1 manual and checked here through all four entry
// points at once.
//
// The sweep looks for the failure mode that made this worth doing: a query
// that raises GL_INVALID_ENUM and leaves the caller's buffer exactly as it
// was. Nothing about that is visible to the caller, which is how Sodium's
// fog occlusion came to cull the world against a cutoff it never read - it
// asks for GL_FOG_MODE, then the matching fog distance, and does the rest on
// the CPU. That sequence is checked on its own below.
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <EGL/egl.h>

typedef unsigned int GLenum, GLuint, GLbitfield;
typedef unsigned char GLboolean, GLubyte;
typedef int GLint, GLsizei;
typedef float GLfloat;
typedef double GLdouble;

#define WIN 16
#define GL_NO_ERROR 0
#define GL_INVALID_ENUM 0x0500
#define GL_FALSE 0
#define GL_TRUE 1
#define GL_EXP 0x0800
#define GL_LINEAR 0x2601
#define GL_SMOOTH 0x1D01
#define GL_FLAT 0x1D00
#define GL_MODELVIEW 0x1700
#define GL_PROJECTION 0x1701
#define GL_FLOAT 0x1406
#define GL_TEXTURE0 0x84C0
#define GL_RENDER 0x1C00
#define GL_FILL 0x1B02
#define GL_NICEST 0x1102
#define GL_DONT_CARE 0x1100
#define GL_COLOR_ARRAY 0x8076
#define GL_ALL_ATTRIB_BITS 0x000FFFFF
#define GL_MAP1_VERTEX_3 0x0D97

// pname, value, and how many values GL 2.1 says it returns.
static const struct { const char* name; GLenum pname; int count; } kState[] = {
    {"GL_ACCUM_ALPHA_BITS", 0x0D5B, 1}, {"GL_ACCUM_BLUE_BITS", 0x0D5A, 1}, {"GL_ACCUM_CLEAR_VALUE", 0x0B80, 4},
    {"GL_ACCUM_GREEN_BITS", 0x0D59, 1}, {"GL_ACCUM_RED_BITS", 0x0D58, 1}, {"GL_ALIASED_POINT_SIZE_RANGE", 0x846D, 2},
    {"GL_ALPHA_BIAS", 0x0D1D, 1}, {"GL_ALPHA_BITS", 0x0D55, 1}, {"GL_ALPHA_SCALE", 0x0D1C, 1},
    {"GL_ALPHA_TEST", 0x0BC0, 1}, {"GL_ALPHA_TEST_REF", 0x0BC2, 1}, {"GL_ATTRIB_STACK_DEPTH", 0x0BB0, 1},
    {"GL_AUTO_NORMAL", 0x0D80, 1}, {"GL_AUX_BUFFERS", 0x0C00, 1}, {"GL_BLUE_BIAS", 0x0D1B, 1},
    {"GL_BLUE_BITS", 0x0D54, 1}, {"GL_BLUE_SCALE", 0x0D1A, 1}, {"GL_CLIENT_ACTIVE_TEXTURE", 0x84E1, 1},
    {"GL_CLIENT_ATTRIB_STACK_DEPTH", 0x0BB1, 1}, {"GL_COLOR_ARRAY", 0x8076, 1}, {"GL_COLOR_ARRAY_BUFFER_BINDING", 0x8898, 1},
    {"GL_COLOR_ARRAY_SIZE", 0x8081, 1}, {"GL_COLOR_ARRAY_STRIDE", 0x8083, 1}, {"GL_COLOR_ARRAY_TYPE", 0x8082, 1},
    {"GL_COLOR_LOGIC_OP", 0x0BF2, 1}, {"GL_COLOR_MATERIAL", 0x0B57, 1}, {"GL_COLOR_MATERIAL_FACE", 0x0B55, 1},
    {"GL_COLOR_MATERIAL_PARAMETER", 0x0B56, 1}, {"GL_COLOR_MATRIX", 0x80B1, 16}, {"GL_COLOR_MATRIX_STACK_DEPTH", 0x80B2, 1},
    {"GL_COLOR_SUM", 0x8458, 1}, {"GL_COLOR_TABLE", 0x80D0, 1}, {"GL_CONVOLUTION_1D", 0x8010, 1},
    {"GL_CONVOLUTION_2D", 0x8011, 1}, {"GL_CULL_FACE_MODE", 0x0B45, 1}, {"GL_CURRENT_COLOR", 0x0B00, 4},
    {"GL_CURRENT_FOG_COORD", 0x8453, 1}, {"GL_CURRENT_INDEX", 0x0B01, 1}, {"GL_CURRENT_NORMAL", 0x0B02, 3},
    {"GL_CURRENT_RASTER_COLOR", 0x0B04, 4}, {"GL_CURRENT_RASTER_DISTANCE", 0x0B09, 1}, {"GL_CURRENT_RASTER_INDEX", 0x0B05, 1},
    {"GL_CURRENT_RASTER_POSITION", 0x0B07, 4}, {"GL_CURRENT_RASTER_POSITION_VALID", 0x0B08, 1}, {"GL_CURRENT_RASTER_SECONDARY_COLOR", 0x845F, 4},
    {"GL_CURRENT_RASTER_TEXTURE_COORDS", 0x0B06, 4}, {"GL_CURRENT_SECONDARY_COLOR", 0x8459, 4}, {"GL_CURRENT_TEXTURE_COORDS", 0x0B03, 4},
    {"GL_DEPTH_BIAS", 0x0D1F, 1}, {"GL_DEPTH_BITS", 0x0D56, 1}, {"GL_DEPTH_SCALE", 0x0D1E, 1},
    {"GL_DOUBLEBUFFER", 0x0C32, 1}, {"GL_DRAW_BUFFER", 0x0C01, 1}, {"GL_EDGE_FLAG", 0x0B43, 1},
    {"GL_EDGE_FLAG_ARRAY", 0x8079, 1}, {"GL_EDGE_FLAG_ARRAY_BUFFER_BINDING", 0x889B, 1}, {"GL_EDGE_FLAG_ARRAY_STRIDE", 0x808C, 1},
    {"GL_FEEDBACK_BUFFER_SIZE", 0x0DF1, 1}, {"GL_FEEDBACK_BUFFER_TYPE", 0x0DF2, 1}, {"GL_FOG", 0x0B60, 1},
    {"GL_FOG_COLOR", 0x0B66, 4}, {"GL_FOG_COORD_ARRAY", 0x8457, 1}, {"GL_FOG_COORD_ARRAY_BUFFER_BINDING", 0x889D, 1},
    {"GL_FOG_COORD_ARRAY_STRIDE", 0x8455, 1}, {"GL_FOG_COORD_ARRAY_TYPE", 0x8454, 1}, {"GL_FOG_COORD_SRC", 0x8450, 1},
    {"GL_FOG_DENSITY", 0x0B62, 1}, {"GL_FOG_END", 0x0B64, 1}, {"GL_FOG_HINT", 0x0C54, 1},
    {"GL_FOG_INDEX", 0x0B61, 1}, {"GL_FOG_MODE", 0x0B65, 1}, {"GL_FOG_START", 0x0B63, 1},
    {"GL_FRONT_FACE", 0x0B46, 1}, {"GL_GENERATE_MIPMAP_HINT", 0x8192, 1}, {"GL_GREEN_BIAS", 0x0D19, 1},
    {"GL_GREEN_BITS", 0x0D53, 1}, {"GL_GREEN_SCALE", 0x0D18, 1}, {"GL_HISTOGRAM", 0x8024, 1},
    {"GL_INDEX_ARRAY", 0x8077, 1}, {"GL_INDEX_ARRAY_BUFFER_BINDING", 0x8899, 1}, {"GL_INDEX_ARRAY_STRIDE", 0x8086, 1},
    {"GL_INDEX_ARRAY_TYPE", 0x8085, 1}, {"GL_INDEX_BITS", 0x0D51, 1}, {"GL_INDEX_CLEAR_VALUE", 0x0C20, 1},
    {"GL_INDEX_LOGIC_OP", 0x0BF1, 1}, {"GL_INDEX_MODE", 0x0C30, 1}, {"GL_INDEX_OFFSET", 0x0D13, 1},
    {"GL_INDEX_SHIFT", 0x0D12, 1}, {"GL_INDEX_WRITEMASK", 0x0C21, 1}, {"GL_LIGHTING", 0x0B50, 1},
    {"GL_LIGHT_MODEL_AMBIENT", 0x0B53, 4}, {"GL_LIGHT_MODEL_COLOR_CONTROL", 0x81F8, 1}, {"GL_LIGHT_MODEL_LOCAL_VIEWER", 0x0B51, 1},
    {"GL_LIGHT_MODEL_TWO_SIDE", 0x0B52, 1}, {"GL_LINE_SMOOTH", 0x0B20, 1}, {"GL_LINE_SMOOTH_HINT", 0x0C52, 1},
    {"GL_LINE_STIPPLE", 0x0B24, 1}, {"GL_LINE_STIPPLE_PATTERN", 0x0B25, 1}, {"GL_LINE_STIPPLE_REPEAT", 0x0B26, 1},
    {"GL_LINE_WIDTH_GRANULARITY", 0x0B23, 1}, {"GL_LINE_WIDTH_RANGE", 0x0B22, 2}, {"GL_LIST_BASE", 0x0B32, 1},
    {"GL_LIST_INDEX", 0x0B33, 1}, {"GL_LIST_MODE", 0x0B30, 1}, {"GL_LOGIC_OP_MODE", 0x0BF0, 1},
    {"GL_MAP1_COLOR_4", 0x0D90, 1}, {"GL_MAP1_GRID_DOMAIN", 0x0DD0, 2}, {"GL_MAP1_GRID_SEGMENTS", 0x0DD1, 1},
    {"GL_MAP1_INDEX", 0x0D91, 1}, {"GL_MAP1_NORMAL", 0x0D92, 1}, {"GL_MAP1_TEXTURE_COORD_1", 0x0D93, 1},
    {"GL_MAP1_TEXTURE_COORD_2", 0x0D94, 1}, {"GL_MAP1_TEXTURE_COORD_3", 0x0D95, 1}, {"GL_MAP1_TEXTURE_COORD_4", 0x0D96, 1},
    {"GL_MAP1_VERTEX_3", 0x0D97, 1}, {"GL_MAP1_VERTEX_4", 0x0D98, 1}, {"GL_MAP2_COLOR_4", 0x0DB0, 1},
    {"GL_MAP2_GRID_DOMAIN", 0x0DD2, 4}, {"GL_MAP2_GRID_SEGMENTS", 0x0DD3, 2}, {"GL_MAP2_INDEX", 0x0DB1, 1},
    {"GL_MAP2_NORMAL", 0x0DB2, 1}, {"GL_MAP2_TEXTURE_COORD_1", 0x0DB3, 1}, {"GL_MAP2_TEXTURE_COORD_2", 0x0DB4, 1},
    {"GL_MAP2_TEXTURE_COORD_3", 0x0DB5, 1}, {"GL_MAP2_TEXTURE_COORD_4", 0x0DB6, 1}, {"GL_MAP2_VERTEX_3", 0x0DB7, 1},
    {"GL_MAP2_VERTEX_4", 0x0DB8, 1}, {"GL_MAP_COLOR", 0x0D10, 1}, {"GL_MAP_STENCIL", 0x0D11, 1},
    {"GL_MATRIX_MODE", 0x0BA0, 1}, {"GL_MAX_ATTRIB_STACK_DEPTH", 0x0D35, 1}, {"GL_MAX_CLIENT_ATTRIB_STACK_DEPTH", 0x0D3B, 1},
    {"GL_MAX_CLIP_PLANES", 0x0D32, 1}, {"GL_MAX_COLOR_MATRIX_STACK_DEPTH", 0x80B3, 1}, {"GL_MAX_EVAL_ORDER", 0x0D30, 1},
    {"GL_MAX_LIGHTS", 0x0D31, 1}, {"GL_MAX_LIST_NESTING", 0x0B31, 1}, {"GL_MAX_MODELVIEW_STACK_DEPTH", 0x0D36, 1},
    {"GL_MAX_NAME_STACK_DEPTH", 0x0D37, 1}, {"GL_MAX_PIXEL_MAP_TABLE", 0x0D34, 1}, {"GL_MAX_PROJECTION_STACK_DEPTH", 0x0D38, 1},
    {"GL_MAX_TEXTURE_COORDS", 0x8871, 1}, {"GL_MAX_TEXTURE_STACK_DEPTH", 0x0D39, 1}, {"GL_MAX_TEXTURE_UNITS", 0x84E2, 1},
    {"GL_MAX_VARYING_FLOATS", 0x8B4B, 1}, {"GL_MINMAX", 0x802E, 1}, {"GL_MODELVIEW_MATRIX", 0x0BA6, 16},
    {"GL_MODELVIEW_STACK_DEPTH", 0x0BA3, 1}, {"GL_NAME_STACK_DEPTH", 0x0D70, 1}, {"GL_NORMALIZE", 0x0BA1, 1},
    {"GL_NORMAL_ARRAY", 0x8075, 1}, {"GL_NORMAL_ARRAY_BUFFER_BINDING", 0x8897, 1}, {"GL_NORMAL_ARRAY_STRIDE", 0x807F, 1},
    {"GL_NORMAL_ARRAY_TYPE", 0x807E, 1}, {"GL_PACK_IMAGE_HEIGHT", 0x806C, 1}, {"GL_PACK_LSB_FIRST", 0x0D01, 1},
    {"GL_PACK_SKIP_IMAGES", 0x806B, 1}, {"GL_PACK_SWAP_BYTES", 0x0D00, 1}, {"GL_PERSPECTIVE_CORRECTION_HINT", 0x0C50, 1},
    {"GL_PIXEL_MAP_A_TO_A_SIZE", 0x0CB9, 1}, {"GL_PIXEL_MAP_B_TO_B_SIZE", 0x0CB8, 1}, {"GL_PIXEL_MAP_G_TO_G_SIZE", 0x0CB7, 1},
    {"GL_PIXEL_MAP_I_TO_A_SIZE", 0x0CB5, 1}, {"GL_PIXEL_MAP_I_TO_B_SIZE", 0x0CB4, 1}, {"GL_PIXEL_MAP_I_TO_G_SIZE", 0x0CB3, 1},
    {"GL_PIXEL_MAP_I_TO_I_SIZE", 0x0CB0, 1}, {"GL_PIXEL_MAP_I_TO_R_SIZE", 0x0CB2, 1}, {"GL_PIXEL_MAP_R_TO_R_SIZE", 0x0CB6, 1},
    {"GL_PIXEL_MAP_S_TO_S_SIZE", 0x0CB1, 1}, {"GL_POINT_DISTANCE_ATTENUATION", 0x8129, 3}, {"GL_POINT_FADE_THRESHOLD_SIZE", 0x8128, 1},
    {"GL_POINT_SIZE", 0x0B11, 1}, {"GL_POINT_SIZE_GRANULARITY", 0x0B13, 1}, {"GL_POINT_SIZE_MAX", 0x8127, 1},
    {"GL_POINT_SIZE_MIN", 0x8126, 1}, {"GL_POINT_SIZE_RANGE", 0x0B12, 2}, {"GL_POINT_SMOOTH", 0x0B10, 1},
    {"GL_POINT_SMOOTH_HINT", 0x0C51, 1}, {"GL_POINT_SPRITE", 0x8861, 1}, {"GL_POLYGON_MODE", 0x0B40, 2},
    {"GL_POLYGON_OFFSET_LINE", 0x2A02, 1}, {"GL_POLYGON_OFFSET_POINT", 0x2A01, 1}, {"GL_POLYGON_SMOOTH", 0x0B41, 1},
    {"GL_POLYGON_SMOOTH_HINT", 0x0C53, 1}, {"GL_POLYGON_STIPPLE", 0x0B42, 1}, {"GL_POST_COLOR_MATRIX_ALPHA_BIAS", 0x80BB, 1},
    {"GL_POST_COLOR_MATRIX_ALPHA_SCALE", 0x80B7, 1}, {"GL_POST_COLOR_MATRIX_BLUE_BIAS", 0x80BA, 1}, {"GL_POST_COLOR_MATRIX_BLUE_SCALE", 0x80B6, 1},
    {"GL_POST_COLOR_MATRIX_COLOR_TABLE", 0x80D2, 1}, {"GL_POST_COLOR_MATRIX_GREEN_BIAS", 0x80B9, 1}, {"GL_POST_COLOR_MATRIX_GREEN_SCALE", 0x80B5, 1},
    {"GL_POST_COLOR_MATRIX_RED_BIAS", 0x80B8, 1}, {"GL_POST_COLOR_MATRIX_RED_SCALE", 0x80B4, 1}, {"GL_POST_CONVOLUTION_ALPHA_BIAS", 0x8023, 1},
    {"GL_POST_CONVOLUTION_ALPHA_SCALE", 0x801F, 1}, {"GL_POST_CONVOLUTION_BLUE_BIAS", 0x8022, 1}, {"GL_POST_CONVOLUTION_BLUE_SCALE", 0x801E, 1},
    {"GL_POST_CONVOLUTION_COLOR_TABLE", 0x80D1, 1}, {"GL_POST_CONVOLUTION_GREEN_BIAS", 0x8021, 1}, {"GL_POST_CONVOLUTION_GREEN_SCALE", 0x801D, 1},
    {"GL_POST_CONVOLUTION_RED_BIAS", 0x8020, 1}, {"GL_POST_CONVOLUTION_RED_SCALE", 0x801C, 1}, {"GL_PROJECTION_MATRIX", 0x0BA7, 16},
    {"GL_PROJECTION_STACK_DEPTH", 0x0BA4, 1}, {"GL_RED_BIAS", 0x0D15, 1}, {"GL_RED_BITS", 0x0D52, 1},
    {"GL_RED_SCALE", 0x0D14, 1}, {"GL_RENDER_MODE", 0x0C40, 1}, {"GL_RESCALE_NORMAL", 0x803A, 1},
    {"GL_RGBA_MODE", 0x0C31, 1}, {"GL_SECONDARY_COLOR_ARRAY", 0x845E, 1}, {"GL_SECONDARY_COLOR_ARRAY_BUFFER_BINDING", 0x889C, 1},
    {"GL_SECONDARY_COLOR_ARRAY_SIZE", 0x845A, 1}, {"GL_SECONDARY_COLOR_ARRAY_STRIDE", 0x845C, 1}, {"GL_SECONDARY_COLOR_ARRAY_TYPE", 0x845B, 1},
    {"GL_SELECTION_BUFFER_SIZE", 0x0DF4, 1}, {"GL_SEPARABLE_2D", 0x8012, 1}, {"GL_SHADE_MODEL", 0x0B54, 1},
    {"GL_STENCIL_BITS", 0x0D57, 1}, {"GL_STEREO", 0x0C33, 1}, {"GL_TEXTURE_1D", 0x0DE0, 1},
    {"GL_TEXTURE_2D", 0x0DE1, 1}, {"GL_TEXTURE_3D", 0x806F, 1}, {"GL_TEXTURE_BINDING_1D", 0x8068, 1},
    {"GL_TEXTURE_COMPRESSION_HINT", 0x84EF, 1}, {"GL_TEXTURE_COORD_ARRAY", 0x8078, 1}, {"GL_TEXTURE_COORD_ARRAY_BUFFER_BINDING", 0x889A, 1},
    {"GL_TEXTURE_COORD_ARRAY_SIZE", 0x8088, 1}, {"GL_TEXTURE_COORD_ARRAY_STRIDE", 0x808A, 1}, {"GL_TEXTURE_COORD_ARRAY_TYPE", 0x8089, 1},
    {"GL_TEXTURE_CUBE_MAP", 0x8513, 1}, {"GL_TEXTURE_GEN_Q", 0x0C63, 1}, {"GL_TEXTURE_GEN_R", 0x0C62, 1},
    {"GL_TEXTURE_GEN_S", 0x0C60, 1}, {"GL_TEXTURE_GEN_T", 0x0C61, 1}, {"GL_TEXTURE_MATRIX", 0x0BA8, 16},
    {"GL_TEXTURE_STACK_DEPTH", 0x0BA5, 1}, {"GL_TRANSPOSE_COLOR_MATRIX", 0x84E6, 16}, {"GL_TRANSPOSE_MODELVIEW_MATRIX", 0x84E3, 16},
    {"GL_TRANSPOSE_PROJECTION_MATRIX", 0x84E4, 16}, {"GL_TRANSPOSE_TEXTURE_MATRIX", 0x84E5, 16}, {"GL_UNPACK_LSB_FIRST", 0x0CF1, 1},
    {"GL_UNPACK_SWAP_BYTES", 0x0CF0, 1}, {"GL_VERTEX_ARRAY", 0x8074, 1}, {"GL_VERTEX_ARRAY_BUFFER_BINDING", 0x8896, 1},
    {"GL_VERTEX_ARRAY_SIZE", 0x807A, 1}, {"GL_VERTEX_ARRAY_STRIDE", 0x807C, 1}, {"GL_VERTEX_ARRAY_TYPE", 0x807B, 1},
    {"GL_VERTEX_PROGRAM_POINT_SIZE", 0x8642, 1}, {"GL_VERTEX_PROGRAM_TWO_SIDE", 0x8643, 1}, {"GL_ZOOM_X", 0x0D16, 1},
    {"GL_ZOOM_Y", 0x0D17, 1},
};

static int failures = 0;
static void (*fGetIntegerv)(GLenum, GLint*);
static void (*fGetFloatv)(GLenum, GLfloat*);
static void (*fGetBooleanv)(GLenum, GLboolean*);
static void (*fGetDoublev)(GLenum, GLdouble*);
static GLboolean (*fIsEnabled)(GLenum);
static GLenum (*fGetError)(void);

static void drainErrors(void) { while (fGetError() != GL_NO_ERROR) {} }

static void expectFloat(const char* what, GLenum pname, GLfloat want) {
    GLfloat got = -12345.0f;
    fGetFloatv(pname, &got);
    if (fabsf(got - want) > 1e-5f) {
        printf("*** %s: expected %g, got %g\n", what, (double)want, (double)got);
        ++failures;
    }
}

static void expectInt(const char* what, GLenum pname, GLint want) {
    GLint got = -12345;
    fGetIntegerv(pname, &got);
    if (got != want) {
        printf("*** %s: expected 0x%x, got 0x%x\n", what, want, got);
        ++failures;
    }
}

int main(void) {
    void* h = dlopen(WRAPPER_LIB_PATH, RTLD_NOW | RTLD_LOCAL);
    if (!h) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 1; }
    void* (*resolve)(const char*);
    *(void**)(&resolve) = dlsym(h, "eglGetProcAddress");
    if (!resolve) { fprintf(stderr, "*** no eglGetProcAddress\n"); return 1; }

    EGLDisplay d = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (d == EGL_NO_DISPLAY || !eglInitialize(d, NULL, NULL)) { printf("SKIP: no EGL display\n"); return 77; }
    const EGLint ca[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
                         EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
    EGLConfig c; EGLint n = 0;
    if (!eglChooseConfig(d, ca, &c, 1, &n) || n == 0) { printf("SKIP: no ES3 config\n"); return 77; }
    const EGLint pa[] = {EGL_WIDTH, WIN, EGL_HEIGHT, WIN, EGL_NONE};
    EGLSurface s = eglCreatePbufferSurface(d, c, pa);
    eglBindAPI(EGL_OPENGL_ES_API);
    const EGLint xa[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    EGLContext x = eglCreateContext(d, c, EGL_NO_CONTEXT, xa);
    if (!eglMakeCurrent(d, s, s, x)) { printf("SKIP: no current context\n"); return 77; }

#define R(v, nm) *(void**)(&v) = resolve(nm); if (!v) { fprintf(stderr, "*** MISSING %s\n", nm); return 1; }
    R(fGetIntegerv, "glGetIntegerv")
    R(fGetFloatv, "glGetFloatv")
    R(fGetBooleanv, "glGetBooleanv")
    R(fGetDoublev, "glGetDoublev")
    R(fIsEnabled, "glIsEnabled")
    R(fGetError, "glGetError")
    void (*fFogi)(GLenum, GLint); R(fFogi, "glFogi")
    void (*fFogf)(GLenum, GLfloat); R(fFogf, "glFogf")
    void (*fShadeModel)(GLenum); R(fShadeModel, "glShadeModel")
    void (*fHint)(GLenum, GLenum); R(fHint, "glHint")
    void (*fEnableClientState)(GLenum); R(fEnableClientState, "glEnableClientState")
    void (*fPushAttrib)(GLbitfield); R(fPushAttrib, "glPushAttrib")
    void (*fPopAttrib)(void); R(fPopAttrib, "glPopAttrib")
    void (*fMatrixMode)(GLenum); R(fMatrixMode, "glMatrixMode")
    void (*fPushMatrix)(void); R(fPushMatrix, "glPushMatrix")
    void (*fPopMatrix)(void); R(fPopMatrix, "glPopMatrix")
    void (*fClearAccum)(GLfloat, GLfloat, GLfloat, GLfloat); R(fClearAccum, "glClearAccum")
    void (*fMapGrid1f)(GLint, GLfloat, GLfloat); R(fMapGrid1f, "glMapGrid1f")
    void (*fEnable)(GLenum); R(fEnable, "glEnable")
#undef R
    drainErrors();

    // --- Phase 1: the whole surface answers, through every entry point ---
    const int rows = (int)(sizeof kState / sizeof kState[0]);
    int unanswered = 0;
    for (int i = 0; i < rows; ++i) {
        GLint iv[16]; GLfloat fv[16]; GLdouble dv[16]; GLboolean bv[16];
        for (int k = 0; k < 16; ++k) { iv[k] = -12345; fv[k] = -12345.0f; dv[k] = -12345.0; bv[k] = 42; }
        fGetIntegerv(kState[i].pname, iv); const GLenum ei = fGetError();
        fGetFloatv(kState[i].pname, fv);   const GLenum ef = fGetError();
        fGetBooleanv(kState[i].pname, bv); const GLenum eb = fGetError();
        fGetDoublev(kState[i].pname, dv);  const GLenum ed = fGetError();
        if (ei || ef || eb || ed) {
            printf("*** %s raised 0x%x/0x%x/0x%x/0x%x\n", kState[i].name, ei, ef, eb, ed);
            ++failures;
            continue;
        }
        // The failure that hides: no error, and the buffer never written.
        if (iv[0] == -12345 && fv[0] == -12345.0f && dv[0] == -12345.0) {
            printf("*** %s answered nothing at all\n", kState[i].name);
            ++unanswered;
            ++failures;
            continue;
        }
        // The four forms are one value in four types. Colours and normals
        // are the documented exception for the integer form, so compare the
        // ones that never rescale.
        for (int k = 0; k < kState[i].count; ++k) {
            if (fabs(dv[k] - (GLdouble)fv[k]) > 1e-4 * (1.0 + fabs(dv[k]))) {
                printf("*** %s[%d]: float says %g, double says %g\n", kState[i].name, k,
                       (double)fv[k], dv[k]);
                ++failures;
            }
            const GLboolean want = dv[k] != 0.0 ? GL_TRUE : GL_FALSE;
            if (bv[k] != want) {
                printf("*** %s[%d]: value %g but boolean says %d\n", kState[i].name, k, dv[k],
                       (int)bv[k]);
                ++failures;
            }
        }
    }
    printf("swept %d state variables, %d unanswered\n", rows, unanswered);

    // --- Phase 2: initial values the GL 2.1 manual states ---
    expectInt("GL_FOG_MODE initial", 0x0B65, GL_EXP);
    expectFloat("GL_FOG_DENSITY initial", 0x0B62, 1.0f);
    expectFloat("GL_FOG_START initial", 0x0B63, 0.0f);
    expectFloat("GL_FOG_END initial", 0x0B64, 1.0f);
    expectInt("GL_SHADE_MODEL initial", 0x0B54, GL_SMOOTH);
    expectInt("GL_MATRIX_MODE initial", 0x0BA0, GL_MODELVIEW);
    expectInt("GL_MODELVIEW_STACK_DEPTH initial", 0x0BA3, 1);
    expectInt("GL_MAX_LIGHTS", 0x0D31, 8);
    expectInt("GL_LIST_BASE initial", 0x0B32, 0);
    expectInt("GL_RENDER_MODE initial", 0x0C40, GL_RENDER);
    expectInt("GL_VERTEX_ARRAY_SIZE initial", 0x807A, 4);
    expectInt("GL_VERTEX_ARRAY_TYPE initial", 0x807B, GL_FLOAT);
    expectInt("GL_CLIENT_ACTIVE_TEXTURE initial", 0x84E1, GL_TEXTURE0);
    expectInt("GL_POLYGON_MODE initial", 0x0B40, GL_FILL);
    expectInt("GL_ATTRIB_STACK_DEPTH initial", 0x0BB0, 0);
    expectInt("GL_MAP1_GRID_SEGMENTS initial", 0x0DD1, 1);
    expectFloat("GL_ZOOM_X initial", 0x0D16, 1.0f);
    expectFloat("GL_ALPHA_TEST_REF initial", 0x0BC2, 0.0f);
    expectFloat("GL_RED_SCALE initial", 0x0D14, 1.0f);
    expectFloat("GL_RED_BIAS initial", 0x0D15, 0.0f);
    {
        GLfloat colour[4] = {0, 0, 0, 0};
        fGetFloatv(0x0B00, colour); // GL_CURRENT_COLOR
        if (colour[0] != 1.0f || colour[1] != 1.0f || colour[2] != 1.0f || colour[3] != 1.0f) {
            printf("*** GL_CURRENT_COLOR initial: %g %g %g %g\n", (double)colour[0],
                   (double)colour[1], (double)colour[2], (double)colour[3]);
            ++failures;
        }
        GLfloat matrix[16] = {0};
        fGetFloatv(0x0BA6, matrix); // GL_MODELVIEW_MATRIX
        for (int i = 0; i < 16; ++i) {
            const GLfloat want = (i % 5) == 0 ? 1.0f : 0.0f;
            if (fabsf(matrix[i] - want) > 1e-6f) {
                printf("*** GL_MODELVIEW_MATRIX initial is not identity at %d: %g\n", i,
                       (double)matrix[i]);
                ++failures;
                break;
            }
        }
    }
    drainErrors();

    // --- Phase 3: Sodium's fog occlusion, exactly as it asks ---
    fFogi(0x0B65, GL_LINEAR);   // glFogi(GL_FOG_MODE, GL_LINEAR)
    fFogf(0x0B64, 128.0f);      // glFogf(GL_FOG_END, 128)
    expectInt("fog occlusion: GL_FOG_MODE after glFogi", 0x0B65, GL_LINEAR);
    expectFloat("fog occlusion: GL_FOG_END after glFogf", 0x0B64, 128.0f);
    fFogi(0x0B65, GL_EXP);
    fFogf(0x0B62, 0.015f);      // glFogf(GL_FOG_DENSITY, ...)
    expectInt("fog occlusion: GL_FOG_MODE back to GL_EXP", 0x0B65, GL_EXP);
    expectFloat("fog occlusion: GL_FOG_DENSITY", 0x0B62, 0.015f);
    drainErrors();

    // --- Phase 4: state set through the API comes back out ---
    fShadeModel(GL_FLAT);
    expectInt("glShadeModel round trip", 0x0B54, GL_FLAT);
    fHint(0x0C54, GL_NICEST); // GL_FOG_HINT: a no-op hint is still state
    expectInt("glHint(GL_FOG_HINT) round trip", 0x0C54, GL_NICEST);
    fClearAccum(0.25f, 0.5f, 0.75f, 1.0f);
    {
        GLfloat accum[4] = {0};
        fGetFloatv(0x0B80, accum); // GL_ACCUM_CLEAR_VALUE
        if (fabsf(accum[0] - 0.25f) > 1e-5f || fabsf(accum[2] - 0.75f) > 1e-5f) {
            printf("*** GL_ACCUM_CLEAR_VALUE round trip: %g %g %g %g\n", (double)accum[0],
                   (double)accum[1], (double)accum[2], (double)accum[3]);
            ++failures;
        }
    }
    fMapGrid1f(7, 2.0f, 5.0f);
    expectInt("glMapGrid1f segments", 0x0DD1, 7);
    {
        GLfloat domain[2] = {0};
        fGetFloatv(0x0DD0, domain); // GL_MAP1_GRID_DOMAIN
        if (fabsf(domain[0] - 2.0f) > 1e-5f || fabsf(domain[1] - 5.0f) > 1e-5f) {
            printf("*** GL_MAP1_GRID_DOMAIN round trip: %g %g\n", (double)domain[0],
                   (double)domain[1]);
            ++failures;
        }
    }
    fMatrixMode(GL_PROJECTION);
    fPushMatrix();
    expectInt("GL_PROJECTION_STACK_DEPTH after push", 0x0BA4, 2);
    fPopMatrix();
    fMatrixMode(GL_MODELVIEW);
    fPushAttrib(GL_ALL_ATTRIB_BITS);
    expectInt("GL_ATTRIB_STACK_DEPTH after push", 0x0BB0, 1);
    fPopAttrib();
    expectInt("GL_ATTRIB_STACK_DEPTH after pop", 0x0BB0, 0);
    drainErrors();

    // --- Phase 5: enables agree between glIsEnabled and the getters ---
    fEnableClientState(GL_COLOR_ARRAY);
    {
        GLboolean b = GL_FALSE;
        fGetBooleanv(GL_COLOR_ARRAY, &b);
        if (!b || !fIsEnabled(GL_COLOR_ARRAY)) {
            printf("*** GL_COLOR_ARRAY enabled but reported %d / %d\n", (int)b,
                   (int)fIsEnabled(GL_COLOR_ARRAY));
            ++failures;
        }
    }
    fEnable(GL_MAP1_VERTEX_3);
    if (!fIsEnabled(GL_MAP1_VERTEX_3)) {
        printf("*** glIsEnabled(GL_MAP1_VERTEX_3) is false after glEnable\n");
        ++failures;
    }
    drainErrors();
    // A state variable is not a capability: glIsEnabled must reject it.
    fIsEnabled(0x0B65); // GL_FOG_MODE
    if (fGetError() != GL_INVALID_ENUM) {
        printf("*** glIsEnabled(GL_FOG_MODE) did not raise GL_INVALID_ENUM\n");
        ++failures;
    }
    drainErrors();

    printf(failures ? "FAIL: %d\n" : "OK (%d)\n", failures);
    return failures ? 1 : 0;
}
