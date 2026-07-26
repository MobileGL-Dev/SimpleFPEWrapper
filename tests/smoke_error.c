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

    printf("OK: wrapper error machine behaves per GL semantics\n");
    return 0;
}
