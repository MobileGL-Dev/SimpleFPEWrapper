// SimpleFPEWrapper - SimpleFPEWrapper/fpe/fpe_shadergen.h
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

#pragma once

#include <string>
#include "defines.h"

struct scratch_t {
    bool texgen_no_input[16] = {}; // units whose coords come only from texgen
    bool primary_color_saved = false;
    std::string last_stage_linkage;
    std::string vs_body;

    bool has_color_input = false;
    bool has_normal_input = false;
    bool has_vertex_color = false;
    bool has_back_vertex_color = false;
    bool has_texcoord[MAX_TEX] = {false};
    bool has_fog_coord_input = false; // glFogCoord*/glFogCoordPointer fed
};

class fpe_shader_generator {
public:
    fpe_shader_generator(const struct fixed_function_state_t& state) : state_(state) {}

    struct program_t generate_program();

private:
    static std::string vertex_shader(const struct fixed_function_state_t& state, scratch_t& scratch);
    static std::string fragment_shader(const struct fixed_function_state_t& state, scratch_t& scratch);

    const fixed_function_state_t& state_;
    scratch_t scratch_;
};
