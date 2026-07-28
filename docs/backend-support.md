# Backend support

SimpleFPEWrapper runs on **either** of these backends:

- **OpenGL ES 3.0 or newer**
- **Desktop OpenGL 3.2 or newer**

The wrapper never selects a codepath from the backend's identity. It does not
call `eglBindAPI`, so the API in force is whatever the host bound before
creating its context, and the wrapper simply uses it.

## How the backend is obtained

`backend/loader.cpp` `dlopen`s **only `libEGL.so`** and resolves every GL entry
point through `eglGetProcAddress`. No GL or GLES library is opened directly, and
no name is hardcoded per backend. Whatever EGL hands back for the current
context is the backend.

A missing entry point resolves to `nullptr`, which is the wrapper's single
capability signal: 417 pointers, each either usable or absent.

## Why the same calls work on both

Of the 417 declared backend entry points, 390 are inside the
GL 3.2 ∩ ES 3.0 intersection and need no thought. The other 27 differ in which
core version first provides them, so each is either never called or guarded:

| entry point | desktop | ES | status in this codebase |
|---|---|---|---|
| `glBindVertexBuffer` | 4.3 | 3.1 | called, guarded |
| `glVertexAttribFormat` | 4.3 | 3.1 | called, guarded |
| `glVertexAttribBinding` | 4.3 | 3.1 | called, guarded |
| `glDrawElementsBaseVertex` | 3.2 | 3.2 / `EXT_draw_elements_base_vertex` | called, guarded |
| `glMultiDrawArrays` | 1.4 | not in ES core | called, guarded |
| `glMultiDrawElementsBaseVertex` | 3.2 | `EXT_multi_draw_elements_base_vertex` | called, guarded |
| `glGetTexLevelParameteriv` | 1.0 | 3.1 | called, guarded |
| `glBufferStorage` | 4.4 | `EXT_buffer_storage` | guarded, with `glBufferStorageEXT` fallback |
| 19 others | — | — | declared, never called |

"Guarded" means every call site tests the pointer against `nullptr` first and has
a fallback or skip. The three separate-attribute-format calls are gated together
by `use_separate_binding` in `glstate.cpp`, which falls back to
`glVertexAttribPointer`; `glDrawElementsBaseVertex` falls back to baked indices;
`glBufferStorage` falls back to `glBufferData` orphaning.

Verified by audit rather than assumption: no entry point outside the
intersection is called without a guard.

## Cross-check against MobileGL

MobileGL's `GLImpl/Exporting/Definitions.cpp` implements 539 GL entry points for
real and stubs out 2229. Every one of the 27 above that this wrapper actually
calls is a real implementation there; every one MobileGL stubs out
(`glBlendBarrier`, `glDebugMessageCallback`, `glMinSampleShading`,
`glPatchParameteri`, `glPushDebugGroup`, `glPopDebugGroup`,
`glPrimitiveBoundingBox`, `glReadnPixels`) falls in this wrapper's never-called
set. Two independently written codebases agree on which entry points are safe
across both backends.

## What the ES detection is for

`sfpewDesktopGLVersion` (`getter.cpp`) is the only place that distinguishes the
backends, and it does so **only to report a version**. Desktop GL loaders parse
`<major>.<minor>` out of `GL_VERSION` and abort on `"OpenGL ES ..."`, so an ES
backend is reported as its desktop equivalent (ES 3.0 → 3.3, ES 3.1 → 4.3,
ES 3.2 → 4.5) with the backend's real string preserved in the suffix:

```
GL_VERSION: 4.5 SFPEW (OpenGL ES 3.2 NVIDIA 610.43.03)
```

For a desktop backend the function returns false and the version is reported
verbatim. Nothing else in the wrapper reads this result — it does not gate a
feature, pick a shader path, or change a draw path.

## Enforcement

This document is a claim; `tools/check_backend_profile.py` is what keeps it
true, wired in as the `backend_profile_current` ctest. It fails on:

1. **A call to a non-universal entry point that no null check protects.** This
   is the crash it exists to prevent: on the backend that lacks the entry point
   `eglGetProcAddress` returns null and the call segfaults. The check scans
   backwards from each call site, so a guard in a *different* function no longer
   counts — which is how it caught the `glMultiDrawArrays` call in
   `tryExecuteCapturedDisplayLists` that sat 150 lines below its early-out.
2. **A newly declared entry point nobody classified.** Any new
   `GL_FUNC_TYPEDEF` changes the generated profile and fails the comparison
   against `docs/backend-profile.json`, forcing a decision about whether it
   needs a guard rather than letting the table above quietly go stale.

Regenerate after an intentional change:

```
python3 tools/check_backend_profile.py . --write docs/backend-profile.json
```

## Test coverage

The suite runs against an ES 3 context (`eglBindAPI(EGL_OPENGL_ES_API)`,
`EGL_OPENGL_ES3_BIT`, `EGL_CONTEXT_CLIENT_VERSION 3`), so **ES is what CI
exercises**. Desktop-backend support rests on the audit above plus manual
testing, not on automated behavioural coverage — the profile check is static.
A desktop-context variant of the smoke tests would close that gap.
