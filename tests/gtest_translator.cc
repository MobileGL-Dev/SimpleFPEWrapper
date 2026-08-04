// SimpleFPEWrapper - tests/gtest_translator.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// End-to-end tests for the plans/09 GLSL translation pipeline:
//  1. Translate real GLSL 1.10/1.20 shaders (compat builtins, ftransform,
//     texture2D, gl_FragColor) through preprocessor+glslang+SPIRV-Cross.
//  2. Assert the ESSL 3.00 output shape.
//  3. If a surfaceless EGL + GLES3 context is available on this machine,
//     feed every translated shader to the REAL driver's glCompileShader
//     and require GL_COMPILE_STATUS == GL_TRUE. Skipped when no GL device
//     exists (e.g. CI without a GPU/llvmpipe).

#include "sfpew_gtest.h"

#include <GLES3/gl3.h>

#include <cstring>

namespace {

using sfpew_test::ContextTest;
using sfpew_test::GLenum;

constexpr GLenum GL_VERTEX_SHADER_ = 0x8B31;
constexpr GLenum GL_FRAGMENT_SHADER_ = 0x8B30;

using TranslateFn = int (*)(unsigned int, const char*, char*, int);
using PassthroughFn = int (*)(unsigned int, const char*, int, unsigned int);

const char* kVertex110 =
    "#version 110\n"
    "varying vec2 uv;\n"
    "void main() {\n"
    "    uv = (gl_TextureMatrix[0] * gl_MultiTexCoord0).xy;\n"
    "    gl_FrontColor = gl_Color * gl_LightSource[0].diffuse;\n"
    "    gl_Position = ftransform();\n"
    "}\n";

const char* kFragment110 =
    "#version 110\n"
    "uniform sampler2D tex;\n"
    "varying vec2 uv;\n"
    "void main() {\n"
    "    vec4 c = texture2D(tex, uv) * gl_Color;\n"
    "    float fog = clamp((gl_Fog.end - gl_FogFragCoord) * gl_Fog.scale, 0.0, 1.0);\n"
    "    gl_FragColor = mix(gl_Fog.color, c, fog);\n"
    "}\n";

const char* kVertex120 =
    "#version 120\n"
    "attribute vec3 aPos;\n"
    "uniform mat4 mvp;\n"
    "varying vec3 normal;\n"
    "void main() {\n"
    "    normal = gl_NormalMatrix * gl_Normal;\n"
    "    gl_Position = mvp * vec4(aPos, 1.0) + gl_ModelViewProjectionMatrix * gl_Vertex * 0.0;\n"
    "}\n";

const char* kVertex120NonSquare =
    "#version 120\n"
    "uniform mat4x3 packed;\n"
    "void main() {\n"
    "    vec3 row = packed * gl_Vertex;\n"
    "    gl_Position = vec4(row, 1.0) + ftransform() * 0.5;\n"
    "}\n";

const char* kFragmentData =
    "#version 120\n"
    "void main() {\n"
    "    gl_FragData[0] = vec4(gl_TexCoord[0].st, 0.0, 1.0);\n"
    "}\n";

// OptiFine shader-pack shape: a sampler NAMED `texture` (legal in 1.20,
// where `texture` is not a builtin) plus gl_FogFragCoord and gl_FragData.
const char* kVertexOptiFine =
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

const char* kFragmentOptiFine =
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
const char* kFragmentShadow =
    "#version 120\n"
    "uniform sampler2DShadow shadowtex0;\n"
    "varying vec4 shadowposition;\n"
    "void main() {\n"
    "    float shadow0 = shadow2D(shadowtex0, shadowposition.xyz).z;\n"
    "    gl_FragColor = vec4(vec3(shadow0), 1.0);\n"
    "}\n";

const char* kFragmentGpuShader4 =
    "#version 120\n"
    "#extension GL_EXT_gpu_shader4 : enable\n"
    "uniform sampler2D colortex0;\n"
    "void main() {\n"
    "    gl_FragColor = texelFetch2D(colortex0, ivec2(gl_FragCoord.xy), 0);\n"
    "}\n";

const char* kFragment400Compat =
    "#version 400 compatibility\n"
    "uniform sampler2D texture;\n"
    "varying vec2 texcoord;\n"
    "void main() {\n"
    "    gl_FragData[0] = texture2D(texture, texcoord);\n"
    "}\n";

struct TranslationCase {
    const char* tag;
    GLenum stage;
    const char* source;
};

static const TranslationCase kTranslationCases[] = {
    {"vs110", GL_VERTEX_SHADER_, kVertex110},
    {"fs110", GL_FRAGMENT_SHADER_, kFragment110},
    {"vs120", GL_VERTEX_SHADER_, kVertex120},
    {"vs120-mat4x3", GL_VERTEX_SHADER_, kVertex120NonSquare},
    {"fs120-fragdata", GL_FRAGMENT_SHADER_, kFragmentData},
    {"vs120-optifine", GL_VERTEX_SHADER_, kVertexOptiFine},
    {"fs120-optifine-texture-sampler", GL_FRAGMENT_SHADER_, kFragmentOptiFine},
    {"fs120-shadow2D-swizzle", GL_FRAGMENT_SHADER_, kFragmentShadow},
    {"fs120-gpu-shader4", GL_FRAGMENT_SHADER_, kFragmentGpuShader4},
    {"fs400-compat-texture-sampler", GL_FRAGMENT_SHADER_, kFragment400Compat},
};

struct PassthroughCase {
    const char* tag;
    GLenum stage;
    const char* source;
    int backend_es;
    unsigned backend_version;
    int expect;
};

static const PassthroughCase kPassthroughCases[] = {
    {"es300-on-es320", GL_VERTEX_SHADER_, "#version 300 es\nvoid main() {}\n", 1, 320, 1},
    {"es310-on-es320", GL_VERTEX_SHADER_, "#version 310 es\nvoid main() {}\n", 1, 320, 1},
    {"es320-on-es310", GL_VERTEX_SHADER_, "#version 320 es\nvoid main() {}\n", 1, 310, 0},
    {"glsl100-on-desktop450", GL_VERTEX_SHADER_, "#version 100\nvoid main() {}\n", 0, 450, 0},
    {"glsl110-on-desktop450", GL_VERTEX_SHADER_, "#version 110\nvoid main() {}\n", 0, 450, 0},
    {"glsl110-compat-on-desktop450", GL_VERTEX_SHADER_,
     "#version 110 compatibility\nvoid main() {}\n", 0, 450, 0},
    {"glsl130-on-desktop450", GL_VERTEX_SHADER_, "#version 130\nvoid main() {}\n", 0, 450, 0},
    {"glsl140-on-desktop450", GL_VERTEX_SHADER_, "#version 140\nvoid main() {}\n", 0, 450, 0},
    {"glsl150-on-desktop450", GL_VERTEX_SHADER_, "#version 150\nvoid main() {}\n", 0, 450, 1},
    {"glsl150-compat-on-desktop450", GL_VERTEX_SHADER_,
     "#version 150 compatibility\nvoid main() {}\n", 0, 450, 0},
    {"glsl330-compat-on-desktop450", GL_VERTEX_SHADER_,
     "#version 330 compatibility\nvoid main() {}\n", 0, 450, 0},
    {"glsl400-compat-on-desktop450", GL_FRAGMENT_SHADER_,
     "#version 400 compatibility\nvoid main() {}\n", 0, 450, 0},
    {"glsl460-compat-on-desktop450", GL_VERTEX_SHADER_,
     "#version 460 compatibility\nvoid main() {}\n", 0, 460, 0},
    {"glsl460-on-desktop450", GL_VERTEX_SHADER_, "#version 460\nvoid main() {}\n", 0, 450, 0},
    {"essl-on-desktop", GL_VERTEX_SHADER_, "#version 300 es\nvoid main() {}\n", 0, 450, 0},
    {"desktop-on-gles", GL_VERTEX_SHADER_, "#version 110\nvoid main() {}\n", 1, 300, 0},
    {"no-version-desktop", GL_VERTEX_SHADER_, "void main() {}\n", 0, 450, 0},
    {"no-version-gles", GL_VERTEX_SHADER_, "void main() {}\n", 1, 300, 0},
    {"late-version-invalid", GL_VERTEX_SHADER_, "void main() {}\n#version 110\n", 0, 450, 0},
};

class TranslatorTest : public ::testing::Test {
protected:
    void SetUp() override {
        library_ = dlopen(WRAPPER_LIB_PATH, RTLD_NOW | RTLD_LOCAL);
        ASSERT_NE(library_, nullptr) << "dlopen(" WRAPPER_LIB_PATH "): " << dlerror();
        translate_ = reinterpret_cast<TranslateFn>(dlsym(library_, "sfpewTranslateGlslForTest"));
        ASSERT_NE(translate_, nullptr) << "sfpewTranslateGlslForTest not exported";
        can_passthrough_ =
            reinterpret_cast<PassthroughFn>(dlsym(library_, "sfpewShaderCanPassthroughForTest"));
        ASSERT_NE(can_passthrough_, nullptr) << "sfpewShaderCanPassthroughForTest not exported";
    }

