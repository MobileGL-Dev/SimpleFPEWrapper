// SimpleFPEWrapper - tests/gtest_user_program_generic_arrays.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// plans/16 H6: a user program drawing through FIXED-FUNCTION arrays must
// still see the application's own GENERIC attribute arrays. OptiFine's
// Shaders.setupArrayPointersVbo() feeds mc_Entity / mc_midTexCoord /
// at_tangent off the same chunk VBO that glVertexPointer/glColorPointer
// read, so dropping the generic half leaves the shader reading the constant
// current value for those inputs.
//
// Judgement colours are swizzle-immune (see the llvmpipe BGRA R/B lesson in
// docs): the per-vertex data is green (0,1,0) fading to white (1,1,1) and the
// constant fallback is magenta (1,0,1), which is R==B. "The array won" is
// therefore G high, and "which vertex" is R - neither can be faked by a
// component-order quirk or by any single constant.

#include "sfpew_gtest.h"

namespace {

using sfpew_test::ContextTest;
using sfpew_test::GLbitfield;
using sfpew_test::GLchar;
using sfpew_test::GLdouble;
using sfpew_test::GLenum;
using sfpew_test::GLfloat;
using sfpew_test::GLint;
using sfpew_test::GLintptr;
using sfpew_test::GLsizei;
using sfpew_test::GLsizeiptr;
using sfpew_test::GLubyte;
using sfpew_test::GLuint;
using sfpew_test::GLushort;
using sfpew_test::PixelProbe;

constexpr int kWindow = 128;
constexpr GLenum GL_QUADS_ = 0x0007;
constexpr GLbitfield GL_COLOR_BUFFER_BIT_ = 0x00004000;
constexpr GLenum GL_VERTEX_ARRAY_ = 0x8074;
constexpr GLenum GL_FLOAT_ = 0x1406;
constexpr GLenum GL_UNSIGNED_SHORT_ = 0x1403;
constexpr GLenum GL_ARRAY_BUFFER_ = 0x8892;
constexpr GLenum GL_STATIC_DRAW_ = 0x88E4;
constexpr GLenum GL_COMPILE_STATUS_ = 0x8B81;
constexpr GLenum GL_LINK_STATUS_ = 0x8B82;
constexpr GLenum GL_VERTEX_SHADER_ = 0x8B31;
constexpr GLenum GL_FRAGMENT_SHADER_ = 0x8B30;
constexpr GLenum GL_NO_ERROR_ = 0;
constexpr GLenum GL_VERTEX_ARRAY_BINDING_ = 0x85B5;
constexpr GLenum GL_ARRAY_BUFFER_BINDING_ = 0x8894;
constexpr GLenum GL_VERTEX_ATTRIB_ARRAY_ENABLED_ = 0x8622;
constexpr GLenum GL_VERTEX_ATTRIB_ARRAY_SIZE_ = 0x8623;
constexpr GLenum GL_VERTEX_ATTRIB_ARRAY_STRIDE_ = 0x8624;
constexpr GLenum GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING_ = 0x889F;

// The location OptiFine pins mc_Entity to; picked here for the same reason -
// it is well clear of anything the translator's fpe_* inputs would land on.
constexpr GLuint kEntityLocation = 11;

class UserProgramGenericArraysTest : public ContextTest {
protected:
    UserProgramGenericArraysTest() : ContextTest(sfpew_test::Backend::GLES3, kWindow) {}

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
        get_program_info_log_ =
            Get<void (*)(GLuint, GLsizei, GLsizei*, GLchar*)>("glGetProgramInfoLog");
        use_program_ = Get<void (*)(GLuint)>("glUseProgram");
        bind_attrib_location_ = Get<void (*)(GLuint, GLuint, const GLchar*)>("glBindAttribLocation");
        vertex_attrib4f_ =
            Get<void (*)(GLuint, GLfloat, GLfloat, GLfloat, GLfloat)>("glVertexAttrib4f");
        vertex_attrib_pointer_ =
            Get<void (*)(GLuint, GLint, GLenum, GLubyte, GLsizei, const void*)>(
                "glVertexAttribPointer");
        enable_vertex_attrib_array_ = Get<void (*)(GLuint)>("glEnableVertexAttribArray");
        disable_vertex_attrib_array_ = Get<void (*)(GLuint)>("glDisableVertexAttribArray");
        get_vertex_attribdv_ = Get<void (*)(GLuint, GLenum, GLdouble*)>("glGetVertexAttribdv");
        gen_buffers_ = Get<void (*)(GLsizei, GLuint*)>("glGenBuffers");
        bind_buffer_ = Get<void (*)(GLenum, GLuint)>("glBindBuffer");
        buffer_data_ =
            Get<void (*)(GLenum, GLsizeiptr, const void*, GLenum)>("glBufferData");
        clear_color_ = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glClearColor");
        clear_ = Get<void (*)(GLbitfield)>("glClear");
        enable_client_state_ = Get<void (*)(GLenum)>("glEnableClientState");
        disable_client_state_ = Get<void (*)(GLenum)>("glDisableClientState");
        vertex_pointer_ = Get<void (*)(GLint, GLenum, GLsizei, const void*)>("glVertexPointer");
        draw_arrays_ = Get<void (*)(GLenum, GLint, GLsizei)>("glDrawArrays");
        draw_elements_ = Get<void (*)(GLenum, GLsizei, GLenum, const void*)>("glDrawElements");
        begin_ = Get<void (*)(GLenum)>("glBegin");
        end_ = Get<void (*)()>("glEnd");
        vertex2f_ = Get<void (*)(GLfloat, GLfloat)>("glVertex2f");
        get_integerv_ = Get<void (*)(GLenum, GLint*)>("glGetIntegerv");
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

