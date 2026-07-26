// SimpleFPEWrapper - tests/smoke_list_recursion.c
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// Display-list safety (plans/02, section D), CPU-side only:
//  - a self-referential list (glNewList(A); glCallList(A); glEndList;
//    glCallList(A)) must terminate at the GL_MAX_LIST_NESTING cap instead
//    of overflowing the native stack;
//  - replaying without any GL context must not crash (backend guards).

#include <dlfcn.h>
#include <stdio.h>

typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef int GLsizei;

#define GL_COMPILE 0x1300

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

    GLuint (*genLists)(GLsizei) = (GLuint(*)(GLsizei))resolve("glGenLists");
    void (*newList)(GLuint, GLenum) = (void (*)(GLuint, GLenum))resolve("glNewList");
    void (*endList)(void) = (void (*)(void))resolve("glEndList");
    void (*callList)(GLuint) = (void (*)(GLuint))resolve("glCallList");
    if (!genLists || !newList || !endList || !callList) {
        fprintf(stderr, "FAIL: display-list entry points missing\n");
        return 1;
    }

    GLuint list = genLists(1);
    if (list == 0) {
        fprintf(stderr, "FAIL: glGenLists(1) returned 0\n");
        return 1;
    }

    newList(list, GL_COMPILE);
    callList(list); // recorded: the list calls itself
    endList();

    callList(list); // must return (nesting cap), not overflow the stack

    printf("OK: self-referential display list terminated at the nesting cap\n");
    return 0;
}
