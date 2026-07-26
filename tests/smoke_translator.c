// SimpleFPEWrapper - tests/smoke_translator.c
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// End-to-end test for the plans/09 GLSL translation pipeline:
//  1. Translate real GLSL 1.10/1.20 shaders (compat builtins, ftransform,
//     texture2D, gl_FragColor) through preprocessor+glslang+SPIRV-Cross.
//  2. Assert the ESSL 3.00 output shape.
//  3. If a surfaceless EGL + GLES3 context is available on this machine,
//     feed every translated shader to the REAL driver's glCompileShader
//     and require GL_COMPILE_STATUS == GL_TRUE. Skipped (exit 77) when no
//     GL device exists (e.g. CI without a GPU/llvmpipe).

#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

#include <EGL/egl.h>
#include <GLES3/gl3.h>

#define GLV 0x8B31 /* GL_VERTEX_SHADER */
#define GLF 0x8B30 /* GL_FRAGMENT_SHADER */

typedef int (*translate_fn)(unsigned int, const char*, char*, int);

static const char* kVertex110 =
    "#version 110\n"
    "varying vec2 uv;\n"
    "void main() {\n"
    "    uv = (gl_TextureMatrix[0] * gl_MultiTexCoord0).xy;\n"
    "    gl_FrontColor = gl_Color * gl_LightSource[0].diffuse;\n"
    "    gl_Position = ftransform();\n"
    "}\n";

static const char* kFragment110 =
    "#version 110\n"
    "uniform sampler2D tex;\n"
    "varying vec2 uv;\n"
    "void main() {\n"
    "    vec4 c = texture2D(tex, uv) * gl_Color;\n"
    "    float fog = clamp((gl_Fog.end - gl_FogFragCoord) * gl_Fog.scale, 0.0, 1.0);\n"
    "    gl_FragColor = mix(gl_Fog.color, c, fog);\n"
    "}\n";

static const char* kVertex120 =
    "#version 120\n"
    "attribute vec3 aPos;\n"
    "uniform mat4 mvp;\n"
    "varying vec3 normal;\n"
    "void main() {\n"
    "    normal = gl_NormalMatrix * gl_Normal;\n"
    "    gl_Position = mvp * vec4(aPos, 1.0) + gl_ModelViewProjectionMatrix * gl_Vertex * 0.0;\n"
    "}\n";

static const char* kVertex120NonSquare =
    "#version 120\n"
    "uniform mat4x3 packed;\n"
    "void main() {\n"
    "    vec3 row = packed * gl_Vertex;\n"
    "    gl_Position = vec4(row, 1.0) + ftransform() * 0.5;\n"
    "}\n";

static const char* kFragmentData =
    "#version 120\n"
    "void main() {\n"
    "    gl_FragData[0] = vec4(gl_TexCoord[0].st, 0.0, 1.0);\n"
    "}\n";

// OptiFine shader-pack shape: a sampler NAMED `texture` (legal in 1.20,
// where `texture` is not a builtin) plus gl_FogFragCoord and gl_FragData.
static const char* kVertexOptiFine =
    "#version 120\n"
    "varying vec2 texcoord;\n"
    "varying vec2 lmcoord;\n"
    "varying vec4 color;\n"
    "void main() {\n"
    "    gl_Position = ftransform();\n"
    "    texcoord = (gl_TextureMatrix[0] * gl_MultiTexCoord0).xy;\n"
    "    lmcoord = (gl_TextureMatrix[1] * gl_MultiTexCoord1).xy;\n"
    "    color = gl_Color;\n"
    "    gl_FogFragCoord = length((gl_ModelViewMatrix * gl_Vertex).xyz);\n"
    "}\n";

static const char* kFragmentOptiFine =
    "#version 120\n"
    "#extension GL_ARB_shader_texture_lod : enable\n"
    "uniform sampler2D texture;\n"
    "uniform sampler2D lightmap;\n"
    "varying vec2 texcoord;\n"
    "varying vec2 lmcoord;\n"
    "varying vec4 color;\n"
    "void main() {\n"
    "    vec4 albedo = texture2D(texture, texcoord) * color;\n"
    "    albedo *= texture2D(lightmap, lmcoord);\n"
    "/* DRAWBUFFERS:0 */\n"
    "    gl_FragData[0] = albedo;\n"
    "}\n";

// Constructs harvested from real shader-pack failures (device dumps):
// legacy shadow2D returns vec4 (so .z must survive), GL_EXT_gpu_shader4
// spellings, and `sampler2D texture` under #version 400 compatibility.
static const char* kFragmentShadow =
    "#version 120\n"
    "uniform sampler2DShadow shadowtex0;\n"
    "varying vec4 shadowposition;\n"
    "void main() {\n"
    "    float shadow0 = shadow2D(shadowtex0, shadowposition.xyz).z;\n"
    "    gl_FragColor = vec4(vec3(shadow0), 1.0);\n"
    "}\n";

