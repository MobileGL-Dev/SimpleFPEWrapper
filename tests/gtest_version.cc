// SimpleFPEWrapper - tests/gtest_version.cc
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
// asserted - only its shape - so a release bump does not break these.
//
// Both backends are checked, because they are reported differently: an ES
// backend is presented as its desktop equivalent with the wrapper named
// right after the level, while a desktop backend's string already parses and
// so is kept verbatim with the wrapper appended. The strings are cached per
// process, so the two cases cannot share one.

#include "sfpew_gtest.h"

#include <cctype>
#include <cstring>
#include <string>

namespace {

using sfpew_test::ContextTest;
using sfpew_test::DesktopContextTest;
using sfpew_test::GLenum;
using sfpew_test::GLubyte;

constexpr GLenum GL_VERSION_ = 0x1F02;
constexpr GLenum GL_RENDERER_ = 0x1F01;

// The version token that follows `marker`, up to whatever separator ends it
// in that string ("," in GL_VERSION, ")" in GL_RENDERER).
std::string VersionAfter(const std::string& text, const char* marker) {
    const size_t at = text.find(marker);
    if (at == std::string::npos) return {};
    const size_t start = at + std::strlen(marker);
    const size_t end = text.find_first_of(",) ", start);
    return text.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

// "26.08", optionally ".<patch>", optionally a "-suffix".
bool LooksLikeCalendarVersion(const std::string& v) {
    size_t i = 0;
    auto digits = [&] {
        const size_t start = i;
        while (i < v.size() && std::isdigit(static_cast<unsigned char>(v[i]))) ++i;
        return i - start;
    };
    if (digits() < 2) return false;
    if (i >= v.size() || v[i] != '.') return false;
    ++i;
    if (digits() < 2) return false;
    if (i < v.size() && v[i] == '.') {
        ++i;
        if (digits() == 0) return false;
    }
    return i == v.size() || v[i] == '-';
}

struct Identity {
    std::string version;
    std::string renderer;
};

Identity ReadIdentity(ContextTest* test) {
    auto get_string = test->Get<const GLubyte* (*)(GLenum)>("glGetString");
    if (get_string == nullptr) return {};
    const auto* version = get_string(GL_VERSION_);
    const auto* renderer = get_string(GL_RENDERER_);
    EXPECT_NE(version, nullptr);
    EXPECT_NE(renderer, nullptr);
    return {version ? reinterpret_cast<const char*>(version) : "",
            renderer ? reinterpret_cast<const char*>(renderer) : ""};
}

// Shared by both backends: whatever the shape, these have to hold.
void CheckCommonIdentity(const Identity& id, const std::string& version_token) {
    // A desktop loader parses the leading "<major>.<minor>": the wrapper's
    // version must not have displaced it.
    ASSERT_FALSE(id.version.empty());
    EXPECT_TRUE(std::isdigit(static_cast<unsigned char>(id.version[0])))
        << "GL_VERSION no longer starts with the GL level: " << id.version;

    EXPECT_TRUE(LooksLikeCalendarVersion(version_token))
        << '"' << version_token << "\" is not a calendar version (expected e.g. 26.08-dev)";

    // The commit is what actually identifies a build, so it travels with it.
    const size_t git = id.version.find("GIT@");
    ASSERT_NE(git, std::string::npos) << "GL_VERSION carries no commit: " << id.version;
    const std::string hash = id.version.substr(git + 4);
    EXPECT_FALSE(hash.empty());
    EXPECT_NE(hash[0], ' ');
    EXPECT_NE(hash[0], ')');

    // GL_RENDERER repeats the version, so a report quoting only that line
    // still carries it.
    const std::string from_renderer = VersionAfter(id.renderer, "(SFPEW ");
    EXPECT_EQ(from_renderer, version_token)
        << "GL_VERSION and GL_RENDERER disagree: " << id.version << " / " << id.renderer;
}

class VersionTest : public ContextTest {};
class VersionDesktopTest : public DesktopContextTest {};

TEST_F(VersionTest, GlesBackendIsPresentedWithTheWrapperNamedAfterTheLevel) {
    const Identity id = ReadIdentity(this);
    RecordProperty("GL_VERSION", id.version);
    RecordProperty("GL_RENDERER", id.renderer);
    CheckCommonIdentity(id, VersionAfter(id.version, "SFPEW "));
    // Additive contract: the backend's own string is quoted after ours.
    EXPECT_NE(id.version.find("OpenGL ES"), std::string::npos)
        << "the backend's version string is not in GL_VERSION: " << id.version;
}

TEST_F(VersionDesktopTest, DesktopBackendKeepsItsOwnStringAndIsAppendedTo) {
    const Identity id = ReadIdentity(this);
    RecordProperty("GL_VERSION", id.version);
    RecordProperty("GL_RENDERER", id.renderer);
    CheckCommonIdentity(id, VersionAfter(id.version, "Simple FPE Wrapper "));
    // A desktop backend's string IS the front of the line, so the wrapper's
    // part must never have taken position zero.
    const size_t suffix = id.version.find("(with ");
    ASSERT_NE(suffix, std::string::npos) << id.version;
    EXPECT_GT(suffix, 0u) << "the backend's own version string was displaced";
}

} // namespace
