// SimpleFPEWrapper - SimpleFPEWrapper/fpe/types.h
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

#pragma once

#include <GL/gl.h>
#include "defines.h"
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <unordered_map>
#include <vector>
#include <memory>
#include <cstddef>
#include "fpe_shadergen.h"
#include "vertexpointer_utils.h"
#include <xxhash64.h>

GLsizei type_size(GLenum type);

struct transformation_t {
    glm::mat4 matrices[4] = {
        glm::mat4(1.0f),
        glm::mat4(1.0f),
        glm::mat4(1.0f),
        glm::mat4(1.0f),
    };
    std::vector<glm::mat4> matrices_stack[4];
    glm::mat4 texture_matrices[MAX_TEX] = {
        glm::mat4(1.0f), glm::mat4(1.0f), glm::mat4(1.0f), glm::mat4(1.0f),
        glm::mat4(1.0f), glm::mat4(1.0f), glm::mat4(1.0f), glm::mat4(1.0f),
        glm::mat4(1.0f), glm::mat4(1.0f), glm::mat4(1.0f), glm::mat4(1.0f),
        glm::mat4(1.0f), glm::mat4(1.0f), glm::mat4(1.0f), glm::mat4(1.0f),
    };
    std::vector<glm::mat4> texture_matrices_stack[MAX_TEX];
    GLenum matrix_mode = GL_MODELVIEW;
};

struct vertexattribute_t {
    GLint size;
    GLenum usage;
    GLenum type;
    GLenum normalized;
    GLsizei stride;
    const void* pointer;
    //    glm::vec4 value;
    //    bool varying = true;
};

#define VERTEX_POINTER_COUNT (7 + MAX_TEX)
struct vertex_pointer_array_t {
    const void* starting_pointer = NULL;
    GLsizei stride = 0;

    struct vertexattribute_t attributes[VERTEX_POINTER_COUNT];
    GLuint compressed_index[VERTEX_POINTER_COUNT];
    uint32_t enabled_pointers = 0;
    bool dirty = false;
    bool buffer_based = false;

    void reset();

    // Split into starting pointer & offset into buffer per pointer
    vertex_pointer_array_t normalize();

    void generate_compressed_index(GLint constant_sizes[VERTEX_POINTER_COUNT]);

    // Get compressed index
    inline GLuint cidx(int i) const { return compressed_index[i]; }
};

struct fixed_function_bool_t {      // glEnable/glDisable
    bool fog_enable = false;        // GL_FOG
    bool lighting_enable = false;   // GL_LIGHTING
    bool alpha_test_enable = false; // GL_ALPHA_TEST
    bool color_material_enable = false;
    bool normalize_enable = false;
    bool rescale_normal_enable = false;
    bool light_enable[MAX_LIGHTS] = {false};
    bool texture_2d_enable[MAX_TEX] = {false};
    // Stored for the plans/08 shader consumption; participates in the
    // program hash via the aggregate but is not read by the generator yet.
    bool clip_plane_enable[6] = {false};
    bool polygon_stipple_enable = false;
    bool line_stipple_enable = false;
    // glEnable(GL_TEXTURE_GEN_S/T/R/Q) per unit; [unit][coord], coord order
    // S,T,R,Q. Shader consumption is the second half of plans/05 5.2.
    bool texture_gen_enable[MAX_TEX][4] = {};
};

struct light_t {
    glm::vec4 ambient = {0, 0, 0, 1};
    glm::vec4 diffuse = {1, 1, 1, 1};
    glm::vec4 specular = {0, 0, 0, 1};
    glm::vec4 position = {0, 0, 1, 0};
    GLfloat constant_attenuation = 1.;
    GLfloat linear_attenuation = 0.;
    GLfloat quadratic_attenuation = 0.;
    glm::vec3 spot_direction = {0, 0, -1};
    GLfloat spot_exp = 0.;
    GLfloat spot_cutoff = 180.; // 0-90, 180
};

