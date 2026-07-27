// SimpleFPEWrapper - SimpleFPEWrapper/getter.cpp
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

#include "GL/gl.h"
#include "init.h"
#include "log.h"
#include <glm/gtc/type_ptr.hpp>
#include <cstdint>
#include <algorithm>
#include <unordered_map>
#include <vector>
#include "fpe/fpe.hpp"
#include "fpe/drawing1x.h"
#include "fpe/list.h"

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
        if (!sfpewEnsureBackend() || g_glFuncs.glGetIntegerv == nullptr) return GL_TEXTURE0;
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

namespace {
// Texture names with GL_GENERATE_MIPMAP enabled (per context via the
// logical-bindings reset pattern; names are context/share scoped).
thread_local std::unordered_map<GLuint, bool> generateMipmapTextures;
} // namespace

namespace {
// Level-0 dimensions per texture name, recorded at upload time: ES 3.0 has
// no glGetTexLevelParameter, so readback sizes must come from the shadow.
struct texture_size_t { GLsizei width = 0, height = 0; };
thread_local std::unordered_map<GLuint, texture_size_t> textureSizes;
} // namespace

void sfpewRememberTextureSize(GLuint texture, GLsizei width, GLsizei height) {
    if (texture != 0) textureSizes[texture] = {width, height};
}

void sfpewSetGenerateMipmap(GLenum, GLuint texture, bool enable) {
    if (texture == 0) return;
    if (enable)
        generateMipmapTextures[texture] = true;
    else
        generateMipmapTextures.erase(texture);
}

void sfpewMaybeGenerateMipmap(GLenum target) {
    if (target == GL_TEXTURE_1D) target = GL_TEXTURE_2D;
    if (target != GL_TEXTURE_2D || g_glFuncs.glGenerateMipmap == nullptr) return;
    const GLuint bound = sfpewLogicalTextureBinding(GL_TEXTURE_2D);
    if (bound != 0 && generateMipmapTextures.count(bound) != 0)
        g_glFuncs.glGenerateMipmap(GL_TEXTURE_2D);
}

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

// The desktop GL level the wrapper presents when the backend is GLES.
// Desktop GL loaders parse "<major>.<minor>" out of GL_VERSION and abort on
// "OpenGL ES ...", and the numeric queries must agree with the string, so
// both go through here. The mapping is the industry-standard equivalence
// (ES 3.0 = GL 3.3, ES 3.1 = GL 4.3, ES 3.2 = GL 4.5); it never downgrades
// what the backend can do and the backend's own version string stays
// visible in the suffix. Returns false for a desktop backend, whose version
// already parses and is reported verbatim.
bool sfpewDesktopGLVersion(int* major, int* minor) {
    static int cached_major = 0, cached_minor = 0, cached_is_es = -1;
    if (cached_is_es < 0) {
        const GLubyte* raw =
            g_glFuncs.glGetString != nullptr ? g_glFuncs.glGetString(GL_VERSION) : nullptr;
        if (raw == nullptr) return false; // no context yet: do not cache
        const char* es = std::strstr((const char*)raw, "OpenGL ES ");
        if (es == nullptr) {
            cached_is_es = 0;
        } else {
            int es_major = 3, es_minor = 0;
            std::sscanf(es + 10, "%d.%d", &es_major, &es_minor);
            if (es_major > 3 || (es_major == 3 && es_minor >= 2)) {
                cached_major = 4;
                cached_minor = 5;
            } else if (es_major == 3 && es_minor == 1) {
                cached_major = 4;
                cached_minor = 3;
            } else {
                cached_major = 3;
                cached_minor = 3;
            }
            cached_is_es = 1;
        }
    }
    if (cached_is_es != 1) return false;
    if (major != nullptr) *major = cached_major;
    if (minor != nullptr) *minor = cached_minor;
    return true;
}

// The DESKTOP extension surface the wrapper implements. LWJGL-era engines
// parse this list (not the backend's GL_OES_* one) to switch on their
// FBO/shader/VBO paths, and resolve the corresponding EXT/ARB entry
// points through {egl,glX}GetProcAddress (lookup.cpp aliases them).
const char* const kDesktopExtensions[] = {
    "GL_ARB_compatibility",
    // FCL-ecosystem probe tokens (pre-dating this list; keep for launchers
    // that detect the wrapper's desktop-GL level by these).
    "OpenGL11",
    "OpenGL12",
    "OpenGL13",
    "OpenGL14",
    "OpenGL15",
    "OpenGL20",
    "OpenGL21",
    "GL_ARB_multitexture",
    "GL_ARB_texture_env_add",
    "GL_ARB_texture_env_combine",
    "GL_ARB_texture_env_dot3",
    "GL_ARB_texture_cube_map",
    "GL_ARB_texture_non_power_of_two",
    "GL_ARB_texture_mirrored_repeat",
    "GL_ARB_depth_texture",
    "GL_ARB_shadow",
    "GL_ARB_vertex_buffer_object",
    "GL_ARB_pixel_buffer_object",
    "GL_ARB_shader_objects",
    "GL_ARB_vertex_shader",
    "GL_ARB_fragment_shader",
    "GL_ARB_shading_language_100",
    "GL_ARB_draw_buffers",
    "GL_ARB_point_parameters",
    "GL_ARB_point_sprite",
    "GL_ARB_framebuffer_object",
    "GL_ARB_texture_float",
    "GL_ARB_half_float_pixel",
    "GL_EXT_framebuffer_object",
    "GL_EXT_framebuffer_blit",
    "GL_EXT_packed_depth_stencil",
    "GL_EXT_blend_func_separate",
    "GL_EXT_blend_minmax",
    "GL_EXT_blend_subtract",
    "GL_EXT_blend_color",
    "GL_EXT_fog_coord",
    "GL_EXT_secondary_color",
    "GL_EXT_texture_lod_bias",
    "GL_SGIS_generate_mipmap",
};
constexpr GLint kDesktopExtensionCount =
    (GLint)(sizeof(kDesktopExtensions) / sizeof(kDesktopExtensions[0]));

