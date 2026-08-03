// SimpleFPEWrapper - tests/gtest_mixed_pipeline.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// plans/09 S9 mixed pipeline (the OptiFine shader-pack shape, captured in
// sfpew-bad-shaderpack.rdc): a USER program bound while geometry arrives
// through FIXED-FUNCTION channels. The fragment shader swaps R<->B, so a
// red input rendering BLUE proves the user shader really ran - the old
// bug substituted the wrapper's internal FPE program (identity colors) or
// dropped the draw entirely (GL_QUADS passthrough, unwired attributes).
//   A: glVertexPointer/glColorPointer (independent client arrays) +
//      glDrawArrays(GL_QUADS)
//   B: glBegin/glEnd immediate quad
//   C: glDrawElements(GL_QUADS, client ushort indices)
//   D: glBindAttribLocation pinning a custom attribute (mc_Entity-style)
//      must survive translation (no hardcoded layout(location) override).
//   E: the emulated alpha test applies to user programs (cutout foliage).
//   F: glDrawRangeElements / glMultiDraw* honor the same plumbing.

#include "sfpew_gtest.h"

#include <optional>

namespace {

using sfpew_test::ContextTest;
using sfpew_test::GLbitfield;
using sfpew_test::GLchar;
using sfpew_test::GLenum;
using sfpew_test::GLfloat;
using sfpew_test::GLint;
using sfpew_test::GLsizei;
using sfpew_test::GLubyte;
using sfpew_test::GLuint;
using sfpew_test::GLushort;

constexpr int kWindow = 256;
constexpr GLenum GL_QUADS_ = 0x0007;
constexpr GLbitfield GL_COLOR_BUFFER_BIT_ = 0x00004000;
constexpr GLenum GL_VERTEX_ARRAY_ = 0x8074;
constexpr GLenum GL_COLOR_ARRAY_ = 0x8076;
constexpr GLenum GL_FLOAT_ = 0x1406;
constexpr GLenum GL_UNSIGNED_SHORT_ = 0x1403;
constexpr GLenum GL_COMPILE_STATUS_ = 0x8B81;
constexpr GLenum GL_LINK_STATUS_ = 0x8B82;
constexpr GLenum GL_VERTEX_SHADER_ = 0x8B31;
constexpr GLenum GL_FRAGMENT_SHADER_ = 0x8B30;
constexpr GLenum GL_ALPHA_TEST_ = 0x0BC0;
constexpr GLenum GL_GREATER_ = 0x0204;
constexpr GLenum GL_LESS_ = 0x0201;
constexpr GLenum GL_NO_ERROR_ = 0;

class MixedPipelineTest : public ContextTest {
protected:
    MixedPipelineTest() : ContextTest(sfpew_test::Backend::GLES3, kWindow) {}

