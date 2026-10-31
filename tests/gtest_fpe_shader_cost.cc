// SimpleFPEWrapper - tests/gtest_fpe_shader_cost.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// What a generated fixed-function fragment shader costs the GPU, which the
// pixels it draws never show. On a tiler that is most of a frame: Minecraft
// over the wrapper on a Mali-G77 was GPU-bound at 1.37x gl4es's per-frame
// GPU time for the same image, from two properties checked here.
//
//  - Precision: fragment math defaults to mediump (fp16 runs at twice the
//    rate), while whatever feeds a texture coordinate or a distance stays
//    highp - fp16 puts atlas texel edges on the neighbor.
//  - discard: only a program drawn with GL_ALPHA_TEST enabled carries the
//    test. A discard anywhere in a fragment shader costs the whole draw its
//    early depth test and forward pixel kill on Mali, and opaque terrain
//    drawn with the test off paid that once the game had used it anywhere.
//  - Varyings: every component is written out per vertex and interpolated
//    per fragment on a tiler, so only what the fragment stage reads is
//    passed - texcoords sized to their unit's sampler, no normal, the eye z
//    alone for plane-distance fog.
//
// The source comes from sfpewLastFixedFunctionFragmentSourceForTest: the
// program the last fixed-function draw used. A pixel read in between flushes
// any batched immediate-mode geometry, so "last draw" is the one just made.

#include "sfpew_gtest.h"

#include <regex>
#include <string>

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

constexpr GLenum kQuads = 0x0007;
constexpr GLenum kAlphaTest = 0x0BC0;
constexpr GLenum kFog = 0x0B60;
constexpr GLenum kFogMode = 0x0B65;
constexpr GLenum kFogStart = 0x0B63;
constexpr GLenum kFogEnd = 0x0B64;
constexpr GLenum kLinear = 0x2601;
constexpr GLenum kNearest = 0x2600;
constexpr GLenum kTexture2D = 0x0DE1;
constexpr GLenum kTexMinFilter = 0x2801;
constexpr GLenum kTexMagFilter = 0x2800;
constexpr GLenum kGreater = 0x0204;
constexpr GLenum kLess = 0x0201;
constexpr GLenum kFloat = 0x1406;
constexpr GLenum kVertexArray = 0x8074;
constexpr GLenum kNormalArray = 0x8075;
constexpr GLenum kTexCoordArray = 0x8078;
constexpr GLenum kTexture0 = 0x84C0;
constexpr GLenum kTexture1 = 0x84C1;
constexpr GLenum kFogDistanceModeNV = 0x855A;
constexpr GLenum kEyeRadialNV = 0x855B;

