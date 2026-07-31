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
#include <unordered_set>
#include <vector>
#include <memory>
#include <cstddef>
#include <type_traits>
#include "fpe_shadergen.h"
#include "vertexpointer_utils.h"
#include <xxhash64.h>

// initial-exec for the handful of thread-locals every exported entry point
// touches: the general-dynamic model costs a _dl_tlsdesc call per access
// (~4% of a draw loop). NOT applied module-wide - that would claim ~1.1 KB of
// glibc's ~1.6 KB static-TLS surplus process-wide, and a host that has
// already spent it could then fail to dlopen this library at all. These few
// are a rounding error against that budget.
#define SFPEW_TLS_HOT __attribute__((tls_model("initial-exec")))

struct glstate_t;

// Snapshot of the last strict resolve on this thread.
extern thread_local SFPEW_TLS_HOT void* tls_snapshot_context;
extern thread_local SFPEW_TLS_HOT glstate_t* tls_snapshot_state;
// The context the wrapper's own eglMakeCurrent recorded, and how many
// resolves have trusted it since it was last reconciled against libEGL.
extern thread_local SFPEW_TLS_HOT void* g_authoritative_context;
extern thread_local SFPEW_TLS_HOT bool g_authoritative_context_known;
extern thread_local SFPEW_TLS_HOT unsigned g_context_reconcile_counter;
// How many resolves may trust the recorded context before it is re-read from
// libEGL. Adaptive: doubles while reconciliation keeps confirming the value,
// and drops back to a tight interval the moment one finds a context the
// wrapper was not told about. An app that never bypasses eglMakeCurrent - the
// normal case - stops paying for the check, while one that does is caught
// sooner than the old fixed interval caught it.
extern thread_local SFPEW_TLS_HOT unsigned g_context_reconcile_interval;
// SFPEW_RELAXED_CONTEXT=1: each thread promises to use one context forever.
extern bool g_sfpew_relaxed_context;
// Reconciles against libEGL and resets the counter. Out of line: it is the
// 1-in-256 path.
void* sfpewReconcileContext();

// std::vector value-initializes what resize() adds, and the immediate-mode
// vertex buffer overwrites every one of those floats on the very next line -
// a memset per vertex, measured at 5% of a small Begin/End batch. This
// allocator leaves default-constructed elements alone; everything else is
// std::allocator. Only for trivially-copyable arithmetic types, where "not
// initialized" is exactly what the caller is about to fix.
template <class T>
struct sfpew_no_init_allocator : std::allocator<T> {
    static_assert(std::is_trivially_default_constructible_v<T>);
    using std::allocator<T>::allocator;
    template <class U> struct rebind { using other = sfpew_no_init_allocator<U>; };
    template <class U, class... Args> void construct(U* p, Args&&... args) {
        ::new (static_cast<void*>(p)) U(std::forward<Args>(args)...);
    }
    template <class U> void construct(U*) noexcept {} // leave it to the writer
};
// The interleaved vertex stream a Begin/End batch collects.
using sfpew_vertex_buffer_t = std::vector<GLfloat, sfpew_no_init_allocator<GLfloat>>;

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

