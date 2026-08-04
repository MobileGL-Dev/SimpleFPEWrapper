// SimpleFPEWrapper - tests/gtest_pixel_maps.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

#include "sfpew_gtest.h"

#include <array>
#include <cstdint>
#include <limits>

namespace {

using sfpew_test::ContextTest;
using sfpew_test::GLbitfield;
using sfpew_test::GLenum;
using sfpew_test::GLfloat;
using sfpew_test::GLint;
using sfpew_test::GLintptr;
using sfpew_test::GLsizei;
using sfpew_test::GLsizeiptr;
using sfpew_test::GLubyte;
using sfpew_test::GLuint;
using sfpew_test::GLushort;
using sfpew_test::LibraryTest;
using sfpew_test::PixelProbe;

constexpr GLenum GL_NO_ERROR_ = 0;
constexpr GLenum GL_INVALID_ENUM_ = 0x0500;
constexpr GLenum GL_INVALID_VALUE_ = 0x0501;
constexpr GLenum GL_INVALID_OPERATION_ = 0x0502;
constexpr GLenum GL_COMPILE_ = 0x1300;
constexpr GLenum GL_PIXEL_MAP_I_TO_I_ = 0x0C70;
constexpr GLenum GL_PIXEL_MAP_I_TO_R_ = 0x0C72;
constexpr GLenum GL_PIXEL_MAP_R_TO_R_ = 0x0C76;
constexpr GLenum GL_PIXEL_MAP_G_TO_G_ = 0x0C77;
constexpr GLenum GL_PIXEL_MAP_B_TO_B_ = 0x0C78;
constexpr GLenum GL_PIXEL_MAP_A_TO_A_ = 0x0C79;
constexpr GLenum GL_PIXEL_MAP_R_TO_R_SIZE_ = 0x0CB6;
constexpr GLenum GL_MAP_COLOR_ = 0x0D10;
constexpr GLenum GL_RGBA_ = 0x1908;
constexpr GLenum GL_UNSIGNED_BYTE_ = 0x1401;
constexpr GLenum GL_PIXEL_PACK_BUFFER_ = 0x88EB;
constexpr GLenum GL_PIXEL_UNPACK_BUFFER_ = 0x88EC;
constexpr GLenum GL_PIXEL_PACK_BUFFER_BINDING_ = 0x88ED;
constexpr GLenum GL_PIXEL_UNPACK_BUFFER_BINDING_ = 0x88EF;
constexpr GLenum GL_STATIC_DRAW_ = 0x88E4;
constexpr GLbitfield GL_COLOR_BUFFER_BIT_ = 0x00004000;

class PixelMapLibraryTest : public LibraryTest {};

TEST_F(PixelMapLibraryTest, SizesValidationAndNumericConversionsMatchTheMapKind) {
    auto pixel_map_fv =
        Get<void (*)(GLenum, GLsizei, const GLfloat*)>("glPixelMapfv");
    auto pixel_map_uiv = Get<void (*)(GLenum, GLsizei, const GLuint*)>("glPixelMapuiv");
    auto pixel_map_usv =
        Get<void (*)(GLenum, GLsizei, const GLushort*)>("glPixelMapusv");
    auto get_map_fv = Get<void (*)(GLenum, GLfloat*)>("glGetPixelMapfv");
    auto get_map_uiv = Get<void (*)(GLenum, GLuint*)>("glGetPixelMapuiv");
    auto get_map_usv = Get<void (*)(GLenum, GLushort*)>("glGetPixelMapusv");
    auto get_integer = Get<void (*)(GLenum, GLint*)>("glGetIntegerv");
    auto get_error = Get<GLenum (*)()>("glGetError");

    const std::array<GLfloat, 3> component = {-1.0f, 0.5f, 2.0f};
    pixel_map_fv(GL_PIXEL_MAP_R_TO_R_, component.size(), component.data());
    std::array<GLfloat, 3> as_float{};
    std::array<GLushort, 3> as_short{};
    get_map_fv(GL_PIXEL_MAP_R_TO_R_, as_float.data());
    get_map_usv(GL_PIXEL_MAP_R_TO_R_, as_short.data());
    EXPECT_EQ(as_float, (std::array<GLfloat, 3>{0.0f, 0.5f, 1.0f}));
    EXPECT_EQ(as_short, (std::array<GLushort, 3>{0, 32768, 65535}));

    GLint size = 0;
    get_integer(GL_PIXEL_MAP_R_TO_R_SIZE_, &size);
    EXPECT_EQ(size, 3);

    const std::array<GLuint, 2> indices = {4u, 19u};
    pixel_map_uiv(GL_PIXEL_MAP_I_TO_I_, indices.size(), indices.data());
    std::array<GLuint, 2> returned_indices{};
    get_map_uiv(GL_PIXEL_MAP_I_TO_I_, returned_indices.data());
    EXPECT_EQ(returned_indices, indices);

    const std::array<GLushort, 2> normalized = {0, 65535};
    pixel_map_usv(GL_PIXEL_MAP_G_TO_G_, normalized.size(), normalized.data());
    std::array<GLfloat, 2> returned_normalized{};
    get_map_fv(GL_PIXEL_MAP_G_TO_G_, returned_normalized.data());
    EXPECT_EQ(returned_normalized, (std::array<GLfloat, 2>{0.0f, 1.0f}));

    pixel_map_fv(0xDEAD, 2, component.data());
    EXPECT_EQ(get_error(), GL_INVALID_ENUM_);
    pixel_map_fv(GL_PIXEL_MAP_I_TO_R_, 3, component.data());
    EXPECT_EQ(get_error(), GL_INVALID_VALUE_);
    pixel_map_fv(GL_PIXEL_MAP_R_TO_R_, 0, component.data());
    EXPECT_EQ(get_error(), GL_INVALID_VALUE_);
    pixel_map_fv(GL_PIXEL_MAP_R_TO_R_, 33, component.data());
    EXPECT_EQ(get_error(), GL_INVALID_VALUE_);
    EXPECT_EQ(get_error(), GL_NO_ERROR_);
}

TEST_F(PixelMapLibraryTest, DisplayListOwnsTheCapturedTable) {
    auto pixel_map = Get<void (*)(GLenum, GLsizei, const GLfloat*)>("glPixelMapfv");
    auto get_map = Get<void (*)(GLenum, GLfloat*)>("glGetPixelMapfv");
    auto gen_lists = Get<GLuint (*)(GLsizei)>("glGenLists");
    auto new_list = Get<void (*)(GLuint, GLenum)>("glNewList");
    auto end_list = Get<void (*)()>("glEndList");
    auto call_list = Get<void (*)(GLuint)>("glCallList");

    std::array<GLfloat, 2> source = {0.25f, 0.75f};
    const GLuint list = gen_lists(1);
    ASSERT_NE(list, 0u);
    new_list(list, GL_COMPILE_);
    pixel_map(GL_PIXEL_MAP_B_TO_B_, source.size(), source.data());
    end_list();
    source = {1.0f, 0.0f};

    call_list(list);
    std::array<GLfloat, 2> returned{};
    get_map(GL_PIXEL_MAP_B_TO_B_, returned.data());
    EXPECT_EQ(returned, (std::array<GLfloat, 2>{0.25f, 0.75f}));
}

class PixelMapContextTest : public ContextTest {
protected:
    void SetUp() override {
        ContextTest::SetUp();
        if (::testing::Test::IsSkipped() || ::testing::Test::HasFatalFailure()) return;
        using MakeCurrentFn = EGLBoolean (*)(EGLDisplay, EGLSurface, EGLSurface, EGLContext);
        auto wrapper_make_current = Get<MakeCurrentFn>("eglMakeCurrent");
        ASSERT_TRUE(wrapper_make_current(display(), surface(), surface(), eglGetCurrentContext()));
    }
};

TEST_F(PixelMapContextTest, PackAndUnpackBuffersUseOffsetsWithoutChangingBindings) {
    auto pixel_map = Get<void (*)(GLenum, GLsizei, const GLfloat*)>("glPixelMapfv");
    auto get_map = Get<void (*)(GLenum, GLushort*)>("glGetPixelMapusv");
    auto gen_buffers = Get<void (*)(GLsizei, GLuint*)>("glGenBuffers");
    auto bind_buffer = Get<void (*)(GLenum, GLuint)>("glBindBuffer");
    auto buffer_data =
        Get<void (*)(GLenum, GLsizeiptr, const void*, GLenum)>("glBufferData");
    auto get_buffer_sub_data =
        Get<void (*)(GLenum, GLintptr, GLsizeiptr, void*)>("glGetBufferSubData");
    auto get_integer = Get<void (*)(GLenum, GLint*)>("glGetIntegerv");
    auto get_error = Get<GLenum (*)()>("glGetError");

    const std::array<GLfloat, 4> source = {99.0f, 0.25f, 0.5f, 0.75f};
    GLuint buffers[2] = {};
    gen_buffers(2, buffers);
    ASSERT_NE(buffers[0], 0u);
    ASSERT_NE(buffers[1], 0u);

    bind_buffer(GL_PIXEL_UNPACK_BUFFER_, buffers[0]);
    buffer_data(GL_PIXEL_UNPACK_BUFFER_, sizeof(source), source.data(), GL_STATIC_DRAW_);
    pixel_map(GL_PIXEL_MAP_R_TO_R_, 3,
              reinterpret_cast<const GLfloat*>(static_cast<uintptr_t>(sizeof(GLfloat))));
    GLint binding = 0;
    get_integer(GL_PIXEL_UNPACK_BUFFER_BINDING_, &binding);
    EXPECT_EQ(binding, static_cast<GLint>(buffers[0]));
    EXPECT_EQ(get_error(), GL_NO_ERROR_);

    const std::array<GLushort, 5> empty{};
    bind_buffer(GL_PIXEL_PACK_BUFFER_, buffers[1]);
    buffer_data(GL_PIXEL_PACK_BUFFER_, sizeof(empty), empty.data(), GL_STATIC_DRAW_);
    get_map(GL_PIXEL_MAP_R_TO_R_,
            reinterpret_cast<GLushort*>(static_cast<uintptr_t>(sizeof(GLushort))));
    get_integer(GL_PIXEL_PACK_BUFFER_BINDING_, &binding);
    EXPECT_EQ(binding, static_cast<GLint>(buffers[1]));

    std::array<GLushort, 5> packed{};
    get_buffer_sub_data(GL_PIXEL_PACK_BUFFER_, 0, sizeof(packed), packed.data());
    EXPECT_EQ(packed[1], 16384);
    EXPECT_EQ(packed[2], 32768);
    EXPECT_EQ(packed[3], 49151);
    EXPECT_EQ(get_error(), GL_NO_ERROR_);

    pixel_map(GL_PIXEL_MAP_R_TO_R_, 2,
              reinterpret_cast<const GLfloat*>(static_cast<uintptr_t>(1)));
    EXPECT_EQ(get_error(), GL_INVALID_OPERATION_);
    get_map(GL_PIXEL_MAP_R_TO_R_,
            reinterpret_cast<GLushort*>(static_cast<uintptr_t>(sizeof(packed) - sizeof(GLushort))));
    EXPECT_EQ(get_error(), GL_INVALID_OPERATION_);
}

TEST_F(PixelMapContextTest, MapColorTransformsDrawPixelsAfterScaleAndBias) {
    auto pixel_map = Get<void (*)(GLenum, GLsizei, const GLfloat*)>("glPixelMapfv");
    auto pixel_transfer = Get<void (*)(GLenum, GLint)>("glPixelTransferi");
    auto viewport = Get<void (*)(GLint, GLint, GLsizei, GLsizei)>("glViewport");
    auto clear_color = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glClearColor");
    auto clear = Get<void (*)(GLbitfield)>("glClear");
    auto window_pos = Get<void (*)(GLint, GLint)>("glWindowPos2i");
    auto pixel_zoom = Get<void (*)(GLfloat, GLfloat)>("glPixelZoom");
    auto draw_pixels =
        Get<void (*)(GLsizei, GLsizei, GLenum, GLenum, const void*)>("glDrawPixels");
    auto read_pixels =
        Get<PixelProbe::ReadPixelsFn>("glReadPixels");
    auto get_error = Get<GLenum (*)()>("glGetError");

    const std::array<GLfloat, 2> red = {1.0f, 0.0f};
    const std::array<GLfloat, 2> green = {0.0f, 1.0f};
    const std::array<GLfloat, 3> blue = {0.25f, 0.75f, 0.5f};
    const std::array<GLfloat, 2> alpha = {1.0f, 1.0f};
    pixel_map(GL_PIXEL_MAP_R_TO_R_, red.size(), red.data());
    pixel_map(GL_PIXEL_MAP_G_TO_G_, green.size(), green.data());
    pixel_map(GL_PIXEL_MAP_B_TO_B_, blue.size(), blue.data());
    pixel_map(GL_PIXEL_MAP_A_TO_A_, alpha.size(), alpha.data());
    pixel_transfer(GL_MAP_COLOR_, 1);

    viewport(0, 0, size(), size());
    clear_color(0.0f, 0.0f, 0.0f, 1.0f);
    clear(GL_COLOR_BUFFER_BIT_);
    window_pos(8, 8);
    pixel_zoom(16.0f, 16.0f);
    const std::array<GLubyte, 4> source = {0, 255, 128, 255};
    draw_pixels(1, 1, GL_RGBA_, GL_UNSIGNED_BYTE_, source.data());

    // defects-plan-3.md: glReadPixels now applies GL_MAP_COLOR too (GL 2.1
    // 4.3.1 requires it, same as glDrawPixels/glCopyPixels) - left enabled,
    // the verification read below would run the already-mapped stored
    // pixel back through the table a second time. Disabling it first
    // isolates what this test is actually about: what glDrawPixels wrote.
    pixel_transfer(GL_MAP_COLOR_, 0);
    const auto pixel = PixelProbe(read_pixels).At(16, 16);
    EXPECT_NEAR(pixel.r, 255, 8);
    EXPECT_NEAR(pixel.g, 255, 8);
    EXPECT_NEAR(pixel.b, 191, 8);
    EXPECT_EQ(get_error(), GL_NO_ERROR_);
}

} // namespace
