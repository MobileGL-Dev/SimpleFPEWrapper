// SimpleFPEWrapper - SimpleFPEWrapper/shader/translator.cpp
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// plans/09 9.1: user GLSL 1.10/1.20 -> ESSL 3.00. The preprocessor only
// does what the toolchain cannot: rewriting compatibility-profile builtins
// (gl_Vertex, gl_ModelViewMatrix, gl_LightSource, ...) onto fpe_* symbols
// declared in an injected prelude, because gl_-prefixed names are reserved
// and cannot be #define'd. Syntax-level conversion (attribute/varying,
// gl_FragColor, texture2D, ...) is glslang's and SPIRV-Cross's job.

#include "translator.h"
#include "../log.h"
#include "../init.h"

#include <glslang/Public/ShaderLang.h>
#include <glslang/Public/ResourceLimits.h>
#include <SPIRV/GlslangToSpv.h>
#include <spirv_glsl.hpp>

#include <cctype>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <cstring>
#include <algorithm>

namespace SFPEW::Shader {

namespace {

// Identifier replacements shared by both stages. Struct-typed builtins
// (gl_LightSource, gl_Fog, materials) map onto identically-shaped fpe_*
// structs declared in the prelude, so member accesses survive verbatim.
const std::unordered_map<std::string, std::string>& commonReplacements() {
    static const std::unordered_map<std::string, std::string> map = {
        {"gl_ModelViewMatrix", "fpe_ModelViewMatrix"},
        {"gl_ProjectionMatrix", "fpe_ProjectionMatrix"},
        {"gl_ModelViewProjectionMatrix", "fpe_ModelViewProjectionMatrix"},
        {"gl_NormalMatrix", "fpe_NormalMatrix"},
        {"gl_ModelViewMatrixInverse", "fpe_ModelViewMatrixInverse"},
        {"gl_ProjectionMatrixInverse", "fpe_ProjectionMatrixInverse"},
        {"gl_TextureMatrix", "fpe_TextureMatrix"},
        {"gl_LightSource", "fpe_LightSource"},
        {"gl_LightModel", "fpe_LightModel"},
        {"gl_FrontMaterial", "fpe_FrontMaterial"},
        {"gl_BackMaterial", "fpe_BackMaterial"},
        {"gl_Fog", "fpe_Fog"},
        {"gl_ClipPlane", "fpe_ClipPlane"},
        {"gl_TexCoord", "fpe_TexCoord"},
        {"gl_FogFragCoord", "fpe_FogFragCoord"},
        {"gl_NormalScale", "fpe_NormalScale"},
        // Legacy sampler builtins vanished from core profiles; the modern
        // overload set accepts the same argument shapes.
        {"texture1D", "texture"},
        {"texture2D", "texture"},
        {"texture3D", "texture"},
        {"textureCube", "texture"},
        {"shadow1D", "texture"},
        {"shadow2D", "texture"},
        {"texture1DProj", "textureProj"},
        {"texture2DProj", "textureProj"},
        {"texture3DProj", "textureProj"},
        {"shadow1DProj", "textureProj"},
        {"shadow2DProj", "textureProj"},
        {"texture1DLod", "textureLod"},
        {"texture2DLod", "textureLod"},
        {"texture3DLod", "textureLod"},
        {"textureCubeLod", "textureLod"},
        {"texture1DProjLod", "textureProjLod"},
        {"texture2DProjLod", "textureProjLod"},
        {"texture3DProjLod", "textureProjLod"},
    };
    return map;
}

const std::unordered_map<std::string, std::string>& vertexReplacements() {
    static const std::unordered_map<std::string, std::string> map = {
        {"gl_Vertex", "fpe_Vertex"},
        {"gl_Normal", "fpe_Normal"},
        {"gl_Color", "fpe_Color"},
        {"gl_SecondaryColor", "fpe_SecondaryColor"},
        {"gl_FogCoord", "fpe_FogCoord"},
        {"gl_MultiTexCoord0", "fpe_MultiTexCoord0"},
        {"gl_MultiTexCoord1", "fpe_MultiTexCoord1"},
        {"gl_MultiTexCoord2", "fpe_MultiTexCoord2"},
        {"gl_MultiTexCoord3", "fpe_MultiTexCoord3"},
        {"gl_MultiTexCoord4", "fpe_MultiTexCoord4"},
        {"gl_MultiTexCoord5", "fpe_MultiTexCoord5"},
        {"gl_MultiTexCoord6", "fpe_MultiTexCoord6"},
        {"gl_MultiTexCoord7", "fpe_MultiTexCoord7"},
        {"gl_FrontColor", "fpe_FrontColor"},
        {"gl_BackColor", "fpe_BackColor"},
        {"gl_FrontSecondaryColor", "fpe_FrontSecondaryColor"},
        {"gl_BackSecondaryColor", "fpe_BackSecondaryColor"},
        {"attribute", "in"},
        {"varying", "out"},
    };
    return map;
}

const std::unordered_map<std::string, std::string>& fragmentReplacements() {
    // In a fragment shader gl_Color/gl_SecondaryColor are the interpolated
    // varyings, not the vertex attributes: same names, different symbols.
    static const std::unordered_map<std::string, std::string> map = {
        {"gl_Color", "fpe_FrontColor"},
        {"gl_SecondaryColor", "fpe_FrontSecondaryColor"},
        // Writing gl_FragColor is not expressible in SPIR-V; route the
        // output through our own out variable.
        {"gl_FragColor", "fpe_FragColor"},
        {"gl_FragData", "fpe_FragData"},
        {"varying", "in"},
    };
    return map;
}

constexpr char kSharedPrelude[] = R"(
struct fpe_LightSourceParameters {
    vec4 ambient; vec4 diffuse; vec4 specular; vec4 position;
    vec4 halfVector; vec3 spotDirection;
    float spotExponent; float spotCutoff; float spotCosCutoff;
    float constantAttenuation; float linearAttenuation; float quadraticAttenuation;
};
struct fpe_LightModelParameters { vec4 ambient; };
struct fpe_MaterialParameters {
    vec4 emission; vec4 ambient; vec4 diffuse; vec4 specular; float shininess;
};
struct fpe_FogParameters { vec4 color; float density; float start; float end; float scale; };
uniform mat4 fpe_ModelViewMatrix;
uniform mat4 fpe_ProjectionMatrix;
uniform mat4 fpe_ModelViewProjectionMatrix;
uniform mat3 fpe_NormalMatrix;
uniform mat4 fpe_ModelViewMatrixInverse;
uniform mat4 fpe_ProjectionMatrixInverse;
uniform mat4 fpe_TextureMatrix[8];
uniform fpe_LightSourceParameters fpe_LightSource[8];
uniform fpe_LightModelParameters fpe_LightModel;
uniform fpe_MaterialParameters fpe_FrontMaterial;
uniform fpe_MaterialParameters fpe_BackMaterial;
uniform fpe_FogParameters fpe_Fog;
uniform vec4 fpe_ClipPlane[6];
uniform float fpe_NormalScale;
)";

constexpr char kVertexPrelude[] = R"(
in vec4 fpe_Vertex;
in vec3 fpe_Normal;
in vec4 fpe_Color;
in vec4 fpe_SecondaryColor;
in float fpe_FogCoord;
in vec4 fpe_MultiTexCoord0;
in vec4 fpe_MultiTexCoord1;
in vec4 fpe_MultiTexCoord2;
in vec4 fpe_MultiTexCoord3;
in vec4 fpe_MultiTexCoord4;
in vec4 fpe_MultiTexCoord5;
in vec4 fpe_MultiTexCoord6;
in vec4 fpe_MultiTexCoord7;
out vec4 fpe_FrontColor;
out vec4 fpe_BackColor;
out vec4 fpe_FrontSecondaryColor;
out vec4 fpe_BackSecondaryColor;
out vec4 fpe_TexCoord[8];
out float fpe_FogFragCoord;
vec4 ftransform() { return fpe_ModelViewProjectionMatrix * fpe_Vertex; }
)";

