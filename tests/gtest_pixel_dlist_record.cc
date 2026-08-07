// SimpleFPEWrapper - tests/gtest_pixel_dlist_record.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// plans/17 P15: glDrawPixels, glCopyPixels, glTexImage2D and glTexSubImage2D
// carried no display-list recording at all. Under GL_COMPILE the rectangle
// was drawn there and then - which GL 2.1 5.4 forbids - and the compiled list
// came out empty, so the image landed once, at the wrong time, and never
// again.
//
// The three-way equivalence this project uses for display lists: immediate,
// GL_COMPILE + glCallList (asserting NOTHING happened while compiling and
// that the replay matches immediate pixel for pixel), and
// GL_COMPILE_AND_EXECUTE.
//
// Plus what makes the pixel commands different from the draw commands
// plans/15 recorded: they carry a payload that must be snapshotted at COMPILE
// time, and the size of that snapshot depends on pixel-store state the
// application may have changed by replay time. glPixelStore's own reference
// page settles it: "the pixel storage modes in effect when [glDrawPixels,
// glTexImage2D, glTexSubImage2D, glBitmap] is placed in a display list
// control the interpretation of memory data ... the pixel storage modes in
// effect when a display list is executed are not significant".
//
// Colour judgements are made on the green channel, or by comparing two
// framebuffers the wrapper produced the same way: llvmpipe reads R and B back
// swapped and none of this is about component order.

#include "sfpew_gtest.h"

#include <cstring>
#include <optional>
#include <string>
#include <vector>

