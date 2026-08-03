// SimpleFPEWrapper - tests/gtest_render.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// First end-to-end FPE render check (plans/11 Q2): every GL call goes
// through the wrapper's resolver. Fourteen phases walk the whole
// fixed-function surface on a real GLES3 device - immediate mode, lighting,
// texturing, client arrays, indexed draws, the element ring, display lists,
// matrix transforms, the alpha test, texenv REPLACE and COMBINE, fog,
// texture-alpha cutouts and the attrib-stack blend restore. Phases share
// state (the phase-3 texture is reused by 10/11/13), so they stay one test
// in the original order.

#include "sfpew_gtest.h"

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

constexpr GLbitfield GL_COLOR_BUFFER_BIT_ = 0x00004000;
constexpr GLenum GL_QUADS_ = 0x0007;
constexpr GLenum GL_RGBA_ = 0x1908;
constexpr GLenum GL_UNSIGNED_BYTE_ = 0x1401;
constexpr GLenum GL_UNSIGNED_SHORT_ = 0x1403;
constexpr GLenum GL_TRIANGLES_ = 0x0004;
constexpr GLenum GL_FLOAT_ = 0x1406;
constexpr GLenum GL_TEXTURE_2D_ = 0x0DE1;
constexpr GLenum GL_NEAREST_ = 0x2600;
constexpr GLenum GL_LIGHTING_ = 0x0B50;
constexpr GLenum GL_LIGHT0_ = 0x4000;
constexpr GLenum GL_DIFFUSE_ = 0x1201;
constexpr GLenum GL_POSITION_ = 0x1203;
constexpr GLenum GL_COLOR_MATERIAL_ = 0x0B57;
constexpr GLenum GL_ALPHA_TEST_ = 0x0BC0;
constexpr GLenum GL_GREATER_ = 0x0204;
constexpr GLenum GL_TEXTURE_ENV_ = 0x2300;
constexpr GLenum GL_TEXTURE_ENV_MODE_ = 0x2200;
constexpr GLenum GL_REPLACE_ = 0x1E01;
constexpr GLenum GL_COMBINE_ = 0x8570;
constexpr GLenum GL_COMBINE_RGB_ = 0x8571;
constexpr GLenum GL_SRC0_RGB_ = 0x8580;
constexpr GLenum GL_CONSTANT_ = 0x8576;
constexpr GLenum GL_OPERAND0_RGB_ = 0x8590;
constexpr GLenum GL_SRC_COLOR_ = 0x0300;
constexpr GLenum GL_COMBINE_ALPHA_ = 0x8572;
constexpr GLenum GL_SRC0_ALPHA_ = 0x8588;
constexpr GLenum GL_TEXTURE_ENV_COLOR_ = 0x2201;
constexpr GLenum GL_MODULATE_ = 0x2100;
constexpr GLenum GL_FOG_ = 0x0B60;
constexpr GLenum GL_FOG_MODE_ = 0x0B65;
constexpr GLenum GL_LINEAR_ = 0x2601;
constexpr GLenum GL_FOG_COLOR_ = 0x0B66;
constexpr GLenum GL_FOG_START_ = 0x0B63;
constexpr GLenum GL_FOG_END_ = 0x0B64;
constexpr GLenum GL_VERTEX_ARRAY_ = 0x8074;
constexpr GLenum GL_COLOR_ARRAY_ = 0x8076;
constexpr GLenum GL_COMPILE_ = 0x1300;
constexpr GLenum GL_BLEND_ = 0x0BE2;
constexpr GLenum GL_ONE_ = 1;
constexpr GLenum GL_SRC_ALPHA_ = 0x0302;
constexpr GLenum GL_ONE_MINUS_SRC_ALPHA_ = 0x0303;
constexpr GLbitfield GL_COLOR_BUFFER_BIT_ATTRIB_ = 0x00004000;
constexpr GLbitfield GL_ENABLE_BIT_ = 0x00002000;
constexpr GLenum GL_NO_ERROR_ = 0;

