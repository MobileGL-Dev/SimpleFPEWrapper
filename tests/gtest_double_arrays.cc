// SimpleFPEWrapper - tests/gtest_double_arrays.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// GL_DOUBLE is a legal client-array type for glVertexPointer, glColorPointer,
// glNormalPointer and glTexCoordPointer in GL 2.1 (section 2.8, table 2.4).
// ES has no such attribute type, so the wrapper has to convert on the CPU; it
// used to forward the type verbatim to glVertexAttribPointer (plans/16 M4).
//
// How that shows up depends entirely on the driver, which is why every case
// asserts the forwarding itself and not only the pixels: Mesa/llvmpipe
// answers GL_INVALID_ENUM, never specifies the attribute and draws nothing,
// while the NVIDIA GLES driver accepts GL_DOUBLE and converts it - the
// defect is invisible in pixels there, and would be equally invisible to a
// pixels-only test on any lenient backend.
//
// Every data source a double array can come from is covered, because they
// take different routes through the wrapper: client memory (one array and
// several), a buffer object, a mix of the two, the secondary colour (whose
// GLSL type follows the array's) and the GL 2.1 user-program path. A
// float-typed render is the control - it pins a failure to the TYPE rather
// than to the geometry.
//
// Green on black: both survive an R/B swap unchanged (llvmpipe's BGRA
// readback quirk).

#include "sfpew_gtest.h"

#include <vector>

namespace {

using sfpew_test::ContextTest;
using sfpew_test::GLbitfield;
using sfpew_test::GLchar;
using sfpew_test::GLdouble;
using sfpew_test::GLenum;
using sfpew_test::GLfloat;
using sfpew_test::GLint;
using sfpew_test::GLsizei;
using sfpew_test::GLsizeiptr;
using sfpew_test::GLubyte;
using sfpew_test::GLuint;

constexpr int kSize = 64;
constexpr GLbitfield GL_COLOR_BUFFER_BIT_ = 0x00004000;
constexpr GLenum GL_TRIANGLE_FAN_ = 0x0006;
constexpr GLenum GL_UNSIGNED_BYTE_ = 0x1401;
constexpr GLenum GL_FLOAT_ = 0x1406;
constexpr GLenum GL_DOUBLE_ = 0x140A;
constexpr GLenum GL_RGBA_ = 0x1908;
constexpr GLenum GL_NO_ERROR_ = 0;
constexpr GLenum GL_VERTEX_ARRAY_ = 0x8074;
constexpr GLenum GL_COLOR_ARRAY_ = 0x8076;
constexpr GLenum GL_SECONDARY_COLOR_ARRAY_ = 0x845E;
constexpr GLenum GL_COLOR_SUM_ = 0x8458;
constexpr GLenum GL_ARRAY_BUFFER_ = 0x8892;
constexpr GLenum GL_STATIC_DRAW_ = 0x88E4;
constexpr GLenum GL_COMPILE_STATUS_ = 0x8B81;
constexpr GLenum GL_LINK_STATUS_ = 0x8B82;
constexpr GLenum GL_VERTEX_SHADER_ = 0x8B31;
constexpr GLenum GL_VERTEX_ATTRIB_ARRAY_ENABLED_ = 0x8622;
constexpr GLenum GL_VERTEX_ATTRIB_ARRAY_TYPE_ = 0x8625;
constexpr GLenum GL_FRAGMENT_SHADER_ = 0x8B30;

constexpr int kVertices = 4;

const GLdouble kPositionsD[kVertices][3] = {
    {-1.0, -1.0, 0.0}, {1.0, -1.0, 0.0}, {1.0, 1.0, 0.0}, {-1.0, 1.0, 0.0}};
const GLfloat kPositionsF[kVertices][3] = {
    {-1.0f, -1.0f, 0.0f}, {1.0f, -1.0f, 0.0f}, {1.0f, 1.0f, 0.0f}, {-1.0f, 1.0f, 0.0f}};
const GLdouble kColorsD[kVertices][4] = {
    {0.0, 1.0, 0.0, 1.0}, {0.0, 1.0, 0.0, 1.0}, {0.0, 1.0, 0.0, 1.0}, {0.0, 1.0, 0.0, 1.0}};
const GLdouble kSecondaryD[kVertices][3] = {
    {0.0, 1.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 1.0, 0.0}};
const GLfloat kColorsF[kVertices][4] = {
    {0.0f, 1.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f, 1.0f},
    {0.0f, 1.0f, 0.0f, 1.0f}};

class DoubleArraysTest : public ContextTest {
protected:
    DoubleArraysTest() : ContextTest(sfpew_test::Backend::GLES3, kSize) {}

