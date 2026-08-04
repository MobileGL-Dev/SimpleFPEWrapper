# SimpleFPEWrapper — Remaining Entry Point Implementation Plan

Status legend: `[ ]` not started · `[~]` partial · `[x]` complete (implemented,
registered, queryable, built, tested)

**Rule for whoever executes this plan: tick a box ONLY when all six gates of
§0.7 pass for that entry point.** A resolvable symbol with no behavior is worse
than an absent one — it silently lies to the application.

---

## 0. Read This First

### 0.1 What this project is

`libSimpleFPEWrapper.so` is a drop-in `libGL` replacement: it implements the
OpenGL 1.x fixed-function pipeline plus GL 2.1 on top of a backend that is
either **desktop GL 3.2+ core** or **GLES 3.0+**. On Android it ships as a
MobileGlues plugin. LGPL-3.0-only.

The API contract has three parts (`ROADMAP.md`, "Product Boundary"):

1. The symbol is exported or returned by `eglGetProcAddress`.
2. The call has the documented fixed-function behavior.
3. Unsupported calls fail **predictably through the GL error path** rather than
   silently doing nothing, forwarding to an incompatible backend entry point,
   or crashing.

Part 3 is why this plan exists. Everything below is currently in violation of
part 1, part 2, or both.

### 0.2 The two-backend invariant (do not break this)

`docs/backend-support.md` and `tools/check_backend_profile.py` enforce it: **no
codepath branches on which backend is present.** The backend call surface is
restricted to the GL 3.2 ∩ ES 3.0 intersection, and anything outside it is
null-guarded at *every* call site.

Practical consequences for this plan:

- `g_glFuncs.glFoo` for any `glFoo` outside the intersection **must** be
  null-checked at every call. `tools/check_backend_profile.py` is a CTest
  (`backend_profile_current`) and will hard-fail the build otherwise.
- Adding a `GL_FUNC_TYPEDEF` to `backend/loader.h` changes the generated
  profile and fails `backend_profile_current` until you classify it. Regenerate
  with `python3 tools/check_backend_profile.py . --write docs/backend-profile.json`
  and read the diff before committing it.
- Where the two backends genuinely disagree (e.g. `glClearDepth` vs
  `glClearDepthf`), the *wrapper* absorbs the difference behind one entry point.
  That is not "branching on the backend" — it is presenting one desktop API on
  top of whichever floor is there.

### 0.3 Key source files and what lives in them

| File | Role |
|---|---|
| `SimpleFPEWrapper/lookup.cpp` | `eglGetProcAddress`. **Every** new entry point must be registered here or it does not exist to an application. |
| `SimpleFPEWrapper/init.h` | Public declarations of wrapper-implemented entry points (`SFPEW_APIENTRY`). |
| `SimpleFPEWrapper/getter.cpp` | The whole `glGet*` family; `fixedFunctionState()` answers all 253 legacy state variables; also holds `glTexImage1D`/`glTexSubImage1D`/`glTexImage2D`. |
| `SimpleFPEWrapper/ordered_passthrough.cpp` | `ORDERED_PASSTHROUGH` / `RECORDED_PASSTHROUGH` macros: entry points that only need a barrier + optional display-list record + a forward. |
| `SimpleFPEWrapper/fpe/types.h` | `glstate_t` (per-context state), `fixed_function_state_t`, `fixed_function_uniform_t`, `fixed_function_draw_data_t`. All new shadow state goes here. |
| `SimpleFPEWrapper/fpe/state.cpp` | `glEnable`/`glDisable`/`glFog*`/`glTexEnv*`/`glPolygonMode` and friends. |
| `SimpleFPEWrapper/fpe/pixelops.cpp` | CPU pixel paths: `glBitmap`, `glDrawPixels`, `glCopyPixels`, `glAccum`, the shared screen-aligned quad drawer, pixel-map getters. |
| `SimpleFPEWrapper/fpe/drawing1x.h` / `.cpp` | Immediate mode: `mglVertex`/`mglColor`/`mglFogCoord` templates, the batch collector, `sfpewEntryBarrier()`. |
| `SimpleFPEWrapper/fpe/transformation.cpp` | Matrix stacks, `glLoadMatrix*`/`glMultMatrix*`. |
| `SimpleFPEWrapper/fpe/list.h` | `LIST_RECORD` / `SELF_CALL` display-list capture. |
| `SimpleFPEWrapper/fpe/vertexpointer.cpp` | `gl*Pointer` family, including `glSecondaryColorPointer`. |
| `SimpleFPEWrapper/backend/loader.h` / `.cpp` | Backend function table (`GL_FUNC_TYPEDEF` / `GL_FUNC_DECL` / `INIT_BACKENDGL_FUNC`). |

### 0.4 The five patterns you will reuse constantly

**(a) Resolve per-context state.** Touching `g_glstate` performs the strict
context resolve; do it once at the top of the entry point:

```cpp
auto& gs = g_glstate;
```

**(b) Raise a GL error.** Never `return` silently on a bad argument:

```cpp
gs.set_error(GL_INVALID_ENUM);   // keeps only the FIRST error, per spec
return;
```

**(c) Drain the pending immediate-mode batch.** Every entry point that is not
part of the immediate-mode vertex family must call this before touching the
backend, or geometry collected but not yet submitted is reordered against your
call:

```cpp
sfpewEntryBarrier();
```

**(d) Record into a display list.** GL 2.1 §5.4 lists which commands compile.
Everything in this plan except the `glGet*`/`glAre*` queries compiles:

```cpp
LIST_RECORD(glColorTable, {{5, tableSizeInBytes}}, target, internalformat, width, format, type, table)
//                          ^ pointer-capture list: {argument_index, byte_count}
```

The `{{index, bytes}}` list deep-copies pointer arguments into the list command
so the caller's buffer may die immediately after. `{}` means no pointers.
Getters and `glAreTexturesResident` must **not** record (they execute
immediately even inside `glNewList`).

**(e) Call the backend, guarded.**

```cpp
if (g_glFuncs.glCompressedTexImage2D != nullptr)
    g_glFuncs.glCompressedTexImage2D(...);
```

### 0.5 Immediate-mode entry points are special

`glSecondaryColor3*` and `glVertexAttrib*` (Group F/G) are *vertex data*
commands. They must:

- use `sfpewVertexDataState()` (not `g_glstate`) to reach state — it is the
  relaxed resolve the vertex family uses;
- **not** call `sfpewEntryBarrier()` — that would break every batch;
- go through an `mgl*` template in `drawing1x.h` so the display-list
  immediate-class classifier (`sfpew_imm::classify`) sees them correctly.

Look at `mglFogCoord` in `fpe/drawing1x.h:108` as the reference shape, and
`glFogCoordf` in `fpe/state.cpp:341` as the reference entry point.

### 0.6 The 1D-texture convention already in place

GLES has no `GL_TEXTURE_1D`. The wrapper stores 1D textures as **N×1 2D
textures** (`getter.cpp:1001`, `fbo.cpp:75`). Any wrap mode samples row 0 of an
N×1 texture, so generated shaders need no coordinate rewrite, and
`GL_TEXTURE_1D` shares the 2D unit enable (`getter.cpp:1247`). Every 1D entry
point in Group B follows this: validate the 1D target, then delegate to the 2D
entry point with `height = 1`.

### 0.7 Definition of done (the six gates)

For **each** entry point, before you tick its box:

1. **Implemented** in the right file, following §0.4/§0.5.
2. **Declared** in `SimpleFPEWrapper/init.h` with `SFPEW_APIENTRY`.
3. **Registered** in `lookup.cpp` with `GETPROC`, plus every EXT/ARB/SGI alias
   an LWJGL-era frontend might ask for (`GETPROC_WRAPPER_ALIAS`).
4. **Queryable**: its state answers through `glGetIntegerv`/`Floatv`/`Doublev`/
   `Booleanv` via `fixedFunctionState()` in `getter.cpp`, and through its own
   dedicated getter where the spec has one.
5. **Display-list correct**: compiles via `LIST_RECORD` if §5.4 says it should,
   executes immediately if it should not.
6. **Built and tested**: `cmake --build build -j$(nproc)` clean, a gtest suite
   added under `tests/`, and `ctest --test-dir build` green. Then regenerate
   `docs/api-manifest.json` and `docs/backend-profile.json`.

### 0.8 Build, test, regenerate

```bash
cd /home/bzlzhh/Documents/Projects/CPP/SimpleFPEWrapper
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
# real NVIDIA rather than llvmpipe:
EGL_PLATFORM=x11 ctest --test-dir build --output-on-failure
python3 tools/gen_api_manifest.py .            # rewrites docs/api-manifest.{json,md}
python3 tools/check_backend_profile.py . --write docs/backend-profile.json
```

`ctest` currently stands at **3026/3026**. It must still be green, with your new
suites added, when you are done.

### 0.9 Test conventions

Tests are gtest, one file per suite: `tests/gtest_<suite>.cc`, registered in
`CMakeLists.txt`. Use the fixtures in `tests/sfpew_gtest.h`:

- `LibraryTest` — `dlopen` only, **no** GL context. Use it for shape/validation
  checks that must not have a context.
- `ContextTest` — GLES3 context via the wrapper's own EGL. `Get<Fn>("glFoo")`
  resolves through `eglGetProcAddress`; `GetOptional<Fn>` tolerates absence;
  `Dlsym<Fn>` reaches `sfpew*ForTest` hooks.
- `DesktopContextTest` — desktop **core** profile. Never compatibility: a
  compatibility context answers everything GL 2.1 ever had and therefore proves
  nothing.
- `PixelProbe` + `SFPEW_EXPECT_LIT` / `SFPEW_EXPECT_BLANK` for render checks.

Missing EGL/config/context → `GTEST_SKIP()`. `gtest_discover_tests` gives each
`TEST` its own process, which is required because the wrapper caches the backend
resolve, identity strings and the GLES-vs-desktop decision in function-local
statics. **Corollary: phases that share state must stay in one `TEST`**, with a
comment saying why.

### 0.10 Commit convention (mandatory)

Format: `[Tag] (Scope): Subject`, then a prose body explaining *why*.

Tags: `Improvement` `Fix` `Test` `Docs` `Debug` `Perf` `Build` `Bench` `Chore`
`CI` `Style` `Revert`. Scopes seen in history: `FPE` `Perf` `Shader` `Texture`
`List` `CTest` `Getter` `Version` — add `Imaging`, `PointParam`, `VertexAttrib`
as needed.

Author `BZLZHH <admin@bzlzhh.top>`. **Never** add `Co-Authored-By`. **Never**
push — that is the maintainer's call. Do not touch untracked paths
(`SIMPLE_WRITE_PROBE`, `sfpew_updates.txt`, `test/`, `sfpew-release.apk*`,
`Testing/`, `history/`).

One commit per group (A/B/C/…), or per sub-group where a group is large.

### 0.11 Reference material

`docs.gl` sources are at `/docsgl/`; the GL 2.1 pages are `/docsgl/gl2/*.xhtml`
(352 pages). Read the actual page for each function — error conditions
especially. Do not implement from memory.

### 0.12 Recommended order

Groups D → C → H → F → G → E → B → I → A. That is easiest-first: D and C are
self-contained, H fixes real bugs cheaply, F/G are mechanical breadth, E touches
the shader generator, B touches the texture path, and A is the largest
subsystem and benefits from everything before it being settled.

---

## Group D — Transpose Matrices (4 entry points)

**Why first:** entirely self-contained, no new state, no backend calls, and it
establishes the rhythm for the rest.

`GL_ARB_transpose_matrix`, core in GL 1.3. These take a matrix in **row-major**
order instead of GL's usual column-major, and are otherwise exactly
`glLoadMatrix*` / `glMultMatrix*`.

| Entry point | Status |
|---|---|
| `glLoadTransposeMatrixf` | [x] |
| `glLoadTransposeMatrixd` | [x] |
| `glMultTransposeMatrixf` | [x] |
| `glMultTransposeMatrixd` | [x] |

