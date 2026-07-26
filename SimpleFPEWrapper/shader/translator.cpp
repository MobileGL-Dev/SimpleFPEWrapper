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
#include <map>
#include <unordered_map>
#include <vector>
#include <cstring>
#include <algorithm>

namespace SFPEW::Shader {

namespace {

// CompilerGLSL subclass so the protected IR is reachable: GLSL 1.20
// uniform initializers survive into the SPIR-V as OpVariable initializers
// and would be re-emitted verbatim, which every ESSL dialect rejects.
// Scrape the constant values for post-link glUniform* application and
// clear the initializer before compile().
class EsslCompiler : public spirv_cross::CompilerGLSL {
public:
    using spirv_cross::CompilerGLSL::CompilerGLSL;

    std::vector<uniform_initializer_t> scrapeUniformInitializers() {
        std::vector<uniform_initializer_t> scraped;
        ir.for_each_typed_id<spirv_cross::SPIRVariable>(
            [&](uint32_t, spirv_cross::SPIRVariable& var) {
                if (var.storage != spv::StorageClassUniformConstant || var.initializer == 0)
                    return;
                const auto* constant =
                    maybe_get<spirv_cross::SPIRConstant>(var.initializer);
                var.initializer = spirv_cross::ID(0);
                if (constant == nullptr) return;

                const auto& type = get_variable_data_type(var);
                uniform_initializer_t init;
                switch (type.basetype) {
                case spirv_cross::SPIRType::Float:
                    init.base = uniform_initializer_t::base_t::f32;
                    break;
                case spirv_cross::SPIRType::Int:
                    init.base = uniform_initializer_t::base_t::i32;
                    break;
                case spirv_cross::SPIRType::UInt:
                    init.base = uniform_initializer_t::base_t::u32;
                    break;
                case spirv_cross::SPIRType::Boolean:
                    init.base = uniform_initializer_t::base_t::b32;
                    break;
                default:
                    return; // structs/doubles: value dropped, output stays legal
                }
                if (!type.array.empty() && !type.array_size_literal.front()) return;
                init.name = get_name(var.self);
                init.columns = type.columns;
                init.vecsize = type.vecsize;
                init.array_size = type.array.empty() ? 1u : std::max(1u, type.array.front());
                flatten(*constant, type, init);
                scraped.push_back(std::move(init));
            });
        return scraped;
    }

private:
    void flatten(const spirv_cross::SPIRConstant& c, const spirv_cross::SPIRType& type,
                 uniform_initializer_t& out) {
        if (!c.subconstants.empty()) {
            const auto& inner = get<spirv_cross::SPIRType>(type.parent_type);
            for (const auto& sub : c.subconstants) {
                const auto* sc = maybe_get<spirv_cross::SPIRConstant>(sub);
                if (sc != nullptr) flatten(*sc, inner, out);
            }
            return;
        }
        for (uint32_t col = 0; col < type.columns; ++col) {
            for (uint32_t row = 0; row < type.vecsize; ++row) {
                switch (out.base) {
                case uniform_initializer_t::base_t::f32:
                    out.f.push_back(c.scalar_f32(col, row));
                    break;
                case uniform_initializer_t::base_t::i32:
                    out.i.push_back(c.scalar_i32(col, row));
                    break;
                case uniform_initializer_t::base_t::u32:
                    out.i.push_back((int)c.scalar(col, row));
                    break;
                case uniform_initializer_t::base_t::b32:
                    out.i.push_back(c.scalar(col, row) != 0 ? 1 : 0);
                    break;
                }
            }
        }
    }
};

// Strict backends (NVIDIA C1068) refuse to compile provably out-of-bounds
// array/vector/matrix indices, while desktop GL treats them as undefined
// values - and legacy shaders rely on that. Clamp every index used in an
// OpAccessChain into range (Mesa's robustness strategy): in-bounds
// accesses are unchanged, out-of-bounds ones read/write the last element.
// Struct member indices are compile-time constants by construction and
// are never touched.
void clampAccessChainIndices(std::vector<unsigned int>& spirv) {
    if (spirv.size() <= 5) return;
    enum : uint32_t {
        OpExtInstImport = 11,
        OpExtInst = 12,
        OpTypeInt = 21,
        OpTypeVector = 23,
        OpTypeMatrix = 24,
        OpTypeArray = 28,
        OpTypeStruct = 30,
        OpTypePointer = 32,
        OpConstant = 43,
        OpFunction = 54,
        OpFunctionParameter = 55,
        OpVariable = 59,
        OpAccessChain = 65,
        OpInBoundsAccessChain = 66,
        GLSLstd450UClamp = 44,
        GLSLstd450SClamp = 45,
    };
    // Instructions that may produce an integer index; all use the standard
    // (result type, result id) word layout. Anything not listed simply
    // never gets clamped - in-bounds behavior is unaffected either way.
    const auto produces_value = [](uint32_t opcode) {
        switch (opcode) {
        case 12 /*ExtInst*/: case 57 /*FunctionCall*/: case 61 /*Load*/:
        case 77 /*VectorExtractDynamic*/: case 81 /*CompositeExtract*/:
        case 124 /*Bitcast*/: case 126 /*SNegate*/: case 128 /*IAdd*/:
        case 130 /*ISub*/: case 132 /*IMul*/: case 134 /*UDiv*/:
        case 135 /*SDiv*/: case 137 /*UMod*/: case 138 /*SRem*/:
        case 139 /*SMod*/: case 169 /*Select*/: case 194: case 195:
        case 196 /*shifts*/: case 197: case 198: case 199 /*bitwise*/:
        case 245 /*Phi*/:
            return true;
        default:
            return false;
        }
    };
    struct type_info_t {
        uint32_t opcode = 0;
        uint32_t inner = 0;   // element/component/column/pointee type
        uint32_t count = 0;   // vector size / matrix columns / array length id
        bool int_signed = false;
        std::vector<uint32_t> members; // struct member types
    };
    std::unordered_map<uint32_t, type_info_t> types;
    std::unordered_map<uint32_t, std::pair<uint32_t, uint32_t>> constants; // id -> (type, word)
    std::unordered_map<uint32_t, uint32_t> value_types; // variable/chain id -> type id
    uint32_t glsl_set = 0;

    const auto instr_at = [&](size_t off) { return spirv[off]; };
    size_t off = 5;
    std::vector<std::pair<size_t, uint32_t>> chains; // (offset, base type id)
    size_t first_function = 0;
    while (off < spirv.size()) {
        const uint32_t opcode = instr_at(off) & 0xffffu;
        const uint32_t len = instr_at(off) >> 16;
        if (len == 0 || off + len > spirv.size()) return; // malformed: leave as-is
        switch (opcode) {
        case OpExtInstImport: {
            // Operand is a string literal; glslang emits "GLSL.std.450".
            const char* name = (const char*)&spirv[off + 2];
            if (std::strncmp(name, "GLSL.std.450", 12) == 0) glsl_set = spirv[off + 1];
            break;
        }
        case OpTypeInt:
            types[spirv[off + 1]] = {opcode, 0, 0, spirv[off + 3] != 0, {}};
            break;
        case OpTypeVector:
            types[spirv[off + 1]] = {opcode, spirv[off + 2], spirv[off + 3], false, {}};
            break;
        case OpTypeMatrix:
            types[spirv[off + 1]] = {opcode, spirv[off + 2], spirv[off + 3], false, {}};
            break;
        case OpTypeArray:
            types[spirv[off + 1]] = {opcode, spirv[off + 2], spirv[off + 3], false, {}};
            break;
        case OpTypeStruct: {
            type_info_t info;
            info.opcode = opcode;
            for (uint32_t w = 2; w < len; ++w) info.members.push_back(spirv[off + w]);
            types[spirv[off + 1]] = std::move(info);
            break;
        }
        case OpTypePointer:
            types[spirv[off + 1]] = {opcode, spirv[off + 3], 0, false, {}};
            break;
        case OpConstant:
            constants[spirv[off + 2]] = {spirv[off + 1], spirv[off + 3]};
            break;
        case OpVariable:
        case OpFunctionParameter:
            value_types[spirv[off + 2]] = spirv[off + 1];
            break;
        case OpFunction:
            if (first_function == 0) first_function = off;
            value_types[spirv[off + 2]] = spirv[off + 1];
            break;
        case OpAccessChain:
        case OpInBoundsAccessChain: {
            value_types[spirv[off + 2]] = spirv[off + 1];
            auto base_type = value_types.find(spirv[off + 3]);
            if (base_type != value_types.end()) {
                auto ptr = types.find(base_type->second);
                if (ptr != types.end() && ptr->second.opcode == OpTypePointer)
                    chains.emplace_back(off, ptr->second.inner);
            }
            break;
        }
        default:
            if (produces_value(opcode) && len >= 3) value_types[spirv[off + 2]] = spirv[off + 1];
            break;
        }
        off += len;
    }
    if (glsl_set == 0 || chains.empty() || first_function == 0) return;

    // Work out which chain indices need a clamp.
    struct clamp_t {
        size_t instr_off;
        uint32_t word;      // operand slot within the instruction
        uint32_t index_id;
        uint32_t int_type;
        bool int_signed;
        uint32_t max_value;
        uint32_t result_id = 0;
    };
    std::vector<clamp_t> clamps;
    for (const auto& [chain_off, base] : chains) {
        uint32_t current = base;
        const uint32_t len = instr_at(chain_off) >> 16;
        for (uint32_t w = 4; w < len; ++w) {
            auto tit = types.find(current);
            if (tit == types.end()) break;
            const auto& t = tit->second;
            uint32_t size = 0;
            if (t.opcode == OpTypeStruct) {
                auto cit = constants.find(spirv[chain_off + w]);
                if (cit == constants.end() || cit->second.second >= t.members.size()) break;
                current = t.members[cit->second.second];
                continue;
            } else if (t.opcode == OpTypeVector || t.opcode == OpTypeMatrix) {
                size = t.count;
                current = t.inner;
            } else if (t.opcode == OpTypeArray) {
                auto cit = constants.find(t.count);
                if (cit == constants.end()) break;
                size = cit->second.second;
                current = t.inner;
            } else {
                break;
            }
            if (size == 0) break;
            const uint32_t index_id = spirv[chain_off + w];
            uint32_t int_type = 0;
            bool is_signed = false;
            auto cit = constants.find(index_id);
            if (cit != constants.end()) {
                auto it_type = types.find(cit->second.first);
                if (it_type == types.end() || it_type->second.opcode != OpTypeInt) continue;
                is_signed = it_type->second.int_signed;
                const uint32_t v = cit->second.second;
                const bool oob = is_signed ? ((int32_t)v < 0 || v >= size) : v >= size;
                if (!oob) continue; // in-range constant: leave untouched
                int_type = cit->second.first;
            } else {
                auto vt = value_types.find(index_id);
                if (vt == value_types.end()) continue;
                auto it_type = types.find(vt->second);
                if (it_type == types.end() || it_type->second.opcode != OpTypeInt) continue;
                is_signed = it_type->second.int_signed;
                int_type = vt->second;
            }
            clamps.push_back({chain_off, w, index_id, int_type, is_signed, size - 1});
        }
    }
    if (clamps.empty()) return;

    // Allocate ids: one constant per (type, value), one result per clamp.
    uint32_t bound = spirv[3];
    std::map<std::pair<uint32_t, uint32_t>, uint32_t> const_ids; // (type, value) -> id
    for (auto& c : clamps) {
        for (uint32_t v : {0u, c.max_value}) {
            if (const_ids.find({c.int_type, v}) == const_ids.end())
                const_ids[{c.int_type, v}] = bound++;
        }
        c.result_id = bound++;
    }

    std::vector<unsigned int> out;
    out.reserve(spirv.size() + clamps.size() * 8 + const_ids.size() * 4);
    out.insert(out.end(), spirv.begin(), spirv.begin() + 5);
    out[3] = bound;
    off = 5;
    while (off < spirv.size()) {
        const uint32_t len = instr_at(off) >> 16;
        if (off == first_function) {
            // All types are declared by now: emit the clamp-bound constants.
            for (const auto& [key, id] : const_ids) {
                out.push_back((4u << 16) | OpConstant);
                out.push_back(key.first);
                out.push_back(id);
                out.push_back(key.second);
            }
        }
        bool patched = false;
        for (const auto& c : clamps) {
            if (c.instr_off != off) continue;
            if (!patched) {
                // Clamp instructions go right before the access chain.
                for (const auto& c2 : clamps) {
                    if (c2.instr_off != off) continue;
                    out.push_back((8u << 16) | OpExtInst);
                    out.push_back(c2.int_type);
                    out.push_back(c2.result_id);
                    out.push_back(glsl_set);
                    out.push_back(c2.int_signed ? GLSLstd450SClamp : GLSLstd450UClamp);
                    out.push_back(c2.index_id);
                    out.push_back(const_ids[{c2.int_type, 0u}]);
                    out.push_back(const_ids[{c2.int_type, c2.max_value}]);
                }
                patched = true;
            }
        }
        const size_t instr_start = out.size();
        out.insert(out.end(), spirv.begin() + off, spirv.begin() + off + len);
        for (const auto& c : clamps) {
            if (c.instr_off == off) out[instr_start + c.word] = c.result_id;
        }
        off += len;
    }
    spirv.swap(out);
}

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
        // The struct TYPE names are user-referencable too (e.g. as function
        // parameter types) and must follow their instances into the prelude.
        {"gl_LightSourceParameters", "fpe_LightSourceParameters"},
        {"gl_LightModelParameters", "fpe_LightModelParameters"},
        {"gl_MaterialParameters", "fpe_MaterialParameters"},
        {"gl_FogParameters", "fpe_FogParameters"},
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

namespace {

// Strip the caller's #version: glslang's OpenGL-SPIR-V environment only
// initializes its builtin tables for modern versions, so the toolchain
// input is always "450 core" - a superset in which every 1.10/1.20
// construct still parses once the preprocessor has rewritten the
// compatibility spellings.
std::string rewriteBody(bool vertex, const std::string& source) {
    std::string body = source;
    const size_t vpos = body.find("#version");
    if (vpos != std::string::npos) {
        const size_t eol = body.find('\n', vpos);
        body = body.substr(0, vpos) + body.substr(eol == std::string::npos ? body.size() : eol + 1);
    }
    return replaceIdentifiers(body, commonReplacements(),
                              vertex ? vertexReplacements() : fragmentReplacements());
}

std::string buildPrelude(bool vertex, bool uses_fragdata) {
    std::string result = "#version 450\n"; // SPIR-V rejects compatibility
    result += kSharedPrelude;
    result += vertex ? kVertexPrelude : kFragmentPrelude;
    if (!vertex) {
        // FragColor and FragData[0] would collide on one location; declare
        // only what the shader actually writes (GL forbids mixing them).
        result += uses_fragdata ? kFragDataOut : kFragColorOut;
    }
    return result;
}

} // namespace

std::string preprocess(GLenum stage, const std::string& source) {
    return preprocess(stage, std::vector<std::string>{source});
}

// Multiple compilation units of one stage (GL 2.1 multi-TU programs, which
// GLES forbids) merge by concatenation: GLSL has no TU-local scope, so
// concatenating the rewritten bodies after a single prelude reproduces the
// linker's global-scope merge for everything short of duplicated
// initialized globals.
std::string preprocess(GLenum stage, const std::vector<std::string>& sources) {
    const bool vertex = stage == GL_VERTEX_SHADER;
    std::vector<std::string> bodies;
    bodies.reserve(sources.size());
    bool uses_fragdata = false;
    for (const auto& source : sources) {
        bodies.push_back(rewriteBody(vertex, source));
        uses_fragdata = uses_fragdata || bodies.back().find("fpe_FragData") != std::string::npos;
    }
    std::string result = buildPrelude(vertex, uses_fragdata);
    for (const auto& body : bodies) {
        result += "#line 1\n"; // keep glslang diagnostics on user line numbers
        result += body;
        if (result.empty() || result.back() != '\n') result += '\n';
    }
    return result;
}

translation_result_t translate(GLenum stage, const std::string& source,
                               const target_language_t& target) {
    return translate(stage, std::vector<std::string>{source}, target);
}

translation_result_t translate(GLenum stage, const std::vector<std::string>& sources,
                               const target_language_t& target) {
    translation_result_t result;
    result.preprocessed = preprocess(stage, sources);

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
    result.parse_ok = true;

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
    clampAccessChainIndices(spirv);

    try {
        EsslCompiler compiler(std::move(spirv));
        spirv_cross::CompilerGLSL::Options opts;
        opts.version = target.version;
        opts.es = target.es;
        opts.vulkan_semantics = false;
        opts.enable_420pack_extension = false;
        // Desktop GLSL 1.20 computes at (at least) single precision;
        // mediump would visibly degrade transcendentals (tan, exp2, ...).
        opts.fragment.default_float_precision =
            spirv_cross::CompilerGLSL::Options::Precision::Highp;
        compiler.set_common_options(opts);
        // The wrapper links everything by NAME (glGetUniformLocation /
        // glGetAttribLocation / varying name matching): the auto-mapped
        // bindings and locations from the per-stage glslang runs are not
        // coherent across stages, and ESSL 310+ would otherwise emit
        // layout(binding=N) on plain default-block uniforms, which no GLSL
        // dialect accepts. Strip them; only fragment outputs keep their
        // locations (gl_FragData[i] must stay routed to draw buffer i).
        const auto resources = compiler.get_shader_resources();
        auto strip = [&compiler](const spirv_cross::Resource& r) {
            compiler.unset_decoration(r.id, spv::DecorationLocation);
            compiler.unset_decoration(r.id, spv::DecorationBinding);
        };
        for (const auto& r : resources.gl_plain_uniforms) strip(r);
        for (const auto& r : resources.sampled_images) strip(r);
        for (const auto& r : resources.separate_images) strip(r);
        for (const auto& r : resources.separate_samplers) strip(r);
        if (lang == EShLangVertex) {
            for (const auto& r : resources.stage_outputs) strip(r);
        } else {
            for (const auto& r : resources.stage_inputs) strip(r);
        }
        result.uniform_initializers = compiler.scrapeUniformInitializers();
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
