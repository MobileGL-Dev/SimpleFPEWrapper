// SimpleFPEWrapper - tests/gtest_texture_border_clamp.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// GL_ARB_texture_border_clamp: GL_CLAMP_TO_BORDER + GL_TEXTURE_BORDER_COLOR
// pass through untouched on every backend that has them, but neither is core
// on a GLES 3.0/3.1 backend without GL_EXT_texture_border_clamp or
// GL_OES_texture_border_clamp - so unlike the rest of kDesktopExtensions,
// this one asks sfpewTextureBorderClampSupported() (init.h) rather than
// assuming the wrapper's 3.2-vs-ES-3.0 floor covers it. This machine's real
// backend is ES 3.2 (confirmed via GL_VERSION), so the first two cases below
// exercise the real pass-through path; SFPEW_TEST_FORCE_NO_TEXTURE_BORDER_
// CLAMP is the only way to reach the GL_INVALID_ENUM path without an actual
// ES 3.0/3.1 device.

#include "sfpew_gtest.h"

#include <cstdlib>
#include <optional>

namespace {

using sfpew_test::ContextTest;
using sfpew_test::GLbitfield;
using sfpew_test::GLenum;
using sfpew_test::GLfloat;
using sfpew_test::GLint;
using sfpew_test::GLsizei;
using sfpew_test::GLubyte;
using sfpew_test::GLuint;
using sfpew_test::PixelProbe;

constexpr GLbitfield GL_COLOR_BUFFER_BIT_ = 0x00004000;
constexpr GLenum GL_QUADS_ = 0x0007;
constexpr GLenum GL_TEXTURE_2D_ = 0x0DE1;
constexpr GLenum GL_TEXTURE_MIN_FILTER_ = 0x2801;
constexpr GLenum GL_TEXTURE_MAG_FILTER_ = 0x2800;
constexpr GLenum GL_TEXTURE_WRAP_S_ = 0x2802;
constexpr GLenum GL_TEXTURE_WRAP_T_ = 0x2803;
constexpr GLenum GL_NEAREST_ = 0x2600;
constexpr GLenum GL_RGBA_ = 0x1908;
constexpr GLenum GL_UNSIGNED_BYTE_ = 0x1401;
constexpr GLenum GL_PROJECTION_ = 0x1701;
constexpr GLenum GL_MODELVIEW_ = 0x1700;
constexpr GLenum GL_NO_ERROR_ = 0;
constexpr GLenum GL_INVALID_ENUM_ = 0x0500;
constexpr GLenum GL_CLAMP_TO_BORDER_ = 0x812D;
constexpr GLenum GL_TEXTURE_BORDER_COLOR_ = 0x1004;
constexpr GLenum GL_EXTENSIONS_ = 0x1F03;

constexpr int kSize = 64;

class TextureBorderClampTest : public ContextTest {
protected:
    TextureBorderClampTest() : ContextTest(sfpew_test::Backend::GLES3, kSize) {}

    void SetUp() override {
        ContextTest::SetUp();
        if (::testing::Test::IsSkipped()) return;
        clear_color_ = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glClearColor");
        clear_ = Get<void (*)(GLbitfield)>("glClear");
        begin_ = Get<void (*)(GLenum)>("glBegin");
        end_ = Get<void (*)()>("glEnd");
        color4f_ = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glColor4f");
        vertex2f_ = Get<void (*)(GLfloat, GLfloat)>("glVertex2f");
        tex_coord2f_ = Get<void (*)(GLfloat, GLfloat)>("glTexCoord2f");
        enable_ = Get<void (*)(GLenum)>("glEnable");
        gen_textures_ = Get<void (*)(GLsizei, GLuint*)>("glGenTextures");
        bind_texture_ = Get<void (*)(GLenum, GLuint)>("glBindTexture");
        tex_image2d_ = Get<void (*)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum,
                                    const void*)>("glTexImage2D");
        tex_parameteri_ = Get<void (*)(GLenum, GLenum, GLint)>("glTexParameteri");
        tex_parameterfv_ = Get<void (*)(GLenum, GLenum, const GLfloat*)>("glTexParameterfv");
        get_parameter_i_ = Get<void (*)(GLenum, GLenum, GLint*)>("glGetTexParameteriv");
        get_parameter_f_ = Get<void (*)(GLenum, GLenum, GLfloat*)>("glGetTexParameterfv");
        matrix_mode_ = Get<void (*)(GLenum)>("glMatrixMode");
        load_identity_ = Get<void (*)()>("glLoadIdentity");
        get_error_ = Get<GLenum (*)()>("glGetError");
        get_string_ = Get<const GLubyte* (*)(GLenum)>("glGetString");
        auto read_pixels =
            Get<void (*)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*)>("glReadPixels");
        ASSERT_NE(read_pixels, nullptr);
        probe_.emplace(read_pixels);