// GL_COLOR_BUFFER_BIT state that lives in the backend rather than in the
// shader generator. It still needs a shadow: glPushAttrib/glPopAttrib must
// restore it (legacy Minecraft brackets GUI and item rendering with
// glPushAttrib, and leaking a blend function corrupts every later
// translucent draw). Defaults are the GL initial values.
struct color_buffer_state_t {
    bool blend_enable = false;
    bool dither_enable = true;
    GLenum src_rgb = GL_ONE, dst_rgb = GL_ZERO;
    GLenum src_alpha = GL_ONE, dst_alpha = GL_ZERO;
    GLenum equation_rgb = GL_FUNC_ADD, equation_alpha = GL_FUNC_ADD;
    GLfloat blend_color[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    GLboolean color_mask[4] = {GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE};
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
    glm::vec4 secondary_color = {0, 0, 0, 1};
    GLfloat fog_coord = 0.0f; // glFogCoord* (GL 1.4 / EXT_fog_coord)
    GLboolean edge_flag = 1; // glEdgeFlag*; GL_TRUE by default (spec 2.6.3)
    glm::vec4 texcoord[MAX_TEX];

    fixed_function_draw_size_t sizes;
    // Stamped from a process-wide counter on every change to `sizes`, so the
    // per-vertex packing path can revalidate its layout with one integer
    // compare instead of re-comparing the whole size block. It lives here
    // rather than inside `sizes` on purpose: `sizes` is compared field-wise
    // and memcmp'd elsewhere to decide whether two runs share a layout, and
    // an epoch inside it would make every such comparison fail. Copying or
    // swapping this struct carries the stamp with the sizes it describes,
    // which is what keeps save/restore paths correct; only a write THROUGH
    // `sizes` needs a new stamp (see sfpewNextSizesEpoch).
    uint64_t sizes_epoch = 0;
};

// Next value for fixed_function_draw_data_t::sizes_epoch. Values are unique
// process-wide, so a layout built for one size block can never be mistaken
// for a different block that happens to be restored into the same slot.
uint64_t sfpewNextSizesEpoch();

// Sentinel for "no Begin/End block is open". GL_NONE is 0 - the SAME value as
// GL_POINTS, the first legal glBegin() mode - so using GL_NONE here made every
// GL_POINTS run indistinguishable from "not in a primitive": mglVertex/mglColor/
// etc. silently dropped every vertex (checked against this exact sentinel),
// glEnd() raised a spurious GL_INVALID_OPERATION on every legal empty-or-full
// GL_POINTS block, and glBegin()'s nested-Begin check silently passed through
// a second glBegin(GL_POINTS) instead of erroring. Found via piglit's
// beginend-coverage.c port, which primes every subtest through GL_POINTS.
constexpr GLenum kNoPrimitive = 0xFFFFFFFFu;

struct fixed_function_draw_state_t {
    GLenum primitive = kNoPrimitive;

    fixed_function_draw_data_t current_data;

    // Immediate-mode attributes are normalized to GLfloat before they reach
    // this buffer. Keep the storage alive across glBegin/glEnd pairs so small
    // legacy draws can reuse their allocation instead of constructing a stream
    // and copying its full string again at glEnd.
    sfpew_vertex_buffer_t vb;

    // Per-vertex edge flags, for GL_LINE polygon mode. Kept OUT of vb: they
    // never reach a shader, only the CPU-side wireframe expansion, and
    // widening every vertex by a byte to carry state almost no application
    // sets would be a poor trade.
    //
    // Empty means "every edge is a boundary", which is both the GL default
    // and what essentially every application leaves it at. advance() starts
    // filling this lazily, the first time a vertex arrives with the flag
    // cleared, so the common path pays one predictable compare per vertex
    // and never allocates.
    std::vector<uint8_t> edge_flags;

    size_t vertex_count = 0;

    // advance() fast path: compact copy plan derived from sizes. Rebuilt
    // whenever the sizes snapshot below stops matching current_data.sizes
    // (a 92-byte compare), which also catches wholesale sizes overwrites
    // (attrib-stack restore, immediate-draw guard) without hooks.
    struct packed_span_t {
        uint16_t src_offset; // in floats, from the start of current_data
        uint16_t count;
    };
    packed_span_t packed_spans[VERTEX_POINTER_COUNT];
    int packed_span_count = -1; // -1: never built
    size_t packed_floats = 0;
    fixed_function_draw_size_t packed_layout_sizes;
    // The current_data.sizes_epoch the spans above were built for. 0 never
    // matches a real stamp, so a default-constructed state always rebuilds.
    uint64_t packed_layout_epoch = 0;

    // Set when set_attribute_size had to repack already-collected vertices
    // (an attribute introduced mid-primitive). The display-list compiler
    // bails on such runs: the repack backfill would freeze compile-time
    // values where a live replay uses replay-time current values.
    bool repacked = false;

    void reset();

    // Put one vertex into vb, from current draw state
    void advance();

    void rebuild_packed_layout();

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
    color_buffer_state_t color_buffer;                // blend / masks (for attrib stack)

