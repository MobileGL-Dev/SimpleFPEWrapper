// SimpleFPEWrapper - tests/gtest_beginend_coverage.cc
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// The begin/end coverage sweep is 1400 lines of C tables and probe
// functions, ported verbatim from piglit and kept as C for that reason:
// translating it to C++ buys nothing and risks subtle changes in a sweep
// whose whole point is being exhaustive. This wrapper runs it as-is,
// mapping its exit codes onto the gtest vocabulary - 0 passes, 77 skips.

#include "sfpew_gtest.h"

extern "C" int sfpew_beginend_coverage_run(void);

TEST(BeginEndCoverage, IllegalInBeginEndRaisesWhereEnforced) {
    const int result = sfpew_beginend_coverage_run();
    if (result == 77) {
        GTEST_SKIP() << "no EGL device";
    }
    ASSERT_EQ(result, 0) << "beginend-coverage sweep failed";
}
