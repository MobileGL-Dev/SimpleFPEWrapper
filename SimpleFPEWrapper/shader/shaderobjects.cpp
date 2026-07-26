// SimpleFPEWrapper - SimpleFPEWrapper/shader/shaderobjects.cpp
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// plans/09 9.2: GL 2.0 shader objects and the ARB_shader_objects aliases
// LWJGL-era applications resolve. glShaderSource keeps the caller's
// original text (GetShaderSource returns it per spec) and glCompileShader
// feeds the translated source to the GLES backend; if translation fails
// the original is passed through (the backend may accept it natively) and
// the translator diagnostics are prepended to the info log.
//
// Shader records are process-global like display-list definitions:
// share-group relationships are invisible to the wrapper (see
// docs/context-model.md).

#include "translator.h"
#include "../init.h"
#include "../log.h"
#include "../fpe/fpe.hpp"
#include "../fpe/drawing1x.h"

#include <algorithm>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

struct shader_record_t {
    GLenum stage = 0;
    std::string original;
    std::string translate_log;
    bool translated = false;
    // GLSL 1.20 uniform initializers scraped by the translator; applied
    // with glUniform* after every successful link of a containing program.
    std::vector<SFPEW::Shader::uniform_initializer_t> uniform_inits;
};

std::mutex g_shader_mutex;
std::unordered_map<GLuint, shader_record_t>& shaderRecords() {
    static std::unordered_map<GLuint, shader_record_t> records;
    return records;
}

} // namespace

GLuint glCreateShader(GLenum type) {
    if (!sfpewEnsureBackend() || g_glFuncs.glCreateShader == nullptr) return 0;
    if (type != GL_VERTEX_SHADER && type != GL_FRAGMENT_SHADER) {
        g_glstate.set_error(GL_INVALID_ENUM);
        return 0;
    }
    const GLuint shader = g_glFuncs.glCreateShader(type);
    if (shader != 0) {
        std::lock_guard<std::mutex> lock(g_shader_mutex);
        shaderRecords()[shader] = shader_record_t{};
        shaderRecords()[shader].stage = type;
    }
    return shader;
}

void glDeleteShader(GLuint shader) {
    if (!sfpewEnsureBackend() || g_glFuncs.glDeleteShader == nullptr) return;
    g_glFuncs.glDeleteShader(shader);
    std::lock_guard<std::mutex> lock(g_shader_mutex);
    shaderRecords().erase(shader);
}

void glShaderSource(GLuint shader, GLsizei count, const GLchar* const* string, const GLint* length) {
    if (!sfpewEnsureBackend()) return;
    if (count < 0) {
        g_glstate.set_error(GL_INVALID_VALUE);
        return;
    }
    if (string == nullptr) return;

    // Concatenate per spec; negative/absent lengths mean NUL-terminated.
    std::string source;
    for (GLsizei i = 0; i < count; ++i) {
        if (string[i] == nullptr) continue;
        if (length != nullptr && length[i] >= 0)
            source.append(string[i], (size_t)length[i]);
        else
            source.append(string[i]);
    }

    std::lock_guard<std::mutex> lock(g_shader_mutex);
    auto it = shaderRecords().find(shader);
    if (it == shaderRecords().end()) {
        g_glstate.set_error(GL_INVALID_OPERATION);
        return;
    }
    it->second.original = std::move(source);
    it->second.translated = false;
    it->second.translate_log.clear();
}