namespace {

using sfpew_test::ContextTest;
using sfpew_test::GLbitfield;
using sfpew_test::GLenum;
using sfpew_test::GLfloat;
using sfpew_test::GLint;
using sfpew_test::GLsizei;
using sfpew_test::GLsizeiptr;
using sfpew_test::GLubyte;
using sfpew_test::GLuint;
using sfpew_test::PixelProbe;

constexpr GLenum GL_NO_ERROR_ = 0;
constexpr GLbitfield GL_COLOR_BUFFER_BIT_ = 0x00004000;
constexpr GLenum GL_RGBA_ = 0x1908;
constexpr GLenum GL_UNSIGNED_BYTE_ = 0x1401;
constexpr GLenum GL_COLOR_ = 0x1800;
constexpr GLenum GL_COMPILE_ = 0x1300;
constexpr GLenum GL_COMPILE_AND_EXECUTE_ = 0x1301;
constexpr GLenum GL_TEXTURE_2D_ = 0x0DE1;
constexpr GLenum GL_TEXTURE_MIN_FILTER_ = 0x2801;
constexpr GLenum GL_TEXTURE_MAG_FILTER_ = 0x2800;
constexpr GLenum GL_NEAREST_ = 0x2600;
constexpr GLenum GL_QUADS_ = 0x0007;
constexpr GLenum GL_PROJECTION_ = 0x1701;
constexpr GLenum GL_MODELVIEW_ = 0x1700;
constexpr GLenum GL_UNPACK_ROW_LENGTH_ = 0x0CF2;
constexpr GLenum GL_UNPACK_SKIP_ROWS_ = 0x0CF3;
constexpr GLenum GL_UNPACK_SKIP_PIXELS_ = 0x0CF4;
constexpr GLenum GL_UNPACK_ALIGNMENT_ = 0x0CF5;
constexpr GLenum GL_PIXEL_UNPACK_BUFFER_ = 0x88EC;
constexpr GLenum GL_STATIC_DRAW_ = 0x88E4;
constexpr GLenum GL_CURRENT_RASTER_POSITION_VALID_ = 0x0B08;

constexpr int kSize = 64;

// Not black, so "nothing happened while the list compiled" is an equality
// against the cleared framebuffer rather than a brightness threshold.
constexpr GLfloat kClearRgba[4] = {0.2f, 0.1f, 0.3f, 1.0f};

// The rectangle glDrawPixels/glCopyPixels paint: 4x4, R == B so the llvmpipe
// swizzle cannot turn it into a different colour, and every texel distinct in
// green so a replay that loses row or column order shows up.
constexpr GLsizei kRect = 4;
std::vector<GLubyte> MakeRect(GLubyte green_base) {
    std::vector<GLubyte> pixels(static_cast<size_t>(kRect) * kRect * 4u);
    for (size_t i = 0; i < static_cast<size_t>(kRect) * kRect; ++i) {
        pixels[i * 4 + 0] = 200;
        pixels[i * 4 + 1] = static_cast<GLubyte>(green_base + i * 4);
        pixels[i * 4 + 2] = 200;
        pixels[i * 4 + 3] = 255;
    }
    return pixels;
}

class PixelListRecordTest : public ContextTest {
protected:
    void SetUp() override {
        ContextTest::SetUp();
        if (::testing::Test::IsSkipped() || ::testing::Test::HasFatalFailure()) return;
        using MakeCurrentFn = EGLBoolean (*)(EGLDisplay, EGLSurface, EGLSurface, EGLContext);
        auto wrapper_make_current = Get<MakeCurrentFn>("eglMakeCurrent");
        ASSERT_TRUE(wrapper_make_current(display(), surface(), surface(), eglGetCurrentContext()));

        get_error_ = Get<GLenum (*)()>("glGetError");
        get_integer_ = Get<void (*)(GLenum, GLint*)>("glGetIntegerv");
        viewport_ = Get<void (*)(GLint, GLint, GLsizei, GLsizei)>("glViewport");
        clear_color_ = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glClearColor");
        clear_ = Get<void (*)(GLbitfield)>("glClear");
        window_pos_ = Get<void (*)(GLint, GLint)>("glWindowPos2i");
        raster_pos2f_ = Get<void (*)(GLfloat, GLfloat)>("glRasterPos2f");
        draw_pixels_ = Get<void (*)(GLsizei, GLsizei, GLenum, GLenum, const void*)>("glDrawPixels");
        copy_pixels_ = Get<void (*)(GLint, GLint, GLsizei, GLsizei, GLenum)>("glCopyPixels");
        read_pixels_ = Get<PixelProbe::ReadPixelsFn>("glReadPixels");
        pixel_store_ = Get<void (*)(GLenum, GLint)>("glPixelStorei");
        tex_image_ = Get<void (*)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum,
                                  const void*)>("glTexImage2D");
        tex_sub_image_ = Get<void (*)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum,
                                      GLenum, const void*)>("glTexSubImage2D");
        gen_textures_ = Get<void (*)(GLsizei, GLuint*)>("glGenTextures");
        bind_texture_ = Get<void (*)(GLenum, GLuint)>("glBindTexture");
        tex_parameteri_ = Get<void (*)(GLenum, GLenum, GLint)>("glTexParameteri");
        gen_buffers_ = Get<void (*)(GLsizei, GLuint*)>("glGenBuffers");
        bind_buffer_ = Get<void (*)(GLenum, GLuint)>("glBindBuffer");
        buffer_data_ = Get<void (*)(GLenum, GLsizeiptr, const void*, GLenum)>("glBufferData");
        delete_buffers_ = Get<void (*)(GLsizei, const GLuint*)>("glDeleteBuffers");
        gen_lists_ = Get<GLuint (*)(GLsizei)>("glGenLists");
        new_list_ = Get<void (*)(GLuint, GLenum)>("glNewList");
        end_list_ = Get<void (*)()>("glEndList");
        call_list_ = Get<void (*)(GLuint)>("glCallList");
        delete_lists_ = Get<void (*)(GLuint, GLsizei)>("glDeleteLists");
        matrix_mode_ = Get<void (*)(GLenum)>("glMatrixMode");
        load_identity_ = Get<void (*)()>("glLoadIdentity");
        enable_ = Get<void (*)(GLenum)>("glEnable");
        begin_ = Get<void (*)(GLenum)>("glBegin");
        end_ = Get<void (*)()>("glEnd");
        color4f_ = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glColor4f");
        vertex2f_ = Get<void (*)(GLfloat, GLfloat)>("glVertex2f");
        tex_coord2f_ = Get<void (*)(GLfloat, GLfloat)>("glTexCoord2f");

