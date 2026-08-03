// SimpleFPEWrapper - tests/gtest_multidraw_native.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// glMultiDraw* forwards to the backend's native multi-draw when no sub-draw
// needs individual work, and otherwise loops over the single-draw path. Both
// halves have to stay correct, so both are exercised here:
//
//   A. native path - user program, own VAO, core mode. Each sub-draw range must
//      land in its own place, which is what catches a forward that drops or
//      mixes up first[]/count[].
//   B. loop path via mode - GL_QUADS cannot forward (it becomes an indexed draw
//      whose index count differs per sub-draw), so it must still render.
//   C. loop path via state - fixed-function arrays with a core mode must still
//      go per-draw, because commit_fpe_state_on_draw uploads only the range
//      each sub-draw covers.
//   D. indexed forwarding - glMultiDrawElements over the same shapes.

#include "sfpew_gtest.h"

#include <optional>

namespace {

using sfpew_test::ContextTest;
using sfpew_test::GLbitfield;
using sfpew_test::GLboolean;
using sfpew_test::GLchar;
using sfpew_test::GLenum;
using sfpew_test::GLfloat;
using sfpew_test::GLint;
using sfpew_test::GLsizei;
using sfpew_test::GLsizeiptr;
using sfpew_test::GLubyte;
using sfpew_test::GLuint;
using sfpew_test::PixelProbe;

constexpr GLbitfield GL_COLOR_BUFFER_BIT_ = 0x00004000;
constexpr GLenum GL_TRIANGLES_ = 0x0004;
constexpr GLenum GL_QUADS_ = 0x0007;
constexpr GLenum GL_FLOAT_ = 0x1406;
constexpr GLenum GL_UNSIGNED_BYTE_ = 0x1401;
constexpr GLenum GL_ARRAY_BUFFER_ = 0x8892;
constexpr GLenum GL_STATIC_DRAW_ = 0x88E4;
constexpr GLenum GL_VERTEX_SHADER_ = 0x8B31;
constexpr GLenum GL_FRAGMENT_SHADER_ = 0x8B30;
constexpr GLenum GL_COMPILE_STATUS_ = 0x8B81;
constexpr GLenum GL_LINK_STATUS_ = 0x8B82;
constexpr GLenum GL_VERTEX_ARRAY_ = 0x8074;
constexpr GLenum GL_NO_ERROR_ = 0;

class MultiDrawNativeTest : public ContextTest {
protected:
    void SetUp() override {
        ContextTest::SetUp();
        if (::testing::Test::IsSkipped()) return;

        clear_color_ = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glClearColor");
        clear_ = Get<void (*)(GLbitfield)>("glClear");
        get_error_ = Get<GLenum (*)()>("glGetError");
        finish_ = Get<void (*)()>("glFinish");
        use_program_ = Get<void (*)(GLuint)>("glUseProgram");
        create_shader_ = Get<GLuint (*)(GLenum)>("glCreateShader");
        shader_source_ = Get<void (*)(GLuint, GLsizei, const GLchar* const*, const GLint*)>(
            "glShaderSource");
        compile_shader_ = Get<void (*)(GLuint)>("glCompileShader");
        get_shaderiv_ = Get<void (*)(GLuint, GLenum, GLint*)>("glGetShaderiv");
        create_program_ = Get<GLuint (*)()>("glCreateProgram");
        attach_shader_ = Get<void (*)(GLuint, GLuint)>("glAttachShader");
        link_program_ = Get<void (*)(GLuint)>("glLinkProgram");
        get_programiv_ = Get<void (*)(GLuint, GLenum, GLint*)>("glGetProgramiv");
        get_program_info_log_ =
            Get<void (*)(GLuint, GLsizei, GLsizei*, GLchar*)>("glGetProgramInfoLog");
        gen_buffers_ = Get<void (*)(GLsizei, GLuint*)>("glGenBuffers");
        bind_buffer_ = Get<void (*)(GLenum, GLuint)>("glBindBuffer");
        buffer_data_ = Get<void (*)(GLenum, GLsizeiptr, const void*, GLenum)>("glBufferData");
        gen_vertex_arrays_ = Get<void (*)(GLsizei, GLuint*)>("glGenVertexArrays");
        bind_vertex_array_ = Get<void (*)(GLuint)>("glBindVertexArray");
        vertex_attrib_pointer_ =
            Get<void (*)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*)>(
                "glVertexAttribPointer");
        enable_vertex_attrib_array_ = Get<void (*)(GLuint)>("glEnableVertexAttribArray");
        get_attrib_location_ = Get<GLint (*)(GLuint, const GLchar*)>("glGetAttribLocation");
        multi_draw_arrays_ = Get<void (*)(GLenum, const GLint*, const GLsizei*, GLsizei)>(
            "glMultiDrawArrays");
        multi_draw_elements_ = Get<void (*)(GLenum, const GLsizei*, GLenum, const void* const*,
                                            GLsizei)>("glMultiDrawElements");
        enable_client_state_ = Get<void (*)(GLenum)>("glEnableClientState");
        disable_client_state_ = Get<void (*)(GLenum)>("glDisableClientState");
        vertex_pointer_ = Get<void (*)(GLint, GLenum, GLsizei, const void*)>("glVertexPointer");
        color4f_ = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glColor4f");
        auto read_pixels =
            Get<void (*)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*)>("glReadPixels");
        ASSERT_NE(read_pixels, nullptr);
        probe_.emplace(read_pixels);

