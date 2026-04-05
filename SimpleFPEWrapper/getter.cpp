#include "GL/gl.h"
#include "init.h"
#include <glm/gtc/type_ptr.hpp>
#include "fpe/fpe.hpp"

inline bool containsMobileGLDev(const std::string& str) {
    return str.find("MobileGL-Dev") != std::string::npos;
}

const GLubyte* glGetString(GLenum name) {
    // we only wrap GL_VERSION GL_RENDERER GL_VENDOR
    switch (name) {
    case GL_VERSION:
        static std::string cachedVersionString;
        if (cachedVersionString.empty()) {
            cachedVersionString =
                std::string((char*)g_glesFuncs.glGetString(GL_VERSION)) + " (with Simple FPE Wrapper)";
        }
        return (const GLubyte*)cachedVersionString.c_str();
    case GL_RENDERER:
        static std::string cachedRendererString;
        if (cachedRendererString.empty()) {
            cachedRendererString = std::string((char*)g_glesFuncs.glGetString(GL_RENDERER)) + " (SFPEW)";
        }
        return (const GLubyte*)cachedRendererString.c_str();
    case GL_VENDOR:
        static std::string cachedVendorString;
        if (cachedVendorString.empty()) {
            cachedVendorString = std::string((char*)g_glesFuncs.glGetString(GL_VENDOR));
            if (!containsMobileGLDev(cachedVendorString)) {
                cachedVendorString += " (SFPEW: MobileGL-Dev)";
            }
        }
        return (const GLubyte*)cachedVendorString.c_str();
    default:
        return g_glesFuncs.glGetString(name);
    }
}

const GLubyte* glGetStringi(GLenum name, GLuint index) {
    if (name != GL_EXTENSIONS) {
        return g_glesFuncs.glGetStringi(name, index);
    }

    switch (index) {
    case 0:
        return (const GLubyte*)"GL_ARB_compatibility";
    case 1:
        return (const GLubyte*)"OpenGL21";
    case 2:
        return (const GLubyte*)"OpenGL11";
    case 3:
        return (const GLubyte*)"OpenGL12";
    case 4:
        return (const GLubyte*)"OpenGL13";
    case 5:
        return (const GLubyte*)"OpenGL14";
    case 6:
        return (const GLubyte*)"OpenGL15";
    case 7:
        return (const GLubyte*)"OpenGL20";
    default:
        return g_glesFuncs.glGetStringi(name, index - 8);
    }
}

void glGetIntegerv(GLenum pname, GLint* params) {
    if (!params) {
        throw std::invalid_argument("params pointer cannot be null");
    }

    switch (pname) {
    case GL_CONTEXT_PROFILE_MASK:
        *params = GL_CONTEXT_COMPATIBILITY_PROFILE_BIT;
        break;
    case GL_CONTEXT_FLAGS:
        *params = 0;
        break;
    case GL_NUM_EXTENSIONS:
        static GLint cachedNumExtensions = -1;
        if (cachedNumExtensions == -1) {
            g_glesFuncs.glGetIntegerv(GL_NUM_EXTENSIONS, &cachedNumExtensions);
            cachedNumExtensions += 8;
        }
        *params = cachedNumExtensions;
        break;
    default:
        g_glesFuncs.glGetIntegerv(pname, params);
        break;
    }
}

void glGetFloatv(GLenum pname, GLfloat* params) {
    switch (pname) {
    case GL_MODELVIEW_MATRIX: {
        auto* ptr = glm::value_ptr(g_glstate.fpe_uniform.transformation.matrices[matrix_idx(GL_MODELVIEW)]);
        memcpy(params, ptr, sizeof(GLfloat) * 16);
        break;
    }
    case GL_PROJECTION_MATRIX: {
        auto* ptr = glm::value_ptr(g_glstate.fpe_uniform.transformation.matrices[matrix_idx(GL_PROJECTION)]);
        memcpy(params, ptr, sizeof(GLfloat) * 16);
        break;
    }
    default:
        g_glesFuncs.glGetFloatv(pname, params);
        break;
    }
}