        viewport_(0, 0, size(), size());
        matrix_mode_(GL_PROJECTION_);
        load_identity_();
        matrix_mode_(GL_MODELVIEW_);
        load_identity_();
        clear_color_(kClearRgba[0], kClearRgba[1], kClearRgba[2], kClearRgba[3]);
        Clear();
        Drain();
    }

    void Drain() {
        while (get_error_() != GL_NO_ERROR_) {}
    }

    void Clear() { clear_(GL_COLOR_BUFFER_BIT_); }

    std::vector<GLubyte> Snapshot() const {
        std::vector<GLubyte> pixels(static_cast<size_t>(size()) * size() * 4u);
        read_pixels_(0, 0, size(), size(), GL_RGBA_, GL_UNSIGNED_BYTE_, pixels.data());
        return pixels;
    }

    // How many pixels differ, for a message worth reading.
    static int Differences(const std::vector<GLubyte>& a, const std::vector<GLubyte>& b) {
        int differing = 0;
        for (size_t i = 0; i + 3 < a.size() && i + 3 < b.size(); i += 4) {
            if (std::memcmp(a.data() + i, b.data() + i, 4) != 0) ++differing;
        }
        return differing;
    }

    void ExpectSame(const std::vector<GLubyte>& expected, const std::vector<GLubyte>& actual,
                    const std::string& what) const {
        EXPECT_EQ(Differences(expected, actual), 0) << what;
    }

    void ExpectCleared(const std::vector<GLubyte>& actual, const std::string& what) const {
        ExpectSame(cleared_, actual, what);
    }

    // The framebuffer as a bare clear leaves it, cached for the "nothing was
    // drawn" comparisons.
    void LatchCleared() {
        Clear();
        cleared_ = Snapshot();
    }

    GLuint NewTexture() {
        GLuint texture = 0;
        gen_textures_(1, &texture);
        bind_texture_(GL_TEXTURE_2D_, texture);
        tex_parameteri_(GL_TEXTURE_2D_, GL_TEXTURE_MIN_FILTER_, GL_NEAREST_);
        tex_parameteri_(GL_TEXTURE_2D_, GL_TEXTURE_MAG_FILTER_, GL_NEAREST_);
        return texture;
    }

    // The bound 2D texture across the whole framebuffer; returns the centre.
    PixelProbe::Rgba SampleTexture() {
        Clear();
        enable_(GL_TEXTURE_2D_);
        begin_(GL_QUADS_);
        color4f_(1.0f, 1.0f, 1.0f, 1.0f);
        tex_coord2f_(0.25f, 0.25f);
        vertex2f_(-1.0f, -1.0f);
        tex_coord2f_(0.75f, 0.25f);
        vertex2f_(1.0f, -1.0f);
        tex_coord2f_(0.75f, 0.75f);
        vertex2f_(1.0f, 1.0f);
        tex_coord2f_(0.25f, 0.75f);
        vertex2f_(-1.0f, 1.0f);
        end_();
        return PixelProbe(read_pixels_).At(kSize / 2, kSize / 2);
    }

    GLenum (*get_error_)() = nullptr;
    void (*get_integer_)(GLenum, GLint*) = nullptr;
    void (*viewport_)(GLint, GLint, GLsizei, GLsizei) = nullptr;
    void (*clear_color_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*clear_)(GLbitfield) = nullptr;
    void (*window_pos_)(GLint, GLint) = nullptr;
    void (*raster_pos2f_)(GLfloat, GLfloat) = nullptr;
    void (*draw_pixels_)(GLsizei, GLsizei, GLenum, GLenum, const void*) = nullptr;
    void (*copy_pixels_)(GLint, GLint, GLsizei, GLsizei, GLenum) = nullptr;
    PixelProbe::ReadPixelsFn read_pixels_ = nullptr;
    void (*pixel_store_)(GLenum, GLint) = nullptr;
    void (*tex_image_)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum,
                       const void*) = nullptr;
    void (*tex_sub_image_)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum,
                           const void*) = nullptr;
    void (*gen_textures_)(GLsizei, GLuint*) = nullptr;
    void (*bind_texture_)(GLenum, GLuint) = nullptr;
    void (*tex_parameteri_)(GLenum, GLenum, GLint) = nullptr;
    void (*gen_buffers_)(GLsizei, GLuint*) = nullptr;
    void (*bind_buffer_)(GLenum, GLuint) = nullptr;
    void (*buffer_data_)(GLenum, GLsizeiptr, const void*, GLenum) = nullptr;
    void (*delete_buffers_)(GLsizei, const GLuint*) = nullptr;
    GLuint (*gen_lists_)(GLsizei) = nullptr;
    void (*new_list_)(GLuint, GLenum) = nullptr;
    void (*end_list_)() = nullptr;
    void (*call_list_)(GLuint) = nullptr;
    void (*delete_lists_)(GLuint, GLsizei) = nullptr;
    void (*matrix_mode_)(GLenum) = nullptr;
    void (*load_identity_)() = nullptr;
    void (*enable_)(GLenum) = nullptr;
    void (*begin_)(GLenum) = nullptr;
    void (*end_)() = nullptr;
    void (*color4f_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*vertex2f_)(GLfloat, GLfloat) = nullptr;
    void (*tex_coord2f_)(GLfloat, GLfloat) = nullptr;

    std::vector<GLubyte> cleared_;
};

