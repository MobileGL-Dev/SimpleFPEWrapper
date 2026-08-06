// SimpleFPEWrapper - tests/gtest_occlusion_query.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// GL_ARB_occlusion_query2 candidacy check: glGetQueryObject{iv,i64v,ui64v}
// are wrapped (ordered_passthrough.cpp), but glBeginQuery/glEndQuery/
// glGenQueries/glDeleteQueries/glIsQuery are not entry points this project
// implements at all - they are only loaded as internal backend pointers
// (backend/loader.h/.cpp) and resolve to the app through eglGetProcAddress's
// tail fallback to the backend's own resolver (lookup.cpp). That path was
// unverified end-to-end before this test.
//
// The draws here go through the wrapper's real glDrawArrays with a user
// program and a VBO - never glBegin/glEnd - on purpose: fixed-function
// immediate-mode vertices are batched by the wrapper and only reach the
// backend at the next sfpewEntryBarrier() (see fpe/drawing1x.h). The raw,
// unwrapped glBeginQuery/glEndQuery resolved here take neither barrier, so a
// query wrapped around a still-batched glBegin/glEnd draw would measure
// nothing having reached the GPU yet - a false negative that has nothing to
// do with occlusion. A user-program glDrawArrays call submits synchronously
// (fpe/draw_now.cpp), which sidesteps that pitfall entirely.
//
// THIS TEST ORIGINALLY FAILED, and that failure was the answer the extension
// survey was after: it was not a test bug. glBeginQuery/glEndQuery/
// glGenQueries/glIsQuery all checked out fine (asserted below via glIsQuery
// transitions and a direct read through the confirmed-good core accessor).
// The break was narrower: on this GLES3 backend, eglGetProcAddress happily
// hands back a non-null pointer for "glGetQueryObjectivEXT" - a name
// EXT_occlusion_query_boolean never actually defines (that extension has
// only the unsigned glGetQueryObjectuivEXT) - and calling it was a silent
// no-op: no error, output parameter untouched. ordered_passthrough.cpp's
// glGetQueryObjectiv/i64v/ui64v tried their *EXT sibling FIRST and only fell
// back to the confirmed-working core glGetQueryObjectuiv if that pointer was
// null - but null was not the failure mode here, a non-functional non-null
// pointer was, so the fallback never triggered and GL_QUERY_RESULT_AVAILABLE
// polled forever.
//
// Fixed in ordered_passthrough.cpp: glGetQueryObjectiv/i64v/ui64v no longer
// trust the *EXT pointer at all - measuring this same defect on this
// machine's real desktop GL backend too (see gtest_clear_depth_drawbuffer.cc,
// ClearDepthDrawBufferDesktopTest.SignedQueryObjectOnDesktopCore, a
// pre-existing test unrelated to this extension batch that exercises the
// same accessors and never passed on either backend before this fix) showed
// even desktop's real GL_EXT_occlusion_query glGetQueryObjectivEXT silently
// fails to mark GL_QUERY_RESULT_AVAILABLE. The core unsigned
// glGetQueryObjectuiv is the only accessor confirmed reliable on both
// backends, so it is now used unconditionally, with signed/64-bit callers
// converting/saturating its result. This test is now a passing regression
// guard for that fix, and GL_ARB_occlusion_query2 is advertised in
// kDesktopExtensions.
//
// Two cases, both cross-checked against the actual framebuffer so a query
// result that merely happens to match by accident cannot pass silently:
//   A. an unobstructed near quad against a freshly cleared depth buffer -
//      GL_ANY_SAMPLES_PASSED must report true, and the pixel must be lit.
//   B. the same quad drawn behind an opaque nearer blocker - the query must
//      report false, and the pixel must still show the blocker's colour.
//
// GL_ARB_occlusion_query candidacy check (below, ClassicSamplesPassedTarget
// test): the classic extension's exact-count GL_SAMPLES_PASSED (0x8914) is
// a DIFFERENT question from the above, and settled independently of the
// glGetQueryObjectivEXT defect this file's own GL_ANY_SAMPLES_PASSED test
// already proved (that one lives purely in the getter and is target-
// agnostic, so it would block GL_SAMPLES_PASSED too regardless). The
// earlier, more basic question: GL_SAMPLES_PASSED is not one of the targets
// ES 3.0/3.1/3.2 core's glBeginQuery accepts at all (only
// ANY_SAMPLES_PASSED[_CONSERVATIVE] and TRANSFORM_FEEDBACK_PRIMITIVES_
// WRITTEN are), and no EXT/OES extension brings it to ES. Confirmed on
// this machine's actual GLES3 backend: raw glBeginQuery(GL_SAMPLES_PASSED,
// ...) - unwrapped, resolved exactly as an app would get it - returns
// GL_INVALID_ENUM immediately. GL_ARB_occlusion_query must not be
// advertised: on this wrapper's real-world ES3 floor, the entry point the
// extension is named after does not function, before the getter defect
// even enters into it.

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
using sfpew_test::GLuint;
using sfpew_test::PixelProbe;