        matrix_mode_(GL_PROJECTION_);
        load_identity_();
        matrix_mode_(GL_MODELVIEW_);
        load_identity_();
        clear_color_(0.0f, 0.0f, 0.0f, 0.0f);
        get_error_();
    }

    // A single green texel, small enough that the whole surface is one
    // uniform color, so any non-green, non-black sample can only be the
    // border.
    GLuint UploadGreenTexture() {
        GLuint texture = 0;
        gen_textures_(1, &texture);
        bind_texture_(GL_TEXTURE_2D_, texture);
        tex_parameteri_(GL_TEXTURE_2D_, GL_TEXTURE_MIN_FILTER_, GL_NEAREST_);
        tex_parameteri_(GL_TEXTURE_2D_, GL_TEXTURE_MAG_FILTER_, GL_NEAREST_);
        const GLubyte green[4] = {0, 255, 0, 255};
        tex_image2d_(GL_TEXTURE_2D_, 0, GL_RGBA_, 1, 1, 0, GL_RGBA_, GL_UNSIGNED_BYTE_, green);
        return texture;
    }

    // Quad corners carry texcoords well outside [0,1] so the screen center
    // samples inside the texture (bilinear average of the four corners is
    // (0.5, 0.5)) while the screen corners sample outside it.
    void DrawOutOfRangeQuad() {
        clear_(GL_COLOR_BUFFER_BIT_);
        begin_(GL_QUADS_);
        color4f_(1.0f, 1.0f, 1.0f, 1.0f);
        tex_coord2f_(-1.0f, -1.0f);
        vertex2f_(-1.0f, -1.0f);
        tex_coord2f_(2.0f, -1.0f);
        vertex2f_(1.0f, -1.0f);
        tex_coord2f_(2.0f, 2.0f);
        vertex2f_(1.0f, 1.0f);
        tex_coord2f_(-1.0f, 2.0f);
        vertex2f_(-1.0f, 1.0f);
        end_();
    }

    void (*clear_color_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*clear_)(GLbitfield) = nullptr;
    void (*begin_)(GLenum) = nullptr;
    void (*end_)() = nullptr;
    void (*color4f_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*vertex2f_)(GLfloat, GLfloat) = nullptr;
    void (*tex_coord2f_)(GLfloat, GLfloat) = nullptr;
    void (*enable_)(GLenum) = nullptr;
    void (*gen_textures_)(GLsizei, GLuint*) = nullptr;
    void (*bind_texture_)(GLenum, GLuint) = nullptr;
    void (*tex_image2d_)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum,
                         const void*) = nullptr;
    void (*tex_parameteri_)(GLenum, GLenum, GLint) = nullptr;
    void (*tex_parameterfv_)(GLenum, GLenum, const GLfloat*) = nullptr;
    void (*get_parameter_i_)(GLenum, GLenum, GLint*) = nullptr;
    void (*get_parameter_f_)(GLenum, GLenum, GLfloat*) = nullptr;
    void (*matrix_mode_)(GLenum) = nullptr;
    void (*load_identity_)() = nullptr;
    GLenum (*get_error_)() = nullptr;
    const GLubyte* (*get_string_)(GLenum) = nullptr;
    std::optional<PixelProbe> probe_;
};