### Implementation — `SimpleFPEWrapper/fpe/transformation.cpp`

GLM already has the operation: `glm::transpose`. Read the existing
`glLoadMatrixf` / `glMultMatrixf` in this file and mirror them exactly,
transposing on the way in.

```cpp
// GL_ARB_transpose_matrix (core in GL 1.3): the caller's 16 floats are in
// row-major order, which is the transpose of everything else in GL.
void glLoadTransposeMatrixf(const GLfloat* m) {
    if (m == nullptr) return;
    LIST_RECORD(glLoadTransposeMatrixf, {{0, 16 * sizeof(GLfloat)}}, m)
    GLfloat column_major[16];
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) column_major[c * 4 + r] = m[r * 4 + c];
    SELF_CALL(glLoadMatrixf, column_major)
}
```

`SELF_CALL` suppresses the inner entry point's own `LIST_RECORD` so the command
is captured once, under its real name. Do the same for the `d` variants (they
take `const GLdouble*`) and for the two `Mult` forms.

### Registration — `lookup.cpp`

```cpp
GETPROC(glLoadTransposeMatrixf, name)
GETPROC(glLoadTransposeMatrixd, name)
GETPROC(glMultTransposeMatrixf, name)
GETPROC(glMultTransposeMatrixd, name)
GETPROC_WRAPPER_ALIAS(glLoadTransposeMatrixfARB, glLoadTransposeMatrixf)
GETPROC_WRAPPER_ALIAS(glLoadTransposeMatrixdARB, glLoadTransposeMatrixd)
GETPROC_WRAPPER_ALIAS(glMultTransposeMatrixfARB, glMultTransposeMatrixf)
GETPROC_WRAPPER_ALIAS(glMultTransposeMatrixdARB, glMultTransposeMatrixd)
```

Add `"GL_ARB_transpose_matrix"` to `kDesktopExtensions` (`getter.cpp:729`).

### Notes

- **No new queryable state.** `GL_TRANSPOSE_MODELVIEW_MATRIX` and friends are
  *derived* pnames — they return the transpose of the corresponding matrix.
  Check whether `fixedFunctionState()` already answers
  `GL_TRANSPOSE_MODELVIEW_MATRIX` / `GL_TRANSPOSE_PROJECTION_MATRIX` /
  `GL_TRANSPOSE_TEXTURE_MATRIX` / `GL_TRANSPOSE_COLOR_MATRIX`
  (`grep -n TRANSPOSE SimpleFPEWrapper/getter.cpp`). If not, add them: same
  source matrix as the non-transpose pname, written out transposed. Those four
  are part of this group's gate 4.
- Matrix mode applies as usual, including `GL_TEXTURE` targeting the active
  unit and `GL_COLOR`.

### Test — `tests/gtest_transpose_matrix.cc`

`ContextTest`. For each of the four: build an asymmetric matrix (so a missed
transpose cannot pass), load/multiply it, and read back
`GL_MODELVIEW_MATRIX` — expect the column-major transpose. Then read
`GL_TRANSPOSE_MODELVIEW_MATRIX` and expect the original row-major bytes.
Cover `glMultTransposeMatrix*` composing onto a non-identity matrix, and one
case under `glMatrixMode(GL_TEXTURE)`.

Register in `CMakeLists.txt` by adding `transpose_matrix` to the driver-needing
`foreach(suite ...)` list.

---

## Group C — Texture Residency (2 entry points)

**Why second:** it is the clearest case in the whole plan of "implement the
contract, not the hardware feature".

| Entry point | Status |
|---|---|
| `glAreTexturesResident` | [x] |
| `glPrioritizeTextures` | [x] |

### The honest semantics

Neither GL 3.2 core nor ES 3.0 has any notion of texture residency — the driver
manages it. GL 2.1 itself permits an implementation to report **every** texture
as resident, and priorities are explicitly advisory ("may be used ... but no
particular behavior is required"). So the correct implementation is not a
stub-with-an-error; it is a real, conforming implementation that tracks the
priority value and reports residency truthfully-as-permitted.

**Do not** raise `GL_INVALID_OPERATION` here. That would be a lie in the other
direction: the call is supported, its effect is simply advisory.

### Implementation — `SimpleFPEWrapper/getter.cpp` (next to the texture entry points)

```cpp
// GL 2.1 4.3.2 / 3.8.11: residency is driver-managed and neither backend
// floor exposes it. Reporting every live texture as resident is a conforming
// answer (the spec allows an implementation where all textures always are),
// and it is what an application checking before a draw needs to see.
GLboolean glAreTexturesResident(GLsizei n, const GLuint* textures, GLboolean* residences) {
    auto& gs = g_glstate;
    if (n < 0) { gs.set_error(GL_INVALID_VALUE); return GL_FALSE; }
    if (n == 0) return GL_TRUE;
    if (textures == nullptr || residences == nullptr) return GL_FALSE;
    sfpewEntryBarrier();
    GLboolean all = GL_TRUE;
    for (GLsizei i = 0; i < n; ++i) {
        // Name 0 and never-created names are errors, not "not resident".
        if (textures[i] == 0 || (g_glFuncs.glIsTexture != nullptr &&
                                 g_glFuncs.glIsTexture(textures[i]) == GL_FALSE)) {
            gs.set_error(GL_INVALID_VALUE);
            return GL_FALSE;
        }
        residences[i] = GL_TRUE;
    }
    return all;
}
```

Two details the spec is explicit about and that are easy to get wrong:

- If **all** queried textures are resident, the return is `GL_TRUE` and the
  contents of `residences` are *undefined* — but writing `GL_TRUE` to each is
  both harmless and friendlier. Do that.
- If any name is 0 or not a texture, raise `GL_INVALID_VALUE` and return
  `GL_FALSE` **without** writing `residences`.
- `glAreTexturesResident` is a **query**: it must execute immediately even
  inside `glNewList`. No `LIST_RECORD`.

```cpp
// Priorities are advisory in GL 2.1 (3.8.11) and unrepresentable on both
// backend floors; the value is kept so GL_TEXTURE_PRIORITY reads back what
// was set, which is the only observable the spec guarantees.
void glPrioritizeTextures(GLsizei n, const GLuint* textures, const GLclampf* priorities) {
    auto& gs = g_glstate;
    if (n < 0) { gs.set_error(GL_INVALID_VALUE); return; }
    LIST_RECORD(glPrioritizeTextures,
                {{1, (size_t)n * sizeof(GLuint)}, {2, (size_t)n * sizeof(GLclampf)}},
                n, textures, priorities)
    if (n == 0 || textures == nullptr || priorities == nullptr) return;
    sfpewEntryBarrier();
    for (GLsizei i = 0; i < n; ++i) {
        if (textures[i] == 0) continue;   // silently ignored, per spec
        gs.texture_priorities[textures[i]] = std::clamp(priorities[i], 0.0f, 1.0f);
    }
}
```

`glPrioritizeTextures` **does** compile into a display list.

### State — `fpe/types.h`, in `glstate_t`

```cpp
// glPrioritizeTextures / glTexParameter(GL_TEXTURE_PRIORITY). Advisory on
// both backend floors; stored so the queries are exact. Keyed by texture
// name; entries are dropped by glDeleteTextures.
unordered_map<GLuint, GLclampf> texture_priorities;
```

Erase from this map in the wrapper's `glDeleteTextures` (find it with
`grep -n "glDeleteTextures" SimpleFPEWrapper/*.cpp`), or the map grows without
bound in a long session and a recycled texture name inherits a stale priority.

### Getter work

- `glGetTexParameterfv/iv(target, GL_TEXTURE_PRIORITY, …)` must return the
  stored value for the texture currently bound to `target`, defaulting to
  **1.0** (the GL initial value). Both backends reject this pname, so the
  wrapper must answer it itself and **must not** forward it.
- `glTexParameterf/i(target, GL_TEXTURE_PRIORITY, v)` must also write this map
  and not forward — check `ordered_passthrough.cpp:328` and add the intercept.

### Registration — `lookup.cpp`

```cpp
GETPROC(glAreTexturesResident, name)
GETPROC(glPrioritizeTextures, name)
GETPROC_WRAPPER_ALIAS(glAreTexturesResidentEXT, glAreTexturesResident)
GETPROC_WRAPPER_ALIAS(glPrioritizeTexturesEXT, glPrioritizeTextures)
```

### Test — `tests/gtest_texture_residency.cc`

`ContextTest`. Cover: all-resident returns `GL_TRUE`; `n < 0` raises
`GL_INVALID_VALUE`; a deleted/never-created name raises `GL_INVALID_VALUE` and
returns `GL_FALSE`; `glPrioritizeTextures` then `glGetTexParameterfv(
GL_TEXTURE_2D, GL_TEXTURE_PRIORITY)` round-trips the clamped value; the default
reads back 1.0; out-of-range priorities clamp to [0,1]; a priority set inside
`glNewList`/`glEndList` takes effect only on `glCallList`, while
`glAreTexturesResident` inside a list executes immediately.

---

## Group H — Half-Implemented Fixes (3 entry points)

**Why third:** these are real, cheap bugs. `glClearDepth` and `glDrawBuffer` are
*missing entirely* from a library that claims desktop GL 2.1 — every legacy app
calls `glClearDepth`.

| Entry point | Status |
|---|---|
| `glClearDepth` | [x] |
| `glDrawBuffer` | [x] |
| `glGetQueryObjectiv` | [x] |

### H.1 `glClearDepth` — `ordered_passthrough.cpp`

Desktop GL takes a `GLdouble`; both backend floors expose only the `f` form
(`loader.h:168` has `glClearDepthf`, and there is no `glClearDepth`). This is
exactly the case §0.2 describes: the wrapper absorbs the spelling difference.

```cpp
// Desktop GL's double-precision spelling. Neither backend floor has it -
// GL 3.2 core and ES 3.0 both took glClearDepthf - so the value is narrowed
// here. The depth clear value is clamped to [0,1] (GL 2.1 4.2.3).
void glClearDepth(GLdouble depth) {
    if (!sfpewEnsureBackend()) return;
    sfpewEntryBarrier();
    LIST_RECORD(glClearDepth, {}, depth)
    if (g_glFuncs.glClearDepthf != nullptr)
        g_glFuncs.glClearDepthf((GLfloat)std::clamp(depth, 0.0, 1.0));
}
```

Also register `glClearDepthf` itself as a wrapper entry point if it is not
already resolvable — check with `grep -n glClearDepthf SimpleFPEWrapper/lookup.cpp`.
Both must record into display lists.

`GL_DEPTH_CLEAR_VALUE` must read back what was set. The backend can answer it,
but verify: if `glGetFloatv(GL_DEPTH_CLEAR_VALUE)` already works through the
backend, no getter change is needed. Confirm on both backends before assuming.

### H.2 `glDrawBuffer` — `ordered_passthrough.cpp`

Singular `glDrawBuffer` does not exist on either floor; plural `glDrawBuffers`
does (`loader.h:341`). Map one onto the other.

```cpp
// GL 2.1 4.2.1. Neither backend floor has the singular form; it is exactly
// glDrawBuffers with n=1, except for GL_FRONT/GL_FRONT_AND_BACK, which name
// the default framebuffer's front buffer - a double-buffered EGL surface has
// no separately addressable front buffer, so those select GL_BACK, which is
// where a swap will present from anyway.
void glDrawBuffer(GLenum buf) {
    if (!sfpewEnsureBackend()) return;
    sfpewEntryBarrier();
    LIST_RECORD(glDrawBuffer, {}, buf)
    if (g_glFuncs.glDrawBuffers == nullptr) return;
    GLenum mapped = buf;
    switch (buf) {
    case GL_FRONT: case GL_FRONT_LEFT: case GL_FRONT_RIGHT:
    case GL_FRONT_AND_BACK: mapped = GL_BACK; break;
    case GL_BACK_LEFT: case GL_BACK_RIGHT: mapped = GL_BACK; break;
    case GL_LEFT: case GL_RIGHT: mapped = GL_BACK; break;
    default: break;   // GL_NONE, GL_BACK, GL_COLOR_ATTACHMENTi pass through
    }
    g_glFuncs.glDrawBuffers(1, &mapped);
}
```

