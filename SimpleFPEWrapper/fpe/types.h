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
#include <sstream>
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

    std::stringstream vb;

    size_t vertex_count = 0;

    void reset();

    // Put one vertex into vb, from current draw state
    void advance();

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

    GLuint fpe_ibo = 0;

    std::vector<uint32_t> fpe_ib;

    struct vertex_pointer_array_t vertexpointer_array;
    struct vertex_pointer_array_t normalized_vpa;
    struct fixed_function_bool_t fpe_bools;
    struct fixed_function_draw_state_t fpe_draw;
};

struct fixed_function_uniform_t {
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

    // glMatrix*
    struct transformation_t transformation;

    // glLightf/i/fv/iv
    light_t lights[MAX_LIGHTS];

    // glMaterial* and glTexEnv*. These are retained even while the current
    // shader generator only approximates their effect.
    material_t materials[2]; // front, back
    texture_env_t texture_env[MAX_TEX];
};

struct program_t {
    std::string vs;
    std::string fs;

    int get_program();

private:
    static int compile_shader(GLenum shader_type, const char* src);
    static int link_program(GLuint vs, GLuint fs);
    bool compile_attempted = false;
    int program = 0;
};

struct glstate_t {
    template <typename K, typename V>
    using unordered_map = std::unordered_map<K, V>;

    // States that can led to layout change / shader recompile
    struct fixed_function_state_t fpe_state;
    struct fixed_function_uniform_t fpe_uniform;

    //    GLuint fpe_vtx_shader = 0;
    //    GLuint fpe_frag_shader = 0;
    //    GLuint fpe_program = 0;

    // enabled_vertexpointers - program
    // TODO: using vp as key is bad! Try to hash the whole fpe_state
    unordered_map<uint64_t, program_t> fpe_programs;
    unordered_map<uint64_t, GLuint> fpe_vaos;

    static constexpr uint64_t s_hash_seed = 2123456789;

    const char* fpe_vtx_shader_src;
    const char* fpe_frag_shader_src;

    static glstate_t& get_instance();

    void send_uniforms(int program);

    std::unique_ptr<XXHash64> p_hash = std::make_unique<XXHash64>(s_hash_seed);

    uint64_t program_hash(bool reset = true);

    uint64_t vertex_attrib_hash(bool reset = true);

    program_t& get_or_generate_program(const uint64_t key);

    bool get_vao(const uint64_t key, GLuint* vao);

    void save_vao(const uint64_t key, const GLuint vao);

    bool send_vertex_attributes(const vertex_pointer_array_t& va) const;
};
