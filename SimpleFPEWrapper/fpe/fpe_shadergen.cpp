// SimpleFPEWrapper - SimpleFPEWrapper/fpe/fpe_shadergen.cpp
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

#include "fpe_shadergen.h"
#include "types.h"
#include <cstdio>
#include <format>
#include <string_view>
#include <GL/gl.h>
#include "../init.h"

#define DEBUG 0

#if GLOBAL_DEBUG || DEBUG
#pragma clang optimize off
#endif

#define CASE(e)                                                                                                        \
    case e:                                                                                                            \
        return #e;

const char* glEnumToString(GLenum e) {
    static char str[128];
    switch (e) {
        /* Boolean values */

        /* Data types */
        CASE(GL_BYTE)
        CASE(GL_UNSIGNED_BYTE)
        CASE(GL_SHORT)
        CASE(GL_UNSIGNED_SHORT)
        CASE(GL_INT)
        CASE(GL_UNSIGNED_INT)
        CASE(GL_FLOAT)
        CASE(GL_2_BYTES)
        CASE(GL_3_BYTES)
        CASE(GL_4_BYTES)
        CASE(GL_DOUBLE)

        CASE(GL_UNSIGNED_BYTE_3_3_2)
        CASE(GL_UNSIGNED_BYTE_2_3_3_REV)
        CASE(GL_UNSIGNED_SHORT_5_6_5)
        CASE(GL_UNSIGNED_SHORT_5_6_5_REV)
        CASE(GL_UNSIGNED_SHORT_4_4_4_4)
        CASE(GL_UNSIGNED_SHORT_4_4_4_4_REV)
        CASE(GL_UNSIGNED_SHORT_5_5_5_1)
        CASE(GL_UNSIGNED_SHORT_1_5_5_5_REV)
        CASE(GL_UNSIGNED_INT_8_8_8_8)
        CASE(GL_UNSIGNED_INT_8_8_8_8_REV)
        CASE(GL_UNSIGNED_INT_10_10_10_2)

        /* Primitives */
        CASE(GL_LINE_LOOP)
        CASE(GL_LINE_STRIP)
        CASE(GL_TRIANGLES)
        CASE(GL_TRIANGLE_STRIP)
        CASE(GL_TRIANGLE_FAN)
        CASE(GL_QUADS)
        CASE(GL_QUAD_STRIP)
        CASE(GL_POLYGON)

        /* Vertex Arrays */
        CASE(GL_VERTEX_ARRAY)
        CASE(GL_NORMAL_ARRAY)
        CASE(GL_COLOR_ARRAY)
        CASE(GL_INDEX_ARRAY)
        CASE(GL_TEXTURE_COORD_ARRAY)
        CASE(GL_EDGE_FLAG_ARRAY)
        CASE(GL_VERTEX_ARRAY_SIZE)
        CASE(GL_VERTEX_ARRAY_TYPE)
        CASE(GL_VERTEX_ARRAY_STRIDE)
        CASE(GL_NORMAL_ARRAY_TYPE)
        CASE(GL_NORMAL_ARRAY_STRIDE)
        CASE(GL_COLOR_ARRAY_SIZE)
        CASE(GL_COLOR_ARRAY_TYPE)
        CASE(GL_COLOR_ARRAY_STRIDE)
        CASE(GL_INDEX_ARRAY_TYPE)
        CASE(GL_INDEX_ARRAY_STRIDE)
        CASE(GL_TEXTURE_COORD_ARRAY_SIZE)
        CASE(GL_TEXTURE_COORD_ARRAY_TYPE)
        CASE(GL_TEXTURE_COORD_ARRAY_STRIDE)
        CASE(GL_EDGE_FLAG_ARRAY_STRIDE)
        CASE(GL_VERTEX_ARRAY_POINTER)
        CASE(GL_NORMAL_ARRAY_POINTER)
        CASE(GL_COLOR_ARRAY_POINTER)
        CASE(GL_INDEX_ARRAY_POINTER)
        CASE(GL_TEXTURE_COORD_ARRAY_POINTER)
        CASE(GL_EDGE_FLAG_ARRAY_POINTER)
        CASE(GL_V2F)
        CASE(GL_V3F)
        CASE(GL_C4UB_V2F)
        CASE(GL_C4UB_V3F)
        CASE(GL_C3F_V3F)
        CASE(GL_N3F_V3F)
        CASE(GL_C4F_N3F_V3F)
        CASE(GL_T2F_V3F)
        CASE(GL_T4F_V4F)
        CASE(GL_T2F_C4UB_V3F)
        CASE(GL_T2F_C3F_V3F)
        CASE(GL_T2F_N3F_V3F)
        CASE(GL_T2F_C4F_N3F_V3F)
        CASE(GL_T4F_C4F_N3F_V4F)

        /* Matrix Mode */
        CASE(GL_MATRIX_MODE)
        CASE(GL_MODELVIEW)
        CASE(GL_PROJECTION)
        CASE(GL_TEXTURE)

        /* Points */
        CASE(GL_POINT_SMOOTH)
        CASE(GL_POINT_SIZE)
        CASE(GL_POINT_SIZE_GRANULARITY)
        CASE(GL_POINT_SIZE_RANGE)

        /* Lines */
        CASE(GL_LINE_SMOOTH)
        CASE(GL_LINE_STIPPLE)
        CASE(GL_LINE_STIPPLE_PATTERN)
        CASE(GL_LINE_STIPPLE_REPEAT)
        CASE(GL_LINE_WIDTH)
        CASE(GL_LINE_WIDTH_GRANULARITY)
        CASE(GL_LINE_WIDTH_RANGE)

        /* Polygons */
        CASE(GL_POINT)
        CASE(GL_LINE)
        CASE(GL_FILL)
        CASE(GL_CW)
        CASE(GL_CCW)
        CASE(GL_FRONT)
        CASE(GL_BACK)
        CASE(GL_POLYGON_MODE)
        CASE(GL_POLYGON_SMOOTH)
        CASE(GL_POLYGON_STIPPLE)
        CASE(GL_EDGE_FLAG)
        CASE(GL_CULL_FACE)
        CASE(GL_CULL_FACE_MODE)
        CASE(GL_FRONT_FACE)
        CASE(GL_POLYGON_OFFSET_FACTOR)
        CASE(GL_POLYGON_OFFSET_UNITS)
        CASE(GL_POLYGON_OFFSET_POINT)
        CASE(GL_POLYGON_OFFSET_LINE)
        CASE(GL_POLYGON_OFFSET_FILL)

        /* Display Lists */
        CASE(GL_COMPILE)
        CASE(GL_COMPILE_AND_EXECUTE)
        CASE(GL_LIST_BASE)
        CASE(GL_LIST_INDEX)
        CASE(GL_LIST_MODE)

        /* Depth buffer */
        CASE(GL_NEVER)
        CASE(GL_LESS)
        CASE(GL_EQUAL)
        CASE(GL_LEQUAL)
        CASE(GL_GREATER)
        CASE(GL_NOTEQUAL)
        CASE(GL_GEQUAL)
        CASE(GL_ALWAYS)
        CASE(GL_DEPTH_TEST)
        CASE(GL_DEPTH_BITS)
        CASE(GL_DEPTH_CLEAR_VALUE)
        CASE(GL_DEPTH_FUNC)
        CASE(GL_DEPTH_RANGE)
        CASE(GL_DEPTH_WRITEMASK)
        CASE(GL_DEPTH_COMPONENT)

        /* Lighting */
        CASE(GL_LIGHTING)
        CASE(GL_LIGHT0)
        CASE(GL_LIGHT1)
        CASE(GL_LIGHT2)
        CASE(GL_LIGHT3)
        CASE(GL_LIGHT4)
        CASE(GL_LIGHT5)
        CASE(GL_LIGHT6)
        CASE(GL_LIGHT7)
        CASE(GL_SPOT_EXPONENT)
        CASE(GL_SPOT_CUTOFF)
        CASE(GL_CONSTANT_ATTENUATION)
        CASE(GL_LINEAR_ATTENUATION)
        CASE(GL_QUADRATIC_ATTENUATION)
        CASE(GL_AMBIENT)
        CASE(GL_DIFFUSE)
        CASE(GL_SPECULAR)
        CASE(GL_SHININESS)
        CASE(GL_EMISSION)
        CASE(GL_POSITION)
        CASE(GL_SPOT_DIRECTION)
        CASE(GL_AMBIENT_AND_DIFFUSE)
        CASE(GL_COLOR_INDEXES)
        CASE(GL_LIGHT_MODEL_TWO_SIDE)
        CASE(GL_LIGHT_MODEL_LOCAL_VIEWER)
        CASE(GL_LIGHT_MODEL_AMBIENT)
        CASE(GL_FRONT_AND_BACK)
        CASE(GL_SHADE_MODEL)
        CASE(GL_FLAT)
        CASE(GL_SMOOTH)
        CASE(GL_COLOR_MATERIAL)
        CASE(GL_COLOR_MATERIAL_FACE)
        CASE(GL_COLOR_MATERIAL_PARAMETER)
        CASE(GL_NORMALIZE)

        /* User clipping planes */
        CASE(GL_CLIP_PLANE0)
        CASE(GL_CLIP_PLANE1)
        CASE(GL_CLIP_PLANE2)
        CASE(GL_CLIP_PLANE3)
        CASE(GL_CLIP_PLANE4)
        CASE(GL_CLIP_PLANE5)

        /* Accumulation buffer */
        CASE(GL_ACCUM_RED_BITS)
        CASE(GL_ACCUM_GREEN_BITS)
        CASE(GL_ACCUM_BLUE_BITS)
        CASE(GL_ACCUM_ALPHA_BITS)
        CASE(GL_ACCUM_CLEAR_VALUE)
        CASE(GL_ACCUM)
        CASE(GL_ADD)
        CASE(GL_LOAD)
        CASE(GL_MULT)
        CASE(GL_RETURN)

        /* Alpha testing */
        CASE(GL_ALPHA_TEST)
        CASE(GL_ALPHA_TEST_REF)
        CASE(GL_ALPHA_TEST_FUNC)

        /* Blending */
        CASE(GL_BLEND)
        CASE(GL_BLEND_SRC)
        CASE(GL_BLEND_DST)
        CASE(GL_ZERO)
        CASE(GL_ONE)
        CASE(GL_SRC_COLOR)
        CASE(GL_ONE_MINUS_SRC_COLOR)
        CASE(GL_SRC_ALPHA)
        CASE(GL_ONE_MINUS_SRC_ALPHA)
        CASE(GL_DST_ALPHA)
        CASE(GL_ONE_MINUS_DST_ALPHA)
        CASE(GL_DST_COLOR)
        CASE(GL_ONE_MINUS_DST_COLOR)
        CASE(GL_SRC_ALPHA_SATURATE)

        /* Render Mode */
        CASE(GL_FEEDBACK)
        CASE(GL_RENDER)
        CASE(GL_SELECT)

        /* Feedback */
        CASE(GL_2D)
        CASE(GL_3D)
        CASE(GL_3D_COLOR)
        CASE(GL_3D_COLOR_TEXTURE)
        CASE(GL_4D_COLOR_TEXTURE)
        CASE(GL_POINT_TOKEN)
        CASE(GL_LINE_TOKEN)
        CASE(GL_LINE_RESET_TOKEN)
        CASE(GL_POLYGON_TOKEN)
        CASE(GL_BITMAP_TOKEN)
        CASE(GL_DRAW_PIXEL_TOKEN)
        CASE(GL_COPY_PIXEL_TOKEN)
        CASE(GL_PASS_THROUGH_TOKEN)
        CASE(GL_FEEDBACK_BUFFER_POINTER)
        CASE(GL_FEEDBACK_BUFFER_SIZE)
        CASE(GL_FEEDBACK_BUFFER_TYPE)

        /* Selection */
        CASE(GL_SELECTION_BUFFER_POINTER)
        CASE(GL_SELECTION_BUFFER_SIZE)

        /* Fog */
        CASE(GL_FOG)
        CASE(GL_FOG_MODE)
        CASE(GL_FOG_DENSITY)
        CASE(GL_FOG_COLOR)
        CASE(GL_FOG_INDEX)
        CASE(GL_FOG_START)
        CASE(GL_FOG_END)
        CASE(GL_LINEAR)
        CASE(GL_EXP)
        CASE(GL_EXP2)

        /* Logic Ops */
        CASE(GL_INDEX_LOGIC_OP)
        CASE(GL_COLOR_LOGIC_OP)
        CASE(GL_LOGIC_OP_MODE)
        CASE(GL_CLEAR)
        CASE(GL_SET)
        CASE(GL_COPY)
        CASE(GL_COPY_INVERTED)
        CASE(GL_NOOP)
        CASE(GL_INVERT)
        CASE(GL_AND)
        CASE(GL_NAND)
        CASE(GL_OR)
        CASE(GL_NOR)
        CASE(GL_XOR)
        CASE(GL_EQUIV)
        CASE(GL_AND_REVERSE)
        CASE(GL_AND_INVERTED)
        CASE(GL_OR_REVERSE)
        CASE(GL_OR_INVERTED)

        /* Stencil */
        CASE(GL_STENCIL_BITS)
        CASE(GL_STENCIL_TEST)
        CASE(GL_STENCIL_CLEAR_VALUE)
        CASE(GL_STENCIL_FUNC)
        CASE(GL_STENCIL_VALUE_MASK)
        CASE(GL_STENCIL_FAIL)
        CASE(GL_STENCIL_PASS_DEPTH_FAIL)
        CASE(GL_STENCIL_PASS_DEPTH_PASS)
        CASE(GL_STENCIL_REF)
        CASE(GL_STENCIL_WRITEMASK)
        CASE(GL_STENCIL_INDEX)
        CASE(GL_KEEP)
        CASE(GL_REPLACE)
        CASE(GL_INCR)
        CASE(GL_DECR)

        /* Buffers, Pixel Drawing/Reading */
        CASE(GL_LEFT)
        CASE(GL_RIGHT)
        CASE(GL_FRONT_LEFT)
        CASE(GL_FRONT_RIGHT)
        CASE(GL_BACK_LEFT)
        CASE(GL_BACK_RIGHT)
        CASE(GL_AUX0)
        CASE(GL_AUX1)
        CASE(GL_AUX2)
        CASE(GL_AUX3)
        CASE(GL_COLOR_INDEX)
        CASE(GL_RED)
        CASE(GL_GREEN)
        CASE(GL_BLUE)
        CASE(GL_ALPHA)
        CASE(GL_LUMINANCE)
        CASE(GL_LUMINANCE_ALPHA)
        CASE(GL_ALPHA_BITS)
        CASE(GL_RED_BITS)
        CASE(GL_GREEN_BITS)
        CASE(GL_BLUE_BITS)
        CASE(GL_INDEX_BITS)
        CASE(GL_SUBPIXEL_BITS)
        CASE(GL_AUX_BUFFERS)
        CASE(GL_READ_BUFFER)
        CASE(GL_DRAW_BUFFER)
        CASE(GL_DOUBLEBUFFER)
        CASE(GL_STEREO)
        CASE(GL_BITMAP)
        CASE(GL_COLOR)
        CASE(GL_DEPTH)
        CASE(GL_STENCIL)
        CASE(GL_DITHER)
        CASE(GL_RGB)
        CASE(GL_RGBA)

        /* Implementation limits */
        CASE(GL_MAX_LIST_NESTING)
        CASE(GL_MAX_EVAL_ORDER)
        CASE(GL_MAX_LIGHTS)
        CASE(GL_MAX_CLIP_PLANES)
        CASE(GL_MAX_TEXTURE_SIZE)
        CASE(GL_MAX_PIXEL_MAP_TABLE)
        CASE(GL_MAX_ATTRIB_STACK_DEPTH)
        CASE(GL_MAX_MODELVIEW_STACK_DEPTH)
        CASE(GL_MAX_NAME_STACK_DEPTH)
        CASE(GL_MAX_PROJECTION_STACK_DEPTH)
        CASE(GL_MAX_TEXTURE_STACK_DEPTH)
        CASE(GL_MAX_VIEWPORT_DIMS)
        CASE(GL_MAX_CLIENT_ATTRIB_STACK_DEPTH)

        /* Gets */
        CASE(GL_ATTRIB_STACK_DEPTH)
        CASE(GL_CLIENT_ATTRIB_STACK_DEPTH)
        CASE(GL_COLOR_CLEAR_VALUE)
        CASE(GL_COLOR_WRITEMASK)
        CASE(GL_CURRENT_INDEX)
        CASE(GL_CURRENT_COLOR)
        CASE(GL_CURRENT_NORMAL)
        CASE(GL_CURRENT_RASTER_COLOR)
        CASE(GL_CURRENT_RASTER_DISTANCE)
        CASE(GL_CURRENT_RASTER_INDEX)
        CASE(GL_CURRENT_RASTER_POSITION)
        CASE(GL_CURRENT_RASTER_TEXTURE_COORDS)
        CASE(GL_CURRENT_RASTER_POSITION_VALID)
        CASE(GL_CURRENT_TEXTURE_COORDS)
        CASE(GL_INDEX_CLEAR_VALUE)
        CASE(GL_INDEX_MODE)
        CASE(GL_INDEX_WRITEMASK)
        CASE(GL_MODELVIEW_MATRIX)
        CASE(GL_MODELVIEW_STACK_DEPTH)
        CASE(GL_NAME_STACK_DEPTH)
        CASE(GL_PROJECTION_MATRIX)
        CASE(GL_PROJECTION_STACK_DEPTH)
        CASE(GL_RENDER_MODE)
        CASE(GL_RGBA_MODE)
        CASE(GL_TEXTURE_MATRIX)
        CASE(GL_TEXTURE_STACK_DEPTH)
        CASE(GL_VIEWPORT)

        /* Evaluators */
        CASE(GL_AUTO_NORMAL)
        CASE(GL_MAP1_COLOR_4)
        CASE(GL_MAP1_INDEX)
        CASE(GL_MAP1_NORMAL)
        CASE(GL_MAP1_TEXTURE_COORD_1)
        CASE(GL_MAP1_TEXTURE_COORD_2)
        CASE(GL_MAP1_TEXTURE_COORD_3)
        CASE(GL_MAP1_TEXTURE_COORD_4)
        CASE(GL_MAP1_VERTEX_3)
        CASE(GL_MAP1_VERTEX_4)
        CASE(GL_MAP2_COLOR_4)
        CASE(GL_MAP2_INDEX)
        CASE(GL_MAP2_NORMAL)
        CASE(GL_MAP2_TEXTURE_COORD_1)
        CASE(GL_MAP2_TEXTURE_COORD_2)
        CASE(GL_MAP2_TEXTURE_COORD_3)
        CASE(GL_MAP2_TEXTURE_COORD_4)
        CASE(GL_MAP2_VERTEX_3)
        CASE(GL_MAP2_VERTEX_4)
        CASE(GL_MAP1_GRID_DOMAIN)
        CASE(GL_MAP1_GRID_SEGMENTS)
        CASE(GL_MAP2_GRID_DOMAIN)
        CASE(GL_MAP2_GRID_SEGMENTS)
        CASE(GL_COEFF)
        CASE(GL_ORDER)
        CASE(GL_DOMAIN)

        /* Hints */
        CASE(GL_PERSPECTIVE_CORRECTION_HINT)
        CASE(GL_POINT_SMOOTH_HINT)
        CASE(GL_LINE_SMOOTH_HINT)
        CASE(GL_POLYGON_SMOOTH_HINT)
        CASE(GL_FOG_HINT)
        CASE(GL_DONT_CARE)
        CASE(GL_FASTEST)
        CASE(GL_NICEST)

        /* Scissor box */
        CASE(GL_SCISSOR_BOX)
        CASE(GL_SCISSOR_TEST)

        /* Pixel Mode / Transfer */
        CASE(GL_MAP_COLOR)
        CASE(GL_MAP_STENCIL)
        CASE(GL_INDEX_SHIFT)
        CASE(GL_INDEX_OFFSET)
        CASE(GL_RED_SCALE)
        CASE(GL_RED_BIAS)
        CASE(GL_GREEN_SCALE)
        CASE(GL_GREEN_BIAS)
        CASE(GL_BLUE_SCALE)
        CASE(GL_BLUE_BIAS)
        CASE(GL_ALPHA_SCALE)
        CASE(GL_ALPHA_BIAS)
        CASE(GL_DEPTH_SCALE)
        CASE(GL_DEPTH_BIAS)
        CASE(GL_PIXEL_MAP_S_TO_S_SIZE)
        CASE(GL_PIXEL_MAP_I_TO_I_SIZE)
        CASE(GL_PIXEL_MAP_I_TO_R_SIZE)
        CASE(GL_PIXEL_MAP_I_TO_G_SIZE)
        CASE(GL_PIXEL_MAP_I_TO_B_SIZE)
        CASE(GL_PIXEL_MAP_I_TO_A_SIZE)
        CASE(GL_PIXEL_MAP_R_TO_R_SIZE)
        CASE(GL_PIXEL_MAP_G_TO_G_SIZE)
        CASE(GL_PIXEL_MAP_B_TO_B_SIZE)
        CASE(GL_PIXEL_MAP_A_TO_A_SIZE)
        CASE(GL_PIXEL_MAP_S_TO_S)
        CASE(GL_PIXEL_MAP_I_TO_I)
        CASE(GL_PIXEL_MAP_I_TO_R)
        CASE(GL_PIXEL_MAP_I_TO_G)
        CASE(GL_PIXEL_MAP_I_TO_B)
        CASE(GL_PIXEL_MAP_I_TO_A)
        CASE(GL_PIXEL_MAP_R_TO_R)
        CASE(GL_PIXEL_MAP_G_TO_G)
        CASE(GL_PIXEL_MAP_B_TO_B)
        CASE(GL_PIXEL_MAP_A_TO_A)
        CASE(GL_PACK_ALIGNMENT)
        CASE(GL_PACK_LSB_FIRST)
        CASE(GL_PACK_ROW_LENGTH)
        CASE(GL_PACK_SKIP_PIXELS)
        CASE(GL_PACK_SKIP_ROWS)
        CASE(GL_PACK_SWAP_BYTES)
        CASE(GL_UNPACK_ALIGNMENT)
        CASE(GL_UNPACK_LSB_FIRST)
        CASE(GL_UNPACK_ROW_LENGTH)
        CASE(GL_UNPACK_SKIP_PIXELS)
        CASE(GL_UNPACK_SKIP_ROWS)
        CASE(GL_UNPACK_SWAP_BYTES)
        CASE(GL_ZOOM_X)
        CASE(GL_ZOOM_Y)

        /* Texture mapping */
        CASE(GL_TEXTURE_ENV)
        CASE(GL_TEXTURE_ENV_MODE)
        CASE(GL_TEXTURE_1D)
        CASE(GL_TEXTURE_2D)
        CASE(GL_TEXTURE_WRAP_S)
        CASE(GL_TEXTURE_WRAP_T)
        CASE(GL_TEXTURE_MAG_FILTER)
        CASE(GL_TEXTURE_MIN_FILTER)
        CASE(GL_TEXTURE_ENV_COLOR)
        CASE(GL_TEXTURE_GEN_S)
        CASE(GL_TEXTURE_GEN_T)
        CASE(GL_TEXTURE_GEN_R)
        CASE(GL_TEXTURE_GEN_Q)
        CASE(GL_TEXTURE_GEN_MODE)
        CASE(GL_TEXTURE_BORDER_COLOR)
        CASE(GL_TEXTURE_WIDTH)
        CASE(GL_TEXTURE_HEIGHT)
        CASE(GL_TEXTURE_BORDER)
        CASE(GL_TEXTURE_COMPONENTS)
        CASE(GL_TEXTURE_RED_SIZE)
        CASE(GL_TEXTURE_GREEN_SIZE)
        CASE(GL_TEXTURE_BLUE_SIZE)
        CASE(GL_TEXTURE_ALPHA_SIZE)
        CASE(GL_TEXTURE_LUMINANCE_SIZE)
        CASE(GL_TEXTURE_INTENSITY_SIZE)
        CASE(GL_NEAREST_MIPMAP_NEAREST)
        CASE(GL_NEAREST_MIPMAP_LINEAR)
        CASE(GL_LINEAR_MIPMAP_NEAREST)
        CASE(GL_LINEAR_MIPMAP_LINEAR)
        CASE(GL_OBJECT_LINEAR)
        CASE(GL_OBJECT_PLANE)
        CASE(GL_EYE_LINEAR)
        CASE(GL_EYE_PLANE)
        CASE(GL_SPHERE_MAP)
        CASE(GL_DECAL)
        CASE(GL_MODULATE)
        CASE(GL_NEAREST)
        CASE(GL_REPEAT)
        CASE(GL_CLAMP)
        CASE(GL_S)
        CASE(GL_T)
        CASE(GL_R)
        CASE(GL_Q)

        /* Utility */
        CASE(GL_VENDOR)
        CASE(GL_RENDERER)
        CASE(GL_VERSION)
        CASE(GL_EXTENSIONS)

        /* Errors */
        CASE(GL_INVALID_ENUM)
        CASE(GL_INVALID_VALUE)
        CASE(GL_INVALID_OPERATION)
        CASE(GL_STACK_OVERFLOW)
        CASE(GL_STACK_UNDERFLOW)
        CASE(GL_OUT_OF_MEMORY)

        /* OpenGL 1.1 */
        CASE(GL_PROXY_TEXTURE_1D)
        CASE(GL_PROXY_TEXTURE_2D)
        CASE(GL_TEXTURE_PRIORITY)
        CASE(GL_TEXTURE_RESIDENT)
        CASE(GL_TEXTURE_BINDING_1D)
        CASE(GL_TEXTURE_BINDING_2D)
        CASE(GL_ALPHA4)
        CASE(GL_ALPHA8)
        CASE(GL_ALPHA12)
        CASE(GL_ALPHA16)
        CASE(GL_LUMINANCE4)
        CASE(GL_LUMINANCE8)
        CASE(GL_LUMINANCE12)
        CASE(GL_LUMINANCE16)
        CASE(GL_LUMINANCE4_ALPHA4)
        CASE(GL_LUMINANCE6_ALPHA2)
        CASE(GL_LUMINANCE8_ALPHA8)
        CASE(GL_LUMINANCE12_ALPHA4)
        CASE(GL_LUMINANCE12_ALPHA12)
        CASE(GL_LUMINANCE16_ALPHA16)
        CASE(GL_INTENSITY)
        CASE(GL_INTENSITY4)
        CASE(GL_INTENSITY8)
        CASE(GL_INTENSITY12)
        CASE(GL_INTENSITY16)
        CASE(GL_R3_G3_B2)
        CASE(GL_RGB4)
        CASE(GL_RGB5)
        CASE(GL_RGB8)
        CASE(GL_RGB10)
        CASE(GL_RGB12)
        CASE(GL_RGB16)
        CASE(GL_RGBA2)
        CASE(GL_RGBA4)
        CASE(GL_RGB5_A1)
        CASE(GL_RGBA8)
        CASE(GL_RGB10_A2)
        CASE(GL_RGBA12)
        CASE(GL_RGBA16)

        /* Buffers Array */
        CASE(GL_BUFFER_SIZE)
        CASE(GL_BUFFER_USAGE)
        CASE(GL_QUERY_COUNTER_BITS)
        CASE(GL_CURRENT_QUERY)
        CASE(GL_QUERY_RESULT)
        CASE(GL_QUERY_RESULT_AVAILABLE)
        CASE(GL_ARRAY_BUFFER)
        CASE(GL_ELEMENT_ARRAY_BUFFER)
        CASE(GL_ARRAY_BUFFER_BINDING)
        CASE(GL_ELEMENT_ARRAY_BUFFER_BINDING)
        CASE(GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING)
        CASE(GL_READ_ONLY)
        CASE(GL_WRITE_ONLY)
        CASE(GL_READ_WRITE)
        CASE(GL_BUFFER_ACCESS)
        CASE(GL_BUFFER_MAPPED)
        CASE(GL_BUFFER_MAP_POINTER)
        CASE(GL_STREAM_DRAW)
        CASE(GL_STREAM_READ)
        CASE(GL_STREAM_COPY)
        CASE(GL_STATIC_DRAW)
        CASE(GL_STATIC_READ)
        CASE(GL_STATIC_COPY)
        CASE(GL_DYNAMIC_DRAW)
        CASE(GL_DYNAMIC_READ)
        CASE(GL_DYNAMIC_COPY)
        CASE(GL_VERTEX_ARRAY_BUFFER_BINDING)
        CASE(GL_NORMAL_ARRAY_BUFFER_BINDING)
        CASE(GL_COLOR_ARRAY_BUFFER_BINDING)
        CASE(GL_INDEX_ARRAY_BUFFER_BINDING)
        CASE(GL_TEXTURE_COORD_ARRAY_BUFFER_BINDING)
        CASE(GL_EDGE_FLAG_ARRAY_BUFFER_BINDING)
        CASE(GL_SECONDARY_COLOR_ARRAY_BUFFER_BINDING)
        CASE(GL_FOG_COORDINATE_ARRAY_BUFFER_BINDING)
        CASE(GL_WEIGHT_ARRAY_BUFFER_BINDING)
        //        CASE(GL_MAP_READ_BIT)
        //        CASE(GL_MAP_WRITE_BIT)
        CASE(GL_BUFFER_ACCESS_FLAGS)
        CASE(GL_BUFFER_MAP_LENGTH)
        CASE(GL_BUFFER_MAP_OFFSET)
        //        CASE(GL_READ_ONLY)
        //        CASE(GL_WRITE_ONLY)
        //        CASE(GL_READ_WRITE)
        CASE(GL_PIXEL_PACK_BUFFER)
        CASE(GL_PIXEL_UNPACK_BUFFER)
        CASE(GL_PIXEL_UNPACK_BUFFER_BINDING)
        CASE(GL_PIXEL_PACK_BUFFER_BINDING)
        CASE(GL_CURRENT_VERTEX_ATTRIB)
        CASE(GL_MAP_PERSISTENT_BIT)
        CASE(GL_QUERY_BUFFER_BINDING_AMD)
        CASE(GL_COPY_READ_BUFFER)
        CASE(GL_COPY_WRITE_BUFFER)

        //        CASE(GL_READ_BUFFER)
        //        CASE(GL_UNPACK_ROW_LENGTH)
        //        CASE(GL_UNPACK_SKIP_ROWS)
        //        CASE(GL_UNPACK_SKIP_PIXELS)
        //        CASE(GL_PACK_ROW_LENGTH)
        //        CASE(GL_PACK_SKIP_ROWS)
        //        CASE(GL_PACK_SKIP_PIXELS)
        //        CASE(GL_COLOR)
        //        CASE(GL_DEPTH)
        //        CASE(GL_STENCIL)
        //        CASE(GL_RED)
        //        CASE(GL_RGB8)
        //        CASE(GL_RGBA8)
        //        CASE(GL_RGB10_A2)
        CASE(GL_TEXTURE_BINDING_3D)
        CASE(GL_UNPACK_SKIP_IMAGES)
        CASE(GL_UNPACK_IMAGE_HEIGHT)
        CASE(GL_TEXTURE_3D)
        CASE(GL_TEXTURE_WRAP_R)
        CASE(GL_MAX_3D_TEXTURE_SIZE)
        CASE(GL_UNSIGNED_INT_2_10_10_10_REV)
        CASE(GL_MAX_ELEMENTS_VERTICES)
        CASE(GL_MAX_ELEMENTS_INDICES)
        CASE(GL_TEXTURE_MIN_LOD)
        CASE(GL_TEXTURE_MAX_LOD)
        CASE(GL_TEXTURE_BASE_LEVEL)
        CASE(GL_TEXTURE_MAX_LEVEL)
        CASE(GL_MIN)
        CASE(GL_MAX)
        CASE(GL_DEPTH_COMPONENT24)
        CASE(GL_MAX_TEXTURE_LOD_BIAS)
        CASE(GL_TEXTURE_COMPARE_MODE)
        CASE(GL_TEXTURE_COMPARE_FUNC)
        //        CASE(GL_CURRENT_QUERY)
        //        CASE(GL_QUERY_RESULT)
        //        CASE(GL_QUERY_RESULT_AVAILABLE)
        //        CASE(GL_BUFFER_MAPPED)
        //        CASE(GL_BUFFER_MAP_POINTER)
        //        CASE(GL_STREAM_READ)
        //        CASE(GL_STREAM_COPY)
        //        CASE(GL_STATIC_READ)
        //        CASE(GL_STATIC_COPY)
        //        CASE(GL_DYNAMIC_READ)
        //        CASE(GL_DYNAMIC_COPY)
        CASE(GL_MAX_DRAW_BUFFERS)
        CASE(GL_DRAW_BUFFER0)
        CASE(GL_DRAW_BUFFER1)
        CASE(GL_DRAW_BUFFER2)
        CASE(GL_DRAW_BUFFER3)
        CASE(GL_DRAW_BUFFER4)
        CASE(GL_DRAW_BUFFER5)
        CASE(GL_DRAW_BUFFER6)
        CASE(GL_DRAW_BUFFER7)
        CASE(GL_DRAW_BUFFER8)
        CASE(GL_DRAW_BUFFER9)
        CASE(GL_DRAW_BUFFER10)
        CASE(GL_DRAW_BUFFER11)
        CASE(GL_DRAW_BUFFER12)
        CASE(GL_DRAW_BUFFER13)
        CASE(GL_DRAW_BUFFER14)
        CASE(GL_DRAW_BUFFER15)
        CASE(GL_MAX_FRAGMENT_UNIFORM_COMPONENTS)
        CASE(GL_MAX_VERTEX_UNIFORM_COMPONENTS)
        CASE(GL_SAMPLER_3D)
        CASE(GL_SAMPLER_2D_SHADOW)
        CASE(GL_FRAGMENT_SHADER_DERIVATIVE_HINT)
        //        CASE(GL_PIXEL_PACK_BUFFER)
        //        CASE(GL_PIXEL_UNPACK_BUFFER)
        //        CASE(GL_PIXEL_PACK_BUFFER_BINDING)
        //        CASE(GL_PIXEL_UNPACK_BUFFER_BINDING)
        CASE(GL_FLOAT_MAT2x3)
        CASE(GL_FLOAT_MAT2x4)
        CASE(GL_FLOAT_MAT3x2)
        CASE(GL_FLOAT_MAT3x4)
        CASE(GL_FLOAT_MAT4x2)
        CASE(GL_FLOAT_MAT4x3)
        CASE(GL_SRGB)
        CASE(GL_SRGB8)
        CASE(GL_SRGB8_ALPHA8)
        CASE(GL_COMPARE_REF_TO_TEXTURE)
        CASE(GL_MAJOR_VERSION)
        CASE(GL_MINOR_VERSION)
        CASE(GL_NUM_EXTENSIONS)
        CASE(GL_RGBA32F)
        CASE(GL_RGB32F)
        CASE(GL_RGBA16F)
        CASE(GL_RGB16F)
        CASE(GL_VERTEX_ATTRIB_ARRAY_INTEGER)
        CASE(GL_MAX_ARRAY_TEXTURE_LAYERS)
        CASE(GL_MIN_PROGRAM_TEXEL_OFFSET)
        CASE(GL_MAX_PROGRAM_TEXEL_OFFSET)
        CASE(GL_MAX_VARYING_COMPONENTS)
        CASE(GL_TEXTURE_2D_ARRAY)
        CASE(GL_TEXTURE_BINDING_2D_ARRAY)
        CASE(GL_R11F_G11F_B10F)
        CASE(GL_UNSIGNED_INT_10F_11F_11F_REV)
        CASE(GL_RGB9_E5)
        CASE(GL_UNSIGNED_INT_5_9_9_9_REV)
        CASE(GL_TRANSFORM_FEEDBACK_VARYING_MAX_LENGTH)
        CASE(GL_TRANSFORM_FEEDBACK_BUFFER_MODE)
        CASE(GL_MAX_TRANSFORM_FEEDBACK_SEPARATE_COMPONENTS)
        CASE(GL_TRANSFORM_FEEDBACK_VARYINGS)
        CASE(GL_TRANSFORM_FEEDBACK_BUFFER_START)
        CASE(GL_TRANSFORM_FEEDBACK_BUFFER_SIZE)
        CASE(GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN)
        CASE(GL_RASTERIZER_DISCARD)
        CASE(GL_MAX_TRANSFORM_FEEDBACK_INTERLEAVED_COMPONENTS)
        CASE(GL_MAX_TRANSFORM_FEEDBACK_SEPARATE_ATTRIBS)
        CASE(GL_INTERLEAVED_ATTRIBS)
        CASE(GL_SEPARATE_ATTRIBS)
        CASE(GL_TRANSFORM_FEEDBACK_BUFFER)
        CASE(GL_TRANSFORM_FEEDBACK_BUFFER_BINDING)
        CASE(GL_RGBA32UI)
        CASE(GL_RGB32UI)
        CASE(GL_RGBA16UI)
        CASE(GL_RGB16UI)
        CASE(GL_RGBA8UI)
        CASE(GL_RGB8UI)
        CASE(GL_RGBA32I)
        CASE(GL_RGB32I)
        CASE(GL_RGBA16I)
        CASE(GL_RGB16I)
        CASE(GL_RGBA8I)
        CASE(GL_RGB8I)
        CASE(GL_RED_INTEGER)
        CASE(GL_RGB_INTEGER)
        CASE(GL_RGBA_INTEGER)
        CASE(GL_SAMPLER_2D_ARRAY)
        CASE(GL_SAMPLER_2D_ARRAY_SHADOW)
        CASE(GL_SAMPLER_CUBE_SHADOW)
        CASE(GL_UNSIGNED_INT_VEC2)
        CASE(GL_UNSIGNED_INT_VEC3)
        CASE(GL_UNSIGNED_INT_VEC4)
        CASE(GL_INT_SAMPLER_2D)
        CASE(GL_INT_SAMPLER_3D)
        CASE(GL_INT_SAMPLER_CUBE)
        CASE(GL_INT_SAMPLER_2D_ARRAY)
        CASE(GL_UNSIGNED_INT_SAMPLER_2D)
        CASE(GL_UNSIGNED_INT_SAMPLER_3D)
        CASE(GL_UNSIGNED_INT_SAMPLER_CUBE)
        CASE(GL_UNSIGNED_INT_SAMPLER_2D_ARRAY)
        //        CASE(GL_BUFFER_ACCESS_FLAGS)
        //        CASE(GL_BUFFER_MAP_LENGTH)
        //        CASE(GL_BUFFER_MAP_OFFSET)
        CASE(GL_DEPTH_COMPONENT32F)
        CASE(GL_DEPTH32F_STENCIL8)
        CASE(GL_FLOAT_32_UNSIGNED_INT_24_8_REV)
        CASE(GL_FRAMEBUFFER_ATTACHMENT_COLOR_ENCODING)
        CASE(GL_FRAMEBUFFER_ATTACHMENT_COMPONENT_TYPE)
        CASE(GL_FRAMEBUFFER_ATTACHMENT_RED_SIZE)
        CASE(GL_FRAMEBUFFER_ATTACHMENT_GREEN_SIZE)
        CASE(GL_FRAMEBUFFER_ATTACHMENT_BLUE_SIZE)
        CASE(GL_FRAMEBUFFER_ATTACHMENT_ALPHA_SIZE)
        CASE(GL_FRAMEBUFFER_ATTACHMENT_DEPTH_SIZE)
        CASE(GL_FRAMEBUFFER_ATTACHMENT_STENCIL_SIZE)
        CASE(GL_FRAMEBUFFER_DEFAULT)
        CASE(GL_FRAMEBUFFER_UNDEFINED)
        CASE(GL_DEPTH_STENCIL_ATTACHMENT)
        CASE(GL_DEPTH_STENCIL)
        CASE(GL_UNSIGNED_INT_24_8)
        CASE(GL_DEPTH24_STENCIL8)
        CASE(GL_UNSIGNED_NORMALIZED)
        CASE(GL_DRAW_FRAMEBUFFER_BINDING)
        CASE(GL_READ_FRAMEBUFFER)
        CASE(GL_DRAW_FRAMEBUFFER)
        CASE(GL_READ_FRAMEBUFFER_BINDING)
        CASE(GL_RENDERBUFFER_SAMPLES)
        CASE(GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LAYER)
        CASE(GL_MAX_COLOR_ATTACHMENTS)
        CASE(GL_COLOR_ATTACHMENT1)
        CASE(GL_COLOR_ATTACHMENT2)
        CASE(GL_COLOR_ATTACHMENT3)
        CASE(GL_COLOR_ATTACHMENT4)
        CASE(GL_COLOR_ATTACHMENT5)
        CASE(GL_COLOR_ATTACHMENT6)
        CASE(GL_COLOR_ATTACHMENT7)
        CASE(GL_COLOR_ATTACHMENT8)
        CASE(GL_COLOR_ATTACHMENT9)
        CASE(GL_COLOR_ATTACHMENT10)
        CASE(GL_COLOR_ATTACHMENT11)
        CASE(GL_COLOR_ATTACHMENT12)
        CASE(GL_COLOR_ATTACHMENT13)
        CASE(GL_COLOR_ATTACHMENT14)
        CASE(GL_COLOR_ATTACHMENT15)
        CASE(GL_COLOR_ATTACHMENT16)
        CASE(GL_COLOR_ATTACHMENT17)
        CASE(GL_COLOR_ATTACHMENT18)
        CASE(GL_COLOR_ATTACHMENT19)
        CASE(GL_COLOR_ATTACHMENT20)
        CASE(GL_COLOR_ATTACHMENT21)
        CASE(GL_COLOR_ATTACHMENT22)
        CASE(GL_COLOR_ATTACHMENT23)
        CASE(GL_COLOR_ATTACHMENT24)
        CASE(GL_COLOR_ATTACHMENT25)
        CASE(GL_COLOR_ATTACHMENT26)
        CASE(GL_COLOR_ATTACHMENT27)
        CASE(GL_COLOR_ATTACHMENT28)
        CASE(GL_COLOR_ATTACHMENT29)
        CASE(GL_COLOR_ATTACHMENT30)
        CASE(GL_COLOR_ATTACHMENT31)
        CASE(GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE)
        CASE(GL_MAX_SAMPLES)
        CASE(GL_HALF_FLOAT)
        //        CASE(GL_MAP_READ_BIT)
        //        CASE(GL_MAP_WRITE_BIT)
        //        CASE(GL_MAP_INVALIDATE_RANGE_BIT)
        //        CASE(GL_MAP_INVALIDATE_BUFFER_BIT)
        CASE(GL_MAP_FLUSH_EXPLICIT_BIT)
        CASE(GL_MAP_UNSYNCHRONIZED_BIT)
        CASE(GL_RG)
        CASE(GL_RG_INTEGER)
        CASE(GL_R8)
        CASE(GL_RG8)
        CASE(GL_R16F)
        CASE(GL_R32F)
        CASE(GL_RG16F)
        CASE(GL_RG32F)
        CASE(GL_R8I)
        CASE(GL_R8UI)
        CASE(GL_R16I)
        CASE(GL_R16UI)
        CASE(GL_R32I)
        CASE(GL_R32UI)
        CASE(GL_RG8I)
        CASE(GL_RG8UI)
        CASE(GL_RG16I)
        CASE(GL_RG16UI)
        CASE(GL_RG32I)
        CASE(GL_RG32UI)
        CASE(GL_VERTEX_ARRAY_BINDING)
        CASE(GL_R8_SNORM)
        CASE(GL_RG8_SNORM)
        CASE(GL_RGB8_SNORM)
        CASE(GL_RGBA8_SNORM)
        CASE(GL_SIGNED_NORMALIZED)
        CASE(GL_PRIMITIVE_RESTART_FIXED_INDEX)
        //        CASE(GL_COPY_READ_BUFFER)
        //        CASE(GL_COPY_WRITE_BUFFER)
        //        CASE(GL_COPY_READ_BUFFER_BINDING)
        //        CASE(GL_COPY_WRITE_BUFFER_BINDING)
        CASE(GL_UNIFORM_BUFFER)
        CASE(GL_UNIFORM_BUFFER_BINDING)
        CASE(GL_UNIFORM_BUFFER_START)
        CASE(GL_UNIFORM_BUFFER_SIZE)
        CASE(GL_MAX_VERTEX_UNIFORM_BLOCKS)
        CASE(GL_MAX_FRAGMENT_UNIFORM_BLOCKS)
        CASE(GL_MAX_COMBINED_UNIFORM_BLOCKS)
        CASE(GL_MAX_UNIFORM_BUFFER_BINDINGS)
        CASE(GL_MAX_UNIFORM_BLOCK_SIZE)
        CASE(GL_MAX_COMBINED_VERTEX_UNIFORM_COMPONENTS)
        CASE(GL_MAX_COMBINED_FRAGMENT_UNIFORM_COMPONENTS)
        CASE(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT)
        CASE(GL_ACTIVE_UNIFORM_BLOCK_MAX_NAME_LENGTH)
        CASE(GL_ACTIVE_UNIFORM_BLOCKS)
        CASE(GL_UNIFORM_TYPE)
        CASE(GL_UNIFORM_SIZE)
        CASE(GL_UNIFORM_NAME_LENGTH)
        CASE(GL_UNIFORM_BLOCK_INDEX)
        CASE(GL_UNIFORM_OFFSET)
        CASE(GL_UNIFORM_ARRAY_STRIDE)
        CASE(GL_UNIFORM_MATRIX_STRIDE)
        CASE(GL_UNIFORM_IS_ROW_MAJOR)
        CASE(GL_UNIFORM_BLOCK_BINDING)
        CASE(GL_UNIFORM_BLOCK_DATA_SIZE)
        CASE(GL_UNIFORM_BLOCK_NAME_LENGTH)
        CASE(GL_UNIFORM_BLOCK_ACTIVE_UNIFORMS)
        CASE(GL_UNIFORM_BLOCK_ACTIVE_UNIFORM_INDICES)
        CASE(GL_UNIFORM_BLOCK_REFERENCED_BY_VERTEX_SHADER)
        CASE(GL_UNIFORM_BLOCK_REFERENCED_BY_FRAGMENT_SHADER)
        CASE(GL_INVALID_INDEX)
        CASE(GL_MAX_VERTEX_OUTPUT_COMPONENTS)
        CASE(GL_MAX_FRAGMENT_INPUT_COMPONENTS)
        CASE(GL_MAX_SERVER_WAIT_TIMEOUT)
        CASE(GL_OBJECT_TYPE)
        CASE(GL_SYNC_CONDITION)
        CASE(GL_SYNC_STATUS)
        CASE(GL_SYNC_FLAGS)
        CASE(GL_SYNC_FENCE)
        CASE(GL_SYNC_GPU_COMMANDS_COMPLETE)
        CASE(GL_UNSIGNALED)
        CASE(GL_SIGNALED)
        CASE(GL_ALREADY_SIGNALED)
        CASE(GL_TIMEOUT_EXPIRED)
        CASE(GL_CONDITION_SATISFIED)
        CASE(GL_WAIT_FAILED)
        //        CASE(GL_SYNC_FLUSH_COMMANDS_BIT)
        //        CASE(GL_TIMEOUT_IGNORED)
        CASE(GL_VERTEX_ATTRIB_ARRAY_DIVISOR)
        CASE(GL_ANY_SAMPLES_PASSED)
        CASE(GL_ANY_SAMPLES_PASSED_CONSERVATIVE)
        CASE(GL_SAMPLER_BINDING)
        CASE(GL_RGB10_A2UI)
        CASE(GL_TEXTURE_SWIZZLE_R)
        CASE(GL_TEXTURE_SWIZZLE_G)
        CASE(GL_TEXTURE_SWIZZLE_B)
        CASE(GL_TEXTURE_SWIZZLE_A)
        //        CASE(GL_GREEN)
        //        CASE(GL_BLUE)
        CASE(GL_INT_2_10_10_10_REV)
        CASE(GL_TRANSFORM_FEEDBACK)
        CASE(GL_TRANSFORM_FEEDBACK_PAUSED)
        CASE(GL_TRANSFORM_FEEDBACK_ACTIVE)
        CASE(GL_TRANSFORM_FEEDBACK_BINDING)
        CASE(GL_PROGRAM_BINARY_RETRIEVABLE_HINT)
        CASE(GL_PROGRAM_BINARY_LENGTH)
        CASE(GL_NUM_PROGRAM_BINARY_FORMATS)
        CASE(GL_PROGRAM_BINARY_FORMATS)
        CASE(GL_COMPRESSED_R11_EAC)
        CASE(GL_COMPRESSED_SIGNED_R11_EAC)
        CASE(GL_COMPRESSED_RG11_EAC)
        CASE(GL_COMPRESSED_SIGNED_RG11_EAC)
        CASE(GL_COMPRESSED_RGB8_ETC2)
        CASE(GL_COMPRESSED_SRGB8_ETC2)
        CASE(GL_COMPRESSED_RGB8_PUNCHTHROUGH_ALPHA1_ETC2)
        CASE(GL_COMPRESSED_SRGB8_PUNCHTHROUGH_ALPHA1_ETC2)
        CASE(GL_COMPRESSED_RGBA8_ETC2_EAC)
        CASE(GL_COMPRESSED_SRGB8_ALPHA8_ETC2_EAC)
        CASE(GL_TEXTURE_IMMUTABLE_FORMAT)
        CASE(GL_MAX_ELEMENT_INDEX)
        CASE(GL_NUM_SAMPLE_COUNTS)
        CASE(GL_TEXTURE_IMMUTABLE_LEVELS)

        CASE(GL_TEXTURE_RECTANGLE)
        CASE(GL_TEXTURE_CUBE_MAP_ARRAY)

        CASE(GL_BGR)
        CASE(GL_BGRA)
        CASE(GL_GREEN_INTEGER)
        CASE(GL_BLUE_INTEGER)
        CASE(GL_BGR_INTEGER)
        CASE(GL_BGRA_INTEGER)

        /*
         * OpenGL 1.3
         */

        /* multitexture */
        CASE(GL_TEXTURE0)
        CASE(GL_TEXTURE1)
        CASE(GL_TEXTURE2)
        CASE(GL_TEXTURE3)
        CASE(GL_TEXTURE4)
        CASE(GL_TEXTURE5)
        CASE(GL_TEXTURE6)
        CASE(GL_TEXTURE7)
        CASE(GL_TEXTURE8)
        CASE(GL_TEXTURE9)
        CASE(GL_TEXTURE10)
        CASE(GL_TEXTURE11)
        CASE(GL_TEXTURE12)
        CASE(GL_TEXTURE13)
        CASE(GL_TEXTURE14)
        CASE(GL_TEXTURE15)
        CASE(GL_TEXTURE16)
        CASE(GL_TEXTURE17)
        CASE(GL_TEXTURE18)
        CASE(GL_TEXTURE19)
        CASE(GL_TEXTURE20)
        CASE(GL_TEXTURE21)
        CASE(GL_TEXTURE22)
        CASE(GL_TEXTURE23)
        CASE(GL_TEXTURE24)
        CASE(GL_TEXTURE25)
        CASE(GL_TEXTURE26)
        CASE(GL_TEXTURE27)
        CASE(GL_TEXTURE28)
        CASE(GL_TEXTURE29)
        CASE(GL_TEXTURE30)
        CASE(GL_TEXTURE31)
        CASE(GL_ACTIVE_TEXTURE)
        CASE(GL_CLIENT_ACTIVE_TEXTURE)
        CASE(GL_MAX_TEXTURE_UNITS)
        /* texture_cube_map */
        CASE(GL_NORMAL_MAP)
        CASE(GL_REFLECTION_MAP)
        CASE(GL_TEXTURE_CUBE_MAP)
        CASE(GL_TEXTURE_BINDING_CUBE_MAP)
        CASE(GL_TEXTURE_CUBE_MAP_POSITIVE_X)
        CASE(GL_TEXTURE_CUBE_MAP_NEGATIVE_X)
        CASE(GL_TEXTURE_CUBE_MAP_POSITIVE_Y)
        CASE(GL_TEXTURE_CUBE_MAP_NEGATIVE_Y)
        CASE(GL_TEXTURE_CUBE_MAP_POSITIVE_Z)
        CASE(GL_TEXTURE_CUBE_MAP_NEGATIVE_Z)
        CASE(GL_PROXY_TEXTURE_CUBE_MAP)
        CASE(GL_MAX_CUBE_MAP_TEXTURE_SIZE)
        /* texture_compression */
        CASE(GL_COMPRESSED_ALPHA)
        CASE(GL_COMPRESSED_LUMINANCE)
        CASE(GL_COMPRESSED_LUMINANCE_ALPHA)
        CASE(GL_COMPRESSED_INTENSITY)
        CASE(GL_COMPRESSED_RGB)
        CASE(GL_COMPRESSED_RGBA)
        CASE(GL_TEXTURE_COMPRESSION_HINT)
        CASE(GL_TEXTURE_COMPRESSED_IMAGE_SIZE)
        CASE(GL_TEXTURE_COMPRESSED)
        CASE(GL_NUM_COMPRESSED_TEXTURE_FORMATS)
        CASE(GL_COMPRESSED_TEXTURE_FORMATS)
        /* multisample */
        CASE(GL_MULTISAMPLE)
        CASE(GL_SAMPLE_ALPHA_TO_COVERAGE)
        CASE(GL_SAMPLE_ALPHA_TO_ONE)
        CASE(GL_SAMPLE_COVERAGE)
        CASE(GL_SAMPLE_BUFFERS)
        CASE(GL_SAMPLES)
        CASE(GL_SAMPLE_COVERAGE_VALUE)
        CASE(GL_SAMPLE_COVERAGE_INVERT)
        CASE(GL_MULTISAMPLE_BIT)
        /* transpose_matrix */
        CASE(GL_TRANSPOSE_MODELVIEW_MATRIX)
        CASE(GL_TRANSPOSE_PROJECTION_MATRIX)
        CASE(GL_TRANSPOSE_TEXTURE_MATRIX)
        CASE(GL_TRANSPOSE_COLOR_MATRIX)
        /* texture_env_combine */
        CASE(GL_COMBINE)
        CASE(GL_COMBINE_RGB)
        CASE(GL_COMBINE_ALPHA)
        CASE(GL_SOURCE0_RGB)
        CASE(GL_SOURCE1_RGB)
        CASE(GL_SOURCE2_RGB)
        CASE(GL_SOURCE0_ALPHA)
        CASE(GL_SOURCE1_ALPHA)
        CASE(GL_SOURCE2_ALPHA)
        CASE(GL_OPERAND0_RGB)
        CASE(GL_OPERAND1_RGB)
        CASE(GL_OPERAND2_RGB)
        CASE(GL_OPERAND0_ALPHA)
        CASE(GL_OPERAND1_ALPHA)
        CASE(GL_OPERAND2_ALPHA)
        CASE(GL_RGB_SCALE)
        CASE(GL_ADD_SIGNED)
        CASE(GL_INTERPOLATE)
        CASE(GL_SUBTRACT)
        CASE(GL_CONSTANT)
        CASE(GL_PRIMARY_COLOR)
        CASE(GL_PREVIOUS)
        /* texture_env_dot3 */
        CASE(GL_DOT3_RGB)
        CASE(GL_DOT3_RGBA)
        /* texture_border_clamp */
        CASE(GL_CLAMP_TO_BORDER)
        /*
         * Miscellaneous
         */
    default:
        sprintf(str, "0x%x", e);
        return str;
    }
}