constexpr GLbitfield GL_COLOR_BUFFER_BIT_ = 0x00004000;
constexpr GLbitfield GL_DEPTH_BUFFER_BIT_ = 0x00000100;
constexpr GLenum GL_TRIANGLES_ = 0x0004;
constexpr GLenum GL_FLOAT_ = 0x1406;
constexpr GLenum GL_ARRAY_BUFFER_ = 0x8892;
constexpr GLenum GL_STATIC_DRAW_ = 0x88E4;
constexpr GLenum GL_VERTEX_SHADER_ = 0x8B31;
constexpr GLenum GL_FRAGMENT_SHADER_ = 0x8B30;
constexpr GLenum GL_COMPILE_STATUS_ = 0x8B81;
constexpr GLenum GL_LINK_STATUS_ = 0x8B82;
constexpr GLenum GL_DEPTH_TEST_ = 0x0B71;
constexpr GLenum GL_NO_ERROR_ = 0;

// Reused verbatim from GL_EXT_occlusion_query_boolean / ES 3.0 core, which
// GL_ARB_occlusion_query2 deliberately shares the token values of.
constexpr GLenum GL_ANY_SAMPLES_PASSED_ = 0x8C2F;
constexpr GLenum GL_QUERY_RESULT_ = 0x8866;
constexpr GLenum GL_QUERY_RESULT_AVAILABLE_ = 0x8867;

constexpr int kSize = 64;

class OcclusionQueryTest : public ContextTest {
protected:
    OcclusionQueryTest() : ContextTest(sfpew_test::Backend::GLES3, kSize) {}