TEST_F(TextureBorderClampTest, WrapClampToBorderSamplesBorderColorOutsideZeroOneRange) {
    UploadGreenTexture();
    tex_parameteri_(GL_TEXTURE_2D_, GL_TEXTURE_WRAP_S_, static_cast<GLint>(GL_CLAMP_TO_BORDER_));
    tex_parameteri_(GL_TEXTURE_2D_, GL_TEXTURE_WRAP_T_, static_cast<GLint>(GL_CLAMP_TO_BORDER_));
    const GLfloat magenta[4] = {1.0f, 0.0f, 1.0f, 1.0f};
    tex_parameterfv_(GL_TEXTURE_2D_, GL_TEXTURE_BORDER_COLOR_, magenta);
    ASSERT_EQ(get_error_(), GL_NO_ERROR_) << "setting up clamp-to-border must not itself error";
    enable_(GL_TEXTURE_2D_);

    DrawOutOfRangeQuad();
    ASSERT_EQ(get_error_(), GL_NO_ERROR_);

    const auto center = probe_->At(kSize / 2, kSize / 2);
    EXPECT_GE(center.g, 200) << "screen center's texcoord (0.5, 0.5) must sample the green texel";
    EXPECT_LE(center.r, 40);
    EXPECT_LE(center.b, 40);

    const auto corner = probe_->At(1, 1);
    EXPECT_GE(corner.r, 200) << "a screen corner outside [0,1] must read back the border color";
    EXPECT_LE(corner.g, 40) << "a border-color sample must not be the edge-clamped green texel";
    EXPECT_GE(corner.b, 200);
}

