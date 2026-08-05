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
#include "version.h"
#include <glm/gtc/type_ptr.hpp>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <unordered_map>
#include <vector>
#include "fpe/fpe.hpp"
#include "fpe/drawing1x.h"
#include "fpe/list.h"
#include "fpe/imaging.h"
#include "fpe/vertexpointer_utils.h"

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
    // Bumped by every real change to the active unit or to a binding. The
    // immediate-mode merge key rides on this instead of re-reading the
    // active unit and its 2D binding for every Begin/End batch: those two
    // reads (a map lookup each) cost more than everything else the merge
    // decision does. Both mutators below already return early when nothing
    // changes, so an unchanged state never bumps and never forces a flush.
    uint64_t generation = 1;
};

thread_local LogicalTextureBindings logicalTextureBindings;

LogicalTextureBindings& getLogicalTextureBindings() {
    // Reconciles against the snapshot of the calling entry's strict resolve
    // (docs/context-model.md); no eglGetCurrentContext of its own.
    const EGLContext context = (EGLContext)glstate_t::cached_context();
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
    const EGLContext context = (EGLContext)glstate_t::cached_context();
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
// Texture image metadata recorded at definition time. ES 3.0 has no
// glGetTexLevelParameter, so the wrapper answers the level properties it can
// know without retaining a second copy of the texture payload.
struct texture_size_t { GLsizei width = 0, height = 0; };
struct texture_level_t {
    GLsizei width = 0;
    GLsizei height = 0;
    GLsizei depth = 1; // GL_TEXTURE_DEPTH; meaningless (stays 1) off GL_TEXTURE_3D
    GLint internal_format = 0;
    GLint border = 0;
    bool compressed = false;
    GLsizei compressed_size = 0;
};
struct texture_metadata_cache_t {
    EGLContext context = EGL_NO_CONTEXT;
    // Keyed on texture name alone: this is only ever written/read for
    // GL_TEXTURE_2D (see repairMipmapChain2D), never for another target, so
    // there is no cross-target collision to guard against.
    std::unordered_map<GLuint, texture_size_t> sizes;
    // defects-plan-2.md 2.7: keyed on (name, target), not name alone - a 2D
    // texture and a 3D/cube texture can share a name over their lifetimes
    // (GL only forbids rebinding one already-used name to a *different*
    // target, not reusing a deleted name for any target), and a cube map's
    // six faces collapse onto one GL_TEXTURE_CUBE_MAP entry per level - spec
    // requires every face at a given level to share the same size/format
    // for cube completeness, so there is nothing to distinguish between
    // them (see textureMetadataTarget() below, the same face-collapsing
    // rule swizzleTargetFor() applies for GL_TEXTURE_SWIZZLE_RGBA).
    std::unordered_map<uint64_t, std::unordered_map<GLint, texture_level_t>> levels;
};

// Collapses a cube face target to GL_TEXTURE_CUBE_MAP and a GL_TEXTURE_1D
// target to GL_TEXTURE_2D (the storage it actually shares, see
// hijack_fpe_states's GL_TEXTURE_1D case) - the metadata key only needs to
// distinguish storage, not every alias GL accepts for it. Defined ahead of
// swizzleTargetFor()'s own copy of the cube-face half of this rule further
// down in this file; kept separate rather than reordered, since that one is
// scoped to its own BGRA-upload section and this one predates it.
GLenum textureMetadataTarget(GLenum target) {
    if (target >= GL_TEXTURE_CUBE_MAP_POSITIVE_X && target <= GL_TEXTURE_CUBE_MAP_NEGATIVE_Z)
        return GL_TEXTURE_CUBE_MAP;
    if (target == GL_TEXTURE_1D) return GL_TEXTURE_2D;
    return target;
}

uint64_t textureMetadataKey(GLuint texture, GLenum target) {
    return (static_cast<uint64_t>(texture) << 32) |
           static_cast<uint64_t>(textureMetadataTarget(target));
}
// A raw pointer behind a lazy heap allocation, not a thread_local
// texture_metadata_cache_t directly: the struct carries two
// std::unordered_maps, and this library is always reached via dlopen (the
// test harness here, and the MobileGlues plugin path on Android), where a
// dlopen'd module's static TLS block is a small, fixed-size reservation that
// a pair of unordered_map control blocks eats into for no benefit - the maps
// themselves live on the heap either way.
thread_local texture_metadata_cache_t* textureMetadataCachePtr = nullptr;

texture_metadata_cache_t& textureMetadata() {
    if (textureMetadataCachePtr == nullptr) textureMetadataCachePtr = new texture_metadata_cache_t();
    auto& textureMetadataCache = *textureMetadataCachePtr;
    const EGLContext context = (EGLContext)glstate_t::cached_context();
    if (textureMetadataCache.context != context) {
        textureMetadataCache = {};
        textureMetadataCache.context = context;
    }
    return textureMetadataCache;
}

void rememberTextureLevel(GLuint texture, GLenum target, GLint level, GLsizei width, GLsizei height,
                          GLint internal_format, GLint border, bool compressed,
                          GLsizei compressed_size, GLsizei depth = 1) {
    if (texture == 0 || level < 0) return;
    auto& metadata = textureMetadata();
    metadata.levels[textureMetadataKey(texture, target)][level] =
        {width, height, depth, internal_format, border, compressed, compressed_size};
    // .sizes is 2D-only (see the field comment); a non-2D target has
    // nothing to record there.
    if (level == 0 && textureMetadataTarget(target) == GL_TEXTURE_2D)
        metadata.sizes[texture] = {width, height};
}

const texture_level_t* findTextureLevel(GLuint texture, GLenum target, GLint level) {
    auto& levels = textureMetadata().levels;
    const auto texture_it = levels.find(textureMetadataKey(texture, target));
    if (texture_it == levels.end()) return nullptr;
    const auto level_it = texture_it->second.find(level);
    return level_it == texture_it->second.end() ? nullptr : &level_it->second;
}
} // namespace

void sfpewRememberTextureSize(GLuint texture, GLsizei width, GLsizei height) {
    if (texture != 0) textureMetadata().sizes[texture] = {width, height};
}

void sfpewSetGenerateMipmap(GLenum, GLuint texture, bool enable) {
    if (texture == 0) return;
    if (enable)
        generateMipmapTextures[texture] = true;
    else
        generateMipmapTextures.erase(texture);
}

namespace {
// SFPEW_MANUAL_MIPGEN: unset/1 = backend call then repair (default);
// 0 = plain backend call; "only" = repair without the backend call, a test
// hook that simulates the stale-level driver so the blit chain has to
// produce every level by itself (levels must already be allocated).
int manualMipmapRepairMode() {
    static const int mode = [] {
        const char* v = getenv("SFPEW_MANUAL_MIPGEN");
        if (v == nullptr) return 1;
        if (v[0] == '0' && v[1] == '\0') return 0;
        if (std::strcmp(v, "only") == 0) return 2;
        return 1;
    }();
    return mode;
}

// The mobile driver was caught leaving the HIGH mip levels of a 2400x1080
// render target STALE across glGenerateMipmap calls - on-device probes of the
// Derivative shader pack showed level 4 tracking the freshly rendered base
// while level 8 still held an image from seconds earlier, on two different GL
// backend implementations over the same GLES driver. The pack's auto-exposure
// averages the scene from level 6 of that chain, so stale-bright levels crush
// the exposure and the screen goes dead dark until the leftover content
// happens to match the view again. A single mipgen call cannot produce
// fresh-4/stale-8 (8 derives from 4), so the driver demonstrably stopped
// partway; rebuild the whole chain ourselves, one LINEAR blit per level.
//
// The backend's glGenerateMipmap has ALREADY run when this is called: it
// allocates any missing levels (a blit cannot) and remains the result for
// any level this repair cannot reach - stopping early is never worse than
// the plain call. Depth/integer chains fail the completeness or blit checks
// on the first level and fall through to that intact result.
// Scratch for the two-hop blits below. Mesa silently DROPS a
// glBlitFramebuffer whose source and destination are levels of the same
// texture (llvmpipe: no GL error, no write), and a conservative mobile
// driver may do the same, so every level is built source level -> scratch
// -> destination level. Sized for the largest destination level and
// reallocated only when the chain's size or internal format changes.
struct mip_scratch_t {
    GLuint tex = 0;
    GLsizei w = 0, h = 0;
    GLint internalformat = 0;
};
thread_local mip_scratch_t mipScratch;

// glTexStorage2D wants a SIZED internal format; legacy chains report the
// unsized names (or the GL 1.x component counts).
GLint sizedInternalFormat(GLint internalformat) {
    switch (internalformat) {
    case GL_RGBA: case 4: return GL_RGBA8;
    case GL_RGB: case 3: return GL_RGB8;
    case GL_RG: return GL_RG8;
    case GL_RED: return GL_R8;
    default: return internalformat;
    }
}

bool ensureMipScratch(GLsizei w, GLsizei h, GLint internalformat) {
    if (g_glFuncs.glTexStorage2D == nullptr || g_glFuncs.glGenTextures == nullptr ||
        g_glFuncs.glDeleteTextures == nullptr || g_glFuncs.glBindTexture == nullptr)
        return false;
    auto& s = mipScratch;
    if (s.tex != 0 && s.w == w && s.h == h && s.internalformat == internalformat) return true;
    if (s.tex != 0) {
        g_glFuncs.glDeleteTextures(1, &s.tex);
        s = {};
    }
    GLint prev = 0;
    g_glFuncs.glGetIntegerv(0x8069 /* GL_TEXTURE_BINDING_2D */, &prev);
    g_glFuncs.glGenTextures(1, &s.tex);
    if (s.tex == 0) return false;
    g_glFuncs.glBindTexture(GL_TEXTURE_2D, s.tex);
    while (g_glFuncs.glGetError() != GL_NO_ERROR) {}
    g_glFuncs.glTexStorage2D(GL_TEXTURE_2D, 1, (GLenum)internalformat, w, h);
    const bool ok = g_glFuncs.glGetError() == GL_NO_ERROR;
    g_glFuncs.glBindTexture(GL_TEXTURE_2D, (GLuint)prev);
    if (!ok) {
        g_glFuncs.glDeleteTextures(1, &s.tex);
        s = {};
        return false;
    }
    s.w = w;
    s.h = h;
    s.internalformat = internalformat;
    return true;
}

void repairMipmapChain2D() {
    if (g_glFuncs.glBlitFramebuffer == nullptr || g_glFuncs.glGenFramebuffers == nullptr ||
        g_glFuncs.glFramebufferTexture2D == nullptr ||
        g_glFuncs.glCheckFramebufferStatus == nullptr || g_glFuncs.glBindFramebuffer == nullptr ||
        g_glFuncs.glGetIntegerv == nullptr || g_glFuncs.glGetError == nullptr ||
        g_glFuncs.glGetTexLevelParameteriv == nullptr)
        return;
    const GLuint texture = sfpewLogicalTextureBinding(GL_TEXTURE_2D);
    if (texture == 0) return;

    GLsizei width = 0, height = 0;
    const auto& texture_sizes = textureMetadata().sizes;
    const auto size_it = texture_sizes.find(texture);
    if (size_it != texture_sizes.end()) {
        width = size_it->second.width;
        height = size_it->second.height;
    } else {
        GLint w = 0, h = 0;
        g_glFuncs.glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w);
        g_glFuncs.glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &h);
        width = w;
        height = h;
    }
    if (width <= 1 && height <= 1) return;

    while (g_glFuncs.glGetError() != GL_NO_ERROR) {}

    GLint internalformat = 0;
    g_glFuncs.glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT,
                                       &internalformat);
    if (internalformat == 0) return;
    if (!ensureMipScratch(std::max<GLsizei>(width >> 1, 1), std::max<GLsizei>(height >> 1, 1),
                          sizedInternalFormat(internalformat)))
        return;

    GLint prev_read = 0, prev_draw = 0;
    g_glFuncs.glGetIntegerv(0x8CAA /* GL_READ_FRAMEBUFFER_BINDING */, &prev_read);
    g_glFuncs.glGetIntegerv(0x8CA6 /* GL_DRAW_FRAMEBUFFER_BINDING */, &prev_draw);
    // Scissor clips glBlitFramebuffer writes; the repair must cover the level.
    const bool scissor =
        g_glFuncs.glIsEnabled != nullptr && g_glFuncs.glIsEnabled(GL_SCISSOR_TEST) == GL_TRUE;
    if (scissor && g_glFuncs.glDisable != nullptr) g_glFuncs.glDisable(GL_SCISSOR_TEST);

    static thread_local GLuint blit_fbos[2] = {0, 0};
    if (blit_fbos[0] == 0) g_glFuncs.glGenFramebuffers(2, blit_fbos);
    if (blit_fbos[0] != 0 && blit_fbos[1] != 0) {
        // Fresh FBOs read from and draw to COLOR_ATTACHMENT0 by default.
        g_glFuncs.glBindFramebuffer(0x8CA8 /* GL_READ_FRAMEBUFFER */, blit_fbos[0]);
        g_glFuncs.glBindFramebuffer(0x8CA9 /* GL_DRAW_FRAMEBUFFER */, blit_fbos[1]);
        GLsizei pw = width > 0 ? width : 1, ph = height > 0 ? height : 1;
        for (GLint level = 1; pw > 1 || ph > 1; ++level) {
            const GLsizei lw = std::max<GLsizei>(width >> level, 1);
            const GLsizei lh = std::max<GLsizei>(height >> level, 1);
            // Hop 1: previous level, downsampled into the scratch corner.
            g_glFuncs.glFramebufferTexture2D(0x8CA8, 0x8CE0 /* GL_COLOR_ATTACHMENT0 */,
                                             GL_TEXTURE_2D, texture, level - 1);
            g_glFuncs.glFramebufferTexture2D(0x8CA9, 0x8CE0, GL_TEXTURE_2D, mipScratch.tex, 0);
            if (g_glFuncs.glCheckFramebufferStatus(0x8CA8) != GL_FRAMEBUFFER_COMPLETE ||
                g_glFuncs.glCheckFramebufferStatus(0x8CA9) != GL_FRAMEBUFFER_COMPLETE)
                break;
            g_glFuncs.glBlitFramebuffer(0, 0, pw, ph, 0, 0, lw, lh, GL_COLOR_BUFFER_BIT,
                                        GL_LINEAR);
            if (g_glFuncs.glGetError() != GL_NO_ERROR) break;
            // Hop 2: scratch corner into the destination level, 1:1.
            g_glFuncs.glFramebufferTexture2D(0x8CA8, 0x8CE0, GL_TEXTURE_2D, mipScratch.tex, 0);
            g_glFuncs.glFramebufferTexture2D(0x8CA9, 0x8CE0, GL_TEXTURE_2D, texture, level);
            if (g_glFuncs.glCheckFramebufferStatus(0x8CA8) != GL_FRAMEBUFFER_COMPLETE ||
                g_glFuncs.glCheckFramebufferStatus(0x8CA9) != GL_FRAMEBUFFER_COMPLETE)
                break;
            g_glFuncs.glBlitFramebuffer(0, 0, lw, lh, 0, 0, lw, lh, GL_COLOR_BUFFER_BIT,
                                        GL_NEAREST);
            if (g_glFuncs.glGetError() != GL_NO_ERROR) break;
            pw = lw;
            ph = lh;
        }
        g_glFuncs.glFramebufferTexture2D(0x8CA8, 0x8CE0, GL_TEXTURE_2D, 0, 0);
        g_glFuncs.glFramebufferTexture2D(0x8CA9, 0x8CE0, GL_TEXTURE_2D, 0, 0);
        g_glFuncs.glBindFramebuffer(0x8CA8, (GLuint)prev_read);
        g_glFuncs.glBindFramebuffer(0x8CA9, (GLuint)prev_draw);
    }
    if (scissor && g_glFuncs.glEnable != nullptr) g_glFuncs.glEnable(GL_SCISSOR_TEST);
    while (g_glFuncs.glGetError() != GL_NO_ERROR) {}
}
} // namespace