Read `/docsgl/gl2/glDrawBuffer.xhtml` for the full accepted-enum list and its
error conditions, and reject anything outside it with `GL_INVALID_ENUM` rather
than forwarding it.

**Framebuffer-dependent validity:** when a user FBO is bound, only
`GL_NONE` and `GL_COLOR_ATTACHMENTi` are legal, and `GL_BACK` is an error;
when the default framebuffer is bound, the reverse. Consult the current
binding (there is already a shadow — `grep -n "glBindFramebuffer" getter.cpp`)
and raise `GL_INVALID_OPERATION` for the mismatched case.

`GL_DRAW_BUFFER` / `GL_DRAW_BUFFER0` must report the mapped value.

### H.3 `glGetQueryObjectiv` — `ordered_passthrough.cpp`

`glGetQueryObjectuiv` exists on both floors (`loader.h:338`); the signed
desktop spelling does not (only `glGetQueryObjectivEXT`, `loader.h:637`, which
is an extension and therefore not guaranteed).

```cpp
// The desktop signed spelling. ES 3.0 has only the unsigned form; read
// through that and narrow. GL_QUERY_RESULT for an occlusion query can exceed
// INT_MAX in principle - saturate rather than wrap, so a caller comparing
// against a sample budget sees a large number instead of a negative one.
void glGetQueryObjectiv(GLuint id, GLenum pname, GLint* params) {
    if (!sfpewEnsureBackend() || params == nullptr) return;
    sfpewEntryBarrier();
    if (g_glFuncs.glGetQueryObjectivEXT != nullptr) {
        g_glFuncs.glGetQueryObjectivEXT(id, pname, params);
        return;
    }
    if (g_glFuncs.glGetQueryObjectuiv == nullptr) return;
    GLuint value = 0;
    g_glFuncs.glGetQueryObjectuiv(id, pname, &value);
    *params = value > (GLuint)INT_MAX ? INT_MAX : (GLint)value;
}
```

Prefer the EXT entry point when present (it is exact); fall back to the
unsigned read otherwise. Both are outside the intersection, so **both** need the
null guard shown.

Also add `glGetQueryObjecti64v` / `glGetQueryObjectui64v` if
`grep` shows them unresolvable — same shape, via `glGetQueryObjecti64vEXT`.

### Registration — `lookup.cpp`

```cpp
GETPROC(glClearDepth, name)
GETPROC(glDrawBuffer, name)
GETPROC(glGetQueryObjectiv, name)
GETPROC_WRAPPER_ALIAS(glGetQueryObjectivARB, glGetQueryObjectiv)
```

### Test — `tests/gtest_clear_depth_drawbuffer.cc`

`ContextTest` **and** `DesktopContextTest` (this group is precisely where the
two floors disagree, so both must be exercised):

- `glClearDepth(0.5)` → `glGetFloatv(GL_DEPTH_CLEAR_VALUE)` is 0.5; values
  outside [0,1] clamp; a `glClear(GL_DEPTH_BUFFER_BIT)` after
  `glClearDepth(0.0)` versus `(1.0)` produces different depth contents
  (verify with a depth-test-dependent draw, since depth cannot be read back
  directly on ES).
- `glDrawBuffer(GL_BACK)` then a draw lands in the surface (`PixelProbe`);
  `glDrawBuffer(GL_NONE)` then a draw lands nowhere; `GL_DRAW_BUFFER` reads
  back; an illegal enum raises `GL_INVALID_ENUM`; with an FBO bound,
  `GL_BACK` raises `GL_INVALID_OPERATION`.
- `glGetQueryObjectiv(GL_QUERY_RESULT)` on a finished occlusion query agrees
  with `glGetQueryObjectuiv`, and `GL_QUERY_RESULT_AVAILABLE` becomes
  `GL_TRUE` after `glFinish()`.

---

## Group F — Immediate-Mode Secondary Color (16 entry points)

`GL_EXT_secondary_color`, core in GL 1.4. `glSecondaryColorPointer` already
exists (`fpe/vertexpointer.cpp:435`), the attribute already has a slot
(**slot 6**, `fixed_function_draw_size_t::secondary_color_size`), the shader
generator already has `fpe_SecondaryColor` (`shader/userprogram.cpp:204`) — only
the per-vertex immediate entry points are missing.

Note `GL_EXT_secondary_color` is **already advertised** in `kDesktopExtensions`
(`getter.cpp:768`). An application that trusts that string and calls
`glSecondaryColor3f` currently gets a null pointer from
`eglGetProcAddress`. That is the bug.

| Entry point | Status | | Entry point | Status |
|---|---|---|---|---|
| `glSecondaryColor3b` | [x] | | `glSecondaryColor3bv` | [x] |
| `glSecondaryColor3s` | [x] | | `glSecondaryColor3sv` | [x] |
| `glSecondaryColor3i` | [x] | | `glSecondaryColor3iv` | [x] |
| `glSecondaryColor3f` | [x] | | `glSecondaryColor3fv` | [x] |
| `glSecondaryColor3d` | [x] | | `glSecondaryColor3dv` | [x] |
| `glSecondaryColor3ub` | [x] | | `glSecondaryColor3ubv` | [x] |
| `glSecondaryColor3us` | [x] | | `glSecondaryColor3usv` | [x] |
| `glSecondaryColor3ui` | [x] | | `glSecondaryColor3uiv` | [x] |

### The conversion rule — get this right or colors are wrong

`glSecondaryColor3f`/`3d` take values **already in [0,1]** and are used as-is
(not clamped until the fragment stage). Every **integer** variant is
*normalized*: the full integer range maps onto [0,1] for unsigned, and onto
[-1,1] for signed. GL 2.1 Table 2.6:

| Type | Conversion |
|---|---|
| `GLbyte` | `c / 127.0` |
| `GLshort` | `c / 32767.0` |
| `GLint` | `c / 2147483647.0` |
| `GLubyte` | `c / 255.0` |
| `GLushort` | `c / 65535.0` |
| `GLuint` | `c / 4294967295.0` |

This is the *same* rule `glColor3*` already uses. **Find that helper and reuse
it** — `grep -n "normalize\|32767\|2147483647" SimpleFPEWrapper/fpe/drawing1x.h
SimpleFPEWrapper/fpe/state.cpp`. Do not write a second copy of the table. If
`mglColor` normalizes inline, factor the conversion into a shared
`template <typename T> GLfloat sfpewNormalizeColorComponent(T)` in
`drawing1x.h` and use it from both.

### Implementation — `fpe/drawing1x.h`

Model on `mglFogCoord` (`drawing1x.h:108`). Alpha is **not** part of the
secondary color: the spec fixes it at 1.0 and the existing default already is
`{0,0,0,1}` (`types.h:239`).

```cpp
// glSecondaryColor3* (GL 1.4 / EXT_secondary_color) feeds attribute slot 6.
// It only reaches the shader when GL_COLOR_SUM is enabled; the value is
// tracked unconditionally so GL_CURRENT_SECONDARY_COLOR is exact.
template <typename Type, GLint N>
void mglSecondaryColor(std::array<Type, N> color) {
    auto& state = sfpewVertexDataState().fpe_state.fpe_draw;
    state.set_attribute_size(6, 3);
    auto& cur = state.current_data.secondary_color;
    for (GLint i = 0; i < N; ++i)
        glm::value_ptr(cur)[i] = sfpewNormalizeColorComponent(color[i]);
    glm::value_ptr(cur)[3] = 1.0f;   // spec: alpha of the secondary color is 1
}
```

`set_attribute_size(6, 3)` — always 3, never `N`; there is no
`glSecondaryColor4*`.

### Entry points — `fpe/state.cpp`, next to the `glFogCoord*` block (line 337)

Follow the shape at `state.cpp:341` exactly:

```cpp
void glSecondaryColor3f(GLfloat r, GLfloat g, GLfloat b) {
    LIST_RECORD(glSecondaryColor3f, {}, r, g, b)
    mglSecondaryColor<GLfloat, 3>({r, g, b});
}

void glSecondaryColor3fv(const GLfloat* v) {
    if (v == nullptr) return;
    LIST_RECORD(glSecondaryColor3fv, {{0, 3 * sizeof(GLfloat)}}, v)
    mglSecondaryColor<GLfloat, 3>({v[0], v[1], v[2]});
}
```

Repeat for all eight types. **Every one records into a display list** (vertex
data commands compile). The vector forms need the `{{0, 3 * sizeof(T)}}`
pointer capture; the scalar forms take `{}`.

### Do NOT call `sfpewEntryBarrier()`

Per §0.5. These are vertex-data commands; a barrier here would flush the batch
mid-primitive.

### Display-list classification

Check `sfpew_imm::classify` in `fpe/list.h:67-255`. It classifies commands as
`begin` / `end` / `vertex_data` / `none` by *function signature*. A
`void(GLfloat, GLfloat, GLfloat)` overload may already classify as
`vertex_data` — verify, and if `glSecondaryColor3f`'s signature collides with
something that must classify as `none`, add an explicit `classify` overload.
**Getting this wrong silently breaks display-list batch merging**, which is a
performance path with correctness consequences (a merged batch that should have
been split draws with the wrong attributes).

### Getter — `getter.cpp`

`GL_CURRENT_SECONDARY_COLOR` (4 values) must read from
`fpe_state.fpe_draw.current_data.secondary_color`. Check whether
`fixedFunctionState()` already answers it; the 253-pname audit may have covered
it with a hardcoded default. If so, replace the default with the live value.

Also confirm `GL_SECONDARY_COLOR_ARRAY_*` pnames (enabled/size/type/stride/
pointer/buffer-binding) are answered from the slot-6 array state.

### `GL_COLOR_SUM`

`getter.cpp:1256` currently reports `GL_COLOR_SUM` as permanently disabled.
Making the secondary color actually *render* requires the shader generator to
add it to the fragment output when `GL_COLOR_SUM` is enabled. Decide
explicitly:

- **Minimum (this group):** entry points + state + queries. `GL_COLOR_SUM`
  keeps reporting disabled, which stays truthful, because nothing sums yet.
