// SimpleFPEWrapper - tests/gtest_mixed_translation_link.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// A shader whose translation fails is passed through with the app's own
// #version, while translated shaders carry the backend's target version -
// and GLSL ES refuses to link a program mixing the two (Mesa enforces it;
// NVIDIA happens to be lenient, which hid this for a long time). The
// wrapper therefore falls back to the ORIGINAL source for every shader of
// a mixed program at link, and restores the translated form when the same
// shader next links into a fully-translated program.
//
// The pass-through member here is a fragment shader that redefines the
// prelude's `fpe_FogParameters` STRUCT: the redefinition retry only yields
// single-line uniform/in/out declarations, so a struct-type collision
// still fails translation - deliberately, as the stable way to force a
// pass-through from an otherwise backend-legal source.
//
// The vertex shader is shared by both programs, so alternating links must
// re-upload it in whichever form the linking program needs:
//
//   link A (mixed)      draw -> magenta  (VS fell back to original)
//   link B (translated)  draw -> green    (VS restored to translated ESSL)
//   link A again         draw -> magenta  (VS fell back again)
//   link B again         draw -> green
//
// Probe colors keep R == B: llvmpipe swaps R/B in fragment output on
// surfaceless BGRA pbuffer configs (a driver bug, reproduced without the
// wrapper), and symmetric colors are immune without losing discrimination.

#include "sfpew_gtest.h"

#include <cstdio>
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
constexpr GLenum GL_VERTEX_ARRAY_ = 0x8074;
constexpr GLenum GL_FOG_COLOR_ = 0x0B66;
constexpr GLenum GL_VERTEX_SHADER_ = 0x8B31;
constexpr GLenum GL_FRAGMENT_SHADER_ = 0x8B30;
constexpr GLenum GL_COMPILE_STATUS_ = 0x8B81;
constexpr GLenum GL_LINK_STATUS_ = 0x8B82;
constexpr GLenum GL_NO_ERROR_ = 0;

using MixedTranslationLinkTest = ContextTest;

TEST_F(MixedTranslationLinkTest, NativeAndRetranslatedProgramsLinkConsistently) {
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

    const auto compile_sh = [&](GLenum stage, const char* src, const char* what) {
        const GLuint sh = create_shader(stage);
        shader_source(sh, 1, &src, nullptr);
        compile_shader(sh);
        GLint ok = 0;
        get_shaderiv(sh, GL_COMPILE_STATUS_, &ok);
        EXPECT_NE(ok, 0) << what << " compile";
        return sh;
    };
    const auto link_ok = [&](GLuint program, const char* what) {
        link_program(program);
        GLint ok = 0;
        get_programiv(program, GL_LINK_STATUS_, &ok);
        if (ok == 0) {
            char log[512] = {};
            get_program_info_log(program, sizeof log, nullptr, log);
            ADD_FAILURE() << what << " link: " << log;
            return false;
        }
        return true;
    };

    // Native ESSL 300 es (self-declared fpe_Vertex yields to the prelude).
    static const char* vs_native_src =
        "#version 300 es\n"
        "in vec4 fpe_Vertex;\n"
        "void main() { gl_Position = fpe_Vertex; }\n";
    // Redefines the prelude's STRUCT type, so translation fails; the
    // backend-legal original must be passed through instead.
    static const char* fs_pass_src =
        "#version 300 es\n"
        "precision mediump float;\n"
        "struct fpe_FogParameters { vec4 color; };\n"
        "uniform fpe_FogParameters fpe_Fog;\n"
        "out vec4 o;\n"
        "void main() { o = vec4(fpe_Fog.color.rgb, 1.0); }\n";
    // Desktop GLSL that cannot pass through to a GLES backend.
    static const char* vs_desktop_src =
        "#version 120\n"
        "void main() { gl_Position = ftransform(); }\n";
    // Native ESSL 300 es that also translates cleanly.
    static const char* fs_green_src =
        "#version 300 es\n"
        "precision mediump float;\n"
        "out vec4 o;\n"
        "void main() { o = vec4(0.0, 1.0, 0.0, 1.0); }\n";

    const GLuint vs_native = compile_sh(GL_VERTEX_SHADER_, vs_native_src, "VS (native)");
    const GLuint fs_pass = compile_sh(GL_FRAGMENT_SHADER_, fs_pass_src, "FS (pass-through)");
    const GLuint vs_desktop = compile_sh(GL_VERTEX_SHADER_, vs_desktop_src, "VS (desktop 120)");
    const GLuint fs_green = compile_sh(GL_FRAGMENT_SHADER_, fs_green_src, "FS (green)");

    const GLuint prog_native = create_program();
    attach_shader(prog_native, vs_native);
    attach_shader(prog_native, fs_pass);
    const GLuint prog_mixed = create_program();
    attach_shader(prog_mixed, vs_desktop);
    attach_shader(prog_mixed, fs_green);

    static const GLfloat pos[] = {-1, -1, 1, -1, 1, 1, -1, -1, 1, 1, -1, 1};
    vertex_pointer(2, GL_FLOAT_, 0, pos);
    enable_client_state(GL_VERTEX_ARRAY_);
    clear_color(0.0f, 0.0f, 0.0f, 1.0f);
    static const GLfloat magenta[] = {1.0f, 0.0f, 1.0f, 1.0f};
    fogfv(GL_FOG_COLOR_, magenta);

    const auto draw_and_expect = [&](GLuint program, int r, int g, int b, const char* what) {
        use_program(program);
        clear(GL_COLOR_BUFFER_BIT_);
        draw_arrays(GL_TRIANGLES_, 0, 6);
        finish();
        const PixelProbe::Rgba p = probe.At(32, 32);
        EXPECT_TRUE((p.r > 200) == (r > 0) && (p.g > 200) == (g > 0) && (p.b > 200) == (b > 0))
            << what << ": pixel = (" << (int)p.r << ',' << (int)p.g << ',' << (int)p.b
            << "), expected (" << r << ',' << g << ',' << b << ')';
    };

    for (int round = 1; round <= 2; ++round) {
        char what[128];
        std::snprintf(what, sizeof what, "round %d: all-native program links", round);
        if (link_ok(prog_native, what)) {
            std::snprintf(what, sizeof what,
                          "round %d: all-native program draws the fed fog color", round);
            draw_and_expect(prog_native, 1, 0, 1, what);
        }
        std::snprintf(what, sizeof what, "round %d: mixed program links after re-translating all",
                      round);
        if (link_ok(prog_mixed, what)) {
            std::snprintf(what, sizeof what, "round %d: mixed program draws green", round);
            draw_and_expect(prog_mixed, 0, 1, 0, what);
        }
    }

    disable_client_state(GL_VERTEX_ARRAY_);
    use_program(0);
    EXPECT_EQ(get_error(), GL_NO_ERROR_);
}

} // namespace