TEST_F(PixelListRecordTest, DrawPixelsCompilesRatherThanDrawing) {
    const std::vector<GLubyte> rect = MakeRect(60);
    LatchCleared();

    window_pos_(8, 8);
    draw_pixels_(kRect, kRect, GL_RGBA_, GL_UNSIGNED_BYTE_, rect.data());
    const std::vector<GLubyte> reference = Snapshot();
    ASSERT_NE(Differences(cleared_, reference), 0) << "the immediate glDrawPixels drew nothing";
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);

    Clear();
    const GLuint list = gen_lists_(1);
    ASSERT_NE(list, 0u);
    new_list_(list, GL_COMPILE_);
    draw_pixels_(kRect, kRect, GL_RGBA_, GL_UNSIGNED_BYTE_, rect.data());
    end_list_();
    ExpectCleared(Snapshot(), "GL_COMPILE drew the pixel rectangle instead of compiling it");
    call_list_(list);
    ExpectSame(reference, Snapshot(), "the glCallList replay does not match the immediate draw");
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);

    Clear();
    const GLuint both = gen_lists_(1);
    new_list_(both, GL_COMPILE_AND_EXECUTE_);
    draw_pixels_(kRect, kRect, GL_RGBA_, GL_UNSIGNED_BYTE_, rect.data());
    end_list_();
    ExpectSame(reference, Snapshot(), "GL_COMPILE_AND_EXECUTE did not draw");
    Clear();
    call_list_(both);
    ExpectSame(reference, Snapshot(), "replaying a GL_COMPILE_AND_EXECUTE list");
    delete_lists_(list, 1);
    delete_lists_(both, 1);
}

TEST_F(PixelListRecordTest, DrawPixelsPayloadIsSnapshotAtCompileTime) {
    std::vector<GLubyte> rect = MakeRect(60);
    LatchCleared();

    window_pos_(8, 8);
    draw_pixels_(kRect, kRect, GL_RGBA_, GL_UNSIGNED_BYTE_, rect.data());
    const std::vector<GLubyte> reference = Snapshot();
    ASSERT_NE(Differences(cleared_, reference), 0);

    Clear();
    const GLuint list = gen_lists_(1);
    new_list_(list, GL_COMPILE_);
    draw_pixels_(kRect, kRect, GL_RGBA_, GL_UNSIGNED_BYTE_, rect.data());
    end_list_();
    ExpectCleared(Snapshot(), "GL_COMPILE drew the pixel rectangle instead of compiling it");

    // The list owns its copy from here on. Overwrite the source, then take
    // the storage away entirely - a list holding the caller's pointer would
    // draw the new contents, or read freed memory.
    for (auto& byte : rect) byte = 0;
    rect.clear();
    rect.shrink_to_fit();

    Clear();
    call_list_(list);
    ExpectSame(reference, Snapshot(), "the replay used the buffer's LATER contents");
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
    delete_lists_(list, 1);
}

