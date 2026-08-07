// SimpleFPEWrapper - tests/gtest_vertexattrib_ordering.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// plans/16 M6: glVertexAttrib* writes a CURRENT value, so it has to be
// ordered against geometry the wrapper is still holding in its immediate-mode
// merge batch (the contract in fpe/drawing1x.h). Two ways it was not:
//   A. the wrapped spellings (4Nub, 1s, 4dv, ...) forwarded to the backend
//      without draining the batch, and
//   B. the float family (1f/2f/3f/4f and their v forms) had no wrapper entry
//      point at all - eglGetProcAddress handed out the backend's own pointer,
//      so the call never even reached the wrapper.
// Either way a constant written between two small glBegin/glEnd runs reached
// the driver first and the earlier run drew with the LATER value.
//
// Judgement colors are green and magenta, both R==B: llvmpipe's BGRA R/B
// swizzle quirk cannot turn one into the other.

#include "sfpew_gtest.h"

#include <string>

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

using GLshort = short;

constexpr int kWindow = 128;
constexpr GLenum GL_QUADS_ = 0x0007;
constexpr GLbitfield GL_COLOR_BUFFER_BIT_ = 0x00004000;
constexpr GLenum GL_VERTEX_SHADER_ = 0x8B31;
constexpr GLenum GL_FRAGMENT_SHADER_ = 0x8B30;
constexpr GLenum GL_COMPILE_STATUS_ = 0x8B81;
constexpr GLenum GL_LINK_STATUS_ = 0x8B82;
constexpr GLenum GL_NO_ERROR_ = 0;

// The slot the constant lives in. 11 is the OptiFine mc_Entity pin and is
// clear of everything the wrapper wires for fixed-function channels.
constexpr GLuint kSlot = 11;

class VertexAttribOrderingTest : public ContextTest {
protected:
    VertexAttribOrderingTest() : ContextTest(sfpew_test::Backend::GLES3, kWindow) {}

    void SetUp() override {
        ContextTest::SetUp();
        if (::testing::Test::IsSkipped() || ::testing::Test::HasFatalFailure()) return;

        begin_ = Get<void (*)(GLenum)>("glBegin");
        end_ = Get<void (*)()>("glEnd");
        vertex2f_ = Get<void (*)(GLfloat, GLfloat)>("glVertex2f");
        color4f_ = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glColor4f");
        clear_color_ = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glClearColor");
        clear_ = Get<void (*)(GLbitfield)>("glClear");
        get_error_ = Get<GLenum (*)()>("glGetError");
        get_error_();
    }

    void Quad(GLfloat left, GLfloat right) {
        begin_(GL_QUADS_);
        vertex2f_(left, -0.9f);
        vertex2f_(right, -0.9f);
        vertex2f_(right, 0.9f);
        vertex2f_(left, 0.9f);
        end_();
    }

    void (*begin_)(GLenum) = nullptr;
    void (*end_)() = nullptr;
    void (*vertex2f_)(GLfloat, GLfloat) = nullptr;
    void (*color4f_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*clear_color_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*clear_)(GLbitfield) = nullptr;
    GLenum (*get_error_)() = nullptr;
};