void glCompileShader(GLuint shader) {
    if (!sfpewEnsureBackend() || g_glFuncs.glCompileShader == nullptr ||
        g_glFuncs.glShaderSource == nullptr) {
        return;
    }
    std::string original;
    GLenum stage = 0;
    {
        std::lock_guard<std::mutex> lock(g_shader_mutex);
        auto it = shaderRecords().find(shader);
        if (it == shaderRecords().end()) {
            g_glstate.set_error(GL_INVALID_OPERATION);
            return;
        }
        original = it->second.original;
        stage = it->second.stage;
    }

    auto result = SFPEW::Shader::translate(stage, original, SFPEW::Shader::detect_backend_target());
    const std::string& upload = result.ok ? result.essl : original;
    if (!result.ok) {
        SFPEW_LOGW("shader %u: translation failed, passing original through:\n%s", shader,
                   result.log.c_str());
    }

    const char* text = upload.c_str();
    const GLint text_length = (GLint)upload.size();
    g_glFuncs.glShaderSource(shader, 1, &text, &text_length);
    g_glFuncs.glCompileShader(shader);

    std::lock_guard<std::mutex> lock(g_shader_mutex);
    auto it = shaderRecords().find(shader);
    if (it != shaderRecords().end()) {
        it->second.translated = result.ok;
        it->second.translate_log = std::move(result.log);
        it->second.uniform_inits = std::move(result.uniform_initializers);
    }
}

namespace {

void applyOneInitializer(const SFPEW::Shader::uniform_initializer_t& init, GLint loc) {
    using base_t = SFPEW::Shader::uniform_initializer_t::base_t;
    const GLsizei count = (GLsizei)init.array_size;
    if (init.columns > 1) {
        if (init.base != base_t::f32) return;
        const GLfloat* v = init.f.data();
        switch (init.columns * 10 + init.vecsize) {
        case 22: if (g_glFuncs.glUniformMatrix2fv) g_glFuncs.glUniformMatrix2fv(loc, count, GL_FALSE, v); break;
        case 33: if (g_glFuncs.glUniformMatrix3fv) g_glFuncs.glUniformMatrix3fv(loc, count, GL_FALSE, v); break;
        case 44: if (g_glFuncs.glUniformMatrix4fv) g_glFuncs.glUniformMatrix4fv(loc, count, GL_FALSE, v); break;
        case 23: if (g_glFuncs.glUniformMatrix2x3fv) g_glFuncs.glUniformMatrix2x3fv(loc, count, GL_FALSE, v); break;
        case 24: if (g_glFuncs.glUniformMatrix2x4fv) g_glFuncs.glUniformMatrix2x4fv(loc, count, GL_FALSE, v); break;
        case 32: if (g_glFuncs.glUniformMatrix3x2fv) g_glFuncs.glUniformMatrix3x2fv(loc, count, GL_FALSE, v); break;
        case 34: if (g_glFuncs.glUniformMatrix3x4fv) g_glFuncs.glUniformMatrix3x4fv(loc, count, GL_FALSE, v); break;
        case 42: if (g_glFuncs.glUniformMatrix4x2fv) g_glFuncs.glUniformMatrix4x2fv(loc, count, GL_FALSE, v); break;
        case 43: if (g_glFuncs.glUniformMatrix4x3fv) g_glFuncs.glUniformMatrix4x3fv(loc, count, GL_FALSE, v); break;
        default: break;
        }
        return;
    }
    if (init.base == base_t::f32) {
        const GLfloat* v = init.f.data();
        switch (init.vecsize) {
        case 1: if (g_glFuncs.glUniform1fv) g_glFuncs.glUniform1fv(loc, count, v); break;
        case 2: if (g_glFuncs.glUniform2fv) g_glFuncs.glUniform2fv(loc, count, v); break;
        case 3: if (g_glFuncs.glUniform3fv) g_glFuncs.glUniform3fv(loc, count, v); break;
        case 4: if (g_glFuncs.glUniform4fv) g_glFuncs.glUniform4fv(loc, count, v); break;
        default: break;
        }
        return;
    }
    if (init.base == base_t::u32 && g_glFuncs.glUniform1uiv != nullptr) {
        const GLuint* v = (const GLuint*)init.i.data();
        switch (init.vecsize) {
        case 1: g_glFuncs.glUniform1uiv(loc, count, v); break;
        case 2: if (g_glFuncs.glUniform2uiv) g_glFuncs.glUniform2uiv(loc, count, v); break;
        case 3: if (g_glFuncs.glUniform3uiv) g_glFuncs.glUniform3uiv(loc, count, v); break;
        case 4: if (g_glFuncs.glUniform4uiv) g_glFuncs.glUniform4uiv(loc, count, v); break;
        default: break;
        }
        return;
    }
    const GLint* v = init.i.data();
    switch (init.vecsize) {
    case 1: if (g_glFuncs.glUniform1iv) g_glFuncs.glUniform1iv(loc, count, v); break;
    case 2: if (g_glFuncs.glUniform2iv) g_glFuncs.glUniform2iv(loc, count, v); break;
    case 3: if (g_glFuncs.glUniform3iv) g_glFuncs.glUniform3iv(loc, count, v); break;
    case 4: if (g_glFuncs.glUniform4iv) g_glFuncs.glUniform4iv(loc, count, v); break;
    default: break;
    }
}

// GL 2.1 semantics: uniform initializers define the post-link values. The
// translated ESSL cannot carry them, so set them here through the uniform
// API. Application values are overwritten exactly like a real relink.
void applyUniformInitializers(GLuint program) {
    if (g_glFuncs.glGetAttachedShaders == nullptr || g_glFuncs.glGetUniformLocation == nullptr ||
        g_glFuncs.glUseProgram == nullptr || g_glFuncs.glGetIntegerv == nullptr) {
        return;
    }
    GLuint shaders[8] = {};
    GLsizei shader_count = 0;
    g_glFuncs.glGetAttachedShaders(program, 8, &shader_count, shaders);

    std::vector<const SFPEW::Shader::uniform_initializer_t*> inits;
    {
        std::lock_guard<std::mutex> lock(g_shader_mutex);
        for (GLsizei s = 0; s < shader_count; ++s) {
            auto it = shaderRecords().find(shaders[s]);
            if (it == shaderRecords().end()) continue;
            for (const auto& init : it->second.uniform_inits) {
                const auto same_name = [&](const auto* other) { return other->name == init.name; };
                if (std::none_of(inits.begin(), inits.end(), same_name)) inits.push_back(&init);
            }
        }
    }
    if (inits.empty()) return;

    GLint previous = 0;
    g_glFuncs.glGetIntegerv(GL_CURRENT_PROGRAM, &previous);
    g_glFuncs.glUseProgram(program);
    for (const auto* init : inits) {
        const GLint loc = g_glFuncs.glGetUniformLocation(program, init->name.c_str());
        if (loc >= 0) applyOneInitializer(*init, loc);
    }
    g_glFuncs.glUseProgram((GLuint)previous);
}

} // namespace