struct material_t {
    glm::vec4 ambient = {0.2f, 0.2f, 0.2f, 1.0f};
    glm::vec4 diffuse = {0.8f, 0.8f, 0.8f, 1.0f};
    glm::vec4 specular = {0.0f, 0.0f, 0.0f, 1.0f};
    glm::vec4 emission = {0.0f, 0.0f, 0.0f, 1.0f};
    glm::vec3 color_indexes = {0.0f, 1.0f, 1.0f};
    GLfloat shininess = 0.0f;
};

struct texture_env_t {
    GLenum mode = GL_MODULATE;
    glm::vec4 color = {0.0f, 0.0f, 0.0f, 0.0f};

    GLenum combine_rgb = GL_MODULATE;
    GLenum combine_alpha = GL_MODULATE;
    GLenum source_rgb[3] = {GL_TEXTURE, GL_PREVIOUS, GL_CONSTANT};
    GLenum source_alpha[3] = {GL_TEXTURE, GL_PREVIOUS, GL_CONSTANT};
    GLenum operand_rgb[3] = {GL_SRC_COLOR, GL_SRC_COLOR, GL_SRC_ALPHA};
    GLenum operand_alpha[3] = {GL_SRC_ALPHA, GL_SRC_ALPHA, GL_SRC_ALPHA};
    GLfloat rgb_scale = 1.0f;
    GLfloat alpha_scale = 1.0f;
    GLfloat lod_bias = 0.0f;
};

// size = 0 means disabled
struct fixed_function_draw_size_t {
    union {
        struct {
            GLint vertex_size = 0;
            GLint normal_size = 0;
            GLint color_size = 0;
            GLint index_size = 0;
            GLint edge_size = 0;
            GLint fog_size = 0;
            GLint secondary_color_size = 0;
            GLint texcoord_size[MAX_TEX] = {0};
        };
        GLint data[VERTEX_POINTER_COUNT];
    };
};

static_assert(offsetof(fixed_function_draw_size_t, texcoord_size) == 7 * sizeof(GLint),
              "texture coordinate sizes must start at vertex attribute slot 7");
static_assert(sizeof(fixed_function_draw_size_t) == VERTEX_POINTER_COUNT * sizeof(GLint),
              "fixed-function size layout must match the vertex attribute slots");

struct fixed_function_draw_data_t {
    glm::vec4 vertex = {0, 0, 0, 1};
    glm::vec3 normal = {0, 0, 1};
    glm::vec4 color = {1, 1, 1, 1};
    glm::vec4 texcoord[MAX_TEX];

    fixed_function_draw_size_t sizes;
};

struct fixed_function_draw_state_t {
    GLenum primitive = GL_NONE;

    fixed_function_draw_data_t current_data;

    // Immediate-mode attributes are normalized to GLfloat before they reach
    // this buffer. Keep the storage alive across glBegin/glEnd pairs so small
    // legacy draws can reuse their allocation instead of constructing a stream
    // and copying its full string again at glEnd.
    std::vector<GLfloat> vb;

    size_t vertex_count = 0;

    void reset();

    // Put one vertex into vb, from current draw state
    void advance();

    // Declare attribute `slot` as size `requested` for the primitive being
    // collected. First attribute use after vertices were already collected
    // repacks vb so every vertex shares one layout (GL 1.x allows e.g. the
    // first glColor* after the first glVertex*). Grow-only within a
    // primitive; call BEFORE overwriting the attribute's current value -
    // already-collected vertices are backfilled with the OLD current value.
    void set_attribute_size(int slot, GLint requested);

    void compile_vertexattrib(vertex_pointer_array_t& va) const;
};

