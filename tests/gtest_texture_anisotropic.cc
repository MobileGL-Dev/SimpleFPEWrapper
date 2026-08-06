// SimpleFPEWrapper - tests/gtest_texture_anisotropic.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// GL_EXT_texture_filter_anisotropic: GL_TEXTURE_MAX_ANISOTROPY_EXT is a real
// GLES3/GL-core sampler parameter the backend already accepts and stores on
// the texture object itself - like GL_TEXTURE_COMPARE_MODE, and unlike
// GL_TEXTURE_PRIORITY/GL_DEPTH_TEXTURE_MODE, which the backend has no storage
// for at all. glTexParameter*'s unconditional passthrough for unrecognized
// pnames (ordered_passthrough.cpp) already forwards it to the driver, and
// glGetTexParameter{f,i}v/glGetFloatv's unconditional passthrough for
// unrecognized pnames (ordered_passthrough.cpp, fpe/ffp_state_query.cpp)
// already answers it back. This file adds
// no wrapper state and no wrapper-side query code for this extension - it
// exists to prove the passthrough claim rather than to exercise anything the
// wrapper itself does.
//
// The extension string is the same story: this extension is not part of the
// wrapper's guaranteed floor (GLES3.0+/GL3.2+ core - it reached core status
// only in GL4.6/ARB_texture_filter_anisotropic), so it is deliberately absent
// from getter_version_strings.cpp's kDesktopExtensions - unlike every other entry there,
// which the wrapper's floor guarantees regardless of the specific device.
// glGetString(GL_EXTENSIONS)'s additive join forwards the backend's own
// string verbatim before appending kDesktopExtensions, so this name reaches
// the caller exactly when - and only when - the real backend advertises it
// itself. That is the same "check the extension string first" contract every
// GL implementation without the extension relies on; advertising it
// unconditionally would be lying on any backend without real anisotropic
// filtering hardware/driver support.
//
// Every test below skips (not fails) on a backend that does not advertise
// the extension - a headless/software backend has nothing to say about
// hardware anisotropic filtering, same reasoning as the rest of this suite's
// environment skips.

#include "sfpew_gtest.h"

#include <cstring>
#include <string>
#include <vector>

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
constexpr GLenum GL_LINEAR_ = 0x2601;
constexpr GLenum GL_LINEAR_MIPMAP_LINEAR_ = 0x2703;
constexpr GLenum GL_REPEAT_ = 0x2901;
constexpr GLenum GL_RGBA_ = 0x1908;
constexpr GLenum GL_UNSIGNED_BYTE_ = 0x1401;
constexpr GLenum GL_PROJECTION_ = 0x1701;
constexpr GLenum GL_MODELVIEW_ = 0x1700;
constexpr GLenum GL_NO_ERROR_ = 0;
constexpr GLenum GL_EXTENSIONS_ = 0x1F03;
constexpr GLenum GL_NUM_EXTENSIONS_ = 0x821D;

// GL_EXT_texture_filter_anisotropic (Khronos registry values; not in every
// GL/gl.h this suite deliberately avoids - see sfpew_gtest.h's header note).
constexpr GLenum GL_TEXTURE_MAX_ANISOTROPY_EXT_ = 0x84FE;
constexpr GLenum GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT_ = 0x84FF;

constexpr int kSize = 64;
constexpr const char* kExtensionName = "GL_EXT_texture_filter_anisotropic";

class TextureAnisotropicTest : public ContextTest {
protected:
    TextureAnisotropicTest() : ContextTest(sfpew_test::Backend::GLES3, kSize) {}

