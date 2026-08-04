// SimpleFPEWrapper - tests/gtest_copypixels_depth.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

#include "sfpew_gtest.h"

namespace {

using sfpew_test::ContextTest;
using sfpew_test::GLbitfield;
using sfpew_test::GLboolean;
using sfpew_test::GLenum;
using sfpew_test::GLfloat;
using sfpew_test::GLint;
using sfpew_test::GLsizei;
using sfpew_test::GLubyte;
using sfpew_test::GLuint;
using sfpew_test::LibraryTest;
using sfpew_test::PixelProbe;

constexpr GLenum GL_NO_ERROR_ = 0;
constexpr GLenum GL_INVALID_ENUM_ = 0x0500;
constexpr GLenum GL_INVALID_VALUE_ = 0x0501;
constexpr GLenum GL_INVALID_OPERATION_ = 0x0502;
constexpr GLbitfield GL_COLOR_BUFFER_BIT_ = 0x00004000;
constexpr GLbitfield GL_DEPTH_BUFFER_BIT_ = 0x00000100;
constexpr GLbitfield GL_STENCIL_BUFFER_BIT_ = 0x00000400;
constexpr GLenum GL_SCISSOR_TEST_ = 0x0C11;
constexpr GLenum GL_DEPTH_TEST_ = 0x0B71;
constexpr GLenum GL_LESS_ = 0x0201;
constexpr GLenum GL_QUADS_ = 0x0007;
constexpr GLenum GL_DEPTH_ = 0x1801;
constexpr GLenum GL_STENCIL_ = 0x1802;
constexpr GLenum GL_STENCIL_INDEX_ = 0x1901;
constexpr GLenum GL_UNSIGNED_BYTE_ = 0x1401;
constexpr GLenum GL_FRAMEBUFFER_ = 0x8D40;
constexpr GLenum GL_READ_FRAMEBUFFER_BINDING_ = 0x8CAA;
constexpr GLenum GL_DRAW_FRAMEBUFFER_BINDING_ = 0x8CA6;
constexpr GLenum GL_FRAMEBUFFER_COMPLETE_ = 0x8CD5;
constexpr GLenum GL_COLOR_ATTACHMENT0_ = 0x8CE0;
constexpr GLenum GL_DEPTH_STENCIL_ATTACHMENT_ = 0x821A;
constexpr GLenum GL_RENDERBUFFER_ = 0x8D41;
constexpr GLenum GL_RENDERBUFFER_BINDING_ = 0x8CA7;
constexpr GLenum GL_DEPTH24_STENCIL8_ = 0x88F0;
constexpr GLenum GL_TEXTURE_2D_ = 0x0DE1;
constexpr GLenum GL_RGBA_ = 0x1908;
constexpr GLenum GL_RGBA8_ = 0x8058;
constexpr GLenum GL_TEXTURE_MIN_FILTER_ = 0x2801;
constexpr GLenum GL_TEXTURE_MAG_FILTER_ = 0x2800;
constexpr GLenum GL_NEAREST_ = 0x2600;
constexpr GLenum GL_MAP_STENCIL_ = 0x0D11;
constexpr GLenum GL_PIXEL_MAP_S_TO_S_ = 0x0C71;
constexpr GLenum GL_DEPTH_BIAS_ = 0x0D1F;

class CopyPixelsLibraryTest : public LibraryTest {};

TEST_F(CopyPixelsLibraryTest, ArgumentValidationNeedsNoContext) {
    auto copy_pixels = Get<void (*)(GLint, GLint, GLsizei, GLsizei, GLenum)>("glCopyPixels");
    auto get_error = Get<GLenum (*)()>("glGetError");

    copy_pixels(0, 0, -1, 1, GL_DEPTH_);
    EXPECT_EQ(get_error(), GL_INVALID_VALUE_);
    copy_pixels(0, 0, 1, 1, 0xDEAD);
    EXPECT_EQ(get_error(), GL_INVALID_ENUM_);
}

class CopyPixelsDepthTest : public ContextTest {
protected:
    void SetUp() override {
        ContextTest::SetUp();
        if (::testing::Test::IsSkipped() || ::testing::Test::HasFatalFailure()) return;
        using MakeCurrentFn = EGLBoolean (*)(EGLDisplay, EGLSurface, EGLSurface, EGLContext);
        auto wrapper_make_current = Get<MakeCurrentFn>("eglMakeCurrent");
        ASSERT_TRUE(wrapper_make_current(display(), surface(), surface(), eglGetCurrentContext()));

        get_error_ = Get<GLenum (*)()>("glGetError");
        viewport_ = Get<void (*)(GLint, GLint, GLsizei, GLsizei)>("glViewport");
        enable_ = Get<void (*)(GLenum)>("glEnable");
        disable_ = Get<void (*)(GLenum)>("glDisable");
        scissor_ = Get<void (*)(GLint, GLint, GLsizei, GLsizei)>("glScissor");
        clear_color_ = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glClearColor");
        clear_depth_ = Get<void (*)(GLfloat)>("glClearDepthf");
        clear_stencil_ = Get<void (*)(GLint)>("glClearStencil");
        stencil_mask_ = Get<void (*)(GLuint)>("glStencilMask");
        clear_ = Get<void (*)(GLbitfield)>("glClear");
        depth_func_ = Get<void (*)(GLenum)>("glDepthFunc");
        depth_mask_ = Get<void (*)(GLboolean)>("glDepthMask");
        begin_ = Get<void (*)(GLenum)>("glBegin");
        end_ = Get<void (*)()>("glEnd");
        color4f_ = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glColor4f");
        vertex3f_ = Get<void (*)(GLfloat, GLfloat, GLfloat)>("glVertex3f");
        window_pos_ = Get<void (*)(GLint, GLint)>("glWindowPos2i");
        pixel_zoom_ = Get<void (*)(GLfloat, GLfloat)>("glPixelZoom");
        copy_pixels_ = Get<void (*)(GLint, GLint, GLsizei, GLsizei, GLenum)>("glCopyPixels");
        read_pixels_ = Get<void (*)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*)>(
            "glReadPixels");
        get_integer_ = Get<void (*)(GLenum, GLint*)>("glGetIntegerv");
        pixel_transferf_ = Get<void (*)(GLenum, GLfloat)>("glPixelTransferf");
        pixel_transferi_ = Get<void (*)(GLenum, GLint)>("glPixelTransferi");
        pixel_mapuiv_ = Get<void (*)(GLenum, GLsizei, const GLuint*)>("glPixelMapuiv");