    // gl_Vertex positions the quad, mc_Entity carries the per-vertex payload.
    // Exactly the OptiFine shape: the shader's own pinned attribute alongside
    // the compatibility builtin the translator rewrites into fpe_Vertex.
    GLuint BuildEntityProgram() {
        static const char* k_vs = "#version 120\n"
                                  "attribute vec4 mc_Entity;\n"
                                  "varying vec4 col;\n"
                                  "void main() { gl_Position = gl_Vertex; col = mc_Entity; }\n";
        static const char* k_fs = "#version 120\n"
                                  "varying vec4 col;\n"
                                  "void main() { gl_FragColor = vec4(col.rgb, 1.0); }\n";
        const GLuint vs = CompileStage(GL_VERTEX_SHADER_, k_vs, "vs");
        const GLuint fs = CompileStage(GL_FRAGMENT_SHADER_, k_fs, "fs");
        if (vs == 0 || fs == 0) return 0;
        const GLuint program = create_program_();
        attach_shader_(program, vs);
        attach_shader_(program, fs);
        bind_attrib_location_(program, kEntityLocation, "mc_Entity");
        link_program_(program);
        GLint linked = 0;
        get_programiv_(program, GL_LINK_STATUS_, &linked);
        if (linked == 0) {
            char log[4096] = {};
            get_program_info_log_(program, sizeof log, nullptr, log);
            ADD_FAILURE() << "link:\n" << log;
            return 0;
        }
        return program;
    }

    // Bottom edge green, top edge white: proves the value is per-vertex, not
    // just "some array element reached the shader".
    void ExpectPerVertexPayload(const char* tag) {
        const PixelProbe probe(read_pixels_);
        const auto bottom = probe.At(kWindow / 2, 4);
        const auto top = probe.At(kWindow / 2, kWindow - 5);
        EXPECT_GT((int)bottom.g, 200) << tag << ": bottom pixel (" << (int)bottom.r << ','
                                     << (int)bottom.g << ',' << (int)bottom.b
                                     << ") - the generic array never reached the shader";
        EXPECT_LT((int)bottom.r, 60) << tag << ": bottom pixel (" << (int)bottom.r << ','
                                    << (int)bottom.g << ',' << (int)bottom.b
                                    << ") - expected the array's green, not the constant";
        EXPECT_GT((int)top.g, 200) << tag << ": top pixel (" << (int)top.r << ',' << (int)top.g
                                  << ',' << (int)top.b << ")";
        EXPECT_GT((int)top.r, 200) << tag << ": top pixel (" << (int)top.r << ',' << (int)top.g
                                  << ',' << (int)top.b
                                  << ") - the payload is not varying per vertex";
    }