TEST_F(PixelListRecordTest, DrawPixelsPayloadFollowsTheCompileTimeUnpackState) {
    // A 4x4 rectangle inside a 16-wide image, two rows down and two pixels
    // in: only the compile-time GL_UNPACK_* values select it.
    constexpr GLsizei kRowPixels = 16;
    const std::vector<GLubyte> tight = MakeRect(60);
    std::vector<GLubyte> padded(static_cast<size_t>(kRowPixels) * 8u * 4u, 0);
    for (int row = 0; row < kRect; ++row) {
        std::memcpy(padded.data() + ((static_cast<size_t>(row) + 2u) * kRowPixels + 2u) * 4u,
                    tight.data() + static_cast<size_t>(row) * kRect * 4u,
                    static_cast<size_t>(kRect) * 4u);
    }
    LatchCleared();

    window_pos_(8, 8);
    pixel_store_(GL_UNPACK_ROW_LENGTH_, kRowPixels);
    pixel_store_(GL_UNPACK_SKIP_ROWS_, 2);
    pixel_store_(GL_UNPACK_SKIP_PIXELS_, 2);
    draw_pixels_(kRect, kRect, GL_RGBA_, GL_UNSIGNED_BYTE_, padded.data());
    const std::vector<GLubyte> reference = Snapshot();
    ASSERT_NE(Differences(cleared_, reference), 0);

    Clear();
    const GLuint list = gen_lists_(1);
    new_list_(list, GL_COMPILE_);
    draw_pixels_(kRect, kRect, GL_RGBA_, GL_UNSIGNED_BYTE_, padded.data());
    end_list_();
    ExpectCleared(Snapshot(), "GL_COMPILE drew the pixel rectangle instead of compiling it");

    // glPixelStore's reference page: the modes in effect when the list is
    // EXECUTED are not significant. Change every one of them, and bind an
    // unpack buffer for good measure - the captured payload is client memory
    // by now and must not be read back as an offset into it.
    pixel_store_(GL_UNPACK_ROW_LENGTH_, 0);
    pixel_store_(GL_UNPACK_SKIP_ROWS_, 0);
    pixel_store_(GL_UNPACK_SKIP_PIXELS_, 0);
    pixel_store_(GL_UNPACK_ALIGNMENT_, 8);
    GLuint buffer = 0;
    gen_buffers_(1, &buffer);
    bind_buffer_(GL_PIXEL_UNPACK_BUFFER_, buffer);
    buffer_data_(GL_PIXEL_UNPACK_BUFFER_, static_cast<GLsizeiptr>(padded.size()), padded.data(),
                 GL_STATIC_DRAW_);
    Drain();

    Clear();
    call_list_(list);
    EXPECT_EQ(get_error_(), GL_NO_ERROR_) << "replaying with an unpack buffer bound";
    ExpectSame(reference, Snapshot(),
               "the replay was interpreted under the unpack state at glCallList time");

    // And the application's own unpack state is where it left it.
    GLint row_length = -1, alignment = -1, bound = -1;
    get_integer_(GL_UNPACK_ROW_LENGTH_, &row_length);
    get_integer_(GL_UNPACK_ALIGNMENT_, &alignment);
    get_integer_(0x88EF /* GL_PIXEL_UNPACK_BUFFER_BINDING */, &bound);
    EXPECT_EQ(row_length, 0);
    EXPECT_EQ(alignment, 8);
    EXPECT_EQ(bound, static_cast<GLint>(buffer));

    bind_buffer_(GL_PIXEL_UNPACK_BUFFER_, 0);
    delete_buffers_(1, &buffer);
    delete_lists_(list, 1);
}

