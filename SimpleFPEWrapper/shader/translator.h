// SimpleFPEWrapper - SimpleFPEWrapper/shader/translator.h
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

#pragma once

#include <GL/gl.h>
#include <string>
#include <vector>

namespace SFPEW::Shader {

// GLSL 1.20 uniform initializers are not expressible in ESSL: the values
// are scraped out of the SPIR-V here and applied with glUniform* right
// after a successful link (the GL 2.1 semantics: initializers define the
// post-link value).
struct uniform_initializer_t {
    std::string name;
    unsigned columns = 1;    // >1 => matrix (columns x vecsize)
    unsigned vecsize = 1;
    unsigned array_size = 1; // flattened element count of the top array
    enum class base_t { f32, i32, u32, b32 } base = base_t::f32;
    std::vector<float> f;    // column-major, used when base == f32
    std::vector<int> i;      // other bases (bools as 0/1)
};

struct translation_result_t {
    bool ok = false;
    bool parse_ok = false;  // the TU parses standalone; a link-only failure
                            // means unresolved cross-TU symbols (defer to
                            // glLinkProgram, GL 2.1 multi-TU semantics)
    std::string essl;       // ESSL 3.00 source on success
    std::string log;        // preprocessor/glslang/SPIRV-Cross diagnostics
    std::string preprocessed; // post-preprocess GLSL (for tests/debugging)
    std::vector<uniform_initializer_t> uniform_initializers;
};

// Backend shading-language target, detected at runtime from the real
// driver (GL_MAJOR/MINOR_VERSION + "OpenGL ES" in GL_VERSION). Never
// hardcode: GLES 3.0/3.1/3.2 -> 300/310/320 es, desktop -> 420/450/...
struct target_language_t {
    unsigned version = 300;
    bool es = true;
    // Backend GL_MAX_DRAW_BUFFERS: sizes the fpe_FragData prelude array
    // (OptiFine packs statically index gl_FragData up to [7], and a
    // constant index past the declared size is a parse error).
    unsigned max_draw_buffers = 4;
};

// Desktop GLSL (any #version 110..460, core or compatibility) -> the
// backend's target language (plans/09 9.1): custom compat-builtin
// preprocessing, then glslang -> SPIR-V -> SPIRV-Cross.
// `stage` is GL_VERTEX_SHADER or GL_FRAGMENT_SHADER.
translation_result_t translate(GLenum stage, const std::string& source,
                               const target_language_t& target);

// Several compilation units of ONE stage translated together (GL 2.1
// multi-TU programs): the rewritten bodies are concatenated after a single
// prelude, which reproduces the GLSL linker's global-scope merge.
translation_result_t translate(GLenum stage, const std::vector<std::string>& sources,
                               const target_language_t& target);

// Queries the current backend once per context and caches the result.
target_language_t detect_backend_target();

// Exposed separately for offline tests: the compat-builtin rewrite and
// prelude injection only (no glslang round trip).
std::string preprocess(GLenum stage, const std::string& source);
std::string preprocess(GLenum stage, const std::vector<std::string>& sources);

} // namespace SFPEW::Shader