constexpr char kFragmentPrelude[] = R"(
in vec4 fpe_FrontColor;
in vec4 fpe_BackColor;
in vec4 fpe_FrontSecondaryColor;
in vec4 fpe_BackSecondaryColor;
in vec4 fpe_TexCoord[8];
in float fpe_FogFragCoord;
)";

constexpr char kFragColorOut[] = "out vec4 fpe_FragColor;\n";
constexpr char kFragDataOut[] = "out vec4 fpe_FragData[4];\n";

bool identChar(char c) { return std::isalnum((unsigned char)c) || c == '_'; }

// Token-level identifier replacement; #define cannot touch reserved gl_*.
std::string replaceIdentifiers(const std::string& src,
                               const std::unordered_map<std::string, std::string>& primary,
                               const std::unordered_map<std::string, std::string>& stage) {
    std::string out;
    out.reserve(src.size() + src.size() / 8);
    size_t i = 0;
    while (i < src.size()) {
        const char c = src[i];
        if (c == '/' && i + 1 < src.size() && (src[i + 1] == '/' || src[i + 1] == '*')) {
            // Copy comments verbatim.
            if (src[i + 1] == '/') {
                while (i < src.size() && src[i] != '\n') out += src[i++];
            } else {
                out += src[i++];
                out += src[i++];
                while (i + 1 < src.size() && !(src[i] == '*' && src[i + 1] == '/')) out += src[i++];
                if (i + 1 < src.size()) { out += src[i++]; out += src[i++]; }
            }
            continue;
        }
        if (identChar(c) && !std::isdigit((unsigned char)c)) {
            size_t j = i;
            while (j < src.size() && identChar(src[j])) ++j;
            const std::string word = src.substr(i, j - i);
            if (auto sit = stage.find(word); sit != stage.end()) {
                out += sit->second;
            } else if (auto pit = primary.find(word); pit != primary.end()) {
                out += pit->second;
            } else {
                out += word;
            }
            i = j;
            continue;
        }
        out += c;
        ++i;
    }
    return out;
}