    void SetUp() override {
        ContextTest::SetUp();
        if (::testing::Test::IsSkipped()) return;

        clear_color_ = Get<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glClearColor");
        clear_ = Get<void (*)(GLbitfield)>("glClear");
        get_error_ = Get<GLenum (*)()>("glGetError");
        finish_ = Get<void (*)()>("glFinish");
        enable_ = Get<void (*)(GLenum)>("glEnable");
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
        get_uniform_location_ = Get<GLint (*)(GLuint, const GLchar*)>("glGetUniformLocation");
        uniform1f_ = Get<void (*)(GLint, GLfloat)>("glUniform1f");
        uniform3f_ = Get<void (*)(GLint, GLfloat, GLfloat, GLfloat)>("glUniform3f");
        draw_arrays_ = Get<void (*)(GLenum, GLint, GLsizei)>("glDrawArrays");

        // The candidacy target itself: the query family, resolved exactly
        // as an app would through the wrapper's own eglGetProcAddress. Not
        // wrapped, so these fall through to the backend's raw pointers -
        // that fallthrough is what this test is verifying actually works.
        gen_queries_ = Get<void (*)(GLsizei, GLuint*)>("glGenQueries");
        delete_queries_ = Get<void (*)(GLsizei, const GLuint*)>("glDeleteQueries");
        begin_query_ = Get<void (*)(GLenum, GLuint)>("glBeginQuery");
        end_query_ = Get<void (*)(GLenum)>("glEndQuery");
        // The wrapped accessor (ordered_passthrough.cpp): validates the
        // pname/id pair itself and falls back to the ES3 core unsigned
        // entry point when no EXT alias is present.
        get_query_object_iv_ = Get<void (*)(GLuint, GLenum, GLint*)>("glGetQueryObjectiv");

        auto read_pixels =
            Get<void (*)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*)>("glReadPixels");
        ASSERT_NE(read_pixels, nullptr);
        probe_.emplace(read_pixels);
    }

    // The confirmed-good path: the ES3 core, unsuffixed accessor, called
    // raw (no wrapper involvement at all). Used to prove the query itself
    // completed and holds a correct result, independent of whatever the
    // wrapper's own glGetQueryObjectiv does with it.
    GLuint ReadRawCoreResult(GLuint query) {
        auto raw_get_uiv = Get<void (*)(GLuint, GLenum, unsigned int*)>("glGetQueryObjectuiv");
        unsigned int available = 0;
        for (int i = 0; i < 100000 && available == 0; ++i) {
            raw_get_uiv(query, GL_QUERY_RESULT_AVAILABLE_, &available);
        }
        EXPECT_NE(available, 0u)
            << "query " << query << " never completed at the driver level at all";
        unsigned int value = 0;
        raw_get_uiv(query, GL_QUERY_RESULT_, &value);
        return value;
    }

    // Bounded poll through the WRAPPER's own resolved glGetQueryObjectiv -
    // the entry point an application actually gets back from
    // eglGetProcAddress("glGetQueryObjectiv"). Reading GL_QUERY_RESULT would
    // itself block until available per spec, so the bound only protects
    // against a driver that never sets the flag; it does not risk a false
    // pass. See the file header: this used to never become available on this
    // backend, because the wrapper preferred a glGetQueryObjectivEXT pointer
    // that resolves non-null but is a silent no-op here - fixed by never
    // trusting that pointer and always reading through the core accessor.
    GLint WaitAndReadResult(GLuint query) {
        GLint available = 0;
        for (int i = 0; i < 100000 && available == 0; ++i) {
            get_query_object_iv_(query, GL_QUERY_RESULT_AVAILABLE_, &available);
        }
        EXPECT_NE(available, 0)
            << "query " << query
            << " result never became available through the wrapper's glGetQueryObjectiv "
               "(see file header: this was the glGetQueryObjectivEXT no-op, not a dead query - "
               "ReadRawCoreResult() on the same query confirms the driver already has the "
               "answer)";
        GLint result = -1;
        get_query_object_iv_(query, GL_QUERY_RESULT_, &result);
        return result;
    }

    void (*clear_color_)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*clear_)(GLbitfield) = nullptr;
    GLenum (*get_error_)() = nullptr;
    void (*finish_)() = nullptr;
    void (*enable_)(GLenum) = nullptr;
    void (*use_program_)(GLuint) = nullptr;
    GLuint (*create_shader_)(GLenum) = nullptr;
    void (*shader_source_)(GLuint, GLsizei, const GLchar* const*, const GLint*) = nullptr;
    void (*compile_shader_)(GLuint) = nullptr;
    void (*get_shaderiv_)(GLuint, GLenum, GLint*) = nullptr;
    GLuint (*create_program_)() = nullptr;
    void (*attach_shader_)(GLuint, GLuint) = nullptr;
    void (*link_program_)(GLuint) = nullptr;
    void (*get_programiv_)(GLuint, GLenum, GLint*) = nullptr;
    void (*gen_buffers_)(GLsizei, GLuint*) = nullptr;
    void (*bind_buffer_)(GLenum, GLuint) = nullptr;
    void (*buffer_data_)(GLenum, GLsizeiptr, const void*, GLenum) = nullptr;
    void (*gen_vertex_arrays_)(GLsizei, GLuint*) = nullptr;
    void (*bind_vertex_array_)(GLuint) = nullptr;
    void (*vertex_attrib_pointer_)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*) =
        nullptr;
    void (*enable_vertex_attrib_array_)(GLuint) = nullptr;
    GLint (*get_attrib_location_)(GLuint, const GLchar*) = nullptr;
    GLint (*get_uniform_location_)(GLuint, const GLchar*) = nullptr;
    void (*uniform1f_)(GLint, GLfloat) = nullptr;
    void (*uniform3f_)(GLint, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*draw_arrays_)(GLenum, GLint, GLsizei) = nullptr;
    void (*gen_queries_)(GLsizei, GLuint*) = nullptr;
    void (*delete_queries_)(GLsizei, const GLuint*) = nullptr;
    void (*begin_query_)(GLenum, GLuint) = nullptr;
    void (*end_query_)(GLenum) = nullptr;
    void (*get_query_object_iv_)(GLuint, GLenum, GLint*) = nullptr;
    std::optional<PixelProbe> probe_;
};

