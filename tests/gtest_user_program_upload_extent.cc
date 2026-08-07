// SimpleFPEWrapper - tests/gtest_user_program_upload_extent.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// A client-memory vertex array only has to hold, per GL 2.1 section 2.8,
// the bytes its attributes actually occupy: the LAST row stops at its last
// attribute, not at the end of a stride. An interleaved row of 16 bytes in a
// stride of 32 therefore requires (count-1)*32 + 16 bytes and nothing more,
// and an application is entitled to end the allocation exactly there.
// commit_fpe_state_on_draw was fixed for that years ago; the two GL 2.1
// user-program paths (a user program current while geometry still arrives
// through glVertexPointer - the OptiFine shape) still uploaded count*stride
// and read a full stride past the last vertex (plans/16 H3).
//
// The array is placed so its last required byte is the last byte of a mapped
// page and the page after it is PROT_NONE, which turns "reads too far" into
// a fault at the exact byte where the over-read begins rather than into
// silently-read neighbouring memory. Reproduced as a SIGSEGV inside
// glBufferData in the plan; the trap below reports it as a test failure
// instead of killing the process.
//
// Green is used for the geometry check because it survives an R/B swap
// unchanged (llvmpipe's BGRA readback quirk).

#include "sfpew_fault_trap.h"
#include "sfpew_gtest.h"

#include <cstring>

namespace {

using sfpew_test::ContextTest;
using sfpew_test::FaultProbe;
using sfpew_test::GLbitfield;
using sfpew_test::GLchar;
using sfpew_test::GLenum;
using sfpew_test::GLfloat;
using sfpew_test::GLint;
using sfpew_test::GLsizei;
using sfpew_test::GLubyte;
using sfpew_test::GLuint;
using sfpew_test::GLushort;
using sfpew_test::GuardedArray;

constexpr int kSize = 64;
constexpr GLbitfield GL_COLOR_BUFFER_BIT_ = 0x00004000;
constexpr GLenum GL_TRIANGLE_FAN_ = 0x0006;
constexpr GLenum GL_UNSIGNED_BYTE_ = 0x1401;
constexpr GLenum GL_UNSIGNED_SHORT_ = 0x1403;
constexpr GLenum GL_FLOAT_ = 0x1406;
constexpr GLenum GL_RGBA_ = 0x1908;
constexpr GLenum GL_VERTEX_ARRAY_ = 0x8074;
constexpr GLenum GL_COLOR_ARRAY_ = 0x8076;
constexpr GLenum GL_COMPILE_STATUS_ = 0x8B81;
constexpr GLenum GL_LINK_STATUS_ = 0x8B82;
constexpr GLenum GL_VERTEX_SHADER_ = 0x8B31;
constexpr GLenum GL_FRAGMENT_SHADER_ = 0x8B30;

// The interleaved row the plan's repro uses: three position floats, four
// colour bytes at offset 12, sixteen bytes of tail padding no attribute
// occupies.
constexpr int kStride = 32;
constexpr int kRowExtent = 12 + 4;
constexpr int kVertices = 4;
constexpr size_t kRequired = static_cast<size_t>(kVertices - 1) * kStride + kRowExtent;

class UserProgramUploadExtentTest : public ContextTest {
protected:
    UserProgramUploadExtentTest() : ContextTest(sfpew_test::Backend::GLES3, kSize) {}

    void SetUp() override {
        ContextTest::SetUp();
        if (::testing::Test::IsSkipped()) return;

        create_shader_ = Get<GLuint (*)(GLenum)>("glCreateShader");
        shader_source_ =
            Get<void (*)(GLuint, GLsizei, const GLchar* const*, const GLint*)>("glShaderSource");
        compile_shader_ = Get<void (*)(GLuint)>("glCompileShader");
        get_shaderiv_ = Get<void (*)(GLuint, GLenum, GLint*)>("glGetShaderiv");
        get_shader_info_log_ =
            Get<void (*)(GLuint, GLsizei, GLsizei*, GLchar*)>("glGetShaderInfoLog");
        create_program_ = Get<GLuint (*)()>("glCreateProgram");
        attach_shader_ = Get<void (*)(GLuint, GLuint)>("glAttachShader");
        link_program_ = Get<void (*)(GLuint)>("glLinkProgram");
        get_programiv_ = Get<void (*)(GLuint, GLenum, GLint*)>("glGetProgramiv");
        use_program_ = Get<void (*)(GLuint)>("glUseProgram");
        clear_color_ = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glClearColor");
        clear_ = Get<void (*)(GLbitfield)>("glClear");
        finish_ = Get<void (*)()>("glFinish");
        enable_client_state_ = Get<void (*)(GLenum)>("glEnableClientState");
        disable_client_state_ = Get<void (*)(GLenum)>("glDisableClientState");
        vertex_pointer_ = Get<void (*)(GLint, GLenum, GLsizei, const void*)>("glVertexPointer");
        color_pointer_ = Get<void (*)(GLint, GLenum, GLsizei, const void*)>("glColorPointer");
        draw_arrays_ = Get<void (*)(GLenum, GLint, GLsizei)>("glDrawArrays");
        draw_elements_ = Get<void (*)(GLenum, GLsizei, GLenum, const void*)>("glDrawElements");
        read_pixels_ =
            Get<void (*)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*)>("glReadPixels");
        ASSERT_NE(read_pixels_, nullptr);