namespace {

// pnames the wrapper answers itself through glGetIntegerv (scalar integers).
bool isWrapperIntegerPname(GLenum pname) {
    switch (pname) {
    case GL_CONTEXT_PROFILE_MASK:
    case GL_CONTEXT_FLAGS:
    case GL_MAJOR_VERSION:
    case GL_MINOR_VERSION:
    case GL_NUM_EXTENSIONS:
    case GL_CURRENT_PROGRAM:
    case GL_ARRAY_BUFFER_BINDING:
    case GL_MAX_LIGHTS:
    case GL_MAX_TEXTURE_UNITS:
    case GL_MAX_TEXTURE_COORDS:
    case GL_MATRIX_MODE:
    case GL_MAX_MODELVIEW_STACK_DEPTH:
    case GL_MAX_PROJECTION_STACK_DEPTH:
    case GL_MAX_TEXTURE_STACK_DEPTH:
    case GL_MODELVIEW_STACK_DEPTH:
    case GL_PROJECTION_STACK_DEPTH:
    case GL_TEXTURE_STACK_DEPTH:
        return true;
    default:
        return false;
    }
}

// Component counts for common float-domain pnames, needed because
// glGetDoublev has no backend equivalent on GLES and must know how many
// values the backend wrote into the staging buffer.
int floatPnameComponentCount(GLenum pname) {
    switch (pname) {
    case GL_MODELVIEW_MATRIX:
    case GL_PROJECTION_MATRIX:
    case GL_TEXTURE_MATRIX:
    case GL_COLOR_MATRIX:
        return 16;
    case GL_VIEWPORT:
    case GL_SCISSOR_BOX:
    case GL_COLOR_CLEAR_VALUE:
    case GL_BLEND_COLOR:
    case GL_FOG_COLOR:
    case GL_LIGHT_MODEL_AMBIENT:
    case GL_CURRENT_COLOR:
        return 4;
    case GL_DEPTH_RANGE:
    case GL_ALIASED_LINE_WIDTH_RANGE:
    case GL_ALIASED_POINT_SIZE_RANGE:
    case GL_MAX_VIEWPORT_DIMS:
        return 2;
    default:
        return 1;
    }
}

} // namespace

void glGetLightfv(GLenum light, GLenum pname, GLfloat* params) {
    if (!params) return;
    if (light < GL_LIGHT0 || light >= GL_LIGHT0 + MAX_LIGHTS) {
        g_glstate.set_error(GL_INVALID_ENUM);
        return;
    }
    const auto& l = g_glstate.fpe_uniform.lights[light - GL_LIGHT0];
    switch (pname) {
    case GL_AMBIENT:
        memcpy(params, glm::value_ptr(l.ambient), 4 * sizeof(GLfloat));
        break;
    case GL_DIFFUSE:
        memcpy(params, glm::value_ptr(l.diffuse), 4 * sizeof(GLfloat));
        break;
    case GL_SPECULAR:
        memcpy(params, glm::value_ptr(l.specular), 4 * sizeof(GLfloat));
        break;
    case GL_POSITION:
        // Stored in eye coordinates (transformed at call time), which is
        // exactly what GetLight returns per spec.
        memcpy(params, glm::value_ptr(l.position), 4 * sizeof(GLfloat));
        break;
    case GL_SPOT_DIRECTION:
        memcpy(params, glm::value_ptr(l.spot_direction), 3 * sizeof(GLfloat));
        break;
    case GL_SPOT_EXPONENT:
        params[0] = l.spot_exp;
        break;
    case GL_SPOT_CUTOFF:
        params[0] = l.spot_cutoff;
        break;
    case GL_CONSTANT_ATTENUATION:
        params[0] = l.constant_attenuation;
        break;
    case GL_LINEAR_ATTENUATION:
        params[0] = l.linear_attenuation;
        break;
    case GL_QUADRATIC_ATTENUATION:
        params[0] = l.quadratic_attenuation;
        break;
    default:
        g_glstate.set_error(GL_INVALID_ENUM);
        break;
    }
}

void glGetLightiv(GLenum light, GLenum pname, GLint* params) {
    if (!params) return;
    GLfloat staging[4] = {};
    glGetLightfv(light, pname, staging);
    const int count = (pname == GL_AMBIENT || pname == GL_DIFFUSE || pname == GL_SPECULAR ||
                       pname == GL_POSITION)
                          ? 4
                          : (pname == GL_SPOT_DIRECTION ? 3 : 1);
    for (int i = 0; i < count; ++i) params[i] = static_cast<GLint>(staging[i]);
}

