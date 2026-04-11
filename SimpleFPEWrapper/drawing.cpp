// SimpleFPEWrapper - SimpleFPEWrapper/drawing.cpp
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

#include "init.h"

#include "fpe/fpe.hpp"
#include "fpe/list.h"

void glDrawArrays(GLenum mode, GLint first, GLsizei count) {
    LIST_RECORD(glDrawArrays, {}, mode, first, count)

    GET_PREV_PROGRAM
    int do_draw_element = commit_fpe_state_on_draw(&mode, &first, &count);
    if (do_draw_element) {
        g_glFuncs.glDrawElements(mode, count, GL_UNSIGNED_INT, (void*)0);
    } else
        g_glFuncs.glDrawArrays(mode, first, count);

    SET_PREV_PROGRAM
    g_glFuncs.glBindVertexArray(0);
}