class FpeShaderCostTest : public ContextTest {
protected:
    void SetUp() override {
        ContextTest::SetUp();
        if (::testing::Test::IsSkipped()) return;
        source_ = Dlsym<const char* (*)()>("sfpewLastFixedFunctionFragmentSourceForTest");
        ASSERT_NE(source_, nullptr) << "the wrapper exports no sfpewLastFixedFunctionFragmentSourceForTest";
        clear_color_ = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glClearColor");
        clear_ = Get<void (*)(GLbitfield)>("glClear");
        enable_ = Get<void (*)(GLenum)>("glEnable");
        disable_ = Get<void (*)(GLenum)>("glDisable");
        begin_ = Get<void (*)(GLenum)>("glBegin");
        end_ = Get<void (*)()>("glEnd");
        color4f_ = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glColor4f");
        tex_coord2f_ = Get<void (*)(GLfloat, GLfloat)>("glTexCoord2f");
        vertex2f_ = Get<void (*)(GLfloat, GLfloat)>("glVertex2f");
        alpha_func_ = Get<void (*)(GLenum, GLfloat)>("glAlphaFunc");
        fogi_ = Get<void (*)(GLenum, GLint)>("glFogi");
        fogf_ = Get<void (*)(GLenum, GLfloat)>("glFogf");
        gen_textures_ = Get<void (*)(GLsizei, GLuint*)>("glGenTextures");
        bind_texture_ = Get<void (*)(GLenum, GLuint)>("glBindTexture");
        tex_image_2d_ = Get<void (*)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum,
                                     const void*)>("glTexImage2D");
        tex_parameteri_ = Get<void (*)(GLenum, GLenum, GLint)>("glTexParameteri");
        read_pixels_ = Get<PixelProbe::ReadPixelsFn>("glReadPixels");
        enable_client_state_ = Get<void (*)(GLenum)>("glEnableClientState");
        vertex_pointer_ = Get<void (*)(GLint, GLenum, GLsizei, const void*)>("glVertexPointer");
        normal_pointer_ = Get<void (*)(GLenum, GLsizei, const void*)>("glNormalPointer");
        tex_coord_pointer_ = Get<void (*)(GLint, GLenum, GLsizei, const void*)>("glTexCoordPointer");
        client_active_texture_ = Get<void (*)(GLenum)>("glClientActiveTexture");
        active_texture_ = Get<void (*)(GLenum)>("glActiveTexture");
        draw_arrays_ = Get<void (*)(GLenum, GLint, GLsizei)>("glDrawArrays");
    }

    // A 1x1 texture of one color on the active unit, nearest-filtered.
    void SolidTexture(const GLubyte (&rgba)[4]) {
        GLuint texture = 0;
        gen_textures_(1, &texture);
        bind_texture_(kTexture2D, texture);
        tex_image_2d_(kTexture2D, 0, 0x1908 /* GL_RGBA */, 1, 1, 0, 0x1908, 0x1401 /* GL_UNSIGNED_BYTE */, rgba);
        tex_parameteri_(kTexture2D, kTexMinFilter, kNearest);
        tex_parameteri_(kTexture2D, kTexMagFilter, kNearest);
    }

    // A full-viewport quad in the current color; the pixel read flushes it.
    void DrawQuad(bool textured = false) {
        begin_(kQuads);
        if (textured) tex_coord2f_(0.0f, 0.0f);
        vertex2f_(-1.0f, -1.0f);
        if (textured) tex_coord2f_(1.0f, 0.0f);
        vertex2f_(1.0f, -1.0f);
        if (textured) tex_coord2f_(1.0f, 1.0f);
        vertex2f_(1.0f, 1.0f);
        if (textured) tex_coord2f_(0.0f, 1.0f);
        vertex2f_(-1.0f, 1.0f);
        end_();
    }

    PixelProbe::Rgba Center() { return PixelProbe(read_pixels_).At(size() / 2, size() / 2); }

    std::string Source() {
        const char* source = source_();
        return source != nullptr ? source : "";
    }

    const char* (*source_)() = nullptr;
    void (*clear_color_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*clear_)(GLbitfield) = nullptr;
    void (*enable_)(GLenum) = nullptr;
    void (*disable_)(GLenum) = nullptr;
    void (*begin_)(GLenum) = nullptr;
    void (*end_)() = nullptr;
    void (*color4f_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*tex_coord2f_)(GLfloat, GLfloat) = nullptr;
    void (*vertex2f_)(GLfloat, GLfloat) = nullptr;
    void (*alpha_func_)(GLenum, GLfloat) = nullptr;
    void (*fogi_)(GLenum, GLint) = nullptr;
    void (*fogf_)(GLenum, GLfloat) = nullptr;
    void (*gen_textures_)(GLsizei, GLuint*) = nullptr;
    void (*bind_texture_)(GLenum, GLuint) = nullptr;
    void (*tex_image_2d_)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*) = nullptr;
    void (*tex_parameteri_)(GLenum, GLenum, GLint) = nullptr;
    PixelProbe::ReadPixelsFn read_pixels_ = nullptr;
    void (*enable_client_state_)(GLenum) = nullptr;
    void (*vertex_pointer_)(GLint, GLenum, GLsizei, const void*) = nullptr;
    void (*normal_pointer_)(GLenum, GLsizei, const void*) = nullptr;
    void (*tex_coord_pointer_)(GLint, GLenum, GLsizei, const void*) = nullptr;
    void (*client_active_texture_)(GLenum) = nullptr;
    void (*active_texture_)(GLenum) = nullptr;
    void (*draw_arrays_)(GLenum, GLint, GLsizei) = nullptr;
};

TEST_F(FpeShaderCostTest, FragmentMathIsMediumpWhileCoordinatesStayHighp) {
    const GLubyte green[4] = {0, 255, 0, 255};
    GLuint texture = 0;
    gen_textures_(1, &texture);
    bind_texture_(kTexture2D, texture);
    tex_image_2d_(kTexture2D, 0, 0x1908 /* GL_RGBA */, 1, 1, 0, 0x1908, 0x1401 /* GL_UNSIGNED_BYTE */, green);
    tex_parameteri_(kTexture2D, kTexMinFilter, kNearest);
    tex_parameteri_(kTexture2D, kTexMagFilter, kNearest);
    enable_(kTexture2D);
    enable_(kFog);
    fogi_(kFogMode, kLinear);
    fogf_(kFogStart, 100.0f);
    fogf_(kFogEnd, 200.0f);

    clear_color_(0.0f, 0.0f, 0.0f, 1.0f);
    clear_(0x00004000 /* GL_COLOR_BUFFER_BIT */);
    color4f_(1.0f, 1.0f, 1.0f, 1.0f);
    DrawQuad(/*textured=*/true);
    EXPECT_GT(Center().g, 200) << "the textured, fogged quad did not draw its texel";

    const std::string fs = Source();
    ASSERT_FALSE(fs.empty()) << "no fixed-function program recorded for the draw";
    EXPECT_NE(fs.find("precision mediump float;"), std::string::npos)
        << "fragment math is not mediump by default:\n" << fs;
    EXPECT_TRUE(std::regex_search(fs, std::regex(R"(in highp vec2 texCoord0;)")))
        << "the texture coordinate lost highp under the mediump default:\n" << fs;
    EXPECT_NE(fs.find("highp float distance"), std::string::npos)
        << "the fog distance lost highp under the mediump default:\n" << fs;
}