void sfpewMaybeGenerateMipmap(GLenum target) {
    if (target == GL_TEXTURE_1D) target = GL_TEXTURE_2D;
    if (target != GL_TEXTURE_2D || g_glFuncs.glGenerateMipmap == nullptr) return;
    const GLuint bound = sfpewLogicalTextureBinding(GL_TEXTURE_2D);
    if (bound != 0 && generateMipmapTextures.count(bound) != 0) {
        g_glFuncs.glGenerateMipmap(GL_TEXTURE_2D);
        if (manualMipmapRepairMode() != 0) repairMipmapChain2D();
    }
}

// GL 3.0 / ES 2.0 core. Not a passthrough: after the backend call the 2D
// chain is rebuilt level by level (see repairMipmapChain2D above). The flush
// keeps any batched immediate-mode drawing ordered before the mipgen reads
// its base level; texture/FBO state is read directly and restored.
void glGenerateMipmap(GLenum target) {
    if (!sfpewEnsureBackend() || g_glFuncs.glGenerateMipmap == nullptr) return;
    sfpewTextureStateBarrier();
    const int mode = manualMipmapRepairMode();
    if (mode != 2) g_glFuncs.glGenerateMipmap(target);
    if (target == GL_TEXTURE_2D && mode != 0) repairMipmapChain2D();
}

GLenum sfpewLogicalActiveTexture() {
    auto& state = getLogicalTextureBindings();
    return getLogicalActiveTexture(state);
}

bool sfpewBackendIsES() {
    // -1 until a context has answered; never cached from a failed query, so an
    // early call cannot pin the wrong answer for the process lifetime.
    static int cached = -1;
    if (cached < 0) {
        const GLubyte* raw =
            g_glFuncs.glGetString != nullptr ? g_glFuncs.glGetString(GL_VERSION) : nullptr;
        if (raw == nullptr) return false;
        cached = std::strstr((const char*)raw, "OpenGL ES") != nullptr ? 1 : 0;
    }
    return cached != 0;
}

// Whether the backend accepts GL_BGRA pixel and vertex formats natively.
// GLES never does (the policy this wrapper is built around). A DESKTOP
// version string normally means it does - but not when the "desktop GL" is
// itself a translation layer that forwards to GLES: MobileGlues reports a
// desktop version yet rewrites GL_BGRA to GL_RGBA without reordering the
// bytes, which shipped exactly that red/blue swap on device. Its version
// string carries "MobileGlues", so it is recognized and treated like GLES.
//
// The conservative direction is CONVERT: RGBA bytes are correct for every
// backend, native BGRA only for some. An unanswerable query (no current
// context yet) therefore reports "does not take BGRA" and is not cached.
namespace {
// MobileGlues has no glMultiDrawArrays at all up to and including V1.3.5 -
// GLES has no such entry point to forward to and that release does not
// emulate it, so the call silently draws nothing. Its indexed relatives
// (glMultiDrawElements, ...BaseVertex, ...Indirect) ARE implemented, and
// every other backend this runs on has the whole family.
//
// The version is not in any string the API exposes, but MobileGlues injects
// it into every shader it compiles as MG_MOBILEGLUES_VERSION (1350 = V1.3.5;
// see its README, "For Shader Developers"). So the probe is a shader: write
// the macro to a transform-feedback varying, run one point with the
// rasterizer off, and read the number back. A backend that is not
// MobileGlues never defines the macro and reports zero - but it is only
// asked at all once the version string has already named MobileGlues.
constexpr int kMobileGluesMultiDrawArraysFixedIn = 1360; // first release with it

int probeMobileGluesVersion() {
    auto& f = g_glFuncs;
    if (f.glCreateShader == nullptr || f.glShaderSource == nullptr ||
        f.glCompileShader == nullptr || f.glCreateProgram == nullptr ||
        f.glAttachShader == nullptr || f.glLinkProgram == nullptr ||
        f.glGetProgramiv == nullptr || f.glTransformFeedbackVaryings == nullptr ||
        f.glBeginTransformFeedback == nullptr || f.glEndTransformFeedback == nullptr ||
        f.glBindBufferBase == nullptr || f.glGenBuffers == nullptr ||
        f.glBufferData == nullptr || f.glMapBufferRange == nullptr ||
        f.glUnmapBuffer == nullptr || f.glDrawArrays == nullptr || f.glEnable == nullptr ||
        f.glDisable == nullptr || f.glDeleteShader == nullptr || f.glDeleteProgram == nullptr ||
        f.glDeleteBuffers == nullptr || f.glGetIntegerv == nullptr ||
        f.glGenVertexArrays == nullptr || f.glBindVertexArray == nullptr ||
        f.glDeleteVertexArrays == nullptr) {
        return -1;
    }

    static const char* source =
        "#version 300 es\n"
        "#ifdef MG_MOBILEGLUES_VERSION\n"
        "#define MG_PROBE float(MG_MOBILEGLUES_VERSION)\n"
        "#else\n"
        "#define MG_PROBE 0.0\n"
        "#endif\n"
        "flat out float mgVersion;\n"
        "void main() {\n"
        "    mgVersion = MG_PROBE;\n"
        "    gl_Position = vec4(0.0, 0.0, 0.0, 1.0);\n"
        "    gl_PointSize = 1.0;\n"
        "}\n";

    // Everything this touches is put back before returning.
    GLint savedProgram = 0, savedVao = 0, savedArrayBuffer = 0, savedTfBuffer = 0;
    f.glGetIntegerv(GL_CURRENT_PROGRAM, &savedProgram);
    f.glGetIntegerv(0x85B5 /* GL_VERTEX_ARRAY_BINDING */, &savedVao);
    f.glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &savedArrayBuffer);
    f.glGetIntegerv(0x8C8F /* GL_TRANSFORM_FEEDBACK_BUFFER_BINDING */, &savedTfBuffer);
    const bool savedDiscard =
        f.glIsEnabled != nullptr && f.glIsEnabled(0x8C89 /* GL_RASTERIZER_DISCARD */) == GL_TRUE;
    while (f.glGetError() != GL_NO_ERROR) {}

    int version = -1;
    GLuint shader = f.glCreateShader(GL_VERTEX_SHADER);
    GLuint program = f.glCreateProgram();
    GLuint buffer = 0, vao = 0;
    f.glGenBuffers(1, &buffer);
    f.glGenVertexArrays(1, &vao);
    if (shader != 0 && program != 0 && buffer != 0 && vao != 0) {
        f.glShaderSource(shader, 1, &source, nullptr);
        f.glCompileShader(shader);
        f.glAttachShader(program, shader);
        static const char* varying = "mgVersion";
        f.glTransformFeedbackVaryings(program, 1, &varying, 0x8C8C /* INTERLEAVED_ATTRIBS */);
        f.glLinkProgram(program);
        GLint linked = GL_FALSE;
        f.glGetProgramiv(program, GL_LINK_STATUS, &linked);
        if (linked == GL_TRUE) {
            f.glBindVertexArray(vao);
            f.glBindBuffer(0x8C8E /* GL_TRANSFORM_FEEDBACK_BUFFER */, buffer);
            f.glBufferData(0x8C8E, (GLsizeiptr)sizeof(GLfloat), nullptr, 0x88E1 /* STREAM_READ */);
            f.glBindBufferBase(0x8C8E, 0, buffer);
            f.glUseProgram(program);
            f.glEnable(0x8C89 /* GL_RASTERIZER_DISCARD */);
            f.glBeginTransformFeedback(GL_POINTS);
            f.glDrawArrays(GL_POINTS, 0, 1);
            f.glEndTransformFeedback();
            f.glDisable(0x8C89);
            if (f.glGetError() == GL_NO_ERROR) {
                void* mapped = f.glMapBufferRange(0x8C8E, 0, (GLsizeiptr)sizeof(GLfloat),
                                                  GL_MAP_READ_BIT);
                if (mapped != nullptr) {
                    GLfloat value = 0.0f;
                    std::memcpy(&value, mapped, sizeof value);
                    f.glUnmapBuffer(0x8C8E);
                    version = (int)(value + 0.5f);
                }
            }
            f.glBindBufferBase(0x8C8E, 0, 0);
        }
    }

    if (buffer != 0) f.glDeleteBuffers(1, &buffer);
    if (program != 0) f.glDeleteProgram(program);
    if (shader != 0) f.glDeleteShader(shader);
    f.glBindVertexArray((GLuint)savedVao);
    if (vao != 0) f.glDeleteVertexArrays(1, &vao);
    f.glBindBuffer(0x8C8E, (GLuint)savedTfBuffer);
    f.glBindBuffer(GL_ARRAY_BUFFER, (GLuint)savedArrayBuffer);
    f.glUseProgram((GLuint)savedProgram);
    if (savedDiscard) f.glEnable(0x8C89); else f.glDisable(0x8C89);
    sfpewInvalidateImmediateDrawState();
    while (f.glGetError() != GL_NO_ERROR) {}
    return version;
}
} // namespace

// True when the backend cannot be asked to draw with glMultiDrawArrays.
bool sfpewBackendLacksMultiDrawArrays() {
    static int cached = -1;
    if (cached < 0) {
        // Lets the indexed path be exercised on a backend that has the array
        // form, which is the only way to test it away from the device.
        const char* forced = getenv("SFPEW_NO_MULTIDRAWARRAYS");
        if (forced != nullptr && forced[0] != '\0' && !(forced[0] == '0' && forced[1] == '\0')) {
            cached = 1;
            SFPEW_LOGI("glMultiDrawArrays avoided by SFPEW_NO_MULTIDRAWARRAYS");
            return true;
        }
        const GLubyte* raw =
            g_glFuncs.glGetString != nullptr ? g_glFuncs.glGetString(GL_VERSION) : nullptr;
        if (raw == nullptr) return true; // unknown backend: take the safe path, do not cache
        const char* version = (const char*)raw;
        if (std::strstr(version, "MobileGlues") == nullptr) {
            cached = 0;
        } else {
            const int probed = probeMobileGluesVersion();
            // A probe that could not run leaves the version unknown, and the
            // releases known to lack the entry point are the ones in the
            // field: assume it is missing rather than draw nothing.
            cached = (probed >= kMobileGluesMultiDrawArraysFixedIn) ? 0 : 1;
            SFPEW_LOGI("backend \"%s\": MG_MOBILEGLUES_VERSION probe returned %d; "
                       "glMultiDrawArrays %s",
                       version, probed, cached != 0 ? "avoided" : "used");
        }
    }
    return cached != 0;
}

bool sfpewBackendTakesBgra() {
    static int cached = -1;
    if (cached < 0) {
        const GLubyte* raw =
            g_glFuncs.glGetString != nullptr ? g_glFuncs.glGetString(GL_VERSION) : nullptr;
        if (raw == nullptr) return false;
        const char* version = (const char*)raw;
        const bool es = std::strstr(version, "OpenGL ES") != nullptr;
        const bool translator = std::strstr(version, "MobileGlues") != nullptr;
        cached = (!es && !translator) ? 1 : 0;
        // Once, with the evidence: which backend this is and what was
        // decided for it. The device round that motivated this feature
        // would have been one log line instead of an rdc excavation.
        SFPEW_LOGI("backend \"%s\": BGRA uploads %s", version,
                   cached != 0 ? "passed through natively" : "converted to RGBA");
    }
    return cached != 0;
}

uint64_t sfpewTextureStateGeneration() { return getLogicalTextureBindings().generation; }

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

GLuint sfpewLogicalTextureBindingForUnit(GLenum unit, GLenum target) {
    auto& state = getLogicalTextureBindings();
    const GLenum query = textureBindingQuery(target);
    if (query == GL_NONE) return 0;

    const auto binding = state.bindings.find(textureBindingKey(unit, target));
    return binding == state.bindings.end() ? 0 : binding->second;
}

namespace {

bool validTextureParameterTarget(GLenum target) {
    if (target == GL_TEXTURE_1D) return true;
    return textureBindingQuery(target) != GL_NONE;
}

GLenum textureParameterTarget(GLenum target) {
    return target == GL_TEXTURE_1D ? GL_TEXTURE_2D : target;
}

GLclampf texturePriority(GLuint texture) {
    const auto& priorities = g_glstate_c.texture_priorities;
    const auto it = priorities.find(texture);
    return it == priorities.end() ? 1.0f : it->second;
}

GLenum depthTextureMode(GLuint texture) {
    const auto& modes = g_glstate_c.texture_depth_mode;
    const auto it = modes.find(texture);
    return it == modes.end() ? GL_LUMINANCE : it->second;
}

} // namespace

void glGetTexParameterfv(GLenum target, GLenum pname, GLfloat* params) {
    auto& gs = g_glstate;
    if (params == nullptr) return;
    if (pname == GL_TEXTURE_PRIORITY || pname == GL_TEXTURE_RESIDENT || pname == GL_DEPTH_TEXTURE_MODE) {
        if (!validTextureParameterTarget(target)) {
            gs.set_error(GL_INVALID_ENUM);
            return;
        }
        sfpewEntryBarrier();
        if (pname == GL_TEXTURE_PRIORITY)
            params[0] = texturePriority(sfpewLogicalTextureBinding(textureParameterTarget(target)));
        else if (pname == GL_DEPTH_TEXTURE_MODE)
            params[0] = static_cast<GLfloat>(depthTextureMode(sfpewLogicalTextureBinding(textureParameterTarget(target))));
        else
            params[0] = 1.0f; // all live and default textures are reported resident
        return;
    }
    if (!sfpewEnsureBackend() || g_glFuncs.glGetTexParameterfv == nullptr) return;
    sfpewEntryBarrier();
    g_glFuncs.glGetTexParameterfv(textureParameterTarget(target), pname, params);
}

