// SimpleFPEWrapper - tests/gtest_texture_scratch_context.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// The scratch GL objects texture_image.cpp keeps for itself - glGetTexImage's
// read framebuffer, the mipmap repair's two blit framebuffers and its scratch
// texture - are thread_local. A GL name only means anything inside the
// context that generated it, so a second, unshared context on the same thread
// finds those names pointing at ITS OWN objects: the app's framebuffers, whose
// attachments the wrapper then rewrites (plans/17 P8). Every cache in this
// file that survives a context switch is supposed to compare
// glstate_t::cached_context() and wipe when it changes; these did not.

#include "sfpew_gtest.h"

#include <array>
#include <vector>

namespace {

using sfpew_test::ContextTest;
using sfpew_test::GLenum;
using sfpew_test::GLint;
using sfpew_test::GLsizei;
using sfpew_test::GLubyte;
using sfpew_test::GLuint;

constexpr GLenum GL_TEXTURE_2D_ = 0x0DE1;
constexpr GLenum GL_TEXTURE_MIN_FILTER_ = 0x2801;
constexpr GLenum GL_TEXTURE_MAG_FILTER_ = 0x2800;
constexpr GLenum GL_NEAREST_ = 0x2600;
constexpr GLenum GL_LINEAR_ = 0x2601;
constexpr GLenum GL_RGBA_ = 0x1908;
constexpr GLenum GL_RGBA8_ = 0x8058;
constexpr GLenum GL_UNSIGNED_BYTE_ = 0x1401;
constexpr GLenum GL_FRAMEBUFFER_ = 0x8D40;
constexpr GLenum GL_COLOR_ATTACHMENT0_ = 0x8CE0;
constexpr GLenum GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME_ = 0x8CD1;
constexpr GLenum GL_NO_ERROR_ = 0;

// Enough of them that whichever small name the wrapper cached in the first
// context lands on one: both contexts hand out names from 1.
constexpr int kAppFramebuffers = 4;
constexpr int kTextureSize = 4;

class ScratchContextTest : public ContextTest {
protected:
    void SetUp() override {
        ContextTest::SetUp();
        if (::testing::Test::IsSkipped() || ::testing::Test::HasFatalFailure()) return;
        make_current_ = Get<EGLBoolean (*)(EGLDisplay, EGLSurface, EGLSurface, EGLContext)>(
            "eglMakeCurrent");
        ASSERT_TRUE(make_current_(display(), surface(), surface(), eglGetCurrentContext()));

        get_error_ = Get<GLenum (*)()>("glGetError");
        gen_textures_ = Get<void (*)(GLsizei, GLuint*)>("glGenTextures");
        bind_texture_ = Get<void (*)(GLenum, GLuint)>("glBindTexture");
        tex_image2d_ = Get<void (*)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum,
                                    const void*)>("glTexImage2D");
        tex_parameteri_ = Get<void (*)(GLenum, GLenum, GLint)>("glTexParameteri");
        get_tex_image_ = Get<void (*)(GLenum, GLint, GLenum, GLenum, void*)>("glGetTexImage");
        generate_mipmap_ = Get<void (*)(GLenum)>("glGenerateMipmap");
        gen_framebuffers_ = Get<void (*)(GLsizei, GLuint*)>("glGenFramebuffers");
        bind_framebuffer_ = Get<void (*)(GLenum, GLuint)>("glBindFramebuffer");
        framebuffer_texture2d_ =
            Get<void (*)(GLenum, GLenum, GLenum, GLuint, GLint)>("glFramebufferTexture2D");
        get_attachment_ = Get<void (*)(GLenum, GLenum, GLenum, GLint*)>(
            "glGetFramebufferAttachmentParameteriv");
    }

