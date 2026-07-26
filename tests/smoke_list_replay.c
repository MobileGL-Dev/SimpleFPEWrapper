// SimpleFPEWrapper - tests/smoke_list_replay.c
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// Display-list replay equivalence (plans/06 6.2), CPU-side: recording a
// state command under GL_COMPILE must NOT execute it, replaying the list
// must, and replay must equal immediate execution of the same command.

#include <dlfcn.h>
#include <stdio.h>

typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef float GLfloat;
typedef int GLint, GLsizei;

#define GL_COMPILE 0x1300
#define GL_FOG_DENSITY 0x0B62
#define GL_FOG 0x0B60

int main(void) {
    void* handle = dlopen(WRAPPER_LIB_PATH, RTLD_NOW | RTLD_LOCAL);
    if (!handle) return 1;
    typedef void* (*resolver_t)(const char*);
    resolver_t resolve = (resolver_t)dlsym(handle, "eglGetProcAddress");
    if (!resolve) return 1;

    GLuint (*genLists)(GLsizei) = (GLuint(*)(GLsizei))resolve("glGenLists");
    void (*newList)(GLuint, GLenum) = (void (*)(GLuint, GLenum))resolve("glNewList");
    void (*endList)(void) = (void (*)(void))resolve("glEndList");
    void (*callList)(GLuint) = (void (*)(GLuint))resolve("glCallList");
    void (*fogf)(GLenum, GLfloat) = (void (*)(GLenum, GLfloat))resolve("glFogf");
    void (*getFloatv)(GLenum, GLfloat*) = (void (*)(GLenum, GLfloat*))resolve("glGetFloatv");
    if (!genLists || !newList || !endList || !callList || !fogf || !getFloatv) return 1;

    GLfloat value = -1.0f;
    fogf(GL_FOG_DENSITY, 0.25f);

    const GLuint list = genLists(1);
    newList(list, GL_COMPILE);
    fogf(GL_FOG_DENSITY, 0.75f); // recorded, must not execute yet
    endList();

    getFloatv(GL_FOG_DENSITY, &value);
    if (value != 0.25f) {
        fprintf(stderr, "FAIL: GL_COMPILE executed the command (density=%f)\n", (double)value);
        return 1;
    }

    callList(list); // replay must apply it
    getFloatv(GL_FOG_DENSITY, &value);
    if (value != 0.75f) {
        fprintf(stderr, "FAIL: replay did not apply the command (density=%f)\n", (double)value);
        return 1;
    }

    // Equivalence: immediate execution lands on the same value.
    fogf(GL_FOG_DENSITY, 0.1f);
    fogf(GL_FOG_DENSITY, 0.75f);
    GLfloat immediate = -1.0f;
    getFloatv(GL_FOG_DENSITY, &immediate);
    if (immediate != value) {
        fprintf(stderr, "FAIL: replay (%f) != immediate (%f)\n", (double)value, (double)immediate);
        return 1;
    }

    printf("OK: COMPILE defers, replay applies, replay == immediate\n");
    return 0;
}