void glGetTexParameteriv(GLenum target, GLenum pname, GLint* params) {
    auto& gs = g_glstate;
    if (params == nullptr) return;
    if (pname == GL_TEXTURE_PRIORITY || pname == GL_TEXTURE_RESIDENT || pname == GL_DEPTH_TEXTURE_MODE) {
        if (!validTextureParameterTarget(target)) {
            gs.set_error(GL_INVALID_ENUM);
            return;
        }
        sfpewEntryBarrier();
        if (pname == GL_TEXTURE_PRIORITY) {
            const GLfloat value = texturePriority(sfpewLogicalTextureBinding(textureParameterTarget(target)));
            *params = static_cast<GLint>(std::lround(value));
        } else if (pname == GL_DEPTH_TEXTURE_MODE) {
            *params = static_cast<GLint>(depthTextureMode(sfpewLogicalTextureBinding(textureParameterTarget(target))));
        } else {
            *params = GL_TRUE;
        }
        return;
    }
    if (!sfpewEnsureBackend() || g_glFuncs.glGetTexParameteriv == nullptr) return;
    sfpewEntryBarrier();
    g_glFuncs.glGetTexParameteriv(textureParameterTarget(target), pname, params);
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
    "GL_ARB_transpose_matrix",
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
    "GL_ARB_imaging",
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
    "GL_EXT_color_table",
    "GL_EXT_color_subtable",
    "GL_EXT_convolution",
    "GL_EXT_histogram",
    "GL_EXT_texture_lod_bias",
    "GL_SGI_color_table",
    "GL_SGI_color_matrix",
    "GL_SGIS_generate_mipmap",
};
constexpr GLint kDesktopExtensionCount =
    (GLint)(sizeof(kDesktopExtensions) / sizeof(kDesktopExtensions[0]));