    void SetUp() override {
        ContextTest::SetUp();
        if (::testing::Test::IsSkipped()) return;

        create_shader_ = Get<GLuint (*)(GLenum)>("glCreateShader");
        shader_source_ = Get<void (*)(GLuint, GLsizei, const GLchar* const*, const GLint*)>(
            "glShaderSource");
        compile_shader_ = Get<void (*)(GLuint)>("glCompileShader");
        get_shaderiv_ = Get<void (*)(GLuint, GLenum, GLint*)>("glGetShaderiv");
        get_shader_info_log_ = Get<void (*)(GLuint, GLsizei, GLsizei*, GLchar*)>("glGetShaderInfoLog");
        create_program_ = Get<GLuint (*)()>("glCreateProgram");
        attach_shader_ = Get<void (*)(GLuint, GLuint)>("glAttachShader");
        link_program_ = Get<void (*)(GLuint)>("glLinkProgram");
        get_programiv_ = Get<void (*)(GLuint, GLenum, GLint*)>("glGetProgramiv");
        use_program_ = Get<void (*)(GLuint)>("glUseProgram");
        bind_attrib_location_ = Get<void (*)(GLuint, GLuint, const GLchar*)>("glBindAttribLocation");
        vertex_attrib4f_ = Get<void (*)(GLuint, GLfloat, GLfloat, GLfloat, GLfloat)>(
            "glVertexAttrib4f");
        clear_color_ = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glClearColor");
        clear_ = Get<void (*)(GLbitfield)>("glClear");
        enable_client_state_ = Get<void (*)(GLenum)>("glEnableClientState");
        disable_client_state_ = Get<void (*)(GLenum)>("glDisableClientState");
        vertex_pointer_ = Get<void (*)(GLint, GLenum, GLsizei, const void*)>("glVertexPointer");
        color_pointer_ = Get<void (*)(GLint, GLenum, GLsizei, const void*)>("glColorPointer");
        draw_arrays_ = Get<void (*)(GLenum, GLint, GLsizei)>("glDrawArrays");
        draw_elements_ = Get<void (*)(GLenum, GLsizei, GLenum, const void*)>("glDrawElements");
        begin_ = Get<void (*)(GLenum)>("glBegin");
        end_ = Get<void (*)()>("glEnd");
        color4f_ = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glColor4f");
        vertex2f_ = Get<void (*)(GLfloat, GLfloat)>("glVertex2f");
        alpha_func_ = Get<void (*)(GLenum, GLfloat)>("glAlphaFunc");
        enable_ = Get<void (*)(GLenum)>("glEnable");
        disable_ = Get<void (*)(GLenum)>("glDisable");
        draw_range_elements_ = Get<void (*)(GLenum, GLuint, GLuint, GLsizei, GLenum,
                                            const void*)>("glDrawRangeElements");
        multi_draw_arrays_ =
            Get<void (*)(GLenum, const GLint*, const GLsizei*, GLsizei)>("glMultiDrawArrays");
        multi_draw_elements_ = Get<void (*)(GLenum, const GLsizei*, GLenum, const void* const*,
                                            GLsizei)>("glMultiDrawElements");
        get_error_ = Get<GLenum (*)()>("glGetError");
        read_pixels_ =
            Get<void (*)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*)>("glReadPixels");
        ASSERT_NE(read_pixels_, nullptr);
        get_error_();
    }

    GLuint CompileStage(GLenum stage, const char* src, const char* tag) {
        const GLuint shader = create_shader_(stage);
        shader_source_(shader, 1, &src, nullptr);
        compile_shader_(shader);
        GLint ok = 0;
        get_shaderiv_(shader, GL_COMPILE_STATUS_, &ok);
        if (ok == 0) {
            char log[4096] = {};
            get_shader_info_log_(shader, sizeof log, nullptr, log);
            ADD_FAILURE() << tag << " compile:\n" << log;
            return 0;
        }
        return shader;
    }

    bool CheckCenter(const char* tag, int r, int g, int b) {
        GLubyte px[4] = {};
        read_pixels_(kWindow / 2, kWindow / 2, 1, 1, 0x1908 /* GL_RGBA */, 0x1401, px);
        const int tol = 8;
        const bool ok = px[0] >= r - tol && px[0] <= r + tol && px[1] >= g - tol &&
                        px[1] <= g + tol && px[2] >= b - tol && px[2] <= b + tol;
        EXPECT_TRUE(ok) << tag << ": center pixel (" << (int)px[0] << ',' << (int)px[1] << ','
                        << (int)px[2] << "), expected (" << r << ',' << g << ',' << b << ')';
        return ok;
    }