- **Full (recommended, folds into Group E's shader work):** make
  `GL_COLOR_SUM` a real enable in `fixed_function_state_t`, part of the program
  key, and have `fpe_shadergen.cpp` emit
  `fragColor.rgb += fpe_SecondaryColor.rgb` when set. Then `GL_COLOR_SUM`
  reports honestly and the feature works.

If you take the minimum, leave a comment at `getter.cpp:1256` saying the
secondary color is tracked but not summed, so the next reader knows the
report is deliberate.

### Registration — `lookup.cpp`

All 16 with `GETPROC`, plus the `EXT` alias for each (LWJGL2 asks for
`glSecondaryColor3fEXT` by preference — see the existing `glFogCoordfEXT`
handling at `lookup.cpp:782`):

```cpp
GETPROC(glSecondaryColor3f, name)
GETPROC_WRAPPER_ALIAS(glSecondaryColor3fEXT, glSecondaryColor3f)
// ... × 16
```

### Test — `tests/gtest_secondary_color.cc`

`ContextTest`. One `TEST` per type asserting the normalization boundary values
(`glSecondaryColor3ub(255,0,0)` → `GL_CURRENT_SECONDARY_COLOR` reads
`{1,0,0,1}`; `glSecondaryColor3b(-128,…)` clamps to −1.0; `glSecondaryColor3s(
32767,…)` → 1.0). Then: alpha is always 1.0 regardless of entry point; a
`glSecondaryColor3f` inside `glBegin`/`glEnd` does not disturb the primary
color; the value survives `glPushAttrib(GL_CURRENT_BIT)`/`glPopAttrib`; and a
list recording `glSecondaryColor3f` replays it on `glCallList`.

---

## Group G — Legacy Generic Vertex Attribute Variants (29 entry points)

GL 2.0 `glVertexAttrib*`. Only the `float` forms are currently reachable, and
they are handed out as **backend** pointers (`GETPROC_BACKEND_ALIAS`,
`lookup.cpp:199`). Everything else — double, short, byte, normalized-integer —
is missing, because neither backend floor has those spellings: ES 3.0 has only
`glVertexAttrib{1,2,3,4}f{,v}` and `glVertexAttribI4{i,ui}{,v}`.

So the wrapper must **convert and forward to the float form**. That is not a
compromise: GL 2.1 §2.7 defines these variants as conversions onto the same
four-component float attribute.

### G.1 Double and short scalar/vector forms (16)

| Entry point | Status | | Entry point | Status |
|---|---|---|---|---|
| `glVertexAttrib1d` | [x] | | `glVertexAttrib1dv` | [x] |
| `glVertexAttrib2d` | [x] | | `glVertexAttrib2dv` | [x] |
| `glVertexAttrib3d` | [x] | | `glVertexAttrib3dv` | [x] |
| `glVertexAttrib4d` | [x] | | `glVertexAttrib4dv` | [x] |
| `glVertexAttrib1s` | [x] | | `glVertexAttrib1sv` | [x] |
| `glVertexAttrib2s` | [x] | | `glVertexAttrib2sv` | [x] |
| `glVertexAttrib3s` | [x] | | `glVertexAttrib3sv` | [x] |
| `glVertexAttrib4s` | [x] | | `glVertexAttrib4sv` | [x] |

**Critical: `s` and `d` are NOT normalized.** `glVertexAttrib1s(index, 5)`
delivers `5.0`, not `5/32767`. Only the explicitly-named `N` forms in G.2
normalize. Getting this backwards is the single most likely bug in this group.

Default expansion for the missing components: `(0, 0, 0, 1)`.

### G.2 Normalized and unnormalized packed forms (12)

| Entry point | Status | Conversion |
|---|---|---|
| `glVertexAttrib4Nbv` | [x] | `c/127`, clamp [−1,1] |
| `glVertexAttrib4Nsv` | [x] | `c/32767` |
| `glVertexAttrib4Niv` | [x] | `c/2147483647` |
| `glVertexAttrib4Nub` | [x] | `c/255` |
| `glVertexAttrib4Nubv` | [x] | `c/255` |
| `glVertexAttrib4Nusv` | [x] | `c/65535` |
| `glVertexAttrib4Nuiv` | [x] | `c/4294967295` |
| `glVertexAttrib4bv` | [x] | direct cast |
| `glVertexAttrib4iv` | [x] | direct cast |
| `glVertexAttrib4sv` | [x] | direct cast (also in G.1) |
| `glVertexAttrib4ubv` | [x] | direct cast |
| `glVertexAttrib4uiv` | [x] | direct cast |
| `glVertexAttrib4usv` | [x] | direct cast |

The `N` prefix means normalized; its absence means a direct numeric cast. Reuse
`sfpewNormalizeColorComponent` from Group F for the `N` forms — it is the same
Table 2.6 rule.

### G.3 `glGetVertexAttribdv` (1)

| Entry point | Status |
|---|---|
| `glGetVertexAttribdv` | [x] |

Neither floor has it. Read through `glGetVertexAttribfv` (which is in the
intersection, `loader.h:241`) and widen each float to double.

### Implementation — new file `SimpleFPEWrapper/shader/vertexattrib.cpp`

Create the file (header banner copied from any existing source — the
SPDX/copyright block is mandatory), add it to `CMakeLists.txt` at line ~139
next to the other `shader/` sources.

Use a macro; 29 hand-written functions is 29 chances to typo an index.

```cpp
// GL 2.0 glVertexAttrib* variants neither backend floor spells. ES 3.0 has
// only the float and integer-attribute forms, so every legacy variant is
// converted here and forwarded to glVertexAttrib4fv - which is what GL 2.1
// 2.7 defines them to mean. Missing components default to (0,0,0,1).
//
// N-prefixed forms normalize by Table 2.6; the plain integer forms are a
// direct numeric cast. That distinction is the whole point of the two
// families: glVertexAttrib4sv(i, {5,...}) delivers 5.0, while
// glVertexAttrib4Nsv(i, {5,...}) delivers 5/32767.

namespace {
void setAttrib4f(GLuint index, GLfloat x, GLfloat y, GLfloat z, GLfloat w) {
    if (!sfpewEnsureBackend() || g_glFuncs.glVertexAttrib4f == nullptr) return;
    // No sfpewEntryBarrier(): this is a vertex-data command (see plans.md 0.5).
    const GLfloat v[4] = {x, y, z, w};
    g_glFuncs.glVertexAttrib4fv(index, v);
}
} // namespace

#define SFPEW_ATTRIB_SCALAR(suffix, type, n, conv)                             \
    void glVertexAttrib##n##suffix(GLuint index, SFPEW_ARGS_##n(type)) {       \
        LIST_RECORD(glVertexAttrib##n##suffix, {}, index, SFPEW_NAMES_##n)     \
        setAttrib4f(index, SFPEW_CONV_##n(conv));                              \
    }
```

Design the macro set to taste, but keep three properties: the index argument is
validated against `GL_MAX_VERTEX_ATTRIBS` with `GL_INVALID_VALUE` on overflow;
every variant records into a display list; and the conversion function is a
parameter so the `N`/non-`N` split is visible at each use site rather than
buried.

**Index validation** — cache the limit, do not query per call:

```cpp
GLint maxVertexAttribs() {
    static GLint cached = 0;
    if (cached == 0 && g_glFuncs.glGetIntegerv != nullptr)
        g_glFuncs.glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &cached);
    return cached != 0 ? cached : 16;   // GL 2.1 minimum
}
```

### Interaction with the fixed-function attribute slots

The wrapper reserves attribute slots 0–(6+MAX_TEX) for fixed-function data
(`VERTEX_POINTER_COUNT`, `types.h:135`). A user program's `glVertexAttrib4f(3,
…)` and the FPE's slot 3 are the same backend attribute. This is **already** the
situation for the float forms, which pass straight through, so these variants
introduce no new hazard — but do not "fix" it here. If a conflict shows up in
testing, note it and raise it separately; it is a design question about
`shader/userprogram.cpp`, not part of this plan.

### Registration — `lookup.cpp`

All 29 with `GETPROC` (**not** `GETPROC_BACKEND_ALIAS` — these are wrapper
implementations now), plus `ARB` aliases:

```cpp
GETPROC(glVertexAttrib1d, name)
GETPROC_WRAPPER_ALIAS(glVertexAttrib1dARB, glVertexAttrib1d)
// ... × 29
GETPROC(glGetVertexAttribdv, name)
GETPROC_WRAPPER_ALIAS(glGetVertexAttribdvARB, glGetVertexAttribdv)
```

Leave the existing float `GETPROC_BACKEND_ALIAS` lines alone.

### Test — `tests/gtest_vertex_attrib_variants.cc`

`ContextTest`. The decisive assertions:

- `glVertexAttrib4Nub(i, 255,0,0,255)` → `glGetVertexAttribfv(i,
  GL_CURRENT_VERTEX_ATTRIB)` reads `{1,0,0,1}`.
- `glVertexAttrib4ubv(i, {255,0,0,255})` reads `{255,0,0,255}` — **not**
  normalized. This one test catches the most likely bug in the group.
- `glVertexAttrib1s(i, 5)` reads `{5,0,0,1}`; `glVertexAttrib2d(i, 1.5, 2.5)`
  reads `{1.5,2.5,0,1}` — default expansion.
- `glGetVertexAttribdv` agrees with `glGetVertexAttribfv` to float precision.
- `index >= GL_MAX_VERTEX_ATTRIBS` raises `GL_INVALID_VALUE`.
- One end-to-end render: bind a user program reading attribute 1, set it with
  `glVertexAttrib4Nub`, draw, and `SFPEW_EXPECT_LIT` the expected color. State
  round-trips prove conversion; only a draw proves it reaches the shader.

---

## Group E — Point Parameters (4 entry points + real rendering)

`GL_ARB_point_parameters`, core in GL 1.4. **`GL_ARB_point_parameters` and
`GL_ARB_point_sprite` are already advertised** (`getter.cpp:757-758`) while
`glPointParameter*` does not exist and `getter.cpp:1329` returns hardcoded
initial values with the comment "glPointParameter is not implemented". Same
class of bug as Group F: the extension string promises something absent.

| Entry point | Status |
|---|---|
| `glPointParameterf` | [x] |
| `glPointParameterfv` | [x] |
| `glPointParameteri` | [x] |
| `glPointParameteriv` | [x] |

### State — `fpe/types.h`

Two homes, and the split matters. Values that only scale a uniform go in
`fixed_function_uniform_t` (next to `point_size`, line 575). Anything that
changes the *shape of the generated shader* goes in `fixed_function_state_t`
and becomes part of the program key.

In `fixed_function_uniform_t`:

```cpp
// glPointParameter* (GL 1.4 / ARB_point_parameters). Distance attenuation
// scales gl_PointSize in the generated vertex shader; the fade threshold
// scales alpha for points smaller than it.
GLfloat point_size_min = 0.0f;
GLfloat point_size_max = 1.0f;   // GL initial value; raised to the
                                 // implementation max by glPointParameter
GLfloat point_fade_threshold_size = 1.0f;
glm::vec3 point_distance_attenuation = {1.0f, 0.0f, 0.0f};  // a, b, c
```

In `fixed_function_state_t`:

```cpp
// GL_POINT_SPRITE_COORD_ORIGIN: GL_LOWER_LEFT or GL_UPPER_LEFT (GL 2.0).
// Part of the program key - it flips gl_PointCoord.y in the fragment shader.
GLenum point_sprite_coord_origin = GL_UPPER_LEFT;
```

`GL_UPPER_LEFT` is the GL default and is what GLES's `gl_PointCoord` already
gives, so the default costs nothing.

### Implementation — `fpe/state.cpp`

```cpp
// GL 1.4 3.3 / ARB_point_parameters. Neither backend floor has a fixed-function
// point pipeline, so attenuation is applied in the generated vertex shader
// (see fpe_shadergen.cpp) rather than forwarded.
void glPointParameterfv(GLenum pname, const GLfloat* params) {
    auto& gs = g_glstate;
    if (params == nullptr) return;
    const int count = pname == GL_POINT_DISTANCE_ATTENUATION ? 3 : 1;
    LIST_RECORD(glPointParameterfv, {{1, (size_t)count * sizeof(GLfloat)}}, pname, params)
    sfpewEntryBarrier();
    auto& uni = gs.fpe_uniform;
    switch (pname) {
    case GL_POINT_SIZE_MIN:
        if (params[0] < 0.0f) { gs.set_error(GL_INVALID_VALUE); return; }
        uni.point_size_min = params[0];
        break;
    case GL_POINT_SIZE_MAX:
        if (params[0] < 0.0f) { gs.set_error(GL_INVALID_VALUE); return; }
        uni.point_size_max = params[0];
        break;
    case GL_POINT_FADE_THRESHOLD_SIZE:
        if (params[0] < 0.0f) { gs.set_error(GL_INVALID_VALUE); return; }
        uni.point_fade_threshold_size = params[0];
        break;
    case GL_POINT_DISTANCE_ATTENUATION:
        uni.point_distance_attenuation = {params[0], params[1], params[2]};
        break;
    case GL_POINT_SPRITE_COORD_ORIGIN:
        if (params[0] != (GLfloat)GL_LOWER_LEFT && params[0] != (GLfloat)GL_UPPER_LEFT) {
            gs.set_error(GL_INVALID_VALUE);
            return;
        }
        gs.fpe_state.point_sprite_coord_origin = (GLenum)params[0];
        break;
    default:
        gs.set_error(GL_INVALID_ENUM);
        return;
    }
}
```

The scalar `glPointParameterf` forwards to the vector form for the single-value
pnames and raises `GL_INVALID_ENUM` for `GL_POINT_DISTANCE_ATTENUATION` (which
has no scalar form). The `i`/`iv` variants convert to float — but note
`GL_POINT_SPRITE_COORD_ORIGIN` is naturally an enum and must survive the
round-trip through float exactly, which it does for these two values.

Use `SELF_CALL` when delegating so the list records under the name the
application called.

### Rendering — `fpe/fpe_shadergen.cpp`

Point size is already emitted as `gl_PointSize` from the generated vertex
shader (`types.h:575` comment). Extend it:

```glsl
// derived point size, GL 2.1 3.3 equation 3.1
float fpe_dist = length(fpe_eye_position.xyz);
float fpe_att = fpe_point_attenuation.x
              + fpe_point_attenuation.y * fpe_dist
              + fpe_point_attenuation.z * fpe_dist * fpe_dist;