namespace {

// pnames the wrapper answers itself through glGetIntegerv (scalar integers).
// The fixed-function state is NOT here - fixedFunctionState() below answers
// that for all four getters at once; this is what is left: context and
// binding state that only makes sense as an integer.
bool isWrapperIntegerPname(GLenum pname) {
    switch (pname) {
    case GL_CONTEXT_PROFILE_MASK:
    case GL_CONTEXT_FLAGS:
    case GL_MAJOR_VERSION:
    case GL_MINOR_VERSION:
    case GL_NUM_EXTENSIONS:
    case GL_CURRENT_PROGRAM:
    case GL_ARRAY_BUFFER_BINDING:
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
    auto& gs = g_glstate;
    if (light < GL_LIGHT0 || light >= GL_LIGHT0 + MAX_LIGHTS) {
        gs.set_error(GL_INVALID_ENUM);
        return;
    }
    const auto& l = gs.fpe_uniform.lights[light - GL_LIGHT0];
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
        gs.set_error(GL_INVALID_ENUM);
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
    auto& gs = g_glstate;
    // GL_FRONT_AND_BACK is valid for setting but not for querying.
    if (face != GL_FRONT && face != GL_BACK) {
        gs.set_error(GL_INVALID_ENUM);
        return;
    }
    const auto& material = gs.fpe_uniform.materials[face == GL_FRONT ? 0 : 1];
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
        gs.set_error(GL_INVALID_ENUM);
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
    auto& gs = g_glstate;
    const int unit = std::clamp(static_cast<int>(sfpewLogicalActiveTexture() - GL_TEXTURE0), 0, MAX_TEX - 1);
    // defects-plan-2.md 2.2.
    if (target == GL_POINT_SPRITE && pname == GL_COORD_REPLACE) {
        params[0] = gs.fpe_state.fpe_bools.point_sprite_coord_replace[unit] ? 1.0f : 0.0f;
        return;
    }
    if (target != GL_TEXTURE_ENV) {
        gs.set_error(GL_INVALID_ENUM);
        return;
    }
    const auto& env = gs.fpe_uniform.texture_env[unit];
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
        gs.set_error(GL_INVALID_ENUM);
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
    auto& gs = g_glstate;
    if (target != GL_TEXTURE_1D && target != GL_PROXY_TEXTURE_1D) {
        gs.set_error(GL_INVALID_ENUM);
        return;
    }
    if (target == GL_PROXY_TEXTURE_1D) return; // no proxy bookkeeping for 1D
    glTexImage2D(GL_TEXTURE_2D, level, internalformat, width, 1, border, format, type, pixels);
}

void glTexSubImage1D(GLenum target, GLint level, GLint xoffset, GLsizei width, GLenum format,
                     GLenum type, const GLvoid* pixels) {
    auto& gs = g_glstate;
    if (target != GL_TEXTURE_1D) {
        gs.set_error(GL_INVALID_ENUM);
        return;
    }
    glTexSubImage2D(GL_TEXTURE_2D, level, xoffset, 0, width, 1, format, type, pixels);
}

namespace {

bool isGenericCompressedFormat(GLenum format) {
    switch (format) {
    case GL_COMPRESSED_ALPHA:
    case GL_COMPRESSED_LUMINANCE:
    case GL_COMPRESSED_LUMINANCE_ALPHA:
    case GL_COMPRESSED_INTENSITY:
    case GL_COMPRESSED_RGB:
    case GL_COMPRESSED_RGBA:
    case 0x8C48: // GL_COMPRESSED_SRGB
    case 0x8C49: // GL_COMPRESSED_SRGB_ALPHA
    case 0x8C4A: // GL_COMPRESSED_SLUMINANCE
    case 0x8C4B: // GL_COMPRESSED_SLUMINANCE_ALPHA
        return true;
    default:
        return false;
    }
}

bool isBlockCompressedFormat(GLenum format) {
    // Every format guaranteed by ES 3.0, plus the formats commonly exposed
    // by desktop drivers, uses a block at least four texels high. Keep the
    // range tests explicit so newly added formats do not accidentally become
    // a driver call with a one-row payload.
    if ((format >= 0x9270 && format <= 0x9279) || // ETC2 / EAC
        (format >= 0x83F0 && format <= 0x83F3) || // S3TC / DXT
        (format >= 0x8C4C && format <= 0x8C4F) || // sRGB S3TC
        (format >= 0x8C70 && format <= 0x8C73) || // LATC
        (format >= 0x8DBB && format <= 0x8DBE) || // RGTC
        (format >= 0x8E8C && format <= 0x8E8F) || // BPTC
        (format >= 0x93B0 && format <= 0x93BD) || // linear ASTC
        (format >= 0x93D0 && format <= 0x93DD) || // sRGB ASTC
        format == 0x86B0 || format == 0x86B1 ||   // FXT1
        format == 0x8D64) {                       // ETC1
        return true;
    }
    // Unknown compressed formats are conservatively block-compressed. A
    // guessed layout can make a driver read beyond the supplied payload.
    return true;
}

} // namespace

void glCopyTexImage1D(GLenum target, GLint level, GLenum internalformat, GLint x, GLint y,
                      GLsizei width, GLint border) {
    auto& gs = g_glstate;
    if (target != GL_TEXTURE_1D) {
        gs.set_error(GL_INVALID_ENUM);
        return;
    }
    if (border != 0 || level < 0 || width < 0) {
        gs.set_error(GL_INVALID_VALUE);
        return;
    }
    LIST_RECORD(glCopyTexImage1D, {}, target, level, internalformat, x, y, width, border)
    SELF_CALL(glCopyTexImage2D, GL_TEXTURE_2D, level, internalformat, x, y, width, 1, border)
}

void glCopyTexSubImage1D(GLenum target, GLint level, GLint xoffset, GLint x, GLint y,
                         GLsizei width) {
    auto& gs = g_glstate;
    if (target != GL_TEXTURE_1D) {
        gs.set_error(GL_INVALID_ENUM);
        return;
    }
    if (level < 0 || xoffset < 0 || width < 0) {
        gs.set_error(GL_INVALID_VALUE);
        return;
    }
    LIST_RECORD(glCopyTexSubImage1D, {}, target, level, xoffset, x, y, width)
    SELF_CALL(glCopyTexSubImage2D, GL_TEXTURE_2D, level, xoffset, 0, x, y, width, 1)
}

void glCopyTexImage2D(GLenum target, GLint level, GLenum internalformat, GLint x, GLint y,
                      GLsizei width, GLsizei height, GLint border) {
    if (level < 0 || width < 0 || height < 0 || border != 0) {
        g_glstate.set_error(GL_INVALID_VALUE);
        return;
    }
    LIST_RECORD(glCopyTexImage2D, {}, target, level, internalformat, x, y, width, height, border)
    sfpewEntryBarrier();
    if (!sfpewEnsureBackend() || g_glFuncs.glCopyTexImage2D == nullptr) return;
    if (sfpewImagingActive()) {
        std::vector<GLfloat> rgba;
        GLsizei output_width = width, output_height = height;
        if (!sfpewImagingReadRgba(x, y, width, height, &rgba, &output_width, &output_height))
            return;
        if (!sfpewImagingSink() && rgba.empty() && width != 0 && height != 0) return;
        sfpew_imaging_upload_t tight;
        sfpewBeginTightImagingUpload(&tight);
        struct guard_t {
            sfpew_imaging_upload_t& tight;
            guard_t(sfpew_imaging_upload_t& value) : tight(value) { sfpewPushImagingBypass(); }
            ~guard_t() {
                sfpewPopImagingBypass();
                sfpewFinishImagingUpload(tight);
            }
        } guard(tight);
        if (sfpewImagingSink()) {
            SELF_CALL(glTexImage2D, target, level, internalformat, output_width, output_height,
                      border, GL_RGBA, GL_FLOAT, nullptr)
        } else {
            SELF_CALL(glTexImage2D, target, level, internalformat, output_width, output_height,
                      border, GL_RGBA, GL_FLOAT, rgba.data())
        }
        return;
    }
    g_glFuncs.glCopyTexImage2D(target, level, internalformat, x, y, width, height, border);
    // defects-plan-2.md 2.7: glCopyTexImage2D can target a cube face too.
    if (target == GL_TEXTURE_2D ||
        (target >= GL_TEXTURE_CUBE_MAP_POSITIVE_X && target <= GL_TEXTURE_CUBE_MAP_NEGATIVE_Z)) {
        rememberTextureLevel(sfpewLogicalTextureBinding(textureMetadataTarget(target)), target, level,
                             width, height, internalformat, border, false, 0);
    }
    sfpewMaybeGenerateMipmap(target);
}

void glCopyTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y,
                         GLsizei width, GLsizei height) {
    if (level < 0 || width < 0 || height < 0) {
        g_glstate.set_error(GL_INVALID_VALUE);
        return;
    }
    LIST_RECORD(glCopyTexSubImage2D, {}, target, level, xoffset, yoffset, x, y, width, height)
    sfpewEntryBarrier();
    if (!sfpewEnsureBackend() || g_glFuncs.glCopyTexSubImage2D == nullptr) return;
    if (sfpewImagingActive()) {
        std::vector<GLfloat> rgba;
        GLsizei output_width = width, output_height = height;
        if (!sfpewImagingReadRgba(x, y, width, height, &rgba, &output_width, &output_height))
            return;
        if (sfpewImagingSink() || (rgba.empty() && width != 0 && height != 0)) return;
        sfpew_imaging_upload_t tight;
        sfpewBeginTightImagingUpload(&tight);
        struct guard_t {
            sfpew_imaging_upload_t& tight;
            guard_t(sfpew_imaging_upload_t& value) : tight(value) { sfpewPushImagingBypass(); }
            ~guard_t() {
                sfpewPopImagingBypass();
                sfpewFinishImagingUpload(tight);
            }
        } guard(tight);
        SELF_CALL(glTexSubImage2D, target, level, xoffset, yoffset, output_width, output_height,
                  GL_RGBA, GL_FLOAT, rgba.data())
        return;
    }
    g_glFuncs.glCopyTexSubImage2D(target, level, xoffset, yoffset, x, y, width, height);
    sfpewMaybeGenerateMipmap(target);
}

void glCompressedTexImage1D(GLenum target, GLint level, GLenum internalformat, GLsizei width,
                            GLint border, GLsizei imageSize, const GLvoid* data) {
    auto& gs = g_glstate;
    if (target != GL_TEXTURE_1D && target != GL_PROXY_TEXTURE_1D) {
        gs.set_error(GL_INVALID_ENUM);
        return;
    }
    if (border != 0 || imageSize < 0 || level < 0 || width < 0) {
        gs.set_error(GL_INVALID_VALUE);
        return;
    }
    if (isGenericCompressedFormat(internalformat)) {
        gs.set_error(GL_INVALID_ENUM);
        return;
    }
    LIST_RECORD(glCompressedTexImage1D,
                {{6, static_cast<size_t>(imageSize > 0 ? imageSize : 0)}}, target, level,
                internalformat, width, border, imageSize, data)
    sfpewEntryBarrier();
    if (target == GL_PROXY_TEXTURE_1D) return;

    // A 1D image is represented by a one-row 2D image here. No block format
    // exposed by either backend floor can represent that height.
    if (isBlockCompressedFormat(internalformat)) {
        gs.set_error(GL_INVALID_OPERATION);
        return;
    }
    if (!sfpewEnsureBackend() || g_glFuncs.glCompressedTexImage2D == nullptr) return;
    g_glFuncs.glCompressedTexImage2D(GL_TEXTURE_2D, level, internalformat, width, 1, border,
                                     imageSize, data);
    rememberTextureLevel(sfpewLogicalTextureBinding(GL_TEXTURE_2D), GL_TEXTURE_2D, level, width, 1,
                         static_cast<GLint>(internalformat), border, true, imageSize);
    sfpewMaybeGenerateMipmap(GL_TEXTURE_2D);
}

void glCompressedTexSubImage1D(GLenum target, GLint level, GLint xoffset, GLsizei width,
                               GLenum format, GLsizei imageSize, const GLvoid* data) {
    auto& gs = g_glstate;
    if (target != GL_TEXTURE_1D) {
        gs.set_error(GL_INVALID_ENUM);
        return;
    }
    if (imageSize < 0 || level < 0 || xoffset < 0 || width < 0) {
        gs.set_error(GL_INVALID_VALUE);
        return;
    }
    if (isGenericCompressedFormat(format)) {
        gs.set_error(GL_INVALID_ENUM);
        return;
    }
    LIST_RECORD(glCompressedTexSubImage1D,
                {{6, static_cast<size_t>(imageSize > 0 ? imageSize : 0)}}, target, level,
                xoffset, width, format, imageSize, data)
    sfpewEntryBarrier();
    if (isBlockCompressedFormat(format)) {
        gs.set_error(GL_INVALID_OPERATION);
        return;
    }
    if (!sfpewEnsureBackend() || g_glFuncs.glCompressedTexSubImage2D == nullptr) return;
    g_glFuncs.glCompressedTexSubImage2D(GL_TEXTURE_2D, level, xoffset, 0, width, 1, format,
                                        imageSize, data);
    sfpewMaybeGenerateMipmap(GL_TEXTURE_2D);
}

void glGetCompressedTexImage(GLenum target, GLint level, GLvoid* pixels) {
    auto& gs = g_glstate;
    if (pixels == nullptr) return;
    switch (target) {
    case GL_TEXTURE_1D:
    case GL_TEXTURE_2D:
    case GL_TEXTURE_3D:
    case GL_TEXTURE_CUBE_MAP_POSITIVE_X:
    case GL_TEXTURE_CUBE_MAP_NEGATIVE_X:
    case GL_TEXTURE_CUBE_MAP_POSITIVE_Y:
    case GL_TEXTURE_CUBE_MAP_NEGATIVE_Y:
    case GL_TEXTURE_CUBE_MAP_POSITIVE_Z:
    case GL_TEXTURE_CUBE_MAP_NEGATIVE_Z:
        break;
    default:
        gs.set_error(GL_INVALID_ENUM);
        return;
    }
    if (level < 0) {
        gs.set_error(GL_INVALID_VALUE);
        return;
    }
    sfpewEntryBarrier();
    if (!sfpewEnsureBackend()) return;

    // GLES 3.0 has no texture readback. Retaining every compressed payload on
    // the CPU would double the footprint of the largest texture allocations,
    // so GLES fails loudly while desktop GL forwards exactly.
    if (!sfpewBackendIsES() && g_glFuncs.glGetCompressedTexImage != nullptr) {
        const GLenum backend_target = target == GL_TEXTURE_1D ? GL_TEXTURE_2D : target;
        g_glFuncs.glGetCompressedTexImage(backend_target, level, pixels);
        return;
    }
    gs.set_error(GL_INVALID_OPERATION);
}

void glGetTexGenfv(GLenum coord, GLenum pname, GLfloat* params) {
    if (!params) return;
    auto& gs = g_glstate;
    const int c = texgenCoordIndex(coord);
    if (c < 0) {
        gs.set_error(GL_INVALID_ENUM);
        return;
    }
    const int unit = std::clamp(static_cast<int>(sfpewLogicalActiveTexture() - GL_TEXTURE0), 0, MAX_TEX - 1);
    switch (pname) {
    case GL_TEXTURE_GEN_MODE:
        params[0] = static_cast<GLfloat>(gs.fpe_state.texture_gen_mode[unit][c]);
        break;
    case GL_OBJECT_PLANE:
        memcpy(params, glm::value_ptr(gs.fpe_uniform.texgen_object_plane[unit][c]), 4 * sizeof(GLfloat));
        break;
    case GL_EYE_PLANE:
        memcpy(params, glm::value_ptr(gs.fpe_uniform.texgen_eye_plane[unit][c]), 4 * sizeof(GLfloat));
        break;
    default:
        gs.set_error(GL_INVALID_ENUM);
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

namespace {

// ---- GL 2.1 fixed-function state --------------------------------------
// Around 250 state variables exist in GL 2.1 that neither an ES 3.0+ nor a
// GL 3.2+ core backend can answer, because they belong to the pipeline this
// wrapper emulates. Passing them through returned GL_INVALID_ENUM and left
// the caller's buffer untouched - silently, since a query that writes
// nothing looks exactly like one that wrote a zero. Sodium's fog occlusion
// found that the hard way: it reads GL_FOG_MODE, then the matching fog
// distance, and culls chunks on the CPU against the result, so an unanswered
// query became a cutoff of zero and the world stopped drawing.
//
// So they are answered here, in one table, which is also what makes the four
// glGet entry points and glIsEnabled agree by construction rather than by
// four switches staying in sync.
//
// Values leave as doubles because that is the widest state type, and the
// conversion runs one way from there (GL 2.1 section 6.1.2): booleans are
// 0 or 1, enums keep their numeric value, and colours and normals are the
// only values an integer query rescales instead of rounding - which is what
// `colour` reports.
enum array_field_t { kArrayEnabled, kArraySize, kArrayType, kArrayStride, kArrayBuffer };

// GL_RED_BITS and its family: ES 3 still answers them, GL 3.1 core removed
// them, so neither backend can be asked directly without breaking on the
// other. The framebuffer knows, and both backends answer THAT - which is
// also the more correct answer, since the depth of what is being drawn into
// is a property of the bound framebuffer, not of the context.
bool framebufferBits(GLenum pname, GLdouble* out) {
    GLenum size_pname = 0;
    int attachment_kind = 0; // 0 colour, 1 depth, 2 stencil
    switch (pname) {
    case GL_RED_BITS: size_pname = GL_FRAMEBUFFER_ATTACHMENT_RED_SIZE; break;
    case GL_GREEN_BITS: size_pname = GL_FRAMEBUFFER_ATTACHMENT_GREEN_SIZE; break;
    case GL_BLUE_BITS: size_pname = GL_FRAMEBUFFER_ATTACHMENT_BLUE_SIZE; break;
    case GL_ALPHA_BITS: size_pname = GL_FRAMEBUFFER_ATTACHMENT_ALPHA_SIZE; break;
    case GL_DEPTH_BITS:
        size_pname = GL_FRAMEBUFFER_ATTACHMENT_DEPTH_SIZE;
        attachment_kind = 1;
        break;
    case GL_STENCIL_BITS:
        size_pname = GL_FRAMEBUFFER_ATTACHMENT_STENCIL_SIZE;
        attachment_kind = 2;
        break;
    default:
        return false;
    }

    *out = 0;
    if (!sfpewEnsureBackend() || g_glFuncs.glGetFramebufferAttachmentParameteriv == nullptr ||
        g_glFuncs.glGetIntegerv == nullptr) {
        return true;
    }

    GLint framebuffer = 0;
    g_glFuncs.glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &framebuffer);
    GLenum attachment;
    if (framebuffer == 0) {
        // The default framebuffer names its colour buffer differently on the
        // two backends; depth and stencil agree.
        int major = 0, minor = 0;
        const bool es_backend = sfpewDesktopGLVersion(&major, &minor);
        attachment = attachment_kind == 1   ? GL_DEPTH
                     : attachment_kind == 2 ? GL_STENCIL
                     : es_backend           ? GL_BACK
                                            : GL_BACK_LEFT;
    } else {
        attachment = attachment_kind == 1   ? GL_DEPTH_ATTACHMENT
                     : attachment_kind == 2 ? GL_STENCIL_ATTACHMENT
                                            : GL_COLOR_ATTACHMENT0;
    }

    // Asking a size of an attachment that holds nothing is an error, so ask
    // what is there first. Nothing attached means nothing to be deep.
    GLint object_type = GL_NONE;
    g_glFuncs.glGetFramebufferAttachmentParameteriv(
        GL_DRAW_FRAMEBUFFER, attachment, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &object_type);
    if (object_type == GL_NONE) return true;

    GLint bits = 0;
    g_glFuncs.glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER, attachment, size_pname,
                                                    &bits);
    *out = bits;
    return true;
}

// `enable` marks the subset glIsEnabled may answer for: a capability, not
// just any state that happens to be 0 or 1. Without it glIsEnabled(GL_FOG_MODE)
// would return a value where the spec wants GL_INVALID_ENUM.
bool fixedFunctionState(GLenum pname, GLdouble* values, int* count, bool* colour, bool* enable) {
    auto& gs = g_glstate;
    const auto& ff = gs.fpe_state;
    const auto& bools = ff.fpe_bools;
    const auto& uni = gs.fpe_uniform;
    const auto& current = ff.fpe_draw.current_data;
    const auto& arrays = ff.vertexpointer_array;
    const int unit =
        std::clamp(static_cast<int>(sfpewLogicalActiveTexture() - GL_TEXTURE0), 0, MAX_TEX - 1);

    *count = 1;
    *colour = false;
    *enable = false;

    const auto scalar = [&](GLdouble value) {
        values[0] = value;
        return true;
    };
    // A capability: same value, but also legal to ask glIsEnabled about.
    const auto capability = [&](bool on) {
        *enable = true;
        values[0] = on ? 1 : 0;
        return true;
    };
    const auto vector = [&](const GLfloat* source, int n) {
        for (int i = 0; i < n; ++i) values[i] = source[i];
        *count = n;
        return true;
    };
    // Colours and normals: same values, but an integer query maps them onto
    // the full integer range rather than rounding them to 0 or 1.
    const auto signal = [&](const GLfloat* source, int n) {
        *colour = true;
        return vector(source, n);
    };
    const auto matrix = [&](const glm::mat4& m, bool transpose) {
        const GLfloat* source = glm::value_ptr(m);
        for (int i = 0; i < 16; ++i) values[i] = transpose ? source[(i % 4) * 4 + i / 4] : source[i];
        *count = 16;
        return true;
    };
    // One accessor for every client array: which array is named by the
    // GL_*_ARRAY enum the query family belongs to. An array that was never
    // specified reports the spec's initial values rather than the contents
    // of a slot nobody filled.
    const auto array = [&](GLenum which, array_field_t field) -> GLdouble {
        const int slot = vp2idx(which);
        if (slot < 0) return 0;
        const auto& attr = arrays.attributes[slot];
        const bool specified = attr.type != 0;
        switch (field) {
        case kArrayEnabled:
            return (arrays.enabled_pointers & vp_mask(which)) != 0 ? 1 : 0;
        case kArraySize:
            if (specified) return attr.size;
            return which == GL_SECONDARY_COLOR_ARRAY ? 3 : 4;
        case kArrayType:
            return specified ? attr.type : GL_FLOAT;
        case kArrayStride:
            return specified ? attr.stride : 0;
        case kArrayBuffer:
            return ff.client_array_buffer_bindings[slot];
        }
        return 0;
    };

    // The evaluator maps and their grid live in evaluators.cpp. Everything
    // it answers except the grid is a capability.
    if (sfpewEvaluatorStateQuery(pname, values, count)) {
        *enable = pname != GL_MAP1_GRID_DOMAIN && pname != GL_MAP1_GRID_SEGMENTS &&
                  pname != GL_MAP2_GRID_DOMAIN && pname != GL_MAP2_GRID_SEGMENTS;
        return true;
    }

    // Lights and clip planes are ranges, not single enums.
    if (pname >= GL_LIGHT0 && pname < GL_LIGHT0 + MAX_LIGHTS)
        return capability(bools.light_enable[pname - GL_LIGHT0]);
    if (pname >= GL_CLIP_PLANE0 && pname < GL_CLIP_PLANE0 + 6)
        return capability(bools.clip_plane_enable[pname - GL_CLIP_PLANE0]);

    switch (pname) {
    // ---- enables the wrapper implements ----
    case GL_FOG: return capability(bools.fog_enable);
    case GL_LIGHTING: return capability(bools.lighting_enable);
    case GL_ALPHA_TEST: return capability(bools.alpha_test_enable);
    case GL_COLOR_MATERIAL: return capability(bools.color_material_enable);
    case GL_NORMALIZE: return capability(bools.normalize_enable);
    case GL_RESCALE_NORMAL: return capability(bools.rescale_normal_enable);
    case GL_POLYGON_STIPPLE: return capability(bools.polygon_stipple_enable);
    case GL_LINE_STIPPLE: return capability(bools.line_stipple_enable);
    case GL_TEXTURE_2D: return capability(bools.texture_2d_enable[unit]);
    // 1D textures are carried on 2D storage here, so they share the enable.
    case GL_TEXTURE_1D: return capability(bools.texture_2d_enable[unit]);
    case GL_TEXTURE_GEN_S: return capability(bools.texture_gen_enable[unit][0]);
    case GL_TEXTURE_GEN_T: return capability(bools.texture_gen_enable[unit][1]);
    case GL_TEXTURE_GEN_R: return capability(bools.texture_gen_enable[unit][2]);
    case GL_TEXTURE_GEN_Q: return capability(bools.texture_gen_enable[unit][3]);
    // defects-plan.md 1.7.
    case GL_TEXTURE_3D: return capability(bools.texture_3d_enable[unit]);
    case GL_TEXTURE_CUBE_MAP: return capability(bools.texture_cube_enable[unit]);
    // defects-plan-2.md 2.2/2.4: both have a real rendering effect now (see
    // fpe_shadergen.cpp), unlike GL_LINE_SMOOTH/GL_POLYGON_SMOOTH below.
    case GL_POINT_SPRITE: return capability(bools.point_sprite_enable);
    case GL_POINT_SMOOTH: return capability(bools.point_smooth_enable);

    // ---- enables for wrapper-side and unsupported pipeline stages ----
    // defects-plan-2.md's scope decisions: GL_LINE_SMOOTH/GL_POLYGON_SMOOTH
    // would need per-primitive geometry processing (line thickening,
    // polygon edge distance) this wrapper's native-rasterizer draw path
    // doesn't have - unlike GL_POINT_SMOOTH above, a real per-point radial
    // falloff needs nothing more than gl_PointCoord. GL_POLYGON_OFFSET_LINE/
    // GL_POLYGON_OFFSET_POINT would need the offset triangle's own depth
    // slope re-applied while the wireframe-expansion path
    // (sfpewDrawMixedPolygonMode) emits its line/point geometry, which it
    // doesn't currently carry through. Same honest-state-no-fake-effect
    // treatment as GL_COLOR_LOGIC_OP/GL_INDEX_LOGIC_OP below, not an
    // oversight.
    case GL_LINE_SMOOTH:
    case GL_POLYGON_SMOOTH:
    case GL_POLYGON_OFFSET_LINE:
    case GL_POLYGON_OFFSET_POINT:
    // glLogicOp itself is real (state.cpp) and GL_LOGIC_OP_MODE reads it
    // back exactly; these two enables stay false deliberately - ES 3.0 core
    // has no fixed-function logic op and no extension-free way to emulate
    // one, so there is no render effect to honestly report as present.
    case GL_COLOR_LOGIC_OP:
    case GL_INDEX_LOGIC_OP:
    case GL_VERTEX_PROGRAM_POINT_SIZE:
    case GL_VERTEX_PROGRAM_TWO_SIDE:
        return capability(false);
    case GL_COLOR_TABLE: return capability(gs.color_tables[0].enabled);
    case GL_POST_CONVOLUTION_COLOR_TABLE: return capability(gs.color_tables[1].enabled);
    case GL_POST_COLOR_MATRIX_COLOR_TABLE: return capability(gs.color_tables[2].enabled);
    case GL_CONVOLUTION_1D: return capability(gs.convolutions[0].enabled);
    case GL_CONVOLUTION_2D: return capability(gs.convolutions[1].enabled);
    case GL_SEPARABLE_2D: return capability(gs.convolutions[2].enabled);
    case GL_HISTOGRAM: return capability(gs.histogram.enabled);
    case GL_MINMAX: return capability(gs.minmax.enabled);
    case GL_COLOR_SUM: return capability(bools.color_sum_enable);

    // ---- current vertex attributes ----
    case GL_CURRENT_COLOR: return signal(glm::value_ptr(current.color), 4);
    case GL_CURRENT_NORMAL: return signal(glm::value_ptr(current.normal), 3);
    case GL_CURRENT_SECONDARY_COLOR: return signal(glm::value_ptr(current.secondary_color), 4);
    case GL_CURRENT_TEXTURE_COORDS: return vector(glm::value_ptr(current.texcoord[unit]), 4);
    case GL_CURRENT_FOG_COORD: return scalar(current.fog_coord);
    case GL_EDGE_FLAG: return scalar(current.edge_flag ? 1 : 0);
    case GL_CURRENT_RASTER_POSITION: return vector(glm::value_ptr(uni.raster_position), 4);
    case GL_CURRENT_RASTER_POSITION_VALID: return scalar(uni.raster_position_valid ? 1 : 0);
    case GL_CURRENT_RASTER_COLOR: return signal(glm::value_ptr(uni.raster_color), 4);
    case GL_CURRENT_RASTER_TEXTURE_COORDS: return vector(glm::value_ptr(uni.raster_texcoord), 4);
    case GL_CURRENT_RASTER_DISTANCE: return scalar(0);
    // Colour-index mode does not exist here; its state keeps its initial
    // values so a query cannot mistake absence for a written zero.
    case GL_CURRENT_INDEX:
    case GL_CURRENT_RASTER_INDEX: return scalar(1);
    case GL_CURRENT_RASTER_SECONDARY_COLOR: {
        static const GLfloat black[4] = {0, 0, 0, 1};
        return signal(black, 4);
    }

    // ---- fog ----
    case GL_FOG_MODE: return scalar(ff.fog_mode);
    case GL_FOG_DENSITY: return scalar(uni.fog_density);
    case GL_FOG_START: return scalar(uni.fog_start);
    case GL_FOG_END: return scalar(uni.fog_end);
    case GL_FOG_COLOR: return signal(glm::value_ptr(uni.fog_color), 4);
    case GL_FOG_INDEX: return scalar(ff.fog_index);
    case GL_FOG_COORD_SRC: return scalar(ff.fog_coord_src);

    // ---- lighting and materials ----
    case GL_LIGHT_MODEL_AMBIENT: return signal(glm::value_ptr(uni.light_model_ambient), 4);
    case GL_LIGHT_MODEL_COLOR_CONTROL: return scalar(ff.light_model_color_ctrl);
    case GL_LIGHT_MODEL_LOCAL_VIEWER: return scalar(ff.light_model_local_viewer ? 1 : 0);
    case GL_LIGHT_MODEL_TWO_SIDE: return scalar(ff.light_model_two_side ? 1 : 0);
    case GL_SHADE_MODEL: return scalar(ff.shade_model);
    case GL_COLOR_MATERIAL_FACE: return scalar(ff.color_material_face);
    case GL_COLOR_MATERIAL_PARAMETER: return scalar(ff.color_material_mode);

    // ---- alpha test, point, line, polygon ----
    case GL_ALPHA_TEST_FUNC: return scalar(ff.alpha_func);
    case GL_ALPHA_TEST_REF: return scalar(uni.alpha_ref);
    case GL_POINT_SIZE: return scalar(uni.point_size);
    case GL_LINE_STIPPLE_PATTERN: return scalar(uni.line_stipple_pattern);
    case GL_LINE_STIPPLE_REPEAT: return scalar(uni.line_stipple_factor);
    case GL_POLYGON_MODE:
        values[0] = uni.polygon_mode_front;
        values[1] = uni.polygon_mode_back;
        *count = 2;
        return true;
    case GL_POINT_SIZE_MIN: return scalar(uni.point_size_min);
    case GL_POINT_SIZE_MAX:
        sfpewInitializePointSizeMax(gs);
        return scalar(uni.point_size_max);
    case GL_POINT_FADE_THRESHOLD_SIZE: return scalar(uni.point_fade_threshold_size);
    case GL_POINT_DISTANCE_ATTENUATION:
        return vector(glm::value_ptr(uni.point_distance_attenuation), 3);
    case GL_POINT_SPRITE_COORD_ORIGIN: return scalar(ff.point_sprite_coord_origin);
    case GL_LOGIC_OP_MODE: return scalar(ff.logic_op_mode);
    // GL 2.1 names the antialiased range GL_SMOOTH_*_RANGE and the plain one
    // GL_POINT_SIZE_RANGE / GL_LINE_WIDTH_RANGE - the same enum either way.
    // A GLES backend reports only its aliased range, which is the range that
    // actually applies here since neither smooth points nor smooth lines
    // exist; a desktop backend knows these names itself, and does not know
    // GL_ALIASED_POINT_SIZE_RANGE at all.
    case GL_POINT_SIZE_RANGE:
    case GL_LINE_WIDTH_RANGE: {
        GLfloat range[2] = {1.0f, 1.0f};
        if (sfpewEnsureBackend() && g_glFuncs.glGetFloatv != nullptr) {
            int major = 0, minor = 0;
            GLenum ask = pname;
            if (sfpewDesktopGLVersion(&major, &minor))
                ask = pname == GL_LINE_WIDTH_RANGE ? GL_ALIASED_LINE_WIDTH_RANGE
                                                   : GL_ALIASED_POINT_SIZE_RANGE;
            g_glFuncs.glGetFloatv(ask, range);
        }
        return vector(range, 2);
    }
    // No backend reports a step size, and a step of one is the claim that
    // cannot promise more than it delivers.
    case GL_POINT_SIZE_GRANULARITY:
    case GL_LINE_WIDTH_GRANULARITY: return scalar(1);
    // The single-buffer name GL 2.1 uses for what GLES calls draw buffer 0.
    case GL_DRAW_BUFFER: {
        GLint buffer = GL_BACK;
        if (sfpewEnsureBackend() && g_glFuncs.glGetIntegerv != nullptr)
            g_glFuncs.glGetIntegerv(GL_DRAW_BUFFER0, &buffer);
        return scalar(buffer);
    }
    // defects-plan-2.md 2.1: real backend state (glReadBuffer sets it
    // directly, GL_READ_BUFFER is a real ES3/GL3.2 pname), no wrapper-side
    // shadow needed - same reasoning as GL_DRAW_BUFFER above.
    case GL_READ_BUFFER: {
        GLint buffer = GL_BACK;
        if (sfpewEnsureBackend() && g_glFuncs.glGetIntegerv != nullptr)
            g_glFuncs.glGetIntegerv(GL_READ_BUFFER, &buffer);
        return scalar(buffer);
    }

    // ---- matrices and their stacks ----
    case GL_MATRIX_MODE: return scalar(uni.transformation.matrix_mode);
    case GL_MODELVIEW_MATRIX:
    case GL_TRANSPOSE_MODELVIEW_MATRIX: {
        const bool transpose = pname == GL_TRANSPOSE_MODELVIEW_MATRIX;
        const auto& m = uni.transformation.matrices[matrix_idx(GL_MODELVIEW)];
        // Minecraft builds its view frustum from this read-back and culls
        // every chunk against it, so a wrong answer here empties the world
        // without a single draw call going missing.
        if (!transpose)
            sfpewListLogMatrixQuery("MODELVIEW", 0,
                                    uni.transformation.matrices_stack[matrix_idx(GL_MODELVIEW)].size(),
                                    glm::value_ptr(m));
        return matrix(m, transpose);
    }
    case GL_PROJECTION_MATRIX:
    case GL_TRANSPOSE_PROJECTION_MATRIX: {
        const bool transpose = pname == GL_TRANSPOSE_PROJECTION_MATRIX;
        const auto& m = uni.transformation.matrices[matrix_idx(GL_PROJECTION)];
        if (!transpose)
            sfpewListLogMatrixQuery("PROJECTION", 1,
                                    uni.transformation.matrices_stack[matrix_idx(GL_PROJECTION)].size(),
                                    glm::value_ptr(m));
        return matrix(m, transpose);
    }
    case GL_TEXTURE_MATRIX:
        return matrix(uni.transformation.texture_matrices[unit], false);
    case GL_TRANSPOSE_TEXTURE_MATRIX:
        return matrix(uni.transformation.texture_matrices[unit], true);
    case GL_COLOR_MATRIX:
        return matrix(uni.transformation.matrices[matrix_idx(GL_COLOR)], false);
    case GL_TRANSPOSE_COLOR_MATRIX:
        return matrix(uni.transformation.matrices[matrix_idx(GL_COLOR)], true);
    // Stack depth counts the current, unpushed matrix as well.
    case GL_MODELVIEW_STACK_DEPTH:
        return scalar(uni.transformation.matrices_stack[matrix_idx(GL_MODELVIEW)].size() + 1);
    case GL_PROJECTION_STACK_DEPTH:
        return scalar(uni.transformation.matrices_stack[matrix_idx(GL_PROJECTION)].size() + 1);
    case GL_TEXTURE_STACK_DEPTH:
        return scalar(uni.transformation.texture_matrices_stack[unit].size() + 1);
    case GL_COLOR_MATRIX_STACK_DEPTH:
        return scalar(uni.transformation.matrices_stack[matrix_idx(GL_COLOR)].size() + 1);
    case GL_MAX_MODELVIEW_STACK_DEPTH: return scalar(MAX_MODELVIEW_STACK_DEPTH);
    case GL_MAX_PROJECTION_STACK_DEPTH: return scalar(MAX_PROJECTION_STACK_DEPTH);
    case GL_MAX_TEXTURE_STACK_DEPTH: return scalar(MAX_TEXTURE_STACK_DEPTH);
    case GL_MAX_COLOR_MATRIX_STACK_DEPTH: return scalar(MAX_COLOR_STACK_DEPTH);

    // ---- fixed-function capacities ----
    case GL_MAX_LIGHTS: return scalar(MAX_LIGHTS);
    case GL_MAX_CLIP_PLANES: return scalar(6);
    // The conventional fixed-function unit count, not MAX_TEX: plans/03
    // keeps the advertised surface at the well-tested subset.
    case GL_MAX_TEXTURE_UNITS:
    case GL_MAX_TEXTURE_COORDS: return scalar(8);
    case GL_MAX_LIST_NESTING: return scalar(64);
    case GL_MAX_NAME_STACK_DEPTH: return scalar(64);
    case GL_MAX_EVAL_ORDER: return scalar(32);
    case GL_MAX_PIXEL_MAP_TABLE: return scalar(SFPEW_MAX_PIXEL_MAP_TABLE);
    case GL_MAX_CONVOLUTION_WIDTH:
    case GL_MAX_CONVOLUTION_HEIGHT: return scalar(SFPEW_MAX_CONVOLUTION_SIZE);

    // ---- client vertex arrays ----
    case GL_CLIENT_ACTIVE_TEXTURE: return scalar(ff.client_active_texture);
    // The client-side array enables are capabilities too: glIsEnabled takes
    // them, and glEnableClientState is what turns them on.
    case GL_VERTEX_ARRAY: return capability(array(GL_VERTEX_ARRAY, kArrayEnabled) != 0);
    case GL_VERTEX_ARRAY_SIZE: return scalar(array(GL_VERTEX_ARRAY, kArraySize));
    case GL_VERTEX_ARRAY_TYPE: return scalar(array(GL_VERTEX_ARRAY, kArrayType));
    case GL_VERTEX_ARRAY_STRIDE: return scalar(array(GL_VERTEX_ARRAY, kArrayStride));
    case GL_VERTEX_ARRAY_BUFFER_BINDING: return scalar(array(GL_VERTEX_ARRAY, kArrayBuffer));
    case GL_NORMAL_ARRAY: return capability(array(GL_NORMAL_ARRAY, kArrayEnabled) != 0);
    case GL_NORMAL_ARRAY_TYPE: return scalar(array(GL_NORMAL_ARRAY, kArrayType));
    case GL_NORMAL_ARRAY_STRIDE: return scalar(array(GL_NORMAL_ARRAY, kArrayStride));
    case GL_NORMAL_ARRAY_BUFFER_BINDING: return scalar(array(GL_NORMAL_ARRAY, kArrayBuffer));
    case GL_COLOR_ARRAY: return capability(array(GL_COLOR_ARRAY, kArrayEnabled) != 0);
    case GL_COLOR_ARRAY_SIZE: return scalar(array(GL_COLOR_ARRAY, kArraySize));
    case GL_COLOR_ARRAY_TYPE: return scalar(array(GL_COLOR_ARRAY, kArrayType));
    case GL_COLOR_ARRAY_STRIDE: return scalar(array(GL_COLOR_ARRAY, kArrayStride));
    case GL_COLOR_ARRAY_BUFFER_BINDING: return scalar(array(GL_COLOR_ARRAY, kArrayBuffer));
    case GL_SECONDARY_COLOR_ARRAY: return capability(array(GL_SECONDARY_COLOR_ARRAY, kArrayEnabled) != 0);
    case GL_SECONDARY_COLOR_ARRAY_SIZE: return scalar(array(GL_SECONDARY_COLOR_ARRAY, kArraySize));
    case GL_SECONDARY_COLOR_ARRAY_TYPE: return scalar(array(GL_SECONDARY_COLOR_ARRAY, kArrayType));
    case GL_SECONDARY_COLOR_ARRAY_STRIDE:
        return scalar(array(GL_SECONDARY_COLOR_ARRAY, kArrayStride));
    case GL_SECONDARY_COLOR_ARRAY_BUFFER_BINDING:
        return scalar(array(GL_SECONDARY_COLOR_ARRAY, kArrayBuffer));
    case GL_TEXTURE_COORD_ARRAY: return capability(array(GL_TEXTURE_COORD_ARRAY, kArrayEnabled) != 0);
    case GL_TEXTURE_COORD_ARRAY_SIZE: return scalar(array(GL_TEXTURE_COORD_ARRAY, kArraySize));
    case GL_TEXTURE_COORD_ARRAY_TYPE: return scalar(array(GL_TEXTURE_COORD_ARRAY, kArrayType));
    case GL_TEXTURE_COORD_ARRAY_STRIDE: return scalar(array(GL_TEXTURE_COORD_ARRAY, kArrayStride));
    case GL_TEXTURE_COORD_ARRAY_BUFFER_BINDING:
        return scalar(array(GL_TEXTURE_COORD_ARRAY, kArrayBuffer));
    case GL_FOG_COORD_ARRAY: return capability(array(GL_FOG_COORD_ARRAY, kArrayEnabled) != 0);
    case GL_FOG_COORD_ARRAY_TYPE: return scalar(array(GL_FOG_COORD_ARRAY, kArrayType));
    case GL_FOG_COORD_ARRAY_STRIDE: return scalar(array(GL_FOG_COORD_ARRAY, kArrayStride));
    case GL_FOG_COORD_ARRAY_BUFFER_BINDING: return scalar(array(GL_FOG_COORD_ARRAY, kArrayBuffer));
    case GL_EDGE_FLAG_ARRAY: return capability(array(GL_EDGE_FLAG_ARRAY, kArrayEnabled) != 0);
    case GL_EDGE_FLAG_ARRAY_STRIDE: return scalar(array(GL_EDGE_FLAG_ARRAY, kArrayStride));
    case GL_EDGE_FLAG_ARRAY_BUFFER_BINDING: return scalar(array(GL_EDGE_FLAG_ARRAY, kArrayBuffer));
    case GL_INDEX_ARRAY: return capability(array(GL_INDEX_ARRAY, kArrayEnabled) != 0);
    case GL_INDEX_ARRAY_TYPE: return scalar(array(GL_INDEX_ARRAY, kArrayType));
    case GL_INDEX_ARRAY_STRIDE: return scalar(array(GL_INDEX_ARRAY, kArrayStride));
    case GL_INDEX_ARRAY_BUFFER_BINDING: return scalar(array(GL_INDEX_ARRAY, kArrayBuffer));

    // ---- display lists ----
    case GL_LIST_BASE: return scalar(DisplayListManager::listBase());
    case GL_LIST_INDEX: return scalar(DisplayListManager::currentList());
    case GL_LIST_MODE:
        return scalar(DisplayListManager::currentList() != 0
                          ? static_cast<GLdouble>(DisplayListManager::currentListMode())
                          : 0);

    // ---- selection and feedback ----
    case GL_RENDER_MODE: return scalar(gs.render_mode);
    case GL_SELECTION_BUFFER_SIZE: return scalar(gs.select_buffer_size);
    case GL_FEEDBACK_BUFFER_SIZE: return scalar(gs.feedback_buffer_size);
    case GL_FEEDBACK_BUFFER_TYPE: return scalar(gs.feedback_type);
    case GL_NAME_STACK_DEPTH: return scalar(gs.name_stack.size());

    // ---- attribute stacks ----
    case GL_ATTRIB_STACK_DEPTH:
    case GL_CLIENT_ATTRIB_STACK_DEPTH:
    case GL_MAX_ATTRIB_STACK_DEPTH:
    case GL_MAX_CLIENT_ATTRIB_STACK_DEPTH: {
        int server = 0, client = 0, maxDepth = 0;
        sfpewAttribStackDepths(&server, &client, &maxDepth);
        if (pname == GL_ATTRIB_STACK_DEPTH) return scalar(server);
        if (pname == GL_CLIENT_ATTRIB_STACK_DEPTH) return scalar(client);
        return scalar(maxDepth);
    }

    // ---- pixel transfer, storage and zoom ----
    case GL_ZOOM_X: return scalar(uni.pixel_zoom_x);
    case GL_ZOOM_Y: return scalar(uni.pixel_zoom_y);
    case GL_RED_SCALE: return scalar(uni.pixel_scale[0]);
    case GL_GREEN_SCALE: return scalar(uni.pixel_scale[1]);
    case GL_BLUE_SCALE: return scalar(uni.pixel_scale[2]);
    case GL_ALPHA_SCALE: return scalar(uni.pixel_scale[3]);
    case GL_DEPTH_SCALE: return scalar(uni.pixel_scale[4]);
    case GL_RED_BIAS: return scalar(uni.pixel_bias[0]);
    case GL_GREEN_BIAS: return scalar(uni.pixel_bias[1]);
    case GL_BLUE_BIAS: return scalar(uni.pixel_bias[2]);
    case GL_ALPHA_BIAS: return scalar(uni.pixel_bias[3]);
    case GL_DEPTH_BIAS: return scalar(uni.pixel_bias[4]);
    case GL_MAP_COLOR: return scalar(uni.pixel_map_color ? 1 : 0);
    case GL_MAP_STENCIL: return scalar(uni.pixel_map_stencil ? 1 : 0);
    case GL_UNPACK_SWAP_BYTES: return scalar(gs.pixel_store_unpack_swap_bytes ? 1 : 0);
    case GL_UNPACK_LSB_FIRST: return scalar(gs.pixel_store_unpack_lsb_first ? 1 : 0);
    case GL_PACK_SWAP_BYTES: return scalar(gs.pixel_store_pack_swap_bytes ? 1 : 0);
    case GL_PACK_LSB_FIRST: return scalar(gs.pixel_store_pack_lsb_first ? 1 : 0);
    // Pack-side 3D skips have no GLES equivalent and nothing reads them.
    case GL_PACK_IMAGE_HEIGHT:
    case GL_PACK_SKIP_IMAGES:
    case GL_INDEX_SHIFT:
    case GL_INDEX_OFFSET: return scalar(0);
    case GL_PIXEL_MAP_I_TO_I_SIZE:
    case GL_PIXEL_MAP_S_TO_S_SIZE:
    case GL_PIXEL_MAP_I_TO_R_SIZE:
    case GL_PIXEL_MAP_I_TO_G_SIZE:
    case GL_PIXEL_MAP_I_TO_B_SIZE:
    case GL_PIXEL_MAP_I_TO_A_SIZE:
    case GL_PIXEL_MAP_R_TO_R_SIZE:
    case GL_PIXEL_MAP_G_TO_G_SIZE:
    case GL_PIXEL_MAP_B_TO_B_SIZE:
    case GL_PIXEL_MAP_A_TO_A_SIZE:
        return scalar(gs.pixel_maps[pname - GL_PIXEL_MAP_I_TO_I_SIZE].size());
    case GL_POST_CONVOLUTION_RED_SCALE:
    case GL_POST_CONVOLUTION_GREEN_SCALE:
    case GL_POST_CONVOLUTION_BLUE_SCALE:
    case GL_POST_CONVOLUTION_ALPHA_SCALE:
        return scalar(uni.post_convolution_scale[pname - GL_POST_CONVOLUTION_RED_SCALE]);
    case GL_POST_COLOR_MATRIX_RED_SCALE:
    case GL_POST_COLOR_MATRIX_GREEN_SCALE:
    case GL_POST_COLOR_MATRIX_BLUE_SCALE:
    case GL_POST_COLOR_MATRIX_ALPHA_SCALE:
        return scalar(uni.post_color_matrix_scale[pname - GL_POST_COLOR_MATRIX_RED_SCALE]);
    case GL_POST_CONVOLUTION_RED_BIAS:
    case GL_POST_CONVOLUTION_GREEN_BIAS:
    case GL_POST_CONVOLUTION_BLUE_BIAS:
    case GL_POST_CONVOLUTION_ALPHA_BIAS:
        return scalar(uni.post_convolution_bias[pname - GL_POST_CONVOLUTION_RED_BIAS]);
    case GL_POST_COLOR_MATRIX_RED_BIAS:
    case GL_POST_COLOR_MATRIX_GREEN_BIAS:
    case GL_POST_COLOR_MATRIX_BLUE_BIAS:
    case GL_POST_COLOR_MATRIX_ALPHA_BIAS:
        return scalar(uni.post_color_matrix_bias[pname - GL_POST_COLOR_MATRIX_RED_BIAS]);

    // ---- accumulation buffer ----
    // Emulated on an RGBA16F attachment (pixelops.cpp), which is what the
    // bit depths report.
    case GL_ACCUM_RED_BITS:
    case GL_ACCUM_GREEN_BITS:
    case GL_ACCUM_BLUE_BITS:
    case GL_ACCUM_ALPHA_BITS: return scalar(16);
    case GL_ACCUM_CLEAR_VALUE: {
        GLfloat rgba[4] = {};
        sfpewAccumClearValue(rgba);
        return signal(rgba, 4);
    }

    // ---- framebuffer and context properties GLES has no query for ----
    case GL_RED_BITS:
    case GL_GREEN_BITS:
    case GL_BLUE_BITS:
    case GL_ALPHA_BITS:
    case GL_DEPTH_BITS:
    case GL_STENCIL_BITS:
        return framebufferBits(pname, values);
    case GL_RGBA_MODE: return scalar(1);
    case GL_INDEX_MODE: return scalar(0);
    case GL_STEREO: return scalar(0);
    case GL_AUX_BUFFERS: return scalar(0);
    case GL_INDEX_BITS: return scalar(0);
    case GL_INDEX_CLEAR_VALUE: return scalar(0);
    case GL_INDEX_WRITEMASK: return scalar(~0u);
    // EGL window surfaces are double buffered; a pbuffer is single buffered
    // but nothing in a legacy frontend branches on this.
    case GL_DOUBLEBUFFER: return scalar(1);

    // ---- hints one of the two floors cannot take, from their shadow ----
    // GL_GENERATE_MIPMAP_HINT is the one that goes the other way: ES 3 has
    // it, a core context rejects it, and NVIDIA's core profile does reject
    // it where Mesa's lets it through.
    case GL_FOG_HINT:
    case GL_LINE_SMOOTH_HINT:
    case GL_PERSPECTIVE_CORRECTION_HINT:
    case GL_POINT_SMOOTH_HINT:
    case GL_POLYGON_SMOOTH_HINT:
    case GL_TEXTURE_COMPRESSION_HINT:
    case GL_GENERATE_MIPMAP_HINT: {
        const int slot = sfpewLegacyHintSlot(pname);
        return scalar(slot >= 0 ? gs.legacy_hints[slot] : GL_DONT_CARE);
    }

    // 1D textures live on 2D storage, so they share its binding.
    case GL_TEXTURE_BINDING_1D: return scalar(sfpewLogicalTextureBinding(GL_TEXTURE_2D));

    default:
        return false;
    }
}

} // namespace

// The client-array pointers, which no glGet* variant can return - they are
// pointers, so they get their own entry point. GLES has none of these
// arrays, and the two buffers below live entirely on the wrapper's side.
void glGetPointerv(GLenum pname, GLvoid** params) {
    if (!params) return;
    auto& gs = g_glstate;
    const auto& arrays = gs.fpe_state.vertexpointer_array;

    GLenum array = 0;
    switch (pname) {
    case GL_VERTEX_ARRAY_POINTER: array = GL_VERTEX_ARRAY; break;
    case GL_NORMAL_ARRAY_POINTER: array = GL_NORMAL_ARRAY; break;
    case GL_COLOR_ARRAY_POINTER: array = GL_COLOR_ARRAY; break;
    case GL_INDEX_ARRAY_POINTER: array = GL_INDEX_ARRAY; break;
    case GL_TEXTURE_COORD_ARRAY_POINTER: array = GL_TEXTURE_COORD_ARRAY; break;
    case GL_EDGE_FLAG_ARRAY_POINTER: array = GL_EDGE_FLAG_ARRAY; break;
    case GL_SECONDARY_COLOR_ARRAY_POINTER: array = GL_SECONDARY_COLOR_ARRAY; break;
    case GL_FOG_COORD_ARRAY_POINTER: array = GL_FOG_COORD_ARRAY; break;
    case GL_FEEDBACK_BUFFER_POINTER:
        *params = gs.feedback_buffer;
        return;
    case GL_SELECTION_BUFFER_POINTER:
        *params = gs.select_buffer;
        return;
    default:
        // Modern pointer queries (GL_DEBUG_CALLBACK_*, mapped buffers) are
        // the backend's; an unknown pname is its error to raise.
        if (sfpewEnsureBackend() && g_glFuncs.glGetPointerv != nullptr)
            g_glFuncs.glGetPointerv(pname, params);
        else
            gs.set_error(GL_INVALID_ENUM);
        return;
    }

    const int slot = vp2idx(array);
    *params = slot >= 0 ? const_cast<GLvoid*>(arrays.attributes[slot].pointer) : nullptr;
}

GLboolean glIsEnabled(GLenum cap) {
    // Every fixed-function enable comes from the same table the glGet family
    // reads, so the two can never disagree about whether fog is on.
    GLdouble values[16] = {};
    int count = 0;
    bool colour = false, enable = false;
    if (fixedFunctionState(cap, values, &count, &colour, &enable) && enable)
        return values[0] != 0 ? GL_TRUE : GL_FALSE;
    if (!sfpewEnsureBackend() || g_glFuncs.glIsEnabled == nullptr) return GL_FALSE;
    return g_glFuncs.glIsEnabled(cap);
}

void glGetBooleanv(GLenum pname, GLboolean* params) {
    if (!params) return;
    GLdouble values[16] = {};
    int count = 0;
    bool colour = false, enable = false;
    if (fixedFunctionState(pname, values, &count, &colour, &enable)) {
        // "GL_FALSE if and only if it is 0.0" - no rounding, no scaling.
        for (int i = 0; i < count; ++i) params[i] = values[i] != 0.0 ? GL_TRUE : GL_FALSE;
        return;
    }
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
    int count = 0;
    bool colour = false, enable = false;
    if (fixedFunctionState(pname, params, &count, &colour, &enable)) return;
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
    const int count_f = floatPnameComponentCount(pname);
    for (int i = 0; i < count_f; ++i) params[i] = static_cast<GLdouble>(staging[i]);
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
                // Desktop-parseable level first, then who is answering and
                // out of which build, backend identity last.
                cachedVersionString = std::to_string(major) + "." + std::to_string(minor) + " " +
                                      kSfpewProjectName + " " + sfpewVersionAndCommit() + " (" +
                                      (const char*)backend + ")";
            } else {
                // A desktop backend's own string already parses, so it stays
                // first and the wrapper appends itself - with the commit,
                // same as above: which build answered is the question a
                // report has to be able to settle either way.
                cachedVersionString = std::string((const char*)backend) + " (with " +
                                      kSfpewProjectFullName + " " + sfpewVersionAndCommit() + ")";
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
            cachedRendererString = std::string((const char*)backend) + " (" + kSfpewProjectName +
                                   " " + sfpewVersionString() + ")";
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
    // Fixed-function state first, and without needing a backend: it is the
    // wrapper's own, and the conversion to integers is the spec's - round,
    // except colours and normals, which map onto the whole integer range.
    {
        GLdouble values[16] = {};
        int count = 0;
        bool colour = false, enable = false;
        if (fixedFunctionState(pname, values, &count, &colour, &enable)) {
            for (int i = 0; i < count; ++i) {
                const double scaled =
                    colour ? std::clamp(values[i], -1.0, 1.0) * 2147483647.0 : values[i];
                params[i] = static_cast<GLint>(std::clamp(std::round(scaled), -2147483648.0,
                                                          2147483647.0));
            }
            return;
        }
    }
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
    default:
        g_glFuncs.glGetIntegerv(pname, params);
        break;
    }
}

