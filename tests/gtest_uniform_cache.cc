// SimpleFPEWrapper - tests/gtest_uniform_cache.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// sfpewFeedUserProgramUniforms() re-sent every fixed-function uniform a user
// program declares on every single draw. Legacy FFP state barely moves between
// draws, so nearly all of it was redundant: 1.16-Sodium/1-frame5312.rdc shows
// the same alpha-test pair re-sent on 602 of 607 draws, 27.6% of the frame's
// GL calls. Each scalar/vec4 uniform now caches its last sent value per
// program.
//
// The failure mode a send-on-change cache introduces is a stale uniform: FFP
// state changes but the shader keeps the old value, so this drives real state
// changes through a declared uniform and reads the result back as color.
//
// fpe_Fog.color is used as the probe because it is a plain vec4 the fragment
// shader can output directly. The sequence matters more than any single draw:
//
//   draw 1  fog color green    -> green    (first send, cache empty)
//   draw 2  unchanged           -> green    (this is the send that gets skipped)
//   draw 3  fog color magenta  -> magenta  (cache must notice the change)
//   draw 4  back to green       -> green    (and notice it changing back)

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
using sfpew_test::PixelProbe;

constexpr GLbitfield GL_COLOR_BUFFER_BIT_ = 0x00004000;
constexpr GLenum GL_TRIANGLES_ = 0x0004;
constexpr GLenum GL_FLOAT_ = 0x1406;
constexpr GLenum GL_VERTEX_SHADER_ = 0x8B31;
constexpr GLenum GL_FRAGMENT_SHADER_ = 0x8B30;
constexpr GLenum GL_COMPILE_STATUS_ = 0x8B81;
constexpr GLenum GL_LINK_STATUS_ = 0x8B82;
constexpr GLenum GL_VERTEX_ARRAY_ = 0x8074;
constexpr GLenum GL_FOG_COLOR_ = 0x0B66;
constexpr GLenum GL_NO_ERROR_ = 0;

using UniformCacheTest = ContextTest;