    GLuint (*create_shader_)(GLenum) = nullptr;
    void (*shader_source_)(GLuint, GLsizei, const GLchar* const*, const GLint*) = nullptr;
    void (*compile_shader_)(GLuint) = nullptr;
    void (*get_shaderiv_)(GLuint, GLenum, GLint*) = nullptr;
    void (*get_shader_info_log_)(GLuint, GLsizei, GLsizei*, GLchar*) = nullptr;
    GLuint (*create_program_)() = nullptr;
    void (*attach_shader_)(GLuint, GLuint) = nullptr;
    void (*link_program_)(GLuint) = nullptr;
    void (*get_programiv_)(GLuint, GLenum, GLint*) = nullptr;
    void (*use_program_)(GLuint) = nullptr;
    void (*bind_attrib_location_)(GLuint, GLuint, const GLchar*) = nullptr;
    void (*vertex_attrib4f_)(GLuint, GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*clear_color_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*clear_)(GLbitfield) = nullptr;
    void (*enable_client_state_)(GLenum) = nullptr;
    void (*disable_client_state_)(GLenum) = nullptr;
    void (*vertex_pointer_)(GLint, GLenum, GLsizei, const void*) = nullptr;
    void (*color_pointer_)(GLint, GLenum, GLsizei, const void*) = nullptr;
    void (*draw_arrays_)(GLenum, GLint, GLsizei) = nullptr;
    void (*draw_elements_)(GLenum, GLsizei, GLenum, const void*) = nullptr;
    void (*begin_)(GLenum) = nullptr;
    void (*end_)() = nullptr;
    void (*color4f_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*vertex2f_)(GLfloat, GLfloat) = nullptr;
    void (*alpha_func_)(GLenum, GLfloat) = nullptr;
    void (*enable_)(GLenum) = nullptr;
    void (*disable_)(GLenum) = nullptr;
    void (*draw_range_elements_)(GLenum, GLuint, GLuint, GLsizei, GLenum, const void*) = nullptr;
    void (*multi_draw_arrays_)(GLenum, const GLint*, const GLsizei*, GLsizei) = nullptr;
    void (*multi_draw_elements_)(GLenum, const GLsizei*, GLenum, const void* const*, GLsizei) =
        nullptr;
    GLenum (*get_error_)() = nullptr;
    void (*read_pixels_)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*) = nullptr;
};

