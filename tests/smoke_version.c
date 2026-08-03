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
//
// Run with "desktop" it makes a desktop GL context instead of an ES one,
// which is the other half of glGetString: a backend whose version already
// parses is reported verbatim and the wrapper appends itself. Both halves
// have to carry the commit, and the strings are cached per process, so the
// two cases cannot be covered by one run.
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

int main(int argc, char** argv) {
    const int desktop = argc > 1 && strcmp(argv[1], "desktop") == 0;
    void* h = dlopen(WRAPPER_LIB_PATH, RTLD_NOW | RTLD_LOCAL);
    if (!h) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 1; }
    void* (*resolve)(const char*);
    *(void**)(&resolve) = dlsym(h, "eglGetProcAddress");
    if (!resolve) { fprintf(stderr, "*** no eglGetProcAddress\n"); return 1; }

    EGLDisplay d = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (d == EGL_NO_DISPLAY || !eglInitialize(d, NULL, NULL)) { printf("SKIP: no EGL display\n"); return 77; }
    const EGLint ca[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE,
                         desktop ? EGL_OPENGL_BIT : EGL_OPENGL_ES3_BIT, EGL_RED_SIZE, 8,
                         EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
    EGLConfig c; EGLint n = 0;
    if (!eglChooseConfig(d, ca, &c, 1, &n) || n == 0) {
        printf("SKIP: no %s config\n", desktop ? "desktop GL" : "ES3");
        return 77;
    }
    const EGLint pa[] = {EGL_WIDTH, WIN, EGL_HEIGHT, WIN, EGL_NONE};
    EGLSurface s = eglCreatePbufferSurface(d, c, pa);
    if (!eglBindAPI(desktop ? EGL_OPENGL_API : EGL_OPENGL_ES_API)) {
        printf("SKIP: no %s API\n", desktop ? "desktop GL" : "ES");
        return 77;
    }
    // A core profile, not whatever the driver hands out by default: a
    // compatibility context still answers everything GL 2.1 ever had, so it
    // cannot show whether the wrapper stands on the floor it claims.
    const EGLint core[] = {0x3098 /* EGL_CONTEXT_MAJOR_VERSION */, 3,
                           0x30FB /* EGL_CONTEXT_MINOR_VERSION */, 3,
                           0x30FD /* EGL_CONTEXT_OPENGL_PROFILE_MASK */,
                           0x00000001 /* CORE_PROFILE_BIT */, EGL_NONE};
    const EGLint esa[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    EGLContext x = eglCreateContext(d, c, EGL_NO_CONTEXT, desktop ? core : esa);
    if (x == EGL_NO_CONTEXT) { printf("SKIP: no %s context\n", desktop ? "core profile" : "ES3"); return 77; }
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

    // An ES backend is reported as its desktop equivalent and the wrapper
    // names itself right after the level; a desktop backend keeps its own
    // string and the wrapper follows in the suffix.
    const char* marker = desktop ? "Simple FPE Wrapper " : "SFPEW ";
    char fromVersion[64] = {0};
    if (!extractVersion(ver, marker, fromVersion, sizeof fromVersion)) {
        printf("*** GL_VERSION carries no wrapper version after \"%s\"\n", marker);
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

    // Additive contract: whatever the backend called itself is still in
    // there. An ES backend's string is quoted after the wrapper's part; a
    // desktop backend's string IS the front of the line, so the wrapper's
    // part must never have taken position zero.
    if (desktop) {
        const char* suffix = strstr(ver, "(with ");
        if (!suffix || suffix == ver) {
            printf("*** the backend's own version string was displaced\n");
            ++failures;
        }
    } else if (!strstr(ver, "OpenGL ES")) {
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