TEST_F(UniformCacheTest, PerProgramCacheTracksFixedFunctionStateChanges) {
    auto clear_color = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glClearColor");
    auto clear = Get<void (*)(GLbitfield)>("glClear");
    auto draw_arrays = Get<void (*)(GLenum, GLint, GLsizei)>("glDrawArrays");
    auto get_error = Get<GLenum (*)()>("glGetError");
    auto finish = Get<void (*)()>("glFinish");
    auto use_program = Get<void (*)(GLuint)>("glUseProgram");
    auto create_shader = Get<GLuint (*)(GLenum)>("glCreateShader");
    auto shader_source = Get<void (*)(GLuint, GLsizei, const GLchar* const*, const GLint*)>(
        "glShaderSource");
    auto compile_shader = Get<void (*)(GLuint)>("glCompileShader");
    auto get_shaderiv = Get<void (*)(GLuint, GLenum, GLint*)>("glGetShaderiv");
    auto create_program = Get<GLuint (*)()>("glCreateProgram");
    auto attach_shader = Get<void (*)(GLuint, GLuint)>("glAttachShader");
    auto link_program = Get<void (*)(GLuint)>("glLinkProgram");
    auto get_programiv = Get<void (*)(GLuint, GLenum, GLint*)>("glGetProgramiv");
    auto get_program_info_log =
        Get<void (*)(GLuint, GLsizei, GLsizei*, GLchar*)>("glGetProgramInfoLog");
    auto enable_client_state = Get<void (*)(GLenum)>("glEnableClientState");
    auto disable_client_state = Get<void (*)(GLenum)>("glDisableClientState");
    auto vertex_pointer = Get<void (*)(GLint, GLenum, GLsizei, const void*)>("glVertexPointer");
    auto fogfv = Get<void (*)(GLenum, const GLfloat*)>("glFogfv");
    auto read_pixels =
        Get<void (*)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*)>("glReadPixels");
    ASSERT_NE(read_pixels, nullptr);
    PixelProbe probe(read_pixels);

    // Declares the fixed-function fog struct the wrapper feeds, and outputs
    // its color, so the pixel IS the uniform's value.
    static const char* vs_src =
        "#version 300 es\n"
        "in vec4 fpe_Vertex;\n"
        "void main() { gl_Position = fpe_Vertex; }\n";
    static const char* fs_src =
        "#version 300 es\n"
        "precision mediump float;\n"
        "struct FpeFog { vec4 color; float density; };\n"
        "uniform FpeFog fpe_Fog;\n"
        "out vec4 o;\n"
        "void main() { o = vec4(fpe_Fog.color.rgb, 1.0); }\n";
    const GLuint vs = create_shader(GL_VERTEX_SHADER_);
    const GLuint fs = create_shader(GL_FRAGMENT_SHADER_);
    shader_source(vs, 1, &vs_src, nullptr);
    shader_source(fs, 1, &fs_src, nullptr);
    compile_shader(vs);
    compile_shader(fs);
    GLint ok = 0;
    get_shaderiv(vs, GL_COMPILE_STATUS_, &ok);
    ASSERT_NE(ok, 0) << "VS compile";
    get_shaderiv(fs, GL_COMPILE_STATUS_, &ok);
    ASSERT_NE(ok, 0) << "FS compile";
    const GLuint program = create_program();
    attach_shader(program, vs);
    attach_shader(program, fs);
    link_program(program);
    get_programiv(program, GL_LINK_STATUS_, &ok);
    ASSERT_NE(ok, 0) << "program link";

    static const GLfloat pos[] = {-1, -1, 1, -1, 1, 1, -1, -1, 1, 1, -1, 1};
    vertex_pointer(2, GL_FLOAT_, 0, pos);
    enable_client_state(GL_VERTEX_ARRAY_);
    use_program(program);
    clear_color(0.0f, 0.0f, 0.0f, 1.0f);

    // Both probe colors keep R == B: Mesa's llvmpipe swaps R/B in fragment
    // output on surfaceless BGRA pbuffer configs (reproduced with a plain
    // GLES3 client, no wrapper involved), and symmetric colors make the
    // uniform-cache check immune to that driver bug without weakening it.
    static const GLfloat green[] = {0.0f, 1.0f, 0.0f, 1.0f};
    static const GLfloat magenta[] = {1.0f, 0.0f, 1.0f, 1.0f};

    const auto draw_and_expect = [&](int r, int g, int b, const char* what) {
        clear(GL_COLOR_BUFFER_BIT_);
        draw_arrays(GL_TRIANGLES_, 0, 6);
        finish();
        const PixelProbe::Rgba p = probe.At(32, 32);
        EXPECT_TRUE((p.r > 200) == (r > 0) && (p.g > 200) == (g > 0) && (p.b > 200) == (b > 0))
            << what << ": pixel = (" << (int)p.r << ',' << (int)p.g << ',' << (int)p.b
            << "), expected (" << r << ',' << g << ',' << b << ')';
    };

    fogfv(GL_FOG_COLOR_, green);
    draw_and_expect(0, 1, 0, "draw 1: fog color reaches the shader");
    // Unchanged: this is the send the cache elides.
    draw_and_expect(0, 1, 0, "draw 2: unchanged uniform still correct (send skipped)");
    // Changed: a stale cache would keep drawing green here.
    fogfv(GL_FOG_COLOR_, magenta);
    draw_and_expect(1, 0, 1, "draw 3: changed uniform is re-sent");
    // Changed back, to catch a cache that only ever updates once.
    fogfv(GL_FOG_COLOR_, green);
    draw_and_expect(0, 1, 0, "draw 4: uniform changed back is re-sent");

    disable_client_state(GL_VERTEX_ARRAY_);
    use_program(0);
    EXPECT_EQ(get_error(), GL_NO_ERROR_);
}

} // namespace
