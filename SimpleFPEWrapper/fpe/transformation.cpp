// SimpleFPEWrapper - SimpleFPEWrapper/fpe/transformation.cpp
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

#include "transformation.h"
#include <glm/gtc/type_ptr.hpp>
#include "list.h"
#include "fpe.hpp"
#include "pointer_utils.h"

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_relational.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_relational.hpp>
#include <glm/ext/vector_float4.hpp>
#include <glm/ext/vector_float3.hpp>
#include <algorithm>
#define DEBUG 0

namespace {

int active_texture_index() {
    GLint active = GL_TEXTURE0;
    g_glFuncs.glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
    return std::clamp(active - (GLint)GL_TEXTURE0, 0, MAX_TEX - 1);
}

glm::mat4& current_matrix(transformation_t& transformation) {
    if (transformation.matrix_mode == GL_TEXTURE) {
        return transformation.texture_matrices[active_texture_index()];
    }
    return transformation.matrices[matrix_idx(transformation.matrix_mode)];
}

std::vector<glm::mat4>& current_matrix_stack(transformation_t& transformation) {
    if (transformation.matrix_mode == GL_TEXTURE) {
        return transformation.texture_matrices_stack[active_texture_index()];
    }
    return transformation.matrices_stack[matrix_idx(transformation.matrix_mode)];
}

} // namespace

int matrix_idx(GLenum matrix_mode) {
    switch (matrix_mode) {
    case GL_MODELVIEW:
        return 0;
    case GL_PROJECTION:
        return 1;
    case GL_TEXTURE:
        return 2;
    case GL_COLOR:
        return 3;
    }
    // LOG_E("Error: 1282");
    return 0;
}

void print_matrix(const glm::mat4& mat) {
#if DEBUG || GLOBAL_DEBUG
    auto* pmat = (const float*)glm::value_ptr(mat);
    // LOG_D_N("[")
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            int idx = j + i * 4;
            // LOG_D_N("%.2f ", pmat[idx])
        }
        // LOG_D("")
    }
    // LOG_D("]")
#endif
}

void glMatrixMode(GLenum mode) {
    // LOG()
    //  LOG_D("glMatrixMode(%s)", glEnumToString(mode))

    LIST_RECORD(glMatrixMode, {}, mode)

    auto& transformation = g_glstate.fpe_uniform.transformation;

    switch (mode) {
    case GL_MODELVIEW:
    case GL_PROJECTION:
    case GL_TEXTURE:
    case GL_COLOR:
        transformation.matrix_mode = mode;
    default:
        break;
    }
}

void glLoadIdentity() {
    // LOG()
    //  LOG_D("glLoadIdentity")

    LIST_RECORD(glLoadIdentity, {})

    auto& transformation = g_glstate.fpe_uniform.transformation;

    current_matrix(transformation) = glm::mat4(1.0);

    // LOG_D("Matrix %s:", glEnumToString(transformation.matrix_mode))
    print_matrix(current_matrix(transformation));
}

void glOrtho(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble near_val, GLdouble far_val) {
    // LOG()
    //  LOG_D("glOrtho(%f, %f, %f, %f, %f, %f)", left, right, bottom, top, near_val, far_val)

    LIST_RECORD(glOrtho, {}, left, right, bottom, top, near_val, far_val)

    // TODO: precision loss?
    SELF_CALL(glOrthof, left, right, bottom, top, near_val, far_val)
}

void glOrthof(GLfloat left, GLfloat right, GLfloat bottom, GLfloat top, GLfloat zNear, GLfloat zFar) {
    // LOG()
    //  LOG_D("glOrthof(%f, %f, %f, %f, %f, %f)", left, right, bottom, top, zNear, zFar)

    LIST_RECORD(glOrthof, {}, left, right, bottom, top, zNear, zFar)

    auto& transformation = g_glstate.fpe_uniform.transformation;

    current_matrix(transformation) *= glm::ortho(left, right, bottom, top, zNear, zFar);
    // LOG_D("Matrix %s:", glEnumToString(transformation.matrix_mode))
    print_matrix(current_matrix(transformation));
}

void glFrustum(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar) {
    LIST_RECORD(glFrustum, {}, left, right, bottom, top, zNear, zFar)

    // The emulated transform state is float-based, matching the generated ES
    // uniforms. Keep the desktop double entry point while accepting that final
    // storage has GLfloat precision.
    SELF_CALL(glFrustumf, (GLfloat)left, (GLfloat)right, (GLfloat)bottom, (GLfloat)top, (GLfloat)zNear,
              (GLfloat)zFar)
}

void glFrustumf(GLfloat left, GLfloat right, GLfloat bottom, GLfloat top, GLfloat zNear, GLfloat zFar) {
    LIST_RECORD(glFrustumf, {}, left, right, bottom, top, zNear, zFar)

    auto& transformation = g_glstate.fpe_uniform.transformation;
    current_matrix(transformation) *= glm::frustum(left, right, bottom, top, zNear, zFar);
    print_matrix(current_matrix(transformation));
}