void glGetMaterialfv(GLenum face, GLenum pname, GLfloat* params) {
    if (!params) return;
    // GL_FRONT_AND_BACK is valid for setting but not for querying.
    if (face != GL_FRONT && face != GL_BACK) {
        g_glstate.set_error(GL_INVALID_ENUM);
        return;
    }
    const auto& material = g_glstate.fpe_uniform.materials[face == GL_FRONT ? 0 : 1];
    switch (pname) {
    case GL_AMBIENT:
        memcpy(params, glm::value_ptr(material.ambient), 4 * sizeof(GLfloat));
        break;
    case GL_DIFFUSE:
        memcpy(params, glm::value_ptr(material.diffuse), 4 * sizeof(GLfloat));
        break;
    case GL_SPECULAR:
        memcpy(params, glm::value_ptr(material.specular), 4 * sizeof(GLfloat));
        break;
    case GL_EMISSION:
        memcpy(params, glm::value_ptr(material.emission), 4 * sizeof(GLfloat));
        break;
    case GL_SHININESS:
        params[0] = material.shininess;
        break;
    case GL_COLOR_INDEXES:
        memcpy(params, glm::value_ptr(material.color_indexes), 3 * sizeof(GLfloat));
        break;
    default:
        g_glstate.set_error(GL_INVALID_ENUM);
        break;
    }
}

void glGetMaterialiv(GLenum face, GLenum pname, GLint* params) {
    if (!params) return;
    GLfloat staging[4] = {};
    glGetMaterialfv(face, pname, staging);
    const int count = pname == GL_SHININESS ? 1 : (pname == GL_COLOR_INDEXES ? 3 : 4);
    for (int i = 0; i < count; ++i) params[i] = static_cast<GLint>(staging[i]);
}

void glGetTexEnvfv(GLenum target, GLenum pname, GLfloat* params) {
    if (!params) return;
    if (target != GL_TEXTURE_ENV) {
        g_glstate.set_error(GL_INVALID_ENUM);
        return;
    }
    const int unit = std::clamp(static_cast<int>(sfpewLogicalActiveTexture() - GL_TEXTURE0), 0, MAX_TEX - 1);
    const auto& env = g_glstate.fpe_uniform.texture_env[unit];
    switch (pname) {
    case GL_TEXTURE_ENV_MODE:
        params[0] = static_cast<GLfloat>(env.mode);
        break;
    case GL_TEXTURE_ENV_COLOR:
        memcpy(params, glm::value_ptr(env.color), 4 * sizeof(GLfloat));
        break;
    case GL_COMBINE_RGB:
        params[0] = static_cast<GLfloat>(env.combine_rgb);
        break;
    case GL_COMBINE_ALPHA:
        params[0] = static_cast<GLfloat>(env.combine_alpha);
        break;
    case GL_SRC0_RGB:
    case GL_SRC1_RGB:
    case GL_SRC2_RGB:
        params[0] = static_cast<GLfloat>(env.source_rgb[pname - GL_SRC0_RGB]);
        break;
    case GL_SRC0_ALPHA:
    case GL_SRC1_ALPHA:
    case GL_SRC2_ALPHA:
        params[0] = static_cast<GLfloat>(env.source_alpha[pname - GL_SRC0_ALPHA]);
        break;
    case GL_OPERAND0_RGB:
    case GL_OPERAND1_RGB:
    case GL_OPERAND2_RGB:
        params[0] = static_cast<GLfloat>(env.operand_rgb[pname - GL_OPERAND0_RGB]);
        break;
    case GL_OPERAND0_ALPHA:
    case GL_OPERAND1_ALPHA:
    case GL_OPERAND2_ALPHA:
        params[0] = static_cast<GLfloat>(env.operand_alpha[pname - GL_OPERAND0_ALPHA]);
        break;
    case GL_RGB_SCALE:
        params[0] = env.rgb_scale;
        break;
    case GL_ALPHA_SCALE:
        params[0] = env.alpha_scale;
        break;
    default:
        g_glstate.set_error(GL_INVALID_ENUM);
        break;
    }
}

void glGetTexEnviv(GLenum target, GLenum pname, GLint* params) {
    if (!params) return;
    GLfloat staging[4] = {};
    glGetTexEnvfv(target, pname, staging);
    const int count = pname == GL_TEXTURE_ENV_COLOR ? 4 : 1;
    for (int i = 0; i < count; ++i) params[i] = static_cast<GLint>(staging[i]);
}

namespace {
int texgenCoordIndex(GLenum coord) {
    switch (coord) {
    case GL_S: return 0;
    case GL_T: return 1;
    case GL_R: return 2;
    case GL_Q: return 3;
    default: return -1;
    }
}
} // namespace

// GL_TEXTURE_1D does not exist on GLES: 1D textures are stored as Nx1 2D
// textures. Any wrap mode samples row 0 of an Nx1 texture, so generated
// shaders need no coordinate rewrite. (plans/05, 5.4)
void glTexImage1D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLint border,
                  GLenum format, GLenum type, const GLvoid* pixels) {
    if (target != GL_TEXTURE_1D && target != GL_PROXY_TEXTURE_1D) {
        g_glstate.set_error(GL_INVALID_ENUM);
        return;
    }
    if (target == GL_PROXY_TEXTURE_1D) return; // no proxy bookkeeping for 1D
    glTexImage2D(GL_TEXTURE_2D, level, internalformat, width, 1, border, format, type, pixels);
}

void glTexSubImage1D(GLenum target, GLint level, GLint xoffset, GLsizei width, GLenum format,
                     GLenum type, const GLvoid* pixels) {
    if (target != GL_TEXTURE_1D) {
        g_glstate.set_error(GL_INVALID_ENUM);
        return;
    }
    glTexSubImage2D(GL_TEXTURE_2D, level, xoffset, 0, width, 1, format, type, pixels);
}