// Direct view of the ordering contract: after a small run is queued, ANY
// glVertexAttrib spelling must have drained it before it changes the value
// that run would be redrawn with.
TEST_F(VertexAttribOrderingTest, EveryConstantSpellingDrainsThePendingBatch) {
    auto pending = Dlsym<int (*)()>("sfpewImmediateBatchPendingForTest");
    ASSERT_NE(pending, nullptr) << "sfpewImmediateBatchPendingForTest not exported";

    clear_color_(0.0f, 0.0f, 0.0f, 1.0f);
    clear_(GL_COLOR_BUFFER_BIT_);
    color4f_(0.0f, 1.0f, 0.0f, 1.0f);

    const auto queue_run = [&](const char* spelling) {
        Quad(-0.5f, 0.5f);
        if (!pending()) {
            ADD_FAILURE() << spelling
                          << ": nothing was buffered, so the case cannot observe the drain";
            return false;
        }
        return true;
    };

    // One representative per family: the float forms (no wrapper entry point
    // at all before the fix), a v form, a scalar short, a double and a
    // normalized byte vector.
    if (queue_run("glVertexAttrib4f")) {
        Get<void (*)(GLuint, GLfloat, GLfloat, GLfloat, GLfloat)>("glVertexAttrib4f")(
            kSlot, 1.0f, 0.0f, 1.0f, 1.0f);
        EXPECT_FALSE(pending()) << "glVertexAttrib4f left immediate geometry buffered";
    }
    if (queue_run("glVertexAttrib3fv")) {
        const GLfloat v[3] = {1.0f, 0.0f, 1.0f};
        Get<void (*)(GLuint, const GLfloat*)>("glVertexAttrib3fv")(kSlot, v);
        EXPECT_FALSE(pending()) << "glVertexAttrib3fv left immediate geometry buffered";
    }
    if (queue_run("glVertexAttrib1f")) {
        Get<void (*)(GLuint, GLfloat)>("glVertexAttrib1f")(kSlot, 0.5f);
        EXPECT_FALSE(pending()) << "glVertexAttrib1f left immediate geometry buffered";
    }
    if (queue_run("glVertexAttrib2f")) {
        Get<void (*)(GLuint, GLfloat, GLfloat)>("glVertexAttrib2f")(kSlot, 0.5f, 0.25f);
        EXPECT_FALSE(pending()) << "glVertexAttrib2f left immediate geometry buffered";
    }
    if (queue_run("glVertexAttrib4s")) {
        Get<void (*)(GLuint, GLshort, GLshort, GLshort, GLshort)>("glVertexAttrib4s")(kSlot, 1, 0,
                                                                                      1, 1);
        EXPECT_FALSE(pending()) << "glVertexAttrib4s left immediate geometry buffered";
    }
    if (queue_run("glVertexAttrib4dv")) {
        const double v[4] = {1.0, 0.0, 1.0, 1.0};
        Get<void (*)(GLuint, const double*)>("glVertexAttrib4dv")(kSlot, v);
        EXPECT_FALSE(pending()) << "glVertexAttrib4dv left immediate geometry buffered";
    }
    if (queue_run("glVertexAttrib4Nubv")) {
        const GLubyte v[4] = {255, 0, 255, 255};
        Get<void (*)(GLuint, const GLubyte*)>("glVertexAttrib4Nubv")(kSlot, v);
        EXPECT_FALSE(pending()) << "glVertexAttrib4Nubv left immediate geometry buffered";
    }

    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

// The consequence in pixels, on the shape that produced it: an OptiFine-style
// user program reading a pinned constant while geometry arrives through
// glBegin/glEnd.
TEST_F(VertexAttribOrderingTest, ConstantWrittenBetweenRunsDoesNotRepaintTheEarlierRun) {
    auto create_shader = Get<GLuint (*)(GLenum)>("glCreateShader");
    auto shader_source =
        Get<void (*)(GLuint, GLsizei, const GLchar* const*, const GLint*)>("glShaderSource");
    auto compile_shader = Get<void (*)(GLuint)>("glCompileShader");
    auto get_shaderiv = Get<void (*)(GLuint, GLenum, GLint*)>("glGetShaderiv");
    auto get_shader_info_log =
        Get<void (*)(GLuint, GLsizei, GLsizei*, GLchar*)>("glGetShaderInfoLog");
    auto create_program = Get<GLuint (*)()>("glCreateProgram");
    auto attach_shader = Get<void (*)(GLuint, GLuint)>("glAttachShader");
    auto bind_attrib_location = Get<void (*)(GLuint, GLuint, const GLchar*)>("glBindAttribLocation");
    auto link_program = Get<void (*)(GLuint)>("glLinkProgram");
    auto get_programiv = Get<void (*)(GLuint, GLenum, GLint*)>("glGetProgramiv");
    auto get_program_info_log =
        Get<void (*)(GLuint, GLsizei, GLsizei*, GLchar*)>("glGetProgramInfoLog");
    auto use_program = Get<void (*)(GLuint)>("glUseProgram");
    auto attrib4f =
        Get<void (*)(GLuint, GLfloat, GLfloat, GLfloat, GLfloat)>("glVertexAttrib4f");
    auto read_pixels =
        Get<void (*)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*)>("glReadPixels");
    ASSERT_NE(read_pixels, nullptr);
    PixelProbe probe(read_pixels);

    static const char* k_vs = "#version 120\n"
                              "attribute vec4 mc_Entity;\n"
                              "varying vec4 col;\n"
                              "void main() { gl_Position = gl_Vertex; col = mc_Entity; }\n";
    static const char* k_fs = "#version 120\n"
                              "varying vec4 col;\n"
                              "void main() { gl_FragColor = vec4(col.rgb, 1.0); }\n";
    const auto compile = [&](GLenum stage, const char* source, const char* tag) {
        const GLuint shader = create_shader(stage);
        shader_source(shader, 1, &source, nullptr);
        compile_shader(shader);
        GLint ok = 0;
        get_shaderiv(shader, GL_COMPILE_STATUS_, &ok);
        if (ok == 0) {
            char log[4096] = {};
            get_shader_info_log(shader, sizeof log, nullptr, log);
            ADD_FAILURE() << tag << " compile:\n" << log;
            return GLuint{0};
        }
        return shader;
    };
    const GLuint vs = compile(GL_VERTEX_SHADER_, k_vs, "vs");
    const GLuint fs = compile(GL_FRAGMENT_SHADER_, k_fs, "fs");
    ASSERT_NE(vs, 0u);
    ASSERT_NE(fs, 0u);
    const GLuint program = create_program();
    attach_shader(program, vs);
    attach_shader(program, fs);
    bind_attrib_location(program, kSlot, "mc_Entity");
    link_program(program);
    GLint linked = 0;
    get_programiv(program, GL_LINK_STATUS_, &linked);
    if (linked == 0) {
        char log[4096] = {};
        get_program_info_log(program, sizeof log, nullptr, log);
        FAIL() << "link:\n" << log;
    }
    use_program(program);

    // Both judgement colors have R == B, so a driver that swaps those two
    // channels on readback still tells them apart by green.
    const auto is_green = [](const PixelProbe::Rgba& p) {
        return p.g >= 200 && p.r <= 55 && p.b <= 55;
    };
    const auto is_magenta = [](const PixelProbe::Rgba& p) {
        return p.g <= 55 && p.r >= 200 && p.b >= 200;
    };
    const auto describe = [](const PixelProbe::Rgba& p) {
        return "(" + std::to_string((int)p.r) + ',' + std::to_string((int)p.g) + ',' +
               std::to_string((int)p.b) + ')';
    };

    clear_color_(0.0f, 0.0f, 0.0f, 1.0f);

    // Control: the constant reaches this shader through one run at all. If
    // this fails the case proves nothing about ordering.
    clear_(GL_COLOR_BUFFER_BIT_);
    attrib4f(kSlot, 0.0f, 1.0f, 0.0f, 1.0f);
    Quad(-0.9f, 0.9f);
    const auto control = probe.At(kWindow / 2, kWindow / 2);
    ASSERT_TRUE(is_green(control))
        << "control: a pinned constant attribute does not reach an immediate-mode draw at all, "
           "read back " << describe(control);

    // The defect: green run, constant changed to magenta, magenta run. The
    // first run is still in the merge batch when the constant changes.
    clear_(GL_COLOR_BUFFER_BIT_);
    attrib4f(kSlot, 0.0f, 1.0f, 0.0f, 1.0f);
    Quad(-0.9f, -0.1f);
    attrib4f(kSlot, 1.0f, 0.0f, 1.0f, 1.0f);
    Quad(0.1f, 0.9f);

    const auto left = probe.At(kWindow / 4, kWindow / 2);
    const auto right = probe.At(3 * kWindow / 4, kWindow / 2);
    EXPECT_TRUE(is_green(left)) << "the earlier run was repainted with the LATER constant: left "
                                   "half read back " << describe(left) << ", expected green";
    EXPECT_TRUE(is_magenta(right))
        << "the later run did not take the new constant: right half read back " << describe(right)
        << ", expected magenta";
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

// The float family must be the WRAPPER's entry point, and the ARB spellings
// must land on the same one - a legacy frontend asks for those by preference.
TEST_F(VertexAttribOrderingTest, FloatFamilyAndItsArbSpellingsResolveToTheWrapper) {
    constexpr const char* names[] = {
        "glVertexAttrib1f",  "glVertexAttrib1fv", "glVertexAttrib2f", "glVertexAttrib2fv",
        "glVertexAttrib3f",  "glVertexAttrib3fv", "glVertexAttrib4f", "glVertexAttrib4fv",
    };
    for (const char* canonical : names) {
        void* wrapper = Dlsym<void*>(canonical);
        EXPECT_NE(wrapper, nullptr) << canonical << " is not a wrapper entry point";
        EXPECT_EQ(Get<void*>(canonical), wrapper) << canonical << " does not resolve to the wrapper";
        const std::string alias = std::string(canonical) + "ARB";
        EXPECT_EQ(Get<void*>(alias.c_str()), wrapper) << alias << " does not resolve to the wrapper";
    }
}

// Owning the entry point brings the float family under glNewList with the
// rest of the family, which is where GL 2.1 puts it - the backend's own
// symbol was executed immediately instead.
TEST_F(VertexAttribOrderingTest, FloatFamilyIsCompiledIntoDisplayLists) {
    auto get_attribfv = Get<void (*)(GLuint, GLenum, GLfloat*)>("glGetVertexAttribfv");
    auto attrib4f = Get<void (*)(GLuint, GLfloat, GLfloat, GLfloat, GLfloat)>("glVertexAttrib4f");
    auto gen_lists = Get<GLuint (*)(GLsizei)>("glGenLists");
    auto new_list = Get<void (*)(GLuint, GLenum)>("glNewList");
    auto end_list = Get<void (*)()>("glEndList");
    auto call_list = Get<void (*)(GLuint)>("glCallList");

    constexpr GLenum GL_CURRENT_VERTEX_ATTRIB_ = 0x8626;
    constexpr GLenum GL_COMPILE_ = 0x1300;
    const auto expect_attrib = [&](GLfloat x, GLfloat y, GLfloat z, GLfloat w, const char* when) {
        GLfloat value[4] = {};
        get_attribfv(kSlot, GL_CURRENT_VERTEX_ATTRIB_, value);
        EXPECT_NEAR(value[0], x, 1e-6f) << when;
        EXPECT_NEAR(value[1], y, 1e-6f) << when;
        EXPECT_NEAR(value[2], z, 1e-6f) << when;
        EXPECT_NEAR(value[3], w, 1e-6f) << when;
    };

    attrib4f(kSlot, 1.0f, 2.0f, 3.0f, 4.0f);
    const GLuint list = gen_lists(1);
    ASSERT_NE(list, 0u);
    new_list(list, GL_COMPILE_);
    attrib4f(kSlot, 5.0f, 6.0f, 7.0f, 8.0f);
    end_list();
    expect_attrib(1, 2, 3, 4, "GL_COMPILE must not execute the call");
    call_list(list);
    expect_attrib(5, 6, 7, 8, "the replayed list must apply the recorded constant");
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

} // namespace