TEST_F(TextureBorderClampTest, BorderColorAndWrapModeRoundTripThroughGetTexParameter) {
    UploadGreenTexture();
    tex_parameteri_(GL_TEXTURE_2D_, GL_TEXTURE_WRAP_S_, static_cast<GLint>(GL_CLAMP_TO_BORDER_));
    const GLfloat cyan[4] = {0.0f, 1.0f, 1.0f, 0.5f};
    tex_parameterfv_(GL_TEXTURE_2D_, GL_TEXTURE_BORDER_COLOR_, cyan);
    ASSERT_EQ(get_error_(), GL_NO_ERROR_);

    GLint wrap_s = -1;
    get_parameter_i_(GL_TEXTURE_2D_, GL_TEXTURE_WRAP_S_, &wrap_s);
    EXPECT_EQ(wrap_s, static_cast<GLint>(GL_CLAMP_TO_BORDER_));

    GLfloat readback[4] = {-1.0f, -1.0f, -1.0f, -1.0f};
    get_parameter_f_(GL_TEXTURE_2D_, GL_TEXTURE_BORDER_COLOR_, readback);
    EXPECT_NEAR(readback[0], 0.0f, 0.01f);
    EXPECT_NEAR(readback[1], 1.0f, 0.01f);
    EXPECT_NEAR(readback[2], 1.0f, 0.01f);
    EXPECT_NEAR(readback[3], 0.5f, 0.01f);
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

TEST_F(TextureBorderClampTest, ExtensionStringAdvertisesArbTextureBorderClampOnThisBackend) {
    // This machine's real backend is ES 3.2 (see file banner): the extension
    // must be present without SFPEW_TEST_FORCE_NO_TEXTURE_BORDER_CLAMP set.
    const GLubyte* extensions = get_string_(GL_EXTENSIONS_);
    ASSERT_NE(extensions, nullptr);
    const std::string joined(reinterpret_cast<const char*>(extensions));
    EXPECT_NE(joined.find("GL_ARB_texture_border_clamp"), std::string::npos);
}

// Separate fixture: the forced-unsupported probe result is cached for the
// life of the process (sfpewTextureBorderClampSupported in
// backend/capabilities.cpp), so
// the environment variable has to be set before any wrapper call reaches it
// - which means before ContextTest::SetUp resolves and touches the backend.
// gtest_discover_tests runs each TEST_F in its own process invocation of
// this binary, so setting it in the constructor (ahead of SetUp) is safe
// without contaminating the cases above.
class TextureBorderClampForcedUnsupportedTest : public ContextTest {
protected:
    TextureBorderClampForcedUnsupportedTest() : ContextTest(sfpew_test::Backend::GLES3, kSize) {
        setenv("SFPEW_TEST_FORCE_NO_TEXTURE_BORDER_CLAMP", "1", 1);
    }

    void SetUp() override {
        ContextTest::SetUp();
        if (::testing::Test::IsSkipped()) return;
        gen_textures_ = Get<void (*)(GLsizei, GLuint*)>("glGenTextures");
        bind_texture_ = Get<void (*)(GLenum, GLuint)>("glBindTexture");
        tex_parameteri_ = Get<void (*)(GLenum, GLenum, GLint)>("glTexParameteri");
        tex_parameterfv_ = Get<void (*)(GLenum, GLenum, const GLfloat*)>("glTexParameterfv");
        get_parameter_i_ = Get<void (*)(GLenum, GLenum, GLint*)>("glGetTexParameteriv");
        get_error_ = Get<GLenum (*)()>("glGetError");
        get_string_ = Get<const GLubyte* (*)(GLenum)>("glGetString");
        get_error_();
    }

    void (*gen_textures_)(GLsizei, GLuint*) = nullptr;
    void (*bind_texture_)(GLenum, GLuint) = nullptr;
    void (*tex_parameteri_)(GLenum, GLenum, GLint) = nullptr;
    void (*tex_parameterfv_)(GLenum, GLenum, const GLfloat*) = nullptr;
    void (*get_parameter_i_)(GLenum, GLenum, GLint*) = nullptr;
    GLenum (*get_error_)() = nullptr;
    const GLubyte* (*get_string_)(GLenum) = nullptr;
};

TEST_F(TextureBorderClampForcedUnsupportedTest,
      ClampToBorderRaisesInvalidEnumAndLeavesWrapModeUnchangedWhenBackendLacksIt) {
    GLuint texture = 0;
    gen_textures_(1, &texture);
    bind_texture_(GL_TEXTURE_2D_, texture);
    get_error_();

    tex_parameteri_(GL_TEXTURE_2D_, GL_TEXTURE_WRAP_S_, static_cast<GLint>(GL_CLAMP_TO_BORDER_));
    EXPECT_EQ(get_error_(), GL_INVALID_ENUM_)
        << "a backend without border-clamp support must not silently accept GL_CLAMP_TO_BORDER";

    // GL_TEXTURE_WRAP_S defaults to GL_REPEAT (0x2901); the rejected call
    // must have left it untouched rather than forwarding the enum.
    GLint wrap_s = -1;
    get_parameter_i_(GL_TEXTURE_2D_, GL_TEXTURE_WRAP_S_, &wrap_s);
    EXPECT_EQ(wrap_s, 0x2901) << "rejected call must not have reached the driver";

    const GLfloat magenta[4] = {1.0f, 0.0f, 1.0f, 1.0f};
    tex_parameterfv_(GL_TEXTURE_2D_, GL_TEXTURE_BORDER_COLOR_, magenta);
    EXPECT_EQ(get_error_(), GL_INVALID_ENUM_)
        << "GL_TEXTURE_BORDER_COLOR itself must be rejected when the backend lacks the extension";
}

TEST_F(TextureBorderClampForcedUnsupportedTest,
      ExtensionStringOmitsArbTextureBorderClampWhenBackendLacksIt) {
    const GLubyte* extensions = get_string_(GL_EXTENSIONS_);
    ASSERT_NE(extensions, nullptr);
    const std::string joined(reinterpret_cast<const char*>(extensions));
    EXPECT_EQ(joined.find("GL_ARB_texture_border_clamp"), std::string::npos)
        << "the string must not claim an extension the forced-unsupported backend lacks";
}

} // namespace