    void TearDown() override {
        if (library_ != nullptr) dlclose(library_);
    }

    // Translates and checks the output shape, or returns false with the
    // failure described.
    bool TranslateAndCheck(GLenum stage, const char* source, const char* tag) {
        if (translate_(stage, source, out_buf_, sizeof out_buf_) != 0) {
            ADD_FAILURE() << "[" << tag << "] translation failed:\n" << out_buf_;
            return false;
        }
        if (std::strstr(out_buf_, "#version 300 es") == nullptr) {
            ADD_FAILURE() << "[" << tag << "] output is not ESSL 300:\n" << out_buf_;
            return false;
        }
        if (std::strstr(out_buf_, "gl_Vertex") != nullptr ||
            std::strstr(out_buf_, "attribute ") != nullptr ||
            std::strstr(out_buf_, "gl_FragColor") != nullptr) {
            ADD_FAILURE() << "[" << tag << "] legacy constructs leaked into ESSL:\n" << out_buf_;
            return false;
        }
        return true;
    }

    void CheckPassthrough(const PassthroughCase& c) {
        const int got =
            can_passthrough_(c.stage, c.source, c.backend_es, c.backend_version);
        EXPECT_EQ(got, c.expect) << "[" << c.tag << "] passthrough";
    }

    TranslateFn translate_ = nullptr;
    PassthroughFn can_passthrough_ = nullptr;
    char out_buf_[1 << 16];
    void* library_ = nullptr;
};

TEST_F(TranslatorTest, NativePassthroughDecisions) {
    for (const auto& c : kPassthroughCases) CheckPassthrough(c);
}

TEST_F(TranslatorTest, LegacyShadersTranslateToEssl300) {
    for (const auto& c : kTranslationCases) {
        ASSERT_TRUE(TranslateAndCheck(c.stage, c.source, c.tag));
    }
}

// The driver-compile phase needs a real ES3 context, which changes what
// version the translator targets (320 on this backend) - so it lives in its
// own fixture and its own process, and the shape checks above stay in the
// context-free one where the target is the default 300.
class TranslatorCompileTest : public ContextTest {
protected:
    void SetUp() override {
        ContextTest::SetUp();
        if (::testing::Test::IsSkipped()) return;
        translate_ = Dlsym<TranslateFn>("sfpewTranslateGlslForTest");
        ASSERT_NE(translate_, nullptr) << "sfpewTranslateGlslForTest not exported";
    }

