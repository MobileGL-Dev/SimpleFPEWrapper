// SimpleFPEWrapper - SimpleFPEWrapper/getter.cpp
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

#include "GL/gl.h"
#include "init.h"
#include <glm/gtc/type_ptr.hpp>
#include <cstdint>
#include <unordered_map>
#include "fpe/fpe.hpp"
#include "fpe/drawing1x.h"

namespace {
struct ProxyTexture2DLevel {
    GLsizei width = 0;
    GLsizei height = 0;
    GLint internalFormat = 0;
    GLint border = 0;
    bool supported = false;
};

struct ProxyTexture2DCache {
    EGLContext context = EGL_NO_CONTEXT;
    std::unordered_map<GLint, ProxyTexture2DLevel> levels;
};

thread_local ProxyTexture2DCache proxyTexture2DCache;

struct LogicalTextureBindings {
    EGLContext context = EGL_NO_CONTEXT;
    GLenum activeTexture = GL_TEXTURE0;
    bool activeTextureKnown = false;
    std::unordered_map<uint64_t, GLuint> bindings;
};

thread_local LogicalTextureBindings logicalTextureBindings;

LogicalTextureBindings& getLogicalTextureBindings() {
    const EGLContext context =
        g_eglFuncs.eglGetCurrentContext ? g_eglFuncs.eglGetCurrentContext() : EGL_NO_CONTEXT;
    if (logicalTextureBindings.context != context) {
        logicalTextureBindings = {};
        logicalTextureBindings.context = context;
    }
    return logicalTextureBindings;
}

GLenum getLogicalActiveTexture(LogicalTextureBindings& state) {
    if (!state.activeTextureKnown) {
        GLint active = GL_TEXTURE0;
        g_glFuncs.glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
        state.activeTexture = static_cast<GLenum>(active);
        state.activeTextureKnown = true;
    }
    return state.activeTexture;
}

GLenum textureBindingQuery(GLenum target) {
    switch (target) {
    case GL_TEXTURE_2D:
        return GL_TEXTURE_BINDING_2D;
    case GL_TEXTURE_CUBE_MAP:
        return GL_TEXTURE_BINDING_CUBE_MAP;
#ifdef GL_TEXTURE_3D
    case GL_TEXTURE_3D:
        return GL_TEXTURE_BINDING_3D;
#endif
#ifdef GL_TEXTURE_2D_ARRAY
    case GL_TEXTURE_2D_ARRAY:
        return GL_TEXTURE_BINDING_2D_ARRAY;
#endif
    default:
        return GL_NONE;
    }
}

uint64_t textureBindingKey(GLenum activeTexture, GLenum target) {
    return (static_cast<uint64_t>(activeTexture) << 32u) | static_cast<uint64_t>(target);
}

std::unordered_map<GLint, ProxyTexture2DLevel>& getProxyTexture2DLevels() {
    const EGLContext context =
        g_eglFuncs.eglGetCurrentContext ? g_eglFuncs.eglGetCurrentContext() : EGL_NO_CONTEXT;
    if (proxyTexture2DCache.context != context) {
        proxyTexture2DCache.context = context;
        proxyTexture2DCache.levels.clear();
    }
    return proxyTexture2DCache.levels;
}

bool isProxyTextureInternalFormat(GLint internalFormat) {
    switch (internalFormat) {
    case 1:
    case 2:
    case 3:
    case 4:
    case GL_COLOR_INDEX:
    case GL_DEPTH_COMPONENT:
    case GL_RED:
    case GL_GREEN:
    case GL_BLUE:
    case GL_ALPHA:
    case GL_RGB:
    case GL_RGBA:
    case GL_LUMINANCE:
    case GL_LUMINANCE_ALPHA:
    case GL_INTENSITY:
    case GL_ALPHA4:
    case GL_ALPHA8:
    case GL_ALPHA12:
    case GL_ALPHA16:
    case GL_LUMINANCE4:
    case GL_LUMINANCE8:
    case GL_LUMINANCE12:
    case GL_LUMINANCE16:
    case GL_LUMINANCE4_ALPHA4:
    case GL_LUMINANCE6_ALPHA2:
    case GL_LUMINANCE8_ALPHA8:
    case GL_LUMINANCE12_ALPHA4:
    case GL_LUMINANCE12_ALPHA12:
    case GL_LUMINANCE16_ALPHA16:
    case GL_INTENSITY4:
    case GL_INTENSITY8:
    case GL_INTENSITY12:
    case GL_INTENSITY16:
    case GL_R3_G3_B2:
    case GL_RGB4:
    case GL_RGB5:
    case GL_RGB8:
    case GL_RGB10:
    case GL_RGB12:
    case GL_RGB16:
    case GL_RGBA2:
    case GL_RGBA4:
    case GL_RGB5_A1:
    case GL_RGBA8:
    case GL_RGB10_A2:
    case GL_RGBA12:
    case GL_RGBA16:
        return true;
    default:
        return false;
    }
}

bool isProxyTextureFormat(GLenum format) {
    switch (format) {
    case GL_COLOR_INDEX:
    case GL_STENCIL_INDEX:
    case GL_DEPTH_COMPONENT:
    case GL_RED:
    case GL_GREEN:
    case GL_BLUE:
    case GL_ALPHA:
    case GL_RGB:
    case GL_RGBA:
    case GL_LUMINANCE:
    case GL_LUMINANCE_ALPHA:
    case GL_BGR:
    case GL_BGRA:
        return true;
    default:
        return false;
    }
}

bool isProxyTextureTypeCompatible(GLenum format, GLenum type) {
    switch (type) {
    case GL_BYTE:
    case GL_UNSIGNED_BYTE:
    case GL_SHORT:
    case GL_UNSIGNED_SHORT:
    case GL_INT:
    case GL_UNSIGNED_INT:
    case GL_FLOAT:
        return true;
    case GL_BITMAP:
        return format == GL_COLOR_INDEX || format == GL_STENCIL_INDEX;
    case GL_UNSIGNED_BYTE_3_3_2:
    case GL_UNSIGNED_BYTE_2_3_3_REV:
    case GL_UNSIGNED_SHORT_5_6_5:
    case GL_UNSIGNED_SHORT_5_6_5_REV:
        return format == GL_RGB || format == GL_BGR;
    case GL_UNSIGNED_SHORT_4_4_4_4:
    case GL_UNSIGNED_SHORT_4_4_4_4_REV:
    case GL_UNSIGNED_SHORT_5_5_5_1:
    case GL_UNSIGNED_SHORT_1_5_5_5_REV:
    case GL_UNSIGNED_INT_8_8_8_8:
    case GL_UNSIGNED_INT_8_8_8_8_REV:
    case GL_UNSIGNED_INT_10_10_10_2:
    case GL_UNSIGNED_INT_2_10_10_10_REV:
        return format == GL_RGBA || format == GL_BGRA;
    default:
        return false;
    }
}

bool getProxyTextureLevelParameter(GLint level, GLenum pname, GLint& value) {
    auto& levels = getProxyTexture2DLevels();
    const auto it = levels.find(level);
    const ProxyTexture2DLevel* state = it == levels.end() ? nullptr : &it->second;
    const bool supported = state != nullptr && state->supported;

    switch (pname) {
    case GL_TEXTURE_WIDTH:
        value = supported ? state->width : 0;
        return true;
    case GL_TEXTURE_HEIGHT:
        value = supported ? state->height : 0;
        return true;
    case GL_TEXTURE_DEPTH:
        value = supported ? 1 : 0;
        return true;
    case GL_TEXTURE_INTERNAL_FORMAT:
        value = supported ? state->internalFormat : 0;
        return true;
    case GL_TEXTURE_BORDER:
        value = supported ? state->border : 0;
        return true;
    case GL_TEXTURE_RED_SIZE:
    case GL_TEXTURE_GREEN_SIZE:
    case GL_TEXTURE_BLUE_SIZE:
    case GL_TEXTURE_ALPHA_SIZE:
    case GL_TEXTURE_LUMINANCE_SIZE:
    case GL_TEXTURE_INTENSITY_SIZE:
    case GL_TEXTURE_COMPRESSED:
    case GL_TEXTURE_COMPRESSED_IMAGE_SIZE:
        value = 0;
        return true;
    default:
        return false;
    }
}
} // namespace