void glGetTexGenfv(GLenum coord, GLenum pname, GLfloat* params) {
    if (!params) return;
    const int c = texgenCoordIndex(coord);
    if (c < 0) {
        g_glstate.set_error(GL_INVALID_ENUM);
        return;
    }
    const int unit = std::clamp(static_cast<int>(sfpewLogicalActiveTexture() - GL_TEXTURE0), 0, MAX_TEX - 1);
    switch (pname) {
    case GL_TEXTURE_GEN_MODE:
        params[0] = static_cast<GLfloat>(g_glstate.fpe_state.texture_gen_mode[unit][c]);
        break;
    case GL_OBJECT_PLANE:
        memcpy(params, glm::value_ptr(g_glstate.fpe_uniform.texgen_object_plane[unit][c]), 4 * sizeof(GLfloat));
        break;
    case GL_EYE_PLANE:
        memcpy(params, glm::value_ptr(g_glstate.fpe_uniform.texgen_eye_plane[unit][c]), 4 * sizeof(GLfloat));
        break;
    default:
        g_glstate.set_error(GL_INVALID_ENUM);
        break;
    }
}

void glGetTexGeniv(GLenum coord, GLenum pname, GLint* params) {
    if (!params) return;
    GLfloat staging[4] = {};
    glGetTexGenfv(coord, pname, staging);
    const int count = pname == GL_TEXTURE_GEN_MODE ? 1 : 4;
    for (int i = 0; i < count; ++i) params[i] = static_cast<GLint>(staging[i]);
}

void glGetTexGendv(GLenum coord, GLenum pname, GLdouble* params) {
    if (!params) return;
    GLfloat staging[4] = {};
    glGetTexGenfv(coord, pname, staging);
    const int count = pname == GL_TEXTURE_GEN_MODE ? 1 : 4;
    for (int i = 0; i < count; ++i) params[i] = static_cast<GLdouble>(staging[i]);
}

GLboolean glIsEnabled(GLenum cap) {
    const auto& bools = g_glstate.fpe_state.fpe_bools;
    switch (cap) {
    case GL_FOG:
        return bools.fog_enable ? GL_TRUE : GL_FALSE;
    case GL_LIGHTING:
        return bools.lighting_enable ? GL_TRUE : GL_FALSE;
    case GL_ALPHA_TEST:
        return bools.alpha_test_enable ? GL_TRUE : GL_FALSE;
    case GL_COLOR_MATERIAL:
        return bools.color_material_enable ? GL_TRUE : GL_FALSE;
    case GL_NORMALIZE:
        return bools.normalize_enable ? GL_TRUE : GL_FALSE;
    case GL_RESCALE_NORMAL:
        return bools.rescale_normal_enable ? GL_TRUE : GL_FALSE;
    case GL_TEXTURE_2D: {
        const int unit = std::clamp(static_cast<int>(sfpewLogicalActiveTexture() - GL_TEXTURE0), 0, MAX_TEX - 1);
        return bools.texture_2d_enable[unit] ? GL_TRUE : GL_FALSE;
    }
    default:
        if (cap >= GL_LIGHT0 && cap < GL_LIGHT0 + MAX_LIGHTS)
            return bools.light_enable[cap - GL_LIGHT0] ? GL_TRUE : GL_FALSE;
        if (!sfpewEnsureBackend() || g_glFuncs.glIsEnabled == nullptr) return GL_FALSE;
        return g_glFuncs.glIsEnabled(cap);
    }
}

void glGetBooleanv(GLenum pname, GLboolean* params) {
    if (!params) return;
    if (isWrapperIntegerPname(pname)) {
        GLint value = 0;
        glGetIntegerv(pname, &value);
        params[0] = value != 0 ? GL_TRUE : GL_FALSE;
        return;
    }
    if (!sfpewEnsureBackend() || g_glFuncs.glGetBooleanv == nullptr) return;
    g_glFuncs.glGetBooleanv(pname, params);
}

void glGetDoublev(GLenum pname, GLdouble* params) {
    if (!params) return;
    if (isWrapperIntegerPname(pname)) {
        GLint value = 0;
        glGetIntegerv(pname, &value);
        params[0] = static_cast<GLdouble>(value);
        return;
    }
    // Everything else is float-domain: stage through the (wrapper) float
    // getter and widen; the count table bounds how much we copy out.
    GLfloat staging[16] = {};
    glGetFloatv(pname, staging);
    const int count = floatPnameComponentCount(pname);
    for (int i = 0; i < count; ++i) params[i] = static_cast<GLdouble>(staging[i]);
}

GLenum glGetError() {
    // Wrapper-detected errors take priority over backend errors: legacy
    // paths validate before any backend call, so ours happened first.
    auto& state = g_glstate;
    if (state.first_error != GL_NO_ERROR) {
        const GLenum error = state.first_error;
        state.first_error = GL_NO_ERROR;
        return error;
    }
    if (!sfpewEnsureBackend() || g_glFuncs.glGetError == nullptr) return GL_NO_ERROR;
    return g_glFuncs.glGetError();
}