constexpr std::string_view mg_shader_header = "#version 300 es\n"
                                               "// MobileGlues FPE Shader\n"
                                               "#ifdef GL_ES\n"
                                               "precision highp float;\n"
                                               "precision highp int;\n"
                                               "#endif\n";
constexpr std::string_view mg_vs_header = "// ** Vertex Shader **\n";
constexpr std::string_view mg_fs_header = "// ** Fragment Shader **\n";
constexpr std::string_view mg_fog_linear_func = "float fog_linear(float distance, float start, float end) {\n"
                                                "    return clamp((end - distance) / max(end - start, 1e-6), 0., 1.);\n"
                                                "}\n";
constexpr std::string_view mg_fog_exp_func = "float fog_exp(float distance, float density) {\n"
                                             "    return clamp(exp(-density * distance), 0., 1.);\n"
                                             "}\n";
constexpr std::string_view mg_fog_exp2_func = "float fog_exp2(float distance, float density) {\n"
                                              "    float scaled = density * distance;\n"
                                              "    return clamp(exp(-scaled * scaled), 0., 1.);\n"
                                              "}\n";
constexpr std::string_view mg_fog_apply_fog_func = "vec3 apply_fog(vec3 objColor, vec3 fogColor, float fogFactor) {\n"
                                                   "    return mix(fogColor, objColor, fogFactor);\n"
                                                   "}\n";