void glGetFloatv(GLenum pname, GLfloat* params) {
    if (!params) return;
    // The fixed-function table answers in doubles; float is a narrowing of
    // the same values, and integers in it (enums, counts) become floats.
    GLdouble values[16] = {};
    int count = 0;
    bool colour = false, enable = false;
    if (fixedFunctionState(pname, values, &count, &colour, &enable)) {
        for (int i = 0; i < count; ++i) params[i] = static_cast<GLfloat>(values[i]);
        return;
    }
    if (isWrapperIntegerPname(pname)) {
        GLint value = 0;
        glGetIntegerv(pname, &value);
        params[0] = static_cast<GLfloat>(value);
        return;
    }
    if (sfpewEnsureBackend() && g_glFuncs.glGetFloatv != nullptr) g_glFuncs.glGetFloatv(pname, params);
}

void glActiveTexture(GLenum texture) {
    if (!sfpewEnsureBackend() || g_glFuncs.glActiveTexture == nullptr) return;
    (void)g_glstate; // entry strict resolve; the binding shadow reads the snapshot
    // Record BEFORE the redundancy shortcut: a list must contain the command
    // even when it matches the current state (GL spec; audit finding).
    LIST_RECORD(glActiveTexture, {}, texture)
    auto& state = getLogicalTextureBindings();
    if (getLogicalActiveTexture(state) == texture) return;

    sfpewTextureStateBarrier();
    g_glFuncs.glActiveTexture(texture);
    state.activeTexture = texture;
    state.activeTextureKnown = true;
    ++state.generation;
}

