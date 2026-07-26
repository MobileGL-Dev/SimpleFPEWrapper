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

namespace SFPEW::Shader {

struct translation_result_t {
    bool ok = false;
    std::string essl;       // ESSL 3.00 source on success
    std::string log;        // preprocessor/glslang/SPIRV-Cross diagnostics
    std::string preprocessed; // post-preprocess GLSL (for tests/debugging)
};

// Backend shading-language target, detected at runtime from the real
// driver (GL_MAJOR/MINOR_VERSION + "OpenGL ES" in GL_VERSION). Never
// hardcode: GLES 3.0/3.1/3.2 -> 300/310/320 es, desktop -> 420/450/...
struct target_language_t {
    unsigned version = 300;
    bool es = true;
};

// Desktop GLSL (any #version 110..460, core or compatibility) -> the
// backend's target language (plans/09 9.1): custom compat-builtin
// preprocessing, then glslang -> SPIR-V -> SPIRV-Cross.
// `stage` is GL_VERTEX_SHADER or GL_FRAGMENT_SHADER.
translation_result_t translate(GLenum stage, const std::string& source,
                               const target_language_t& target);

// Queries the current backend once per context and caches the result.
target_language_t detect_backend_target();

// Exposed separately for offline tests: the compat-builtin rewrite and
// prelude injection only (no glslang round trip).
std::string preprocess(GLenum stage, const std::string& source);

} // namespace SFPEW::Shader
