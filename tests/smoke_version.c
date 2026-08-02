// SimpleFPEWrapper - tests/smoke_version.c
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// The wrapper's version has to be findable by whoever reads a bug report and
// harmless to whoever parses the string for something else. So: GL_VERSION
// still opens with the "<major>.<minor>" a desktop loader needs, the version
// and the commit both appear in it, the backend stays visible after them, and
// GL_RENDERER tells the same version. The number itself is deliberately not
// asserted - only its shape - so a release bump does not break this test.
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <EGL/egl.h>

typedef unsigned int GLenum;
typedef unsigned char GLubyte;

#define WIN 16
#define GL_VERSION 0x1F02
#define GL_RENDERER 0x1F01

static int failures = 0;

// Copies the version token that follows `marker`, stopping at the separator
// that ends it in either string ("," in GL_VERSION, ")" in GL_RENDERER).
static int extractVersion(const char* text, const char* marker, char* out, size_t outSize) {
    const char* at = strstr(text, marker);
    if (!at) return 0;
    at += strlen(marker);
    size_t n = 0;
    while (at[n] && at[n] != ',' && at[n] != ')' && at[n] != ' ' && n + 1 < outSize) {
        out[n] = at[n];
        ++n;
    }
    out[n] = '\0';
    return n > 0;
}

// "26.08", optionally ".<patch>", optionally a "-suffix".
static int looksLikeCalVer(const char* v) {
    size_t i = 0;
    int digits = 0;
    while (isdigit((unsigned char)v[i])) { ++i; ++digits; }
    if (digits < 2 || v[i] != '.') return 0;
    ++i;
    digits = 0;
    while (isdigit((unsigned char)v[i])) { ++i; ++digits; }
    if (digits < 2) return 0;
    if (v[i] == '.') {
        ++i;
        digits = 0;
        while (isdigit((unsigned char)v[i])) { ++i; ++digits; }
        if (digits == 0) return 0;
    }
    return v[i] == '\0' || v[i] == '-';
}

int main(void) {
    void* h = dlopen(WRAPPER_LIB_PATH, RTLD_NOW | RTLD_LOCAL);
    if (!h) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 1; }
    void* (*resolve)(const char*);
    *(void**)(&resolve) = dlsym(h, "eglGetProcAddress");
    if (!resolve) { fprintf(stderr, "*** no eglGetProcAddress\n"); return 1; }

    EGLDisplay d = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (d == EGL_NO_DISPLAY || !eglInitialize(d, NULL, NULL)) { printf("SKIP: no EGL display\n"); return 77; }
    const EGLint ca[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE,
                         EGL_OPENGL_ES3_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
                         EGL_ALPHA_SIZE, 8, EGL_NONE};
    EGLConfig c; EGLint n = 0;
    if (!eglChooseConfig(d, ca, &c, 1, &n) || n == 0) { printf("SKIP: no ES3 config\n"); return 77; }
    const EGLint pa[] = {EGL_WIDTH, WIN, EGL_HEIGHT, WIN, EGL_NONE};
    EGLSurface s = eglCreatePbufferSurface(d, c, pa);
    eglBindAPI(EGL_OPENGL_ES_API);
    const EGLint xa[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    EGLContext x = eglCreateContext(d, c, EGL_NO_CONTEXT, xa);
    if (!eglMakeCurrent(d, s, s, x)) { printf("SKIP: no current context\n"); return 77; }

    const GLubyte* (*fGetString)(GLenum);
    *(void**)(&fGetString) = resolve("glGetString");
    if (!fGetString) { fprintf(stderr, "*** MISSING glGetString\n"); return 1; }

    const char* ver = (const char*)fGetString(GL_VERSION);
    const char* renderer = (const char*)fGetString(GL_RENDERER);
    printf("GL_VERSION  = %s\n", ver ? ver : "(null)");
    printf("GL_RENDERER = %s\n", renderer ? renderer : "(null)");
    if (!ver || !renderer) { fprintf(stderr, "*** null identity string\n"); return 1; }

    // A desktop loader parses the leading "<major>.<minor>": the version must
    // not have displaced it.
    if (!isdigit((unsigned char)ver[0])) {
        printf("*** GL_VERSION no longer starts with the GL level\n");
        ++failures;
    }

    char fromVersion[64] = {0};
    if (!extractVersion(ver, "SFPEW ", fromVersion, sizeof fromVersion) &&
        !extractVersion(ver, "Simple FPE Wrapper ", fromVersion, sizeof fromVersion)) {
        printf("*** GL_VERSION carries no wrapper version\n");
        ++failures;
    } else if (!looksLikeCalVer(fromVersion)) {
        printf("*** \"%s\" is not a calendar version (expected e.g. 26.08-dev)\n", fromVersion);
        ++failures;
    }

    // The commit is what actually identifies a build, so it travels with it.
    const char* git = strstr(ver, "GIT@");
    if (!git || strlen(git) <= 4 || git[4] == ' ' || git[4] == ')') {
        printf("*** GL_VERSION carries no commit\n");
        ++failures;
    } else if (strncmp(git + 4, "unknown", 7) == 0) {
        printf("note: built outside a git checkout, commit reported as unknown\n");
    }

    // Additive contract: whatever the backend called itself is still in there.
    if (!strchr(ver, '(') || !strstr(ver, "OpenGL")) {
        printf("note: backend version string not recognizable in GL_VERSION\n");
    }

    char fromRenderer[64] = {0};
    if (!extractVersion(renderer, "(SFPEW ", fromRenderer, sizeof fromRenderer)) {
        printf("*** GL_RENDERER carries no wrapper version\n");
        ++failures;
    } else if (fromVersion[0] && strcmp(fromVersion, fromRenderer) != 0) {
        printf("*** GL_VERSION says %s but GL_RENDERER says %s\n", fromVersion, fromRenderer);
        ++failures;
    }

    printf(failures ? "FAIL: %d\n" : "OK (%d)\n", failures);
    return failures ? 1 : 0;
}