float fpe_size = fpe_point_size * inversesqrt(max(fpe_att, 1e-6));
gl_PointSize = clamp(fpe_size, fpe_point_size_min, fpe_point_size_max);
```

Guard the whole block behind "attenuation is not the default {1,0,0}" so the
overwhelmingly common case generates the same shader it does today. That
condition must be part of the **program key** (a bool: attenuation active or
not), otherwise a cached program built without attenuation is reused after it
is enabled.

Add the three uniforms to `program_uniform_locations_t` /
`program_uniform_values_t` (`types.h:624`, `:660`) and upload them beside
`point_size`.

**Fade threshold** multiplies alpha when the derived size is below the
threshold (GL 2.1 3.3): `alpha *= min(size/threshold, 1.0)^2`. Implement it or
leave it out — but if you leave it out, say so in a comment at the
`GL_POINT_FADE_THRESHOLD_SIZE` case, because the state will read back correctly
while doing nothing, which is exactly the failure mode this whole plan exists
to eliminate.

### Getter — `getter.cpp:1329`

Replace the four hardcoded returns with reads of the new state:

```cpp
case GL_POINT_SIZE_MIN: return scalar(uni.point_size_min);
case GL_POINT_SIZE_MAX: return scalar(uni.point_size_max);
case GL_POINT_FADE_THRESHOLD_SIZE: return scalar(uni.point_fade_threshold_size);
case GL_POINT_DISTANCE_ATTENUATION:
    return vector(glm::value_ptr(uni.point_distance_attenuation), 3);
case GL_POINT_SPRITE_COORD_ORIGIN: return scalar(gs.fpe_state.point_sprite_coord_origin);
```

Delete the "glPointParameter is not implemented" comment.

`GL_POINT_SIZE_MAX`'s *initial* value is the implementation maximum, not 1.0 —
GL 2.1 Table 6.10 says "1.0" but the spec text says the initial max is the
larger of 1 and the implementation's max point size. Seed it from
`GL_POINT_SIZE_RANGE`'s upper bound at first use, and note which reading you
chose in a comment.

### `GL_POINT_SPRITE`

`getter.cpp:1256` reports `GL_POINT_SPRITE` as permanently disabled. With
`gl_PointCoord` available on both floors, point sprites are implementable:
enable it as real state, put it in the program key, and have the fragment
shader sample with `gl_PointCoord` (flipped per
`point_sprite_coord_origin`) instead of the interpolated texcoord. Also needs
`glTexEnvi(GL_POINT_SPRITE, GL_COORD_REPLACE, GL_TRUE)` per unit
(`state.cpp:86` is the `glTexEnv` shared body).

This is optional for this group. If skipped, keep the honest "disabled" report.
If done, tick a separate line for it and update `getter.cpp:1256`.

### Registration — `lookup.cpp`

```cpp
GETPROC(glPointParameterf, name)
GETPROC(glPointParameterfv, name)
GETPROC(glPointParameteri, name)
GETPROC(glPointParameteriv, name)
GETPROC_WRAPPER_ALIAS(glPointParameterfARB, glPointParameterf)
GETPROC_WRAPPER_ALIAS(glPointParameterfvARB, glPointParameterfv)
GETPROC_WRAPPER_ALIAS(glPointParameterfEXT, glPointParameterf)
GETPROC_WRAPPER_ALIAS(glPointParameterfvEXT, glPointParameterfv)
GETPROC_WRAPPER_ALIAS(glPointParameteriNV, glPointParameteri)
GETPROC_WRAPPER_ALIAS(glPointParameterivNV, glPointParameteriv)
```

### Test — `tests/gtest_point_parameters.cc`

`ContextTest`. State: every pname round-trips through all four setters and all
four getter types; negative sizes raise `GL_INVALID_VALUE`; a bad pname raises
`GL_INVALID_ENUM`; `GL_POINT_DISTANCE_ATTENUATION` through
`glPointParameterf` raises `GL_INVALID_ENUM`; an invalid coord origin raises
`GL_INVALID_VALUE`; values survive `glPushAttrib(GL_POINT_BIT)`/`glPopAttrib`.

Rendering: draw a `GL_POINTS` primitive at two different eye-space depths with
attenuation `{0,0,1}` and use `PixelProbe::FindLit` to confirm the near point
covers more pixels than the far one. With the default `{1,0,0}`, both must
cover the same area — that is the regression guard proving the common path is
unchanged.

---

## Group B — 1D / Compressed / Copy Texture Completion (5 entry points)

| Entry point | Status |
|---|---|
| `glCopyTexImage1D` | [x] |
| `glCopyTexSubImage1D` | [x] |
| `glCompressedTexImage1D` | [x] |
| `glCompressedTexSubImage1D` | [x] |
| `glGetCompressedTexImage` | [x] |

`glCopyTexImage1D` appears once in the tree (a mention, not an implementation —
`grep` it). All five follow the N×1 convention of §0.6.

### B.1 `glCopyTexImage1D` / `glCopyTexSubImage1D` — `getter.cpp`, next to `glTexImage1D` (line 1001)

Both 2D forms are in the intersection, so this is a straight delegation.

```cpp
// 1D textures are N x 1 2D textures here (see glTexImage1D above), so a 1D
// framebuffer copy is a 2D copy one row tall.
void glCopyTexImage1D(GLenum target, GLint level, GLenum internalformat, GLint x, GLint y,
                      GLsizei width, GLint border) {
    auto& gs = g_glstate;
    if (target != GL_TEXTURE_1D) { gs.set_error(GL_INVALID_ENUM); return; }
    if (border != 0) { gs.set_error(GL_INVALID_VALUE); return; }
    LIST_RECORD(glCopyTexImage1D, {}, target, level, internalformat, x, y, width, border)
    SELF_CALL(glCopyTexImage2D, GL_TEXTURE_2D, level, internalformat, x, y, width, 1, border)
}