    void SetUp() override {
        ContextTest::SetUp();
        if (::testing::Test::IsSkipped()) return;

        clear_color_ = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glClearColor");
        clear_ = Get<void (*)(GLbitfield)>("glClear");
        finish_ = Get<void (*)()>("glFinish");
        get_error_ = Get<GLenum (*)()>("glGetError");
        enable_client_state_ = Get<void (*)(GLenum)>("glEnableClientState");
        disable_client_state_ = Get<void (*)(GLenum)>("glDisableClientState");
        vertex_pointer_ = Get<void (*)(GLint, GLenum, GLsizei, const void*)>("glVertexPointer");
        color_pointer_ = Get<void (*)(GLint, GLenum, GLsizei, const void*)>("glColorPointer");
        color4f_ = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glColor4f");
        secondary_color_pointer_ =
            Get<void (*)(GLint, GLenum, GLsizei, const void*)>("glSecondaryColorPointer");
        enable_ = Get<void (*)(GLenum)>("glEnable");
        disable_ = Get<void (*)(GLenum)>("glDisable");
        draw_arrays_ = Get<void (*)(GLenum, GLint, GLsizei)>("glDrawArrays");
        gen_buffers_ = Get<void (*)(GLsizei, GLuint*)>("glGenBuffers");
        delete_buffers_ = Get<void (*)(GLsizei, const GLuint*)>("glDeleteBuffers");
        bind_buffer_ = Get<void (*)(GLenum, GLuint)>("glBindBuffer");
        buffer_data_ = Get<void (*)(GLenum, GLsizeiptr, const void*, GLenum)>("glBufferData");
        read_pixels_ =
            Get<void (*)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*)>("glReadPixels");
        create_shader_ = Get<GLuint (*)(GLenum)>("glCreateShader");
        shader_source_ =
            Get<void (*)(GLuint, GLsizei, const GLchar* const*, const GLint*)>("glShaderSource");
        compile_shader_ = Get<void (*)(GLuint)>("glCompileShader");
        get_shaderiv_ = Get<void (*)(GLuint, GLenum, GLint*)>("glGetShaderiv");
        create_program_ = Get<GLuint (*)()>("glCreateProgram");
        attach_shader_ = Get<void (*)(GLuint, GLuint)>("glAttachShader");
        link_program_ = Get<void (*)(GLuint)>("glLinkProgram");
        get_programiv_ = Get<void (*)(GLuint, GLenum, GLint*)>("glGetProgramiv");
        use_program_ = Get<void (*)(GLuint)>("glUseProgram");
        get_vertex_attribiv_ = Get<void (*)(GLuint, GLenum, GLint*)>("glGetVertexAttribiv");
        ASSERT_NE(read_pixels_, nullptr);
        ASSERT_NE(get_vertex_attribiv_, nullptr);

        clear_color_(0.0f, 0.0f, 0.0f, 1.0f);
        while (get_error_() != GL_NO_ERROR_) {
        }
    }

    void TearDown() override {
        if (disable_client_state_ != nullptr) {
            disable_client_state_(GL_VERTEX_ARRAY_);
            disable_client_state_(GL_COLOR_ARRAY_);
        }
        ContextTest::TearDown();
    }