TEST_F(PixelListRecordTest, DrawPixelsIsRecordedWhateverTheRasterPositionWasWhenCompiled) {
    const std::vector<GLubyte> rect = MakeRect(60);
    LatchCleared();

    window_pos_(8, 8);
    draw_pixels_(kRect, kRect, GL_RGBA_, GL_UNSIGNED_BYTE_, rect.data());
    const std::vector<GLubyte> reference = Snapshot();
    ASSERT_NE(Differences(cleared_, reference), 0);

    // Compile with the raster position invalid: that is an execution-time
    // condition, so it must not decide whether the command is compiled.
    Clear();
    raster_pos2f_(-5.0f, -5.0f);
    GLint valid = -1;
    get_integer_(GL_CURRENT_RASTER_POSITION_VALID_, &valid);
    ASSERT_EQ(valid, 0) << "this case needs an invalid raster position to compile under";

    const GLuint list = gen_lists_(1);
    new_list_(list, GL_COMPILE_);
    draw_pixels_(kRect, kRect, GL_RGBA_, GL_UNSIGNED_BYTE_, rect.data());
    end_list_();
    ExpectCleared(Snapshot(), "GL_COMPILE drew instead of compiling");

    window_pos_(8, 8);
    Clear();
    call_list_(list);
    ExpectSame(reference, Snapshot(),
               "the command was dropped because the raster position was invalid while compiling");
    delete_lists_(list, 1);
}

TEST_F(PixelListRecordTest, CopyPixelsCompilesRatherThanCopying) {
    const std::vector<GLubyte> rect = MakeRect(60);
    LatchCleared();

    // One source rectangle, painted immediately, that every phase copies
    // from; only the glCopyPixels is compiled.
    auto paint_source = [&] {
        Clear();
        window_pos_(4, 4);
        draw_pixels_(kRect, kRect, GL_RGBA_, GL_UNSIGNED_BYTE_, rect.data());
    };

    paint_source();
    const std::vector<GLubyte> source_only = Snapshot();
    window_pos_(24, 24);
    copy_pixels_(4, 4, kRect, kRect, GL_COLOR_);
    const std::vector<GLubyte> reference = Snapshot();
    ASSERT_NE(Differences(source_only, reference), 0) << "the immediate glCopyPixels copied nothing";
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);

    paint_source();
    window_pos_(24, 24);
    const GLuint list = gen_lists_(1);
    new_list_(list, GL_COMPILE_);
    copy_pixels_(4, 4, kRect, kRect, GL_COLOR_);
    end_list_();
    ExpectSame(source_only, Snapshot(), "GL_COMPILE copied instead of compiling");
    call_list_(list);
    ExpectSame(reference, Snapshot(), "the glCallList replay does not match the immediate copy");

    paint_source();
    window_pos_(24, 24);
    const GLuint both = gen_lists_(1);
    new_list_(both, GL_COMPILE_AND_EXECUTE_);
    copy_pixels_(4, 4, kRect, kRect, GL_COLOR_);
    end_list_();
    ExpectSame(reference, Snapshot(), "GL_COMPILE_AND_EXECUTE did not copy");
    paint_source();
    window_pos_(24, 24);
    call_list_(both);
    ExpectSame(reference, Snapshot(), "replaying a GL_COMPILE_AND_EXECUTE list");
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
    delete_lists_(list, 1);
    delete_lists_(both, 1);
}

// Red first, green from the list: the "did the upload happen yet" question is
// then a green-channel reading rather than a query about an undefined level.
constexpr GLsizei kTex = 4;
std::vector<GLubyte> SolidTexels(GLubyte red, GLubyte green, GLubyte blue) {
    std::vector<GLubyte> texels(static_cast<size_t>(kTex) * kTex * 4u);
    for (size_t i = 0; i < static_cast<size_t>(kTex) * kTex; ++i) {
        texels[i * 4 + 0] = red;
        texels[i * 4 + 1] = green;
        texels[i * 4 + 2] = blue;
        texels[i * 4 + 3] = 255;
    }
    return texels;
}