constexpr std::string_view mg_fog_uniforms = "uniform vec4 FogColor;\n"
                                             "uniform float FogDensity;\n"
                                             "uniform float FogStart;\n"
                                             "uniform float FogEnd;\n";

constexpr std::string_view mg_alpharef_uniform = "uniform float alpharef;\n";

const std::string alpha_test(GLenum func, const std::string_view varname, const std::string_view alpharef) {
    constexpr std::string_view fmt = R"(
    // Alpha Test, func = {}
    if (!({}.a {} {})) {{
        discard;
    }}
)";

    constexpr std::string_view fmt_eq = R"(
    // Alpha Test, func = GL_EQUAL
    if (abs({0}.a - {1}) > 0.00001) {{
        discard;
    }}
)";

    constexpr std::string_view fmt_neq = R"(
    // Alpha Test, func = GL_NOTEQUAL
    if (abs({0}.a - {1}) <= 0.00001) {{
        discard;
    }}
)";

    switch (func) {
    case GL_NEVER:
        return "    // Alpha Test\n"
               "    discard;\n";
    case GL_LESS:
        return std::format(fmt, glEnumToString(func), varname, "<", alpharef);
    case GL_EQUAL:
        return std::format(fmt_eq, varname, alpharef);
    case GL_LEQUAL:
        return std::format(fmt, glEnumToString(func), varname, "<=", alpharef);
    case GL_GREATER:
        return std::format(fmt, glEnumToString(func), varname, ">", alpharef);
    case GL_NOTEQUAL:
        return std::format(fmt_neq, varname, alpharef);
    case GL_GEQUAL:
        return std::format(fmt, glEnumToString(func), varname, ">=", alpharef);
    case GL_ALWAYS:
        return "    // Alpha Test\n"
               "    // GL_ALWAYS\n";
    }
    return std::string("    ALPHA TEST ERROR: unknown func: ") + glEnumToString(func);
}