        // Straight pass-through of the fixed-function channels: the point of
        // the case is the upload, so the program only has to be a USER
        // program that consumes gl_Vertex/gl_Color.
        static const char* k_vs = "#version 120\n"
                                  "varying vec4 col;\n"
                                  "void main() { gl_Position = gl_Vertex; col = gl_Color; }\n";
        static const char* k_fs = "#version 120\n"
                                  "varying vec4 col;\n"
                                  "void main() { gl_FragColor = col; }\n";
        const GLuint vs = CompileStage(GL_VERTEX_SHADER_, k_vs, "vs");
        const GLuint fs = CompileStage(GL_FRAGMENT_SHADER_, k_fs, "fs");
        ASSERT_NE(vs, 0u);
        ASSERT_NE(fs, 0u);
        program_ = create_program_();
        attach_shader_(program_, vs);
        attach_shader_(program_, fs);
        link_program_(program_);
        GLint linked = 0;
        get_programiv_(program_, GL_LINK_STATUS_, &linked);
        ASSERT_NE(linked, 0) << "link";
        use_program_(program_);

        clear_color_(0.0f, 0.0f, 0.0f, 1.0f);
        clear_(GL_COLOR_BUFFER_BIT_);
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

    // A full-viewport fan whose four rows start at `data`.
    static void FillFan(unsigned char* data) {
        static const GLfloat corners[kVertices][3] = {
            {-1.0f, -1.0f, 0.0f}, {1.0f, -1.0f, 0.0f}, {1.0f, 1.0f, 0.0f}, {-1.0f, 1.0f, 0.0f}};
        for (int v = 0; v < kVertices; ++v) {
            std::memcpy(data + v * kStride, corners[v], sizeof corners[v]);
            const unsigned char green[4] = {0, 255, 0, 255};
            std::memcpy(data + v * kStride + 12, green, sizeof green);
        }
    }

    void DeclareArrays(const unsigned char* data) {
        enable_client_state_(GL_VERTEX_ARRAY_);
        enable_client_state_(GL_COLOR_ARRAY_);
        vertex_pointer_(3, GL_FLOAT_, kStride, data);
        color_pointer_(4, GL_UNSIGNED_BYTE_, kStride, data + 12);
    }

    void ExpectGreenQuad(const char* tag) {
        GLubyte px[4] = {};
        finish_();
        read_pixels_(kSize / 2, kSize / 2, 1, 1, GL_RGBA_, GL_UNSIGNED_BYTE_, px);
        EXPECT_GT(px[1], 200) << tag << ": centre (" << (int)px[0] << ',' << (int)px[1] << ','
                              << (int)px[2] << ')';
        EXPECT_LT(px[0], 60) << tag << ": centre red";
        EXPECT_LT(px[2], 60) << tag << ": centre blue";
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
    void (*clear_color_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*clear_)(GLbitfield) = nullptr;
    void (*finish_)() = nullptr;
    void (*enable_client_state_)(GLenum) = nullptr;
    void (*disable_client_state_)(GLenum) = nullptr;
    void (*vertex_pointer_)(GLint, GLenum, GLsizei, const void*) = nullptr;
    void (*color_pointer_)(GLint, GLenum, GLsizei, const void*) = nullptr;
    void (*draw_arrays_)(GLenum, GLint, GLsizei) = nullptr;
    void (*draw_elements_)(GLenum, GLsizei, GLenum, const void*) = nullptr;
    void (*read_pixels_)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*) = nullptr;
    GLuint program_ = 0;
};

TEST_F(UserProgramUploadExtentTest, DrawArraysStopsAtTheLastRowsLastAttribute) {
    GuardedArray array(kRequired);
    ASSERT_NE(array.data(), nullptr) << "mmap";
    FillFan(array.data());
    DeclareArrays(array.data());

    FaultProbe probe;
    if (probe.Try()) draw_arrays_(GL_TRIANGLE_FAN_, 0, kVertices);
    ASSERT_FALSE(probe.faulted()) << "glDrawArrays uploaded past byte " << kRequired
                                  << " of a client array that only has that many";
    ExpectGreenQuad("glDrawArrays");
}

TEST_F(UserProgramUploadExtentTest, DrawArraysAccountsForTheFirstVertexOffset) {
    // first=1: the upload starts one stride in, so the array still has to end
    // exactly at the last row's last attribute for the extent to be exercised.
    GuardedArray array(kRequired);
    ASSERT_NE(array.data(), nullptr) << "mmap";
    FillFan(array.data());
    DeclareArrays(array.data());

    FaultProbe probe;
    if (probe.Try()) draw_arrays_(GL_TRIANGLE_FAN_, 1, kVertices - 1);
    ASSERT_FALSE(probe.faulted())
        << "glDrawArrays(first=1) uploaded past the end of the client array";
}

TEST_F(UserProgramUploadExtentTest, DrawElementsStopsAtTheLastRowsLastAttribute) {
    GuardedArray array(kRequired);
    ASSERT_NE(array.data(), nullptr) << "mmap";
    FillFan(array.data());
    DeclareArrays(array.data());

    static const GLushort indices[kVertices] = {0, 1, 2, 3};
    FaultProbe probe;
    if (probe.Try()) draw_elements_(GL_TRIANGLE_FAN_, kVertices, GL_UNSIGNED_SHORT_, indices);
    ASSERT_FALSE(probe.faulted()) << "glDrawElements uploaded past byte " << kRequired
                                  << " of a client array that only has that many";
    ExpectGreenQuad("glDrawElements");
}

} // namespace