class RenderTest : public ContextTest {
protected:
    void SetUp() override {
        ContextTest::SetUp();
        if (::testing::Test::IsSkipped()) return;

        clear_color_ = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glClearColor");
        clear_ = Get<void (*)(GLbitfield)>("glClear");
        begin_ = Get<void (*)(GLenum)>("glBegin");
        end_ = Get<void (*)()>("glEnd");
        color3f_ = Get<void (*)(GLfloat, GLfloat, GLfloat)>("glColor3f");
        color4f_ = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glColor4f");
        vertex2f_ = Get<void (*)(GLfloat, GLfloat)>("glVertex2f");
        read_pixels_ =
            Get<void (*)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*)>("glReadPixels");
        get_error_ = Get<GLenum (*)()>("glGetError");
        enable_ = Get<void (*)(GLenum)>("glEnable");
        disable_ = Get<void (*)(GLenum)>("glDisable");
        lightfv_ = Get<void (*)(GLenum, GLenum, const GLfloat*)>("glLightfv");
        normal3f_ = Get<void (*)(GLfloat, GLfloat, GLfloat)>("glNormal3f");
        gen_textures_ = Get<void (*)(GLsizei, GLuint*)>("glGenTextures");
        bind_texture_ = Get<void (*)(GLenum, GLuint)>("glBindTexture");
        tex_image2d_ =
            Get<void (*)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum,
                         const void*)>("glTexImage2D");
        tex_parameteri_ = Get<void (*)(GLenum, GLenum, GLint)>("glTexParameteri");
        tex_coord2f_ = Get<void (*)(GLfloat, GLfloat)>("glTexCoord2f");
        enable_client_state_ = Get<void (*)(GLenum)>("glEnableClientState");
        disable_client_state_ = Get<void (*)(GLenum)>("glDisableClientState");
        vertex_pointer_ = Get<void (*)(GLint, GLenum, GLsizei, const void*)>("glVertexPointer");
        color_pointer_ = Get<void (*)(GLint, GLenum, GLsizei, const void*)>("glColorPointer");
        draw_arrays_ = Get<void (*)(GLenum, GLint, GLsizei)>("glDrawArrays");
        draw_elements_ = Get<void (*)(GLenum, GLsizei, GLenum, const void*)>("glDrawElements");
        gen_lists_ = Get<GLuint (*)(GLsizei)>("glGenLists");
        new_list_ = Get<void (*)(GLuint, GLenum)>("glNewList");
        end_list_ = Get<void (*)()>("glEndList");
        call_list_ = Get<void (*)(GLuint)>("glCallList");
        translatef_ = Get<void (*)(GLfloat, GLfloat, GLfloat)>("glTranslatef");
        load_identity_ = Get<void (*)()>("glLoadIdentity");
        alpha_func_ = Get<void (*)(GLenum, GLfloat)>("glAlphaFunc");
        tex_envi_ = Get<void (*)(GLenum, GLenum, GLint)>("glTexEnvi");
        tex_envfv_ = Get<void (*)(GLenum, GLenum, const GLfloat*)>("glTexEnvfv");
        fogf_ = Get<void (*)(GLenum, GLfloat)>("glFogf");
        fogi_ = Get<void (*)(GLenum, GLint)>("glFogi");
        fogfv_ = Get<void (*)(GLenum, const GLfloat*)>("glFogfv");
        push_attrib_ = Get<void (*)(GLbitfield)>("glPushAttrib");
        pop_attrib_ = Get<void (*)()>("glPopAttrib");
        blend_func_ = Get<void (*)(GLenum, GLenum)>("glBlendFunc");
        ASSERT_NE(read_pixels_, nullptr);
        get_error_();
    }

    void Quad(GLfloat r, GLfloat g, GLfloat b, bool with_texcoords = false) {
        begin_(GL_QUADS_);
        color3f_(r, g, b);
        if (with_texcoords) tex_coord2f_(0.0f, 0.0f);
        vertex2f_(-1.0f, -1.0f);
        if (with_texcoords) tex_coord2f_(1.0f, 0.0f);
        vertex2f_(1.0f, -1.0f);
        if (with_texcoords) tex_coord2f_(1.0f, 1.0f);
        vertex2f_(1.0f, 1.0f);
        if (with_texcoords) tex_coord2f_(0.0f, 1.0f);
        vertex2f_(-1.0f, 1.0f);
        end_();
    }

    void Pixel(GLubyte* out) { read_pixels_(32, 32, 1, 1, GL_RGBA_, GL_UNSIGNED_BYTE_, out); }

    void (*clear_color_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*clear_)(GLbitfield) = nullptr;
    void (*begin_)(GLenum) = nullptr;
    void (*end_)() = nullptr;
    void (*color3f_)(GLfloat, GLfloat, GLfloat) = nullptr;
    void (*color4f_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*vertex2f_)(GLfloat, GLfloat) = nullptr;
    void (*read_pixels_)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*) = nullptr;
    GLenum (*get_error_)() = nullptr;
    void (*enable_)(GLenum) = nullptr;
    void (*disable_)(GLenum) = nullptr;
    void (*lightfv_)(GLenum, GLenum, const GLfloat*) = nullptr;
    void (*normal3f_)(GLfloat, GLfloat, GLfloat) = nullptr;
    void (*gen_textures_)(GLsizei, GLuint*) = nullptr;
    void (*bind_texture_)(GLenum, GLuint) = nullptr;
    void (*tex_image2d_)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum,
                         const void*) = nullptr;
    void (*tex_parameteri_)(GLenum, GLenum, GLint) = nullptr;
    void (*tex_coord2f_)(GLfloat, GLfloat) = nullptr;
    void (*enable_client_state_)(GLenum) = nullptr;
    void (*disable_client_state_)(GLenum) = nullptr;
    void (*vertex_pointer_)(GLint, GLenum, GLsizei, const void*) = nullptr;
    void (*color_pointer_)(GLint, GLenum, GLsizei, const void*) = nullptr;
    void (*draw_arrays_)(GLenum, GLint, GLsizei) = nullptr;
    void (*draw_elements_)(GLenum, GLsizei, GLenum, const void*) = nullptr;
    GLuint (*gen_lists_)(GLsizei) = nullptr;
    void (*new_list_)(GLuint, GLenum) = nullptr;
    void (*end_list_)() = nullptr;
    void (*call_list_)(GLuint) = nullptr;
    void (*translatef_)(GLfloat, GLfloat, GLfloat) = nullptr;
    void (*load_identity_)() = nullptr;
    void (*alpha_func_)(GLenum, GLfloat) = nullptr;
    void (*tex_envi_)(GLenum, GLenum, GLint) = nullptr;
    void (*tex_envfv_)(GLenum, GLenum, const GLfloat*) = nullptr;
    void (*fogf_)(GLenum, GLfloat) = nullptr;
    void (*fogi_)(GLenum, GLint) = nullptr;
    void (*fogfv_)(GLenum, const GLfloat*) = nullptr;
    void (*push_attrib_)(GLbitfield) = nullptr;
    void (*pop_attrib_)() = nullptr;
    void (*blend_func_)(GLenum, GLenum) = nullptr;
};

