#!/usr/bin/env python3
# SimpleFPEWrapper - tools/check_backend_profile.py
# Copyright (c) 2026 MobileGL-Dev
# Licensed under the GNU Lesser General Public License v3.0:
#   https://www.gnu.org/licenses/gpl-3.0.txt
#   https://www.gnu.org/licenses/lgpl-3.0.txt
# SPDX-License-Identifier: LGPL-3.0-only
# End of Source File Header

"""Enforces the backend support profile documented in docs/backend-support.md.

The wrapper runs on either a desktop GL 3.2+ or a GLES 3.0+ backend and no
codepath branches on which one it got (docs/backend-support.md). That only
holds while every backend entry point outside the GL 3.2 n ES 3.0 intersection
stays behind a null check, because on the backend that lacks it
eglGetProcAddress returns null and an unguarded call segfaults.

Two things are checked:

1. Every entry point in NON_UNIVERSAL is null-guarded at each call site.
   An unguarded one is a hard error - that is the crash this file exists to
   prevent.
2. The declared entry point set matches docs/backend-profile.json. A new
   GL_FUNC_TYPEDEF changes the generated file and fails the comparison, which
   forces whoever added it to decide whether it needs a guard rather than
   letting NON_UNIVERSAL silently go stale.

Usage:
  check_backend_profile.py <repo-root>                     report to stdout, rule 1
  check_backend_profile.py <repo-root> --write <file>       regenerate the profile
  check_backend_profile.py <repo-root> --check <file>       rules 1 and 2 (CI gate)
"""

import json
import os
import re
import sys

# Backend entry points NOT in the GL 3.2 n ES 3.0 intersection: available on
# one backend but needing a newer version or an extension on the other, so a
# call must be null-guarded. Value is the reason, kept for the report.
#
# Sources: OpenGL 4.6 and OpenGL ES 3.2 specs, plus the EXT/ARB registry.
# Adding an entry point here is how you declare "needs a guard"; the
# comparison in rule 2 will tell you when that decision is needed.
NON_UNIVERSAL = {
    "glBindVertexBuffer": "GL 4.3 / ES 3.1",
    "glBlendBarrier": "ES 3.2 only (GL: KHR_blend_equation_advanced)",
    "glBlendEquationi": "GL 4.0 / ES 3.2",
    "glBlendFunci": "GL 4.0 / ES 3.2",
    "glBufferStorage": "GL 4.4 / ES: EXT_buffer_storage",
    "glColorMaski": "GL 3.0 / ES 3.2",
    "glCopyImageSubData": "GL 4.3 / ES 3.2",
    "glDebugMessageCallback": "GL 4.3 / ES 3.2",
    "glDisablei": "GL 3.0 / ES 3.2",
    "glDispatchCompute": "GL 4.3 / ES 3.1",
    "glDrawElementsBaseVertex": "GL 3.2 / ES 3.2",
    "glEnablei": "GL 3.0 / ES 3.2",
    "glFramebufferTexture": "GL 3.2 / ES 3.2",
    "glGetCompressedTexImage": "GL only: ES has no texture readback",
    "glGetTexImage": "GL only: ES has no texture readback",
    "glGetTexLevelParameteriv": "GL 1.0 / ES 3.1",
    "glMemoryBarrier": "GL 4.2 / ES 3.1",
    "glMinSampleShading": "GL 4.0 / ES 3.2",
    "glMultiDrawArrays": "GL 1.4 / ES: EXT_multi_draw_arrays",
    "glMultiDrawElements": "GL 1.4 / ES: EXT_multi_draw_arrays",
    "glMultiDrawElementsBaseVertex": "GL 3.2 / ES: EXT_multi_draw_elements_base_vertex",
    "glObjectLabel": "GL 4.3 / ES 3.2",
    "glPatchParameteri": "GL 4.0 / ES 3.2",
    "glPopDebugGroup": "GL 4.3 / ES 3.2",
    "glPrimitiveBoundingBox": "ES 3.2 only",
    "glPushDebugGroup": "GL 4.3 / ES 3.2",
    "glReadnPixels": "GL 4.5 / ES 3.2",
    "glTexBuffer": "GL 3.1 / ES 3.2",
    "glVertexAttribBinding": "GL 4.3 / ES 3.1",
    "glVertexAttribFormat": "GL 4.3 / ES 3.1",
}

TYPEDEF_RE = re.compile(r"GL_FUNC_TYPEDEF\(\s*[^,]+,\s*(\w+)")

# A vendor/extension suffix already says "not core on either backend", so these
# need a guard by construction and are derived rather than hand-listed. Keeping
# them out of NON_UNIVERSAL means the table needs no edit for every new
# extension alias, but they are guard-checked exactly the same way.
SUFFIX_RE = re.compile(r"(EXT|OES|ARB|KHR|NV|AMD|IMG|APPLE|QCOM|ANGLE)$")