std::once_flag g_glslang_once;

} // namespace

std::string preprocess(GLenum stage, const std::string& source) {
    const bool vertex = stage == GL_VERTEX_SHADER;

    // Strip the caller's #version: glslang's OpenGL-SPIR-V environment only
    // initializes its builtin tables for modern versions, so the toolchain
    // input is always "450 compatibility" - a superset in which every 1.10/
    // 1.20 construct (attribute/varying/texture2D) is still legal.
    std::string body = source;
    const size_t vpos = body.find("#version");
    if (vpos != std::string::npos) {
        const size_t eol = body.find('\n', vpos);
        body = body.substr(0, vpos) + body.substr(eol == std::string::npos ? body.size() : eol + 1);
    }
    const std::string version_line = "#version 450\n"; // SPIR-V rejects compatibility

    const std::string rewritten =
        replaceIdentifiers(body, commonReplacements(),
                           vertex ? vertexReplacements() : fragmentReplacements());

    std::string result = version_line;
    result += kSharedPrelude;
    result += vertex ? kVertexPrelude : kFragmentPrelude;
    if (!vertex) {
        // FragColor and FragData[0] would collide on one location; declare
        // only what the shader actually writes (GL forbids mixing them).
        if (rewritten.find("fpe_FragData") != std::string::npos)
            result += kFragDataOut;
        else
            result += kFragColorOut;
    }
    result += "#line 1\n"; // keep glslang diagnostics on user line numbers
    result += rewritten;
    return result;
}