    void SetUp() override {
        ContextTest::SetUp();
        if (::testing::Test::IsSkipped()) return;

        get_string_ = Get<const GLubyte* (*)(GLenum)>("glGetString");
        get_stringi_ = Get<const GLubyte* (*)(GLenum, GLuint)>("glGetStringi");
        get_integerv_ = Get<void (*)(GLenum, GLint*)>("glGetIntegerv");
        get_floatv_ = Get<void (*)(GLenum, GLfloat*)>("glGetFloatv");
        get_error_ = Get<GLenum (*)()>("glGetError");
        gen_textures_ = Get<void (*)(GLsizei, GLuint*)>("glGenTextures");
        bind_texture_ = Get<void (*)(GLenum, GLuint)>("glBindTexture");
        tex_image2d_ = Get<void (*)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum,
                                    const void*)>("glTexImage2D");
        tex_parameteri_ = Get<void (*)(GLenum, GLenum, GLint)>("glTexParameteri");
        tex_parameterf_ = Get<void (*)(GLenum, GLenum, GLfloat)>("glTexParameterf");
        tex_parameteriv_ = Get<void (*)(GLenum, GLenum, const GLint*)>("glTexParameteriv");
        tex_parameterfv_ = Get<void (*)(GLenum, GLenum, const GLfloat*)>("glTexParameterfv");
        get_parameter_i_ = Get<void (*)(GLenum, GLenum, GLint*)>("glGetTexParameteriv");
        get_parameter_f_ = Get<void (*)(GLenum, GLenum, GLfloat*)>("glGetTexParameterfv");
        generate_mipmap_ = Get<void (*)(GLenum)>("glGenerateMipmap");
        clear_color_ = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glClearColor");
        clear_ = Get<void (*)(GLbitfield)>("glClear");
        begin_ = Get<void (*)(GLenum)>("glBegin");
        end_ = Get<void (*)()>("glEnd");
        color4f_ = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glColor4f");
        vertex2f_ = Get<void (*)(GLfloat, GLfloat)>("glVertex2f");
        tex_coord2f_ = Get<void (*)(GLfloat, GLfloat)>("glTexCoord2f");
        enable_ = Get<void (*)(GLenum)>("glEnable");
        matrix_mode_ = Get<void (*)(GLenum)>("glMatrixMode");
        load_identity_ = Get<void (*)()>("glLoadIdentity");
        finish_ = Get<void (*)()>("glFinish");
        auto read_pixels =
            Get<void (*)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*)>("glReadPixels");
        ASSERT_NE(read_pixels, nullptr);
        probe_.emplace(read_pixels);

        const GLubyte* raw = get_string_(GL_EXTENSIONS_);
        ASSERT_NE(raw, nullptr) << "glGetString(GL_EXTENSIONS) == NULL";
        extensions_.assign(reinterpret_cast<const char*>(raw));
        if (extensions_.find(kExtensionName) == std::string::npos) {
            GTEST_SKIP() << "backend does not advertise " << kExtensionName
                         << " - correctly not advertised by the wrapper either";
        }

        matrix_mode_(GL_PROJECTION_);
        load_identity_();
        matrix_mode_(GL_MODELVIEW_);
        load_identity_();
        get_error_(); // clear whatever setup left behind
    }

    GLuint MakeSolidTexture() {
        GLuint texture = 0;
        gen_textures_(1, &texture);
        bind_texture_(GL_TEXTURE_2D_, texture);
        static const GLubyte texel[4] = {200, 100, 50, 255};
        tex_image2d_(GL_TEXTURE_2D_, 0, GL_RGBA_, 1, 1, 0, GL_RGBA_, GL_UNSIGNED_BYTE_, texel);
        return texture;
    }

    // High-frequency vertical stripes - the colour varies only with X, never
    // with Y - mipmapped. Deliberately NOT a checkerboard: a checkerboard's
    // colour is (x/cell + y/cell) % 2, and box-filtering that over a V span
    // that covers a whole number of periods averages to 50% grey for every
    // X equally (an XOR-cancellation artefact of the pattern, not a
    // filtering effect), which would make isotropic and anisotropic
    // filtering look identical here for the wrong reason. Because this
    // texture never varies with Y, blurring Y is always harmless; the only
    // way X detail can be lost is if the mip level chosen for the (unrelated)
    // Y minification drags X's resolution down with it - exactly the
    // artifact GL_EXT_texture_filter_anisotropic exists to avoid.
    GLuint MakeStripedTexture(int size, int cell) {
        GLuint texture = 0;
        gen_textures_(1, &texture);
        bind_texture_(GL_TEXTURE_2D_, texture);
        std::vector<GLubyte> pixels(static_cast<size_t>(size) * size * 4);
        for (int y = 0; y < size; ++y) {
            for (int x = 0; x < size; ++x) {
                const bool white = (x / cell) % 2 == 0;
                const GLubyte v = white ? 255 : 0;
                const size_t i = (static_cast<size_t>(y) * size + x) * 4;
                pixels[i + 0] = v;
                pixels[i + 1] = v;
                pixels[i + 2] = v;
                pixels[i + 3] = 255;
            }
        }
        tex_image2d_(GL_TEXTURE_2D_, 0, GL_RGBA_, size, size, 0, GL_RGBA_, GL_UNSIGNED_BYTE_,
                    pixels.data());
        tex_parameteri_(GL_TEXTURE_2D_, GL_TEXTURE_WRAP_S_, static_cast<GLint>(GL_REPEAT_));
        tex_parameteri_(GL_TEXTURE_2D_, GL_TEXTURE_WRAP_T_, static_cast<GLint>(GL_REPEAT_));
        tex_parameteri_(GL_TEXTURE_2D_, GL_TEXTURE_MIN_FILTER_,
                        static_cast<GLint>(GL_LINEAR_MIPMAP_LINEAR_));
        tex_parameteri_(GL_TEXTURE_2D_, GL_TEXTURE_MAG_FILTER_, static_cast<GLint>(GL_LINEAR_));
        generate_mipmap_(GL_TEXTURE_2D_);
        return texture;
    }

    // Full-viewport quad. vRepeat wraps the V coordinate that many times
    // over the quad's height while U covers 0..1 once over its width, so a
    // vRepeat > 1 minifies only the V axis - the anisotropic case.
    void DrawFullQuad(GLfloat vRepeat) {
        clear_(GL_COLOR_BUFFER_BIT_);
        color4f_(1.0f, 1.0f, 1.0f, 1.0f);
        begin_(GL_QUADS_);
        tex_coord2f_(0.0f, 0.0f);
        vertex2f_(-1.0f, -1.0f);
        tex_coord2f_(1.0f, 0.0f);
        vertex2f_(1.0f, -1.0f);
        tex_coord2f_(1.0f, vRepeat);
        vertex2f_(1.0f, 1.0f);
        tex_coord2f_(0.0f, vRepeat);
        vertex2f_(-1.0f, 1.0f);
        end_();
        finish_();
    }

    // Sample of "how much horizontal (U-axis) detail survived": the peak to
    // peak red-channel swing along one scanline. High under anisotropic
    // filtering (fine U detail preserved), collapses toward 0 under
    // isotropic-only filtering forced onto a mip level coarse enough to
    // satisfy the V axis, which blurs U along with it.
    int ScanlineRedSwing(int y) {
        GLubyte minV = 255, maxV = 0;
        for (int x = 4; x < kSize - 4; ++x) {
            const auto pixel = probe_->At(x, y);
            minV = std::min(minV, pixel.r);
            maxV = std::max(maxV, pixel.r);
        }
        return static_cast<int>(maxV) - static_cast<int>(minV);
    }

    const GLubyte* (*get_string_)(GLenum) = nullptr;
    const GLubyte* (*get_stringi_)(GLenum, GLuint) = nullptr;
    void (*get_integerv_)(GLenum, GLint*) = nullptr;
    void (*get_floatv_)(GLenum, GLfloat*) = nullptr;
    GLenum (*get_error_)() = nullptr;
    void (*gen_textures_)(GLsizei, GLuint*) = nullptr;
    void (*bind_texture_)(GLenum, GLuint) = nullptr;
    void (*tex_image2d_)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum,
                         const void*) = nullptr;
    void (*tex_parameteri_)(GLenum, GLenum, GLint) = nullptr;
    void (*tex_parameterf_)(GLenum, GLenum, GLfloat) = nullptr;
    void (*tex_parameteriv_)(GLenum, GLenum, const GLint*) = nullptr;
    void (*tex_parameterfv_)(GLenum, GLenum, const GLfloat*) = nullptr;
    void (*get_parameter_i_)(GLenum, GLenum, GLint*) = nullptr;
    void (*get_parameter_f_)(GLenum, GLenum, GLfloat*) = nullptr;
    void (*generate_mipmap_)(GLenum) = nullptr;
    void (*clear_color_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*clear_)(GLbitfield) = nullptr;
    void (*begin_)(GLenum) = nullptr;
    void (*end_)() = nullptr;
    void (*color4f_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*vertex2f_)(GLfloat, GLfloat) = nullptr;
    void (*tex_coord2f_)(GLfloat, GLfloat) = nullptr;
    void (*enable_)(GLenum) = nullptr;
    void (*matrix_mode_)(GLenum) = nullptr;
    void (*load_identity_)() = nullptr;
    void (*finish_)() = nullptr;
    std::optional<PixelProbe> probe_;
    std::string extensions_;
};