        gen_framebuffers_ = Get<void (*)(GLsizei, GLuint*)>("glGenFramebuffers");
        delete_framebuffers_ = Get<void (*)(GLsizei, const GLuint*)>("glDeleteFramebuffers");
        bind_framebuffer_ = Get<void (*)(GLenum, GLuint)>("glBindFramebuffer");
        check_framebuffer_ = Get<GLenum (*)(GLenum)>("glCheckFramebufferStatus");
        framebuffer_texture_ =
            Get<void (*)(GLenum, GLenum, GLenum, GLuint, GLint)>("glFramebufferTexture2D");
        framebuffer_renderbuffer_ =
            Get<void (*)(GLenum, GLenum, GLenum, GLuint)>("glFramebufferRenderbuffer");
        gen_renderbuffers_ = Get<void (*)(GLsizei, GLuint*)>("glGenRenderbuffers");
        delete_renderbuffers_ = Get<void (*)(GLsizei, const GLuint*)>("glDeleteRenderbuffers");
        bind_renderbuffer_ = Get<void (*)(GLenum, GLuint)>("glBindRenderbuffer");
        renderbuffer_storage_ =
            Get<void (*)(GLenum, GLenum, GLsizei, GLsizei)>("glRenderbufferStorage");
        gen_textures_ = Get<void (*)(GLsizei, GLuint*)>("glGenTextures");
        delete_textures_ = Get<void (*)(GLsizei, const GLuint*)>("glDeleteTextures");
        bind_texture_ = Get<void (*)(GLenum, GLuint)>("glBindTexture");
        tex_image_ = Get<void (*)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum,
                                  const void*)>("glTexImage2D");
        tex_parameter_ = Get<void (*)(GLenum, GLenum, GLint)>("glTexParameteri");