void glBindTexture(GLenum target, GLuint texture) {
    if (!sfpewEnsureBackend() || g_glFuncs.glBindTexture == nullptr) return;
    (void)g_glstate; // entry strict resolve; the binding shadow reads the snapshot
    if (target == GL_TEXTURE_1D) target = GL_TEXTURE_2D; // Nx1 emulation
    LIST_RECORD(glBindTexture, {}, target, texture)
    auto& state = getLogicalTextureBindings();
    const GLenum activeTexture = getLogicalActiveTexture(state);
    const GLenum query = textureBindingQuery(target);
    const uint64_t key = textureBindingKey(activeTexture, target);

    if (query != GL_NONE) {
        if (sfpewLogicalTextureBinding(target) == texture) return;
    }

    sfpewTextureStateBarrier();
    g_glFuncs.glBindTexture(target, texture);
    if (query != GL_NONE) {
        state.bindings[key] = texture;
        ++state.generation;
    }
}

void glDeleteTextures(GLsizei n, const GLuint* textures) {
    if (!sfpewEnsureBackend() || g_glFuncs.glDeleteTextures == nullptr) return;
    (void)g_glstate; // entry strict resolve; the binding shadow reads the snapshot
    sfpewEntryBarrier();
    g_glFuncs.glDeleteTextures(n, textures);
    if (n <= 0 || textures == nullptr) return;

    auto& state = getLogicalTextureBindings();
    auto& gs = g_glstate_c;
    for (auto& [key, binding] : state.bindings) {
        for (GLsizei i = 0; i < n; ++i) {
            if (binding == textures[i]) {
                binding = 0;
                ++state.generation;
                break;
            }
        }
    }
    auto& metadata = textureMetadata();
    for (GLsizei i = 0; i < n; ++i) {
        gs.texture_priorities.erase(textures[i]);
        gs.texture_depth_mode.erase(textures[i]);
        gs.texture_compare_mode.erase(textures[i]);
        metadata.sizes.erase(textures[i]);
        metadata.levels.erase(textures[i]);
    }
}