static const char* kFragmentGpuShader4 =
    "#version 120\n"
    "#extension GL_EXT_gpu_shader4 : enable\n"
    "uniform sampler2D colortex0;\n"
    "void main() {\n"
    "    gl_FragColor = texelFetch2D(colortex0, ivec2(gl_FragCoord.xy), 0);\n"
    "}\n";

static const char* kFragment400Compat =
    "#version 400 compatibility\n"
    "uniform sampler2D texture;\n"
    "varying vec2 texcoord;\n"
    "void main() {\n"
    "    gl_FragData[0] = texture2D(texture, texcoord);\n"
    "}\n";

static char out_buf[1 << 16];

static int translate_and_check(translate_fn translate, unsigned int stage, const char* src,
                               const char* tag) {
    if (translate(stage, src, out_buf, sizeof(out_buf)) != 0) {
        fprintf(stderr, "FAIL[%s]: translation failed:\n%s\n", tag, out_buf);
        return 0;
    }
    if (strstr(out_buf, "#version 300 es") == NULL) {
        fprintf(stderr, "FAIL[%s]: output is not ESSL 300:\n%.400s\n", tag, out_buf);
        return 0;
    }
    if (strstr(out_buf, "gl_Vertex") != NULL || strstr(out_buf, "attribute ") != NULL ||
        strstr(out_buf, "gl_FragColor") != NULL) {
        fprintf(stderr, "FAIL[%s]: legacy constructs leaked into ESSL:\n%.400s\n", tag, out_buf);
        return 0;
    }
    return 1;
}

static int compile_on_device(unsigned int gl_stage, const char* essl, const char* tag) {
    GLuint shader = glCreateShader(gl_stage);
    glShaderSource(shader, 1, &essl, NULL);
    glCompileShader(shader);
    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok != GL_TRUE) {
        char info[2048] = {0};
        glGetShaderInfoLog(shader, sizeof(info), NULL, info);
        fprintf(stderr, "FAIL[%s]: driver rejected translated ESSL:\n%s\n---\n%.800s\n", tag, info,
                essl);
    }
    glDeleteShader(shader);
    return ok == GL_TRUE;
}

int main(void) {
    void* handle = dlopen(WRAPPER_LIB_PATH, RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        fprintf(stderr, "FAIL: dlopen: %s\n", dlerror());
        return 1;
    }
    translate_fn translate = (translate_fn)dlsym(handle, "sfpewTranslateGlslForTest");
    if (!translate) {
        fprintf(stderr, "FAIL: sfpewTranslateGlslForTest not exported\n");
        return 1;
    }

    // Phase 1: offline translation shape checks.
    struct { unsigned int stage; const char* src; const char* tag; } cases[] = {
        {GLV, kVertex110, "vs110"},
        {GLF, kFragment110, "fs110"},
        {GLV, kVertex120, "vs120"},
        {GLV, kVertex120NonSquare, "vs120-mat4x3"},
        {GLF, kFragmentData, "fs120-fragdata"},
        {GLV, kVertexOptiFine, "vs120-optifine"},
        {GLF, kFragmentOptiFine, "fs120-optifine-texture-sampler"},
        {GLF, kFragmentShadow, "fs120-shadow2D-swizzle"},
        {GLF, kFragmentGpuShader4, "fs120-gpu-shader4"},
        {GLF, kFragment400Compat, "fs400-compat-texture-sampler"},
    };
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        if (!translate_and_check(translate, cases[i].stage, cases[i].src, cases[i].tag)) return 1;
    }
    printf("OK: %u shaders translated to ESSL 300\n", (unsigned)(sizeof(cases) / sizeof(cases[0])));

    // Phase 2: compile the translations on a real GLES3 device if one exists.
    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display == EGL_NO_DISPLAY || !eglInitialize(display, NULL, NULL)) {
        printf("SKIP: no EGL display; device compile phase skipped\n");
        return 77;
    }
    static const EGLint config_attribs[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE,
                                            EGL_OPENGL_ES3_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
                                            EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
    EGLConfig config;
    EGLint num_config = 0;
    if (!eglChooseConfig(display, config_attribs, &config, 1, &num_config) || num_config == 0) {
        printf("SKIP: no ES3 EGL config\n");
        return 77;
    }
    eglBindAPI(EGL_OPENGL_ES_API);
    static const EGLint ctx_attribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, ctx_attribs);
    if (context == EGL_NO_CONTEXT ||
        !eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context)) {
        printf("SKIP: could not make an ES3 context current (surfaceless unsupported?)\n");
        return 77;
    }

    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        if (translate(cases[i].stage, cases[i].src, out_buf, sizeof(out_buf)) != 0) return 1;
        const unsigned gl_stage = cases[i].stage;
        if (!compile_on_device(gl_stage, out_buf, cases[i].tag)) return 1;
    }
    printf("OK: all translations compile on the real GLES3 driver (%s)\n",
           (const char*)glGetString(GL_RENDERER));
    return 0;
}