    // Last value handed to the backend for the plain per-fragment state
    // entries. They are pure state: nothing else in the wrapper writes them,
    // and an app cannot tell a skipped redundant call from an issued one, so
    // a repeat is dropped instead of crossing into the driver. A legacy
    // renderer re-asserts this state around every pass, and the driver side
    // of those calls dominated the state-churn benchmark.
    struct backend_state_shadow_t {
        bool known = false;
        GLenum depth_func = GL_LESS;
        GLboolean depth_mask = GL_TRUE;
        GLenum cull_face = GL_BACK;
        GLenum front_face = GL_CCW;
        GLint scissor[4] = {0, 0, 0, 0};
        GLfloat polygon_offset[2] = {0.0f, 0.0f};
        GLfloat line_width = 1.0f;
        GLenum stencil_func = GL_ALWAYS;
        GLint stencil_ref = 0;
        GLuint stencil_value_mask = 0xFFFFFFFFu;
        GLuint stencil_mask = 0xFFFFFFFFu;
        GLenum stencil_fail = GL_KEEP;
        GLenum stencil_zfail = GL_KEEP;
        GLenum stencil_zpass = GL_KEEP;

        // Server enables the wrapper never touches itself, so their last
        // issued value is known exactly. GL_BLEND and GL_DITHER are absent
        // on purpose: the accumulation-buffer path and the attrib-stack
        // restore drive those directly, which would leave a shadow stale.
        // Index order must match backendEnableSlot().
        enum : int {
            kEnableDepthTest,
            kEnableStencilTest,
            kEnableScissorTest,
            kEnableCullFace,
            kEnablePolygonOffsetFill,
            kEnableSampleAlphaToCoverage,
            kEnableSampleCoverage,
            kEnableCount,
        };
        bool enable_known[kEnableCount] = {};
        bool enable_value[kEnableCount] = {};
    } backend_state;

    // The vertex layout an immediate-mode draw derives from its size block:
    // the compiled pointer array, its normalized form and the compressed
    // attribute indices. Every draw rebuilt all three - including a
    // stack-constructed fixed_function_draw_state_t just to hold the input -
    // even though a draw loop feeds the same layout over and over. Keyed on
    // both size blocks because the shader's view (stream slots plus constant
    // ones) drives the attribute indices.
    struct immediate_layout_cache_t {
        bool valid = false;
        fixed_function_draw_size_t sizes{};
        fixed_function_draw_size_t shader_sizes{};
        vertex_pointer_array_t compiled{};
        vertex_pointer_array_t normalized{};
    } immediate_layout;
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

    // Separate VAO for feeding fixed-function vertex data into USER
    // programs (plans/09 S9 mixed pipeline): their attribute locations are
    // linker-assigned per program, so the fpe_vao attribute cache (indexed
    // by compressed FPE slots) cannot be shared. The mask tracks which
    // attribute locations are currently enabled inside this VAO.
    GLuint fpe_user_vao = 0;
    uint64_t fpe_user_vao_enabled = 0;

    GLuint fpe_vbo = 0;

    GLuint fpe_immediate_vbo = 0;
    // Ring segmentation fences (4 segments): waiting on a quarter instead
    // of glFinish keeps wrap-around from stalling the whole pipeline.
    void* fpe_immediate_fences[4] = {};
    size_t fpe_immediate_vbo_capacity = 0;
    size_t fpe_immediate_vbo_offset = 0;
    void* fpe_immediate_vbo_map = nullptr;
    bool fpe_immediate_vbo_persistent_attempted = false;

    // Index ring for FPE-converted glDrawElements whose indices come from
    // client memory or had to be rewritten. glBufferData per draw orphaned and
    // reallocated a buffer every call, which measured as roughly half the cost
    // of an indexed client-array draw (plans/12).
    GLuint fpe_element_ring = 0;
    void* fpe_element_ring_fences[4] = {};
    size_t fpe_element_ring_capacity = 0;
    size_t fpe_element_ring_offset = 0;
    void* fpe_element_ring_map = nullptr;
    bool fpe_element_ring_persistent_attempted = false;

    GLuint fpe_ibo = 0;