struct fixed_function_state_t {
    GLenum client_active_texture = GL_TEXTURE0;      // glClientActiveTexture, specifies active texcood
    GLenum alpha_func = GL_ALWAYS;                   // glAlphaFunc
    GLenum fog_mode = GL_EXP;                        // glFogi(GL_FOG_MODE)
    GLint fog_index = 0;                             // glFogi(GL_FOG_INDEX)
    GLenum fog_coord_src = GL_FRAGMENT_DEPTH;        // glFogi(GL_FOG_COORD_SRC)
    GLenum shade_model = GL_SMOOTH;                  // glShadeModel
    GLenum light_model_color_ctrl = GL_SINGLE_COLOR; // glLightModel(GL_LIGHT_MODEL_COLOR_CONTROL)
    int light_model_local_viewer = 0;                // glLightModel(GL_LIGHT_MODEL_LOCAL_VIEWER)
    int light_model_two_side = 0;                    // glLightModel(GL_LIGHT_MODEL_TWO_SIDE)
    GLenum color_material_face = GL_FRONT_AND_BACK;
    GLenum color_material_mode = GL_AMBIENT_AND_DIFFUSE;
    // glTexGen mode per unit per coord (S,T,R,Q); GL_EYE_LINEAR planes are
    // captured in eye space at call time, object planes verbatim.
    GLenum texture_gen_mode[MAX_TEX][4] = {};

    // Texture environment mode changes the generated fragment shader. Keep a
    // compact copy in shader state while the numeric parameters remain uniforms.
    GLenum texture_env_mode[MAX_TEX] = {
        GL_MODULATE, GL_MODULATE, GL_MODULATE, GL_MODULATE,
        GL_MODULATE, GL_MODULATE, GL_MODULATE, GL_MODULATE,
        GL_MODULATE, GL_MODULATE, GL_MODULATE, GL_MODULATE,
        GL_MODULATE, GL_MODULATE, GL_MODULATE, GL_MODULATE,
    };

    // Fixed-function VAO
    // Reserve a vao purely for fpe, so that
    // it won't mess up with other states in
    // programmable pipeline.
    GLuint fpe_vao = 0;

    GLuint fpe_vbo = 0;

    GLuint fpe_immediate_vbo = 0;
    // Ring segmentation fences (4 segments): waiting on a quarter instead
    // of glFinish keeps wrap-around from stalling the whole pipeline.
    void* fpe_immediate_fences[4] = {};
    size_t fpe_immediate_vbo_capacity = 0;
    size_t fpe_immediate_vbo_offset = 0;
    void* fpe_immediate_vbo_map = nullptr;
    bool fpe_immediate_vbo_persistent_attempted = false;

    GLuint fpe_ibo = 0;

    // Dedicated element buffer for FPE-converted glDrawElements calls
    // (client-memory indices and rewritten GL_QUADS). fpe_ibo belongs to the
    // QUADS-from-arrays cache and must not be stomped with foreign indices.
    GLuint fpe_element_ibo = 0;

    std::vector<uint32_t> fpe_ib;
    std::vector<uint16_t> fpe_ib16;
    GLuint fpe_ib_first = 0;
    size_t fpe_ib_quad_count = 0;
    GLenum fpe_ib_type = GL_UNSIGNED_SHORT;
    bool fpe_ib_valid = false;
    bool fpe_ibo_bound = false;

    struct vertex_pointer_array_t vertexpointer_array;
    struct vertex_pointer_array_t normalized_vpa;
    // GL_ARRAY_BUFFER binding captured at each gl*Pointer call (client
    // state, so it lives on the per-context aggregate; previously a
    // process-wide global that leaked across contexts - audit finding).
    GLuint client_array_buffer_bindings[VERTEX_POINTER_COUNT] = {};
    struct fixed_function_bool_t fpe_bools;
    struct fixed_function_draw_state_t fpe_draw;
};

struct fixed_function_uniform_t {
    fixed_function_uniform_t() {
        // GL default stipple is all ones: nothing rejected.
        for (auto& b : polygon_stipple) b = 0xFF;
        for (auto& row : polygon_stipple_rows) row = 0xFFFFFFFFu;
    }

    // glPolygonStipple: raw 128-byte mask for GetPolygonStipple, plus a
    // row-packed LSB-at-x0 form the fragment shader bit-tests directly.
    GLubyte polygon_stipple[128];
    GLuint polygon_stipple_rows[32];
    GLint line_stipple_factor = 1;
    GLushort line_stipple_pattern = 0xFFFF;

