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
    EXPECT_TRUE(std::regex_search(fs, std::regex(R"(in highp vec[234] texCoord0;)")))
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

} // namespace
