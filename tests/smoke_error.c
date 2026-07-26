// SimpleFPEWrapper - tests/smoke_error.c
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// Wrapper GL error machine (plans/02, section A), CPU-side only:
//  - glGetError with no error and no backend returns GL_NO_ERROR
//  - an invalid call (glDeleteLists range < 0) latches GL_INVALID_VALUE
//  - glGetError returns it once, then reverts to GL_NO_ERROR
//  - only the FIRST unread error is kept (GL semantics)

#include <dlfcn.h>
#include <stdio.h>

#define GL_NO_ERROR 0
#define GL_INVALID_VALUE 0x0501

typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef int GLsizei;

int main(void) {
    void* handle = dlopen(WRAPPER_LIB_PATH, RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        fprintf(stderr, "FAIL: dlopen: %s\n", dlerror());
        return 1;
    }

    typedef void* (*resolver_t)(const char*);
    resolver_t resolve = (resolver_t)dlsym(handle, "eglGetProcAddress");
    if (!resolve) {
        fprintf(stderr, "FAIL: no eglGetProcAddress\n");
        return 1;
    }

    GLenum (*getError)(void) = (GLenum(*)(void))resolve("glGetError");
    void (*deleteLists)(GLuint, GLsizei) = (void (*)(GLuint, GLsizei))resolve("glDeleteLists");
    GLuint (*genLists)(GLsizei) = (GLuint(*)(GLsizei))resolve("glGenLists");
    if (!getError || !deleteLists || !genLists) {
        fprintf(stderr, "FAIL: resolver missing glGetError/glDeleteLists/glGenLists\n");
        return 1;
    }

    GLenum e = getError();
    if (e != GL_NO_ERROR) {
        fprintf(stderr, "FAIL: initial glGetError = 0x%x, want 0\n", e);
        return 1;
    }

    deleteLists(1, -1);
    genLists(-5); // would latch a second error; the first must win

    e = getError();
    if (e != GL_INVALID_VALUE) {
        fprintf(stderr, "FAIL: glGetError after invalid calls = 0x%x, want 0x501\n", e);
        return 1;
    }
    e = getError();
    if (e != GL_NO_ERROR) {
        fprintf(stderr, "FAIL: second glGetError = 0x%x, want 0 (error must clear)\n", e);
        return 1;
    }

    if (genLists(0) != 0) {
        fprintf(stderr, "FAIL: glGenLists(0) must return 0\n");
        return 1;
    }
    if (getError() != GL_NO_ERROR) {
        fprintf(stderr, "FAIL: glGenLists(0) must not set an error\n");
        return 1;
    }

    // --- Extended error-contract matrix (production hardening) ----------
    void (*matrixMode)(GLenum) = (void (*)(GLenum))resolve("glMatrixMode");
    void (*pushMatrix)(void) = (void (*)(void))resolve("glPushMatrix");
    void (*popMatrix)(void) = (void (*)(void))resolve("glPopMatrix");
    void (*begin)(GLenum) = (void (*)(GLenum))resolve("glBegin");
    void (*end)(void) = (void (*)(void))resolve("glEnd");
    void (*alphaFunc)(GLenum, float) = (void (*)(GLenum, float))resolve("glAlphaFunc");
    void (*popAttrib)(void) = (void (*)(void))resolve("glPopAttrib");
    void (*popName)(void) = (void (*)(void))resolve("glPopName");
    void (*texGeni)(GLenum, GLenum, int) = (void (*)(GLenum, GLenum, int))resolve("glTexGeni");
    if (!matrixMode || !pushMatrix || !popMatrix || !begin || !end || !alphaFunc || !popAttrib ||
        !popName || !texGeni) {
        fprintf(stderr, "FAIL: extended entry points missing\n");
        return 1;
    }
#define EXPECT_ERROR(call, expected, tag)                                                          \
    do {                                                                                           \
        call;                                                                                      \
        GLenum got = getError();                                                                   \
        if (got != (expected)) {                                                                   \
            fprintf(stderr, "FAIL[%s]: got 0x%x, want 0x%x\n", tag, got, (unsigned)(expected));   \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

    EXPECT_ERROR(matrixMode(0x1234), 0x0500 /* INVALID_ENUM */, "glMatrixMode(bad)");
    EXPECT_ERROR(popMatrix(), 0x0504 /* STACK_UNDERFLOW */, "glPopMatrix(empty)");
    for (int i = 0; i < 64; ++i) pushMatrix(); // modelview cap is 64
    EXPECT_ERROR(pushMatrix(), 0x0503 /* STACK_OVERFLOW */, "glPushMatrix(full)");
    for (int i = 0; i < 64; ++i) popMatrix();
    (void)getError();

    EXPECT_ERROR(begin(0x1234), 0x0500, "glBegin(bad mode)");
    EXPECT_ERROR(end(), 0x0502 /* INVALID_OPERATION */, "glEnd(unmatched)");
    begin(0x0007 /* QUADS */);
    EXPECT_ERROR(begin(0x0007), 0x0502, "glBegin(nested)");
    end();
    (void)getError();

    EXPECT_ERROR(alphaFunc(0x1234, 0.5f), 0x0500, "glAlphaFunc(bad func)");
    EXPECT_ERROR(popAttrib(), 0x0504, "glPopAttrib(empty)");
    EXPECT_ERROR(popName(), 0x0504, "glPopName(empty)");
    EXPECT_ERROR(texGeni(0x2000 /* GL_S */, 0x2500 /* GL_TEXTURE_GEN_MODE */, 0x1234), 0x0500,
                 "glTexGeni(bad mode)");
    EXPECT_ERROR(texGeni(0x1234, 0x2500, 0x2400), 0x0500, "glTexGeni(bad coord)");

    printf("OK: extended error-contract matrix holds\n");
    printf("OK: wrapper error machine behaves per GL semantics\n");
    return 0;
}