const GLubyte* glGetString(GLenum name) {
    if (!sfpewEnsureBackend() || g_glFuncs.glGetString == nullptr) return nullptr;

    // we only wrap GL_VERSION GL_RENDERER GL_VENDOR
    // Backend glGetString returns null without a current context; report
    // that to the caller and, crucially, never let it poison the caches.
    switch (name) {
    case GL_VERSION: {
        static std::string cachedVersionString;
        if (cachedVersionString.empty()) {
            const GLubyte* backend = g_glFuncs.glGetString(GL_VERSION);
            if (!backend) return nullptr;
            int major = 0, minor = 0;
            if (sfpewDesktopGLVersion(&major, &minor)) {
                // Desktop-parseable level first, backend identity after it.
                cachedVersionString = std::to_string(major) + "." + std::to_string(minor) +
                                      " SFPEW (" + (const char*)backend + ")";
            } else {
                cachedVersionString =
                    std::string((const char*)backend) + " (with Simple FPE Wrapper)";
            }
        }
        return (const GLubyte*)cachedVersionString.c_str();
    }
    case GL_SHADING_LANGUAGE_VERSION: {
        // Must pair with the GL level above: GL 3.3 -> GLSL 3.30, GL 4.x ->
        // GLSL 4.x0. The translator accepts any input version regardless.
        static std::string cachedGlslString;
        if (cachedGlslString.empty()) {
            int major = 0, minor = 0;
            if (sfpewDesktopGLVersion(&major, &minor)) {
                const GLubyte* backend = g_glFuncs.glGetString(GL_SHADING_LANGUAGE_VERSION);
                cachedGlslString = std::to_string(major) + "." + std::to_string(minor) +
                                   "0 SFPEW (" + (backend ? (const char*)backend : "") + ")";
            } else {
                const GLubyte* backend = g_glFuncs.glGetString(GL_SHADING_LANGUAGE_VERSION);
                if (!backend) return nullptr;
                cachedGlslString = (const char*)backend;
            }
        }
        return (const GLubyte*)cachedGlslString.c_str();
    }
    case GL_EXTENSIONS: {
        // ADDITIVE surface: everything the backend really exposes, plus the
        // desktop extensions the wrapper implements on top. The backend's
        // own capability set (full ES 3.2 / GL 4.6) must stay visible.
        static std::string cachedExtensionString;
        if (cachedExtensionString.empty()) {
            const GLubyte* backend = g_glFuncs.glGetString(GL_EXTENSIONS);
            if (!backend) return nullptr;
            std::string joined((const char*)backend);
            for (GLint i = 0; i < kDesktopExtensionCount; ++i) {
                if (joined.find(kDesktopExtensions[i]) != std::string::npos) continue;
                joined += ' ';
                joined += kDesktopExtensions[i];
            }
            cachedExtensionString = std::move(joined);
        }
        return (const GLubyte*)cachedExtensionString.c_str();
    }
    case GL_RENDERER: {
        static std::string cachedRendererString;
        if (cachedRendererString.empty()) {
            const GLubyte* backend = g_glFuncs.glGetString(GL_RENDERER);
            if (!backend) return nullptr;
            cachedRendererString = std::string((const char*)backend) + " (SFPEW)";
        }
        return (const GLubyte*)cachedRendererString.c_str();
    }
    case GL_VENDOR: {
        static std::string cachedVendorString;
        if (cachedVendorString.empty()) {
            const GLubyte* backend = g_glFuncs.glGetString(GL_VENDOR);
            if (!backend) return nullptr;
            cachedVendorString = std::string((const char*)backend);
            if (!containsMobileGLDev(cachedVendorString)) {
                cachedVendorString += " (SFPEW: MobileGL-Dev)";
            }
        }
        return (const GLubyte*)cachedVendorString.c_str();
    }
    default:
        return g_glFuncs.glGetString(name);
    }
}

const GLubyte* glGetStringi(GLenum name, GLuint index) {
    if (!sfpewEnsureBackend() || g_glFuncs.glGetStringi == nullptr) return nullptr;

    if (name != GL_EXTENSIONS) {
        return g_glFuncs.glGetStringi(name, index);
    }
    // Additive like glGetString(GL_EXTENSIONS): the wrapper's desktop
    // extensions first, then everything the backend exposes.
    if (index < (GLuint)kDesktopExtensionCount)
        return (const GLubyte*)kDesktopExtensions[index];
    return g_glFuncs.glGetStringi(name, index - (GLuint)kDesktopExtensionCount);
}