    // Dedicated element buffer for FPE-converted glDrawElements calls
    // (client-memory indices and rewritten GL_QUADS). fpe_ibo belongs to the
    // QUADS-from-arrays cache and must not be stomped with foreign indices.
    GLuint fpe_element_ibo = 0;

    // Repeat-index cache for FPE-converted glDrawElements: apps redraw the
    // same client-memory index pattern every frame (GUI widgets, glyph quads,
    // chunk passes), so the second consecutive draw with byte-identical
    // indices promotes them into a device-local buffer and later draws pay a
    // memcmp instead of a ring upload - and the GPU pulls indices from device
    // memory instead of the coherent ring. `pending` holds the last streamed
    // bytes; `bytes` holds what `buffer` currently contains.
    GLuint fpe_element_reuse_buffer = 0;
    std::vector<uint8_t> fpe_element_reuse_bytes;
    std::vector<uint8_t> fpe_element_reuse_pending;

    std::vector<uint32_t> fpe_ib;
    std::vector<uint16_t> fpe_ib16;
    GLuint fpe_ib_first = 0;
    size_t fpe_ib_quad_count = 0;
    GLenum fpe_ib_type = GL_UNSIGNED_SHORT;
    bool fpe_ib_valid = false;
    // Which of the two quad triangulations the cached indices hold; see
    // prepare_quad_indices. Part of the cache key: switching shade models
    // has to regenerate them.
    bool fpe_ib_flat = false;
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
    GLint alpha_func = -1;
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
    // Encoded alpha func the program last received (0 = off/GL_ALWAYS,
    // 1..7 = GL_NEVER..GL_GEQUAL); uniform-driven, never part of the key.
    GLint alpha_func = 0;
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
    // The combiner and texgen parameters the generator bakes into the shader
    // source. Without them the cache had to declare a miss whenever a unit
    // ran GL_COMBINE or texgen, which is precisely the multi-stage setup a
    // legacy renderer uses - so those draws re-hashed the entire
    // fixed-function state every time.
    struct combiner_signature_t {
        GLenum combine_rgb = 0, combine_alpha = 0;
        GLenum source_rgb[3]{}, source_alpha[3]{};
        GLenum operand_rgb[3]{}, operand_alpha[3]{};
        GLfloat rgb_scale = 0.0f, alpha_scale = 0.0f;
        bool operator==(const combiner_signature_t&) const = default;
    } combiners[MAX_TEX];
    GLenum texture_gen_mode[MAX_TEX][4]{};
};

// The subset of a texture environment the shader generator bakes into its
// source. The environment colour and LOD bias are excluded on purpose: they
// reach the shader as uniforms, so changing them must not mint a new program.
inline program_hash_cache_t::combiner_signature_t sfpewCombinerSignature(const texture_env_t& env) {
    program_hash_cache_t::combiner_signature_t out;
    out.combine_rgb = env.combine_rgb;
    out.combine_alpha = env.combine_alpha;
    for (int i = 0; i < 3; ++i) {
        out.source_rgb[i] = env.source_rgb[i];
        out.source_alpha[i] = env.source_alpha[i];
        out.operand_rgb[i] = env.operand_rgb[i];
        out.operand_alpha[i] = env.operand_alpha[i];
    }
    out.rgb_scale = env.rgb_scale;
    out.alpha_scale = env.alpha_scale;
    return out;
}

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
    // Backend program names created BY THE WRAPPER (FPE-generated programs,
    // pixel-op helpers). The logical program shadow consults this: a value it
    // reads back from the backend that is one of ours is a leftover of the
    // wrapper's own deferred bindings, never the app's program - recording it
    // would hijack every later fixed-function draw onto the user-program
    // path, which is exactly the on-device flicker (plans/12, the 1.12 black
    // screen was the same disease through a different window).
    std::unordered_set<int> internal_programs;
    // Same invariant for buffer objects: rings, quad-index buffers, the
    // display-list arena and captured static VBOs are bound only inside
    // wrapper draw scopes, so the array-buffer shadow reading one back from
    // the backend is always leftover wrapper state, never the app's binding.
    // (On-device forensics caught exactly that: "arraybuffer shadow heal
    // 0 -> 4" where buffer 4 is the wrapper's own arena.)
    std::unordered_set<unsigned> internal_buffers;
    unordered_map<program_key_t, GLuint, program_key_hash_t> fpe_vaos;
    program_key_t last_program_key{};
    program_t* last_program = nullptr;
    // Several ways, because applications alternate between a handful of
    // fixed-function configurations rather than settling on one: toggling
    // the alpha test around a GUI widget, or lighting between passes, made a
    // single-entry cache miss on every draw and re-hash the entire
    // fixed-function state twice over. Round-robin replacement; four ways
    // covers the alternation depth real renderers show.
    static constexpr int kProgramHashCacheWays = 4;
    program_hash_cache_t program_hash_cache[kProgramHashCacheWays];
    int program_hash_cache_next = 0;
    vertex_attribute_cache_entry_t fpe_vertex_attributes[VERTEX_POINTER_COUNT];
    bool fpe_vertex_binding_valid = false;
    GLuint fpe_vertex_binding_buffer = 0;
    GLintptr fpe_vertex_binding_offset = 0;
    GLsizei fpe_vertex_binding_stride = 0;