TEST_F(FpeShaderCostTest, OnlyAlphaTestedDrawsCarryADiscard) {
    color4f_(0.0f, 1.0f, 0.0f, 1.0f);
    enable_(kAlphaTest);
    alpha_func_(kGreater, 0.5f);
    DrawQuad();
    (void)Center();
    const char* alpha_tested = source_();
    ASSERT_NE(alpha_tested, nullptr);
    EXPECT_NE(std::string(alpha_tested).find("discard"), std::string::npos)
        << "the alpha-tested draw carries no discard";

    // The comparison is a uniform: a new function reuses the same program.
    alpha_func_(kLess, 0.25f);
    DrawQuad();
    (void)Center();
    EXPECT_EQ(source_(), alpha_tested) << "glAlphaFunc minted a new program";

    // The case that cost the GPU: the test used once, then off.
    disable_(kAlphaTest);
    DrawQuad();
    (void)Center();
    EXPECT_EQ(Source().find("discard"), std::string::npos)
        << "a draw with GL_ALPHA_TEST off still carries the discard:\n" << Source();
}

TEST_F(FpeShaderCostTest, AlphaTestStillRejectsAndPassesAcrossToggles) {
    clear_color_(0.0f, 0.0f, 0.0f, 1.0f);
    color4f_(0.0f, 1.0f, 0.0f, 0.25f);

    enable_(kAlphaTest);
    alpha_func_(kGreater, 0.5f);
    clear_(0x00004000);
    DrawQuad();
    EXPECT_LT(Center().g, 20) << "alpha 0.25 passed GL_GREATER 0.5";

    alpha_func_(kLess, 0.5f);
    clear_(0x00004000);
    DrawQuad();
    EXPECT_GT(Center().g, 200) << "alpha 0.25 failed GL_LESS 0.5";

    disable_(kAlphaTest);
    alpha_func_(kGreater, 0.5f);
    clear_(0x00004000);
    DrawQuad();
    EXPECT_GT(Center().g, 200) << "a disabled alpha test still rejected the quad";

    enable_(kAlphaTest);
    clear_(0x00004000);
    DrawQuad();
    EXPECT_LT(Center().g, 20) << "re-enabling the alpha test did not reject again";
}

// Minecraft's terrain draw, in the shape that matters here: client arrays
// with a normal, a 2D texture on unit 0, a coordinate array on a unit whose
// texture is off, and plane-distance fog.
TEST_F(FpeShaderCostTest, VaryingsCarryOnlyWhatTheFragmentStageReads) {
    const GLubyte green[4] = {0, 255, 0, 255};
    active_texture_(kTexture0);
    SolidTexture(green);
    enable_(kTexture2D);
    enable_(kFog);
    fogi_(kFogMode, kLinear);
    fogf_(kFogStart, 100.0f);
    fogf_(kFogEnd, 200.0f);

    static const GLfloat positions[] = {-1, -1, 1, -1, 1, 1, -1, 1};
    static const GLfloat normals[] = {0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1};
    static const GLfloat coords[] = {0, 0, 1, 0, 1, 1, 0, 1};
    enable_client_state_(kVertexArray);
    vertex_pointer_(2, kFloat, 0, positions);
    enable_client_state_(kNormalArray);
    normal_pointer_(kFloat, 0, normals);
    client_active_texture_(kTexture0);
    enable_client_state_(kTexCoordArray);
    tex_coord_pointer_(2, kFloat, 0, coords);
    client_active_texture_(kTexture1);
    enable_client_state_(kTexCoordArray);
    tex_coord_pointer_(2, kFloat, 0, coords);

    clear_color_(0.0f, 0.0f, 0.0f, 1.0f);
    clear_(0x00004000);
    color4f_(1.0f, 1.0f, 1.0f, 1.0f);
    draw_arrays_(kQuads, 0, 4);
    EXPECT_GT(Center().g, 200) << "the terrain-shaped draw did not draw its texel";

    std::string fs = Source();
    ASSERT_FALSE(fs.empty());
    EXPECT_NE(fs.find("in highp vec2 texCoord0;"), std::string::npos)
        << "a 2D unit's texcoord is not a highp vec2:\n" << fs;
    EXPECT_EQ(fs.find("texCoord1"), std::string::npos)
        << "a unit with its texture off still receives a texcoord:\n" << fs;
    EXPECT_EQ(fs.find("vertexNormal"), std::string::npos)
        << "the fragment stage receives a normal it never reads:\n" << fs;
    EXPECT_NE(fs.find("in highp float vEyeZ;"), std::string::npos)
        << "plane-distance fog does not interpolate the eye z alone:\n" << fs;
    EXPECT_EQ(fs.find("vViewPosition"), std::string::npos)
        << "plane-distance fog still carries the eye position:\n" << fs;

    // Radial distance is not affine: it needs the position per fragment.
    fogi_(kFogDistanceModeNV, kEyeRadialNV);
    clear_(0x00004000);
    draw_arrays_(kQuads, 0, 4);
    EXPECT_GT(Center().g, 200);
    fs = Source();
    EXPECT_NE(fs.find("in highp vec3 vViewPosition;"), std::string::npos)
        << "radial fog lost the per-fragment eye position:\n" << fs;
}

} // namespace