    // The mechanism, independently of how forgiving the driver underneath is:
    // whatever the wrapper handed glVertexAttribPointer is still readable off
    // the VAO it drew from, because a fixed-function draw holds its state
    // rather than restoring it and glGetVertexAttribiv takes no barrier. The
    // NVIDIA GLES driver accepts GL_DOUBLE and converts it, Mesa/llvmpipe
    // answers GL_INVALID_ENUM and specifies nothing - this assertion catches
    // the forwarding on both.
    void ExpectNoDoubleAttributeType(const char* tag) {
        for (GLuint index = 0; index < 8; ++index) {
            GLint enabled = 0;
            GLint type = 0;
            get_vertex_attribiv_(index, GL_VERTEX_ATTRIB_ARRAY_ENABLED_, &enabled);
            get_vertex_attribiv_(index, GL_VERTEX_ATTRIB_ARRAY_TYPE_, &type);
            if (enabled == 0) continue;
            EXPECT_NE(type, (GLint)GL_DOUBLE_)
                << tag << ": attribute " << index << " was specified as GL_DOUBLE";
        }
    }

    // Whole-viewport fan; a dropped attribute leaves the clear colour behind.
    void ExpectGreenQuad(const char* tag) {
        GLubyte px[4] = {};
        finish_();
        read_pixels_(kSize / 2, kSize / 2, 1, 1, GL_RGBA_, GL_UNSIGNED_BYTE_, px);
        EXPECT_GT(px[1], 200) << tag << ": centre (" << (int)px[0] << ',' << (int)px[1] << ','
                              << (int)px[2] << ')';
        EXPECT_LT(px[0], 60) << tag << ": centre red";
        EXPECT_LT(px[2], 60) << tag << ": centre blue";
        EXPECT_EQ(get_error_(), GL_NO_ERROR_) << tag << ": latched error";
    }

    GLuint LinkPassthroughProgram() {
        static const char* k_vs = "#version 120\n"
                                  "varying vec4 col;\n"
                                  "void main() { gl_Position = gl_Vertex; col = gl_Color; }\n";
        static const char* k_fs = "#version 120\n"
                                  "varying vec4 col;\n"
                                  "void main() { gl_FragColor = col; }\n";
        const GLuint vs = create_shader_(GL_VERTEX_SHADER_);
        shader_source_(vs, 1, &k_vs, nullptr);
        compile_shader_(vs);
        const GLuint fs = create_shader_(GL_FRAGMENT_SHADER_);
        shader_source_(fs, 1, &k_fs, nullptr);
        compile_shader_(fs);
        GLint ok = 0;
        get_shaderiv_(vs, GL_COMPILE_STATUS_, &ok);
        EXPECT_NE(ok, 0) << "vs compile";
        get_shaderiv_(fs, GL_COMPILE_STATUS_, &ok);
        EXPECT_NE(ok, 0) << "fs compile";
        const GLuint program = create_program_();
        attach_shader_(program, vs);
        attach_shader_(program, fs);
        link_program_(program);
        GLint linked = 0;
        get_programiv_(program, GL_LINK_STATUS_, &linked);
        EXPECT_NE(linked, 0) << "link";
        return program;
    }

    void (*clear_color_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*clear_)(GLbitfield) = nullptr;
    void (*finish_)() = nullptr;
    GLenum (*get_error_)() = nullptr;
    void (*enable_client_state_)(GLenum) = nullptr;
    void (*disable_client_state_)(GLenum) = nullptr;
    void (*vertex_pointer_)(GLint, GLenum, GLsizei, const void*) = nullptr;
    void (*color_pointer_)(GLint, GLenum, GLsizei, const void*) = nullptr;
    void (*color4f_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*secondary_color_pointer_)(GLint, GLenum, GLsizei, const void*) = nullptr;
    void (*enable_)(GLenum) = nullptr;
    void (*disable_)(GLenum) = nullptr;
    void (*draw_arrays_)(GLenum, GLint, GLsizei) = nullptr;
    void (*gen_buffers_)(GLsizei, GLuint*) = nullptr;
    void (*delete_buffers_)(GLsizei, const GLuint*) = nullptr;
    void (*bind_buffer_)(GLenum, GLuint) = nullptr;
    void (*buffer_data_)(GLenum, GLsizeiptr, const void*, GLenum) = nullptr;
    void (*read_pixels_)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*) = nullptr;
    GLuint (*create_shader_)(GLenum) = nullptr;
    void (*shader_source_)(GLuint, GLsizei, const GLchar* const*, const GLint*) = nullptr;
    void (*compile_shader_)(GLuint) = nullptr;
    void (*get_shaderiv_)(GLuint, GLenum, GLint*) = nullptr;
    GLuint (*create_program_)() = nullptr;
    void (*attach_shader_)(GLuint, GLuint) = nullptr;
    void (*link_program_)(GLuint) = nullptr;
    void (*get_programiv_)(GLuint, GLenum, GLint*) = nullptr;
    void (*use_program_)(GLuint) = nullptr;
    void (*get_vertex_attribiv_)(GLuint, GLenum, GLint*) = nullptr;
};