TEST_F(MixedPipelineTest, UserProgramConsumesFixedFunctionChannels) {
    // R<->B swapping shader pair: proves WHICH program produced the pixel.
    static const char* k_vs = "#version 120\n"
                              "varying vec4 col;\n"
                              "void main() { gl_Position = gl_Vertex; col = gl_Color; }\n";
    static const char* k_fs = "#version 120\n"
                              "varying vec4 col;\n"
                              "void main() { gl_FragColor = vec4(col.b, col.g, col.r, 1.0); }\n";
    const GLuint vs = CompileStage(GL_VERTEX_SHADER_, k_vs, "vs");
    const GLuint fs = CompileStage(GL_FRAGMENT_SHADER_, k_fs, "fs");
    ASSERT_NE(vs, 0u);
    ASSERT_NE(fs, 0u);
    GLuint program = create_program_();
    attach_shader_(program, vs);
    attach_shader_(program, fs);
    link_program_(program);
    GLint linked = 0;
    get_programiv_(program, GL_LINK_STATUS_, &linked);
    ASSERT_NE(linked, 0) << "link";
    use_program_(program);

    static const GLfloat quad_pos[8] = {-1, -1, 1, -1, 1, 1, -1, 1};
    static const GLfloat quad_red[16] = {1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1};

    // --- A: user program + independent client arrays + GL_QUADS ----------
    clear_color_(0, 0, 0, 1);
    clear_(GL_COLOR_BUFFER_BIT_);
    enable_client_state_(GL_VERTEX_ARRAY_);
    enable_client_state_(GL_COLOR_ARRAY_);
    vertex_pointer_(2, GL_FLOAT_, 0, quad_pos);
    color_pointer_(4, GL_FLOAT_, 0, quad_red);
    draw_arrays_(GL_QUADS_, 0, 4);
    ASSERT_TRUE(CheckCenter("A: client arrays + QUADS through user shader (red->blue)", 0, 0, 255));

    // --- C: same arrays via glDrawElements(GL_QUADS) ----------------------
    static const GLushort quad_idx[4] = {0, 1, 2, 3};
    clear_(GL_COLOR_BUFFER_BIT_);
    draw_elements_(GL_QUADS_, 4, GL_UNSIGNED_SHORT_, quad_idx);
    ASSERT_TRUE(CheckCenter("C: client-index QUADS through user shader (red->blue)", 0, 0, 255));
    disable_client_state_(GL_VERTEX_ARRAY_);
    disable_client_state_(GL_COLOR_ARRAY_);

    // --- B: user program + immediate mode --------------------------------
    clear_(GL_COLOR_BUFFER_BIT_);
    color4f_(0.0f, 1.0f, 0.0f, 1.0f); // green: swap keeps green (control for
    begin_(GL_QUADS_);                // "did the draw happen at all")
    vertex2f_(-1, -1);
    vertex2f_(1, -1);
    vertex2f_(1, 1);
    vertex2f_(-1, 1);
    end_();
    ASSERT_TRUE(CheckCenter("B1: immediate QUADS through user shader (green)", 0, 255, 0));
    clear_(GL_COLOR_BUFFER_BIT_);
    color4f_(1.0f, 0.0f, 0.0f, 1.0f); // red -> must come out BLUE
    begin_(GL_QUADS_);
    vertex2f_(-1, -1);
    vertex2f_(1, -1);
    vertex2f_(1, 1);
    vertex2f_(-1, 1);
    end_();
    ASSERT_TRUE(CheckCenter("B2: immediate QUADS through user shader (red->blue)", 0, 0, 255));

    // --- D: glBindAttribLocation pinning must survive translation --------
    static const char* k_vs_attr =
        "#version 120\n"
        "attribute vec4 mc_Entity;\n"
        "varying vec4 col;\n"
        "void main() { gl_Position = gl_Vertex; col = mc_Entity; }\n";
    static const char* k_fs_attr = "#version 120\n"
                                   "varying vec4 col;\n"
                                   "void main() { gl_FragColor = col; }\n";
    const GLuint vs2 = CompileStage(GL_VERTEX_SHADER_, k_vs_attr, "vs-attr");
    const GLuint fs2 = CompileStage(GL_FRAGMENT_SHADER_, k_fs_attr, "fs-attr");
    ASSERT_NE(vs2, 0u);
    ASSERT_NE(fs2, 0u);
    const GLuint program2 = create_program_();
    attach_shader_(program2, vs2);
    attach_shader_(program2, fs2);
    bind_attrib_location_(program2, 11, "mc_Entity"); // the OptiFine pattern
    link_program_(program2);
    get_programiv_(program2, GL_LINK_STATUS_, &linked);
    ASSERT_NE(linked, 0) << "attr-pin program link";
    use_program_(program2);
    vertex_attrib4f_(11, 0.0f, 1.0f, 1.0f, 1.0f); // cyan via the PINNED slot
    clear_(GL_COLOR_BUFFER_BIT_);
    enable_client_state_(GL_VERTEX_ARRAY_);
    vertex_pointer_(2, GL_FLOAT_, 0, quad_pos);
    draw_arrays_(GL_QUADS_, 0, 4);
    ASSERT_TRUE(CheckCenter("D: glBindAttribLocation(11, mc_Entity) honored (cyan)", 0, 255, 255));

    // --- E: alpha test applies to USER programs (cutout foliage) ---------
    static const char* k_fs_alpha =
        "#version 120\n"
        "varying vec4 col;\n"
        "void main() { gl_FragColor = vec4(1.0, 0.0, 0.0, col.a); }\n";
    const GLuint fs3 = CompileStage(GL_FRAGMENT_SHADER_, k_fs_alpha, "fs-alpha");
    ASSERT_NE(fs3, 0u);
    const GLuint program3 = create_program_();
    attach_shader_(program3, vs);
    attach_shader_(program3, fs3);
    link_program_(program3);
    get_programiv_(program3, GL_LINK_STATUS_, &linked);
    ASSERT_NE(linked, 0) << "alpha-test program link";
    use_program_(program3);
    enable_client_state_(GL_VERTEX_ARRAY_);
    enable_client_state_(GL_COLOR_ARRAY_);
    vertex_pointer_(2, GL_FLOAT_, 0, quad_pos);
    // alpha 0.25 vs ref 0.5 with GL_GREATER: every fragment must be culled,
    // leaving the green clear color untouched.
    static const GLfloat quad_a25[16] = {1, 1, 1, 0.25f, 1, 1, 1, 0.25f,
                                         1, 1, 1, 0.25f, 1, 1, 1, 0.25f};
    static const GLfloat quad_a75[16] = {1, 1, 1, 0.75f, 1, 1, 1, 0.75f,
                                         1, 1, 1, 0.75f, 1, 1, 1, 0.75f};
    color_pointer_(4, GL_FLOAT_, 0, quad_a25);
    enable_(GL_ALPHA_TEST_);
    alpha_func_(GL_GREATER_, 0.5f);
    clear_color_(0.0f, 1.0f, 0.0f, 1.0f);
    clear_(GL_COLOR_BUFFER_BIT_);
    draw_arrays_(GL_QUADS_, 0, 4);
    ASSERT_TRUE(CheckCenter("E1: alpha 0.25 GREATER 0.5 -> discarded (green survives)", 0, 255, 0));
    // Same state, alpha 0.75: the fragment passes and paints red.
    color_pointer_(4, GL_FLOAT_, 0, quad_a75);
    clear_(GL_COLOR_BUFFER_BIT_);
    draw_arrays_(GL_QUADS_, 0, 4);
    ASSERT_TRUE(CheckCenter("E2: alpha 0.75 GREATER 0.5 -> kept (red)", 255, 0, 0));
    // Disabling the test must let the low-alpha fragment through again.
    disable_(GL_ALPHA_TEST_);
    color_pointer_(4, GL_FLOAT_, 0, quad_a25);
    clear_(GL_COLOR_BUFFER_BIT_);
    draw_arrays_(GL_QUADS_, 0, 4);
    ASSERT_TRUE(CheckCenter("E3: alpha test disabled -> kept (red)", 255, 0, 0));
    // GL_LESS is the mirror image: 0.25 passes, 0.75 does not.
    enable_(GL_ALPHA_TEST_);
    alpha_func_(GL_LESS_, 0.5f);
    clear_(GL_COLOR_BUFFER_BIT_);
    draw_arrays_(GL_QUADS_, 0, 4);
    ASSERT_TRUE(CheckCenter("E4: alpha 0.25 LESS 0.5 -> kept (red)", 255, 0, 0));
    color_pointer_(4, GL_FLOAT_, 0, quad_a75);
    clear_(GL_COLOR_BUFFER_BIT_);
    draw_arrays_(GL_QUADS_, 0, 4);
    ASSERT_TRUE(CheckCenter("E5: alpha 0.75 LESS 0.5 -> discarded (green survives)", 0, 255, 0));
    disable_(GL_ALPHA_TEST_);

    // --- F: glDrawRangeElements / glMultiDraw* honor the same plumbing ---
    use_program_(program); // back to the R<->B swapping program
    color_pointer_(4, GL_FLOAT_, 0, quad_red);
    clear_(GL_COLOR_BUFFER_BIT_);
    draw_range_elements_(GL_QUADS_, 0, 3, 4, GL_UNSIGNED_SHORT_, quad_idx);
    ASSERT_TRUE(CheckCenter("F1: glDrawRangeElements(QUADS) through user shader (red->blue)", 0, 0,
                            255));
    static const GLint md_first[1] = {0};
    static const GLsizei md_count[1] = {4};
    clear_(GL_COLOR_BUFFER_BIT_);
    multi_draw_arrays_(GL_QUADS_, md_first, md_count, 1);
    ASSERT_TRUE(CheckCenter("F2: glMultiDrawArrays(QUADS) through user shader (red->blue)", 0, 0,
                            255));
    const void* md_indices[1] = {quad_idx};
    clear_(GL_COLOR_BUFFER_BIT_);
    multi_draw_elements_(GL_QUADS_, md_count, GL_UNSIGNED_SHORT_, md_indices, 1);
    ASSERT_TRUE(CheckCenter("F3: glMultiDrawElements(QUADS) through user shader (red->blue)", 0, 0,
                            255));
    // The emulated alpha test must reach these paths too (cutout foliage
    // drawn via glDrawRangeElements is exactly the OptiFine terrain shape).
    use_program_(program3);
    color_pointer_(4, GL_FLOAT_, 0, quad_a25);
    enable_(GL_ALPHA_TEST_);
    alpha_func_(GL_GREATER_, 0.5f);
    clear_(GL_COLOR_BUFFER_BIT_);
    draw_range_elements_(GL_QUADS_, 0, 3, 4, GL_UNSIGNED_SHORT_, quad_idx);
    ASSERT_TRUE(CheckCenter("F4: alpha test applies to glDrawRangeElements (green survives)", 0,
                            255, 0));
    disable_(GL_ALPHA_TEST_);
    disable_client_state_(GL_VERTEX_ARRAY_);
    disable_client_state_(GL_COLOR_ARRAY_);
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

} // namespace