        viewport_(0, 0, size(), size());
        pixel_zoom_(1.0f, 1.0f);
        disable_(GL_SCISSOR_TEST_);
    }

    // No DepthAt() helper: glReadPixels(GL_DEPTH_COMPONENT) comes back
    // GL_INVALID_OPERATION on this driver regardless of which framebuffer is
    // bound (verified independently), so depth is checked indirectly - see
    // OverlappingDepthCopyUsesStableSnapshotAndRestoresBindings below.

    GLubyte StencilAt(int x, int y) const {
        GLubyte value = 0;
        read_pixels_(x, y, 1, 1, GL_STENCIL_INDEX_, GL_UNSIGNED_BYTE_, &value);
        return value;
    }

    void ClearDepthRect(GLint x, GLint y, GLsizei width, GLsizei height, GLfloat value) const {
        enable_(GL_SCISSOR_TEST_);
        scissor_(x, y, width, height);
        clear_depth_(value);
        clear_(GL_DEPTH_BUFFER_BIT_);
        disable_(GL_SCISSOR_TEST_);
    }

    void ClearStencilRect(GLint x, GLint y, GLsizei width, GLsizei height, GLint value) const {
        enable_(GL_SCISSOR_TEST_);
        scissor_(x, y, width, height);
        clear_stencil_(value);
        clear_(GL_STENCIL_BUFFER_BIT_);
        disable_(GL_SCISSOR_TEST_);
    }

    GLenum (*get_error_)() = nullptr;
    void (*viewport_)(GLint, GLint, GLsizei, GLsizei) = nullptr;
    void (*enable_)(GLenum) = nullptr;
    void (*disable_)(GLenum) = nullptr;
    void (*scissor_)(GLint, GLint, GLsizei, GLsizei) = nullptr;
    void (*clear_color_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*clear_depth_)(GLfloat) = nullptr;
    void (*clear_stencil_)(GLint) = nullptr;
    void (*stencil_mask_)(GLuint) = nullptr;
    void (*clear_)(GLbitfield) = nullptr;
    void (*depth_func_)(GLenum) = nullptr;
    void (*depth_mask_)(GLboolean) = nullptr;
    void (*begin_)(GLenum) = nullptr;
    void (*end_)() = nullptr;
    void (*color4f_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*vertex3f_)(GLfloat, GLfloat, GLfloat) = nullptr;
    void (*window_pos_)(GLint, GLint) = nullptr;
    void (*pixel_zoom_)(GLfloat, GLfloat) = nullptr;
    void (*copy_pixels_)(GLint, GLint, GLsizei, GLsizei, GLenum) = nullptr;
    void (*read_pixels_)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*) = nullptr;
    void (*get_integer_)(GLenum, GLint*) = nullptr;
    void (*gen_framebuffers_)(GLsizei, GLuint*) = nullptr;
    void (*delete_framebuffers_)(GLsizei, const GLuint*) = nullptr;
    void (*bind_framebuffer_)(GLenum, GLuint) = nullptr;
    GLenum (*check_framebuffer_)(GLenum) = nullptr;
    void (*framebuffer_texture_)(GLenum, GLenum, GLenum, GLuint, GLint) = nullptr;
    void (*framebuffer_renderbuffer_)(GLenum, GLenum, GLenum, GLuint) = nullptr;
    void (*gen_renderbuffers_)(GLsizei, GLuint*) = nullptr;
    void (*delete_renderbuffers_)(GLsizei, const GLuint*) = nullptr;
    void (*bind_renderbuffer_)(GLenum, GLuint) = nullptr;
    void (*renderbuffer_storage_)(GLenum, GLenum, GLsizei, GLsizei) = nullptr;
    void (*gen_textures_)(GLsizei, GLuint*) = nullptr;
    void (*delete_textures_)(GLsizei, const GLuint*) = nullptr;
    void (*bind_texture_)(GLenum, GLuint) = nullptr;
    void (*tex_image_)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum,
                       const void*) = nullptr;
    void (*tex_parameter_)(GLenum, GLenum, GLint) = nullptr;
    void (*pixel_transferf_)(GLenum, GLfloat) = nullptr;
    void (*pixel_transferi_)(GLenum, GLint) = nullptr;
    void (*pixel_mapuiv_)(GLenum, GLsizei, const GLuint*) = nullptr;
};