// The control: identical geometry declared as floats. If this one fails the
// suite is wrong about something other than the type.
TEST_F(DoubleArraysTest, FloatTwinOfEveryCaseRenders) {
    clear_(GL_COLOR_BUFFER_BIT_);
    enable_client_state_(GL_VERTEX_ARRAY_);
    enable_client_state_(GL_COLOR_ARRAY_);
    vertex_pointer_(3, GL_FLOAT_, 0, kPositionsF);
    color_pointer_(4, GL_FLOAT_, 0, kColorsF);
    draw_arrays_(GL_TRIANGLE_FAN_, 0, kVertices);
    ExpectGreenQuad("float client arrays");
}

TEST_F(DoubleArraysTest, ClientMemoryDoublePositionAndColorRender) {
    clear_(GL_COLOR_BUFFER_BIT_);
    enable_client_state_(GL_VERTEX_ARRAY_);
    enable_client_state_(GL_COLOR_ARRAY_);
    vertex_pointer_(3, GL_DOUBLE_, 0, kPositionsD);
    color_pointer_(4, GL_DOUBLE_, 0, kColorsD);
    draw_arrays_(GL_TRIANGLE_FAN_, 0, kVertices);
    ExpectNoDoubleAttributeType("double client arrays");
    ExpectGreenQuad("double client arrays");
}

// A lone double array takes a different route from two of them - the
// interleaving repack needs at least two arrays to be worth running.
TEST_F(DoubleArraysTest, LoneDoublePositionArrayRenders) {
    clear_(GL_COLOR_BUFFER_BIT_);
    enable_client_state_(GL_VERTEX_ARRAY_);
    disable_client_state_(GL_COLOR_ARRAY_);
    color4f_(0.0f, 1.0f, 0.0f, 1.0f);
    vertex_pointer_(3, GL_DOUBLE_, 0, kPositionsD);
    draw_arrays_(GL_TRIANGLE_FAN_, 0, kVertices);
    ExpectNoDoubleAttributeType("lone double position array");
    ExpectGreenQuad("lone double position array");
}

TEST_F(DoubleArraysTest, BufferBackedDoubleArraysRender) {
    GLuint vbo = 0;
    gen_buffers_(1, &vbo);
    ASSERT_NE(vbo, 0u);
    // One interleaved block: 3 position doubles then 4 colour doubles.
    std::vector<GLdouble> block;
    for (int v = 0; v < kVertices; ++v) {
        for (int c = 0; c < 3; ++c) block.push_back(kPositionsD[v][c]);
        for (int c = 0; c < 4; ++c) block.push_back(kColorsD[v][c]);
    }
    bind_buffer_(GL_ARRAY_BUFFER_, vbo);
    buffer_data_(GL_ARRAY_BUFFER_, (GLsizeiptr)(block.size() * sizeof(GLdouble)), block.data(),
                 GL_STATIC_DRAW_);
    const GLsizei stride = 7 * (GLsizei)sizeof(GLdouble);
    enable_client_state_(GL_VERTEX_ARRAY_);
    enable_client_state_(GL_COLOR_ARRAY_);
    vertex_pointer_(3, GL_DOUBLE_, stride, nullptr);
    color_pointer_(4, GL_DOUBLE_, stride, (const void*)(3 * sizeof(GLdouble)));
    clear_(GL_COLOR_BUFFER_BIT_);
    draw_arrays_(GL_TRIANGLE_FAN_, 0, kVertices);
    ExpectNoDoubleAttributeType("double arrays in a buffer object");
    ExpectGreenQuad("double arrays in a buffer object");
    bind_buffer_(GL_ARRAY_BUFFER_, 0);
    delete_buffers_(1, &vbo);
}