    GLdouble AttribQuery(GLuint index, GLenum pname) {
        GLdouble value[4] = {};
        get_vertex_attribdv_(index, pname, value);
        return value[0];
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
    void (*get_program_info_log_)(GLuint, GLsizei, GLsizei*, GLchar*) = nullptr;
    void (*use_program_)(GLuint) = nullptr;
    void (*bind_attrib_location_)(GLuint, GLuint, const GLchar*) = nullptr;
    void (*vertex_attrib4f_)(GLuint, GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*vertex_attrib_pointer_)(GLuint, GLint, GLenum, GLubyte, GLsizei, const void*) = nullptr;
    void (*enable_vertex_attrib_array_)(GLuint) = nullptr;
    void (*disable_vertex_attrib_array_)(GLuint) = nullptr;
    void (*get_vertex_attribdv_)(GLuint, GLenum, GLdouble*) = nullptr;
    void (*gen_buffers_)(GLsizei, GLuint*) = nullptr;
    void (*bind_buffer_)(GLenum, GLuint) = nullptr;
    void (*buffer_data_)(GLenum, GLsizeiptr, const void*, GLenum) = nullptr;
    void (*clear_color_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*clear_)(GLbitfield) = nullptr;
    void (*enable_client_state_)(GLenum) = nullptr;
    void (*disable_client_state_)(GLenum) = nullptr;
    void (*vertex_pointer_)(GLint, GLenum, GLsizei, const void*) = nullptr;
    void (*draw_arrays_)(GLenum, GLint, GLsizei) = nullptr;
    void (*draw_elements_)(GLenum, GLsizei, GLenum, const void*) = nullptr;
    void (*begin_)(GLenum) = nullptr;
    void (*end_)() = nullptr;
    void (*vertex2f_)(GLfloat, GLfloat) = nullptr;
    void (*get_integerv_)(GLenum, GLint*) = nullptr;
    GLenum (*get_error_)() = nullptr;
    void (*read_pixels_)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*) = nullptr;
};

// Quad corners in the order glVertexPointer feeds GL_QUADS.
const GLfloat kQuadPos[8] = {-1, -1, 1, -1, 1, 1, -1, 1};
// Bottom two vertices green, top two white.
const GLfloat kEntity[16] = {
    0, 1, 0, 1, 0, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1,
};

TEST_F(UserProgramGenericArraysTest, GenericArrayBeatsTheConstantOnDrawArrays) {
    const GLuint program = BuildEntityProgram();
    ASSERT_NE(program, 0u);
    use_program_(program);

    GLuint vbo = 0;
    gen_buffers_(1, &vbo);
    bind_buffer_(GL_ARRAY_BUFFER_, vbo);
    buffer_data_(GL_ARRAY_BUFFER_, (GLsizeiptr)sizeof kEntity, kEntity, GL_STATIC_DRAW_);
    vertex_attrib_pointer_(kEntityLocation, 4, GL_FLOAT_, 0, 0, nullptr);
    enable_vertex_attrib_array_(kEntityLocation);
    // The fallback OptiFine leaves in place; picking it up means the array
    // was dropped.
    vertex_attrib4f_(kEntityLocation, 1.0f, 0.0f, 1.0f, 1.0f);
    bind_buffer_(GL_ARRAY_BUFFER_, 0);

    clear_color_(0, 0, 0, 1);
    clear_(GL_COLOR_BUFFER_BIT_);
    enable_client_state_(GL_VERTEX_ARRAY_);
    vertex_pointer_(2, GL_FLOAT_, 0, kQuadPos);
    draw_arrays_(GL_QUADS_, 0, 4);
    ExpectPerVertexPayload("glDrawArrays");
    disable_client_state_(GL_VERTEX_ARRAY_);
    disable_vertex_attrib_array_(kEntityLocation);
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

TEST_F(UserProgramGenericArraysTest, GenericArrayBeatsTheConstantOnDrawElements) {
    const GLuint program = BuildEntityProgram();
    ASSERT_NE(program, 0u);
    use_program_(program);

    GLuint vbo = 0;
    gen_buffers_(1, &vbo);
    bind_buffer_(GL_ARRAY_BUFFER_, vbo);
    buffer_data_(GL_ARRAY_BUFFER_, (GLsizeiptr)sizeof kEntity, kEntity, GL_STATIC_DRAW_);
    vertex_attrib_pointer_(kEntityLocation, 4, GL_FLOAT_, 0, 0, nullptr);
    enable_vertex_attrib_array_(kEntityLocation);
    vertex_attrib4f_(kEntityLocation, 1.0f, 0.0f, 1.0f, 1.0f);
    bind_buffer_(GL_ARRAY_BUFFER_, 0);

    static const GLushort kIndices[4] = {0, 1, 2, 3};
    clear_color_(0, 0, 0, 1);
    clear_(GL_COLOR_BUFFER_BIT_);
    enable_client_state_(GL_VERTEX_ARRAY_);
    vertex_pointer_(2, GL_FLOAT_, 0, kQuadPos);
    draw_elements_(GL_QUADS_, 4, GL_UNSIGNED_SHORT_, kIndices);
    ExpectPerVertexPayload("glDrawElements");
    disable_client_state_(GL_VERTEX_ARRAY_);
    disable_vertex_attrib_array_(kEntityLocation);
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

// The chunk-VBO shape verbatim: position and the pinned attribute interleaved
// in ONE buffer, which is what setupArrayPointersVbo builds.
TEST_F(UserProgramGenericArraysTest, PositionAndGenericAttributeShareOneVertexBuffer) {
    const GLuint program = BuildEntityProgram();
    ASSERT_NE(program, 0u);
    use_program_(program);

    GLfloat interleaved[4 * 6];
    for (int v = 0; v < 4; ++v) {
        interleaved[v * 6 + 0] = kQuadPos[v * 2 + 0];
        interleaved[v * 6 + 1] = kQuadPos[v * 2 + 1];
        for (int c = 0; c < 4; ++c) interleaved[v * 6 + 2 + c] = kEntity[v * 4 + c];
    }
    GLuint vbo = 0;
    gen_buffers_(1, &vbo);
    bind_buffer_(GL_ARRAY_BUFFER_, vbo);
    buffer_data_(GL_ARRAY_BUFFER_, (GLsizeiptr)sizeof interleaved, interleaved, GL_STATIC_DRAW_);
    vertex_attrib_pointer_(kEntityLocation, 4, GL_FLOAT_, 0, 24,
                           (const void*)(GLintptr)(2 * sizeof(GLfloat)));
    enable_vertex_attrib_array_(kEntityLocation);
    vertex_attrib4f_(kEntityLocation, 1.0f, 0.0f, 1.0f, 1.0f);

    clear_color_(0, 0, 0, 1);
    clear_(GL_COLOR_BUFFER_BIT_);
    enable_client_state_(GL_VERTEX_ARRAY_);
    vertex_pointer_(2, GL_FLOAT_, 24, nullptr);
    draw_arrays_(GL_QUADS_, 0, 4);
    ExpectPerVertexPayload("shared chunk VBO");
    disable_client_state_(GL_VERTEX_ARRAY_);
    disable_vertex_attrib_array_(kEntityLocation);
    bind_buffer_(GL_ARRAY_BUFFER_, 0);
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

// Both halves of the OptiFine chunk shape at once: gl_Color off
// glColorPointer AND mc_Entity off glVertexAttribPointer, in the same draw.
// The fragment shader puts gl_Color.r in R *and* B so the judgement stays
// R==B symmetric, and mc_Entity.g in G. Dropping either input moves a
// different channel.
TEST_F(UserProgramGenericArraysTest, FixedFunctionAndGenericChannelsArriveTogether) {
    static const char* k_vs = "#version 120\n"
                              "attribute vec4 mc_Entity;\n"
                              "varying vec4 ff;\n"
                              "varying vec4 generic;\n"
                              "void main() { gl_Position = gl_Vertex; ff = gl_Color;"
                              " generic = mc_Entity; }\n";
    static const char* k_fs = "#version 120\n"
                              "varying vec4 ff;\n"
                              "varying vec4 generic;\n"
                              "void main() { gl_FragColor = vec4(ff.r, generic.g, ff.r, 1.0); }\n";
    const GLuint vs = CompileStage(GL_VERTEX_SHADER_, k_vs, "vs");
    const GLuint fs = CompileStage(GL_FRAGMENT_SHADER_, k_fs, "fs");
    ASSERT_NE(vs, 0u);
    ASSERT_NE(fs, 0u);
    const GLuint program = create_program_();
    attach_shader_(program, vs);
    attach_shader_(program, fs);
    bind_attrib_location_(program, kEntityLocation, "mc_Entity");
    link_program_(program);
    GLint linked = 0;
    get_programiv_(program, GL_LINK_STATUS_, &linked);
    ASSERT_NE(linked, 0);
    use_program_(program);

    // gl_Color.r: 0 on the bottom edge, 1 on the top. mc_Entity.g: always 1.
    static const GLfloat kColors[16] = {
        0, 0, 0, 1, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    };
    static const GLfloat kGreens[16] = {
        0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1,
    };
    auto color_pointer = Get<void (*)(GLint, GLenum, GLsizei, const void*)>("glColorPointer");
    ASSERT_NE(color_pointer, nullptr);

    GLuint vbo = 0;
    gen_buffers_(1, &vbo);
    bind_buffer_(GL_ARRAY_BUFFER_, vbo);
    buffer_data_(GL_ARRAY_BUFFER_, (GLsizeiptr)sizeof kGreens, kGreens, GL_STATIC_DRAW_);
    vertex_attrib_pointer_(kEntityLocation, 4, GL_FLOAT_, 0, 0, nullptr);
    enable_vertex_attrib_array_(kEntityLocation);
    vertex_attrib4f_(kEntityLocation, 1.0f, 0.0f, 1.0f, 1.0f);
    bind_buffer_(GL_ARRAY_BUFFER_, 0);

    clear_color_(0, 0, 0, 1);
    clear_(GL_COLOR_BUFFER_BIT_);
    enable_client_state_(GL_VERTEX_ARRAY_);
    enable_client_state_(0x8076 /* GL_COLOR_ARRAY */);
    vertex_pointer_(2, GL_FLOAT_, 0, kQuadPos);
    color_pointer(4, GL_FLOAT_, 0, kColors);
    draw_arrays_(GL_QUADS_, 0, 4);

    const PixelProbe probe(read_pixels_);
    const auto bottom = probe.At(kWindow / 2, 4);
    const auto top = probe.At(kWindow / 2, kWindow - 5);
    EXPECT_GT((int)bottom.g, 200) << "bottom (" << (int)bottom.r << ',' << (int)bottom.g << ','
                                 << (int)bottom.b << ") - mc_Entity never arrived";
    EXPECT_GT((int)top.g, 200) << "top (" << (int)top.r << ',' << (int)top.g << ',' << (int)top.b
                              << ") - mc_Entity never arrived";
    EXPECT_LT((int)bottom.r, 60) << "bottom (" << (int)bottom.r << ',' << (int)bottom.g << ','
                                << (int)bottom.b << ") - gl_Color stopped arriving";
    EXPECT_GT((int)top.r, 200) << "top (" << (int)top.r << ',' << (int)top.g << ',' << (int)top.b
                              << ") - gl_Color stopped arriving";
    disable_client_state_(0x8076);
    disable_client_state_(GL_VERTEX_ARRAY_);
    disable_vertex_attrib_array_(kEntityLocation);
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

// glDrawArrays(mode, first, count) with a client-memory fixed-function array:
// the gather folds `first` into its upload and issues the draw at 0, but the
// generic array in the app's VBO was never rebased. Vertices 0-3 carry the
// constant's own colour, so reading the wrong four elements is indistinguishable
// from dropping the array - which is the point.
TEST_F(UserProgramGenericArraysTest, GenericArrayFollowsANonZeroFirst) {
    const GLuint program = BuildEntityProgram();
    ASSERT_NE(program, 0u);
    use_program_(program);

    // 0-3: an offscreen decoy quad. 4-7: the quad under test.
    GLfloat positions[8 * 2];
    GLfloat entity[8 * 4];
    for (int v = 0; v < 4; ++v) {
        positions[v * 2 + 0] = -2.0f;
        positions[v * 2 + 1] = -2.0f;
        entity[v * 4 + 0] = 1.0f;
        entity[v * 4 + 1] = 0.0f;
        entity[v * 4 + 2] = 1.0f;
        entity[v * 4 + 3] = 1.0f;
    }
    for (int v = 0; v < 4; ++v) {
        positions[(4 + v) * 2 + 0] = kQuadPos[v * 2 + 0];
        positions[(4 + v) * 2 + 1] = kQuadPos[v * 2 + 1];
        for (int c = 0; c < 4; ++c) entity[(4 + v) * 4 + c] = kEntity[v * 4 + c];
    }

    GLuint vbo = 0;
    gen_buffers_(1, &vbo);
    bind_buffer_(GL_ARRAY_BUFFER_, vbo);
    buffer_data_(GL_ARRAY_BUFFER_, (GLsizeiptr)sizeof entity, entity, GL_STATIC_DRAW_);
    vertex_attrib_pointer_(kEntityLocation, 4, GL_FLOAT_, 0, 0, nullptr);
    enable_vertex_attrib_array_(kEntityLocation);
    vertex_attrib4f_(kEntityLocation, 1.0f, 0.0f, 1.0f, 1.0f);
    bind_buffer_(GL_ARRAY_BUFFER_, 0);

    clear_color_(0, 0, 0, 1);
    clear_(GL_COLOR_BUFFER_BIT_);
    enable_client_state_(GL_VERTEX_ARRAY_);
    vertex_pointer_(2, GL_FLOAT_, 0, positions);
    draw_arrays_(GL_QUADS_, 4, 4);
    ExpectPerVertexPayload("glDrawArrays(first = 4)");
    disable_client_state_(GL_VERTEX_ARRAY_);
    disable_vertex_attrib_array_(kEntityLocation);
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

// Generic attribute array state is per vertex array object. Declaring it in
// the app's own VAO must reach the draw, and must NOT leak into a draw made
// with a different VAO bound.
TEST_F(UserProgramGenericArraysTest, TheShadowIsPerVertexArrayObject) {
    auto gen_vertex_arrays = Get<void (*)(GLsizei, GLuint*)>("glGenVertexArrays");
    auto bind_vertex_array = Get<void (*)(GLuint)>("glBindVertexArray");
    ASSERT_NE(gen_vertex_arrays, nullptr);
    ASSERT_NE(bind_vertex_array, nullptr);

    const GLuint program = BuildEntityProgram();
    ASSERT_NE(program, 0u);
    use_program_(program);

    GLuint vbo = 0;
    gen_buffers_(1, &vbo);
    bind_buffer_(GL_ARRAY_BUFFER_, vbo);
    buffer_data_(GL_ARRAY_BUFFER_, (GLsizeiptr)sizeof kEntity, kEntity, GL_STATIC_DRAW_);
    bind_buffer_(GL_ARRAY_BUFFER_, 0);

    GLuint vao = 0;
    gen_vertex_arrays(1, &vao);
    ASSERT_NE(vao, 0u);
    bind_vertex_array(vao);
    bind_buffer_(GL_ARRAY_BUFFER_, vbo);
    vertex_attrib_pointer_(kEntityLocation, 4, GL_FLOAT_, 0, 0, nullptr);
    enable_vertex_attrib_array_(kEntityLocation);
    bind_buffer_(GL_ARRAY_BUFFER_, 0);
    vertex_attrib4f_(kEntityLocation, 1.0f, 0.0f, 1.0f, 1.0f);

    clear_color_(0, 0, 0, 1);
    clear_(GL_COLOR_BUFFER_BIT_);
    enable_client_state_(GL_VERTEX_ARRAY_);
    vertex_pointer_(2, GL_FLOAT_, 0, kQuadPos);
    draw_arrays_(GL_QUADS_, 0, 4);
    ExpectPerVertexPayload("app VAO");

    // VAO 0 never had the array declared, so its draw takes the constant.
    bind_vertex_array(0);
    clear_(GL_COLOR_BUFFER_BIT_);
    draw_arrays_(GL_QUADS_, 0, 4);
    const PixelProbe probe(read_pixels_);
    const auto centre = probe.At(kWindow / 2, kWindow / 2);
    EXPECT_LT((int)centre.g, 60) << "VAO 0 centre (" << (int)centre.r << ',' << (int)centre.g << ','
                                << (int)centre.b << ") - another VAO's array leaked into this draw";
    EXPECT_GT((int)centre.r, 200) << "VAO 0 centre (" << (int)centre.r << ',' << (int)centre.g
                                 << ',' << (int)centre.b << ")";
    disable_client_state_(GL_VERTEX_ARRAY_);
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

// Disabling the array must hand the location back to the current value on the
// very next draw - the replay adds locations to a wrapper-owned VAO that
// nothing else disables.
TEST_F(UserProgramGenericArraysTest, DisablingTheArrayRestoresTheCurrentValue) {
    const GLuint program = BuildEntityProgram();
    ASSERT_NE(program, 0u);
    use_program_(program);

    GLuint vbo = 0;
    gen_buffers_(1, &vbo);
    bind_buffer_(GL_ARRAY_BUFFER_, vbo);
    buffer_data_(GL_ARRAY_BUFFER_, (GLsizeiptr)sizeof kEntity, kEntity, GL_STATIC_DRAW_);
    vertex_attrib_pointer_(kEntityLocation, 4, GL_FLOAT_, 0, 0, nullptr);
    enable_vertex_attrib_array_(kEntityLocation);
    vertex_attrib4f_(kEntityLocation, 1.0f, 0.0f, 1.0f, 1.0f);
    bind_buffer_(GL_ARRAY_BUFFER_, 0);

    clear_color_(0, 0, 0, 1);
    clear_(GL_COLOR_BUFFER_BIT_);
    enable_client_state_(GL_VERTEX_ARRAY_);
    vertex_pointer_(2, GL_FLOAT_, 0, kQuadPos);
    draw_arrays_(GL_QUADS_, 0, 4);
    ExpectPerVertexPayload("array enabled");

    disable_vertex_attrib_array_(kEntityLocation);
    clear_(GL_COLOR_BUFFER_BIT_);
    draw_arrays_(GL_QUADS_, 0, 4);
    const PixelProbe probe(read_pixels_);
    const auto centre = probe.At(kWindow / 2, kWindow / 2);
    EXPECT_LT((int)centre.g, 60) << "after disable, centre (" << (int)centre.r << ','
                                << (int)centre.g << ',' << (int)centre.b
                                << ") - the array is still bound in the wrapper's VAO";
    EXPECT_GT((int)centre.r, 200) << "after disable, centre (" << (int)centre.r << ','
                                 << (int)centre.g << ',' << (int)centre.b << ")";
    disable_client_state_(GL_VERTEX_ARRAY_);
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

// GL 2.1 2.8: vertex arrays are consumed by the array draw commands only.
// Inside glBegin/glEnd every generic attribute takes its CURRENT value, so
// the constant must win here even with the array enabled. Guard, not a
// reproduction - it passes before the fix too, and must keep doing so.
TEST_F(UserProgramGenericArraysTest, ImmediateModeStillTakesTheCurrentValue) {
    const GLuint program = BuildEntityProgram();
    ASSERT_NE(program, 0u);
    use_program_(program);

    GLuint vbo = 0;
    gen_buffers_(1, &vbo);
    bind_buffer_(GL_ARRAY_BUFFER_, vbo);
    buffer_data_(GL_ARRAY_BUFFER_, (GLsizeiptr)sizeof kEntity, kEntity, GL_STATIC_DRAW_);
    vertex_attrib_pointer_(kEntityLocation, 4, GL_FLOAT_, 0, 0, nullptr);
    enable_vertex_attrib_array_(kEntityLocation);
    vertex_attrib4f_(kEntityLocation, 1.0f, 0.0f, 1.0f, 1.0f); // magenta
    bind_buffer_(GL_ARRAY_BUFFER_, 0);

    clear_color_(0, 0, 0, 1);
    clear_(GL_COLOR_BUFFER_BIT_);
    begin_(GL_QUADS_);
    vertex2f_(-1, -1);
    vertex2f_(1, -1);
    vertex2f_(1, 1);
    vertex2f_(-1, 1);
    end_();

    const PixelProbe probe(read_pixels_);
    const auto centre = probe.At(kWindow / 2, kWindow / 2);
    EXPECT_GT((int)centre.r, 200) << "immediate centre (" << (int)centre.r << ',' << (int)centre.g
                                 << ',' << (int)centre.b << ")";
    EXPECT_GT((int)centre.b, 200) << "immediate centre (" << (int)centre.r << ',' << (int)centre.g
                                 << ',' << (int)centre.b << ")";
    EXPECT_LT((int)centre.g, 60) << "immediate centre (" << (int)centre.r << ',' << (int)centre.g
                                << ',' << (int)centre.b
                                << ") - glBegin/glEnd must not consume the array";
    disable_vertex_attrib_array_(kEntityLocation);
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

// Whatever the wrapper does to satisfy the shader, the application's own
// vertex-array object must come back exactly as it was left.
TEST_F(UserProgramGenericArraysTest, TheApplicationsAttributeStateSurvivesTheDraw) {
    const GLuint program = BuildEntityProgram();
    ASSERT_NE(program, 0u);
    use_program_(program);

    GLuint vbo = 0;
    gen_buffers_(1, &vbo);
    bind_buffer_(GL_ARRAY_BUFFER_, vbo);
    buffer_data_(GL_ARRAY_BUFFER_, (GLsizeiptr)sizeof kEntity, kEntity, GL_STATIC_DRAW_);
    vertex_attrib_pointer_(kEntityLocation, 4, GL_FLOAT_, 0, 16, nullptr);
    enable_vertex_attrib_array_(kEntityLocation);
    bind_buffer_(GL_ARRAY_BUFFER_, 0);

    GLint vao_before = -1;
    get_integerv_(GL_VERTEX_ARRAY_BINDING_, &vao_before);

    clear_color_(0, 0, 0, 1);
    clear_(GL_COLOR_BUFFER_BIT_);
    enable_client_state_(GL_VERTEX_ARRAY_);
    vertex_pointer_(2, GL_FLOAT_, 0, kQuadPos);
    draw_arrays_(GL_QUADS_, 0, 4);
    disable_client_state_(GL_VERTEX_ARRAY_);

    GLint vao_after = -1;
    get_integerv_(GL_VERTEX_ARRAY_BINDING_, &vao_after);
    EXPECT_EQ(vao_after, vao_before) << "the draw left a different VAO bound";
    GLint array_buffer_after = -1;
    get_integerv_(GL_ARRAY_BUFFER_BINDING_, &array_buffer_after);
    EXPECT_EQ(array_buffer_after, 0) << "the draw left one of its own buffers bound";

    // glGetVertexAttrib{i,f}v are backend-fallthrough, so they would report
    // whichever VAO the deferred restore still has bound; glGetVertexAttribdv
    // is wrapped and takes the entry barrier, which hands the app's state back
    // first. Reading through it is the only way to ask about the app's VAO.
    EXPECT_EQ(AttribQuery(kEntityLocation, GL_VERTEX_ATTRIB_ARRAY_ENABLED_), 1.0);
    EXPECT_EQ(AttribQuery(kEntityLocation, GL_VERTEX_ATTRIB_ARRAY_SIZE_), 4.0);
    EXPECT_EQ(AttribQuery(kEntityLocation, GL_VERTEX_ATTRIB_ARRAY_STRIDE_), 16.0);
    EXPECT_EQ(AttribQuery(kEntityLocation, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING_), (GLdouble)vbo);

    disable_vertex_attrib_array_(kEntityLocation);
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

} // namespace