TEST_F(CopyPixelsDepthTest, OverlappingDepthCopyUsesStableSnapshotAndRestoresBindings) {
    // glReadPixels(GL_DEPTH_COMPONENT) comes back GL_INVALID_OPERATION on
    // this driver on every framebuffer - default or FBO-backed alike
    // (verified independently: the blit itself completes with no error and
    // a GL_FRAMEBUFFER_COMPLETE destination, only the readback format is
    // rejected). Depth is instead checked the way an application actually
    // observes it: render a probe quad through the ordinary depth-tested
    // pipeline at a window depth between the two copied values (0.2 and
    // 0.8) and see which region it wins in color.
    clear_color_(0.0f, 0.0f, 0.0f, 1.0f);
    clear_depth_(1.0f);
    clear_(GL_COLOR_BUFFER_BIT_ | GL_DEPTH_BUFFER_BIT_);
    ClearDepthRect(4, 4, 8, 16, 0.2f);
    ClearDepthRect(12, 4, 8, 16, 0.8f);

    window_pos_(8, 4);
    copy_pixels_(4, 4, 16, 16, GL_DEPTH_);
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);

    GLint read_fbo = -1, draw_fbo = -1;
    get_integer_(GL_READ_FRAMEBUFFER_BINDING_, &read_fbo);
    get_integer_(GL_DRAW_FRAMEBUFFER_BINDING_, &draw_fbo);
    EXPECT_EQ(read_fbo, 0);
    EXPECT_EQ(draw_fbo, 0);

    // With the default depth range [0,1] and identity matrices, window depth
    // is (ndc.z + 1) / 2, the same convention glCopyPixels' own quad path
    // uses. ndc.z=0 lands at window depth 0.5: it must lose (leave black)
    // over the 0.2 region and win (paint white) over the 0.8 region.
    enable_(GL_DEPTH_TEST_);
    depth_func_(GL_LESS_);
    depth_mask_(sfpew_test::GL_TRUE_);
    color4f_(1.0f, 1.0f, 1.0f, 1.0f);
    begin_(GL_QUADS_);
    vertex3f_(-1.0f, -1.0f, 0.0f);
    vertex3f_(1.0f, -1.0f, 0.0f);
    vertex3f_(1.0f, 1.0f, 0.0f);
    vertex3f_(-1.0f, 1.0f, 0.0f);
    end_();
    const auto probe = sfpew_test::PixelProbe(read_pixels_);
    const auto shallow = probe.At(10, 10); // copied depth 0.2: probe loses, stays black
    const auto deep = probe.At(20, 10);    // copied depth 0.8: probe wins, turns white
    EXPECT_LT(shallow.r, 40);
    EXPECT_GT(deep.r, 200);
    disable_(GL_DEPTH_TEST_);
}