void glLinkProgram(GLuint program) {
    if (!sfpewEnsureBackend() || g_glFuncs.glLinkProgram == nullptr) return;
    g_glFuncs.glLinkProgram(program);

    GLint status = GL_FALSE;
    if (g_glFuncs.glGetProgramiv != nullptr)
        g_glFuncs.glGetProgramiv(program, GL_LINK_STATUS, &status);
    if (status == GL_TRUE) applyUniformInitializers(program);
}

void glGetShaderSource(GLuint shader, GLsizei bufSize, GLsizei* length, GLchar* source) {
    if (source == nullptr || bufSize <= 0) return;
    std::lock_guard<std::mutex> lock(g_shader_mutex);
    auto it = shaderRecords().find(shader);
    if (it == shaderRecords().end()) {
        g_glstate.set_error(GL_INVALID_OPERATION);
        return;
    }
    // Per spec: the caller's ORIGINAL source, not what the backend compiled.
    const std::string& text = it->second.original;
    const GLsizei n = (GLsizei)std::min((size_t)bufSize - 1, text.size());
    std::memcpy(source, text.data(), (size_t)n);
    source[n] = '\0';
    if (length != nullptr) *length = n;
}

void glGetShaderInfoLog(GLuint shader, GLsizei bufSize, GLsizei* length, GLchar* infoLog) {
    if (infoLog == nullptr || bufSize <= 0) return;
    if (!sfpewEnsureBackend() || g_glFuncs.glGetShaderInfoLog == nullptr) return;

    std::string combined;
    {
        std::lock_guard<std::mutex> lock(g_shader_mutex);
        auto it = shaderRecords().find(shader);
        if (it != shaderRecords().end() && !it->second.translate_log.empty()) {
            combined = "[SFPEW translator]\n" + it->second.translate_log + "\n";
        }
    }
    GLsizei backend_length = 0;
    std::vector<GLchar> backend_log((size_t)bufSize);
    g_glFuncs.glGetShaderInfoLog(shader, bufSize, &backend_length, backend_log.data());
    combined.append(backend_log.data(), (size_t)std::max(backend_length, 0));

    const GLsizei n = (GLsizei)std::min((size_t)bufSize - 1, combined.size());
    std::memcpy(infoLog, combined.data(), (size_t)n);
    infoLog[n] = '\0';
    if (length != nullptr) *length = n;
}

