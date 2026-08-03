// SimpleFPEWrapper - tests/gtest_lwjgl_surface.cc
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

#include "sfpew_gtest.h"

#include <cstring>

namespace {

using sfpew_test::ContextTest;
using sfpew_test::GLenum;

constexpr GLenum GL_EXTENSIONS_ = 0x1F03;
constexpr GLenum GL_VERSION_ = 0x1F02;

constexpr const char* kMustResolve[] = {
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

constexpr const char* kMustAdvertise[] = {
    "GL_EXT_framebuffer_object", "GL_ARB_framebuffer_object", "GL_ARB_shader_objects",
    "GL_ARB_vertex_shader",      "GL_ARB_fragment_shader",    "GL_ARB_shading_language_100",
    "GL_ARB_draw_buffers",       "GL_ARB_vertex_buffer_object",
    "GL_ARB_depth_texture",      "GL_ARB_multitexture",
};

using LwjglSurfaceTest = ContextTest;

TEST_F(LwjglSurfaceTest, DesktopFboShaderVboSurfaceResolves) {
    // Resolution requires a loadable backend (GETPROC_BACKEND_ALIAS reads
    // the loaded function table), but not a current context.
    int missing = 0;
    for (const char* name : kMustResolve) {
        if (GetOptional<void (*)()>(name) == nullptr) {
            ADD_FAILURE() << "eglGetProcAddress(\"" << name << "\") == NULL";
            ++missing;
        }
    }
    EXPECT_EQ(missing, 0) << "desktop FBO/shader/VBO entry points must all resolve";
}

TEST_F(LwjglSurfaceTest, ExtensionListIsAdditive) {
    auto get_string = Get<const unsigned char* (*)(GLenum)>("glGetString");
    ASSERT_NE(get_string, nullptr);

    const char* extensions = reinterpret_cast<const char*>(get_string(GL_EXTENSIONS_));
    ASSERT_NE(extensions, nullptr) << "glGetString(GL_EXTENSIONS) == NULL";

    for (const char* ext : kMustAdvertise) {
        EXPECT_NE(std::strstr(extensions, ext), nullptr) << "extension list lacks " << ext;
    }

    // ADDITIVE contract: the backend's native surface must survive.
    EXPECT_TRUE(std::strstr(extensions, "GL_OES_") != nullptr ||
                std::strstr(extensions, "GL_KHR_") != nullptr)
        << "backend's own extensions vanished from the list";

    const char* version = reinterpret_cast<const char*>(get_string(GL_VERSION_));
    EXPECT_NE(version, nullptr);
    if (version != nullptr) {
        EXPECT_NE(std::strstr(version, "OpenGL ES"), nullptr)
            << "GL_VERSION no longer reports the backend: " << version;
    }
}

} // namespace