TEST_F(CopyPixelsDepthTest, StencilCopyWorksOnPackedAttachmentAndMissingPlaneIsExplicit) {
    GLuint texture = 0, renderbuffer = 0, framebuffer = 0;
    gen_textures_(1, &texture);
    bind_texture_(GL_TEXTURE_2D_, texture);
    tex_parameter_(GL_TEXTURE_2D_, GL_TEXTURE_MIN_FILTER_, GL_NEAREST_);
    tex_parameter_(GL_TEXTURE_2D_, GL_TEXTURE_MAG_FILTER_, GL_NEAREST_);
    tex_image_(GL_TEXTURE_2D_, 0, GL_RGBA8_, size(), size(), 0, GL_RGBA_, GL_UNSIGNED_BYTE_,
               nullptr);

    gen_renderbuffers_(1, &renderbuffer);
    bind_renderbuffer_(GL_RENDERBUFFER_, renderbuffer);
    renderbuffer_storage_(GL_RENDERBUFFER_, GL_DEPTH24_STENCIL8_, size(), size());
    gen_framebuffers_(1, &framebuffer);
    bind_framebuffer_(GL_FRAMEBUFFER_, framebuffer);
    framebuffer_texture_(GL_FRAMEBUFFER_, GL_COLOR_ATTACHMENT0_, GL_TEXTURE_2D_, texture, 0);
    framebuffer_renderbuffer_(GL_FRAMEBUFFER_, GL_DEPTH_STENCIL_ATTACHMENT_, GL_RENDERBUFFER_,
                              renderbuffer);
    ASSERT_EQ(check_framebuffer_(GL_FRAMEBUFFER_), GL_FRAMEBUFFER_COMPLETE_);

    stencil_mask_(0xFFu);
    clear_stencil_(1);
    clear_(GL_COLOR_BUFFER_BIT_ | GL_DEPTH_BUFFER_BIT_ | GL_STENCIL_BUFFER_BIT_);
    ClearStencilRect(4, 4, 8, 16, 3);
    ClearStencilRect(12, 4, 8, 16, 7);
    window_pos_(8, 4);
    copy_pixels_(4, 4, 16, 16, GL_STENCIL_);
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
    EXPECT_EQ(StencilAt(10, 10), 3);
    EXPECT_EQ(StencilAt(20, 10), 7);

    GLint bound_fbo = 0, bound_renderbuffer = 0;
    get_integer_(GL_DRAW_FRAMEBUFFER_BINDING_, &bound_fbo);
    get_integer_(GL_RENDERBUFFER_BINDING_, &bound_renderbuffer);
    EXPECT_EQ(bound_fbo, static_cast<GLint>(framebuffer));
    EXPECT_EQ(bound_renderbuffer, static_cast<GLint>(renderbuffer));

    framebuffer_renderbuffer_(GL_FRAMEBUFFER_, GL_DEPTH_STENCIL_ATTACHMENT_, GL_RENDERBUFFER_, 0);
    window_pos_(32, 32);
    copy_pixels_(0, 0, 1, 1, GL_STENCIL_);
    EXPECT_EQ(get_error_(), GL_INVALID_OPERATION_);

    bind_framebuffer_(GL_FRAMEBUFFER_, 0);
    bind_renderbuffer_(GL_RENDERBUFFER_, 0);
    bind_texture_(GL_TEXTURE_2D_, 0);
    delete_framebuffers_(1, &framebuffer);
    delete_renderbuffers_(1, &renderbuffer);
    delete_textures_(1, &texture);
}

// defects-plan-3.md: glCopyPixels(GL_DEPTH) + GL_DEPTH_BIAS. Same driver
// limitation as OverlappingDepthCopyUsesStableSnapshotAndRestoresBindings
// above rules out a direct DepthAt() readback, so this checks the same way:
// a depth-tested probe quad at a window depth between the biased and the
// (bug) unbiased outcome. A bias that got silently skipped - the actual
// regression this guards against - copies the raw 0.9 through unchanged
// and the probe would incorrectly win instead of lose.
TEST_F(CopyPixelsDepthTest, DepthCopyAppliesBias) {
    ASSERT_NE(pixel_transferf_, nullptr);
    clear_color_(0.0f, 0.0f, 0.0f, 1.0f);
    clear_depth_(1.0f);
    clear_(GL_COLOR_BUFFER_BIT_ | GL_DEPTH_BUFFER_BIT_);
    ClearDepthRect(4, 4, 16, 16, 0.9f);

    pixel_transferf_(GL_DEPTH_BIAS_, -0.5f); // correctly-copied depth: 0.4
    window_pos_(40, 4);
    copy_pixels_(4, 4, 16, 16, GL_DEPTH_);
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
    pixel_transferf_(GL_DEPTH_BIAS_, 0.0f);

    enable_(GL_DEPTH_TEST_);
    depth_func_(GL_LESS_);
    depth_mask_(sfpew_test::GL_TRUE_);
    color4f_(1.0f, 1.0f, 1.0f, 1.0f);
    begin_(GL_QUADS_);
    vertex3f_(-1.0f, -1.0f, 0.0f);
    vertex3f_(1.0f, -1.0f, 0.0f);
    vertex3f_(1.0f, 1.0f, 0.0f);
    vertex3f_(-1.0f, 1.0f, 0.0f);
    end_();
    // ndc.z=0 -> window depth 0.5: must lose (0.5 is not < 0.4) and leave
    // the copied region black. Only the (unbiased, buggy) 0.9 outcome
    // would let it win here.
    const auto probe = sfpew_test::PixelProbe(read_pixels_);
    const auto copied = probe.At(48, 12);
    EXPECT_LT(copied.r, 40);
    disable_(GL_DEPTH_TEST_);
}