    // glAlphaFunc
    GLclampf alpha_ref = 0.0f;

    // glFogf
    GLfloat fog_density = 1.f;
    GLfloat fog_start = 0.f;
    GLfloat fog_end = 1.f;
    // glFogfv/iv
    glm::vec4 fog_color = {0., 0., 0., 0.};

    // glLightModel
    glm::vec4 light_model_ambient = {0.2, 0.2, 0.2, 1.0};

    // glPolygonMode: GL_FILL is native; LINE/POINT emulation is plans/08.
    GLenum polygon_mode_front = GL_FILL;
    GLenum polygon_mode_back = GL_FILL;

    // glTexGen plane equations: [unit][coord] object and eye variants.
    glm::vec4 texgen_object_plane[MAX_TEX][4] = {};
    glm::vec4 texgen_eye_plane[MAX_TEX][4] = {};

    // glClipPlane equations, already transformed into eye space at call
    // time per spec; consumed by the generated shaders in plans/08.
    glm::dvec4 clip_planes[6] = {};

    // glPixelZoom / glPixelTransfer scale-bias (consumed by the CPU pixel
    // paths in plans/08; state and queries are exact).
    GLfloat pixel_zoom_x = 1.0f, pixel_zoom_y = 1.0f;
    GLfloat pixel_scale[5] = {1, 1, 1, 1, 1}; // R,G,B,A,Depth
    GLfloat pixel_bias[5] = {0, 0, 0, 0, 0};
    bool pixel_map_color = false;
    bool pixel_map_stencil = false;

    // Raster position state (glRasterPos*/glWindowPos*): window coords,
    // validity, and associated attributes. Consumed by the plans/08
    // bitmap/pixel paths; queryable via GL_CURRENT_RASTER_*.
    glm::vec4 raster_position = {0, 0, 0, 1}; // window x, y, z, clip w
    bool raster_position_valid = true;
    glm::vec4 raster_color = {1, 1, 1, 1};
    glm::vec4 raster_texcoord = {0, 0, 0, 1};

    // glPointSize (GLES has no fixed point-size state; emitted as
    // gl_PointSize from the generated vertex shader)
    GLfloat point_size = 1.0f;

    // glMatrix*
    struct transformation_t transformation;

    // glLightf/i/fv/iv
    light_t lights[MAX_LIGHTS];

    // glMaterial* and glTexEnv*. These are retained even while the current
    // shader generator only approximates their effect.
    material_t materials[2]; // front, back
    texture_env_t texture_env[MAX_TEX];
};

struct program_uniform_locations_t {
    GLint model_view = -1;
    GLint model_view_projection = -1;
    GLint normal = -1;
    GLint light_model_ambient = -1;
    GLint front_material_ambient = -1;
    GLint front_material_diffuse = -1;
    GLint front_material_emission = -1;
    GLint front_material_specular = -1;
    GLint front_material_shininess = -1;
    GLint back_material_ambient = -1;
    GLint back_material_diffuse = -1;
    GLint back_material_emission = -1;
    GLint back_material_specular = -1;
    GLint back_material_shininess = -1;
    GLint light_ambient[MAX_LIGHTS] = {};
    GLint light_diffuse[MAX_LIGHTS] = {};
    GLint light_specular[MAX_LIGHTS] = {};
    GLint light_position[MAX_LIGHTS] = {};
    GLint light_attenuation[MAX_LIGHTS] = {};
    GLint light_spot_direction[MAX_LIGHTS] = {};
    GLint light_spot_params[MAX_LIGHTS] = {};
    GLint sampler[MAX_TEX] = {};
    GLint texture_matrix[MAX_TEX] = {};
    GLint texture_env_color[MAX_TEX] = {};
    GLint texgen_obj_planes[MAX_TEX] = {};
    GLint texgen_eye_planes[MAX_TEX] = {};
    GLint clip_planes[6] = {};
    GLint polygon_stipple_rows = -1;
    GLint fog_color = -1;
    GLint fog_density = -1;
    GLint fog_start = -1;
    GLint fog_end = -1;
    GLint alpha_ref = -1;
    GLint point_size = -1;
    bool initialized = false;