// --- ARB_shader_objects aliases -----------------------------------------
// GLhandleARB and GLuint share the value space here; the ARB pname values
// are numerically identical to their core equivalents.

extern "C" {

GLuint sfpewCreateShaderObjectARB(GLenum type) { return glCreateShader(type); }
GLuint sfpewCreateProgramObjectARB(void) {
    if (!sfpewEnsureBackend() || g_glFuncs.glCreateProgram == nullptr) return 0;
    return g_glFuncs.glCreateProgram();
}

void sfpewDeleteObjectARB(GLuint object) {
    if (!sfpewEnsureBackend()) return;
    if (g_glFuncs.glIsShader != nullptr && g_glFuncs.glIsShader(object)) {
        glDeleteShader(object);
    } else if (g_glFuncs.glDeleteProgram != nullptr) {
        g_glFuncs.glDeleteProgram(object);
    }
}

void sfpewAttachObjectARB(GLuint program, GLuint shader) {
    if (!sfpewEnsureBackend() || g_glFuncs.glAttachShader == nullptr) return;
    g_glFuncs.glAttachShader(program, shader);
}

void sfpewDetachObjectARB(GLuint program, GLuint shader) {
    if (!sfpewEnsureBackend() || g_glFuncs.glDetachShader == nullptr) return;
    g_glFuncs.glDetachShader(program, shader);
}

void sfpewGetObjectParameterivARB(GLuint object, GLenum pname, GLint* params) {
    if (params == nullptr || !sfpewEnsureBackend()) return;
    if (g_glFuncs.glIsShader != nullptr && g_glFuncs.glIsShader(object)) {
        if (g_glFuncs.glGetShaderiv != nullptr) g_glFuncs.glGetShaderiv(object, pname, params);
    } else {
        if (g_glFuncs.glGetProgramiv != nullptr) g_glFuncs.glGetProgramiv(object, pname, params);
    }
}

void sfpewGetInfoLogARB(GLuint object, GLsizei maxLength, GLsizei* length, GLchar* infoLog) {
    if (!sfpewEnsureBackend()) return;
    if (g_glFuncs.glIsShader != nullptr && g_glFuncs.glIsShader(object)) {
        glGetShaderInfoLog(object, maxLength, length, infoLog);
    } else if (g_glFuncs.glGetProgramInfoLog != nullptr && infoLog != nullptr && maxLength > 0) {
        g_glFuncs.glGetProgramInfoLog(object, maxLength, length, infoLog);
    }
}

GLuint sfpewGetHandleARB(GLenum pname) {
    if (pname != 0x8B8D /* GL_PROGRAM_OBJECT_ARB */) return 0;
    return (GLuint)sfpewLogicalProgram();
}

} // extern "C"