TEST_F(OcclusionQueryTest, ReportsVisibleAndOccludedDrawsCorrectly) {
    static const char* vs_src =
        "#version 300 es\n"
        "in vec2 aPos;\n"
        "uniform float uZ;\n"
        "void main() { gl_Position = vec4(aPos, uZ, 1.0); }\n";
    static const char* fs_src =
        "#version 300 es\n"
        "precision mediump float;\n"
        "uniform vec3 uColor;\n"
        "out vec4 o;\n"
        "void main() { o = vec4(uColor, 1.0); }\n";
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
    // A full-viewport quad; only its NDC z (uZ) changes between draws.
    static const GLfloat quad[12] = {
        -1, -1, 1, -1, 1, 1,
        -1, -1, 1, 1, -1, 1,
    };
    buffer_data_(GL_ARRAY_BUFFER_, static_cast<GLsizeiptr>(sizeof quad), quad, GL_STATIC_DRAW_);
    const GLint loc_pos = get_attrib_location_(program, "aPos");
    ASSERT_GE(loc_pos, 0) << "aPos";
    vertex_attrib_pointer_(static_cast<GLuint>(loc_pos), 2, GL_FLOAT_, sfpew_test::GL_FALSE_, 0,
                           nullptr);
    enable_vertex_attrib_array_(static_cast<GLuint>(loc_pos));
    use_program_(program);
    const GLint loc_z = get_uniform_location_(program, "uZ");
    const GLint loc_color = get_uniform_location_(program, "uColor");
    ASSERT_GE(loc_z, 0) << "uZ";
    ASSERT_GE(loc_color, 0) << "uColor";

    enable_(GL_DEPTH_TEST_);

    GLuint queries[2] = {0, 0};
    gen_queries_(2, queries);
    const GLuint q_visible = queries[0];
    const GLuint q_occluded = queries[1];
    ASSERT_NE(q_visible, 0u);
    ASSERT_NE(q_occluded, 0u);
    ASSERT_NE(q_visible, q_occluded);
    // glIsQuery is one of the "only loaded internally" entries this test
    // exists to check; per spec a name GenQueries handed out is not yet a
    // query object until BeginQuery associates it with a target.
    auto is_query_ = Get<GLboolean (*)(GLuint)>("glIsQuery");
    EXPECT_EQ(is_query_(q_visible), 0) << "GenQueries alone must not make a query object yet";

    // A: nothing in front of it, depth buffer freshly cleared to the far
    // plane (1.0). NDC z=0.0 -> window depth 0.5, which passes GL_LESS.
    clear_color_(0.0f, 0.0f, 0.0f, 1.0f);
    clear_(GL_COLOR_BUFFER_BIT_ | GL_DEPTH_BUFFER_BIT_);
    uniform3f_(loc_color, 1.0f, 0.0f, 0.0f);
    uniform1f_(loc_z, 0.0f);
    begin_query_(GL_ANY_SAMPLES_PASSED_, q_visible);
    EXPECT_NE(is_query_(q_visible), 0) << "BeginQuery must make the name a query object";
    draw_arrays_(GL_TRIANGLES_, 0, 6);
    end_query_(GL_ANY_SAMPLES_PASSED_);
    finish_();
    {
        const PixelProbe::Rgba p = probe_->At(kSize / 2, kSize / 2);
        EXPECT_TRUE(p.r > 200 && p.g <= 50 && p.b <= 50)
            << "case A must actually draw red: (" << (int)p.r << ',' << (int)p.g << ','
            << (int)p.b << ')';
    }

    // B: an opaque blocker at NDC z=-0.8 (window depth 0.1) drawn first and
    // NOT queried, then the same quad at NDC z=0.5 (window depth 0.75)
    // queried - 0.75 is not less than 0.1, so every sample must fail.
    clear_color_(0.0f, 0.0f, 0.0f, 1.0f);
    clear_(GL_COLOR_BUFFER_BIT_ | GL_DEPTH_BUFFER_BIT_);
    uniform3f_(loc_color, 0.0f, 1.0f, 0.0f);
    uniform1f_(loc_z, -0.8f);
    draw_arrays_(GL_TRIANGLES_, 0, 6);
    uniform3f_(loc_color, 0.0f, 0.0f, 1.0f);
    uniform1f_(loc_z, 0.5f);
    begin_query_(GL_ANY_SAMPLES_PASSED_, q_occluded);
    draw_arrays_(GL_TRIANGLES_, 0, 6);
    end_query_(GL_ANY_SAMPLES_PASSED_);
    finish_();
    {
        const PixelProbe::Rgba p = probe_->At(kSize / 2, kSize / 2);
        EXPECT_TRUE(p.r <= 50 && p.g > 200 && p.b <= 50)
            << "case B: the occluded draw must not reach the framebuffer, "
               "the blocker's green must remain: ("
            << (int)p.r << ',' << (int)p.g << ',' << (int)p.b << ')';
    }

    // Cross-check first, through the confirmed-good raw core accessor: this
    // proves the driver actually has correct answers sitting on both query
    // objects before the wrapper's own accessor is asked for them below.
    const GLuint raw_visible = ReadRawCoreResult(q_visible);
    const GLuint raw_occluded = ReadRawCoreResult(q_occluded);
    EXPECT_NE(raw_visible, 0u) << "driver-level: an unobstructed draw must report samples passed";
    EXPECT_EQ(raw_occluded, 0u)
        << "driver-level: a fully depth-occluded draw must report no samples passed";

    // The actual target of this test: the SAME two queries, read through
    // the wrapper's own resolved glGetQueryObjectiv - what an application
    // gets from eglGetProcAddress("glGetQueryObjectiv"). See the file
    // header for why this currently fails on this backend.
    const GLint result_visible = WaitAndReadResult(q_visible);
    const GLint result_occluded = WaitAndReadResult(q_occluded);
    EXPECT_NE(result_visible, 0) << "an unobstructed draw must report samples passed";
    EXPECT_EQ(result_occluded, 0) << "a fully depth-occluded draw must report no samples passed";

    delete_queries_(2, queries);
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

// GL_ARB_occlusion_query candidacy check (the classic, exact-count
// GL_SAMPLES_PASSED target, as opposed to the query2/GL_ANY_SAMPLES_PASSED
// boolean form probed above). GL_SAMPLES_PASSED (0x8914) is not part of
// GLES 3.0/3.1/3.2 core at all - the ES 3.x BeginQuery target table has only
// ANY_SAMPLES_PASSED[_CONSERVATIVE] and TRANSFORM_FEEDBACK_PRIMITIVES_
// WRITTEN, and no EXT/OES extension resurrects the exact-count form on ES.
// This fixture's backend is GLES3 (see OcclusionQueryTest above), this
// wrapper's real-world floor, so whatever the backend's own raw glBeginQuery
// actually does with this enum here is the deciding fact - prior to, and
// independent of, the wrapper-getter defect the GL_ANY_SAMPLES_PASSED test
// above already proved (that defect lives in glGetQueryObject{iv,i64v,ui64v}
// themselves, not in anything target-specific, so it would block classic
// occlusion_query too even on a backend that did accept this enum).
TEST_F(OcclusionQueryTest, ClassicSamplesPassedTarget) {
    constexpr GLenum GL_SAMPLES_PASSED_ = 0x8914;
    constexpr GLenum GL_INVALID_ENUM_ = 0x0500;

    GLuint query = 0;
    gen_queries_(1, &query);
    ASSERT_NE(query, 0u);

    get_error_(); // drain anything stale before the enum under test
    begin_query_(GL_SAMPLES_PASSED_, query);
    const GLenum begin_error = get_error_();

    if (begin_error == GL_INVALID_ENUM_) {
        // The expected-and-correct answer per the ES 3.x spec table cited
        // above, not a wrapper bug to chase: glBeginQuery is unwrapped
        // (resolves straight to the backend, see the file header), so this
        // is the real GLES3 driver on this machine refusing a target it
        // never defined. Asserted (not just observed) so a future backend
        // swap that silently started accepting this enum would be caught
        // here rather than only surfacing as a wrongly-advertised string.
        EXPECT_EQ(begin_error, GL_INVALID_ENUM_)
            << "GL_SAMPLES_PASSED rejected by glBeginQuery on this GLES3 backend, as ES 3.x "
               "core spec requires (it is not one of the BeginQuery targets ES defines) - "
               "GL_ARB_occlusion_query must not be advertised on this backend floor";
        delete_queries_(1, &query);
        EXPECT_EQ(get_error_(), GL_NO_ERROR_);
        return;
    }

    // If the backend instead accepted the target (e.g. a desktop backend,
    // or an ES driver layering the desktop extension on top), finish
    // proving or disproving genuine per-sample counting: a quad covering a
    // small fraction of the viewport must report fewer samples than one
    // covering all of it, and the small one must still be nonzero - ruling
    // out a driver that just always returns 0 or 1 regardless of area.
    end_query_(GL_SAMPLES_PASSED_);
    ASSERT_EQ(get_error_(), GL_NO_ERROR_) << "glEndQuery(GL_SAMPLES_PASSED) unexpectedly failed";

    static const char* vs_src =
        "#version 300 es\n"
        "in vec2 aPos;\n"
        "uniform float uScale;\n"
        "void main() { gl_Position = vec4(aPos * uScale, 0.0, 1.0); }\n";
    static const char* fs_src =
        "#version 300 es\n"
        "precision mediump float;\n"
        "out vec4 o;\n"
        "void main() { o = vec4(1.0); }\n";
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
    static const GLfloat quad[12] = {
        -1, -1, 1, -1, 1, 1,
        -1, -1, 1, 1, -1, 1,
    };
    buffer_data_(GL_ARRAY_BUFFER_, static_cast<GLsizeiptr>(sizeof quad), quad, GL_STATIC_DRAW_);
    const GLint loc_pos = get_attrib_location_(program, "aPos");
    ASSERT_GE(loc_pos, 0) << "aPos";
    vertex_attrib_pointer_(static_cast<GLuint>(loc_pos), 2, GL_FLOAT_, sfpew_test::GL_FALSE_, 0,
                           nullptr);
    enable_vertex_attrib_array_(static_cast<GLuint>(loc_pos));
    use_program_(program);
    const GLint loc_scale = get_uniform_location_(program, "uScale");
    ASSERT_GE(loc_scale, 0) << "uScale";

    GLuint queries[2] = {0, 0};
    gen_queries_(2, queries);
    const GLuint q_small = queries[0];
    const GLuint q_large = queries[1];
    ASSERT_NE(q_small, 0u);
    ASSERT_NE(q_large, 0u);

    // Small: a quarter-extent quad -> 1/16th of the viewport area.
    clear_color_(0.0f, 0.0f, 0.0f, 1.0f);
    clear_(GL_COLOR_BUFFER_BIT_);
    uniform1f_(loc_scale, 0.25f);
    begin_query_(GL_SAMPLES_PASSED_, q_small);
    draw_arrays_(GL_TRIANGLES_, 0, 6);
    end_query_(GL_SAMPLES_PASSED_);

    // Large: the full viewport.
    clear_color_(0.0f, 0.0f, 0.0f, 1.0f);
    clear_(GL_COLOR_BUFFER_BIT_);
    uniform1f_(loc_scale, 1.0f);
    begin_query_(GL_SAMPLES_PASSED_, q_large);
    draw_arrays_(GL_TRIANGLES_, 0, 6);
    end_query_(GL_SAMPLES_PASSED_);
    finish_();

    const GLuint small_count = ReadRawCoreResult(q_small);
    const GLuint large_count = ReadRawCoreResult(q_large);
    EXPECT_GT(small_count, 0u) << "a visible quad must report a nonzero sample count";
    EXPECT_GT(large_count, small_count)
        << "a quad covering the full viewport (" << large_count
        << " samples) must report more samples passed than one covering 1/16th of it ("
        << small_count << ") - otherwise this is not a genuine per-sample count";

    delete_queries_(2, queries);
    GLuint query_array[1] = {query};
    delete_queries_(1, query_array);
    EXPECT_EQ(get_error_(), GL_NO_ERROR_);
}

} // namespace