GLenum sfpewLogicalActiveTexture() {
    auto& state = getLogicalTextureBindings();
    return getLogicalActiveTexture(state);
}

GLuint sfpewLogicalTextureBinding(GLenum target) {
    auto& state = getLogicalTextureBindings();
    const GLenum query = textureBindingQuery(target);
    if (query == GL_NONE) return 0;

    const uint64_t key = textureBindingKey(getLogicalActiveTexture(state), target);
    auto binding = state.bindings.find(key);
    if (binding == state.bindings.end()) {
        GLint current = 0;
        g_glFuncs.glGetIntegerv(query, &current);
        binding = state.bindings.emplace(key, static_cast<GLuint>(current)).first;
    }
    return binding->second;
}

inline bool containsMobileGLDev(const std::string& str) {
    return str.find("MobileGL-Dev") != std::string::npos;
}

const GLubyte* glGetString(GLenum name) {
    // we only wrap GL_VERSION GL_RENDERER GL_VENDOR
    switch (name) {
    case GL_VERSION:
        static std::string cachedVersionString;
        if (cachedVersionString.empty()) {
            cachedVersionString = std::string((char*)g_glFuncs.glGetString(GL_VERSION)) + " (with Simple FPE Wrapper)";
        }
        return (const GLubyte*)cachedVersionString.c_str();
    case GL_RENDERER:
        static std::string cachedRendererString;
        if (cachedRendererString.empty()) {
            cachedRendererString = std::string((char*)g_glFuncs.glGetString(GL_RENDERER)) + " (SFPEW)";
        }
        return (const GLubyte*)cachedRendererString.c_str();
    case GL_VENDOR:
        static std::string cachedVendorString;
        if (cachedVendorString.empty()) {
            cachedVendorString = std::string((char*)g_glFuncs.glGetString(GL_VENDOR));
            if (!containsMobileGLDev(cachedVendorString)) {
                cachedVendorString += " (SFPEW: MobileGL-Dev)";
            }
        }
        return (const GLubyte*)cachedVendorString.c_str();
    default:
        return g_glFuncs.glGetString(name);
    }
}