std::string vp2in_name(GLenum vp, int index) {
    switch (vp) {
    case GL_VERTEX_ARRAY:
        return "Position";
    case GL_NORMAL_ARRAY:
        return "Normal";
    case GL_COLOR_ARRAY:
        return "Color";
    case GL_INDEX_ARRAY:
        return "Index";
        //        case GL_EDGE_FLAG_ARRAY:
        //            return "EdgeFlag";
    case GL_FOG_COORD_ARRAY:
        return "FogCoord";
    case GL_SECONDARY_COLOR_ARRAY:
        return "SecColor";
    default: {
        int texidx = index - 7;
        if (texidx >= 0 && texidx < GL_MAX_TEXTURE_IMAGE_UNITS)
            return "UV" + std::to_string(texidx);
        else
            break;
    }
    }
    // LOG_E("ERROR: 1280 %s(%s, %d)", __func__, glEnumToString(vp), index)
    return "ERROR";
}

std::string vp2out_name(GLenum vp, int index) {
    switch (vp) {
    case GL_VERTEX_ARRAY:
        return "Position";
    case GL_NORMAL_ARRAY:
        return "vertexNormal";
    case GL_COLOR_ARRAY:
        return "vertexColor";
    case GL_INDEX_ARRAY:
        return "vertexIndex";
        //        case GL_EDGE_FLAG_ARRAY:
        //            return "vertexEdgeFlag";
    case GL_FOG_COORD_ARRAY:
        return "vertexFogCoord";
    case GL_SECONDARY_COLOR_ARRAY:
        return "vertexSecColor";
    default: {
        int texidx = index - 7;
        if (texidx >= 0 && texidx < GL_MAX_TEXTURE_IMAGE_UNITS)
            return "texCoord" + std::to_string(texidx);
        else
            break;
    }
    }
    // LOG_E("ERROR: 1280 %s(%s, %d)", __func__, glEnumToString(vp), index)
    return "ERROR";
}