// defects-plan-3.md: glCopyPixels(GL_STENCIL) + GL_MAP_STENCIL, mirroring
// StencilCopyWorksOnPackedAttachmentAndMissingPlaneIsExplicit's FBO setup.
// One copy runs with the map disabled (the untouched fast blit path must
// keep passing raw values through) and a second with it enabled, matching
// GL_PIXEL_MAP_S_TO_S's raw-index-lookup convention already established
// for glDrawPixels(GL_STENCIL_INDEX) in gtest_drawpixels_stencil.cc.
TEST_F(CopyPixelsDepthTest, StencilCopyAppliesMapStencilAndDefaultPathStaysRaw) {
    ASSERT_NE(pixel_transferi_, nullptr);
    ASSERT_NE(pixel_mapuiv_, nullptr);
    GLuint texture = 0, renderbuffer = 0, framebuffer = 0;
    gen_textures_(1, &texture);
    bind_texture_(GL_TEXTURE_2D_, texture);
    tex_parameter_(GL_TEXTURE_2D_, GL_TEXTURE_MIN_FILTER_, GL_NEAREST_);
    tex_parameter_(GL_TEXTURE_2D_, GL_TEXTURE_MAG_FILTER_, GL_NEAREST_);
    tex_image_(GL_TEXTURE_2D_, 0, GL_RGBA8_, size(), size(), 0, GL_RGBA_, GL_UNSIGNED_BYTE_,
               nullptr);

    gen_renderbuffers_(1, &renderbuffer);
    bind_renderbuffer_(GL_RENDERBUFFER_, renderbuffer);
    renderbuffer_storage_(GL_RENDERBUFFER_, GL_DEPTH24_STENCIL8_, size(), size());
    gen_framebuffers_(1, &framebuffer);
    bind_framebuffer_(GL_FRAMEBUFFER_, framebuffer);
    framebuffer_texture_(GL_FRAMEBUFFER_, GL_COLOR_ATTACHMENT0_, GL_TEXTURE_2D_, texture, 0);
    framebuffer_renderbuffer_(GL_FRAMEBUFFER_, GL_DEPTH_STENCIL_ATTACHMENT_, GL_RENDERBUFFER_,
                              renderbuffer);
    ASSERT_EQ(check_framebuffer_(GL_FRAMEBUFFER_), GL_FRAMEBUFFER_COMPLETE_);

    stencil_mask_(0xFFu);
    clear_stencil_(0);
    clear_(GL_COLOR_BUFFER_BIT_ | GL_DEPTH_BUFFER_BIT_ | GL_STENCIL_BUFFER_BIT_);
    ClearStencilRect(4, 4, 16, 16, 3);

    window_pos_(40, 4);
    copy_pixels_(4, 4, 16, 16, GL_STENCIL_);
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
    EXPECT_EQ(StencilAt(48, 12), 3) << "map disabled: the fast blit path must stay untouched";

    const GLuint table[8] = {0, 1, 2, 0x99u, 4, 5, 6, 7};
    pixel_mapuiv_(GL_PIXEL_MAP_S_TO_S_, 8, table);
    pixel_transferi_(GL_MAP_STENCIL_, 1);
    window_pos_(40, 24);
    copy_pixels_(4, 4, 16, 16, GL_STENCIL_);
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
    pixel_transferi_(GL_MAP_STENCIL_, 0);
    EXPECT_EQ(StencilAt(48, 32), 0x99u) << "map enabled: index 3 must remap through the table";

    bind_framebuffer_(GL_FRAMEBUFFER_, 0);
    bind_renderbuffer_(GL_RENDERBUFFER_, 0);
    bind_texture_(GL_TEXTURE_2D_, 0);
    delete_framebuffers_(1, &framebuffer);
    delete_renderbuffers_(1, &renderbuffer);
    delete_textures_(1, &texture);
}

} // namespace