const GLubyte* glGetStringi(GLenum name, GLuint index) {
    if (name != GL_EXTENSIONS) {
        return g_glFuncs.glGetStringi(name, index);
    }

    switch (index) {
    case 0:
        return (const GLubyte*)"GL_ARB_compatibility";
    case 1:
        return (const GLubyte*)"OpenGL21";
    case 2:
        return (const GLubyte*)"OpenGL11";
    case 3:
        return (const GLubyte*)"OpenGL12";
    case 4:
        return (const GLubyte*)"OpenGL13";
    case 5:
        return (const GLubyte*)"OpenGL14";
    case 6:
        return (const GLubyte*)"OpenGL15";
    case 7:
        return (const GLubyte*)"OpenGL20";
    default:
        return g_glFuncs.glGetStringi(name, index - 8);
    }
}

void glGetIntegerv(GLenum pname, GLint* params) {
    if (!params) {
        throw std::invalid_argument("params pointer cannot be null");
    }

    switch (pname) {
    case GL_CONTEXT_PROFILE_MASK:
        *params = GL_CONTEXT_COMPATIBILITY_PROFILE_BIT;
        break;
    case GL_CONTEXT_FLAGS:
        *params = 0;
        break;
    case GL_NUM_EXTENSIONS:
        static GLint cachedNumExtensions = -1;
        if (cachedNumExtensions == -1) {
            g_glFuncs.glGetIntegerv(GL_NUM_EXTENSIONS, &cachedNumExtensions);
            cachedNumExtensions += 8;
        }
        *params = cachedNumExtensions;
        break;
    case GL_CURRENT_PROGRAM:
        *params = sfpewLogicalProgram();
        break;
    case GL_ARRAY_BUFFER_BINDING:
        *params = static_cast<GLint>(sfpewLogicalArrayBufferBinding());
        break;
    default:
        g_glFuncs.glGetIntegerv(pname, params);
        break;
    }
}