// TODO: deal with integer flat qualifier
std::string type2str(GLenum type, int size) {
    if (size == 1) {
        switch (type) {
        case GL_UNSIGNED_BYTE:
        case GL_UNSIGNED_INT:
            //                return "uint";
        case GL_BYTE:
        case GL_SHORT:
        case GL_INT:
            //                return "int";
        case GL_FLOAT:
            return "float";
        case GL_DOUBLE:
            return "double";
        default:
            return "ERROR";
        }
    } else {
        switch (type) {
        case GL_UNSIGNED_BYTE:
        case GL_UNSIGNED_INT:
            //                return "uvec" + std::to_string(size);
        case GL_BYTE:
        case GL_SHORT:
        case GL_INT:
            //                return "ivec" + std::to_string(size);
        case GL_FLOAT:
            return "vec" + std::to_string(size);
        case GL_DOUBLE:
            return "dvec" + std::to_string(size);
        default:
            return "ERROR";
        }
    }
}

int texture_unit_from_attribute(int attribute_index) {
    const int unit = attribute_index - 7;
    // Texture coordinate arrays occupy slots 7+n. Use the slot instead of the
    // synthetic usage tag: GL_TEXTURE_COORD_ARRAY+n overlaps real desktop
    // enums, and immediate mode deliberately tags every unit with the base enum.
    return unit >= 0 && unit < MAX_TEX ? unit : -1;
}

