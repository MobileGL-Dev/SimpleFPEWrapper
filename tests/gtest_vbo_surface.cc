// SimpleFPEWrapper - tests/gtest_vbo_surface.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// plans/12: the buffer / vertex-attribute surface goes through the wrapper.
//
// glBufferData, glVertexAttribPointer, glEnableVertexAttribArray and friends
// read and write through the bound GL_ARRAY_BUFFER and VAO. They used to
// resolve to the backend's own pointers, leaving the wrapper shadowing the
// array-buffer binding while blind to writes through it. This drives them the
// way an LWJGL frontend does and interleaves fixed-function immediate draws,
// which is the case that breaks if the wrapper ever leaves its own ring buffer
// or VAO bound across an entry point:
//   A: VBO + attributes + user program renders (proves plain pass-through)
//   B: an immediate-mode fixed-function draw between two VBO draws
//   C: the VBO draw again, unchanged - its buffer and attribute state must
//      have survived the fixed-function draw in between
//   C2: pre-FPE program/buffer/attribute state survives glEnd without re-bind
//   C3: ...and survives a following texture bind (the narrowed barrier)
//   D: glBufferSubData / the ARB spellings reach the same wrappers
//   E: glGetBufferParameteriv / glMapBufferRange / glUnmapBuffer round-trip

#include "sfpew_gtest.h"

#include <cstring>
#include <optional>