    static constexpr uint64_t s_hash_seed = 2123456789;
    static constexpr uint64_t s_hash_seed2 = 0x9e3779b97f4a7c15ull;

    const char* fpe_vtx_shader_src;
    const char* fpe_frag_shader_src;

    // Backend binding shadows: the VAO the wrapper last bound on the backend,
    // and the element-array binding of VAO 0 (element bindings are VAO state;
    // only VAO 0's needs explicit save/restore). They replace the draw
    // guard's two synchronous glGetIntegerv round-trips and self-heal with a
    // real query every 256 draws, mirroring the logical program shadow.
    GLint backend_vao_binding = 0;
    bool backend_vao_known = false;
    GLint backend_vao0_element_binding = 0;
    bool backend_vao0_element_known = false;
    unsigned backend_shadow_heal_counter = 0;

    // Deferred restore of the app's draw state (plans/12).
    //
    // A fixed-function draw binds the wrapper's own program/VAO/array buffer.
    // Restoring the app's immediately means a run of fixed-function draws
    // restores and re-binds the same three things per draw - about 7 driver
    // calls a batch, measured at 3.3x of tinybatch on NVIDIA. Instead the
    // wrapper's bindings stay put and the app's are saved here until
    // sfpewEntryBarrier() puts them back, which every entry point outside the
    // immediate-mode vertex family does. held == false means the backend is in
    // the app's state and the rest of these fields mean nothing.
    struct deferred_draw_state_t {
        bool held = false;
        GLint program = 0;
        GLint vertex_array = 0;
        GLint array_buffer = 0;
        // -1: the restored VAO carries its own element binding.
        GLint element_array_buffer = -1;
    } deferred_draw;

    // Deferring the restore only removes half the per-draw driver calls; the
    // other half is the next draw re-binding what is already bound. This says
    // "the immediate-draw program/VAO/ring buffer are live on the backend, for
    // this program id" so drawImmediateVertices can skip its three binds.
    //
    // -1 = not live. Deliberately narrow: only drawImmediateVertices sets it,
    // and every other path that binds any of those three clears it via
    // sfpewInvalidateImmediateDrawState(). Failing to SET it only costs speed;
    // failing to CLEAR it would skip a needed bind, so the clear belongs with
    // the bind, not with the caller.
    GLint immediate_live_program = -1;
    // The buffer bound alongside immediate_live_program. A compiled display
    // list replays from its own resident buffer while a live glBegin/glEnd
    // streams through the ring, so the armed fast path must key on both or one
    // shape inherits the other's binding.
    GLuint immediate_live_buffer = 0;

    // Validity of fpe_state.normalized_vpa as a reusable layout cache (see
    // commit_fpe_state_on_draw). True only when the cached copy came from a
    // plain normalize() - never from a gather, which bakes first/count in -
    // and fpe_normalized_sizes snapshots the constant-attribute sizes the
    // compressed index was generated against. Everything that overwrites or
    // clears normalized_vpa outside that one rebuild site must drop this.
    bool fpe_normalized_valid = false;
    fixed_function_draw_size_t fpe_normalized_sizes{};