    bool CompileOnDevice(GLenum stage, const char* tag) {
        int index = -1;
        for (size_t i = 0; i < sizeof(kTranslationCases) / sizeof(kTranslationCases[0]); ++i)
            if (std::strcmp(kTranslationCases[i].tag, tag) == 0) {
                index = static_cast<int>(i);
                break;
            }
        if (index < 0) return false;
        if (translate_(stage, kTranslationCases[index].source, out_buf_, sizeof out_buf_) != 0)
            return false;
        GLuint shader = glCreateShader(stage);
        const GLchar* source = out_buf_;
        glShaderSource(shader, 1, &source, nullptr);
        glCompileShader(shader);
        GLint ok = GL_FALSE;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
        if (ok != GL_TRUE) {
            char info[2048] = {};
            glGetShaderInfoLog(shader, sizeof info, nullptr, info);
            ADD_FAILURE() << "[" << tag << "] driver rejected translated ESSL:\n"
                          << info << "\n---\n" << out_buf_;
        }
        glDeleteShader(shader);
        return ok == GL_TRUE;
    }

    TranslateFn translate_ = nullptr;
    char out_buf_[1 << 16];
};

TEST_F(TranslatorCompileTest, TranslationsCompileOnTheRealDriver) {
    for (const auto& c : kTranslationCases) {
        ASSERT_TRUE(CompileOnDevice(c.stage, c.tag));
    }
}

} // namespace