namespace {

using sfpew_test::ContextTest;
using sfpew_test::GLbitfield;
using sfpew_test::GLboolean;
using sfpew_test::GLchar;
using sfpew_test::GLenum;
using sfpew_test::GLfloat;
using sfpew_test::GLint;
using sfpew_test::GLintptr;
using sfpew_test::GLsizei;
using sfpew_test::GLsizeiptr;
using sfpew_test::GLubyte;
using sfpew_test::GLuint;

constexpr int kWindow = 256;
constexpr GLenum GL_ARRAY_BUFFER_ = 0x8892;
constexpr GLenum GL_TEXTURE_2D_ = 0x0DE1;
constexpr GLenum GL_TEXTURE0_ = 0x84C0;
constexpr GLenum GL_STATIC_DRAW_ = 0x88E4;
constexpr GLenum GL_BUFFER_SIZE_ = 0x8764;
constexpr GLbitfield GL_MAP_READ_BIT_ = 0x0001;
constexpr GLenum GL_TRIANGLES_ = 0x0004;
constexpr GLenum GL_QUADS_ = 0x0007;
constexpr GLbitfield GL_COLOR_BUFFER_BIT_ = 0x00004000;
constexpr GLenum GL_FLOAT_ = 0x1406;
constexpr GLenum GL_COMPILE_STATUS_ = 0x8B81;
constexpr GLenum GL_LINK_STATUS_ = 0x8B82;
constexpr GLenum GL_VERTEX_SHADER_ = 0x8B31;
constexpr GLenum GL_FRAGMENT_SHADER_ = 0x8B30;
constexpr GLenum GL_NO_ERROR_ = 0;

class VboSurfaceTest : public ContextTest {
protected:
    VboSurfaceTest() : ContextTest(sfpew_test::Backend::GLES3, kWindow) {}

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
        get_attrib_location_ = Get<GLint (*)(GLuint, const GLchar*)>("glGetAttribLocation");
        gen_buffers_ = Get<void (*)(GLsizei, GLuint*)>("glGenBuffers");
        bind_buffer_ = Get<void (*)(GLenum, GLuint)>("glBindBuffer");
        buffer_data_ = Get<void (*)(GLenum, GLsizeiptr, const void*, GLenum)>("glBufferData");
        buffer_sub_data_ =
            Get<void (*)(GLenum, GLintptr, GLsizeiptr, const void*)>("glBufferSubData");
        get_buffer_parameteriv_ = Get<void (*)(GLenum, GLenum, GLint*)>("glGetBufferParameteriv");
        map_buffer_range_ = Get<void* (*)(GLenum, GLintptr, GLsizeiptr, GLbitfield)>(
            "glMapBufferRange");
        unmap_buffer_ = Get<GLboolean (*)(GLenum)>("glUnmapBuffer");
        vertex_attrib_pointer_ =
            Get<void (*)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*)>(
                "glVertexAttribPointer");
        enable_vertex_attrib_array_ = Get<void (*)(GLuint)>("glEnableVertexAttribArray");
        disable_vertex_attrib_array_ = Get<void (*)(GLuint)>("glDisableVertexAttribArray");
        clear_color_ = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glClearColor");
        clear_ = Get<void (*)(GLbitfield)>("glClear");
        draw_arrays_ = Get<void (*)(GLenum, GLint, GLsizei)>("glDrawArrays");
        begin_ = Get<void (*)(GLenum)>("glBegin");
        end_ = Get<void (*)()>("glEnd");
        color4f_ = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glColor4f");
        vertex2f_ = Get<void (*)(GLfloat, GLfloat)>("glVertex2f");
        bind_texture_ = Get<void (*)(GLenum, GLuint)>("glBindTexture");
        active_texture_ = Get<void (*)(GLenum)>("glActiveTexture");
        get_error_ = Get<GLenum (*)()>("glGetError");
        read_pixels_ =
            Get<void (*)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*)>("glReadPixels");
        ASSERT_NE(read_pixels_, nullptr);
        get_error_();
    }

    GLuint BuildProgram() {
        static const char* vs_src =
            "#version 300 es\n"
            "in vec2 aPos;\n"
            "in vec3 aColor;\n"
            "out vec3 vColor;\n"
            "void main() { vColor = aColor; gl_Position = vec4(aPos, 0.0, 1.0); }\n";
        static const char* fs_src =
            "#version 300 es\n"
            "precision mediump float;\n"
            "in vec3 vColor;\n"
            "out vec4 o;\n"
            "void main() { o = vec4(vColor, 1.0); }\n";
        const GLuint vs = create_shader_(GL_VERTEX_SHADER_);
        shader_source_(vs, 1, &vs_src, nullptr);
        compile_shader_(vs);
        GLint ok = 0;
        get_shaderiv_(vs, GL_COMPILE_STATUS_, &ok);
        if (ok == 0) {
            char log[4096] = {};
            get_shader_info_log_(vs, sizeof log, nullptr, log);
            ADD_FAILURE() << "VS compile:\n" << log;
            return 0;
        }
        const GLuint fs = create_shader_(GL_FRAGMENT_SHADER_);
        shader_source_(fs, 1, &fs_src, nullptr);
        compile_shader_(fs);
        get_shaderiv_(fs, GL_COMPILE_STATUS_, &ok);
        if (ok == 0) {
            char log[4096] = {};
            get_shader_info_log_(fs, sizeof log, nullptr, log);
            ADD_FAILURE() << "FS compile:\n" << log;
            return 0;
        }
        const GLuint program = create_program_();
        attach_shader_(program, vs);
        attach_shader_(program, fs);
        link_program_(program);
        get_programiv_(program, GL_LINK_STATUS_, &ok);
        if (ok == 0) {
            ADD_FAILURE() << "link";
            return 0;
        }
        return program;
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

    // A green full-screen triangle pair, interleaved position+color.
    static const GLfloat* Quad() {
        static const GLfloat k_quad[] = {
            -1.0f, -1.0f, 0.0f, 1.0f, 0.0f, 1.0f,  -1.0f, 0.0f, 1.0f, 0.0f,
            1.0f,  1.0f,  0.0f, 1.0f, 0.0f, -1.0f, -1.0f, 0.0f, 1.0f, 0.0f,
            1.0f,  1.0f,  0.0f, 1.0f, 0.0f, -1.0f, 1.0f,  0.0f, 1.0f, 0.0f,
        };
        return k_quad;
    }
    static constexpr size_t kQuadFloats = 30;
    static constexpr size_t kQuadBytes = kQuadFloats * sizeof(GLfloat);

    // A fixed-function immediate run long enough that the small-run merger
    // declines it, so it draws at glEnd and leaves the wrapper's
    // program/VAO/buffers live. A 4-vertex run would instead sit pending
    // until the next barrier, and that barrier restores right after flushing
    // - which is exactly the window the C2/C3 checks below need to exist in
    // order to police it.
    void FpeDrawUnmerged() {
        begin_(GL_QUADS_);
        color4f_(1.0f, 0.0f, 0.0f, 1.0f);
        for (int i = 0; i < 20; ++i) { // 80 vertices, past the 64-vertex merge limit
            const GLfloat y0 = -0.5f + static_cast<GLfloat>(i) * 0.05f;
            const GLfloat y1 = y0 + 0.05f;
            vertex2f_(-0.5f, y0);
            vertex2f_(0.5f, y0);
            vertex2f_(0.5f, y1);
            vertex2f_(-0.5f, y1);
        }
        end_();
    }

    bool DrawVbo(GLuint program, GLuint vbo, GLint loc_pos, GLint loc_color, const char* tag,
                 int r, int g, int b) {
        clear_color_(0.0f, 0.0f, 0.0f, 1.0f);
        clear_(GL_COLOR_BUFFER_BIT_);
        use_program_(program);
        bind_buffer_(GL_ARRAY_BUFFER_, vbo);
        vertex_attrib_pointer_(static_cast<GLuint>(loc_pos), 2, GL_FLOAT_,
                               sfpew_test::GL_FALSE_, 5 * static_cast<GLsizei>(sizeof(GLfloat)),
                               nullptr);
        vertex_attrib_pointer_(static_cast<GLuint>(loc_color), 3, GL_FLOAT_,
                               sfpew_test::GL_FALSE_, 5 * static_cast<GLsizei>(sizeof(GLfloat)),
                               reinterpret_cast<const void*>(2 * sizeof(GLfloat)));
        enable_vertex_attrib_array_(static_cast<GLuint>(loc_pos));
        enable_vertex_attrib_array_(static_cast<GLuint>(loc_color));
        draw_arrays_(GL_TRIANGLES_, 0, 6);
        return CheckCenter(tag, r, g, b);
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
    GLint (*get_attrib_location_)(GLuint, const GLchar*) = nullptr;
    void (*gen_buffers_)(GLsizei, GLuint*) = nullptr;
    void (*bind_buffer_)(GLenum, GLuint) = nullptr;
    void (*buffer_data_)(GLenum, GLsizeiptr, const void*, GLenum) = nullptr;
    void (*buffer_sub_data_)(GLenum, GLintptr, GLsizeiptr, const void*) = nullptr;
    void (*get_buffer_parameteriv_)(GLenum, GLenum, GLint*) = nullptr;
    void* (*map_buffer_range_)(GLenum, GLintptr, GLsizeiptr, GLbitfield) = nullptr;
    GLboolean (*unmap_buffer_)(GLenum) = nullptr;
    void (*vertex_attrib_pointer_)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*) =
        nullptr;
    void (*enable_vertex_attrib_array_)(GLuint) = nullptr;
    void (*disable_vertex_attrib_array_)(GLuint) = nullptr;
    void (*clear_color_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*clear_)(GLbitfield) = nullptr;
    void (*draw_arrays_)(GLenum, GLint, GLsizei) = nullptr;
    void (*begin_)(GLenum) = nullptr;
    void (*end_)() = nullptr;
    void (*color4f_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*vertex2f_)(GLfloat, GLfloat) = nullptr;
    void (*bind_texture_)(GLenum, GLuint) = nullptr;
    void (*active_texture_)(GLenum) = nullptr;
    GLenum (*get_error_)() = nullptr;
    void (*read_pixels_)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*) = nullptr;
};