void add_vs_inout(const fixed_function_state_t& state, scratch_t& scratch, std::string& vs) {
    auto& vpa = state.normalized_vpa;
    // LOG_D("[shadergen] enabled_ptr: 0x%x", vpa.enabled_pointers)
#if DEBUG || GLOBAL_DEBUG
    vs += std::format("// enabled_ptr: 0x{:x}\n", vpa.enabled_pointers);
#endif
    for (int i = 0; i < VERTEX_POINTER_COUNT; ++i) {
        bool enabled = ((vpa.enabled_pointers >> i) & 1);

        if (enabled || state.fpe_draw.current_data.sizes.data[i] > 0) {
            auto& vp = vpa.attributes[i];

            if (enabled) { // LOG_D("attrib #%d, cidx #%u: type = %s, size = %d, stride = %d, usage = %s, ptr = %p", i,
                           // vpa.cidx(i),
                           //      glEnumToString(vp.type), vp.size, vp.stride, glEnumToString(vp.usage), vp.pointer)
            } else {
                // LOG_D("attrib #%d, cidx #%u: type = %s, usage = %s, size = %d (disabled)", i, vpa.cidx(i),
                //         glEnumToString(vp.type), glEnumToString(vp.usage), state.fpe_draw.current_data.sizes.data[i])
            }

            const GLenum usage = enabled ? vp.usage : idx2vp(i);
            const int texid = texture_unit_from_attribute(i);
            std::string in_name = vp2in_name(usage, i);
            // Generic vertex attributes fill missing components with (0, 0, 1).
            // A vec4 shader input therefore handles legacy 2/3/4 component
            // positions, colors (including alpha=1), and texture coordinates.
            const bool needs_default_components =
                usage == GL_VERTEX_ARRAY || usage == GL_COLOR_ARRAY || texid >= 0;
            std::string type;
            if (usage == GL_NORMAL_ARRAY)
                type = "vec3";
            else if (needs_default_components)
                type = "vec4";
            else
                type = enabled ? type2str(vp.type, vp.size) : type2str(GL_FLOAT, 4);

            vs += std::format("layout (location = {}) in {} {};\n", vpa.cidx(i), type, in_name);

            if (usage == GL_VERTEX_ARRAY) { // GL_VERTEX_ARRAY will be written into gl_Position
                continue;
            }

            std::string out_name = vp2out_name(usage, i);
            std::string linkage;

            linkage += type;
            linkage += ' ';
            linkage += out_name;
            linkage += ";\n";

            vs += "out ";
            vs += linkage;

            scratch.last_stage_linkage += "in " + linkage;

            // TODO: Fog / vertex lighting. Texture coordinates first pass
            // through the matrix belonging to their server texture unit.
            if (texid >= 0) {
                scratch.vs_body += std::format("    {} = TexMat{} * {};\n", out_name, texid, in_name);
                // LOG_D("has_texcoord[%d] = true", texid)
                scratch.has_texcoord[texid] = true;
            } else {
                scratch.vs_body += std::format("    {} = {};\n", out_name, in_name);
            }

            if (usage == GL_COLOR_ARRAY) {
                scratch.has_color_input = true;
                scratch.has_vertex_color = true;
            }
            if (usage == GL_NORMAL_ARRAY) scratch.has_normal_input = true;
        }
    }

    if (state.fpe_bools.lighting_enable && !scratch.has_vertex_color) {
        vs += "out vec4 vertexColor;\n";
        scratch.last_stage_linkage += "in vec4 vertexColor;\n";
        scratch.has_vertex_color = true;
    }
    if (state.fpe_bools.lighting_enable && state.light_model_two_side) {
        vs += "out vec4 vertexBackColor;\n";
        scratch.last_stage_linkage += "in vec4 vertexBackColor;\n";
        scratch.has_back_vertex_color = true;
    }

    if (state.fpe_bools.fog_enable) {
        vs += "out vec3 vViewPosition;\n";
    }
}

