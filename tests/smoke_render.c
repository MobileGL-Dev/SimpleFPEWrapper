// SimpleFPEWrapper - tests/smoke_render.c
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// First end-to-end FPE render check (plans/11 Q2): every GL call goes
// through the wrapper's resolver; an immediate-mode red quad must come
// back red through glReadPixels on a real GLES3 device. Skips (77) when
// the machine has no EGL device.

#include <dlfcn.h>
#include <stdio.h>

#include <EGL/egl.h>

typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef unsigned char GLubyte;
typedef float GLfloat;
typedef int GLint, GLsizei;
typedef unsigned int GLbitfield;

#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_QUADS 0x0007
#define GL_RGBA 0x1908
#define GL_UNSIGNED_BYTE 0x1401

int main(void) {
    void* handle = dlopen(WRAPPER_LIB_PATH, RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        fprintf(stderr, "FAIL: dlopen: %s\n", dlerror());
        return 1;
    }
    typedef void* (*resolver_t)(const char*);
    resolver_t resolve = (resolver_t)dlsym(handle, "eglGetProcAddress");
    if (!resolve) return 1;

    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display == EGL_NO_DISPLAY || !eglInitialize(display, NULL, NULL)) {
        printf("SKIP: no EGL display\n");
        return 77;
    }
    static const EGLint config_attribs[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE,
                                            EGL_OPENGL_ES3_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
                                            EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
    EGLConfig config;
    EGLint num_config = 0;
    if (!eglChooseConfig(display, config_attribs, &config, 1, &num_config) || num_config == 0) {
        printf("SKIP: no ES3 pbuffer config\n");
        return 77;
    }
    static const EGLint pbuffer_attribs[] = {EGL_WIDTH, 64, EGL_HEIGHT, 64, EGL_NONE};
    EGLSurface surface = eglCreatePbufferSurface(display, config, pbuffer_attribs);
    eglBindAPI(EGL_OPENGL_ES_API);
    static const EGLint ctx_attribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, ctx_attribs);
    if (surface == EGL_NO_SURFACE || context == EGL_NO_CONTEXT ||
        !eglMakeCurrent(display, surface, surface, context)) {
        printf("SKIP: could not make an ES3 pbuffer context current\n");
        return 77;
    }

    void (*fClearColor)(GLfloat, GLfloat, GLfloat, GLfloat) =
        (void (*)(GLfloat, GLfloat, GLfloat, GLfloat))resolve("glClearColor");
    void (*fClear)(GLbitfield) = (void (*)(GLbitfield))resolve("glClear");
    void (*fBegin)(GLenum) = (void (*)(GLenum))resolve("glBegin");
    void (*fEnd)(void) = (void (*)(void))resolve("glEnd");
    void (*fColor3f)(GLfloat, GLfloat, GLfloat) = (void (*)(GLfloat, GLfloat, GLfloat))resolve("glColor3f");
    void (*fVertex2f)(GLfloat, GLfloat) = (void (*)(GLfloat, GLfloat))resolve("glVertex2f");
    void (*fReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*) =
        (void (*)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*))resolve("glReadPixels");
    GLenum (*fGetError)(void) = (GLenum(*)(void))resolve("glGetError");
    if (!fClearColor || !fClear || !fBegin || !fEnd || !fColor3f || !fVertex2f || !fReadPixels ||
        !fGetError) {
        fprintf(stderr, "FAIL: resolver missing entry points\n");
        return 1;
    }

    fClearColor(0.0f, 0.0f, 1.0f, 1.0f);
    fClear(GL_COLOR_BUFFER_BIT);

    // Identity matrices: NDC coordinates directly. Full-screen red quad.
    fBegin(GL_QUADS);
    fColor3f(1.0f, 0.0f, 0.0f);
    fVertex2f(-1.0f, -1.0f);
    fVertex2f(1.0f, -1.0f);
    fVertex2f(1.0f, 1.0f);
    fVertex2f(-1.0f, 1.0f);
    fEnd();

    GLubyte pixel[4] = {0, 0, 0, 0};
    fReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);

    const GLenum error = fGetError();
    if (error != 0) {
        fprintf(stderr, "FAIL: glGetError() = 0x%x after the draw\n", error);
        return 1;
    }
    if (pixel[0] < 200 || pixel[1] > 50 || pixel[2] > 50) {
        fprintf(stderr, "FAIL: center pixel is (%u,%u,%u,%u), expected red\n", pixel[0], pixel[1],
                pixel[2], pixel[3]);
        return 1;
    }
    printf("OK: phase 1 immediate-mode quad is red\n");

    // --- Phase 2: vertex lighting (plans/04). A directional white light
    // shining down -z onto a +z-facing red quad: diffuse keeps red bright.
    void (*fEnable)(GLenum) = (void (*)(GLenum))resolve("glEnable");
    void (*fDisable)(GLenum) = (void (*)(GLenum))resolve("glDisable");
    void (*fLightfv)(GLenum, GLenum, const GLfloat*) =
        (void (*)(GLenum, GLenum, const GLfloat*))resolve("glLightfv");
    void (*fNormal3f)(GLfloat, GLfloat, GLfloat) =
        (void (*)(GLfloat, GLfloat, GLfloat))resolve("glNormal3f");
    if (!fEnable || !fDisable || !fLightfv || !fNormal3f) return 1;

    fClearColor(0.0f, 0.0f, 1.0f, 1.0f);
    fClear(GL_COLOR_BUFFER_BIT);
    fEnable(0x0B50 /* GL_LIGHTING */);
    fEnable(0x4000); /* GL_LIGHT0 */
    static const GLfloat white[4] = {1, 1, 1, 1};
    static const GLfloat dir[4] = {0, 0, 1, 0}; // directional, towards viewer
    fLightfv(0x4000, 0x1201 /* GL_DIFFUSE */, white);
    fLightfv(0x4000, 0x1203 /* GL_POSITION */, dir);
    fEnable(0x0B57 /* GL_COLOR_MATERIAL */);

    fBegin(GL_QUADS);
    fColor3f(1.0f, 0.0f, 0.0f);
    fNormal3f(0.0f, 0.0f, 1.0f);
    fVertex2f(-1.0f, -1.0f);
    fVertex2f(1.0f, -1.0f);
    fVertex2f(1.0f, 1.0f);
    fVertex2f(-1.0f, 1.0f);
    fEnd();

    fReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    if (fGetError() != 0) {
        fprintf(stderr, "FAIL: GL error after the lit draw\n");
        return 1;
    }
    if (pixel[0] < 150 || pixel[1] > 80 || pixel[2] > 80) {
        fprintf(stderr, "FAIL: lit pixel is (%u,%u,%u), expected bright red\n", pixel[0], pixel[1],
                pixel[2]);
        return 1;
    }
    fDisable(0x0B50);
    fDisable(0x0B57);
    printf("OK: phase 2 directional lighting keeps the quad red\n");

    // --- Phase 3: GL_MODULATE texturing (plans/05). A solid green texture
    // times a white quad must come back green.
    void (*fGenTextures)(GLsizei, GLuint*) = (void (*)(GLsizei, GLuint*))resolve("glGenTextures");
    void (*fBindTexture)(GLenum, GLuint) = (void (*)(GLenum, GLuint))resolve("glBindTexture");
    void (*fTexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*) =
        (void (*)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum,
                  const void*))resolve("glTexImage2D");
    void (*fTexParameteri)(GLenum, GLenum, GLint) =
        (void (*)(GLenum, GLenum, GLint))resolve("glTexParameteri");
    void (*fTexCoord2f)(GLfloat, GLfloat) = (void (*)(GLfloat, GLfloat))resolve("glTexCoord2f");
    if (!fGenTextures || !fBindTexture || !fTexImage2D || !fTexParameteri || !fTexCoord2f) return 1;

    GLuint texture = 0;
    fGenTextures(1, &texture);
    fBindTexture(0x0DE1 /* GL_TEXTURE_2D */, texture);
    static const GLubyte green_texel[4] = {0, 255, 0, 255};
    fTexImage2D(0x0DE1, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, green_texel);
    fTexParameteri(0x0DE1, 0x2801 /* GL_TEXTURE_MIN_FILTER */, 0x2600 /* GL_NEAREST */);
    fTexParameteri(0x0DE1, 0x2800 /* GL_TEXTURE_MAG_FILTER */, 0x2600);
    fEnable(0x0DE1); // GL_TEXTURE_2D

    fClearColor(0.0f, 0.0f, 1.0f, 1.0f);
    fClear(GL_COLOR_BUFFER_BIT);
    fBegin(GL_QUADS);
    fColor3f(1.0f, 1.0f, 1.0f);
    fTexCoord2f(0.0f, 0.0f);
    fVertex2f(-1.0f, -1.0f);
    fTexCoord2f(1.0f, 0.0f);
    fVertex2f(1.0f, -1.0f);
    fTexCoord2f(1.0f, 1.0f);
    fVertex2f(1.0f, 1.0f);
    fTexCoord2f(0.0f, 1.0f);
    fVertex2f(-1.0f, 1.0f);
    fEnd();

    fReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    if (fGetError() != 0) {
        fprintf(stderr, "FAIL: GL error after the textured draw\n");
        return 1;
    }
    if (pixel[1] < 200 || pixel[0] > 50 || pixel[2] > 50) {
        fprintf(stderr, "FAIL: textured pixel is (%u,%u,%u), expected green\n", pixel[0], pixel[1],
                pixel[2]);
        return 1;
    }
    printf("OK: phase 3 GL_MODULATE texturing renders green\n");
    fDisable(0x0DE1); // GL_TEXTURE_2D off for the array phases

    // --- Phase 4: client vertex arrays via glDrawArrays (the Minecraft
    // chunk path: program 0 + enabled arrays + client memory).
    void (*fEnableClientState)(GLenum) = (void (*)(GLenum))resolve("glEnableClientState");
    void (*fDisableClientState)(GLenum) = (void (*)(GLenum))resolve("glDisableClientState");
    void (*fVertexPointer)(GLint, GLenum, GLsizei, const void*) =
        (void (*)(GLint, GLenum, GLsizei, const void*))resolve("glVertexPointer");
    void (*fColorPointer)(GLint, GLenum, GLsizei, const void*) =
        (void (*)(GLint, GLenum, GLsizei, const void*))resolve("glColorPointer");
    void (*fDrawArrays)(GLenum, GLint, GLsizei) =
        (void (*)(GLenum, GLint, GLsizei))resolve("glDrawArrays");
    void (*fDrawElements)(GLenum, GLsizei, GLenum, const void*) =
        (void (*)(GLenum, GLsizei, GLenum, const void*))resolve("glDrawElements");
    if (!fEnableClientState || !fDisableClientState || !fVertexPointer || !fColorPointer ||
        !fDrawArrays || !fDrawElements) {
        fprintf(stderr, "FAIL: array entry points missing\n");
        return 1;
    }

    // Interleaved x,y + r,g,b,a floats for a full-screen magenta quad
    // as two triangles.
    static const GLfloat verts[] = {-1, -1, 1, -1, 1, 1, -1, -1, 1, 1, -1, 1};
    static const GLfloat colors[] = {1, 0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1,
                                     1, 0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1};
    fClearColor(0.0f, 0.0f, 1.0f, 1.0f);
    fClear(GL_COLOR_BUFFER_BIT);
    fEnableClientState(0x8074 /* GL_VERTEX_ARRAY */);
    fEnableClientState(0x8076 /* GL_COLOR_ARRAY */);
    fVertexPointer(2, 0x1406 /* GL_FLOAT */, 0, verts);
    fColorPointer(4, 0x1406, 0, colors);
    fDrawArrays(0x0004 /* GL_TRIANGLES */, 0, 6);
    fReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    if (fGetError() != 0 || pixel[0] < 200 || pixel[1] > 50 || pixel[2] < 200) {
        fprintf(stderr, "FAIL: phase 4 array draw pixel (%u,%u,%u)\n", pixel[0], pixel[1], pixel[2]);
        return 1;
    }
    printf("OK: phase 4 client vertex arrays render magenta\n");

    // --- Phase 5: glDrawElements with client-memory indices.
    static const GLfloat quad_verts[] = {-1, -1, 1, -1, 1, 1, -1, 1};
    static const GLfloat yellow[] = {1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0, 1};
    static const unsigned short tri_idx[] = {0, 1, 2, 0, 2, 3};
    fClearColor(0.0f, 0.0f, 1.0f, 1.0f);
    fClear(GL_COLOR_BUFFER_BIT);
    fVertexPointer(2, 0x1406, 0, quad_verts);
    fColorPointer(4, 0x1406, 0, yellow);
    fDrawElements(0x0004, 6, 0x1403 /* GL_UNSIGNED_SHORT */, tri_idx);
    fReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    if (fGetError() != 0 || pixel[0] < 200 || pixel[1] < 200 || pixel[2] > 50) {
        fprintf(stderr, "FAIL: phase 5 DrawElements pixel (%u,%u,%u)\n", pixel[0], pixel[1],
                pixel[2]);
        return 1;
    }
    printf("OK: phase 5 client-index glDrawElements renders yellow\n");

    // --- Phase 6: indexed GL_QUADS (the S2 rewrite path).
    static const unsigned short quad_idx[] = {0, 1, 2, 3};
    fClearColor(0.0f, 0.0f, 1.0f, 1.0f);
    fClear(GL_COLOR_BUFFER_BIT);
    fDrawElements(GL_QUADS, 4, 0x1403, quad_idx);
    fReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    if (fGetError() != 0 || pixel[0] < 200 || pixel[1] < 200 || pixel[2] > 50) {
        fprintf(stderr, "FAIL: phase 6 indexed QUADS pixel (%u,%u,%u)\n", pixel[0], pixel[1],
                pixel[2]);
        return 1;
    }
    printf("OK: phase 6 indexed GL_QUADS rewrite renders yellow\n");
    fDisableClientState(0x8074);
    fDisableClientState(0x8076);

    // --- Phase 7: display-list replay must reproduce the immediate image.
    GLuint (*fGenLists)(GLsizei) = (GLuint(*)(GLsizei))resolve("glGenLists");
    void (*fNewList)(GLuint, GLenum) = (void (*)(GLuint, GLenum))resolve("glNewList");
    void (*fEndList)(void) = (void (*)(void))resolve("glEndList");
    void (*fCallList)(GLuint) = (void (*)(GLuint))resolve("glCallList");
    if (!fGenLists || !fNewList || !fEndList || !fCallList) return 1;

    const GLuint list = fGenLists(1);
    fNewList(list, 0x1300 /* GL_COMPILE */);
    fBegin(GL_QUADS);
    fColor3f(0.0f, 1.0f, 1.0f); // cyan
    fVertex2f(-1.0f, -1.0f);
    fVertex2f(1.0f, -1.0f);
    fVertex2f(1.0f, 1.0f);
    fVertex2f(-1.0f, 1.0f);
    fEnd();
    fEndList();

    fClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    fClear(GL_COLOR_BUFFER_BIT);
    fReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    if (pixel[1] > 50) {
        fprintf(stderr, "FAIL: phase 7 GL_COMPILE executed the draw\n");
        return 1;
    }
    fCallList(list);
    fReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    if (fGetError() != 0 || pixel[1] < 200 || pixel[2] < 200 || pixel[0] > 50) {
        fprintf(stderr, "FAIL: phase 7 replay pixel (%u,%u,%u), expected cyan\n", pixel[0],
                pixel[1], pixel[2]);
        return 1;
    }
    printf("OK: phase 7 display-list compile defers and replay renders cyan\n");

    // --- Phase 8: matrix transforms. Translate a half-screen quad right:
    // the left edge region must stay background.
    void (*fTranslatef)(GLfloat, GLfloat, GLfloat) =
        (void (*)(GLfloat, GLfloat, GLfloat))resolve("glTranslatef");
    void (*fLoadIdentity)(void) = (void (*)(void))resolve("glLoadIdentity");
    if (!fTranslatef || !fLoadIdentity) return 1;
    fClearColor(0.0f, 0.0f, 1.0f, 1.0f);
    fClear(GL_COLOR_BUFFER_BIT);
    fTranslatef(1.0f, 0.0f, 0.0f); // shift the left-half quad into the right half
    fBegin(GL_QUADS);
    fColor3f(1.0f, 0.0f, 0.0f);
    fVertex2f(-1.0f, -1.0f);
    fVertex2f(0.0f, -1.0f);
    fVertex2f(0.0f, 1.0f);
    fVertex2f(-1.0f, 1.0f);
    fEnd();
    fLoadIdentity();
    GLubyte left[4], right[4];
    fReadPixels(8, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, left);
    fReadPixels(48, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, right);
    if (fGetError() != 0 || right[0] < 200 || left[2] < 200 || left[0] > 50) {
        fprintf(stderr, "FAIL: phase 8 transform left=(%u,%u,%u) right=(%u,%u,%u)\n", left[0],
                left[1], left[2], right[0], right[1], right[2]);
        return 1;
    }
    printf("OK: phase 8 glTranslatef moves geometry correctly\n");

    // --- Phase 9: alpha test. A quad with alpha 0.25 under GL_GREATER 0.5
    // must be fully discarded.
    void (*fAlphaFunc)(GLenum, GLfloat) = (void (*)(GLenum, GLfloat))resolve("glAlphaFunc");
    void (*fColor4f)(GLfloat, GLfloat, GLfloat, GLfloat) =
        (void (*)(GLfloat, GLfloat, GLfloat, GLfloat))resolve("glColor4f");
    if (!fAlphaFunc || !fColor4f) return 1;
    fClearColor(0.0f, 0.0f, 1.0f, 1.0f);
    fClear(GL_COLOR_BUFFER_BIT);
    fEnable(0x0BC0 /* GL_ALPHA_TEST */);
    fAlphaFunc(0x0204 /* GL_GREATER */, 0.5f);
    fBegin(GL_QUADS);
    fColor4f(1.0f, 0.0f, 0.0f, 0.25f);
    fVertex2f(-1.0f, -1.0f);
    fVertex2f(1.0f, -1.0f);
    fVertex2f(1.0f, 1.0f);
    fVertex2f(-1.0f, 1.0f);
    fEnd();
    fDisable(0x0BC0);
    fReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    if (fGetError() != 0 || pixel[2] < 200 || pixel[0] > 50) {
        fprintf(stderr, "FAIL: phase 9 alpha test pixel (%u,%u,%u), expected background blue\n",
                pixel[0], pixel[1], pixel[2]);
        return 1;
    }
    printf("OK: phase 9 alpha test discards low-alpha fragments\n");

    // --- Phase 10: GL_REPLACE texenv ignores the vertex color.
    void (*fTexEnvi)(GLenum, GLenum, GLint) = (void (*)(GLenum, GLenum, GLint))resolve("glTexEnvi");
    void (*fTexEnvfv)(GLenum, GLenum, const GLfloat*) =
        (void (*)(GLenum, GLenum, const GLfloat*))resolve("glTexEnvfv");
    if (!fTexEnvi || !fTexEnvfv) return 1;
    fBindTexture(0x0DE1, texture); // solid green from phase 3
    fEnable(0x0DE1);
    fTexEnvi(0x2300 /* GL_TEXTURE_ENV */, 0x2200 /* GL_TEXTURE_ENV_MODE */, 0x1E01 /* GL_REPLACE */);
    fClearColor(0.0f, 0.0f, 1.0f, 1.0f);
    fClear(GL_COLOR_BUFFER_BIT);
    fBegin(GL_QUADS);
    fColor3f(1.0f, 0.0f, 0.0f); // must be ignored by REPLACE
    fTexCoord2f(0.0f, 0.0f);
    fVertex2f(-1.0f, -1.0f);
    fTexCoord2f(1.0f, 0.0f);
    fVertex2f(1.0f, -1.0f);
    fTexCoord2f(1.0f, 1.0f);
    fVertex2f(1.0f, 1.0f);
    fTexCoord2f(0.0f, 1.0f);
    fVertex2f(-1.0f, 1.0f);
    fEnd();
    fReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    if (fGetError() != 0 || pixel[1] < 200 || pixel[0] > 50) {
        fprintf(stderr, "FAIL: phase 10 REPLACE pixel (%u,%u,%u)\n", pixel[0], pixel[1], pixel[2]);
        return 1;
    }
    printf("OK: phase 10 GL_REPLACE ignores vertex color\n");

    // --- Phase 11: GL_COMBINE, REPLACE from GL_CONSTANT: output must be
    // exactly the texenv color regardless of texture and vertex color.
    static const GLfloat env_color[4] = {1.0f, 0.5f, 0.0f, 1.0f}; // orange
    fTexEnvi(0x2300, 0x2200, 0x8570 /* GL_COMBINE */);
    fTexEnvi(0x2300, 0x8571 /* GL_COMBINE_RGB */, 0x1E01 /* GL_REPLACE */);
    fTexEnvi(0x2300, 0x8580 /* GL_SRC0_RGB */, 0x8576 /* GL_CONSTANT */);
    fTexEnvi(0x2300, 0x8590 /* GL_OPERAND0_RGB */, 0x0300 /* GL_SRC_COLOR */);
    fTexEnvi(0x2300, 0x8572 /* GL_COMBINE_ALPHA */, 0x1E01);
    fTexEnvi(0x2300, 0x8588 /* GL_SRC0_ALPHA */, 0x8576);
    fTexEnvfv(0x2300, 0x2201 /* GL_TEXTURE_ENV_COLOR */, env_color);
    fClearColor(0.0f, 0.0f, 1.0f, 1.0f);
    fClear(GL_COLOR_BUFFER_BIT);
    fBegin(GL_QUADS);
    fColor3f(0.0f, 1.0f, 0.0f);
    fTexCoord2f(0.0f, 0.0f);
    fVertex2f(-1.0f, -1.0f);
    fTexCoord2f(1.0f, 0.0f);
    fVertex2f(1.0f, -1.0f);
    fTexCoord2f(1.0f, 1.0f);
    fVertex2f(1.0f, 1.0f);
    fTexCoord2f(0.0f, 1.0f);
    fVertex2f(-1.0f, 1.0f);
    fEnd();
    fReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    if (fGetError() != 0 || pixel[0] < 200 || pixel[1] < 100 || pixel[1] > 160 || pixel[2] > 50) {
        fprintf(stderr, "FAIL: phase 11 COMBINE pixel (%u,%u,%u), expected orange\n", pixel[0],
                pixel[1], pixel[2]);
        return 1;
    }
    fTexEnvi(0x2300, 0x2200, 0x2100 /* GL_MODULATE */);
    fDisable(0x0DE1);
    printf("OK: phase 11 GL_COMBINE constant replace renders orange\n");

    // --- Phase 12: saturated linear fog turns everything the fog color.
    void (*fFogf)(GLenum, GLfloat) = (void (*)(GLenum, GLfloat))resolve("glFogf");
    void (*fFogi)(GLenum, GLint) = (void (*)(GLenum, GLint))resolve("glFogi");
    void (*fFogfv)(GLenum, const GLfloat*) = (void (*)(GLenum, const GLfloat*))resolve("glFogfv");
    if (!fFogf || !fFogi || !fFogfv) return 1;
    static const GLfloat fog_color[4] = {0.0f, 1.0f, 1.0f, 1.0f}; // cyan
    fEnable(0x0B60 /* GL_FOG */);
    fFogi(0x0B65 /* GL_FOG_MODE */, 0x2601 /* GL_LINEAR */);
    fFogfv(0x0B66 /* GL_FOG_COLOR */, fog_color);
    fFogf(0x0B63 /* GL_FOG_START */, -10.0f);
    fFogf(0x0B64 /* GL_FOG_END */, -9.0f); // eye depth 0 >> end: fully fogged
    fClearColor(0.0f, 0.0f, 1.0f, 1.0f);
    fClear(GL_COLOR_BUFFER_BIT);
    fBegin(GL_QUADS);
    fColor3f(1.0f, 0.0f, 0.0f);
    fVertex2f(-1.0f, -1.0f);
    fVertex2f(1.0f, -1.0f);
    fVertex2f(1.0f, 1.0f);
    fVertex2f(-1.0f, 1.0f);
    fEnd();
    fDisable(0x0B60);
    fReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    if (fGetError() != 0 || pixel[1] < 200 || pixel[2] < 200 || pixel[0] > 50) {
        fprintf(stderr, "FAIL: phase 12 fog pixel (%u,%u,%u), expected fog cyan\n", pixel[0],
                pixel[1], pixel[2]);
        return 1;
    }
    printf("OK: phase 12 saturated linear fog renders the fog color\n");

    printf("OK: all FPE render phases passed on the real GLES3 device\n");
    return 0;
}
