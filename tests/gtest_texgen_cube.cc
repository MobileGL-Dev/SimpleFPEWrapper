// SimpleFPEWrapper - tests/gtest_texgen_cube.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// defects-plan-2.md 2.6: GL_REFLECTION_MAP/GL_NORMAL_MAP texgen already
// produced a full eye-space 3-component vector - exactly a cube map's
// natural sample coordinate - but unit_uses_texgen/texgen_needs_eye/
// texgen_needs_normal gated on texture_2d_enable only, so a cube (or 3D)
// unit with texgen enabled never activated it and sampled a degenerate
// vec3(0.0) direction instead. Covers GL_NORMAL_MAP (the simplest case to
// predict: the generated coordinate is exactly the transformed normal) and
// GL_REFLECTION_MAP (the eye-to-vertex vector reflected about the normal).

#include "sfpew_gtest.h"

#include <cstring>
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

constexpr GLenum GL_NO_ERROR_ = 0;
constexpr GLenum GL_TEXTURE_CUBE_MAP_ = 0x8513;
constexpr GLenum GL_TEXTURE_CUBE_MAP_POSITIVE_X_ = 0x8515;
constexpr GLenum GL_TEXTURE_MIN_FILTER_ = 0x2801;
constexpr GLenum GL_TEXTURE_MAG_FILTER_ = 0x2800;
constexpr GLenum GL_NEAREST_ = 0x2600;
constexpr GLenum GL_RGBA_ = 0x1908;
constexpr GLenum GL_RGBA8_ = 0x8058;
constexpr GLenum GL_UNSIGNED_BYTE_ = 0x1401;
constexpr GLenum GL_TEXTURE_GEN_S_ = 0x0C60;
constexpr GLenum GL_TEXTURE_GEN_T_ = 0x0C61;
constexpr GLenum GL_TEXTURE_GEN_R_ = 0x0C62;
constexpr GLenum GL_TEXTURE_GEN_MODE_ = 0x2500;
constexpr GLenum GL_S_ = 0x2000;
constexpr GLenum GL_T_ = 0x2001;
constexpr GLenum GL_R_ = 0x2002;
constexpr GLenum GL_NORMAL_MAP_ = 0x8511;
constexpr GLenum GL_REFLECTION_MAP_ = 0x8512;
constexpr GLenum GL_QUADS_ = 0x0007;
constexpr GLenum GL_PROJECTION_ = 0x1701;
constexpr GLenum GL_MODELVIEW_ = 0x1700;
constexpr GLbitfield GL_COLOR_BUFFER_BIT_ = 0x00004000;

constexpr int kSize = 32;

class TexgenCubeTest : public ContextTest {
protected:
    TexgenCubeTest() : ContextTest(sfpew_test::Backend::GLES3, kSize) {}