void add_vs_uniforms(const fixed_function_state_t& state, scratch_t& scratch, std::string& vs) {
    // Transformation matrix
    vs += "uniform mat4 ModelViewProjMat;\n";
    if (state.fpe_bools.fog_enable) {
        vs += "uniform mat4 ModelViewMat;\n";
    }
    if (state.fpe_bools.lighting_enable) {
        vs += "uniform mat3 NormalMat;\n"
              "uniform vec4 LightModelAmbient;\n"
              "uniform vec4 FrontMaterialAmbient;\n"
              "uniform vec4 FrontMaterialDiffuse;\n"
              "uniform vec4 FrontMaterialEmission;\n";
        if (state.light_model_two_side) {
            vs += "uniform vec4 BackMaterialAmbient;\n"
                  "uniform vec4 BackMaterialDiffuse;\n"
                  "uniform vec4 BackMaterialEmission;\n";
        }
        for (int i = 0; i < MAX_LIGHTS; ++i) {
            if (!state.fpe_bools.light_enable[i]) continue;
            vs += std::format("uniform vec4 Light{}Ambient;\n"
                              "uniform vec4 Light{}Diffuse;\n"
                              "uniform vec4 Light{}Position;\n",
                              i, i, i);
        }
    }
    for (int i = 0; i < MAX_TEX; ++i) {
        if (scratch.has_texcoord[i]) {
            vs += std::format("uniform mat4 TexMat{};\n", i);
        }
    }
}

bool color_material_applies(const fixed_function_state_t& state, GLenum face) {
    if (!state.fpe_bools.color_material_enable) return false;
    return state.color_material_face == face || state.color_material_face == GL_FRONT_AND_BACK;
}

void add_color_material(const fixed_function_state_t& state, GLenum face, const std::string& prefix,
                        std::string& vs) {
    if (!color_material_applies(state, face)) return;

    switch (state.color_material_mode) {
    case GL_AMBIENT:
        vs += std::format("    {}Ambient = incomingColor;\n", prefix);
        break;
    case GL_DIFFUSE:
        vs += std::format("    {}Diffuse = incomingColor;\n", prefix);
        break;
    case GL_EMISSION:
        vs += std::format("    {}Emission = incomingColor;\n", prefix);
        break;
    case GL_AMBIENT_AND_DIFFUSE:
        vs += std::format("    {}Ambient = incomingColor;\n"
                          "    {}Diffuse = incomingColor;\n",
                          prefix, prefix);
        break;
    default:
        // Specular color material is retained in state, but this compatibility
        // shader intentionally implements the ambient/diffuse vanilla path.
        break;
    }
}

void add_lighting_calculation(const fixed_function_state_t& state, const std::string& prefix,
                              const std::string& normal, const std::string& output, std::string& vs) {
    vs += std::format(
        "    vec3 {0}Lit = ({0}Emission + LightModelAmbient * {0}Ambient).rgb;\n", prefix);
    for (int i = 0; i < MAX_LIGHTS; ++i) {
        if (!state.fpe_bools.light_enable[i]) continue;
        vs += std::format(
            "    vec3 {2}LightDirection{0} = normalize(Light{0}Position.xyz);\n"
            "    float {2}DiffuseFactor{0} = max(dot({1}, {2}LightDirection{0}), 0.0);\n"
            "    {2}Lit += (Light{0}Ambient * {2}Ambient).rgb;\n"
            "    {2}Lit += {2}DiffuseFactor{0} * (Light{0}Diffuse * {2}Diffuse).rgb;\n",
            i, normal, prefix);
    }
    vs += std::format("    {} = vec4(clamp({}Lit, 0.0, 1.0), clamp({}Diffuse.a, 0.0, 1.0));\n",
                      output, prefix, prefix);
}