void glGetIntegerv(GLenum pname, GLint* params) {
    // A null out-pointer is caller error; GL never throws. Returning quietly
    // here keeps C callers alive (error injection arrives with the S2 error
    // machine).
    if (!params) return;
    if (!sfpewEnsureBackend() || g_glFuncs.glGetIntegerv == nullptr) return;

    switch (pname) {
    case GL_CONTEXT_PROFILE_MASK:
        *params = GL_CONTEXT_COMPATIBILITY_PROFILE_BIT;
        break;
    case GL_CONTEXT_FLAGS:
        *params = 0;
        break;
    case GL_MAJOR_VERSION:
    case GL_MINOR_VERSION: {
        // Must agree with the desktop level glGetString(GL_VERSION) reports:
        // a loader that trusts one and validates the other would otherwise
        // see GL 4.5 in the string and 3.2 in the integers.
        int major = 0, minor = 0;
        if (sfpewDesktopGLVersion(&major, &minor)) {
            *params = pname == GL_MAJOR_VERSION ? major : minor;
        } else {
            g_glFuncs.glGetIntegerv(pname, params);
        }
        break;
    }
    case GL_NUM_EXTENSIONS: {
        static GLint cachedNumExtensions = -1;
        if (cachedNumExtensions < 0) {
            // Without a current context the backend leaves the out-param
            // untouched; only cache a value the backend actually wrote.
            GLint backendCount = -1;
            g_glFuncs.glGetIntegerv(GL_NUM_EXTENSIONS, &backendCount);
            if (backendCount < 0) {
                *params = 0;
                return;
            }
            cachedNumExtensions = backendCount + kDesktopExtensionCount;
        }
        *params = cachedNumExtensions;
        break;
    }
    case GL_CURRENT_PROGRAM:
        *params = sfpewLogicalProgram();
        break;
    case GL_ARRAY_BUFFER_BINDING:
        *params = static_cast<GLint>(sfpewLogicalArrayBufferBinding());
        break;

    // Fixed-function capacity and matrix-state queries. These pnames do not
    // exist on the GLES backend (passthrough returned GL_INVALID_ENUM and
    // left the out-param untouched), yet they are the first things legacy
    // apps ask for.
    case GL_MAX_LIGHTS:
        *params = MAX_LIGHTS;
        break;
    case GL_MAX_TEXTURE_UNITS: // == GL_MAX_TEXTURE_COORDS consumers
    case GL_MAX_TEXTURE_COORDS:
        // Report the conventional fixed-function unit count, not MAX_TEX:
        // plans/03 keeps the advertised surface at the well-tested subset.
        *params = 8;
        break;
    case GL_MATRIX_MODE:
        *params = static_cast<GLint>(g_glstate.fpe_uniform.transformation.matrix_mode);
        break;
    case GL_MAX_MODELVIEW_STACK_DEPTH:
        *params = MAX_MODELVIEW_STACK_DEPTH;
        break;
    case GL_MAX_PROJECTION_STACK_DEPTH:
        *params = MAX_PROJECTION_STACK_DEPTH;
        break;
    case GL_MAX_TEXTURE_STACK_DEPTH:
        *params = MAX_TEXTURE_STACK_DEPTH;
        break;
    // Stack depth includes the current (unpushed) matrix per GL spec.
    case GL_MODELVIEW_STACK_DEPTH:
        *params = static_cast<GLint>(
            g_glstate.fpe_uniform.transformation.matrices_stack[matrix_idx(GL_MODELVIEW)].size() + 1);
        break;
    case GL_PROJECTION_STACK_DEPTH:
        *params = static_cast<GLint>(
            g_glstate.fpe_uniform.transformation.matrices_stack[matrix_idx(GL_PROJECTION)].size() + 1);
        break;
    case GL_MAX_LIST_NESTING:
        *params = 64;
        break;
    case GL_LIST_INDEX:
        *params = static_cast<GLint>(DisplayListManager::currentList());
        break;
    case GL_LIST_MODE:
        *params = DisplayListManager::currentList() != 0
                      ? static_cast<GLint>(DisplayListManager::currentListMode())
                      : 0;
        break;
    case GL_LIST_BASE:
        *params = static_cast<GLint>(DisplayListManager::listBase());
        break;
    case GL_TEXTURE_STACK_DEPTH: {
        const int unit = std::clamp(static_cast<int>(sfpewLogicalActiveTexture() - GL_TEXTURE0), 0, MAX_TEX - 1);
        *params = static_cast<GLint>(
            g_glstate.fpe_uniform.transformation.texture_matrices_stack[unit].size() + 1);
        break;
    }
    default:
        g_glFuncs.glGetIntegerv(pname, params);
        break;
    }
}

void glGetFloatv(GLenum pname, GLfloat* params) {
    if (!params) return;
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
    case GL_TEXTURE_MATRIX: {
        const int unit = std::clamp(static_cast<int>(sfpewLogicalActiveTexture() - GL_TEXTURE0), 0, MAX_TEX - 1);
        auto* ptr = glm::value_ptr(g_glstate.fpe_uniform.transformation.texture_matrices[unit]);
        memcpy(params, ptr, sizeof(GLfloat) * 16);
        break;
    }
    case GL_ZOOM_X:
        params[0] = g_glstate.fpe_uniform.pixel_zoom_x;
        break;
    case GL_ZOOM_Y:
        params[0] = g_glstate.fpe_uniform.pixel_zoom_y;
        break;
    case GL_FOG_DENSITY:
        params[0] = g_glstate.fpe_uniform.fog_density;
        break;
    case GL_FOG_START:
        params[0] = g_glstate.fpe_uniform.fog_start;
        break;
    case GL_FOG_END:
        params[0] = g_glstate.fpe_uniform.fog_end;
        break;
    case GL_CURRENT_RASTER_POSITION:
        memcpy(params, glm::value_ptr(g_glstate.fpe_uniform.raster_position), 4 * sizeof(GLfloat));
        break;
    case GL_CURRENT_RASTER_COLOR:
        memcpy(params, glm::value_ptr(g_glstate.fpe_uniform.raster_color), 4 * sizeof(GLfloat));
        break;
    case GL_CURRENT_RASTER_TEXTURE_COORDS:
        memcpy(params, glm::value_ptr(g_glstate.fpe_uniform.raster_texcoord), 4 * sizeof(GLfloat));
        break;
    case GL_CURRENT_RASTER_POSITION_VALID:
        params[0] = g_glstate.fpe_uniform.raster_position_valid ? 1.0f : 0.0f;
        break;
    case GL_COLOR_MATRIX: {
        auto* ptr = glm::value_ptr(g_glstate.fpe_uniform.transformation.matrices[matrix_idx(GL_COLOR)]);
        memcpy(params, ptr, sizeof(GLfloat) * 16);
        break;
    }
    default:
        if (sfpewEnsureBackend() && g_glFuncs.glGetFloatv != nullptr) g_glFuncs.glGetFloatv(pname, params);
        break;
    }
}