        clear_color_(0.0f, 0.0f, 0.0f, 1.0f);
        get_error_();
    }

    void CheckPixel(int x, int y, int r, int g, int b, const char* what) {
        const PixelProbe::Rgba p = probe_->At(x, y);
        EXPECT_TRUE((p.r > 200) == (r > 0) && (p.g > 200) == (g > 0) && (p.b > 200) == (b > 0))
            << what << ": pixel(" << x << ',' << y << ") = (" << (int)p.r << ',' << (int)p.g
            << ',' << (int)p.b << "), expected (" << r << ',' << g << ',' << b << ')';
    }

    void (*clear_color_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*clear_)(GLbitfield) = nullptr;
    GLenum (*get_error_)() = nullptr;
    void (*finish_)() = nullptr;
    void (*use_program_)(GLuint) = nullptr;
    GLuint (*create_shader_)(GLenum) = nullptr;
    void (*shader_source_)(GLuint, GLsizei, const GLchar* const*, const GLint*) = nullptr;
    void (*compile_shader_)(GLuint) = nullptr;
    void (*get_shaderiv_)(GLuint, GLenum, GLint*) = nullptr;
    GLuint (*create_program_)() = nullptr;
    void (*attach_shader_)(GLuint, GLuint) = nullptr;
    void (*link_program_)(GLuint) = nullptr;
    void (*get_programiv_)(GLuint, GLenum, GLint*) = nullptr;
    void (*get_program_info_log_)(GLuint, GLsizei, GLsizei*, GLchar*) = nullptr;
    void (*gen_buffers_)(GLsizei, GLuint*) = nullptr;
    void (*bind_buffer_)(GLenum, GLuint) = nullptr;
    void (*buffer_data_)(GLenum, GLsizeiptr, const void*, GLenum) = nullptr;
    void (*gen_vertex_arrays_)(GLsizei, GLuint*) = nullptr;
    void (*bind_vertex_array_)(GLuint) = nullptr;
    void (*vertex_attrib_pointer_)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*) =
        nullptr;
    void (*enable_vertex_attrib_array_)(GLuint) = nullptr;
    GLint (*get_attrib_location_)(GLuint, const GLchar*) = nullptr;
    void (*multi_draw_arrays_)(GLenum, const GLint*, const GLsizei*, GLsizei) = nullptr;
    void (*multi_draw_elements_)(GLenum, const GLsizei*, GLenum, const void* const*, GLsizei) =
        nullptr;
    void (*enable_client_state_)(GLenum) = nullptr;
    void (*disable_client_state_)(GLenum) = nullptr;
    void (*vertex_pointer_)(GLint, GLenum, GLsizei, const void*) = nullptr;
    void (*color4f_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    std::optional<PixelProbe> probe_;
};