    // Attribute stack storage (defined in attribstack.cpp); lives here so
    // it is per-context like everything else on this aggregate. shared_ptr
    // erases the deleter, so the incomplete type is fine in this header.
    std::shared_ptr<struct attrib_stacks_t> attrib_stacks;

    // Strict context resolve: calls eglGetCurrentContext() and refreshes the
    // thread-local snapshot. Every context-sensitive exported entry point must
    // reach this exactly once (its first context access); on glvnd desktops
    // one call costs ~425ns (getpid + dispatch mutex inside the driver), so
    // everything downstream of the entry uses current() instead.
    static glstate_t& get_instance();

    // Relaxed accessor: returns the snapshot of the last strict resolve on
    // this thread (a TLS read), resolving strictly only if the thread has
    // never resolved. Safe anywhere downstream of an entry's strict resolve:
    // the current context cannot change mid-call on the calling thread.
    static glstate_t& current();

    // Vertex-data accessor for glVertex/glColor/glTexCoord/glNormal-class
    // entries: while a Begin/End batch is collecting (primitive != kNoPrimitive)
    // the batch stays pinned to the snapshot context and skips the strict
    // resolve entirely (a context switch mid-Begin/End is undefined; we
    // define it as "the batch belongs to the Begin context"). Outside a
    // batch this is a strict resolve, preserving the documented contract.
    static glstate_t& current_vertex_data();

    // The EGLContext observed by the last strict resolve on this thread.
    // Logical shadows reconcile against this instead of re-calling
    // eglGetCurrentContext; freshness is provided by the entry's resolve.
    static void* cached_context();

    void send_uniforms(program_t& program);

    program_key_t program_hash();

    program_t& get_or_generate_program(const program_key_t& key);

    bool get_vao(const program_key_t& key, GLuint* vao);

    void save_vao(const program_key_t& key, const GLuint vao);

    bool send_vertex_attributes(const vertex_pointer_array_t& va, GLuint array_buffer, GLintptr binding_offset = 0);
};

// The strict resolve every exported entry point performs, inlined. Only the
// context switch and the periodic reconciliation stay out of line: the call
// itself was 48% of a glGetFloatv, and every entry point pays it once.
inline void* sfpewCurrentContextInline() {
    if (!g_authoritative_context_known) return sfpewReconcileContext();
    // Bounds how long an eglMakeCurrent that bypassed the wrapper can go
    // unnoticed (docs/context-model.md). The bound adapts - see the interval's
    // declaration - because on glvnd the query itself costs ~425ns, which at a
    // fixed 256 was 9% of a glGet.
    if (++g_context_reconcile_counter < g_context_reconcile_interval)
        return g_authoritative_context;
    return sfpewReconcileContext();
}

// The vertex-data pin: while a Begin/End batch is collecting, every
// glVertex/glColor/glNormal/glTexCoord stays on the state the glBegin
// resolved, so they cost a thread-local read instead of a resolve. Three of
// them run per immediate-mode vertex, which is why this is inline.
inline glstate_t& sfpewVertexDataState();

inline glstate_t& sfpewResolveState() {
    if (g_sfpew_relaxed_context && tls_snapshot_state != nullptr &&
        tls_snapshot_context != nullptr) {
        return *tls_snapshot_state;
    }
    if (sfpewCurrentContextInline() == tls_snapshot_context && tls_snapshot_state != nullptr)
        return *tls_snapshot_state;
    return glstate_t::get_instance(); // switch, or first resolve on this thread
}

inline glstate_t& sfpewVertexDataState() {
    // Pin only to a real context: a Begin issued with no current context
    // lands on the no-context state, and vertices arriving after the app then
    // makes one current must resolve strictly (and be dropped by the
    // primitive == kNoPrimitive guard) exactly like the pre-snapshot
    // behaviour.
    glstate_t* const state = tls_snapshot_state;
    if (state != nullptr && state->fpe_state.fpe_draw.primitive != kNoPrimitive &&
        tls_snapshot_context != nullptr) {
        return *state;
    }
    return glstate_t::get_instance();
}