GLboolean glAreTexturesResident(GLsizei n, const GLuint* textures, GLboolean* residences) {
    auto& gs = g_glstate;
    if (n < 0) {
        gs.set_error(GL_INVALID_VALUE);
        return GL_FALSE;
    }
    if (n == 0) return GL_TRUE;
    if (textures == nullptr || residences == nullptr) return GL_FALSE;
    sfpewEntryBarrier();
    // Validate the complete input before touching the caller's output buffer.
    for (GLsizei i = 0; i < n; ++i) {
        if (textures[i] == 0 || g_glFuncs.glIsTexture == nullptr ||
            g_glFuncs.glIsTexture(textures[i]) == GL_FALSE) {
            gs.set_error(GL_INVALID_VALUE);
            return GL_FALSE;
        }
    }
    for (GLsizei i = 0; i < n; ++i) residences[i] = GL_TRUE;
    return GL_TRUE;
}

void glPrioritizeTextures(GLsizei n, const GLuint* textures, const GLclampf* priorities) {
    auto& gs = g_glstate;
    if (n < 0) {
        gs.set_error(GL_INVALID_VALUE);
        return;
    }
    LIST_RECORD(glPrioritizeTextures,
                {{1, (size_t)n * sizeof(GLuint)}, {2, (size_t)n * sizeof(GLclampf)}},
                n, textures, priorities)
    if (n == 0 || textures == nullptr || priorities == nullptr) return;
    sfpewEntryBarrier();
    for (GLsizei i = 0; i < n; ++i) {
        if (textures[i] == 0) continue;
        if (g_glFuncs.glIsTexture != nullptr && g_glFuncs.glIsTexture(textures[i]) == GL_FALSE)
            continue;
        gs.texture_priorities[textures[i]] = std::clamp(priorities[i], 0.0f, 1.0f);
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

// GL_ARB_depth_texture / GLES3+desktop-GL3 core natively accept these as
// internalformat, so mapLegacyInternalFormat above has no case for them -
// they need only the GL_DEPTH_TEXTURE_MODE swizzle below, not a storage
// rewrite.
bool isDepthTextureInternalFormat(GLint internalformat) {
    switch (internalformat) {
    case GL_DEPTH_COMPONENT:
    case GL_DEPTH_COMPONENT16:
    case GL_DEPTH_COMPONENT24:
    case GL_DEPTH_COMPONENT32F:
        return true;
    default:
        return false;
    }
}

// GL_DEPTH_TEXTURE_MODE (GL_ARB_depth_texture) sampling semantics as a
// GL_TEXTURE_SWIZZLE_RGBA array. GL_RED is the core GLES3/GL3 default
// (introduced when the fixed mode was deprecated) - depth sampled straight
// into .r with no replication.
void depthTextureModeSwizzle(GLenum mode, GLint (&out)[4]) {
    switch (mode) {
    case GL_INTENSITY:
        out[0] = out[1] = out[2] = out[3] = GL_RED;
        return;
    case GL_ALPHA:
        out[0] = out[1] = out[2] = GL_ZERO;
        out[3] = GL_RED;
        return;
    case GL_RED:
        out[0] = GL_RED; out[1] = GL_GREEN; out[2] = GL_BLUE; out[3] = GL_ALPHA;
        return;
    case GL_LUMINANCE:
    default:
        out[0] = out[1] = out[2] = GL_RED;
        out[3] = GL_ONE;
        return;
    }
}

// GL_BGRA uploads: GLES3 has no core BGRA path; swap on the CPU.
// Tightly-packed rows are assumed (the common LWJGL/awt case); exotic
// unpack state falls back to passthrough with a log line.
} // namespace

// Reusable trigger for both glTexImage2D (a depth texture just got uploaded)
// and glTexParameter* (GL_DEPTH_TEXTURE_MODE changed on an already-uploaded
// one). GL_ARB_shadow's GL_TEXTURE_COMPARE_MODE turned out NOT to need a
// third trigger through here: a sampler2DShadow's texture() call returns a
// plain float, so GL_TEXTURE_SWIZZLE_RGBA (whatever it is set to) never
// reaches the shader for a shadow-sampled unit - fpe_shadergen.cpp handles
// that case with its own shader-side vec4 broadcast instead. See
// glstate.cpp's resync_texture_shadow_sample() and the texture_shadow_sample
// codegen branch in fpe_shadergen.cpp's add_fs_body.
void sfpewApplyDepthTextureModeSwizzle(GLenum target, GLuint texture) {
    if (texture == 0 || g_glFuncs.glTexParameteriv == nullptr) return;
    const texture_level_t* level0 = findTextureLevel(texture, target, 0);
    if (level0 == nullptr || !isDepthTextureInternalFormat(level0->internal_format)) return;
    GLint swizzle[4];
    depthTextureModeSwizzle(depthTextureMode(texture), swizzle);
    g_glFuncs.glTexParameteriv(swizzleTargetFor(target), GL_TEXTURE_SWIZZLE_RGBA, swizzle);
}

// Reorder a GL_BGRA upload into tightly packed RGBA bytes, reading the source
// the way the backend would have: through the bound pixel unpack buffer when
// one is bound (mapped for reading - the bytes never pass through the wrapper
// otherwise), and honouring GL_UNPACK_ROW_LENGTH / SKIP_ROWS / SKIP_PIXELS.
// The tight-row shortcut this replaces broke every sub-rectangle upload out
// of a larger image, which is exactly how Minecraft stitches its atlases.
//
// On success the unpack state that would misread the TIGHT result is cleared
// and any unpack buffer is unbound; sfpewFinishBgraUpload restores both after
// the caller's backend upload. Alignment needs no handling: both source and
// result rows are whole 4-byte pixels.
bool sfpewPrepareBgraUpload(GLsizei width, GLsizei height, GLenum type, const void* pixels,
                            sfpew_bgra_upload_t* out) {
    *out = {};
    if (width <= 0 || height <= 0) return false;
    if (type != GL_UNSIGNED_BYTE && type != GL_UNSIGNED_INT_8_8_8_8 &&
        type != GL_UNSIGNED_INT_8_8_8_8_REV) {
        return false;
    }
    if (g_glFuncs.glGetIntegerv == nullptr || g_glFuncs.glPixelStorei == nullptr) return false;

    GLint row_length = 0, skip_rows = 0, skip_pixels = 0, pbo = 0;
    g_glFuncs.glGetIntegerv(GL_UNPACK_ROW_LENGTH, &row_length);
    g_glFuncs.glGetIntegerv(GL_UNPACK_SKIP_ROWS, &skip_rows);
    g_glFuncs.glGetIntegerv(GL_UNPACK_SKIP_PIXELS, &skip_pixels);
    g_glFuncs.glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &pbo);
    if (row_length < 0 || skip_rows < 0 || skip_pixels < 0) return false;

    const size_t row_px = row_length > 0 ? (size_t)row_length : (size_t)width;
    if (row_px < (size_t)width + (size_t)skip_pixels &&
        !(row_length > 0 && (size_t)row_length >= (size_t)width)) {
        // A row the caller described as narrower than the rectangle it is
        // uploading; let the backend produce whatever error it would have.
        if (row_length > 0) return false;
    }
    const size_t start = ((size_t)skip_rows * row_px + (size_t)skip_pixels) * 4u;
    const size_t needed = start + (((size_t)height - 1u) * row_px + (size_t)width) * 4u;

    const uint8_t* source = nullptr;
    GLuint copy_scratch = 0;
    if (pbo != 0) {
        if (g_glFuncs.glMapBufferRange == nullptr || g_glFuncs.glUnmapBuffer == nullptr)
            return false;
        // SFPEW_TEST_FORCE_BGRA_COPY=1: pretend the direct mapping failed,
        // exercising the copy path below on drivers whose direct mapping
        // succeeds. The copy path exists FOR drivers where it does not
        // (mobile refuses READ mappings of *_DRAW buffers), so without this
        // it would only ever run on the machines that need it least.
        static const bool force_copy_path = [] {
            const char* v = getenv("SFPEW_TEST_FORCE_BGRA_COPY");
            return v != nullptr && v[0] != '\0' && v[0] != '0';
        }();
        source = force_copy_path
                     ? nullptr
                     : static_cast<const uint8_t*>(g_glFuncs.glMapBufferRange(
                           GL_PIXEL_UNPACK_BUFFER, (GLintptr)(uintptr_t)pixels,
                           (GLsizeiptr)needed, GL_MAP_READ_BIT));
        if (source == nullptr) {
            // Mobile drivers routinely refuse a READ mapping of a buffer
            // whose usage hint was *_DRAW (the natural hint for an upload
            // PBO). Reading it back through a copy into a *_READ scratch is
            // the path those drivers do serve. This runs once per upload of
            // a shape the wrapper cannot otherwise reach, not per frame.
            while (g_glFuncs.glGetError() != GL_NO_ERROR) {} // eat the failed map's error
            if (g_glFuncs.glCopyBufferSubData == nullptr || g_glFuncs.glGenBuffers == nullptr ||
                g_glFuncs.glDeleteBuffers == nullptr || g_glFuncs.glBindBuffer == nullptr ||
                g_glFuncs.glBufferData == nullptr) {
                return false;
            }
            g_glFuncs.glGenBuffers(1, &copy_scratch);
            g_glFuncs.glBindBuffer(GL_COPY_WRITE_BUFFER, copy_scratch);
            g_glFuncs.glBufferData(GL_COPY_WRITE_BUFFER, (GLsizeiptr)needed, nullptr,
                                   GL_STREAM_READ);
            g_glFuncs.glCopyBufferSubData(GL_PIXEL_UNPACK_BUFFER, GL_COPY_WRITE_BUFFER,
                                          (GLintptr)(uintptr_t)pixels, 0, (GLsizeiptr)needed);
            source = static_cast<const uint8_t*>(g_glFuncs.glMapBufferRange(
                GL_COPY_WRITE_BUFFER, 0, (GLsizeiptr)needed, GL_MAP_READ_BIT));
            if (source == nullptr) {
                SFPEW_LOGW("BGRA upload: unpack buffer %d unmappable even via copy; "
                           "conversion impossible", pbo);
                g_glFuncs.glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
                g_glFuncs.glDeleteBuffers(1, &copy_scratch);
                return false;
            }
        }
    } else {
        if (pixels == nullptr) return false;
        source = static_cast<const uint8_t*>(pixels);
    }

    thread_local std::vector<uint8_t> scratch;
    scratch.resize((size_t)width * (size_t)height * 4u);
    for (size_t row = 0; row < (size_t)height; ++row) {
        const uint8_t* in = source + start + row * row_px * 4u;
        uint8_t* line = scratch.data() + row * (size_t)width * 4u;
        if (type == GL_UNSIGNED_INT_8_8_8_8) {
            // Big-endian component packing: bytes in memory are A,R,G,B on a
            // little-endian host.
            for (size_t px = 0; px < (size_t)width * 4u; px += 4) {
                line[px + 0] = in[px + 1];
                line[px + 1] = in[px + 2];
                line[px + 2] = in[px + 3];
                line[px + 3] = in[px + 0];
            }
        } else {
            // GL_UNSIGNED_BYTE and _REV both lay the bytes out B,G,R,A.
            for (size_t px = 0; px < (size_t)width * 4u; px += 4) {
                line[px + 0] = in[px + 2];
                line[px + 1] = in[px + 1];
                line[px + 2] = in[px + 0];
                line[px + 3] = in[px + 3];
            }
        }
    }

    if (copy_scratch != 0) {
        g_glFuncs.glUnmapBuffer(GL_COPY_WRITE_BUFFER);
        g_glFuncs.glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
        g_glFuncs.glDeleteBuffers(1, &copy_scratch);
        g_glFuncs.glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        out->rebind_pbo = (GLuint)pbo;
    } else if (pbo != 0) {
        g_glFuncs.glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
        g_glFuncs.glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        out->rebind_pbo = (GLuint)pbo;
    }
    if (row_length != 0) g_glFuncs.glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    if (skip_rows != 0) g_glFuncs.glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
    if (skip_pixels != 0) g_glFuncs.glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    out->row_length = row_length;
    out->skip_rows = skip_rows;
    out->skip_pixels = skip_pixels;
    out->pixels = scratch.data();
    return true;
}