TEST_F(PixelListRecordTest, TexImage2DCompilesRatherThanUploading) {
    const std::vector<GLubyte> red = SolidTexels(200, 0, 200);
    const std::vector<GLubyte> green = SolidTexels(0, 220, 0);
    NewTexture();
    tex_image_(GL_TEXTURE_2D_, 0, GL_RGBA_, kTex, kTex, 0, GL_RGBA_, GL_UNSIGNED_BYTE_,
               red.data());
    Drain();
    ASSERT_LE(SampleTexture().g, 40) << "the setup upload did not take";

    const GLuint list = gen_lists_(1);
    ASSERT_NE(list, 0u);
    new_list_(list, GL_COMPILE_);
    tex_image_(GL_TEXTURE_2D_, 0, GL_RGBA_, kTex, kTex, 0, GL_RGBA_, GL_UNSIGNED_BYTE_,
               green.data());
    end_list_();
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
    EXPECT_LE(SampleTexture().g, 40) << "GL_COMPILE uploaded instead of compiling";

    call_list_(list);
    EXPECT_GE(SampleTexture().g, 150) << "the compiled upload never happened";
    delete_lists_(list, 1);

    // GL_COMPILE_AND_EXECUTE uploads once now and once per replay.
    tex_image_(GL_TEXTURE_2D_, 0, GL_RGBA_, kTex, kTex, 0, GL_RGBA_, GL_UNSIGNED_BYTE_,
               red.data());
    const GLuint both = gen_lists_(1);
    new_list_(both, GL_COMPILE_AND_EXECUTE_);
    tex_image_(GL_TEXTURE_2D_, 0, GL_RGBA_, kTex, kTex, 0, GL_RGBA_, GL_UNSIGNED_BYTE_,
               green.data());
    end_list_();
    EXPECT_GE(SampleTexture().g, 150) << "GL_COMPILE_AND_EXECUTE did not upload";
    tex_image_(GL_TEXTURE_2D_, 0, GL_RGBA_, kTex, kTex, 0, GL_RGBA_, GL_UNSIGNED_BYTE_,
               red.data());
    call_list_(both);
    EXPECT_GE(SampleTexture().g, 150) << "replaying a GL_COMPILE_AND_EXECUTE list";
    delete_lists_(both, 1);
}

TEST_F(PixelListRecordTest, TexImage2DPayloadIsSnapshotAtCompileTime) {
    const std::vector<GLubyte> red = SolidTexels(200, 0, 200);
    std::vector<GLubyte> green = SolidTexels(0, 220, 0);
    NewTexture();
    tex_image_(GL_TEXTURE_2D_, 0, GL_RGBA_, kTex, kTex, 0, GL_RGBA_, GL_UNSIGNED_BYTE_,
               red.data());
    Drain();

    const GLuint list = gen_lists_(1);
    new_list_(list, GL_COMPILE_);
    tex_image_(GL_TEXTURE_2D_, 0, GL_RGBA_, kTex, kTex, 0, GL_RGBA_, GL_UNSIGNED_BYTE_,
               green.data());
    end_list_();
    EXPECT_LE(SampleTexture().g, 40) << "GL_COMPILE uploaded instead of compiling";

    green = SolidTexels(200, 0, 200);
    green.clear();
    green.shrink_to_fit();

    call_list_(list);
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
    EXPECT_GE(SampleTexture().g, 150)
        << "the replay used the source buffer's LATER contents, not the ones it was compiled with";
    delete_lists_(list, 1);
}

TEST_F(PixelListRecordTest, TexImage2DReplayIgnoresTheUnpackBufferBoundAtCallTime) {
    const std::vector<GLubyte> red = SolidTexels(200, 0, 200);
    const std::vector<GLubyte> green = SolidTexels(0, 220, 0);
    NewTexture();
    tex_image_(GL_TEXTURE_2D_, 0, GL_RGBA_, kTex, kTex, 0, GL_RGBA_, GL_UNSIGNED_BYTE_,
               red.data());
    Drain();

    const GLuint list = gen_lists_(1);
    new_list_(list, GL_COMPILE_);
    tex_image_(GL_TEXTURE_2D_, 0, GL_RGBA_, kTex, kTex, 0, GL_RGBA_, GL_UNSIGNED_BYTE_,
               green.data());
    end_list_();
    EXPECT_LE(SampleTexture().g, 40) << "GL_COMPILE uploaded instead of compiling";

    // The captured payload is client memory, so an unpack buffer bound here
    // would turn the pointer back into an offset - the mirror hazard commit
    // 08cc918 named for the compressed family.
    GLuint buffer = 0;
    gen_buffers_(1, &buffer);
    bind_buffer_(GL_PIXEL_UNPACK_BUFFER_, buffer);
    buffer_data_(GL_PIXEL_UNPACK_BUFFER_, static_cast<GLsizeiptr>(red.size()), red.data(),
                 GL_STATIC_DRAW_);
    Drain();

    call_list_(list);
    EXPECT_EQ(get_error_(), GL_NO_ERROR_) << "replaying with an unpack buffer bound";
    bind_buffer_(GL_PIXEL_UNPACK_BUFFER_, 0);
    EXPECT_GE(SampleTexture().g, 150) << "the replay read the buffer bound at glCallList time";
    delete_buffers_(1, &buffer);
    delete_lists_(list, 1);
}