void glCopyTexSubImage1D(GLenum target, GLint level, GLint xoffset, GLint x, GLint y,
                         GLsizei width) {
    auto& gs = g_glstate;
    if (target != GL_TEXTURE_1D) { gs.set_error(GL_INVALID_ENUM); return; }
    LIST_RECORD(glCopyTexSubImage1D, {}, target, level, xoffset, x, y, width)
    SELF_CALL(glCopyTexSubImage2D, GL_TEXTURE_2D, level, xoffset, 0, x, y, width, 1)
}
```

Check whether the wrapper already wraps `glCopyTexImage2D`/`glCopyTexSubImage2D`
(`grep -n "glCopyTexImage2D" SimpleFPEWrapper/`). If they are raw passthroughs,
call `g_glFuncs.glCopyTexImage2D` directly with a null guard instead of
`SELF_CALL`.

### B.2 `glCompressedTexImage1D` / `glCompressedTexSubImage1D`

Same delegation, one wrinkle: a compressed **block** format cannot be 1 pixel
tall. Every format in ES 3.0 core (ETC2/EAC) and every DXT/S3TC format uses at
least a 4×4 block.

Resolve it this way and say so in the comment:

```cpp
// Compressed 1D textures carry the N x 1 convention, which no block-compressed
// format can express - every ETC2/EAC and S3TC block is at least 4x4. GL 2.1
// only requires 1D support for formats an implementation actually advertises,
// and neither backend floor advertises a 1x-tall-capable one, so a genuinely
// block-compressed 1D upload is refused with GL_INVALID_OPERATION rather than
// silently uploading garbage. Non-block formats (if a backend exposes any)
// delegate to the 2D path.
void glCompressedTexImage1D(GLenum target, GLint level, GLenum internalformat, GLsizei width,
                            GLint border, GLsizei imageSize, const void* data) {
    auto& gs = g_glstate;
    if (target != GL_TEXTURE_1D && target != GL_PROXY_TEXTURE_1D) {
        gs.set_error(GL_INVALID_ENUM);
        return;
    }
    if (border != 0) { gs.set_error(GL_INVALID_VALUE); return; }
    if (imageSize < 0) { gs.set_error(GL_INVALID_VALUE); return; }
    LIST_RECORD(glCompressedTexImage1D, {{6, (size_t)(imageSize > 0 ? imageSize : 0)}},
                target, level, internalformat, width, border, imageSize, data)
    if (target == GL_PROXY_TEXTURE_1D) return;
    if (sfpewIsBlockCompressedFormat(internalformat)) {
        gs.set_error(GL_INVALID_OPERATION);
        return;
    }
    SELF_CALL(glCompressedTexImage2D, GL_TEXTURE_2D, level, internalformat, width, 1, border,
              imageSize, data)
}
```

Write `sfpewIsBlockCompressedFormat(GLenum)` as a small static table in
`getter.cpp`: all `GL_COMPRESSED_{RGB,RGBA,SRGB,SRGB8,R11,RG11,SIGNED_*}_*`
ETC2/EAC names, the four `GL_COMPRESSED_RGB{,A}_S3TC_DXT*` names, and the
ASTC range `0x93B0..0x93BD` / `0x93D0..0x93DD`. Being conservative is correct
here: an unknown format should be treated as block-compressed and refused,
because uploading a misinterpreted compressed payload can hard-crash a driver.

### B.3 `glGetCompressedTexImage` — the hard one

Desktop-only, and there is **no** way to read texture data back on GLES 3.0.
Options, in order of preference:

1. **If the backend is desktop GL** and `g_glFuncs.glGetCompressedTexImage` is
   non-null, forward directly. This is exact.
2. **Otherwise**, the wrapper must either keep a CPU-side shadow of every
   compressed upload, or fail. Shadowing every compressed texture doubles the
   memory cost of the largest allocations in a game — unacceptable as a default.

So: forward when possible, and raise `GL_INVALID_OPERATION` when not, with the
reason in the comment. Refusing loudly is the contract (§0.1 part 3); it is
strictly better than returning an uninitialized buffer, which is what a caller
gets today from the null pointer.

```cpp
// Desktop-only. GLES 3.0 has no texture readback of any kind, and shadowing
// every compressed upload on the CPU would double the footprint of the largest
// allocations a game makes. On a desktop backend this is exact; on GLES it
// fails loudly rather than handing back an uninitialized buffer.
void glGetCompressedTexImage(GLenum target, GLint level, void* pixels) {
    auto& gs = g_glstate;
    if (pixels == nullptr) return;
    sfpewEntryBarrier();
    if (!sfpewEnsureBackend()) return;
    if (g_glFuncs.glGetCompressedTexImage != nullptr) {
        GLenum t = target == GL_TEXTURE_1D ? GL_TEXTURE_2D : target;
        g_glFuncs.glGetCompressedTexImage(t, level, pixels);
        return;
    }
    gs.set_error(GL_INVALID_OPERATION);
}
```

Adding `glGetCompressedTexImage` to `backend/loader.h` means a new
`GL_FUNC_TYPEDEF`, which changes `docs/backend-profile.json` and fails
`backend_profile_current` until you regenerate it. It also belongs in
`check_backend_profile.py`'s `NON_UNIVERSAL` set with the note
"GL only: ES has no texture readback". Do both in the same commit.

### Getters that go with this group

`GL_TEXTURE_COMPRESSED`, `GL_TEXTURE_COMPRESSED_IMAGE_SIZE`,
`GL_TEXTURE_INTERNAL_FORMAT` via `glGetTexLevelParameteriv` — check whether the
wrapper answers these (`grep -n "glGetTexLevelParameter" SimpleFPEWrapper/`). On
GLES they must be answered by the wrapper from its own per-level bookkeeping or
refused; they are not forwardable.

`GL_NUM_COMPRESSED_TEXTURE_FORMATS` / `GL_COMPRESSED_TEXTURE_FORMATS` come from
the backend and should already work — verify on both backends.

### Registration — `lookup.cpp`

```cpp
GETPROC(glCopyTexImage1D, name)
GETPROC(glCopyTexSubImage1D, name)
GETPROC(glCompressedTexImage1D, name)
GETPROC(glCompressedTexSubImage1D, name)
GETPROC(glGetCompressedTexImage, name)
GETPROC_WRAPPER_ALIAS(glCompressedTexImage1DARB, glCompressedTexImage1D)
GETPROC_WRAPPER_ALIAS(glCompressedTexSubImage1DARB, glCompressedTexSubImage1D)
GETPROC_WRAPPER_ALIAS(glGetCompressedTexImageARB, glGetCompressedTexImage)
GETPROC_WRAPPER_ALIAS(glCopyTexImage1DEXT, glCopyTexImage1D)
GETPROC_WRAPPER_ALIAS(glCopyTexSubImage1DEXT, glCopyTexSubImage1D)
```

### Test — `tests/gtest_texture_1d_copy.cc`

`ContextTest` for the copy paths: render a known 2-color pattern, then
`glCopyTexImage1D` a row of it, bind that texture to a textured quad, draw, and
`SFPEW_EXPECT_LIT` the sampled colors. `glCopyTexSubImage1D` updating a
sub-range of an existing 1D texture. Wrong target → `GL_INVALID_ENUM`;
`border != 0` → `GL_INVALID_VALUE`.

For compressed: a block-compressed `glCompressedTexImage1D` raises
`GL_INVALID_OPERATION`; a negative `imageSize` raises `GL_INVALID_VALUE`.

For `glGetCompressedTexImage`: `DesktopContextTest` asserts a compressed upload
round-trips byte-for-byte; `ContextTest` (GLES3) asserts it raises
`GL_INVALID_OPERATION` and leaves the caller's buffer untouched. Two fixtures,
two opposite expectations — that is the point.

---

## Group I — Completing the Half-Implemented Paths

These are not missing symbols; they are entry points that resolve and then do
less than they claim. Each item is a documented deviation today — the goal is to
either close it or make the deviation explicit and bounded.

| Item | Status |
|---|---|
| I.1 `glDrawPixels` formats/types + PBO | [x] |
| I.2 `glCopyPixels` `GL_DEPTH` / `GL_STENCIL` | [x] |
| I.3 `glPolygonMode` independent front/back | [x] |
| I.4 Feedback color/texture payloads | [x] |
| I.5 Selection with VBO / non-float vertices | [x] |
| I.6 Pixel maps (`glPixelMap*`) | [x] |

### I.1 `glDrawPixels` — `fpe/pixelops.cpp:458`

Currently `GL_UNSIGNED_BYTE` and a limited format set only; a bound pixel
unpack buffer is skipped outright (`pixelops.cpp:465`).

Work:

1. **Type coverage.** Add `GL_BYTE`, `GL_UNSIGNED_SHORT`, `GL_SHORT`,
   `GL_UNSIGNED_INT`, `GL_INT`, `GL_FLOAT`, `GL_HALF_FLOAT`, and the packed
   types (`GL_UNSIGNED_SHORT_5_6_5`, `_4_4_4_4`, `_5_5_5_1`,
   `GL_UNSIGNED_INT_8_8_8_8{,_REV}`, `_10_10_10_2`, `_2_10_10_10_REV`).
   Convert on the CPU to tightly-packed `GL_RGBA`/`GL_UNSIGNED_BYTE` (or
   `GL_FLOAT` for the float types, to keep range) and hand that to the existing
   `drawQuad()`. One conversion function keyed on (format, type) → RGBA float,
   then one pack step, rather than a combinatorial explosion of paths.
2. **Format coverage.** `GL_RED`, `GL_GREEN`, `GL_BLUE`, `GL_ALPHA`,
   `GL_RGB`, `GL_RGBA`, `GL_BGR`, `GL_BGRA`, `GL_LUMINANCE`,
   `GL_LUMINANCE_ALPHA`, `GL_STENCIL_INDEX`, `GL_DEPTH_COMPONENT`.
   `GL_GREEN`/`GL_BLUE` expand as (0,c,0,1)/(0,0,c,1) per Table 3.6 — easy to
   get wrong; read `/docsgl/gl2/glDrawPixels.xhtml`.
   `GL_DEPTH_COMPONENT` writes depth, not color: it needs a shader writing
   `gl_FragDepth` with color writes masked off. `GL_STENCIL_INDEX` needs
   stencil writes; if that is out of reach, refuse it with
   `GL_INVALID_OPERATION` and note it.
3. **PBO support.** Instead of skipping, map the bound buffer. The BGRA upload
   path already solves exactly this problem — read `sfpewPrepareBgraUpload`
   (`init.h:51`) and reuse its approach: it neutralizes unpack state, reads
   through the PBO, and restores everything afterwards.
4. **Pixel transfer.** `glPixelTransfer` scale/bias (`types.h:559`,
   `pixel_scale`/`pixel_bias`) and `glPixelZoom` (`pixel_zoom_x/y`) are stored
   but check whether `glDrawPixels` actually applies them. Zoom changes the
   destination rectangle size; scale/bias multiply-add each component before
   the quad is drawn. Both are cheap once the CPU conversion path from (1)
   exists.

### I.2 `glCopyPixels` — `fpe/pixelops.cpp:544`

`GL_COLOR` only today (`pixelops.cpp:551`).

- `GL_DEPTH`: blit depth via `glBlitFramebuffer` with
  `GL_DEPTH_BUFFER_BIT` — in the intersection, so this is straightforward.
  Source and destination are the same framebuffer at different offsets, which
  `glBlitFramebuffer` permits only when the regions do not overlap; for the
  overlapping case, blit through a scratch FBO. The accumulation buffer code in
  this file (`ensureAccum`, `pixelops.cpp:269`) is the model for creating and
  sizing a scratch attachment.
- `GL_STENCIL`: same with `GL_STENCIL_BUFFER_BIT`, contingent on the surface
  having a stencil attachment; raise `GL_INVALID_OPERATION` if not.
- Keep `GL_COLOR` on the existing quad path — it honors blend/depth/scissor,
  which `glBlitFramebuffer` does not, and the spec wants the pixel path.

### I.3 `glPolygonMode` — `fpe/state.cpp:1521`

Independent front/back modes fall back to fill. The state is already tracked
(`uni.polygon_mode_front` / `_back`, `getter.cpp:1324`) and
`tests/gtest_polygon_mode.cc` already covers 27 same-mode cases.

The fix is a two-pass draw: split the primitive stream by facing, draw the
front-facing subset with the front mode and the back-facing subset with the
back mode. Two viable routes:

- **`glCullFace` two-pass** (recommended): draw once with
  `glEnable(GL_CULL_FACE)`, `glCullFace(GL_BACK)` in the front mode, then again
  with `glCullFace(GL_FRONT)` in the back mode. Cheap, no CPU work, exactly
  correct for `GL_FILL`/`GL_LINE`/`GL_POINT` combinations. Must save and restore
  the app's cull state, and must respect `glFrontFace`.
- CPU facing classification during the wireframe expansion — more code, more
  cost, no benefit over the above.

Take the two-pass route. Guard it behind `front != back` so the common case is
untouched, and extend `gtest_polygon_mode.cc` with the mixed combinations
(front `GL_LINE` + back `GL_FILL` and the reverse are the cases that matter).

### I.4 Feedback color/texture payloads — `fpe/selection.cpp`

`GL_3D_COLOR`, `GL_3D_COLOR_TEXTURE`, `GL_4D_COLOR_TEXTURE` currently degrade
to the coordinate stream. The CPU transform already produces window
coordinates; the missing part is carrying the per-vertex color and texcoord
through to the feedback buffer.

The immediate-mode path already has both in `fixed_function_draw_data_t`
(`color`, `texcoord[]`). Emit them in the spec's order and count: `GL_3D_COLOR`
is x,y,z + 4 color values; `GL_3D_COLOR_TEXTURE` adds 4 texcoords;
`GL_4D_COLOR_TEXTURE` is x,y,z,w + 4 + 4. Respect `GL_INDEX_MODE` (color-index
mode emits 1 value instead of 4) — or, since this wrapper has no color-index
mode, always emit RGBA and note it.

### I.5 Selection with VBO / non-float vertices — `drawing.cpp:1166`

Draws are skipped when selection is active and the vertex data is in a VBO or
is not float. Fix by reading the data back: `glGetBufferSubData` is already a
wrapper entry point (`lookup.cpp:210`), and `type_size()` (`types.h:83`) plus
the existing conversion helpers in `fpe/vertexpointer_utils.cpp` handle
non-float types. Convert to float, run the existing CPU transform, done.

This is the highest-value item in Group I for real applications: legacy
Minecraft mods use `glRenderMode(GL_SELECT)` for entity picking, and a skipped
draw means an unclickable object.

### I.6 Pixel maps — `fpe/pixelops.cpp:323`

`glPixelMap{f,ui,us}v` / `glGetPixelMap*` currently report the size-1 identity
table because no entry point stores anything. There is no `glPixelMap*` setter
at all.

Add the four setters (`glPixelMapfv`, `glPixelMapuiv`, `glPixelMapusv`) with
storage in `glstate_t`:

```cpp
// glPixelMap* tables (GL 2.1 3.6.3). Ten maps, each a power-of-two-sized
// lookup applied during pixel transfer; the initial state is a size-1
// identity, which is what "no mapping" reads back as.
std::vector<GLfloat> pixel_maps[10];
```

Index them with the existing `validPixelMap()` helper (`pixelops.cpp:329`),
extended to return the index. Sizes must be powers of two (`GL_INVALID_VALUE`
otherwise) and at most `GL_MAX_PIXEL_MAP_TABLE` (32 is the GL minimum; report
whatever you choose consistently).

Then apply them in the CPU pixel path from I.1 when
`GL_MAP_COLOR`/`GL_MAP_STENCIL` is enabled (`pixel_map_color`,
`pixel_map_stencil` already exist at `types.h:562`). Update
`glGetPixelMapfv`/`uiv`/`usv` and the `GL_PIXEL_MAP_*_SIZE` getters
(`getter.cpp:1522`) to report the real tables, and delete the
"not implemented" comments.

### Test — extend existing suites

Do **not** create one giant suite. Extend the files that already own each area:
`tests/gtest_polygon_mode.cc` (I.3), and add
`tests/gtest_drawpixels_formats.cc` (I.1), `tests/gtest_copypixels_depth.cc`
(I.2), `tests/gtest_feedback_payloads.cc` (I.4),
`tests/gtest_selection_vbo.cc` (I.5), `tests/gtest_pixel_maps.cc` (I.6).

---

## Group A — The Imaging Subset (32 entry points)

`GL_ARB_imaging`. This is the largest group and the only one that adds a whole
new stage to an existing pipeline. Do it last.

### A.0 Understand the pipeline before writing anything

Every one of these 32 functions configures **one stage of the pixel transfer
path**. That path runs on pixel data flowing through `glDrawPixels`,
`glReadPixels`, `glTexImage*`, `glTexSubImage*`, `glCopyTex*`, and
`glCopyPixels`. GL 2.1 §3.6.3, Figure 3.7 — the order is fixed:

```
unpack  →  [1] GL_COLOR_TABLE
        →  [2] convolution (GL_CONVOLUTION_1D | _2D | GL_SEPARABLE_2D)
        →  [3] GL_POST_CONVOLUTION_COLOR_TABLE
        →  [4] color matrix (glMatrixMode(GL_COLOR))
        →  [5] GL_POST_COLOR_MATRIX_COLOR_TABLE
        →  [6] GL_HISTOGRAM   (tap, does not modify)
        →  [7] GL_MINMAX      (tap, does not modify)
        →  destination (texture / framebuffer / client memory)