void add_vs_body(const fixed_function_state_t& state, scratch_t& scratch, std::string& vs) {
    vs += "void main() {\n"
          //            "   gl_Position = ProjMat * ModelViewMat * vec4(Position, 1.0);\n";
          "    gl_Position = ModelViewProjMat * Position;\n";
    if (state.fpe_bools.fog_enable) {
        vs += "    vec4 viewPosition = ModelViewMat * Position;\n"
              "    vViewPosition = viewPosition.xyz;\n";
    }
    vs += scratch.vs_body;

    if (state.fpe_bools.lighting_enable) {
        vs += scratch.has_color_input ? "    vec4 incomingColor = Color;\n"
                                      : "    vec4 incomingColor = vec4(1.0);\n";
        vs += scratch.has_normal_input ? "    vec3 transformedNormal = NormalMat * Normal;\n"
                                       : "    vec3 transformedNormal = NormalMat * vec3(0.0, 0.0, 1.0);\n";
        if (state.fpe_bools.normalize_enable || state.fpe_bools.rescale_normal_enable) {
            // RESCALE_NORMAL is defined for uniform ModelView scales. A full
            // normalization produces the same result in that defined case and
            // remains stable for the item-render transforms used by Minecraft.
            vs += "    transformedNormal = normalize(transformedNormal);\n";
        }

        vs += "    vec4 frontAmbient = FrontMaterialAmbient;\n"
              "    vec4 frontDiffuse = FrontMaterialDiffuse;\n"
              "    vec4 frontEmission = FrontMaterialEmission;\n";
        add_color_material(state, GL_FRONT, "front", vs);
        add_lighting_calculation(state, "front", "transformedNormal", "vertexColor", vs);

        if (state.light_model_two_side) {
            vs += "    vec4 backAmbient = BackMaterialAmbient;\n"
                  "    vec4 backDiffuse = BackMaterialDiffuse;\n"
                  "    vec4 backEmission = BackMaterialEmission;\n";
            add_color_material(state, GL_BACK, "back", vs);
            add_lighting_calculation(state, "back", "-transformedNormal", "vertexBackColor", vs);
        }
    }
    vs += "}\n";
}

void add_fs_uniforms(const fixed_function_state_t& state, scratch_t& scratch, std::string& fs) {
    for (int i = 0; i < MAX_TEX; ++i) {
        if (state.fpe_bools.texture_2d_enable[i]) {
            fs += std::format("uniform sampler2D Sampler{};\n", i);
            if (state.texture_env_mode[i] == GL_BLEND) {
                fs += std::format("uniform vec4 TexEnvColor{};\n", i);
            }
        }
    }

    if (state.fpe_bools.fog_enable) {
        fs += mg_fog_uniforms;
    }

    if (state.fpe_bools.alpha_test_enable) {
        fs += mg_alpharef_uniform;
    }
}

void add_fs_inout(const fixed_function_state_t& state, scratch_t& scratch, std::string& fs) {
    // Linking from VS
    fs += scratch.last_stage_linkage;
    fs += "\n";
    if (state.fpe_bools.fog_enable) {
        fs += "in vec3 vViewPosition;\n";
    }
    fs += "out vec4 FragColor;\n";
}

void add_fs_body(const fixed_function_state_t& state, scratch_t& scratch, std::string& fs) {
    // Fog function
    if (state.fpe_bools.fog_enable) {
        fs += mg_fog_apply_fog_func;
        switch (state.fog_mode) {
        case GL_LINEAR:
            fs += mg_fog_linear_func;
            break;
        case GL_EXP:
            fs += mg_fog_exp_func;
            break;
        case GL_EXP2:
            fs += mg_fog_exp2_func;
            break;
        }
    }

    // TODO: Replace this hardcode with something better...
    fs += "void main() {\n";

    if (scratch.has_back_vertex_color)
        fs += "    vec4 color = gl_FrontFacing ? vertexColor : vertexBackColor;\n";
    else if (scratch.has_vertex_color)
        fs += "    vec4 color = vertexColor;\n";
    else
        fs += "    vec4 color = vec4(1., 1., 1., 1.);\n";

    for (int i = 0; i < MAX_TEX; ++i) {
        if (!state.fpe_bools.texture_2d_enable[i]) continue;

        const std::string coord =
            scratch.has_texcoord[i] ? std::format("texCoord{}.xy", i) : "vec2(0.0)";
        fs += std::format("\n"
                          "    // Texturing #{0}\n"
                          "    vec4 texcolor{0} = texture(Sampler{0}, {1});\n",
                          i, coord);

        switch (state.texture_env_mode[i]) {
        case GL_REPLACE:
            fs += std::format("    color = texcolor{};\n", i);
            break;
        case GL_DECAL:
            fs += std::format("    color.rgb = mix(color.rgb, texcolor{0}.rgb, texcolor{0}.a);\n", i);
            break;
        case GL_BLEND:
            fs += std::format(
                "    color.rgb = mix(color.rgb, TexEnvColor{0}.rgb, texcolor{0}.rgb);\n"
                "    color.a *= texcolor{0}.a;\n",
                i);
            break;
        case GL_ADD:
            fs += std::format("    color.rgb += texcolor{0}.rgb;\n"
                              "    color.a *= texcolor{0}.a;\n",
                              i);
            break;
        case GL_COMBINE:
            // Minecraft's fixed-function lightmap config uses COMBINE with
            // MODULATE for texture and previous color.
        case GL_MODULATE:
        default:
            fs += std::format("    color *= texcolor{};\n", i);
            break;
        }
    }

    // Alpha test
    if (state.fpe_bools.alpha_test_enable)
        fs += alpha_test(state.alpha_func, "color", "alpharef");
    else
        fs += "    // Alpha Test\n"
              "    // (Disabled)\n";

    // Fog calculation
    if (state.fpe_bools.fog_enable) {
        // Fixed-function fog uses eye-space depth by default. Radial distance
        // is only selected through the optional NV fog-distance extension.
        fs += "    float distance = abs(vViewPosition.z);\n";
        switch (state.fog_mode) {
        case GL_LINEAR:
            fs += "    float fogFactor = fog_linear(distance, FogStart, FogEnd);\n";
            break;
        case GL_EXP:
            fs += "    float fogFactor = fog_exp(distance, FogDensity);\n";
            break;
        case GL_EXP2:
            fs += "    float fogFactor = fog_exp2(distance, FogDensity);\n";
            break;
        }
        fs += "    color.rgb = apply_fog(color.rgb, FogColor.rgb, fogFactor);\n";
        //        fs += "    color = vec4(fogFactor, fogFactor, fogFactor, 1.);\n";
    }

    fs += "   FragColor = color;\n"
          "}";
}

program_t fpe_shader_generator::generate_program() {
    program_t program;

    program.vs = vertex_shader(state_, scratch_);
    program.fs = fragment_shader(state_, scratch_);

    return program;
}

std::string fpe_shader_generator::vertex_shader(const fixed_function_state_t& state, scratch_t& scratch) {
    std::string shader;
    shader += mg_shader_header;
    shader += mg_vs_header;

    shader += "\n";
    add_vs_inout(state, scratch, shader);
    shader += "\n";
    add_vs_uniforms(state, scratch, shader);
    shader += "\n";
    add_vs_body(state, scratch, shader);

    return shader;
}

std::string fpe_shader_generator::fragment_shader(const fixed_function_state_t& state, scratch_t& scratch) {
    std::string shader;

    shader += mg_shader_header;
    shader += mg_fs_header;

    shader += "\n";
    add_fs_inout(state, scratch, shader);
    shader += "\n";
    add_fs_uniforms(state, scratch, shader);
    shader += "\n";
    add_fs_body(state, scratch, shader);

    return shader;
}

int program_t::get_program() {
    if (compile_attempted) return program;
    compile_attempted = true;

    int vss = compile_shader(GL_VERTEX_SHADER, vs.c_str());
    if (vss < 0) return 0;
    int fss = compile_shader(GL_FRAGMENT_SHADER, fs.c_str());
    if (fss < 0) {
        g_glFuncs.glDeleteShader(vss);
        return 0;
    }
    program = link_program(vss, fss);
    g_glFuncs.glDeleteShader(vss);
    g_glFuncs.glDeleteShader(fss);
    if (program < 0) program = 0;
    return program;
}

int program_t::compile_shader(GLenum shader_type, const char* src) {
    char compile_info[4096] = {};

    int shader = g_glFuncs.glCreateShader(shader_type);
    if (shader == 0) return -1;

    g_glFuncs.glShaderSource(shader, 1, &src, NULL);

    g_glFuncs.glCompileShader(shader);

    int success = 0;
    g_glFuncs.glGetShaderiv(shader, GL_COMPILE_STATUS, &success);

    if (!success) {
        g_glFuncs.glGetShaderInfoLog(shader, sizeof(compile_info), NULL, compile_info);

        printf("[FPE] %s shader compile error: %s\nShader source:\n%s\n",
               (shader_type == GL_VERTEX_SHADER) ? "vertex" : "fragment", compile_info, src);
        g_glFuncs.glDeleteShader(shader);
#if DEBUG || GLOBAL_DEBUG
        abort();
#endif
        return -1;
    }

    return shader;
}

int program_t::link_program(GLuint vs, GLuint fs) {
    char compile_info[4096] = {};

    int program = g_glFuncs.glCreateProgram();
    if (program == 0) return -1;

    g_glFuncs.glAttachShader(program, vs);

    g_glFuncs.glAttachShader(program, fs);

    g_glFuncs.glLinkProgram(program);

    int success = 0;
    g_glFuncs.glGetProgramiv(program, GL_LINK_STATUS, &success);

    if (!success) {
        g_glFuncs.glGetProgramInfoLog(program, sizeof(compile_info), NULL, compile_info);

        printf("[FPE] program link error: %s\n", compile_info);
        g_glFuncs.glDeleteProgram(program);
#if DEBUG || GLOBAL_DEBUG
        abort();
#endif
        return -1;
    }
    // LOG_E("program link success");
    return program;
}