TEST_F(TextureAnisotropicTest, ExtensionStringIsAdvertisedConsistentlyByStringAndIndexedForm) {
    // SetUp already required this to be present in glGetString(GL_EXTENSIONS)
    // to reach this point; glGetStringi must enumerate the same name, and
    // GL_NUM_EXTENSIONS must be large enough to reach it - all three are
    // plain backend passthrough (getter_version_strings.cpp,
    // fpe/ffp_state_query.cpp), never wrapper bookkeeping for
    // this extension.
    GLint count = 0;
    get_integerv_(GL_NUM_EXTENSIONS_, &count);
    ASSERT_GT(count, 0);
    bool found = false;
    for (GLint i = 0; i < count && !found; ++i) {
        const GLubyte* entry = get_stringi_(GL_EXTENSIONS_, static_cast<GLuint>(i));
        if (entry != nullptr &&
            std::strcmp(reinterpret_cast<const char*>(entry), kExtensionName) == 0) {
            found = true;
        }
    }
    EXPECT_TRUE(found) << "glGetStringi(GL_EXTENSIONS, i) never returned " << kExtensionName
                       << " across " << count << " entries";
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

TEST_F(TextureAnisotropicTest, MaxAnisotropyCapIsQueryableAndAtLeastOne) {
    GLfloat cap = 0.0f;
    get_floatv_(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT_, &cap);
    EXPECT_GE(cap, 1.0f) << "the implementation-defined cap must be at least 1.0";
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

TEST_F(TextureAnisotropicTest, DefaultsToOneAndRoundTripsThroughAllFourSettersAndBothGetters) {
    MakeSolidTexture();

    GLfloat value_f = -1.0f;
    get_parameter_f_(GL_TEXTURE_2D_, GL_TEXTURE_MAX_ANISOTROPY_EXT_, &value_f);
    EXPECT_FLOAT_EQ(value_f, 1.0f) << "GL_TEXTURE_MAX_ANISOTROPY_EXT defaults to 1.0";

    tex_parameterf_(GL_TEXTURE_2D_, GL_TEXTURE_MAX_ANISOTROPY_EXT_, 2.0f);
    get_parameter_f_(GL_TEXTURE_2D_, GL_TEXTURE_MAX_ANISOTROPY_EXT_, &value_f);
    EXPECT_FLOAT_EQ(value_f, 2.0f);
    GLint value_i = -1;
    get_parameter_i_(GL_TEXTURE_2D_, GL_TEXTURE_MAX_ANISOTROPY_EXT_, &value_i);
    EXPECT_EQ(value_i, 2);

    const GLfloat fv_value = 4.0f;
    tex_parameterfv_(GL_TEXTURE_2D_, GL_TEXTURE_MAX_ANISOTROPY_EXT_, &fv_value);
    get_parameter_f_(GL_TEXTURE_2D_, GL_TEXTURE_MAX_ANISOTROPY_EXT_, &value_f);
    EXPECT_FLOAT_EQ(value_f, 4.0f);

    tex_parameteri_(GL_TEXTURE_2D_, GL_TEXTURE_MAX_ANISOTROPY_EXT_, 8);
    get_parameter_i_(GL_TEXTURE_2D_, GL_TEXTURE_MAX_ANISOTROPY_EXT_, &value_i);
    EXPECT_EQ(value_i, 8);
    get_parameter_f_(GL_TEXTURE_2D_, GL_TEXTURE_MAX_ANISOTROPY_EXT_, &value_f);
    EXPECT_FLOAT_EQ(value_f, 8.0f);

    const GLint iv_value = 2;
    tex_parameteriv_(GL_TEXTURE_2D_, GL_TEXTURE_MAX_ANISOTROPY_EXT_, &iv_value);
    get_parameter_i_(GL_TEXTURE_2D_, GL_TEXTURE_MAX_ANISOTROPY_EXT_, &value_i);
    EXPECT_EQ(value_i, 2);

    EXPECT_EQ(get_error_(), GL_NO_ERROR_)
        << "GL_TEXTURE_MAX_ANISOTROPY_EXT must reach the driver through every setter/getter "
           "combination without the wrapper raising an error of its own";
}

TEST_F(TextureAnisotropicTest, ValueAboveTheImplementationCapReachesTheDriverUnrejected) {
    // Measured on this machine's real driver (NVIDIA, cap 16.0): a request
    // above GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT round-trips back EXACTLY as
    // set (164 in, 164 out), not clamped to the cap - the same
    // store-what-was-asked/clamp-only-the-effective-filtering behaviour as
    // GL_LINE_WIDTH vs GL_ALIASED_LINE_WIDTH_RANGE. This is real backend
    // behaviour the wrapper has no part in (it never touches this pname);
    // what this proves is that the value truly reached the driver's own
    // per-texture sampler state - a wrapper bug that silently dropped or
    // rejected the call would leave the default 1.0 in place instead.
    MakeSolidTexture();
    GLfloat cap = 1.0f;
    get_floatv_(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT_, &cap);
    ASSERT_GE(cap, 1.0f);

    const GLfloat requested = cap * 4.0f + 100.0f;
    tex_parameterf_(GL_TEXTURE_2D_, GL_TEXTURE_MAX_ANISOTROPY_EXT_, requested);
    GLfloat value_f = -1.0f;
    get_parameter_f_(GL_TEXTURE_2D_, GL_TEXTURE_MAX_ANISOTROPY_EXT_, &value_f);
    EXPECT_FLOAT_EQ(value_f, requested);
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

TEST_F(TextureAnisotropicTest,
      HighAnisotropyPreservesInPlaneDetailThatLowAnisotropyLosesUnderSingleAxisMinification) {
    // Real render test: vertical stripes (full detail in U, no variation in
    // V at all) minified 8x in V only, by repeating the V texture coordinate
    // 8 times over the quad's height while U covers its normal 0..1 once.
    // GL's mip pyramid halves BOTH dimensions together, so isotropic
    // trilinear forced onto the mip level the V axis needs (~level 3 of a
    // 64-wide texture) also collapses U resolution to the same handful of
    // texels - even though nothing in V needed U to lose anything. That is
    // the exact artifact GL_EXT_texture_filter_anisotropic exists to avoid:
    // it takes several samples along the minified (V) axis at a much
    // sharper base LOD, so U keeps its native resolution. Measured on this
    // machine: aniso 1.0 flattens the stripes to uniform 50% grey (peak
    // to peak swing 0); aniso 16.0 (the backend's own cap) restores the
    // stripes to their full 0/255 swing, identical to the unminified
    // baseline. The margin below is generous on purpose - the property
    // under test is "measurably more detail survives", not a specific
    // driver-dependent quality delta.
    constexpr int kTexSize = 64;
    constexpr int kCell = 4;
    constexpr GLfloat kVRepeat = 8.0f;
    MakeStripedTexture(kTexSize, kCell);
    enable_(GL_TEXTURE_2D_);
    clear_color_(0.0f, 0.0f, 0.0f, 1.0f);

    GLfloat cap = 1.0f;
    get_floatv_(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT_, &cap);
    if (cap < 4.0f) GTEST_SKIP() << "backend's anisotropy cap (" << cap << ") is too low to "
                                    "distinguish from isotropic here";

    tex_parameterf_(GL_TEXTURE_2D_, GL_TEXTURE_MAX_ANISOTROPY_EXT_, 1.0f);
    DrawFullQuad(kVRepeat);
    const int isotropic_swing = ScanlineRedSwing(kSize / 2);

    tex_parameterf_(GL_TEXTURE_2D_, GL_TEXTURE_MAX_ANISOTROPY_EXT_, cap);
    DrawFullQuad(kVRepeat);
    const int anisotropic_swing = ScanlineRedSwing(kSize / 2);

    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
    EXPECT_GT(anisotropic_swing, isotropic_swing + 20)
        << "anisotropy " << cap << " (swing=" << anisotropic_swing
        << ") must preserve visibly more in-plane stripe detail than anisotropy 1.0 (swing="
        << isotropic_swing << ") once minified 8x along the other axis";
}

} // namespace