    void SetUp() override {
        ContextTest::SetUp();
        if (::testing::Test::IsSkipped()) return;
        enable_ = Get<void (*)(GLenum)>("glEnable");
        tex_geni_ = Get<void (*)(GLenum, GLenum, GLint)>("glTexGeni");
        viewport_ = Get<void (*)(GLint, GLint, GLsizei, GLsizei)>("glViewport");
        matrix_mode_ = Get<void (*)(GLenum)>("glMatrixMode");
        load_identity_ = Get<void (*)()>("glLoadIdentity");
        clear_color_ = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glClearColor");
        clear_ = Get<void (*)(GLbitfield)>("glClear");
        begin_ = Get<void (*)(GLenum)>("glBegin");
        end_ = Get<void (*)()>("glEnd");
        color4f_ = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glColor4f");
        normal3f_ = Get<void (*)(GLfloat, GLfloat, GLfloat)>("glNormal3f");
        vertex3f_ = Get<void (*)(GLfloat, GLfloat, GLfloat)>("glVertex3f");
        get_error_ = Get<GLenum (*)()>("glGetError");
        gen_textures_ = Get<void (*)(GLsizei, GLuint*)>("glGenTextures");
        bind_texture_ = Get<void (*)(GLenum, GLuint)>("glBindTexture");
        tex_parameteri_ = Get<void (*)(GLenum, GLenum, GLint)>("glTexParameteri");
        tex_image2d_ = Get<void (*)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum,
                                    const void*)>("glTexImage2D");
        auto read_pixels =
            Get<void (*)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*)>("glReadPixels");
        ASSERT_NE(read_pixels, nullptr);
        probe_.emplace(read_pixels);

        viewport_(0, 0, kSize, kSize);
        matrix_mode_(GL_PROJECTION_);
        load_identity_();
        matrix_mode_(GL_MODELVIEW_);
        load_identity_();
        clear_color_(0.0f, 0.0f, 0.0f, 1.0f);
    }

    // Six faces, six solid distinguishable colors.
    GLuint MakeCubeMap() {
        static constexpr GLubyte kFaceColor[6][4] = {
            {255, 0, 0, 255},   // +X red
            {0, 255, 0, 255},   // -X green
            {0, 0, 255, 255},   // +Y blue
            {255, 255, 0, 255}, // -Y yellow
            {255, 0, 255, 255}, // +Z magenta
            {0, 255, 255, 255}, // -Z cyan
        };
        GLuint texture = 0;
        gen_textures_(1, &texture);
        bind_texture_(GL_TEXTURE_CUBE_MAP_, texture);
        tex_parameteri_(GL_TEXTURE_CUBE_MAP_, GL_TEXTURE_MIN_FILTER_, GL_NEAREST_);
        tex_parameteri_(GL_TEXTURE_CUBE_MAP_, GL_TEXTURE_MAG_FILTER_, GL_NEAREST_);
        for (int face = 0; face < 6; ++face) {
            GLubyte solid[2 * 2 * 4];
            for (int t = 0; t < 4; ++t) memcpy(&solid[t * 4], kFaceColor[face], 4);
            tex_image2d_(static_cast<GLenum>(GL_TEXTURE_CUBE_MAP_POSITIVE_X_ + face), 0, GL_RGBA8_,
                        2, 2, 0, GL_RGBA_, GL_UNSIGNED_BYTE_, solid);
        }
        return texture;
    }

    void EnableNormalMapTexgen() {
        tex_geni_(GL_S_, GL_TEXTURE_GEN_MODE_, static_cast<GLint>(GL_NORMAL_MAP_));
        tex_geni_(GL_T_, GL_TEXTURE_GEN_MODE_, static_cast<GLint>(GL_NORMAL_MAP_));
        tex_geni_(GL_R_, GL_TEXTURE_GEN_MODE_, static_cast<GLint>(GL_NORMAL_MAP_));
        enable_(GL_TEXTURE_GEN_S_);
        enable_(GL_TEXTURE_GEN_T_);
        enable_(GL_TEXTURE_GEN_R_);
    }

    void EnableReflectionMapTexgen() {
        tex_geni_(GL_S_, GL_TEXTURE_GEN_MODE_, static_cast<GLint>(GL_REFLECTION_MAP_));
        tex_geni_(GL_T_, GL_TEXTURE_GEN_MODE_, static_cast<GLint>(GL_REFLECTION_MAP_));
        tex_geni_(GL_R_, GL_TEXTURE_GEN_MODE_, static_cast<GLint>(GL_REFLECTION_MAP_));
        enable_(GL_TEXTURE_GEN_S_);
        enable_(GL_TEXTURE_GEN_T_);
        enable_(GL_TEXTURE_GEN_R_);
    }

    // A full-viewport quad at z=0, every vertex sharing one normal - the
    // uniform normal (not vertex position) is what NORMAL_MAP's generated
    // coordinate depends on, so a flat quad is enough.
    void DrawFullscreenQuad(GLfloat nx, GLfloat ny, GLfloat nz) {
        clear_(GL_COLOR_BUFFER_BIT_);
        color4f_(1.0f, 1.0f, 1.0f, 1.0f);
        normal3f_(nx, ny, nz);
        begin_(GL_QUADS_);
        vertex3f_(-1.0f, -1.0f, 0.0f);
        vertex3f_(1.0f, -1.0f, 0.0f);
        vertex3f_(1.0f, 1.0f, 0.0f);
        vertex3f_(-1.0f, 1.0f, 0.0f);
        end_();
    }

    PixelProbe::Rgba Center() { return probe_->At(kSize / 2, kSize / 2); }

    void (*enable_)(GLenum) = nullptr;
    void (*tex_geni_)(GLenum, GLenum, GLint) = nullptr;
    void (*viewport_)(GLint, GLint, GLsizei, GLsizei) = nullptr;
    void (*matrix_mode_)(GLenum) = nullptr;
    void (*load_identity_)() = nullptr;
    void (*clear_color_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*clear_)(GLbitfield) = nullptr;
    void (*begin_)(GLenum) = nullptr;
    void (*end_)() = nullptr;
    void (*color4f_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*normal3f_)(GLfloat, GLfloat, GLfloat) = nullptr;
    void (*vertex3f_)(GLfloat, GLfloat, GLfloat) = nullptr;
    GLenum (*get_error_)() = nullptr;
    void (*gen_textures_)(GLsizei, GLuint*) = nullptr;
    void (*bind_texture_)(GLenum, GLuint) = nullptr;
    void (*tex_parameteri_)(GLenum, GLenum, GLint) = nullptr;
    void (*tex_image2d_)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum,
                         const void*) = nullptr;
    std::optional<PixelProbe> probe_;
};