```

Each stage is independently `glEnable`-able and **every one is off by default**.
That is the single most important fact for this group: with all seven disabled —
which is every real application — the pipeline must be bit-identical to today
and cost nothing. Build one `bool anyImagingEnabled()` check and short-circuit on
it.

Stages 1, 3, 5 are the *same operation* on three different tables. Stage 4 is
already implemented (`GL_COLOR` matrix mode exists —
`MAX_COLOR_STACK_DEPTH`, `transformation.cpp:51`); confirm it and reuse it.

### A.1 New files

Create `SimpleFPEWrapper/fpe/imaging.h` and `SimpleFPEWrapper/fpe/imaging.cpp`
(SPDX banner mandatory), add the `.cpp` to `CMakeLists.txt` after
`fpe/pixelops.cpp` (line 133).

`imaging.h` exposes exactly one thing to the rest of the wrapper, plus the
entry-point declarations:

```cpp
// Runs the enabled pixel-transfer stages (GL 2.1 3.6.3, Figure 3.7) over
// `count` RGBA float pixels in place. Returns false when nothing is enabled,
// which is the case for every application that does not use ARB_imaging - the
// caller then keeps its existing fast path untouched.
//
// Convolution changes the image SIZE, so width/height are in-out.
bool sfpewImagingTransfer(GLfloat* rgba, GLsizei* width, GLsizei* height);
// Cheap predicate for callers that want to skip a float conversion entirely.
bool sfpewImagingActive();
```

### A.2 State — `fpe/types.h`, in `glstate_t`

```cpp
// --- GL_ARB_imaging pixel transfer stages (GL 2.1 3.6.3) ----------------
// Every stage is disabled by default and the whole subsystem is bypassed
// then (see sfpewImagingActive), so a normal application pays nothing.

// The three colour lookup tables, in pipeline order: GL_COLOR_TABLE,
// GL_POST_CONVOLUTION_COLOR_TABLE, GL_POST_COLOR_MATRIX_COLOR_TABLE.
// Entries are RGBA floats regardless of the internalformat the caller
// requested; `format` records what was asked for so the getters can report
// it and so components the format omits read back as their identity.
struct color_table_t {
    bool enabled = false;
    GLenum internalformat = GL_RGBA;
    GLsizei width = 0;                        // 0 = no table loaded
    std::vector<glm::vec4> entries;
    GLfloat scale[4] = {1, 1, 1, 1};
    GLfloat bias[4] = {0, 0, 0, 0};
};
color_table_t color_tables[3];

// GL_CONVOLUTION_1D, GL_CONVOLUTION_2D, GL_SEPARABLE_2D. The separable
// filter keeps its row and column in `separable_row`/`separable_column`;
// `entries` is unused for it.
struct convolution_t {
    bool enabled = false;
    GLenum internalformat = GL_RGBA;
    GLsizei width = 0, height = 0;
    std::vector<glm::vec4> entries;           // width*height, row-major
    std::vector<glm::vec4> separable_row;
    std::vector<glm::vec4> separable_column;
    GLfloat filter_scale[4] = {1, 1, 1, 1};
    GLfloat filter_bias[4] = {0, 0, 0, 0};
    GLenum border_mode = GL_REDUCE;
    GLfloat border_color[4] = {0, 0, 0, 0};
};
convolution_t convolutions[3];

// GL_HISTOGRAM / GL_PROXY_HISTOGRAM. `sink` true discards the pixels after
// the tap; false lets them continue down the pipeline.
struct histogram_t {
    bool enabled = false;
    GLenum internalformat = GL_RGBA;
    GLsizei width = 0;
    bool sink = false;
    std::vector<glm::uvec4> counts;
};
histogram_t histogram;

// GL_MINMAX. Initial state per GL 2.1 Table 6.35: min = +1, max = 0, i.e.
// the reset state, not the extremes of the float range.
struct minmax_t {
    bool enabled = false;
    GLenum internalformat = GL_RGBA;
    bool sink = false;
    glm::vec4 minimum = {1, 1, 1, 1};
    glm::vec4 maximum = {0, 0, 0, 0};
};
minmax_t minmax;
```

Index helpers in `imaging.cpp`: `colorTableIndex(GLenum)` mapping
`GL_COLOR_TABLE`→0, `GL_POST_CONVOLUTION_COLOR_TABLE`→1,
`GL_POST_COLOR_MATRIX_COLOR_TABLE`→2 and the three `GL_PROXY_*` forms to the
same slots (proxies validate only, they store nothing), returning −1 for
anything else. Same shape for convolutions.

### A.3 Colour tables (9 entry points)

| Entry point | Status |
|---|---|
| `glColorTable` | [ ] |
| `glColorSubTable` | [ ] |
| `glColorTableParameterfv` | [ ] |
| `glColorTableParameteriv` | [ ] |
| `glCopyColorTable` | [ ] |
| `glCopyColorSubTable` | [ ] |
| `glGetColorTable` | [ ] |
| `glGetColorTableParameterfv` | [ ] |
| `glGetColorTableParameteriv` | [ ] |

`glColorTable(target, internalformat, width, format, type, table)`:

- `width` must be a **power of two**, else `GL_INVALID_VALUE`. (This is the
  rule most implementations relaxed; GL 2.1 still requires it, and
  `GL_MAX_COLOR_TABLE_SIZE` bounds it.)
- Decode `(format, type)` into RGBA floats using the shared conversion helper
  from I.1 — if Group I is not done, write the helper in `imaging.cpp` and have
  I.1 adopt it later. **Do not write two decoders.**
- Apply `scale`/`bias` at *load* time, per spec: stored entry =
  `clamp(value * scale + bias, 0, 1)`.
- Components the `internalformat` omits are filled with the identity: R/G/B → 0,
  A → 1 for `GL_ALPHA`-only tables, and so on. Read
  `/docsgl/gl2/glColorTable.xhtml` Table 3.15 rather than guessing.
- `GL_PROXY_*` targets validate and set the proxy state queryable through
  `glGetColorTableParameteriv`, storing no data.

`glColorSubTable(target, start, count, format, type, data)`: replaces
`[start, start+count)`; `start + count > width` → `GL_INVALID_VALUE`. Errors if
no table is loaded.

`glCopyColorTable` / `glCopyColorSubTable`: read from the framebuffer with
`glReadPixels` (already wrapped, `ordered_passthrough.cpp:117`) into a scratch
buffer, then take the `glColorTable`/`glColorSubTable` path. One row, `width`
pixels, at `(x, y)`.

`glGetColorTable(target, format, type, table)`: encode the stored RGBA floats
into the requested `(format, type)` and write them out. Honors **pack** pixel
store state — `GL_PACK_ALIGNMENT`, `GL_PACK_ROW_LENGTH`, `GL_PACK_SWAP_BYTES`
(`types.h:794`). A bound pixel **pack** buffer must be honored too; there is a
`sfpewPackPboBound()` helper already (`grep` it in `ordered_passthrough.cpp`).

`glGetColorTableParameter{f,i}v` pnames: `GL_COLOR_TABLE_SCALE`,
`GL_COLOR_TABLE_BIAS`, `GL_COLOR_TABLE_FORMAT`, `GL_COLOR_TABLE_WIDTH`,
`GL_COLOR_TABLE_RED_SIZE`, `_GREEN_SIZE`, `_BLUE_SIZE`, `_ALPHA_SIZE`,
`_LUMINANCE_SIZE`, `_INTENSITY_SIZE`. The `*_SIZE` pnames report bits per
component for the chosen internalformat — report 8 for the 8-bit formats, 32
for the float ones, 0 for components the format omits.

### A.4 Convolution (13 entry points)

| Entry point | Status |
|---|---|
| `glConvolutionFilter1D` | [ ] |
| `glConvolutionFilter2D` | [ ] |
| `glConvolutionParameterf` | [ ] |
| `glConvolutionParameterfv` | [ ] |
| `glConvolutionParameteri` | [ ] |
| `glConvolutionParameteriv` | [ ] |
| `glCopyConvolutionFilter1D` | [ ] |
| `glCopyConvolutionFilter2D` | [ ] |
| `glGetConvolutionFilter` | [ ] |
| `glGetConvolutionParameterfv` | [ ] |
| `glGetConvolutionParameteriv` | [ ] |
| `glSeparableFilter2D` | [ ] |
| `glGetSeparableFilter` | [ ] |

Filter upload mirrors §A.3: decode `(format, type)` → RGBA float, apply
`GL_CONVOLUTION_FILTER_SCALE` / `_FILTER_BIAS` at load time, store.

Kernel width/height are bounded by `GL_MAX_CONVOLUTION_WIDTH` /
`_HEIGHT` — pick 32 and report it consistently through
`glGetIntegerv`; exceeding it is `GL_INVALID_VALUE`. Unlike colour tables,
convolution kernel sizes need **not** be powers of two.

`glConvolutionParameter*` pnames: `GL_CONVOLUTION_BORDER_MODE`
(`GL_REDUCE` | `GL_CONSTANT_BORDER` | `GL_REPLICATE_BORDER`),
`GL_CONVOLUTION_BORDER_COLOR` (4 floats), `GL_CONVOLUTION_FILTER_SCALE`,
`GL_CONVOLUTION_FILTER_BIAS`. Note the scalar `glConvolutionParameterf` accepts
only the single-valued pnames; the 4-valued ones through it are
`GL_INVALID_ENUM`.

**The border modes change the output size** and this is where implementations
usually get it wrong:

- `GL_REDUCE` — output is `(w - kw + 1) × (h - kh + 1)`. The image *shrinks*.
  Every downstream consumer must handle that, which is why
  `sfpewImagingTransfer` takes width/height as in-out parameters.
- `GL_CONSTANT_BORDER` — output keeps the input size; samples outside use
  `GL_CONVOLUTION_BORDER_COLOR`.
- `GL_REPLICATE_BORDER` — output keeps the input size; edge pixels are clamped.

`glSeparableFilter2D(target, internalformat, width, height, format, type, row,
column)`: target must be `GL_SEPARABLE_2D`. Stores two 1D filters; the
convolution is then row-pass then column-pass, which is `O(w·h·(kw+kh))` instead
of `O(w·h·kw·kh)`. Implement the separable path separately rather than expanding
it into a full 2D kernel — the whole point of the entry point is the cost.

`glGetSeparableFilter(target, format, type, row, column, span)`: writes both
filters; `span` is unused in GL 2.1 (it exists for future use) — accept `nullptr`
for it.

`glGetConvolutionFilter` mirrors `glGetColorTable`, honoring pack state.

`glGetConvolutionParameter{f,i}v` adds `GL_CONVOLUTION_FORMAT`,
`GL_CONVOLUTION_WIDTH`, `GL_CONVOLUTION_HEIGHT`,
`GL_MAX_CONVOLUTION_WIDTH`, `GL_MAX_CONVOLUTION_HEIGHT` to the settable pnames
above.

**Integer-vs-float conversion trap:** for `glGetConvolutionParameteriv` with
`GL_CONVOLUTION_BORDER_COLOR` or the scale/bias pnames, the float values are
converted per GL 2.1 §6.1.2 — colours map [0,1] onto the full int range, but
scale/bias are *not* colours and are rounded, not scaled. Read the spec text.

### A.5 Histogram (5 entry points)

| Entry point | Status |
|---|---|
| `glHistogram` | [ ] |
| `glGetHistogram` | [ ] |
| `glGetHistogramParameterfv` | [ ] |
| `glGetHistogramParameteriv` | [ ] |
| `glResetHistogram` | [ ] |

`glHistogram(target, width, internalformat, sink)`: `width` must be a power of
two; `GL_PROXY_HISTOGRAM` validates only. Allocates and zeroes `width` bins per
component.

Collection, in `sfpewImagingTransfer` stage 6: for each pixel, for each of R, G,
B, A independently, `bin = clamp((int)(component * (width-1) + 0.5f), 0,
width-1)` and increment. Counts saturate at `2^32-1` rather than wrapping —
say so in a comment, because a wrapped count is a silently wrong answer.

`glGetHistogram(target, reset, format, type, values)`: writes the bins as if
they were a `width × 1` image in `(format, type)`, honoring pack state; if
`reset` is `GL_TRUE`, zeroes them afterwards. Errors with
`GL_INVALID_OPERATION` if no histogram is defined.

`glResetHistogram(target)` zeroes without reading.

`glGetHistogramParameter{f,i}v` pnames: `GL_HISTOGRAM_WIDTH`,
`GL_HISTOGRAM_FORMAT`, `GL_HISTOGRAM_RED_SIZE` … `_ALPHA_SIZE`,
`_LUMINANCE_SIZE`, `GL_HISTOGRAM_SINK`.

### A.6 Minmax (5 entry points)

| Entry point | Status |
|---|---|
| `glMinmax` | [ ] |
| `glGetMinmax` | [ ] |
| `glGetMinmaxParameterfv` | [ ] |
| `glGetMinmaxParameteriv` | [ ] |
| `glResetMinmax` | [ ] |

`glMinmax(target, internalformat, sink)` — target is `GL_MINMAX` only.

Collection, stage 7: component-wise `min`/`max` over every pixel. **The reset
state is min = 1.0, max = 0.0** (GL 2.1 Table 6.35), deliberately inverted so
the first pixel sets both. Do not initialize to ±FLT_MAX.

`glGetMinmax(target, reset, format, type, values)`: writes exactly **two**
pixels — minimum then maximum — in `(format, type)`, honoring pack state.
`glResetMinmax(target)` restores min=1, max=0.

`glGetMinmaxParameter{f,i}v`: `GL_MINMAX_FORMAT`, `GL_MINMAX_SINK`.

### A.7 Wiring the stages into the pixel paths

Once `sfpewImagingTransfer` exists, call it from every path that carries pixel
data. Each call site is the same three lines: check `sfpewImagingActive()`,
convert to RGBA float, run the transfer, convert back.

| Call site | File |
|---|---|
| `glDrawPixels` | `fpe/pixelops.cpp:458` |
| `glReadPixels` | `ordered_passthrough.cpp:117` |
| `glTexImage2D` | `getter.cpp:2190` |
| `glTexSubImage2D` | `getter.cpp` (find it) |
| `glTexImage1D` / `glTexSubImage1D` | `getter.cpp:1004`, `:1016` (inherited via the 2D delegation — verify, do not double-apply) |
| `glCopyTexImage*` / `glCopyTexSubImage*` | Group B |
| `glCopyPixels` | `fpe/pixelops.cpp:544` |
| `glColorTable` / `glConvolutionFilter*` themselves | per spec these are **not** subject to the pipeline; only `glColorSubTable`'s and the filters' own scale/bias apply |

**Do not** apply the pipeline to compressed uploads (`glCompressedTexImage*`) —
the spec exempts them.

The `sink` flags matter here: if `histogram.sink` or `minmax.sink` is true, the
pixels are consumed by the tap and **nothing is written to the destination**.
`glTexImage2D` with a sinking histogram enabled defines a texture with
unspecified contents but still allocates. Read §3.6.3 for the exact wording.

### A.8 `glEnable` / `glDisable` / `glIsEnabled`

Add all seven targets to `fpe/state.cpp`'s enable switch and to
`getter.cpp:1256`'s `glIsEnabled` — where `GL_HISTOGRAM` and `GL_MINMAX` are
currently hardcoded to report disabled. Once this group works, those two lines
must read the real state; leaving them is a silent lie.

Targets: `GL_COLOR_TABLE`, `GL_POST_CONVOLUTION_COLOR_TABLE`,
`GL_POST_COLOR_MATRIX_COLOR_TABLE`, `GL_CONVOLUTION_1D`, `GL_CONVOLUTION_2D`,
`GL_SEPARABLE_2D`, `GL_HISTOGRAM`, `GL_MINMAX`.

### A.9 Extension string

Add `"GL_ARB_imaging"` to `kDesktopExtensions` (`getter.cpp:729`) **only when
all 32 entry points and the pipeline wiring are done**. Advertising it early
recreates exactly the bug Groups E and F exist to fix. Consider also
`GL_EXT_convolution`, `GL_SGI_color_table`, `GL_SGI_color_matrix`,
`GL_EXT_histogram` for applications that probe the older spellings.

### A.10 Registration — `lookup.cpp`

All 32 with `GETPROC`, plus the historical aliases, which legacy code does use:

```cpp
GETPROC(glColorTable, name)
GETPROC_WRAPPER_ALIAS(glColorTableSGI, glColorTable)
GETPROC_WRAPPER_ALIAS(glColorTableEXT, glColorTable)
GETPROC(glConvolutionFilter1D, name)
GETPROC_WRAPPER_ALIAS(glConvolutionFilter1DEXT, glConvolutionFilter1D)
GETPROC(glHistogram, name)
GETPROC_WRAPPER_ALIAS(glHistogramEXT, glHistogram)
GETPROC(glMinmax, name)
GETPROC_WRAPPER_ALIAS(glMinmaxEXT, glMinmax)
// ... and so on for all 32
```

`GL_SGI_color_table` uses `*SGI`; `GL_EXT_convolution`, `GL_EXT_histogram` and
`GL_EXT_color_subtable` use `*EXT`. Check `/docsgl/gl2/` for the exact spellings
per function rather than assuming both exist for all of them.

### A.11 Display lists

All 32 **except the `glGet*` queries** compile into display lists. The uploads
need pointer capture sized correctly, which for these is not trivial:

```cpp
// The captured size must be what the (format,type,width) triple actually
// spans under the CURRENT unpack state, not width*4 - a caller uploading
// GL_LUMINANCE/GL_UNSIGNED_BYTE with GL_UNPACK_ROW_LENGTH set spans
// something else entirely, and over-copying reads past their buffer.
LIST_RECORD(glColorTable, {{5, sfpewPixelSpanBytes(width, 1, format, type)}},
            target, internalformat, width, format, type, table)