void glGetFloatv(GLenum pname, GLfloat* params) {
    switch (pname) {
    case GL_MODELVIEW_MATRIX: {
        auto* ptr = glm::value_ptr(g_glstate.fpe_uniform.transformation.matrices[matrix_idx(GL_MODELVIEW)]);
        memcpy(params, ptr, sizeof(GLfloat) * 16);
        break;
    }
    case GL_PROJECTION_MATRIX: {
        auto* ptr = glm::value_ptr(g_glstate.fpe_uniform.transformation.matrices[matrix_idx(GL_PROJECTION)]);
        memcpy(params, ptr, sizeof(GLfloat) * 16);
        break;
    }
    default:
        g_glFuncs.glGetFloatv(pname, params);
        break;
    }
}

void glActiveTexture(GLenum texture) {
    auto& state = getLogicalTextureBindings();
    if (getLogicalActiveTexture(state) == texture) return;

    flushPendingImmediateDraws();
    g_glFuncs.glActiveTexture(texture);
    state.activeTexture = texture;
    state.activeTextureKnown = true;
}

void glBindTexture(GLenum target, GLuint texture) {
    auto& state = getLogicalTextureBindings();
    const GLenum activeTexture = getLogicalActiveTexture(state);
    const GLenum query = textureBindingQuery(target);
    const uint64_t key = textureBindingKey(activeTexture, target);

    if (query != GL_NONE) {
        if (sfpewLogicalTextureBinding(target) == texture) return;
    }

    flushPendingImmediateDraws();
    g_glFuncs.glBindTexture(target, texture);
    if (query != GL_NONE) state.bindings[key] = texture;
}

void glDeleteTextures(GLsizei n, const GLuint* textures) {
    flushPendingImmediateDraws();
    g_glFuncs.glDeleteTextures(n, textures);
    if (n <= 0 || textures == nullptr) return;

    auto& bindings = getLogicalTextureBindings().bindings;
    for (auto& [key, binding] : bindings) {
        for (GLsizei i = 0; i < n; ++i) {
            if (binding == textures[i]) {
                binding = 0;
                break;
            }
        }
    }
}

void glTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border,
                  GLenum format, GLenum type, const GLvoid* pixels) {
    flushPendingImmediateDraws();
    if (target != GL_PROXY_TEXTURE_2D) {
        g_glFuncs.glTexImage2D(target, level, internalformat, width, height, border, format, type, pixels);
        return;
    }

    // Proxy textures only test whether an allocation would be accepted. Do not let a
    // backend inspect or copy the caller's pixels for a target that has no storage.
    g_glFuncs.glTexImage2D(target, level, internalformat, width, height, border, format, type, nullptr);

    if (level < 0) return;

    ProxyTexture2DLevel state;
    state.width = width;
    state.height = height;
    state.internalFormat = internalformat;
    state.border = border;

    GLint maxTextureSize = 0;
    g_glFuncs.glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize);
    GLint maxLevelSize = maxTextureSize;
    for (GLint currentLevel = 0; currentLevel < level && maxLevelSize > 0; ++currentLevel) {
        maxLevelSize >>= 1;
    }

    const bool dimensionsSupported = maxLevelSize > 0 && width >= 0 && height >= 0 && width <= maxLevelSize &&
                                     height <= maxLevelSize;
    const bool internalFormatSupported = isProxyTextureInternalFormat(internalformat);
    state.supported = border == 0 && dimensionsSupported && internalFormatSupported &&
                      isProxyTextureFormat(format) && isProxyTextureTypeCompatible(format, type);
    getProxyTexture2DLevels()[level] = state;
}

void glGetTexLevelParameteriv(GLenum target, GLint level, GLenum pname, GLint* params) {
    if (target != GL_PROXY_TEXTURE_2D) {
        g_glFuncs.glGetTexLevelParameteriv(target, level, pname, params);
        return;
    }

    GLint value = 0;
    if (!getProxyTextureLevelParameter(level, pname, value)) {
        g_glFuncs.glGetTexLevelParameteriv(target, level, pname, params);
        return;
    }
    if (params) *params = value;
}

void glGetTexLevelParameterfv(GLenum target, GLint level, GLenum pname, GLfloat* params) {
    if (target != GL_PROXY_TEXTURE_2D) {
        g_glFuncs.glGetTexLevelParameterfv(target, level, pname, params);
        return;
    }

    GLint value = 0;
    if (!getProxyTextureLevelParameter(level, pname, value)) {
        g_glFuncs.glGetTexLevelParameterfv(target, level, pname, params);
        return;
    }
    if (params) *params = static_cast<GLfloat>(value);
}