void glScalef(GLfloat x, GLfloat y, GLfloat z) {
    // LOG()
    //  LOG_D("glScalef(%f, %f, %f)", x, y, z)

    LIST_RECORD(glScalef, {}, x, y, z)

    auto& transformation = g_glstate.fpe_uniform.transformation;

    auto& matrix = current_matrix(transformation);
    matrix = glm::scale(matrix, glm::vec3(x, y, z));
    // LOG_D("Matrix %s:", glEnumToString(transformation.matrix_mode))
    print_matrix(matrix);
}

void glTranslatef(GLfloat x, GLfloat y, GLfloat z) {
    // LOG()
    //  LOG_D("glTranslatef(%f, %f, %f)", x, y, z)

    LIST_RECORD(glTranslatef, {}, x, y, z)

    auto& transformation = g_glstate.fpe_uniform.transformation;

    auto& matrix = current_matrix(transformation);
    matrix = glm::translate(matrix, glm::vec3(x, y, z));
    // LOG_D("Matrix %s:", glEnumToString(transformation.matrix_mode))
    print_matrix(matrix);
}

void glRotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z) {
    // LOG()
    //  LOG_D("glRotatef, angle = %.2f, x = %.2f, y = %.2f, z = %.2f", angle, x, y, z)

    LIST_RECORD(glRotatef, {}, angle, x, y, z)

    auto& transformation = g_glstate.fpe_uniform.transformation;

    auto& matrix = current_matrix(transformation);
    matrix = glm::rotate(matrix, (GLfloat)(angle * M_PI / 180.f), glm::vec3(x, y, z));
    // LOG_D("Matrix %s:", glEnumToString(transformation.matrix_mode))
    print_matrix(matrix);
}

void glRotated(GLdouble angle, GLdouble x, GLdouble y, GLdouble z) {
    // LOG()
    //  LOG_D("glRotated(%f, %f, %f, %f)", angle, x, y, z)

    LIST_RECORD(glRotated, {}, angle, x, y, z)

    // TODO: precision loss?
    SELF_CALL(glRotatef, angle, x, y, z)
}

void glScaled(GLdouble x, GLdouble y, GLdouble z) {
    // LOG()
    //  LOG_D("glScaled(%f, %f, %f)", x, y, z)

    LIST_RECORD(glScaled, {}, x, y, z)

    // TODO: precision loss?
    SELF_CALL(glScalef, x, y, z)
}

void glTranslated(GLdouble x, GLdouble y, GLdouble z) {
    // LOG()
    //  LOG_D("glTranslated(%f, %f, %f)", x, y, z)

    LIST_RECORD(glTranslated, {}, x, y, z)

    // TODO: precision loss?
    SELF_CALL(glTranslatef, x, y, z);
}

void glLoadMatrixd(const GLdouble* m) {
    LIST_RECORD(glLoadMatrixd, {{0, sizeof(GLdouble) * 16}}, m)

    if (!m) return;
    GLfloat converted[16];
    for (int i = 0; i < 16; ++i) converted[i] = (GLfloat)m[i];
    SELF_CALL(glLoadMatrixf, converted)
}

void glLoadMatrixf(const GLfloat* m) {
    LIST_RECORD(glLoadMatrixf, {{0, sizeof(GLfloat) * 16}}, m)

    if (!m) return;
    auto& transformation = g_glstate.fpe_uniform.transformation;
    current_matrix(transformation) = glm::make_mat4(m);
    print_matrix(current_matrix(transformation));
}

void glMultMatrixd(const GLdouble* m) {
    LIST_RECORD(glMultMatrixd, {{0, sizeof(GLdouble) * 16}}, m)

    if (!m) return;
    GLfloat converted[16];
    for (int i = 0; i < 16; ++i) converted[i] = (GLfloat)m[i];
    SELF_CALL(glMultMatrixf, converted)
}

void glMultMatrixf(const GLfloat* m) {
    // LOG()
    //  LOG_D("glMultMatrixf(%p)", m)

    LIST_RECORD(glMultMatrixf, {{0, sizeof(GLfloat) * 16}}, m)

    if (!m) return;
    auto& transformation = g_glstate.fpe_uniform.transformation;

    // LOG_D("Matrix %s:", glEnumToString(transformation.matrix_mode))
    auto& matrix = current_matrix(transformation);
    print_matrix(matrix);
    // LOG_D("*")
    auto mat = glm::make_mat4(m);
    print_matrix(mat);

    matrix *= mat;
    // LOG_D("=")

    print_matrix(matrix);
}

void glPushMatrix(void) {
    // LOG()
    //  LOG_D("glPushMatrix()")

    LIST_RECORD(glPushMatrix, {})

    auto& transformation = g_glstate.fpe_uniform.transformation;

    auto& mat = current_matrix(transformation);
    current_matrix_stack(transformation).push_back(mat);

    // LOG_D("Matrix %s:", glEnumToString(transformation.matrix_mode))
    print_matrix(mat);
}

void glPopMatrix(void) {
    // LOG()
    //  LOG_D("glPopMatrix()")

    LIST_RECORD(glPopMatrix, {})

    auto& transformation = g_glstate.fpe_uniform.transformation;

    auto& mat = current_matrix(transformation);
    auto& stack = current_matrix_stack(transformation);
    if (stack.empty()) return;
    mat = stack.back();
    stack.pop_back();

    // LOG_D("Matrix %s:", glEnumToString(transformation.matrix_mode))
    print_matrix(mat);
}