TEST_F(PixelListRecordTest, TexSubImage2DCompilesRatherThanUploading) {
    const std::vector<GLubyte> red = SolidTexels(200, 0, 200);
    std::vector<GLubyte> green = SolidTexels(0, 220, 0);
    NewTexture();
    tex_image_(GL_TEXTURE_2D_, 0, GL_RGBA_, kTex, kTex, 0, GL_RGBA_, GL_UNSIGNED_BYTE_,
               red.data());
    Drain();
    ASSERT_LE(SampleTexture().g, 40);

    const GLuint list = gen_lists_(1);
    new_list_(list, GL_COMPILE_);
    tex_sub_image_(GL_TEXTURE_2D_, 0, 0, 0, kTex, kTex, GL_RGBA_, GL_UNSIGNED_BYTE_, green.data());
    end_list_();
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
    EXPECT_LE(SampleTexture().g, 40) << "GL_COMPILE uploaded instead of compiling";

    // Same snapshot rule as the full upload.
    green = SolidTexels(200, 0, 200);
    green.clear();
    green.shrink_to_fit();

    call_list_(list);
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
    EXPECT_GE(SampleTexture().g, 150)
        << "the compiled sub-upload never happened, or carried the wrong payload";
    delete_lists_(list, 1);
}

TEST_F(PixelListRecordTest, TexSubImage2DPayloadFollowsTheCompileTimeUnpackState) {
    constexpr GLsizei kRowPixels = 16;
    const std::vector<GLubyte> red = SolidTexels(200, 0, 200);
    const std::vector<GLubyte> green = SolidTexels(0, 220, 0);
    std::vector<GLubyte> padded(static_cast<size_t>(kRowPixels) * 8u * 4u, 0);
    for (int row = 0; row < kTex; ++row) {
        std::memcpy(padded.data() + ((static_cast<size_t>(row) + 2u) * kRowPixels + 2u) * 4u,
                    green.data() + static_cast<size_t>(row) * kTex * 4u,
                    static_cast<size_t>(kTex) * 4u);
    }
    NewTexture();
    tex_image_(GL_TEXTURE_2D_, 0, GL_RGBA_, kTex, kTex, 0, GL_RGBA_, GL_UNSIGNED_BYTE_,
               red.data());
    Drain();

    pixel_store_(GL_UNPACK_ROW_LENGTH_, kRowPixels);
    pixel_store_(GL_UNPACK_SKIP_ROWS_, 2);
    pixel_store_(GL_UNPACK_SKIP_PIXELS_, 2);
    const GLuint list = gen_lists_(1);
    new_list_(list, GL_COMPILE_);
    tex_sub_image_(GL_TEXTURE_2D_, 0, 0, 0, kTex, kTex, GL_RGBA_, GL_UNSIGNED_BYTE_,
                   padded.data());
    end_list_();
    EXPECT_LE(SampleTexture().g, 40) << "GL_COMPILE uploaded instead of compiling";
    pixel_store_(GL_UNPACK_ROW_LENGTH_, 0);
    pixel_store_(GL_UNPACK_SKIP_ROWS_, 0);
    pixel_store_(GL_UNPACK_SKIP_PIXELS_, 0);
    Drain();

    call_list_(list);
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
    EXPECT_GE(SampleTexture().g, 150)
        << "the replay read the sub-rectangle under the unpack state at glCallList time";
    delete_lists_(list, 1);
}

} // namespace