// One double array in client memory, one float array in a buffer: neither of
// the two single-source routes can express it.
TEST_F(DoubleArraysTest, DoubleClientArrayMixedWithABufferBackedFloatArrayRenders) {
    GLuint vbo = 0;
    gen_buffers_(1, &vbo);
    ASSERT_NE(vbo, 0u);
    bind_buffer_(GL_ARRAY_BUFFER_, vbo);
    buffer_data_(GL_ARRAY_BUFFER_, (GLsizeiptr)sizeof kColorsF, kColorsF, GL_STATIC_DRAW_);
    enable_client_state_(GL_COLOR_ARRAY_);
    color_pointer_(4, GL_FLOAT_, 0, nullptr);
    bind_buffer_(GL_ARRAY_BUFFER_, 0);
    enable_client_state_(GL_VERTEX_ARRAY_);
    vertex_pointer_(3, GL_DOUBLE_, 0, kPositionsD);
    clear_(GL_COLOR_BUFFER_BIT_);
    draw_arrays_(GL_TRIANGLE_FAN_, 0, kVertices);
    ExpectNoDoubleAttributeType("double client array mixed with a buffer-backed float array");
    ExpectGreenQuad("double client array mixed with a buffer-backed float array");
    delete_buffers_(1, &vbo);
}

// Position, colour and texture coordinates are declared vec3/vec4 in the
// generated shader whatever the array says; the secondary colour is one of
// the few whose declared GLSL type follows the array's, so a double one used
// to reach the compiler as `dvec3`. Primary colour black plus a green
// secondary under GL_COLOR_SUM sums to green.
TEST_F(DoubleArraysTest, DoubleSecondaryColorArrayRenders) {
    clear_(GL_COLOR_BUFFER_BIT_);
    color4f_(0.0f, 0.0f, 0.0f, 1.0f);
    enable_(GL_COLOR_SUM_);
    enable_client_state_(GL_VERTEX_ARRAY_);
    enable_client_state_(GL_SECONDARY_COLOR_ARRAY_);
    vertex_pointer_(3, GL_DOUBLE_, 0, kPositionsD);
    secondary_color_pointer_(3, GL_DOUBLE_, 0, kSecondaryD);
    draw_arrays_(GL_TRIANGLE_FAN_, 0, kVertices);
    ExpectNoDoubleAttributeType("double secondary colour array");
    ExpectGreenQuad("double secondary colour array");
    disable_client_state_(GL_SECONDARY_COLOR_ARRAY_);
    disable_(GL_COLOR_SUM_);
}

// GL 2.1: a bound user program still consumes the fixed-function arrays, and
// the conversion has to reach that path too.
TEST_F(DoubleArraysTest, UserProgramConsumesDoubleArrays) {
    const GLuint program = LinkPassthroughProgram();
    ASSERT_NE(program, 0u);
    use_program_(program);
    clear_(GL_COLOR_BUFFER_BIT_);
    enable_client_state_(GL_VERTEX_ARRAY_);
    enable_client_state_(GL_COLOR_ARRAY_);
    vertex_pointer_(3, GL_DOUBLE_, 0, kPositionsD);
    color_pointer_(4, GL_DOUBLE_, 0, kColorsD);
    draw_arrays_(GL_TRIANGLE_FAN_, 0, kVertices);
    ExpectNoDoubleAttributeType("double client arrays through a user program");
    ExpectGreenQuad("double client arrays through a user program");
    use_program_(0);
}

} // namespace