TEST_F(TexgenCubeTest, NormalMapSamplesTheFaceMatchingTheVertexNormal) {
    MakeCubeMap();
    enable_(GL_TEXTURE_CUBE_MAP_);
    EnableNormalMapTexgen();

    // GL_NORMAL_MAP's generated coordinate is exactly the (NormalMat-
    // transformed) vertex normal - identity modelview here, so it comes
    // through unchanged and must select the face matching each normal.
    DrawFullscreenQuad(1.0f, 0.0f, 0.0f);
    const auto plus_x = Center();
    EXPECT_GT(plus_x.r, 200) << "+X normal must sample the +X (red) face";
    EXPECT_LT(plus_x.g, 60);
    EXPECT_LT(plus_x.b, 60);

    DrawFullscreenQuad(-1.0f, 0.0f, 0.0f);
    const auto minus_x = Center();
    EXPECT_LT(minus_x.r, 60) << "-X normal must sample the -X (green) face, not +X again";
    EXPECT_GT(minus_x.g, 200);

    DrawFullscreenQuad(0.0f, 0.0f, 1.0f);
    const auto plus_z = Center();
    EXPECT_GT(plus_z.r, 200) << "+Z normal must sample the +Z (magenta) face";
    EXPECT_GT(plus_z.b, 200);
    EXPECT_LT(plus_z.g, 60);

    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

TEST_F(TexgenCubeTest, ReflectionMapSamplesTheReflectedEyeVector) {
    MakeCubeMap();
    enable_(GL_TEXTURE_CUBE_MAP_);
    EnableReflectionMapTexgen();

    // Identity modelview: eyePosition == object-space position, so
    // texgenU = normalize(vertexPos). A quad at z=-1 facing the camera
    // (normal (0,0,1)) reflects the incident vector (0,0,-1) about (0,0,1)
    // to (0,0,1) - the +Z (magenta) face. reflect(I,N) = I - 2*dot(N,I)*N.
    clear_(GL_COLOR_BUFFER_BIT_);
    color4f_(1.0f, 1.0f, 1.0f, 1.0f);
    normal3f_(0.0f, 0.0f, 1.0f);
    begin_(GL_QUADS_);
    vertex3f_(-1.0f, -1.0f, -1.0f);
    vertex3f_(1.0f, -1.0f, -1.0f);
    vertex3f_(1.0f, 1.0f, -1.0f);
    vertex3f_(-1.0f, 1.0f, -1.0f);
    end_();

    const auto reflected = Center();
    EXPECT_GT(reflected.r, 200) << "reflected eye vector must land on the +Z (magenta) face";
    EXPECT_GT(reflected.b, 200);
    EXPECT_LT(reflected.g, 60);
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

} // namespace
