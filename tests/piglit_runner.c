// SimpleFPEWrapper - tests/piglit_runner.c
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// Minimal piglit shader_test runner (plans/11): executes the subset of the
// piglit [test] command language used by the vendored tests in
// tests/piglit/, with every GL call resolved through the wrapper's
// eglGetProcAddress - so user shaders exercise the full translation
// pipeline. FS-only tests get the fixed-function-equivalent default vertex
// shader, matching upstream shader_runner semantics.
//
// Exit codes: 0 pass, 1 fail, 77 skip (no device / unsupported require).

#include <dlfcn.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <EGL/egl.h>

typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef unsigned char GLubyte;
typedef unsigned char GLboolean;
typedef float GLfloat;
typedef int GLint, GLsizei;
typedef char GLchar;
typedef unsigned int GLbitfield;
typedef long GLsizeiptr_local;

#define WIN 64

static void* (*resolve)(const char*);

static GLuint (*pCreateShader)(GLenum);
static void (*pShaderSource)(GLuint, GLsizei, const GLchar* const*, const GLint*);
static void (*pCompileShader)(GLuint);
static void (*pGetShaderiv)(GLuint, GLenum, GLint*);
static void (*pGetShaderInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*);
static GLuint (*pCreateProgram)(void);
static void (*pAttachShader)(GLuint, GLuint);
static void (*pLinkProgram)(GLuint);
static void (*pGetProgramiv)(GLuint, GLenum, GLint*);
static void (*pGetProgramInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*);
static void (*pUseProgram)(GLuint);
static GLint (*pGetAttribLocation)(GLuint, const GLchar*);
static GLint (*pGetUniformLocation)(GLuint, const GLchar*);
static void (*pUniform1f)(GLint, GLfloat);
static void (*pUniform2f)(GLint, GLfloat, GLfloat);
static void (*pUniform3f)(GLint, GLfloat, GLfloat, GLfloat);
static void (*pUniform4f)(GLint, GLfloat, GLfloat, GLfloat, GLfloat);
static void (*pUniform1i)(GLint, GLint);
static void (*pUniform2i)(GLint, GLint, GLint);
static void (*pUniform3i)(GLint, GLint, GLint, GLint);
static void (*pUniform4i)(GLint, GLint, GLint, GLint, GLint);
static void (*pUniformMatrix2fv)(GLint, GLsizei, GLboolean, const GLfloat*);
static void (*pUniformMatrix3fv)(GLint, GLsizei, GLboolean, const GLfloat*);
static void (*pUniformMatrix4fv)(GLint, GLsizei, GLboolean, const GLfloat*);
static void (*pClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
static void (*pClear)(GLbitfield);
static void (*pOrtho)(double, double, double, double, double, double);
static void (*pReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*);
static void (*pGenVertexArrays)(GLsizei, GLuint*);
static void (*pBindVertexArray)(GLuint);
static void (*pGenBuffers)(GLsizei, GLuint*);
static void (*pBindBuffer)(GLenum, GLuint);
static void (*pBufferData)(GLenum, long, const void*, GLenum);
static void (*pVertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*);
static void (*pEnableVertexAttribArray)(GLuint);
static void (*pDrawArrays)(GLenum, GLint, GLsizei);
static GLenum (*pGetError)(void);

#define R(dst, name)                                                                               \
    do {                                                                                           \
        *(void**)(&dst) = resolve(name);                                                           \
        if (!dst) {                                                                                \
            fprintf(stderr, "FAIL: cannot resolve %s\n", name);                                    \
            return 0;                                                                              \
        }                                                                                          \
    } while (0)

static int resolve_all(void) {
    R(pCreateShader, "glCreateShader");
    R(pShaderSource, "glShaderSource");
    R(pCompileShader, "glCompileShader");
    R(pGetShaderiv, "glGetShaderiv");
    R(pGetShaderInfoLog, "glGetShaderInfoLog");
    R(pCreateProgram, "glCreateProgram");
    R(pAttachShader, "glAttachShader");
    R(pLinkProgram, "glLinkProgram");
    R(pGetProgramiv, "glGetProgramiv");
    R(pGetProgramInfoLog, "glGetProgramInfoLog");
    R(pUseProgram, "glUseProgram");
    R(pGetAttribLocation, "glGetAttribLocation");
    R(pGetUniformLocation, "glGetUniformLocation");
    R(pUniform1f, "glUniform1f");
    R(pUniform2f, "glUniform2f");
    R(pUniform3f, "glUniform3f");
    R(pUniform4f, "glUniform4f");
    R(pUniform1i, "glUniform1i");
    R(pUniform2i, "glUniform2i");
    R(pUniform3i, "glUniform3i");
    R(pUniform4i, "glUniform4i");
    R(pUniformMatrix2fv, "glUniformMatrix2fv");
    R(pUniformMatrix3fv, "glUniformMatrix3fv");
    R(pUniformMatrix4fv, "glUniformMatrix4fv");
    R(pClearColor, "glClearColor");
    R(pClear, "glClear");
    R(pOrtho, "glOrtho");
    R(pReadPixels, "glReadPixels");
    R(pGenVertexArrays, "glGenVertexArrays");
    R(pBindVertexArray, "glBindVertexArray");
    R(pGenBuffers, "glGenBuffers");
    R(pBindBuffer, "glBindBuffer");
    R(pBufferData, "glBufferData");
    R(pVertexAttribPointer, "glVertexAttribPointer");
    R(pEnableVertexAttribArray, "glEnableVertexAttribArray");
    R(pDrawArrays, "glDrawArrays");
    R(pGetError, "glGetError");
    return 1;
}

static char* section_body(char* text, const char* name) {
    char header[64];
    snprintf(header, sizeof(header), "[%s]", name);
    char* start = strstr(text, header);
    if (!start) return NULL;
    start += strlen(header);
    while (*start == '\n' || *start == '\r') ++start;
    char* end = start;
    while ((end = strchr(end, '\n')) != NULL) {
        if (end[1] == '[') break;
        ++end;
    }
    size_t len = end ? (size_t)(end - start) : strlen(start);
    char* out = malloc(len + 1);
    memcpy(out, start, len);
    out[len] = '\0';
    return out;
}

static GLuint compile_stage(GLenum stage, const char* src, const char* tag) {
    GLuint shader = pCreateShader(stage);
    const GLint len = (GLint)strlen(src);
    pShaderSource(shader, 1, &src, &len);
    pCompileShader(shader);
    GLint ok = 0;
    pGetShaderiv(shader, 0x8B81 /* COMPILE_STATUS */, &ok);
    if (!ok) {
        char info[4096] = {0};
        pGetShaderInfoLog(shader, sizeof(info), NULL, info);
        fprintf(stderr, "FAIL: %s shader compile:\n%s\n", tag, info);
        return 0;
    }
    return shader;
}

static GLuint g_program;
static float g_ortho_w = 0.0f, g_ortho_h = 0.0f; // nonzero after `ortho`

static int draw_rect(float x, float y, float w, float h) {
    GLint loc = pGetAttribLocation(g_program, "fpe_Vertex");
    if (loc < 0) loc = pGetAttribLocation(g_program, "piglit_vertex");
    if (loc < 0) {
        fprintf(stderr, "FAIL: no vertex attribute (fpe_Vertex/piglit_vertex)\n");
        return 0;
    }
    const GLfloat verts[8] = {x, y, x + w, y, x, y + h, x + w, y + h};
    pBufferData(0x8892 /* ARRAY_BUFFER */, sizeof(verts), verts, 0x88E8 /* DYNAMIC_DRAW */);
    pVertexAttribPointer((GLuint)loc, 2, 0x1406 /* FLOAT */, 0, 0, 0);
    pEnableVertexAttribArray((GLuint)loc);
    pDrawArrays(0x0005 /* TRIANGLE_STRIP */, 0, 4);
    return 1;
}

static int probe_pixels(int px, int py, int pw, int ph, const float expected[4], int comps) {
    static GLubyte pixels[WIN * WIN * 4];
    pReadPixels(0, 0, WIN, WIN, 0x1908 /* RGBA */, 0x1401 /* UNSIGNED_BYTE */, pixels);
    const float tolerance = 0.02f;
    for (int yy = py; yy < py + ph; ++yy) {
        for (int xx = px; xx < px + pw; ++xx) {
            const GLubyte* p = &pixels[(yy * WIN + xx) * 4];
            for (int c = 0; c < comps; ++c) {
                const float got = p[c] / 255.0f;
                if (fabsf(got - expected[c]) > tolerance) {
                    fprintf(stderr,
                            "FAIL: probe at (%d,%d) channel %d: got %.4f, expected %.4f\n"
                            "      pixel (%u,%u,%u,%u)\n",
                            xx, yy, c, got, expected[c], p[0], p[1], p[2], p[3]);
                    return 0;
                }
            }
        }
    }
    return 1;
}

static int run_test_section(char* body) {
    char* save = NULL;
    for (char* line = strtok_r(body, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        while (*line == ' ' || *line == '\t') ++line;
        if (*line == '\0' || *line == '#') continue;
        float a, b, c, d, e, f, g;
        char name[128];
        char type[16];
        if (sscanf(line, "clear color %f %f %f %f", &a, &b, &c, &d) == 4) {
            pClearColor(a, b, c, d);
        } else if (strcmp(line, "clear") == 0) {
            pClear(0x4000 /* COLOR_BUFFER_BIT */);
        } else if (strcmp(line, "ortho") == 0) {
            pOrtho(0, WIN, 0, WIN, -1, 1);
            g_ortho_w = WIN;
            g_ortho_h = WIN;
        } else if (sscanf(line, "draw rect %f %f %f %f", &a, &b, &c, &d) == 4) {
            if (!draw_rect(a, b, c, d)) return 0;
        } else if (sscanf(line, "relative probe rgba ( %f , %f ) ( %f , %f , %f , %f )", &a, &b, &c,
                          &d, &e, &f) == 6) {
            const float exp4[4] = {c, d, e, f};
            if (!probe_pixels((int)(a * (WIN - 1)), (int)(b * (WIN - 1)), 1, 1, exp4, 4)) return 0;
        } else if (sscanf(line, "relative probe rgb ( %f , %f ) ( %f , %f , %f )", &a, &b, &c, &d,
                          &e) == 5) {
            const float exp3[4] = {c, d, e, 0};
            if (!probe_pixels((int)(a * (WIN - 1)), (int)(b * (WIN - 1)), 1, 1, exp3, 3)) return 0;
        } else if (sscanf(line, "probe all rgba %f %f %f %f", &a, &b, &c, &d) == 4) {
            const float exp4[4] = {a, b, c, d};
            if (!probe_pixels(0, 0, WIN, WIN, exp4, 4)) return 0;
        } else if (sscanf(line, "probe rgba %f %f %f %f %f %f", &a, &b, &c, &d, &e, &f) == 6) {
            const float exp4[4] = {c, d, e, f};
            if (!probe_pixels((int)a, (int)b, 1, 1, exp4, 4)) return 0;
        } else if (sscanf(line, "probe rgb %f %f %f %f %f", &a, &b, &c, &d, &e) == 5) {
            const float exp3[4] = {c, d, e, 0};
            if (!probe_pixels((int)a, (int)b, 1, 1, exp3, 3)) return 0;
        } else if (sscanf(line, "uniform %15s %127s %f %f %f %f", type, name, &a, &b, &c, &d) >= 3) {
            const GLint loc = pGetUniformLocation(g_program, name);
            if (loc < 0) continue; // optimized out: not an error per piglit
            const int n = sscanf(line, "uniform %*s %*s %f %f %f %f %f %f %f", &a, &b, &c, &d, &e,
                                 &f, &g);
            if (strcmp(type, "float") == 0) pUniform1f(loc, a);
            else if (strcmp(type, "int") == 0) pUniform1i(loc, (GLint)a);
            else if (strcmp(type, "vec2") == 0) pUniform2f(loc, a, b);
            else if (strcmp(type, "vec3") == 0) pUniform3f(loc, a, b, c);
            else if (strcmp(type, "vec4") == 0) pUniform4f(loc, a, b, c, d);
            else if (strcmp(type, "ivec2") == 0) pUniform2i(loc, (GLint)a, (GLint)b);
            else if (strcmp(type, "ivec3") == 0) pUniform3i(loc, (GLint)a, (GLint)b, (GLint)c);
            else if (strcmp(type, "ivec4") == 0) pUniform4i(loc, (GLint)a, (GLint)b, (GLint)c, (GLint)d);
            else if (strncmp(type, "mat", 3) == 0) {
                float m[16];
                const int want = (type[3] - '0') * (type[3] - '0');
                char* cursor = strstr(line, name) + strlen(name);
                for (int i = 0; i < want; ++i) m[i] = strtof(cursor, &cursor);
                if (type[3] == '2') pUniformMatrix2fv(loc, 1, 0, m);
                else if (type[3] == '3') pUniformMatrix3fv(loc, 1, 0, m);
                else pUniformMatrix4fv(loc, 1, 0, m);
            } else {
                fprintf(stderr, "SKIP: uniform type %s unsupported\n", type);
                exit(77);
            }
            (void)n;
        } else {
            fprintf(stderr, "SKIP: unsupported test command: %s\n", line);
            exit(77);
        }
    }
    const GLenum err = pGetError();
    if (err != 0) {
        fprintf(stderr, "FAIL: glGetError() = 0x%x after the test script\n", err);
        return 0;
    }
    return 1;
}

static const char* kDefaultVS = "#version 110\n"
                                "void main() { gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex; }\n";

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: piglit_runner <file.shader_test>\n");
        return 1;
    }
    FILE* fp = fopen(argv[1], "rb");
    if (!fp) return 1;
    fseek(fp, 0, SEEK_END);
    const long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    char* text = malloc((size_t)size + 1);
    if (fread(text, 1, (size_t)size, fp) != (size_t)size) return 1;
    text[size] = '\0';
    fclose(fp);

    // Requirements were pre-filtered when vendoring; still honor unknowns.
    char* require = section_body(text, "require");
    if (require) {
        char* save = NULL;
        for (char* line = strtok_r(require, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
            while (*line == ' ' || *line == '\t') ++line;
            if (*line == '\0') continue;
            if (strncmp(line, "GLSL >= 1.1", 11) && strncmp(line, "GLSL >= 1.2", 11) &&
                strncmp(line, "GL >= ", 6)) {
                printf("SKIP: unsupported requirement: %s\n", line);
                return 77;
            }
        }
    }

    void* handle = dlopen(WRAPPER_LIB_PATH, RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        fprintf(stderr, "FAIL: dlopen: %s\n", dlerror());
        return 1;
    }
    resolve = (void* (*)(const char*))dlsym(handle, "eglGetProcAddress");
    if (!resolve || !resolve_all()) return 1;

    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display == EGL_NO_DISPLAY || !eglInitialize(display, NULL, NULL)) {
        printf("SKIP: no EGL display\n");
        return 77;
    }
    static const EGLint cfg_attribs[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE,
                                         EGL_OPENGL_ES3_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
                                         EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
    EGLConfig config;
    EGLint num_config = 0;
    if (!eglChooseConfig(display, cfg_attribs, &config, 1, &num_config) || num_config == 0) {
        printf("SKIP: no ES3 config\n");
        return 77;
    }
    static const EGLint pb_attribs[] = {EGL_WIDTH, WIN, EGL_HEIGHT, WIN, EGL_NONE};
    EGLSurface surface = eglCreatePbufferSurface(display, config, pb_attribs);
    eglBindAPI(EGL_OPENGL_ES_API);
    static const EGLint ctx_attribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, ctx_attribs);
    if (surface == EGL_NO_SURFACE || context == EGL_NO_CONTEXT ||
        !eglMakeCurrent(display, surface, surface, context)) {
        printf("SKIP: no current context\n");
        return 77;
    }

    char* vs_src = section_body(text, "vertex shader");
    char* fs_src = section_body(text, "fragment shader");
    if (!fs_src) return 1;
    const GLuint vs = compile_stage(0x8B31, vs_src ? vs_src : kDefaultVS, "vertex");
    const GLuint fs = compile_stage(0x8B30, fs_src, "fragment");
    if (!vs || !fs) return 1;
    g_program = pCreateProgram();
    pAttachShader(g_program, vs);
    pAttachShader(g_program, fs);
    pLinkProgram(g_program);
    GLint linked = 0;
    pGetProgramiv(g_program, 0x8B82 /* LINK_STATUS */, &linked);
    if (!linked) {
        char info[4096] = {0};
        pGetProgramInfoLog(g_program, sizeof(info), NULL, info);
        fprintf(stderr, "FAIL: link:\n%s\n", info);
        return 1;
    }
    pUseProgram(g_program);

    GLuint vao = 0, vbo = 0;
    pGenVertexArrays(1, &vao);
    pBindVertexArray(vao);
    pGenBuffers(1, &vbo);
    pBindBuffer(0x8892, vbo);

    char* test = section_body(text, "test");
    if (!test || !run_test_section(test)) return 1;
    printf("PASS: %s\n", argv[1]);
    return 0;
}
