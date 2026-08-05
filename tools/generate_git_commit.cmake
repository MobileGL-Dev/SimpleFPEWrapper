# SimpleFPEWrapper - tools/generate_git_commit.cmake
# Copyright (c) 2026 MobileGL-Dev
# Licensed under the GNU Lesser General Public License v3.0:
#   https://www.gnu.org/licenses/gpl-3.0.txt
#   https://www.gnu.org/licenses/lgpl-3.0.txt
# SPDX-License-Identifier: LGPL-3.0-only
# End of Source File Header

# Run via `cmake -D SRC_DIR=... -D OUT_FILE=... -P` from an `add_custom_target(... ALL ...)`
# in the main CMakeLists.txt, so it reruns on every build invocation, not
# only when CMake itself is reconfigured - see that target's comment for why.

find_package(Git QUIET)
set(SFPEW_GIT_COMMIT "unknown")
if(GIT_FOUND)
        execute_process(COMMAND ${GIT_EXECUTABLE} -C ${SRC_DIR} rev-parse --short=12 HEAD
                        OUTPUT_VARIABLE SFPEW_GIT_COMMIT_RAW
                        OUTPUT_STRIP_TRAILING_WHITESPACE
                        ERROR_QUIET RESULT_VARIABLE SFPEW_GIT_RC)
        if(SFPEW_GIT_RC EQUAL 0)
                set(SFPEW_GIT_COMMIT "${SFPEW_GIT_COMMIT_RAW}")
        endif()
endif()

set(SFPEW_GIT_COMMIT_HEADER_CONTENT
        "// Generated at build time by tools/generate_git_commit.cmake - do not edit or commit.\n#pragma once\n#define SFPEW_GIT_COMMIT \"${SFPEW_GIT_COMMIT}\"\n")

# Only touch the file - and only then invalidate the couple of translation
# units that #include it - when the commit actually changed, instead of on
# every single build regardless.
if(EXISTS "${OUT_FILE}")
        file(READ "${OUT_FILE}" SFPEW_GIT_COMMIT_HEADER_EXISTING)
else()
        set(SFPEW_GIT_COMMIT_HEADER_EXISTING "")
endif()
if(NOT SFPEW_GIT_COMMIT_HEADER_EXISTING STREQUAL SFPEW_GIT_COMMIT_HEADER_CONTENT)
        file(WRITE "${OUT_FILE}" "${SFPEW_GIT_COMMIT_HEADER_CONTENT}")
endif()