TEST_F(MultiDrawNativeTest, ForwardsNativelyWhereValidAndLoopsWhereNot) {
    // Three vertical bands as TRIANGLES, 6 verts each: red at x in
    // [-1,-1/3), green at [-1/3,1/3), blue at [1/3,1]. Sub-draw i is verts
    // 6i..6i+5, so a forward that ignores first[]/count[] cannot produce the
    // right picture.
    GLfloat verts[3 * 6 * 5];
    {
        const GLfloat xs[3][2] = {{-1.0f, -1.0f / 3.0f}, {-1.0f / 3.0f, 1.0f / 3.0f},
                                  {1.0f / 3.0f, 1.0f}};
        const GLfloat cols[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
        int w = 0;
        for (int band = 0; band < 3; ++band) {
            const GLfloat x0 = xs[band][0], x1 = xs[band][1];
            const GLfloat quad[6][2] = {{x0, -1}, {x1, -1}, {x1, 1}, {x0, -1}, {x1, 1}, {x0, 1}};
            for (int v = 0; v < 6; ++v) {
                verts[w++] = quad[v][0];
                verts[w++] = quad[v][1];
                verts[w++] = cols[band][0];
                verts[w++] = cols[band][1];
                verts[w++] = cols[band][2];
            }
        }
    }
    static const char* vs_src =
        "#version 300 es\n"
        "in vec2 aPos;\n"
        "in vec3 aCol;\n"
        "out vec3 vCol;\n"
        "void main() { vCol = aCol; gl_Position = vec4(aPos, 0.0, 1.0); }\n";
    static const char* fs_src =
        "#version 300 es\n"
        "precision mediump float;\n"
        "in vec3 vCol;\n"
        "out vec4 o;\n"
        "void main() { o = vec4(vCol, 1.0); }\n";
    const GLuint vs = create_shader_(GL_VERTEX_SHADER_);
    const GLuint fs = create_shader_(GL_FRAGMENT_SHADER_);
    shader_source_(vs, 1, &vs_src, nullptr);
    shader_source_(fs, 1, &fs_src, nullptr);
    compile_shader_(vs);
    compile_shader_(fs);
    GLint ok = 0;
    get_shaderiv_(vs, GL_COMPILE_STATUS_, &ok);
    ASSERT_NE(ok, 0) << "VS compile";
    get_shaderiv_(fs, GL_COMPILE_STATUS_, &ok);
    ASSERT_NE(ok, 0) << "FS compile";
    const GLuint program = create_program_();
    attach_shader_(program, vs);
    attach_shader_(program, fs);
    link_program_(program);
    get_programiv_(program, GL_LINK_STATUS_, &ok);
    ASSERT_NE(ok, 0) << "program link";

    GLuint vao = 0, vbo = 0;
    gen_vertex_arrays_(1, &vao);
    bind_vertex_array_(vao);
    gen_buffers_(1, &vbo);
    bind_buffer_(GL_ARRAY_BUFFER_, vbo);
    buffer_data_(GL_ARRAY_BUFFER_, static_cast<GLsizeiptr>(sizeof verts), verts, GL_STATIC_DRAW_);
    const GLint loc_pos = get_attrib_location_(program, "aPos");
    const GLint loc_col = get_attrib_location_(program, "aCol");
    ASSERT_GE(loc_pos, 0) << "aPos";
    ASSERT_GE(loc_col, 0) << "aCol";
    vertex_attrib_pointer_(static_cast<GLuint>(loc_pos), 2, GL_FLOAT_, sfpew_test::GL_FALSE_,
                           5 * static_cast<GLsizei>(sizeof(GLfloat)), nullptr);
    vertex_attrib_pointer_(static_cast<GLuint>(loc_col), 3, GL_FLOAT_, sfpew_test::GL_FALSE_,
                           5 * static_cast<GLsizei>(sizeof(GLfloat)),
                           reinterpret_cast<const void*>(2 * sizeof(GLfloat)));
    enable_vertex_attrib_array_(static_cast<GLuint>(loc_pos));
    enable_vertex_attrib_array_(static_cast<GLuint>(loc_col));
    use_program_(program);

    // A: all three bands, one call. This is the native forward.
    static const GLint firsts_all[3] = {0, 6, 12};
    static const GLsizei counts_all[3] = {6, 6, 6};
    clear_(GL_COLOR_BUFFER_BIT_);
    multi_draw_arrays_(GL_TRIANGLES_, firsts_all, counts_all, 3);
    finish_();
    CheckPixel(10, 32, 1, 0, 0, "A: native multidraw, band 0 red");
    CheckPixel(32, 32, 0, 1, 0, "A: native multidraw, band 1 green");
    CheckPixel(54, 32, 0, 0, 1, "A: native multidraw, band 2 blue");

    // A2: a subset, to pin that first[]/count[] are honored rather than the
    // whole buffer being drawn. Only band 2 is named; bands 0 and 1 stay clear.
    static const GLint firsts_one[1] = {12};
    static const GLsizei counts_one[1] = {6};
    clear_(GL_COLOR_BUFFER_BIT_);
    multi_draw_arrays_(GL_TRIANGLES_, firsts_one, counts_one, 1);
    finish_();
    CheckPixel(54, 32, 0, 0, 1, "A2: subset draws the named range");
    CheckPixel(10, 32, 0, 0, 0, "A2: subset leaves band 0 clear");
    CheckPixel(32, 32, 0, 0, 0, "A2: subset leaves band 1 clear");

    // B: GL_QUADS cannot forward. Same geometry re-expressed as 4-vertex
    // quads so the loop path is the only way it can render.
    GLfloat quad_verts[3 * 4 * 5];
    {
        const GLfloat xs[3][2] = {{-1.0f, -1.0f / 3.0f}, {-1.0f / 3.0f, 1.0f / 3.0f},
                                  {1.0f / 3.0f, 1.0f}};
        const GLfloat cols[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
        int w = 0;
        for (int band = 0; band < 3; ++band) {
            const GLfloat x0 = xs[band][0], x1 = xs[band][1];
            const GLfloat quad[4][2] = {{x0, -1}, {x1, -1}, {x1, 1}, {x0, 1}};
            for (int v = 0; v < 4; ++v) {
                quad_verts[w++] = quad[v][0];
                quad_verts[w++] = quad[v][1];
                quad_verts[w++] = cols[band][0];
                quad_verts[w++] = cols[band][1];
                quad_verts[w++] = cols[band][2];
            }
        }
    }
    buffer_data_(GL_ARRAY_BUFFER_, static_cast<GLsizeiptr>(sizeof quad_verts), quad_verts,
                GL_STATIC_DRAW_);
    static const GLint quad_firsts[3] = {0, 4, 8};
    static const GLsizei quad_counts[3] = {4, 4, 4};
    clear_(GL_COLOR_BUFFER_BIT_);
    multi_draw_arrays_(GL_QUADS_, quad_firsts, quad_counts, 3);
    finish_();
    CheckPixel(10, 32, 1, 0, 0, "B: GL_QUADS multidraw falls back to the loop, band 0");
    CheckPixel(32, 32, 0, 1, 0, "B: GL_QUADS multidraw falls back to the loop, band 1");
    CheckPixel(54, 32, 0, 0, 1, "B: GL_QUADS multidraw falls back to the loop, band 2");

    // D: indexed forwarding over the TRIANGLES layout.
    buffer_data_(GL_ARRAY_BUFFER_, static_cast<GLsizeiptr>(sizeof verts), verts, GL_STATIC_DRAW_);
    static const GLubyte idx0[6] = {0, 1, 2, 3, 4, 5};
    static const GLubyte idx2[6] = {12, 13, 14, 15, 16, 17};
    const void* idx_ptrs[2] = {idx0, idx2};
    static const GLsizei idx_counts[2] = {6, 6};
    clear_(GL_COLOR_BUFFER_BIT_);
    multi_draw_elements_(GL_TRIANGLES_, idx_counts, GL_UNSIGNED_BYTE_, idx_ptrs, 2);
    finish_();
    CheckPixel(10, 32, 1, 0, 0, "D: indexed multidraw, band 0 red");
    CheckPixel(54, 32, 0, 0, 1, "D: indexed multidraw, band 2 blue");
    CheckPixel(32, 32, 0, 0, 0, "D: indexed multidraw skips the unreferenced band");

    use_program_(0);
    bind_vertex_array_(0);

    // C: fixed-function arrays with a core mode must NOT forward - each
    // sub-draw needs its own state commit and upload. Client memory, no user
    // program.
    static const GLfloat ff_verts[] = {
        -1, -1, -1.0f / 3.0f, -1, -1.0f / 3.0f, 1,
        -1, -1, -1.0f / 3.0f, 1,  -1,           1,
        1.0f / 3.0f, -1, 1, -1, 1, 1,
        1.0f / 3.0f, -1, 1,  1,  1.0f / 3.0f, 1,
    };
    static const GLint ff_firsts[2] = {0, 6};
    static const GLsizei ff_counts[2] = {6, 6};
    clear_color_(0.0f, 0.0f, 1.0f, 1.0f);
    clear_(GL_COLOR_BUFFER_BIT_);
    color4f_(0.0f, 1.0f, 0.0f, 1.0f);
    enable_client_state_(GL_VERTEX_ARRAY_);
    vertex_pointer_(2, GL_FLOAT_, 0, ff_verts);
    multi_draw_arrays_(GL_TRIANGLES_, ff_firsts, ff_counts, 2);
    finish_();
    CheckPixel(10, 32, 0, 1, 0, "C: fixed-function multidraw, sub-draw 0 green");
    CheckPixel(54, 32, 0, 1, 0, "C: fixed-function multidraw, sub-draw 1 green");
    CheckPixel(32, 32, 0, 0, 1, "C: fixed-function multidraw leaves the gap clear");
    disable_client_state_(GL_VERTEX_ARRAY_);
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

} // namespace
