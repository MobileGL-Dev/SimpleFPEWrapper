// SimpleFPEWrapper - tests/smoke_lwjgl_surface.c
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// LWJGL-era engines flip a capability bit only when EVERY entry point of
// an extension resolves, then crash at call time if one is missing. This
// test resolves the full FBO/shader/VBO desktop surface through
// eglGetProcAddress and checks the advertised extension list is ADDITIVE:
// backend extensions still present, desktop ones appended.

#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

#include <EGL/egl.h>

static const char* kMustResolve[] = {
    // GL_EXT_framebuffer_object (complete set)
    "glGenFramebuffersEXT", "glDeleteFramebuffersEXT", "glBindFramebufferEXT",
    "glIsFramebufferEXT", "glCheckFramebufferStatusEXT", "glFramebufferTexture1DEXT",
    "glFramebufferTexture2DEXT", "glFramebufferTexture3DEXT", "glFramebufferRenderbufferEXT",
    "glGetFramebufferAttachmentParameterivEXT", "glGenRenderbuffersEXT",
    "glDeleteRenderbuffersEXT", "glIsRenderbufferEXT", "glBindRenderbufferEXT",
    "glRenderbufferStorageEXT", "glGetRenderbufferParameterivEXT", "glGenerateMipmapEXT",
    // blit + multisample + core ARB_framebuffer_object spellings
    "glBlitFramebufferEXT", "glRenderbufferStorageMultisampleEXT", "glGenFramebuffers",
    "glRenderbufferStorage", "glFramebufferTexture1D", "glFramebufferTexture3D",
    // ARB_shader_objects / ARB_vertex_shader / ARB_fragment_shader
    "glCreateShaderObjectARB", "glCreateProgramObjectARB", "glDeleteObjectARB",
    "glAttachObjectARB", "glDetachObjectARB", "glShaderSourceARB", "glCompileShaderARB",
    "glLinkProgramARB", "glUseProgramObjectARB", "glValidateProgramARB",
    "glGetObjectParameterivARB", "glGetInfoLogARB", "glGetUniformLocationARB",
    "glGetAttribLocationARB", "glBindAttribLocationARB", "glGetActiveUniformARB",
    "glGetActiveAttribARB", "glGetUniformfvARB", "glGetUniformivARB",
    "glUniform1fARB", "glUniform2fARB", "glUniform3fARB", "glUniform4fARB",
    "glUniform1iARB", "glUniform2iARB", "glUniform3iARB", "glUniform4iARB",
    "glUniform1fvARB", "glUniform2fvARB", "glUniform3fvARB", "glUniform4fvARB",
    "glUniform1ivARB", "glUniform2ivARB", "glUniform3ivARB", "glUniform4ivARB",
    "glUniformMatrix2fvARB", "glUniformMatrix3fvARB", "glUniformMatrix4fvARB",
    "glVertexAttribPointerARB", "glEnableVertexAttribArrayARB",
    "glDisableVertexAttribArrayARB", "glVertexAttrib4fARB", "glVertexAttrib4fvARB",
    // ARB_draw_buffers
    "glDrawBuffersARB", "glDrawBuffers",
    // Blend extensions the wrapper advertises: legacy Minecraft calls
    // glBlendFuncSeparateEXT for translucent foliage/water, and LWJGL only
    // enables the capability when every function of the extension resolves.
    "glBlendFuncSeparateEXT", "glBlendEquationEXT", "glBlendEquationSeparateEXT",
    "glBlendColorEXT", "glBlendFuncSeparate", "glBlendEquationSeparate", "glBlendColor",
    // ARB_vertex_buffer_object / GL15
    "glGenBuffersARB", "glBindBufferARB", "glBufferDataARB", "glBufferSubDataARB",
    "glDeleteBuffersARB", "glIsBufferARB", "glGetBufferParameterivARB", "glMapBufferARB",
    "glUnmapBufferARB", "glGetBufferSubDataARB", "glMapBuffer", "glGetBufferSubData",
};

static const char* kMustAdvertise[] = {
    "GL_EXT_framebuffer_object", "GL_ARB_framebuffer_object", "GL_ARB_shader_objects",
    "GL_ARB_vertex_shader",      "GL_ARB_fragment_shader",    "GL_ARB_shading_language_100",
    "GL_ARB_draw_buffers",       "GL_ARB_vertex_buffer_object",
    "GL_ARB_depth_texture",      "GL_ARB_multitexture",
};

int main(void) {
    void* handle = dlopen(WRAPPER_LIB_PATH, RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        fprintf(stderr, "FAIL: dlopen: %s\n", dlerror());
        return 1;
    }
    typedef void* (*resolver_t)(const char*);
    resolver_t resolve = (resolver_t)dlsym(handle, "eglGetProcAddress");
    if (!resolve) return 1;

    // Resolution requires a loadable backend (GETPROC_BACKEND_ALIAS reads
    // the loaded function table), but not a current context.
    int missing = 0;
    for (unsigned i = 0; i < sizeof(kMustResolve) / sizeof(kMustResolve[0]); ++i) {
        if (resolve(kMustResolve[i]) == NULL) {
            fprintf(stderr, "FAIL: eglGetProcAddress(\"%s\") == NULL\n", kMustResolve[i]);
            ++missing;
        }
    }
    if (missing != 0) return 1;
    printf("OK: %zu desktop FBO/shader/VBO entry points resolve\n",
           sizeof(kMustResolve) / sizeof(kMustResolve[0]));

    // Extension string checks need a real context.
    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display == EGL_NO_DISPLAY || !eglInitialize(display, NULL, NULL)) {
        printf("SKIP: no EGL display; extension-string phase skipped\n");
        return 77;
    }
    static const EGLint config_attribs[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE,
                                            EGL_OPENGL_ES3_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
                                            EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
    EGLConfig config;
    EGLint num_config = 0;
    if (!eglChooseConfig(display, config_attribs, &config, 1, &num_config) || num_config == 0) {
        printf("SKIP: no ES3 config\n");
        return 77;
    }
    eglBindAPI(EGL_OPENGL_ES_API);
    static const EGLint ctx_attribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, ctx_attribs);
    if (context == EGL_NO_CONTEXT ||
        !eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context)) {
        printf("SKIP: no current ES3 context\n");
        return 77;
    }

    typedef const unsigned char* (*getstring_t)(unsigned int);
    getstring_t fGetString = (getstring_t)resolve("glGetString");
    const char* extensions = (const char*)fGetString(0x1F03 /* GL_EXTENSIONS */);
    if (!extensions) {
        fprintf(stderr, "FAIL: glGetString(GL_EXTENSIONS) == NULL\n");
        return 1;
    }
    for (unsigned i = 0; i < sizeof(kMustAdvertise) / sizeof(kMustAdvertise[0]); ++i) {
        if (strstr(extensions, kMustAdvertise[i]) == NULL) {
            fprintf(stderr, "FAIL: extension list lacks %s\n", kMustAdvertise[i]);
            return 1;
        }
    }
    // ADDITIVE contract: the backend's native surface must survive.
    if (strstr(extensions, "GL_OES_") == NULL && strstr(extensions, "GL_KHR_") == NULL) {
        fprintf(stderr, "FAIL: backend's own extensions vanished from the list\n");
        return 1;
    }
    const char* version = (const char*)fGetString(0x1F02 /* GL_VERSION */);
    if (!version || strstr(version, "OpenGL ES") == NULL) {
        fprintf(stderr, "FAIL: GL_VERSION no longer reports the backend: %s\n",
                version ? version : "(null)");
        return 1;
    }
    printf("OK: additive extension surface verified (%s)\n", version);
    return 0;
}