```

Write `sfpewPixelSpanBytes(width, height, format, type)` honoring
`GL_UNPACK_ALIGNMENT` / `_ROW_LENGTH` / `_SKIP_PIXELS` / `_SKIP_ROWS`. This
helper is needed by Group I too — put it somewhere shared
(`fpe/pointer_utils.h` is the natural home) and use it from both.

`glResetHistogram` and `glResetMinmax` also compile.

### A.12 `fixedFunctionState()` additions — `getter.cpp`

The 253-pname table already answers `GL_HISTOGRAM`/`GL_MINMAX` as disabled and
almost certainly hardcodes the rest of the imaging pnames. Every pname in
§A.3–A.6 must now read live state:

`GL_COLOR_TABLE`, `GL_POST_CONVOLUTION_COLOR_TABLE`,
`GL_POST_COLOR_MATRIX_COLOR_TABLE`, `GL_CONVOLUTION_1D`, `GL_CONVOLUTION_2D`,
`GL_SEPARABLE_2D`, `GL_HISTOGRAM`, `GL_MINMAX`, `GL_MAX_COLOR_TABLE_SIZE`,
`GL_MAX_CONVOLUTION_WIDTH`, `GL_MAX_CONVOLUTION_HEIGHT`,
`GL_POST_CONVOLUTION_RED_SCALE` and its 7 siblings (scale/bias per component,
post-convolution and post-color-matrix), `GL_COLOR_MATRIX`,
`GL_COLOR_MATRIX_STACK_DEPTH`, `GL_MAX_COLOR_MATRIX_STACK_DEPTH`.

The post-convolution and post-color-matrix scale/bias pnames are set through
`glPixelTransfer{f,i}` — check whether that entry point already accepts them
(`grep -n glPixelTransfer SimpleFPEWrapper/`) and extend it if not. They are
8 more floats in `fixed_function_uniform_t` next to `pixel_scale`/`pixel_bias`.

### A.13 Tests

Split by subsystem; one giant file will be unmaintainable.

- `tests/gtest_imaging_color_table.cc` — load/subload/copy/get round-trips for
  each internalformat; non-power-of-two width → `GL_INVALID_VALUE`; scale/bias
  applied at load; proxy targets store nothing but answer parameters; all ten
  `glGetColorTableParameter*` pnames.
- `tests/gtest_imaging_convolution.cc` — a 3×3 identity kernel leaves an image
  unchanged; a known blur kernel produces hand-computed values;
  **`GL_REDUCE` shrinks the output by `kw-1` × `kh-1`** (the assertion most
  worth having); `GL_CONSTANT_BORDER` uses the border colour;
  `GL_REPLICATE_BORDER` clamps; separable and equivalent-2D kernels agree
  pixel-for-pixel; oversized kernels error.
- `tests/gtest_imaging_histogram_minmax.cc` — a known image produces
  hand-computed bins; `reset` empties them; the initial minmax is (1,0) and one
  pixel sets both; `sink` suppresses the destination write; saturation at
  `2^32-1`; both `glGet*Parameter*` families.
- `tests/gtest_imaging_pipeline.cc` — the stages compose **in the specified
  order**: enable a colour table that inverts, a convolution that blurs, and a
  post-convolution table that halves, then assert the result matches
  applying them in that sequence and not any other. This is the test that
  proves the pipeline, and it is the one that would catch a mis-ordered stage.
- **Zero-overhead regression**: with nothing enabled, a `glTexImage2D` +
  textured draw produces byte-identical output to the same sequence before this
  group existed. Assert against a hardcoded expected pixel set, so a future
  change that accidentally routes the default path through the imaging code is
  caught.

Register each in `CMakeLists.txt`'s driver-needing `foreach(suite ...)` list.

---

## Appendix: Out of Scope

Recorded so the next reader does not go looking.

**GLX (all entry points except `glXGetProcAddress`/`glXGetProcAddressARB`)** —
this is an EGL wrapper. GLX is an X11 window-system binding; on Android there is
no X11, and on the desktop the launcher uses EGL. If a future frontend needs
GLX, it is a separate shim over EGL, not part of the FPE wrapper.

**GLU (59 pages under `/docsgl/gl2/`)** — GLU is a *utility library* layered on
GL, historically shipped as `libGLU.so`, not part of `libGL.so`. `gluPerspective`,
`gluLookAt`, `gluBuild2DMipmaps`, the tessellator, the NURBS engine: all of it is
client-side code calling public GL. It does not belong in a `libGL` replacement.
If a frontend needs it, build `libGLU` separately against this wrapper.

**Color-index mode** — `GL_INDEX_MODE` reports `GL_FALSE` and there is no
colour-index framebuffer. Every `glIndex*` entry point is therefore a no-op on
current state, which is conforming for an RGBA-only implementation. Leave it.

**`glGetTexImage` (uncompressed readback)** — same constraint as
`glGetCompressedTexImage` (§B.3): impossible on GLES 3.0 without shadowing every
upload. If it is currently a null pointer, giving it the same
forward-or-`GL_INVALID_OPERATION` treatment as §B.3 is a reasonable addition to
Group B; decide when you get there.

---

## Appendix: Progress Summary

| Group | Entry points | Done |
|---|---|---|
| A — Imaging subset | 32 | 0 / 32 |
| B — 1D/compressed/copy texture | 5 | 5 / 5 |
| C — Texture residency | 2 | 2 / 2 |
| D — Transpose matrices | 4 | 4 / 4 |
| E — Point parameters | 4 | 4 / 4 |
| F — Secondary colour immediate | 16 | 16 / 16 |
| G — Vertex attrib variants | 29 | 29 / 29 |
| H — Half-implemented fixes | 3 | 3 / 3 |
| I — Completing partial paths | 6 items | 6 / 6 |
| **Total** | **95 entry points + 6 items** | **69 / 101** |

Update this table as groups complete. When it reads 95/95, the three-part API
contract in §0.1 holds for everything `docs.gl`'s GL 2.1 pages describe, minus
the documented out-of-scope surface above.