void sfpewFinishBgraUpload(const sfpew_bgra_upload_t& upload) {
    if (g_glFuncs.glPixelStorei != nullptr) {
        if (upload.row_length != 0) g_glFuncs.glPixelStorei(GL_UNPACK_ROW_LENGTH, upload.row_length);
        if (upload.skip_rows != 0) g_glFuncs.glPixelStorei(GL_UNPACK_SKIP_ROWS, upload.skip_rows);
        if (upload.skip_pixels != 0)
            g_glFuncs.glPixelStorei(GL_UNPACK_SKIP_PIXELS, upload.skip_pixels);
    }
    if (upload.rebind_pbo != 0 && g_glFuncs.glBindBuffer != nullptr)
        g_glFuncs.glBindBuffer(GL_PIXEL_UNPACK_BUFFER, upload.rebind_pbo);
}

void glTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border,
                  GLenum format, GLenum type, const GLvoid* pixels) {
    if (!sfpewEnsureBackend() || g_glFuncs.glTexImage2D == nullptr || g_glFuncs.glGetIntegerv == nullptr) return;
    // Restores unpack state and any unbound pixel buffer once the upload
    // below - whichever return path it takes - is done.
    struct bgra_upload_guard_t {
        sfpew_bgra_upload_t state{};
        bool active = false;
        ~bgra_upload_guard_t() {
            if (active) sfpewFinishBgraUpload(state);
        }
    } bgraUpload;
    struct imaging_upload_guard_t {
        sfpew_imaging_upload_t state{};
        bool active = false;
        ~imaging_upload_guard_t() {
            if (active) sfpewFinishImagingUpload(state);
        }
    } imagingUpload;
    (void)g_glstate; // entry strict resolve; the binding/proxy shadows read the snapshot
    sfpewEntryBarrier();
    if (target != GL_PROXY_TEXTURE_2D && sfpewImagingActive() &&
        (pixels != nullptr || sfpewUnpackPboBound())) {
        imagingUpload.active =
            sfpewPrepareImagingUpload(width, height, format, type, pixels, &imagingUpload.state);
        if (imagingUpload.active) {
            if (!imagingUpload.state.valid) return;
            format = GL_RGBA;
            type = GL_FLOAT;
            width = imagingUpload.state.width;
            height = imagingUpload.state.height;
            if (imagingUpload.state.sink) {
                pixels = nullptr;
            } else {
                pixels = imagingUpload.state.rgba.data();
            }
        }
    }
    if (target != GL_PROXY_TEXTURE_2D) {
        // Desktop GL takes GL_BGRA as-is; on GLES every BGRA source - client
        // memory or a pixel unpack buffer - is reordered into tight RGBA by
        // sfpewPrepareBgraUpload, which also neutralizes the unpack state the
        // tight result would be misread under. The guard restores both after
        // the backend call below.
        if (format == GL_BGRA && !sfpewBackendTakesBgra()) {
            if (pixels == nullptr && !sfpewUnpackPboBound()) {
                // Pure allocation: nothing to reorder.
                format = GL_RGBA;
                type = GL_UNSIGNED_BYTE;
                if (internalformat == GL_BGRA) internalformat = GL_RGBA8;
            } else if (sfpewPrepareBgraUpload(width, height, type, pixels, &bgraUpload.state)) {
                bgraUpload.active = true;
                pixels = bgraUpload.state.pixels;
                format = GL_RGBA;
                type = GL_UNSIGNED_BYTE;
                if (internalformat == GL_BGRA) internalformat = GL_RGBA8;
            } else {
                // Conversion genuinely impossible (exotic type, or a buffer
                // no mapping path can read). Pass it through in the ONE form
                // EXT_texture_format_BGRA8888 defines - internalformat and
                // format both GL_BGRA, type UNSIGNED_BYTE - so a backend
                // with that extension renders it correctly instead of
                // byte-copying BGRA bytes into RGBA storage. Without the
                // extension this errors, exactly like the raw passthrough
                // did.
                SFPEW_LOGW("glTexImage2D: GL_BGRA upload (type 0x%x) could not be converted; "
                           "passing through as EXT_texture_format_BGRA8888", type);
                internalformat = GL_BGRA;
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
            // defects-plan-2.md 2.7: glTexImage2D also uploads cube faces -
            // these fell out of texture_metadata_cache_t entirely before,
            // same gap as glGetTexImage's 3D/cube case (defects-plan.md 1.2).
            if (target == GL_TEXTURE_2D ||
                (target >= GL_TEXTURE_CUBE_MAP_POSITIVE_X && target <= GL_TEXTURE_CUBE_MAP_NEGATIVE_Z)) {
                rememberTextureLevel(sfpewLogicalTextureBinding(textureMetadataTarget(target)), target,
                                     level, width, height, internalformat, border, false, 0);
            }
            if (g_glFuncs.glTexParameteriv != nullptr)
                g_glFuncs.glTexParameteriv(swizzleTargetFor(target), GL_TEXTURE_SWIZZLE_RGBA, mapping.swizzle);
            sfpewMaybeGenerateMipmap(target);
            return;
        }

        g_glFuncs.glTexImage2D(target, level, internalformat, width, height, border, format, type, pixels);
        if (target == GL_TEXTURE_2D ||
            (target >= GL_TEXTURE_CUBE_MAP_POSITIVE_X && target <= GL_TEXTURE_CUBE_MAP_NEGATIVE_Z)) {
            rememberTextureLevel(sfpewLogicalTextureBinding(textureMetadataTarget(target)), target, level,
                                 width, height, internalformat, border, false, 0);
            // GL_DEPTH_COMPONENT/16/24/32F pass straight through above (no
            // legacy-format rewrite exists for them); they still need the
            // GL_DEPTH_TEXTURE_MODE swizzle applied post-upload.
            if (level == 0 && isDepthTextureInternalFormat(internalformat))
                sfpewApplyDepthTextureModeSwizzle(target, sfpewLogicalTextureBinding(textureMetadataTarget(target)));
        }
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

// defects-plan-2.md 2.7: glTexImage3D was entirely unwrapped before this -
// not in lookup.cpp's GETPROC table, so eglGetProcAddress("glTexImage3D")
// fell straight through to the real backend (both floors have it
// natively, ES 3.0 core and GL 3.2 core) with zero wrapper-side
// bookkeeping. Deliberately minimal, unlike glTexImage2D above: no BGRA
// conversion, no legacy-internal-format rewriting, no ARB_imaging hook -
// those exist for glTexImage2D because that is the path every legacy
// texture upload actually takes; a 3D upload choosing one of those same
// formats is a vanishingly rare combination not worth this function's
// complexity. Passes straight through and only adds the metadata
// glGetTexLevelParameteriv's ES fallback needs.
void glTexImage3D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height,
                  GLsizei depth, GLint border, GLenum format, GLenum type, const GLvoid* pixels) {
    if (!sfpewEnsureBackend() || g_glFuncs.glTexImage3D == nullptr) return;
    sfpewEntryBarrier();
    g_glFuncs.glTexImage3D(target, level, internalformat, width, height, depth, border, format, type,
                           pixels);
    if (target == GL_TEXTURE_3D) {
        rememberTextureLevel(sfpewLogicalTextureBinding(GL_TEXTURE_3D), target, level, width, height,
                             internalformat, border, false, 0, depth);
    }
}

void glGetTexImage(GLenum target, GLint level, GLenum format, GLenum type, GLvoid* pixels) {
    if (pixels == nullptr) return;
    if (!sfpewEnsureBackend() || g_glFuncs.glGenFramebuffers == nullptr ||
        g_glFuncs.glFramebufferTexture2D == nullptr || g_glFuncs.glReadPixels == nullptr) {
        return;
    }
    auto& gs = g_glstate;
    if (target == GL_TEXTURE_1D) target = GL_TEXTURE_2D; // Nx1 emulation

    // GL_TEXTURE_3D and the six cube faces: same "GLES has no texture
    // readback" ceiling as glGetCompressedTexImage - unlike the per-level
    // SIZE query (glGetTexLevelParameteriv), which texture_metadata_cache_t
    // now covers for these targets too (defects-plan-2.md 2.7), reading
    // pixel data back needs a GLES readback path that does not exist for
    // any target, not even 2D's own FBO+glReadPixels trick. Forward exactly
    // on desktop, where glGetTexImage handles every target and size
    // natively; refuse loudly on GLES rather than guessing a size or
    // reading the wrong slice.
    switch (target) {
    case GL_TEXTURE_3D:
    case GL_TEXTURE_CUBE_MAP_POSITIVE_X:
    case GL_TEXTURE_CUBE_MAP_NEGATIVE_X:
    case GL_TEXTURE_CUBE_MAP_POSITIVE_Y:
    case GL_TEXTURE_CUBE_MAP_NEGATIVE_Y:
    case GL_TEXTURE_CUBE_MAP_POSITIVE_Z:
    case GL_TEXTURE_CUBE_MAP_NEGATIVE_Z:
        if (!sfpewBackendIsES() && g_glFuncs.glGetTexImage != nullptr) {
            g_glFuncs.glGetTexImage(target, level, format, type, pixels);
            return;
        }
        gs.set_error(GL_INVALID_OPERATION);
        return;
    default:
        break;
    }

    if (target != GL_TEXTURE_2D) {
        SFPEW_LOGW("glGetTexImage: target 0x%x not supported", target);
        gs.set_error(GL_INVALID_ENUM);
        return;
    }
    const GLuint texture = sfpewLogicalTextureBinding(GL_TEXTURE_2D);
    const auto& texture_sizes = textureMetadata().sizes;
    const auto size_it = texture_sizes.find(texture);
    if (texture == 0 || size_it == texture_sizes.end()) {
        gs.set_error(GL_INVALID_OPERATION);
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
    if (params == nullptr) return;
    auto& gs = g_glstate;
    sfpewEntryBarrier();
    if (!sfpewEnsureBackend()) return;

    if (target == GL_PROXY_TEXTURE_1D) target = GL_PROXY_TEXTURE_2D;
    if (target == GL_PROXY_TEXTURE_2D) {
        GLint value = 0;
        if (getProxyTextureLevelParameter(level, pname, value)) {
            *params = value;
            return;
        }
        if (g_glFuncs.glGetTexLevelParameteriv != nullptr)
            g_glFuncs.glGetTexLevelParameteriv(target, level, pname, params);
        else
            gs.set_error(GL_INVALID_OPERATION);
        return;
    }

    if (target == GL_TEXTURE_1D) target = GL_TEXTURE_2D;
    if (!sfpewBackendIsES() && g_glFuncs.glGetTexLevelParameteriv != nullptr) {
        g_glFuncs.glGetTexLevelParameteriv(target, level, pname, params);
        return;
    }

    // ES 3.0 has no level query. The N x 1 texture paths, glTexImage3D, and
    // the six cube faces (defects-plan-2.md 2.7) record the properties
    // observable without retaining image bytes; unknown targets/levels are
    // refused instead of returning an invented value.
    const bool is_cube_face =
        target >= GL_TEXTURE_CUBE_MAP_POSITIVE_X && target <= GL_TEXTURE_CUBE_MAP_NEGATIVE_Z;
    if (target != GL_TEXTURE_2D && target != GL_TEXTURE_3D && !is_cube_face) {
        gs.set_error(GL_INVALID_OPERATION);
        return;
    }
    if (level < 0) {
        gs.set_error(GL_INVALID_VALUE);
        return;
    }
    const GLenum binding_point = textureMetadataTarget(target);
    const texture_level_t* info =
        findTextureLevel(sfpewLogicalTextureBinding(binding_point), target, level);
    if (info == nullptr) {
        gs.set_error(GL_INVALID_OPERATION);
        return;
    }
    switch (pname) {
    case GL_TEXTURE_WIDTH: *params = info->width; break;
    case GL_TEXTURE_HEIGHT: *params = info->height; break;
    case GL_TEXTURE_DEPTH: *params = info->depth; break;
    case GL_TEXTURE_INTERNAL_FORMAT: *params = info->internal_format; break;
    case GL_TEXTURE_BORDER: *params = info->border; break;
    case GL_TEXTURE_COMPRESSED: *params = info->compressed ? GL_TRUE : GL_FALSE; break;
    case GL_TEXTURE_COMPRESSED_IMAGE_SIZE:
        if (!info->compressed) {
            gs.set_error(GL_INVALID_OPERATION);
            return;
        }
        *params = info->compressed_size;
        break;
    default:
        gs.set_error(GL_INVALID_OPERATION);
        break;
    }
}

void glGetTexLevelParameterfv(GLenum target, GLint level, GLenum pname, GLfloat* params) {
    if (params == nullptr) return;
    GLint value = 0;
    glGetTexLevelParameteriv(target, level, pname, &value);
    *params = static_cast<GLfloat>(value);
}