TEST_F(RenderTest, AllFourteenFpePhasesRenderOnTheDevice) {
    GLubyte pixel[4] = {};

    // Phase 1: immediate-mode quad is red.
    clear_color_(0.0f, 0.0f, 1.0f, 1.0f);
    clear_(GL_COLOR_BUFFER_BIT_);
    Quad(1.0f, 0.0f, 0.0f);
    Pixel(pixel);
    ASSERT_EQ(get_error_(), GL_NO_ERROR_);
    EXPECT_TRUE(pixel[0] >= 200 && pixel[1] <= 50 && pixel[2] <= 50)
        << "phase 1: immediate quad (" << (int)pixel[0] << ',' << (int)pixel[1] << ','
        << (int)pixel[2] << ") expected red";

    // Phase 2: vertex lighting keeps the red quad bright.
    enable_(GL_LIGHTING_);
    enable_(GL_LIGHT0_);
    static const GLfloat white[4] = {1, 1, 1, 1};
    static const GLfloat dir[4] = {0, 0, 1, 0};
    lightfv_(GL_LIGHT0_, GL_DIFFUSE_, white);
    lightfv_(GL_LIGHT0_, GL_POSITION_, dir);
    enable_(GL_COLOR_MATERIAL_);
    clear_(GL_COLOR_BUFFER_BIT_);
    begin_(GL_QUADS_);
    color3f_(1.0f, 0.0f, 0.0f);
    normal3f_(0.0f, 0.0f, 1.0f);
    vertex2f_(-1.0f, -1.0f);
    vertex2f_(1.0f, -1.0f);
    vertex2f_(1.0f, 1.0f);
    vertex2f_(-1.0f, 1.0f);
    end_();
    Pixel(pixel);
    ASSERT_EQ(get_error_(), GL_NO_ERROR_);
    EXPECT_TRUE(pixel[0] >= 150 && pixel[1] <= 80 && pixel[2] <= 80)
        << "phase 2: lit pixel (" << (int)pixel[0] << ',' << (int)pixel[1] << ',' << (int)pixel[2]
        << ") expected bright red";
    disable_(GL_LIGHTING_);
    disable_(GL_COLOR_MATERIAL_);

    // Phase 3: GL_MODULATE texturing renders green.
    GLuint texture = 0;
    gen_textures_(1, &texture);
    bind_texture_(GL_TEXTURE_2D_, texture);
    static const GLubyte green_texel[4] = {0, 255, 0, 255};
    tex_image2d_(GL_TEXTURE_2D_, 0, GL_RGBA_, 1, 1, 0, GL_RGBA_, GL_UNSIGNED_BYTE_, green_texel);
    tex_parameteri_(GL_TEXTURE_2D_, 0x2801, GL_NEAREST_);
    tex_parameteri_(GL_TEXTURE_2D_, 0x2800, GL_NEAREST_);
    enable_(GL_TEXTURE_2D_);
    clear_(GL_COLOR_BUFFER_BIT_);
    Quad(1.0f, 1.0f, 1.0f, /*with_texcoords=*/true);
    Pixel(pixel);
    ASSERT_EQ(get_error_(), GL_NO_ERROR_);
    EXPECT_TRUE(pixel[1] >= 200 && pixel[0] <= 50 && pixel[2] <= 50)
        << "phase 3: textured pixel (" << (int)pixel[0] << ',' << (int)pixel[1] << ','
        << (int)pixel[2] << ") expected green";
    disable_(GL_TEXTURE_2D_);

    // Phase 4: client vertex arrays render magenta.
    static const GLfloat verts[] = {-1, -1, 1, -1, 1, 1, -1, -1, 1, 1, -1, 1};
    static const GLfloat colors[] = {1, 0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1,
                                     1, 0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1};
    clear_(GL_COLOR_BUFFER_BIT_);
    enable_client_state_(GL_VERTEX_ARRAY_);
    enable_client_state_(GL_COLOR_ARRAY_);
    vertex_pointer_(2, GL_FLOAT_, 0, verts);
    color_pointer_(4, GL_FLOAT_, 0, colors);
    draw_arrays_(GL_TRIANGLES_, 0, 6);
    Pixel(pixel);
    EXPECT_TRUE(get_error_() == GL_NO_ERROR_ && pixel[0] >= 200 && pixel[1] <= 50 &&
                pixel[2] >= 200)
        << "phase 4: array draw pixel (" << (int)pixel[0] << ',' << (int)pixel[1] << ','
        << (int)pixel[2] << ')';

    // Phase 5: client-index glDrawElements renders yellow.
    static const GLfloat quad_verts[] = {-1, -1, 1, -1, 1, 1, -1, 1};
    static const GLfloat yellow[] = {1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0, 1};
    static const unsigned short tri_idx[] = {0, 1, 2, 0, 2, 3};
    clear_(GL_COLOR_BUFFER_BIT_);
    vertex_pointer_(2, GL_FLOAT_, 0, quad_verts);
    color_pointer_(4, GL_FLOAT_, 0, yellow);
    draw_elements_(GL_TRIANGLES_, 6, GL_UNSIGNED_SHORT_, tri_idx);
    Pixel(pixel);
    EXPECT_TRUE(get_error_() == GL_NO_ERROR_ && pixel[0] >= 200 && pixel[1] >= 200 &&
                pixel[2] <= 50)
        << "phase 5: DrawElements pixel (" << (int)pixel[0] << ',' << (int)pixel[1] << ','
        << (int)pixel[2] << ')';

    // Phase 6: indexed GL_QUADS rewrite renders yellow.
    static const unsigned short quad_idx[] = {0, 1, 2, 3};
    clear_(GL_COLOR_BUFFER_BIT_);
    draw_elements_(GL_QUADS_, 4, GL_UNSIGNED_SHORT_, quad_idx);
    Pixel(pixel);
    EXPECT_TRUE(get_error_() == GL_NO_ERROR_ && pixel[0] >= 200 && pixel[1] >= 200 &&
                pixel[2] <= 50)
        << "phase 6: indexed QUADS pixel (" << (int)pixel[0] << ',' << (int)pixel[1] << ','
        << (int)pixel[2] << ')';

    // Phase 6b: the element ring survives wrap-around.
    static const GLfloat corner_verts[] = {-1, -1, 1, -1, 1, 1, -1, 1};
    static const GLfloat green4[] = {0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1};
    static unsigned short lap_idx[2][60000];
    for (int set = 0; set < 2; ++set) {
        for (int i = 0; i < 60000; i += 3) {
            lap_idx[set][i + 0] = set ? 2 : 0;
            lap_idx[set][i + 1] = set ? 3 : 1;
            lap_idx[set][i + 2] = set ? 0 : 2;
        }
    }
    vertex_pointer_(2, GL_FLOAT_, 0, corner_verts);
    color_pointer_(4, GL_FLOAT_, 0, green4);
    clear_(GL_COLOR_BUFFER_BIT_);
    for (int d = 0; d < 100; ++d) draw_elements_(GL_TRIANGLES_, 60000, GL_UNSIGNED_SHORT_, lap_idx[d & 1]);
    Pixel(pixel);
    EXPECT_TRUE(get_error_() == GL_NO_ERROR_ && pixel[1] >= 200 && pixel[0] <= 50 &&
                pixel[2] <= 50)
        << "phase 6b: element-ring wrap pixel (" << (int)pixel[0] << ',' << (int)pixel[1] << ','
        << (int)pixel[2] << ") expected green";
    disable_client_state_(GL_VERTEX_ARRAY_);
    disable_client_state_(GL_COLOR_ARRAY_);

    // Phase 7: GL_COMPILE defers, replay renders cyan.
    const GLuint list = gen_lists_(1);
    new_list_(list, GL_COMPILE_);
    begin_(GL_QUADS_);
    color3f_(0.0f, 1.0f, 1.0f);
    vertex2f_(-1.0f, -1.0f);
    vertex2f_(1.0f, -1.0f);
    vertex2f_(1.0f, 1.0f);
    vertex2f_(-1.0f, 1.0f);
    end_();
    end_list_();
    clear_color_(0.0f, 0.0f, 0.0f, 1.0f);
    clear_(GL_COLOR_BUFFER_BIT_);
    Pixel(pixel);
    EXPECT_TRUE(pixel[1] <= 50) << "phase 7: GL_COMPILE must not draw while recording";
    call_list_(list);
    Pixel(pixel);
    EXPECT_TRUE(get_error_() == GL_NO_ERROR_ && pixel[1] >= 200 && pixel[2] >= 200 &&
                pixel[0] <= 50)
        << "phase 7: replay pixel (" << (int)pixel[0] << ',' << (int)pixel[1] << ','
        << (int)pixel[2] << ") expected cyan";

    // Phase 8: glTranslatef moves geometry correctly.
    clear_color_(0.0f, 0.0f, 1.0f, 1.0f);
    clear_(GL_COLOR_BUFFER_BIT_);
    translatef_(1.0f, 0.0f, 0.0f);
    begin_(GL_QUADS_);
    color3f_(1.0f, 0.0f, 0.0f);
    vertex2f_(-1.0f, -1.0f);
    vertex2f_(0.0f, -1.0f);
    vertex2f_(0.0f, 1.0f);
    vertex2f_(-1.0f, 1.0f);
    end_();
    load_identity_();
    GLubyte left[4], right[4];
    read_pixels_(8, 32, 1, 1, GL_RGBA_, GL_UNSIGNED_BYTE_, left);
    read_pixels_(48, 32, 1, 1, GL_RGBA_, GL_UNSIGNED_BYTE_, right);
    EXPECT_TRUE(get_error_() == GL_NO_ERROR_ && right[0] >= 200 && left[2] >= 200 &&
                left[0] <= 50)
        << "phase 8: transform left=(" << (int)left[0] << ',' << (int)left[1] << ','
        << (int)left[2] << ") right=(" << (int)right[0] << ',' << (int)right[1] << ','
        << (int)right[2] << ')';

    // Phase 9: alpha test discards low-alpha fragments.
    clear_(GL_COLOR_BUFFER_BIT_);
    enable_(GL_ALPHA_TEST_);
    alpha_func_(GL_GREATER_, 0.5f);
    begin_(GL_QUADS_);
    color4f_(1.0f, 0.0f, 0.0f, 0.25f);
    vertex2f_(-1.0f, -1.0f);
    vertex2f_(1.0f, -1.0f);
    vertex2f_(1.0f, 1.0f);
    vertex2f_(-1.0f, 1.0f);
    end_();
    disable_(GL_ALPHA_TEST_);
    Pixel(pixel);
    EXPECT_TRUE(get_error_() == GL_NO_ERROR_ && pixel[2] >= 200 && pixel[0] <= 50)
        << "phase 9: alpha test pixel (" << (int)pixel[0] << ',' << (int)pixel[1] << ','
        << (int)pixel[2] << ") expected background blue";

    // Phase 10: GL_REPLACE texenv ignores the vertex color.
    bind_texture_(GL_TEXTURE_2D_, texture);
    enable_(GL_TEXTURE_2D_);
    tex_envi_(GL_TEXTURE_ENV_, GL_TEXTURE_ENV_MODE_, GL_REPLACE_);
    clear_(GL_COLOR_BUFFER_BIT_);
    begin_(GL_QUADS_);
    color3f_(1.0f, 0.0f, 0.0f);
    tex_coord2f_(0.0f, 0.0f);
    vertex2f_(-1.0f, -1.0f);
    tex_coord2f_(1.0f, 0.0f);
    vertex2f_(1.0f, -1.0f);
    tex_coord2f_(1.0f, 1.0f);
    vertex2f_(1.0f, 1.0f);
    tex_coord2f_(0.0f, 1.0f);
    vertex2f_(-1.0f, 1.0f);
    end_();
    Pixel(pixel);
    EXPECT_TRUE(get_error_() == GL_NO_ERROR_ && pixel[1] >= 200 && pixel[0] <= 50)
        << "phase 10: REPLACE pixel (" << (int)pixel[0] << ',' << (int)pixel[1] << ','
        << (int)pixel[2] << ')';

    // Phase 11: GL_COMBINE constant replace renders orange.
    static const GLfloat env_color[4] = {1.0f, 0.5f, 0.0f, 1.0f};
    tex_envi_(GL_TEXTURE_ENV_, GL_TEXTURE_ENV_MODE_, GL_COMBINE_);
    tex_envi_(GL_TEXTURE_ENV_, GL_COMBINE_RGB_, GL_REPLACE_);
    tex_envi_(GL_TEXTURE_ENV_, GL_SRC0_RGB_, GL_CONSTANT_);
    tex_envi_(GL_TEXTURE_ENV_, GL_OPERAND0_RGB_, GL_SRC_COLOR_);
    tex_envi_(GL_TEXTURE_ENV_, GL_COMBINE_ALPHA_, GL_REPLACE_);
    tex_envi_(GL_TEXTURE_ENV_, GL_SRC0_ALPHA_, GL_CONSTANT_);
    tex_envfv_(GL_TEXTURE_ENV_, GL_TEXTURE_ENV_COLOR_, env_color);
    clear_(GL_COLOR_BUFFER_BIT_);
    begin_(GL_QUADS_);
    color3f_(0.0f, 1.0f, 0.0f);
    tex_coord2f_(0.0f, 0.0f);
    vertex2f_(-1.0f, -1.0f);
    tex_coord2f_(1.0f, 0.0f);
    vertex2f_(1.0f, -1.0f);
    tex_coord2f_(1.0f, 1.0f);
    vertex2f_(1.0f, 1.0f);
    tex_coord2f_(0.0f, 1.0f);
    vertex2f_(-1.0f, 1.0f);
    end_();
    Pixel(pixel);
    EXPECT_TRUE(get_error_() == GL_NO_ERROR_ && pixel[0] >= 200 && pixel[1] >= 100 &&
                pixel[1] <= 160 && pixel[2] <= 50)
        << "phase 11: COMBINE pixel (" << (int)pixel[0] << ',' << (int)pixel[1] << ','
        << (int)pixel[2] << ") expected orange";
    tex_envi_(GL_TEXTURE_ENV_, GL_TEXTURE_ENV_MODE_, GL_MODULATE_);
    disable_(GL_TEXTURE_2D_);

    // Phase 12: saturated linear fog renders the fog color.
    static const GLfloat fog_color[4] = {0.0f, 1.0f, 1.0f, 1.0f};
    enable_(GL_FOG_);
    fogi_(GL_FOG_MODE_, GL_LINEAR_);
    fogfv_(GL_FOG_COLOR_, fog_color);
    fogf_(GL_FOG_START_, -10.0f);
    fogf_(GL_FOG_END_, -9.0f);
    clear_(GL_COLOR_BUFFER_BIT_);
    Quad(1.0f, 0.0f, 0.0f);
    disable_(GL_FOG_);
    Pixel(pixel);
    EXPECT_TRUE(get_error_() == GL_NO_ERROR_ && pixel[1] >= 200 && pixel[2] >= 200 &&
                pixel[0] <= 50)
        << "phase 12: fog pixel (" << (int)pixel[0] << ',' << (int)pixel[1] << ','
        << (int)pixel[2] << ") expected fog cyan";

    // Phase 13: texture-alpha cutout culls exactly the transparent texels.
    static const GLubyte cutout_texels[2 * 4] = {
        255, 0, 0, 0,
        255, 0, 0, 255,
    };
    GLuint cutout_texture = 0;
    gen_textures_(1, &cutout_texture);
    bind_texture_(GL_TEXTURE_2D_, cutout_texture);
    tex_image2d_(GL_TEXTURE_2D_, 0, GL_RGBA_, 2, 1, 0, GL_RGBA_, GL_UNSIGNED_BYTE_,
                 cutout_texels);
    tex_parameteri_(GL_TEXTURE_2D_, 0x2801, GL_NEAREST_);
    tex_parameteri_(GL_TEXTURE_2D_, 0x2800, GL_NEAREST_);
    enable_(GL_TEXTURE_2D_);
    enable_(GL_ALPHA_TEST_);
    alpha_func_(GL_GREATER_, 0.1f);
    clear_(GL_COLOR_BUFFER_BIT_);
    begin_(GL_QUADS_);
    color4f_(1.0f, 1.0f, 1.0f, 1.0f);
    tex_coord2f_(0.0f, 0.0f);
    vertex2f_(-1.0f, -1.0f);
    tex_coord2f_(1.0f, 0.0f);
    vertex2f_(1.0f, -1.0f);
    tex_coord2f_(1.0f, 1.0f);
    vertex2f_(1.0f, 1.0f);
    tex_coord2f_(0.0f, 1.0f);
    vertex2f_(-1.0f, 1.0f);
    end_();
    disable_(GL_ALPHA_TEST_);
    disable_(GL_TEXTURE_2D_);
    GLubyte transparent_half[4] = {};
    GLubyte opaque_half[4] = {};
    read_pixels_(16, 32, 1, 1, GL_RGBA_, GL_UNSIGNED_BYTE_, transparent_half);
    read_pixels_(48, 32, 1, 1, GL_RGBA_, GL_UNSIGNED_BYTE_, opaque_half);
    ASSERT_EQ(get_error_(), GL_NO_ERROR_) << "phase 13 raised a GL error";
    EXPECT_TRUE(transparent_half[2] >= 200 && transparent_half[0] <= 50)
        << "phase 13: alpha-0 texels NOT culled: (" << (int)transparent_half[0] << ','
        << (int)transparent_half[1] << ',' << (int)transparent_half[2]
        << ") expected background blue";
    EXPECT_TRUE(opaque_half[0] >= 200 && opaque_half[2] <= 50)
        << "phase 13: opaque texels wrongly culled: (" << (int)opaque_half[0] << ','
        << (int)opaque_half[1] << ',' << (int)opaque_half[2] << ") expected red";

    // Phase 14: glPushAttrib/glPopAttrib restores the blend state.
    clear_(GL_COLOR_BUFFER_BIT_);
    push_attrib_(GL_COLOR_BUFFER_BIT_ATTRIB_);
    enable_(GL_BLEND_);
    blend_func_(GL_ONE_, GL_ONE_);
    pop_attrib_();
    begin_(GL_QUADS_);
    color4f_(1.0f, 0.0f, 0.0f, 0.5f);
    vertex2f_(-1.0f, -1.0f);
    vertex2f_(1.0f, -1.0f);
    vertex2f_(1.0f, 1.0f);
    vertex2f_(-1.0f, 1.0f);
    end_();
    Pixel(pixel);
    ASSERT_EQ(get_error_(), GL_NO_ERROR_);
    EXPECT_TRUE(pixel[0] >= 200 && pixel[2] <= 50)
        << "phase 14: pixel (" << (int)pixel[0] << ',' << (int)pixel[1] << ',' << (int)pixel[2]
        << ") expected opaque red - glPopAttrib did not restore the blend state";
    // GL_ENABLE_BIT is the more common legacy bracket and covers the enable
    // flags only, GL_BLEND among them.
    clear_(GL_COLOR_BUFFER_BIT_);
    push_attrib_(GL_ENABLE_BIT_);
    enable_(GL_BLEND_);
    blend_func_(GL_ONE_, GL_ONE_);
    pop_attrib_();
    begin_(GL_QUADS_);
    color4f_(1.0f, 0.0f, 0.0f, 0.5f);
    vertex2f_(-1.0f, -1.0f);
    vertex2f_(1.0f, -1.0f);
    vertex2f_(1.0f, 1.0f);
    vertex2f_(-1.0f, 1.0f);
    end_();
    Pixel(pixel);
    EXPECT_TRUE(get_error_() == GL_NO_ERROR_ && pixel[0] >= 200 && pixel[2] <= 50)
        << "phase 14 GL_ENABLE_BIT: pixel (" << (int)pixel[0] << ',' << (int)pixel[1] << ','
        << (int)pixel[2] << ") expected opaque red - glPopAttrib did not restore GL_BLEND";
    blend_func_(GL_SRC_ALPHA_, GL_ONE_MINUS_SRC_ALPHA_);
}

} // namespace