    void TearDown() override {
        if (second_context_ != EGL_NO_CONTEXT) {
            make_current_(display(), EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            eglDestroyContext(display(), second_context_);
            second_context_ = EGL_NO_CONTEXT;
        }
        ContextTest::TearDown();
    }

    // A second, deliberately UNSHARED context on the same thread: with a
    // share group the names would still resolve, which is precisely the case
    // that hides this.
    bool MakeSecondContextCurrent() {
        EGLint config_id = 0;
        eglQueryContext(display(), eglGetCurrentContext(), EGL_CONFIG_ID, &config_id);
        const EGLint config_attribs[] = {EGL_CONFIG_ID, config_id, EGL_NONE};
        EGLConfig config = nullptr;
        EGLint count = 0;
        if (!eglChooseConfig(display(), config_attribs, &config, 1, &count) || count == 0)
            return false;
        const EGLint context_attribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
        second_context_ = eglCreateContext(display(), config, EGL_NO_CONTEXT, context_attribs);
        if (second_context_ == EGL_NO_CONTEXT) return false;
        return make_current_(display(), surface(), surface(), second_context_) == EGL_TRUE;
    }

    GLuint NewTexture(GLubyte red, GLubyte green, GLubyte blue) {
        GLuint texture = 0;
        gen_textures_(1, &texture);
        bind_texture_(GL_TEXTURE_2D_, texture);
        tex_parameteri_(GL_TEXTURE_2D_, GL_TEXTURE_MIN_FILTER_, GL_LINEAR_);
        tex_parameteri_(GL_TEXTURE_2D_, GL_TEXTURE_MAG_FILTER_, GL_NEAREST_);
        std::vector<GLubyte> pixels(static_cast<size_t>(kTextureSize) * kTextureSize * 4);
        for (size_t i = 0; i < pixels.size(); i += 4) {
            pixels[i] = red;
            pixels[i + 1] = green;
            pixels[i + 2] = blue;
            pixels[i + 3] = 255;
        }
        tex_image2d_(GL_TEXTURE_2D_, 0, static_cast<GLint>(GL_RGBA8_), kTextureSize, kTextureSize, 0,
                     GL_RGBA_, GL_UNSIGNED_BYTE_, pixels.data());
        return texture;
    }

    // Framebuffers the app owns in the CURRENT context, each with its own
    // texture attached, and the attachment names to check afterwards.
    struct AppFramebuffers {
        std::array<GLuint, kAppFramebuffers> fbos{};
        std::array<GLuint, kAppFramebuffers> textures{};
    };

    AppFramebuffers CreateAppFramebuffers() {
        AppFramebuffers app;
        gen_framebuffers_(kAppFramebuffers, app.fbos.data());
        for (int i = 0; i < kAppFramebuffers; ++i) {
            app.textures[i] = NewTexture(0, 255, 0);
            bind_framebuffer_(GL_FRAMEBUFFER_, app.fbos[i]);
            framebuffer_texture2d_(GL_FRAMEBUFFER_, GL_COLOR_ATTACHMENT0_, GL_TEXTURE_2D_,
                                   app.textures[i], 0);
        }
        bind_framebuffer_(GL_FRAMEBUFFER_, 0);
        return app;
    }

    void ExpectAttachmentsIntact(const AppFramebuffers& app, const char* what) {
        for (int i = 0; i < kAppFramebuffers; ++i) {
            bind_framebuffer_(GL_FRAMEBUFFER_, app.fbos[i]);
            GLint name = -1;
            get_attachment_(GL_FRAMEBUFFER_, GL_COLOR_ATTACHMENT0_,
                            GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME_, &name);
            EXPECT_EQ(static_cast<GLuint>(name), app.textures[i])
                << what << ": framebuffer " << app.fbos[i] << " lost its attachment";
        }
        bind_framebuffer_(GL_FRAMEBUFFER_, 0);
    }

    void Drain() {
        while (get_error_() != GL_NO_ERROR_) {}
    }

    EGLBoolean (*make_current_)(EGLDisplay, EGLSurface, EGLSurface, EGLContext) = nullptr;
    GLenum (*get_error_)() = nullptr;
    void (*gen_textures_)(GLsizei, GLuint*) = nullptr;
    void (*bind_texture_)(GLenum, GLuint) = nullptr;
    void (*tex_image2d_)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum,
                         const void*) = nullptr;
    void (*tex_parameteri_)(GLenum, GLenum, GLint) = nullptr;
    void (*get_tex_image_)(GLenum, GLint, GLenum, GLenum, void*) = nullptr;
    void (*generate_mipmap_)(GLenum) = nullptr;
    void (*gen_framebuffers_)(GLsizei, GLuint*) = nullptr;
    void (*bind_framebuffer_)(GLenum, GLuint) = nullptr;
    void (*framebuffer_texture2d_)(GLenum, GLenum, GLenum, GLuint, GLint) = nullptr;
    void (*get_attachment_)(GLenum, GLenum, GLenum, GLint*) = nullptr;
    EGLContext second_context_ = EGL_NO_CONTEXT;
};

TEST_F(ScratchContextTest, GetTexImageDoesNotReuseAnotherContextsScratchFramebuffer) {
    // Context 1: one readback, purely to make the wrapper allocate its
    // scratch read framebuffer here.
    NewTexture(255, 0, 0);
    std::vector<GLubyte> readback(static_cast<size_t>(kTextureSize) * kTextureSize * 4, 0);
    get_tex_image_(GL_TEXTURE_2D_, 0, GL_RGBA_, GL_UNSIGNED_BYTE_, readback.data());
    Drain();

    if (!MakeSecondContextCurrent()) GTEST_SKIP() << "no second ES3 context";

    const AppFramebuffers app = CreateAppFramebuffers();
    Drain();

    // Context 2's own readback: the scratch framebuffer name from context 1
    // resolves to one of the app's framebuffers here.
    const GLuint source = NewTexture(0, 0, 255);
    std::fill(readback.begin(), readback.end(), 0);
    get_tex_image_(GL_TEXTURE_2D_, 0, GL_RGBA_, GL_UNSIGNED_BYTE_, readback.data());
    EXPECT_EQ(get_error_(), GL_NO_ERROR_) << "readback in the second context";
    EXPECT_NE(source, 0u);
    EXPECT_EQ(readback[2], 255) << "the readback did not return the texture's own texels";

    ExpectAttachmentsIntact(app, "glGetTexImage");
}

TEST_F(ScratchContextTest, MipmapRepairDoesNotReuseAnotherContextsScratchObjects) {
    NewTexture(255, 0, 0);
    generate_mipmap_(GL_TEXTURE_2D_);
    Drain();

    if (!MakeSecondContextCurrent()) GTEST_SKIP() << "no second ES3 context";

    const AppFramebuffers app = CreateAppFramebuffers();
    Drain();

    NewTexture(0, 0, 255);
    generate_mipmap_(GL_TEXTURE_2D_);
    EXPECT_EQ(get_error_(), GL_NO_ERROR_) << "mipmap generation in the second context";

    ExpectAttachmentsIntact(app, "glGenerateMipmap");
}

} // namespace