void glActiveTexture(GLenum texture) {
    if (!sfpewEnsureBackend() || g_glFuncs.glActiveTexture == nullptr) return;
    // Record BEFORE the redundancy shortcut: a list must contain the command
    // even when it matches the current state (GL spec; audit finding).
    LIST_RECORD(glActiveTexture, {}, texture)
    auto& state = getLogicalTextureBindings();
    if (getLogicalActiveTexture(state) == texture) return;

    flushPendingImmediateDraws();
    g_glFuncs.glActiveTexture(texture);
    state.activeTexture = texture;
    state.activeTextureKnown = true;
}

void glBindTexture(GLenum target, GLuint texture) {
    if (!sfpewEnsureBackend() || g_glFuncs.glBindTexture == nullptr) return;
    if (target == GL_TEXTURE_1D) target = GL_TEXTURE_2D; // Nx1 emulation
    LIST_RECORD(glBindTexture, {}, target, texture)
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
    if (!sfpewEnsureBackend() || g_glFuncs.glDeleteTextures == nullptr) return;
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

namespace {

// Legacy desktop internalformats have no GLES3 equivalent. Map them onto
// R8/RG8 storage plus a texture swizzle that reproduces the fixed-function
// sampling semantics (plans/05, 5.3).
struct legacy_format_mapping_t {
    GLint internalformat;
    GLenum format;
    GLint swizzle[4];
};

bool mapLegacyInternalFormat(GLint internalformat, legacy_format_mapping_t& out) {
    switch (internalformat) {
    case GL_ALPHA:
    case GL_ALPHA4:
    case GL_ALPHA8:
    case GL_ALPHA12:
    case GL_ALPHA16:
        out = {GL_R8, GL_RED, {GL_ZERO, GL_ZERO, GL_ZERO, GL_RED}};
        return true;
    case GL_LUMINANCE:
    case GL_LUMINANCE4:
    case GL_LUMINANCE8:
    case GL_LUMINANCE12:
    case GL_LUMINANCE16:
    case 1:
        out = {GL_R8, GL_RED, {GL_RED, GL_RED, GL_RED, GL_ONE}};
        return true;
    case GL_LUMINANCE_ALPHA:
    case GL_LUMINANCE4_ALPHA4:
    case GL_LUMINANCE6_ALPHA2:
    case GL_LUMINANCE8_ALPHA8:
    case GL_LUMINANCE12_ALPHA4:
    case GL_LUMINANCE12_ALPHA12:
    case GL_LUMINANCE16_ALPHA16:
    case 2:
        out = {GL_RG8, GL_RG, {GL_RED, GL_RED, GL_RED, GL_GREEN}};
        return true;
    case GL_INTENSITY:
    case GL_INTENSITY4:
    case GL_INTENSITY8:
    case GL_INTENSITY12:
    case GL_INTENSITY16:
        out = {GL_R8, GL_RED, {GL_RED, GL_RED, GL_RED, GL_RED}};
        return true;
    case 3:
        out = {GL_RGB8, GL_RGB, {GL_RED, GL_GREEN, GL_BLUE, GL_ONE}};
        return true;
    case 4:
        out = {GL_RGBA8, GL_RGBA, {GL_RED, GL_GREEN, GL_BLUE, GL_ALPHA}};
        return true;
    // GL 2.1 sRGB internalformats (EXT_texture_sRGB) onto the ES3 natives.
    case GL_SRGB:
    case GL_SRGB8:
        out = {GL_SRGB8, GL_RGB, {GL_RED, GL_GREEN, GL_BLUE, GL_ONE}};
        return true;
    case GL_SRGB_ALPHA:
    case GL_SRGB8_ALPHA8:
        out = {GL_SRGB8_ALPHA8, GL_RGBA, {GL_RED, GL_GREEN, GL_BLUE, GL_ALPHA}};
        return true;
    default:
        return false;
    }
}

GLenum swizzleTargetFor(GLenum target) {
    if (target >= GL_TEXTURE_CUBE_MAP_POSITIVE_X && target <= GL_TEXTURE_CUBE_MAP_NEGATIVE_Z)
        return GL_TEXTURE_CUBE_MAP;
    return target;
}

// GL_BGRA uploads: GLES3 has no core BGRA path; swap on the CPU.
// Tightly-packed rows are assumed (the common LWJGL/awt case); exotic
// unpack state falls back to passthrough with a log line.
const void* swapBgraPixels(GLsizei width, GLsizei height, GLenum type, const GLvoid* pixels,
                           std::vector<uint8_t>& scratch) {
    if (pixels == nullptr) return nullptr;
    if (type != GL_UNSIGNED_BYTE && type != GL_UNSIGNED_INT_8_8_8_8 && type != GL_UNSIGNED_INT_8_8_8_8_REV) {
        return nullptr;
    }
    const size_t count = (size_t)width * (size_t)height * 4u;
    scratch.resize(count);
    const auto* src = static_cast<const uint8_t*>(pixels);
    for (size_t px = 0; px < count; px += 4) {
        scratch[px + 0] = src[px + 2];
        scratch[px + 1] = src[px + 1];
        scratch[px + 2] = src[px + 0];
        scratch[px + 3] = src[px + 3];
    }
    return scratch.data();
}

} // namespace

void glTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border,
                  GLenum format, GLenum type, const GLvoid* pixels) {
    if (!sfpewEnsureBackend() || g_glFuncs.glTexImage2D == nullptr || g_glFuncs.glGetIntegerv == nullptr) return;
    flushPendingImmediateDraws();
    if (target != GL_PROXY_TEXTURE_2D) {
        thread_local std::vector<uint8_t> bgraScratch;
        if (format == GL_BGRA && !sfpewUnpackPboBound()) {
            const void* swapped = swapBgraPixels(width, height, type, pixels, bgraScratch);
            if (swapped != nullptr || pixels == nullptr) {
                pixels = swapped;
                format = GL_RGBA;
                type = GL_UNSIGNED_BYTE;
                if (internalformat == GL_BGRA) internalformat = GL_RGBA8;
            } else {
                SFPEW_LOGW("glTexImage2D: unsupported GL_BGRA type 0x%x passed through", type);
            }
        }

        legacy_format_mapping_t mapping{};
        if (mapLegacyInternalFormat(internalformat, mapping)) {
            // PBO uploads keep their data GPU-side; format rewriting is safe
            // (no dereference) but the BGRA swap above already bailed out.
            // Rewrite the caller's legacy format pair too: GL_ALPHA/
            // GL_LUMINANCE* client data is single/dual channel and GLES3
            // only accepts it through RED/RG uploads.
            GLenum upload_format = format;
            if (format == GL_ALPHA || format == GL_LUMINANCE) upload_format = GL_RED;
            else if (format == GL_LUMINANCE_ALPHA) upload_format = GL_RG;
            g_glFuncs.glTexImage2D(target, level, mapping.internalformat, width, height, border,
                                   upload_format, type, pixels);
            if (g_glFuncs.glTexParameteriv != nullptr)
                g_glFuncs.glTexParameteriv(swizzleTargetFor(target), GL_TEXTURE_SWIZZLE_RGBA, mapping.swizzle);
            sfpewMaybeGenerateMipmap(target);
            return;
        }

        g_glFuncs.glTexImage2D(target, level, internalformat, width, height, border, format, type, pixels);
        if (level == 0) sfpewRememberTextureSize(sfpewLogicalTextureBinding(GL_TEXTURE_2D), width, height);
        sfpewMaybeGenerateMipmap(target);
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

void glGetTexImage(GLenum target, GLint level, GLenum format, GLenum type, GLvoid* pixels) {
    if (pixels == nullptr) return;
    if (!sfpewEnsureBackend() || g_glFuncs.glGenFramebuffers == nullptr ||
        g_glFuncs.glFramebufferTexture2D == nullptr || g_glFuncs.glReadPixels == nullptr) {
        return;
    }
    if (target == GL_TEXTURE_1D) target = GL_TEXTURE_2D; // Nx1 emulation
    if (target != GL_TEXTURE_2D) {
        SFPEW_LOGW("glGetTexImage: target 0x%x not supported", target);
        g_glstate.set_error(GL_INVALID_ENUM);
        return;
    }
    const GLuint texture = sfpewLogicalTextureBinding(GL_TEXTURE_2D);
    const auto size_it = textureSizes.find(texture);
    if (texture == 0 || size_it == textureSizes.end()) {
        g_glstate.set_error(GL_INVALID_OPERATION);
        return;
    }
    const GLsizei width = std::max<GLsizei>(size_it->second.width >> level, 1);
    const GLsizei height = std::max<GLsizei>(size_it->second.height >> level, 1);

    // Read through a scratch FBO; the wrapper glReadPixels handles BGRA.
    GLint prev_read = 0, prev_draw = 0;
    g_glFuncs.glGetIntegerv(0x8CAA /* GL_READ_FRAMEBUFFER_BINDING */, &prev_read);
    g_glFuncs.glGetIntegerv(0x8CA6 /* GL_DRAW_FRAMEBUFFER_BINDING */, &prev_draw);
    static thread_local GLuint scratch_fbo = 0;
    if (scratch_fbo == 0) g_glFuncs.glGenFramebuffers(1, &scratch_fbo);
    g_glFuncs.glBindFramebuffer(0x8CA8 /* GL_READ_FRAMEBUFFER */, scratch_fbo);
    g_glFuncs.glFramebufferTexture2D(0x8CA8, 0x8CE0 /* GL_COLOR_ATTACHMENT0 */, GL_TEXTURE_2D,
                                     texture, level);
    glReadPixels(0, 0, width, height, format, type, pixels);
    g_glFuncs.glFramebufferTexture2D(0x8CA8, 0x8CE0, GL_TEXTURE_2D, 0, 0);
    g_glFuncs.glBindFramebuffer(0x8CA8, prev_read);
    g_glFuncs.glBindFramebuffer(0x8CA9 /* GL_DRAW_FRAMEBUFFER */, prev_draw);
}

void glGetTexLevelParameteriv(GLenum target, GLint level, GLenum pname, GLint* params) {
    if (!sfpewEnsureBackend() || g_glFuncs.glGetTexLevelParameteriv == nullptr) return;
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
    if (!sfpewEnsureBackend() || g_glFuncs.glGetTexLevelParameterfv == nullptr) return;
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