TEST_F(VboSurfaceTest, BufferAndAttributeSurfaceRoutesThroughTheWrapper) {
    const GLuint program = BuildProgram();
    ASSERT_NE(program, 0u);
    const GLint loc_pos = get_attrib_location_(program, "aPos");
    const GLint loc_color = get_attrib_location_(program, "aColor");
    ASSERT_GE(loc_pos, 0);
    ASSERT_GE(loc_color, 0);

    GLuint vbo = 0;
    gen_buffers_(1, &vbo);
    ASSERT_NE(vbo, 0u) << "glGenBuffers gave 0";
    bind_buffer_(GL_ARRAY_BUFFER_, vbo);
    buffer_data_(GL_ARRAY_BUFFER_, static_cast<GLsizeiptr>(kQuadBytes), Quad(), GL_STATIC_DRAW_);

    // E: the buffer really holds what glBufferData was given, read back
    // through the wrapper's own query and mapping entry points.
    GLint size = 0;
    get_buffer_parameteriv_(GL_ARRAY_BUFFER_, GL_BUFFER_SIZE_, &size);
    EXPECT_EQ(size, static_cast<GLint>(kQuadBytes)) << "GL_BUFFER_SIZE";
    const GLfloat* mapped = static_cast<const GLfloat*>(map_buffer_range_(
        GL_ARRAY_BUFFER_, 0, static_cast<GLsizeiptr>(kQuadBytes), GL_MAP_READ_BIT_));
    ASSERT_NE(mapped, nullptr) << "glMapBufferRange returned NULL";
    EXPECT_TRUE(std::memcmp(mapped, Quad(), kQuadBytes) == 0)
        << "mapped contents differ from glBufferData input";
    EXPECT_TRUE(unmap_buffer_(GL_ARRAY_BUFFER_)) << "glUnmapBuffer";

    // A: the VBO path renders at all.
    ASSERT_TRUE(DrawVbo(program, vbo, loc_pos, loc_color,
                        "A: VBO + attributes through user program", 0, 255, 0));

    // B: a fixed-function immediate draw in between. This is the wrapper
    // binding its own program, VAO and ring buffer; by the time glEnd returns
    // the app's must be back, or C below renders wrong.
    use_program_(0);
    clear_color_(0.0f, 0.0f, 0.0f, 1.0f);
    clear_(GL_COLOR_BUFFER_BIT_);
    begin_(GL_QUADS_);
    color4f_(1.0f, 0.0f, 0.0f, 1.0f);
    vertex2f_(-1.0f, -1.0f);
    vertex2f_(1.0f, -1.0f);
    vertex2f_(1.0f, 1.0f);
    vertex2f_(-1.0f, 1.0f);
    end_();
    ASSERT_TRUE(CheckCenter("B: fixed-function immediate quad between VBO draws", 255, 0, 0));

    // C: the same VBO draw as A, with no re-upload. Its buffer contents and
    // attribute pointers have to have survived B untouched.
    ASSERT_TRUE(
        DrawVbo(program, vbo, loc_pos, loc_color, "C: VBO draw unchanged after the FPE draw", 0,
                255, 0));

    // C2: the one that actually polices the draw guard. Everything is set up
    // BEFORE the fixed-function draw and deliberately NOT re-established
    // after it, so the draw below runs on whatever program, array buffer and
    // VAO-0 attribute state the wrapper left behind. C above cannot catch a
    // leak because DrawVbo re-binds all three itself.
    use_program_(program);
    bind_buffer_(GL_ARRAY_BUFFER_, vbo);
    vertex_attrib_pointer_(static_cast<GLuint>(loc_pos), 2, GL_FLOAT_, sfpew_test::GL_FALSE_,
                           5 * static_cast<GLsizei>(sizeof(GLfloat)), nullptr);
    vertex_attrib_pointer_(static_cast<GLuint>(loc_color), 3, GL_FLOAT_, sfpew_test::GL_FALSE_,
                           5 * static_cast<GLsizei>(sizeof(GLfloat)),
                           reinterpret_cast<const void*>(2 * sizeof(GLfloat)));
    enable_vertex_attrib_array_(static_cast<GLuint>(loc_pos));
    enable_vertex_attrib_array_(static_cast<GLuint>(loc_color));
    FpeDrawUnmerged(); // wrapper binds its own program / VAO / ring buffer here
    clear_color_(0.0f, 0.0f, 0.0f, 1.0f);
    clear_(GL_COLOR_BUFFER_BIT_);
    draw_arrays_(GL_TRIANGLES_, 0, 6); // no re-bind: relies on the state above
    ASSERT_TRUE(CheckCenter("C2: pre-FPE program/buffer/attribute state survived glEnd", 0, 255,
                            0));

    // C3: same as C2, but a texture bind sits between the fixed-function draw
    // and the app's draw. glBindTexture deliberately does NOT hand the app's
    // program/VAO/buffers back (it cannot observe them, and restoring there
    // cost a full save/restore cycle per texture switch - plans/12), so the
    // wrapper's state stays live across it. glDrawArrays' own barrier is then
    // the only thing standing between that and the app drawing with the
    // wrapper's program.
    use_program_(program);
    bind_buffer_(GL_ARRAY_BUFFER_, vbo);
    vertex_attrib_pointer_(static_cast<GLuint>(loc_pos), 2, GL_FLOAT_, sfpew_test::GL_FALSE_,
                           5 * static_cast<GLsizei>(sizeof(GLfloat)), nullptr);
    vertex_attrib_pointer_(static_cast<GLuint>(loc_color), 3, GL_FLOAT_, sfpew_test::GL_FALSE_,
                           5 * static_cast<GLsizei>(sizeof(GLfloat)),
                           reinterpret_cast<const void*>(2 * sizeof(GLfloat)));
    enable_vertex_attrib_array_(static_cast<GLuint>(loc_pos));
    enable_vertex_attrib_array_(static_cast<GLuint>(loc_color));
    // The clear goes BEFORE the fixed-function draw on purpose: glClearColor
    // and glClear both fire the full barrier, so placing them after the
    // texture bind would restore the app's state and this check could never
    // fail. glDrawArrays' own barrier is then the only restore left in the
    // sequence.
    clear_color_(0.0f, 0.0f, 0.0f, 1.0f);
    clear_(GL_COLOR_BUFFER_BIT_);
    FpeDrawUnmerged();
    bind_texture_(GL_TEXTURE_2D_, 0); // the narrowed barrier: deliberately no restore
    active_texture_(GL_TEXTURE0_);
    draw_arrays_(GL_TRIANGLES_, 0, 6); // still no re-bind
    ASSERT_TRUE(CheckCenter("C3: app state survived an FPE draw followed by a texture bind", 0,
                            255, 0));

    // D: glBufferSubData through the wrapper repaints the quad blue, proving
    // writes land in the app's buffer and not somewhere the wrapper owns.
    GLfloat blue[kQuadFloats];
    std::memcpy(blue, Quad(), kQuadBytes);
    for (size_t v = 0; v < 6; ++v) {
        blue[v * 5 + 2] = 0.0f;
        blue[v * 5 + 3] = 0.0f;
        blue[v * 5 + 4] = 1.0f;
    }
    bind_buffer_(GL_ARRAY_BUFFER_, vbo);
    buffer_sub_data_(GL_ARRAY_BUFFER_, 0, static_cast<GLsizeiptr>(sizeof blue), blue);
    ASSERT_TRUE(DrawVbo(program, vbo, loc_pos, loc_color,
                        "D: glBufferSubData recolored the app's buffer", 0, 0, 255));

    disable_vertex_attrib_array_(static_cast<GLuint>(loc_pos));
    disable_vertex_attrib_array_(static_cast<GLuint>(loc_color));
    bind_buffer_(GL_ARRAY_BUFFER_, 0);
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

} // namespace