translation_result_t translate(GLenum stage, const std::string& source,
                               const target_language_t& target) {
    translation_result_t result;
    result.preprocessed = preprocess(stage, source);

    std::call_once(g_glslang_once, [] { glslang::InitializeProcess(); });

    const EShLanguage lang = stage == GL_VERTEX_SHADER ? EShLangVertex : EShLangFragment;
    glslang::TShader shader(lang);
    const char* text = result.preprocessed.c_str();
    shader.setStrings(&text, 1);
    shader.setEnvInput(glslang::EShSourceGlsl, lang, glslang::EShClientOpenGL, 450);
    shader.setEnvClient(glslang::EShClientOpenGL, glslang::EShTargetOpenGL_450);
    shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_0);
    // OpenGL SPIR-V requires explicit locations/bindings; auto-map them and
    // let the linker resolve attributes by name from the ESSL output.
    shader.setAutoMapLocations(true);
    shader.setAutoMapBindings(true);

    const EShMessages messages = (EShMessages)(EShMsgSpvRules | EShMsgKeepUncalled);
    if (!shader.parse(GetDefaultResources(), 450, ECoreProfile, false, false, messages)) {
        result.log = std::string("glslang parse:\n") + shader.getInfoLog();
        return result;
    }

    glslang::TProgram program;
    program.addShader(&shader);
    if (!program.link(messages) || !program.mapIO()) {
        result.log = std::string("glslang link:\n") + program.getInfoLog();
        return result;
    }

    std::vector<unsigned int> spirv;
    glslang::SpvOptions options;
    options.disableOptimizer = true;
    options.validate = false;
    glslang::GlslangToSpv(*program.getIntermediate(lang), spirv, &options);
    if (spirv.empty()) {
        result.log = "glslang produced no SPIR-V";
        return result;
    }

    try {
        spirv_cross::CompilerGLSL compiler(std::move(spirv));
        spirv_cross::CompilerGLSL::Options opts;
        opts.version = target.version;
        opts.es = target.es;
        opts.vulkan_semantics = false;
        opts.enable_420pack_extension = false;
        compiler.set_common_options(opts);
        result.essl = compiler.compile();
        result.ok = true;
    } catch (const std::exception& e) {
        result.log = std::string("SPIRV-Cross: ") + e.what();
    }
    return result;
}

} // namespace SFPEW::Shader

namespace SFPEW::Shader {

target_language_t detect_backend_target() {
    // Cached per context like the other logical shadows.
    static thread_local struct {
        EGLContext context = (EGLContext)(intptr_t)-1;
        target_language_t target{};
    } cache;
    const EGLContext current =
        g_eglFuncs.eglGetCurrentContext ? g_eglFuncs.eglGetCurrentContext() : EGL_NO_CONTEXT;
    if (cache.context == current) return cache.target;

    target_language_t target; // safe default: ESSL 300
    if (current != EGL_NO_CONTEXT && g_glFuncs.glGetString != nullptr &&
        g_glFuncs.glGetIntegerv != nullptr) {
        const char* version = (const char*)g_glFuncs.glGetString(GL_VERSION);
        GLint major = 3, minor = 0;
        g_glFuncs.glGetIntegerv(GL_MAJOR_VERSION, &major);
        g_glFuncs.glGetIntegerv(GL_MINOR_VERSION, &minor);
        if (version != nullptr && std::strstr(version, "OpenGL ES") != nullptr) {
            target.es = true;
            target.version = major >= 3 ? (unsigned)(300 + minor * 10) : 300;
            if (target.version > 320) target.version = 320;
        } else if (version != nullptr) {
            target.es = false;
            target.version = (unsigned)(major * 100 + minor * 10); // 420, 450, ...
            if (target.version < 330) target.version = 330;
        }
    }
    cache.context = current;
    cache.target = target;
    return target;
}

} // namespace SFPEW::Shader

// C hook so the CTest suite can exercise the pipeline through dlopen.
extern "C" __attribute__((visibility("default"))) int
sfpewTranslateGlslForTest(unsigned int stage, const char* source, char* out, int out_size) {
    if (source == nullptr || out == nullptr || out_size <= 0) return -1;
    // The test hook targets the backend when one is current, else ESSL 300.
    const auto result = SFPEW::Shader::translate(stage, source, SFPEW::Shader::detect_backend_target());
    const std::string& payload = result.ok ? result.essl : result.log;
    const int n = (int)std::min((size_t)out_size - 1, payload.size());
    std::memcpy(out, payload.data(), (size_t)n);
    out[n] = '\0';
    return result.ok ? 0 : 1;
}