    void initialize(GLuint program);
};

struct program_uniform_values_t {
    glm::mat4 model_view{};
    glm::mat4 projection{};
    glm::vec4 light_model_ambient{};
    glm::vec4 material_ambient[2]{};
    glm::vec4 material_diffuse[2]{};
    glm::vec4 material_emission[2]{};
    glm::vec4 material_specular[2]{};
    GLfloat material_shininess[2]{};
    glm::vec4 light_ambient[MAX_LIGHTS]{};
    glm::vec4 light_diffuse[MAX_LIGHTS]{};
    glm::vec4 light_specular[MAX_LIGHTS]{};
    glm::vec4 light_position[MAX_LIGHTS]{};
    glm::vec4 light_attenuation[MAX_LIGHTS]{};   // xyz = kc, kl, kq
    glm::vec4 light_spot_direction[MAX_LIGHTS]{};
    glm::vec4 light_spot_params[MAX_LIGHTS]{};   // x = cos(cutoff) or -2, y = exponent
    glm::mat4 texture_matrix[MAX_TEX]{};
    glm::vec4 texture_env_color[MAX_TEX]{};
    glm::vec4 texgen_obj_planes[MAX_TEX][4]{};
    glm::vec4 texgen_eye_planes[MAX_TEX][4]{};
    glm::vec4 clip_planes[6]{};
    GLuint polygon_stipple_rows[32]{};
    glm::vec4 fog_color{};
    GLfloat fog_density = 0.0f;
    GLfloat fog_start = 0.0f;
    GLfloat fog_end = 0.0f;
    GLclampf alpha_ref = 0.0f;
    GLfloat point_size = 1.0f;
    bool initialized = false;
};

struct program_t {
    std::string vs;
    std::string fs;
    program_uniform_locations_t uniforms;
    program_uniform_values_t uniform_values;

    int get_program();

private:
    static int compile_shader(GLenum shader_type, const char* src);
    static int link_program(GLuint vs, GLuint fs);
    bool compile_attempted = false;
    int program = 0;
};

struct vertex_attribute_cache_entry_t {
    bool enable_known = false;
    bool enabled = false;
    bool pointer_valid = false;
    bool separate_binding = false;
    GLuint array_buffer = 0;
    GLint size = 0;
    GLenum type = 0;
    GLenum normalized = 0;
    GLsizei stride = 0;
    const void* pointer = nullptr;
};

struct program_vertex_signature_t {
    GLint size = 0;
    GLenum usage = 0;
    GLenum type = 0;
    GLenum normalized = 0;
};

// 128-bit program cache key: two independently seeded XXHash64 passes over
// the same state stream. A single 64-bit value used as the map key directly
// meant a hash collision silently reused the wrong shader; a collision now
// requires both halves to collide (~2^-128).
struct program_key_t {
    uint64_t lo = 0;
    uint64_t hi = 0;
    bool operator==(const program_key_t&) const = default;
};

struct program_key_hash_t {
    size_t operator()(const program_key_t& key) const noexcept { return static_cast<size_t>(key.lo); }
};

struct program_hash_cache_t {
    bool valid = false;
    program_key_t hash{};
    uint32_t enabled_pointers = 0;
    fixed_function_draw_size_t constant_sizes{};
    program_vertex_signature_t vertices[VERTEX_POINTER_COUNT]{};
    GLenum client_active_texture = 0;
    GLenum alpha_func = 0;
    GLenum fog_mode = 0;
    GLint fog_index = 0;
    GLenum fog_coord_src = 0;
    GLenum shade_model = 0;
    GLenum light_model_color_ctrl = 0;
    int light_model_local_viewer = 0;
    int light_model_two_side = 0;
    GLenum color_material_face = 0;
    GLenum color_material_mode = 0;
    fixed_function_bool_t bools{};
    GLenum texture_env_mode[MAX_TEX]{};
};