def guard_required(declared):
    """Entry points whose call sites must be null-guarded."""
    required = {name: "vendor/extension suffix: never core"
                for name in declared if SUFFIX_RE.search(name)}
    required.update(NON_UNIVERSAL)
    return required


def declared_entry_points(repo):
    header = os.path.join(repo, "SimpleFPEWrapper", "backend", "loader.h")
    with open(header, encoding="utf-8", errors="replace") as handle:
        return sorted(set(TYPEDEF_RE.findall(handle.read())))


def sources(repo):
    root = os.path.join(repo, "SimpleFPEWrapper")
    for base, _dirs, files in os.walk(root):
        for name in files:
            if name.endswith((".cpp", ".h", ".hpp")):
                yield os.path.join(base, name)


def audit_guards(repo, names):
    """Finds calls to `names` through g_glFuncs that no null check protects.

    A guard is a `!= nullptr` / `== nullptr` / truthiness test naming the same
    entry point earlier in the enclosing function. Scanning back a bounded
    window rather than parsing C++ keeps this cheap; the window is generous
    because the guard is often at the top of a long function.
    """
    unguarded = []
    call_counts = {name: 0 for name in names}
    for path in sources(repo):
        with open(path, encoding="utf-8", errors="replace") as handle:
            lines = handle.readlines()
        for index, line in enumerate(lines):
            stripped = line.lstrip()
            if stripped.startswith("//") or stripped.startswith("*"):
                continue
            for name in names:
                # A call looks like g_glFuncs.<name>( - a bare mention in a
                # null test or an assignment does not.
                if f"g_glFuncs.{name}(" not in line:
                    continue
                call_counts[name] += 1
                window = "".join(lines[max(0, index - 60):index + 1])
                guarded = (
                    f"{name} != nullptr" in window
                    or f"{name} == nullptr" in window
                    or f"g_glFuncs.{name} &&" in window
                    or f"g_glFuncs.{name})" in window
                    or f"auto {name}" in window
                )
                if not guarded:
                    rel = os.path.relpath(path, repo)
                    unguarded.append(f"{rel}:{index + 1}: unguarded g_glFuncs.{name}")
    return call_counts, unguarded


def main(argv):
    if len(argv) < 2:
        sys.stderr.write(__doc__)
        return 2
    repo = argv[1]
    mode = argv[2] if len(argv) > 2 else None
    target = argv[3] if len(argv) > 3 else None
    if mode in ("--write", "--check") and target is None:
        sys.stderr.write(f"{mode} needs a file path\n")
        return 2

    declared = declared_entry_points(repo)
    if not declared:
        sys.stderr.write("no GL_FUNC_TYPEDEF found in backend/loader.h\n")
        return 1

    needs_guard = guard_required(declared)

    # NON_UNIVERSAL must describe entry points that actually exist, or a
    # rename would quietly drop a guard requirement.
    unknown = sorted(set(NON_UNIVERSAL) - set(declared))
    if unknown:
        sys.stderr.write(
            "NON_UNIVERSAL names no longer declared in loader.h (renamed or removed):\n"
        )
        for name in unknown:
            sys.stderr.write(f"  {name}\n")
        return 1

    call_counts, unguarded = audit_guards(repo, sorted(needs_guard))

    report = {
        "profile": "desktop GL 3.2+ or GLES 3.0+",
        "doc": "docs/backend-support.md",
        "declared_entry_points": len(declared),
        "universal": len(declared) - len(needs_guard),
        "needs_guard": {
            name: {"availability": needs_guard[name], "call_sites": call_counts[name]}
            for name in sorted(needs_guard)
        },
        "entry_points": declared,
    }

    rendered = json.dumps(report, indent=2, sort_keys=True) + "\n"
    stale = False
    if mode == "--write":
        os.makedirs(os.path.dirname(os.path.abspath(target)), exist_ok=True)
        with open(target, "w", encoding="utf-8") as handle:
            handle.write(rendered)
    elif mode == "--check":
        try:
            with open(target, encoding="utf-8") as handle:
                committed = handle.read()
        except OSError:
            committed = None
        if committed != rendered:
            stale = True
    else:
        sys.stdout.write(rendered)

    if stale:
        sys.stderr.write(
            f"\n{target} is stale. The declared backend entry point set changed.\n"
            "Regenerate it, and if a new entry point is outside the\n"
            "GL 3.2 n ES 3.0 intersection add it to NON_UNIVERSAL with a guard:\n"
            f"  python3 tools/check_backend_profile.py . --write {target}\n"
        )

    if unguarded:
        sys.stderr.write(
            "\nBackend calls that would crash on the backend lacking them.\n"
            "Guard each with `if (g_glFuncs.<name> != nullptr)` or provide a\n"
            "fallback (see docs/backend-support.md):\n"
        )
        for entry in unguarded:
            sys.stderr.write(f"  {entry}\n")
    return 1 if (unguarded or stale) else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