// glstate_t is THE aggregate for all context-scoped FPE state. It is a
// process-wide singleton today (glstate_t::get_instance()); plans/07 turns
// that accessor into a per-EGL-context lookup. New context-scoped state must
// live in here — never in file-scope globals.
struct glstate_t {
    template <typename K, typename V, typename H = std::hash<K>>
    using unordered_map = std::unordered_map<K, V, H>;

    // Set once init_fpe() has created the FPE-owned GL objects for this
    // context (previously the file-scope global fpe_inited).
    bool fpe_ready = false;

    // Desktop-only glPixelStore modes, consumed by the CPU pixel repack
    // pipeline (plans/05 and plans/08); GLES rejects these pnames.
    bool pixel_store_unpack_swap_bytes = false;
    bool pixel_store_unpack_lsb_first = false;
    bool pixel_store_pack_swap_bytes = false;
    bool pixel_store_pack_lsb_first = false;

    // Selection / feedback (plans/10 10.3). CPU transform results only;
    // nothing reaches the GPU while render_mode != GL_RENDER.
    GLenum render_mode = GL_RENDER;
    GLuint* select_buffer = nullptr;
    GLsizei select_buffer_size = 0;
    GLuint select_written = 0;
    GLuint select_hit_count = 0;
    bool select_overflow = false;
    std::vector<GLuint> name_stack;
    bool hit_pending = false;
    float hit_min_z = 1.0f, hit_max_z = 0.0f;
    GLfloat* feedback_buffer = nullptr;
    GLsizei feedback_buffer_size = 0;
    GLenum feedback_type = 0x0601 /* GL_3D */;
    GLsizei feedback_written = 0;
    bool feedback_overflow = false;

    // Wrapper-side GL error slot. GL semantics: only the first error since
    // the last glGetError() is kept; later ones are discarded until read.
    GLenum first_error = GL_NO_ERROR;

    void set_error(GLenum error) {
        if (first_error == GL_NO_ERROR) first_error = error;
    }

    // States that can led to layout change / shader recompile
    struct fixed_function_state_t fpe_state;
    struct fixed_function_uniform_t fpe_uniform;

    //    GLuint fpe_vtx_shader = 0;
    //    GLuint fpe_frag_shader = 0;
    //    GLuint fpe_program = 0;

    // Keyed by the dual-seeded 128-bit program_key_t (see above); the old
    // "vp as key" TODO is long obsolete - the hash covers all shader state.
    unordered_map<program_key_t, program_t, program_key_hash_t> fpe_programs;
    unordered_map<program_key_t, GLuint, program_key_hash_t> fpe_vaos;
    program_key_t last_program_key{};
    program_t* last_program = nullptr;
    program_hash_cache_t program_hash_cache;
    vertex_attribute_cache_entry_t fpe_vertex_attributes[VERTEX_POINTER_COUNT];
    bool fpe_vertex_binding_valid = false;
    GLuint fpe_vertex_binding_buffer = 0;
    GLintptr fpe_vertex_binding_offset = 0;
    GLsizei fpe_vertex_binding_stride = 0;

    static constexpr uint64_t s_hash_seed = 2123456789;
    static constexpr uint64_t s_hash_seed2 = 0x9e3779b97f4a7c15ull;

    const char* fpe_vtx_shader_src;
    const char* fpe_frag_shader_src;

    // Attribute stack storage (defined in attribstack.cpp); lives here so
    // it is per-context like everything else on this aggregate. shared_ptr
    // erases the deleter, so the incomplete type is fine in this header.
    std::shared_ptr<struct attrib_stacks_t> attrib_stacks;

    static glstate_t& get_instance();

    void send_uniforms(program_t& program);

    program_key_t program_hash();

    program_t& get_or_generate_program(const program_key_t& key);

    bool get_vao(const program_key_t& key, GLuint* vao);

    void save_vao(const program_key_t& key, const GLuint vao);

    bool send_vertex_attributes(const vertex_pointer_array_t& va, GLuint array_buffer, GLintptr binding_offset = 0);
};
