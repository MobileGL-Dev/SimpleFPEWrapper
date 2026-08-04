# SimpleFPEWrapper — Defect Remediation Plan

Source: `/tmp/sfpew-status-report.txt` (a gap-analysis report generated after the
95-entry-point `plans.md` work landed), independently verified claim-by-claim
against the source tree before this plan was written. One claim in that report
turned out to be **stale** (§0 below) — corrected here so it isn't
re-"fixed" a second time.

Status legend: `[ ]` not started · `[x]` complete (implemented, tested, built,
verified) · `[~]` partial / accepted-limitation, see item

---

## §0. Correction to the source report

**glEdgeFlag / GL_LINE polygon-mode edge suppression is NOT a gap.** The
report cited `state.cpp:1188`'s comment on `glEdgeFlag()`, which said
suppression "does not consume this value yet" — but that comment was stale.
`sfpewBuildWireframeIndices` (`fpe.cpp:552`) already consumes edge flags
(`if (edge_flags != nullptr && a < flag_count && edge_flags[a] == 0) return;`),
called from both the uniform-GL_LINE path (`drawing1x.cpp:547`) and the
mixed-polygon-mode path (`fpe.cpp:1036`), and `tests/gtest_edgeflag.cc`
already exercises and passes it (`PolygonSuppressesVerticalEdges`,
`TwoQuadsInOneBeginEndEachSuppressTheirOwnVerticals`,
`ConstantFlagSetOutsideBeginEndAppliesToTheWholePrimitive`).
**Already fixed in this pass:** reworded the stale comment (`state.cpp`) to
point at the real consumer instead of claiming the gap.

Lesson for whoever reads a "gap" claim in this file: verify against the
*code*, not just a comment — comments rot faster than the code they describe.

---

## §1. Real, confirmed defects

### 1.1 [x] `glPushAttrib`/`glPopAttrib`: depth/stencil/scissor not captured

**File:** `SimpleFPEWrapper/fpe/attribstack.cpp`. Confirmed gap — the file's
own header comment says so and the code has no `GL_DEPTH_BUFFER_BIT` /
`GL_STENCIL_BUFFER_BIT` / `GL_SCISSOR_BIT` handling at all.

**This is easier than it looks.** `fixed_function_state_t::backend_state`
(`types.h:414`, `backend_state_shadow_t`) already tracks every value needed —
`depth_func`, `depth_mask`, `scissor[4]`, `stencil_func/ref/value_mask/mask/
fail/zfail/zpass`, and `enable_known[]`/`enable_value[]` for
`kEnableDepthTest`/`kEnableStencilTest`/`kEnableScissorTest`. This shadow is
already actively maintained by the `SHADOWED_STATE` macros in
`ordered_passthrough.cpp` (`glDepthFunc`, `glDepthMask`, `glScissor`,
`glStencilFunc`, `glStencilMask`, `glStencilOp`) for every draw. Nothing new
needs to be tracked — only captured into `attrib_snapshot_t` and replayed.

Mirror the existing `color_buffer`/`GL_COLOR_BUFFER_BIT` pattern exactly:

**`attrib_snapshot_t`** (near the existing `color_buffer` field): add a
`fixed_function_state_t::backend_state_shadow_t depth_stencil_scissor{};`
field (just copy the whole shadow struct — simpler than picking fields, and
correct since the struct only holds POD).

**`glPushAttrib`**, alongside the existing `if (mask & GL_COLOR_BUFFER_BIT)`
block:
```cpp
if (mask & (GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT | GL_SCISSOR_BIT | GL_ENABLE_BIT))
    snap.depth_stencil_scissor = st.backend_state;
```
(One combined capture — cheap, POD, and `GL_ENABLE_BIT` needs the same three
`enable_value[]` slots color_buffer already demonstrates sharing across bits.)

**`glPopAttrib`**: write a `restore_depth_stencil_scissor(const shadow_t&
current, const shadow_t& wanted, GLbitfield mask)` beside `restore_color_buffer`
in `state.cpp`, gated per-field on which bit(s) requested it:
```cpp
if (mask & GL_DEPTH_BUFFER_BIT) {
    if (wanted.depth_func != current.depth_func && g_glFuncs.glDepthFunc)
        g_glFuncs.glDepthFunc(wanted.depth_func);
    if (wanted.depth_mask != current.depth_mask && g_glFuncs.glDepthMask)
        g_glFuncs.glDepthMask(wanted.depth_mask);
}
if (mask & GL_STENCIL_BUFFER_BIT) {
    if ((wanted.stencil_func != current.stencil_func || wanted.stencil_ref != current.stencil_ref ||
         wanted.stencil_value_mask != current.stencil_value_mask) && g_glFuncs.glStencilFunc)
        g_glFuncs.glStencilFunc(wanted.stencil_func, wanted.stencil_ref, wanted.stencil_value_mask);
    if (wanted.stencil_mask != current.stencil_mask && g_glFuncs.glStencilMask)
        g_glFuncs.glStencilMask(wanted.stencil_mask);
    if ((wanted.stencil_fail != current.stencil_fail || wanted.stencil_zfail != current.stencil_zfail ||
         wanted.stencil_zpass != current.stencil_zpass) && g_glFuncs.glStencilOp)
        g_glFuncs.glStencilOp(wanted.stencil_fail, wanted.stencil_zfail, wanted.stencil_zpass);
}
if (mask & GL_SCISSOR_BIT) {
    if (std::memcmp(wanted.scissor, current.scissor, sizeof(wanted.scissor)) != 0 && g_glFuncs.glScissor)
        g_glFuncs.glScissor(wanted.scissor[0], wanted.scissor[1], wanted.scissor[2], wanted.scissor[3]);
}
if (mask & (GL_DEPTH_BUFFER_BIT | GL_ENABLE_BIT) && wanted.enable_known[shadow_t::kEnableDepthTest])
    (wanted.enable_value[shadow_t::kEnableDepthTest] ? g_glFuncs.glEnable : g_glFuncs.glDisable)(GL_DEPTH_TEST);
// ...same pattern for kEnableStencilTest/GL_STENCIL_TEST and kEnableScissorTest/GL_SCISSOR_TEST
```
**Critical:** after issuing the raw backend calls, write the restored values
back into `st.backend_state` (mirroring `st.color_buffer = snap.color_buffer;`
at `attribstack.cpp:287`). Skipping this desyncs the shadow from reality —
exactly the class of bug the TLS/depth-test-enable review found elsewhere
this session: a raw backend call that bypasses the wrapper's own shadowed
entry point must update the shadow itself, or a later `SHADOWED_STATE`-gated
call de-duplicates against a stale belief and silently drops a real change.

**Test:** `tests/gtest_pushattrib_depth_stencil_scissor.cc` — push with each of
`GL_DEPTH_BUFFER_BIT`, `GL_STENCIL_BUFFER_BIT`, `GL_SCISSOR_BIT` individually
and combined via `GL_ALL_ATTRIB_BITS`; change every captured value; pop; assert
via `glGetIntegerv`/`glGetBooleanv`/`glIsEnabled` that each reverted. Also
assert an UN-pushed bit's changes survive the pop (proves the mask gating,
not a blanket restore). One render-backed case: push, disable `GL_DEPTH_TEST`,
draw two overlapping quads (both visible without depth testing), pop, confirm
depth testing is back on by drawing a third occluded quad and checking it's
hidden.

**Done. Found and fixed a real, pre-existing, higher-impact bug along the
way.** Implemented exactly as planned (the field-level selective copy design
held up; no surprises there). But the render-backed test and the
`GL_ALL_ATTRIB_BITS` case were flaky in a way that traced back to
`SHADOWED_STATE` (`ordered_passthrough.cpp`): `if (shadow.known && (shadow_test))
return;` short-circuits `shadow_test` on the very first call to ANY
`SHADOWED_STATE`-gated entry point in a process (`glDepthFunc`, `glScissor`,
`glStencilFunc`, etc.) — but `shadow_test`'s false-branch is where the new
value actually gets written into `shadow`. So the first call forwards
correctly to the backend but leaves the shadow at its compile-time default;
the SECOND call to the same entry point then compares against that wrong
default and can wrongly treat a real change as redundant, silently dropping
it. Fixed by always evaluating `shadow_test` and gating only the
skip-the-backend-call decision on `shadow.known`. This was invisible until
something read the shadow's value fields directly and expected them to match
reality — which glPopAttrib's restore does, and which turned out to be
**the actual root cause of the `glDrawPixels(GL_DEPTH_COMPONENT)` "known
failing" bug from the previous session (§1.3)** — see that section.

### 1.2 `glGetTexImage`: no 3D/cube path

**File:** `SimpleFPEWrapper/getter.cpp:2737`. Currently only `GL_TEXTURE_2D`
(with 1D silently remapped to it); everything else is `GL_INVALID_ENUM`.

Same shape as `glGetCompressedTexImage` (already fixed to forward-on-desktop /
refuse-on-ES in the prior session): GLES 3.0 has no texture readback of any
kind, full stop — this isn't specific to 3D/cube. Extend the target switch:
accept `GL_TEXTURE_3D`, `GL_TEXTURE_CUBE_MAP_POSITIVE_X` through
`_NEGATIVE_Z`, forward on desktop GL (`g_glFuncs.glGetTexImage` is in the
GL-only, ES-absent set — guard it), else `GL_INVALID_OPERATION` with a comment
matching `glGetCompressedTexImage`'s.

**Test:** extend `tests/gtest_getter_*` (find the getter suite covering
`glGetTexImage` today) with a `DesktopContextTest` round-trip for a 3D texture
and a cube face, and a `ContextTest` (GLES3) case asserting
`GL_INVALID_OPERATION` and an untouched output buffer.

### 1.3 [x] `glDrawPixels(GL_DEPTH_COMPONENT)`: depth write never reaches the buffer

**Resolved as a side effect of §1.1 — was never a drawQuad/shader bug at
all.** The blit-based rewrite this section originally recommended was never
implemented and turned out to be unnecessary. Root cause was the
`SHADOWED_STATE` cold-shadow bug described at the end of §1.1: this test's
own `depth_func_(GL_ALWAYS_)` call (needed so `drawQuad`'s depth write always
passes) was the *first* `glDepthFunc` call in the process, which left the
shadow silently wrong; the test's own verification quad then asked for
`GL_LESS` and that call was wrongly treated as redundant against the stale
shadow, so the backend silently stayed at `GL_ALWAYS` for it — the probe
"won" against whatever was actually in the buffer regardless of what
`glDrawPixels` had written, exactly mimicking "the write never landed". Every
piece of evidence gathered in the prior session (no GL errors, correct
uploaded texel, correct uniform locations, correct state readback *at the
moment queried*) was individually true and collectively misleading, because
none of it caught a shadow that was wrong precisely between two specific
calls. Fixed by the `SHADOWED_STATE` fix in §1.1; no changes needed in
`pixelops.cpp` beyond the two defensive improvements already made in the
prior session (forcing `GL_DEPTH_TEST`/`GL_ALWAYS` during the depth-mode
quad draw, and the unconditional `gl_FragDepth` write) — both still correct
and worth keeping, just not what fixed this.

**Test:** `tests/gtest_drawpixels_formats.cc`'s
`DepthPixelsWriteDepthWithoutChangingColorOrColorMask` passes; comment
updated to describe the real root cause instead of "known failing".

### 1.4 `GL_COLOR_SUM`: secondary color never reaches the fragment

**Files:** `SimpleFPEWrapper/fpe/fpe_shadergen.cpp`, `getter.cpp:1647`,
`SimpleFPEWrapper/fpe/types.h`.

Entry points/state/queries already exist (Group F, prior session); this is
purely the shader-generator half that was explicitly deferred there.

1. Add `bool color_sum_enable` to `fixed_function_bool_t` (or wherever
   `alpha_test_enable` etc. live) and wire `glEnable`/`glDisable(GL_COLOR_SUM)`
   to it in `state.cpp`'s enable switch (find the existing switch — the
   "unsupported, report false" bucket currently swallows this case; move it
   out and forward to the new bool).
2. Make `color_sum_enable` part of the **program key** — it changes generated
   fragment shader text, so a program compiled without it must not be reused
   once it's toggled on. (See how `fog_enable`/`alpha_test_enable` are already
   folded into the key — same pattern.)
3. In `fpe_shadergen.cpp`'s fragment-output assembly (wherever the final
   `FragColor`/similar is written), when the key's `color_sum_enable` is set,
   add `uniform vec4 SecondaryColor;` and `fragColor.rgb += SecondaryColor.rgb;`
   (alpha is NOT summed — GL 2.1 3.9.1 defines secondary color as RGB-only,
   consistent with `glSecondaryColor3*` never taking an alpha component).
4. Upload `SecondaryColor` alongside the other per-vertex/per-draw uniforms —
   find where `fpe_draw.current_data.secondary_color` (already populated by
   the Group F entry points, `types.h:239` area) is read today (likely
   nowhere yet outside the getter) and wire it into
   `program_uniform_locations_t`/`program_uniform_values_t` +
   `send_uniforms`, the same way `fog_color` etc. are handled.
5. Flip `getter.cpp:1647`'s `case GL_COLOR_SUM: return capability(false);` to
   read the new `color_sum_enable` bool.

**Test:** extend `tests/gtest_secondary_color.cc` (or add
`gtest_color_sum.cc`) with a render check: primary color red (1,0,0), 
secondary color green (0,1,0), `GL_COLOR_SUM` enabled → pixel should read
yellow-ish (1,1,0); disabled → pixel should read red only. Cover both
immediate-mode and vertex-array-array secondary color sources if
`glSecondaryColorPointer` is already wired into the draw path (it is, per the
Group F/existing `fpe/vertexpointer.cpp`).

### 1.5 `glLineStipple`: state exact, rasterization not implemented

**File:** `SimpleFPEWrapper/fpe/state.cpp:1495` area (state setter, already
correct), no consumer anywhere.

This is a genuinely harder one — line stippling is a per-fragment,
distance-along-the-line pattern test, and neither GL 3.2 core nor ES 3.0 has
it natively (removed from core GL 3.1+, never in ES). Two viable approaches:

- **CPU pattern expansion** (simpler, matches this project's existing
  GL_LINE-polygon-mode CPU wireframe-expansion style): for each line segment
  in a stippled draw, walk it in screen space and emit only the sub-segments
  where `(factor * pattern bit at floor(distance/factor)) != 0`, uploading a
  denser vertex/index buffer of just the "on" dashes. Reuses the existing
  `sfpewBuildWireframeIndices`-adjacent infrastructure conceptually but needs
  actual position math (screen-space or object-space distance, per GL 2.1
  spec — object-space, actually: "stipple counter... incremented for each
  fragment", which is really screen-space in practice for all real
  implementations; match that, it's what applications expect).
- **Fragment-shader pattern test**: pass the stipple pattern/factor as a
  uniform, compute a per-fragment "distance along line" varying from the
  vertex shader (accumulated screen-space length), and `discard` where the
  pattern bit is 0. Cheaper to implement than CPU expansion, but needs
  `GL_LINE_STIPPLE` folded into the program key (new shader variant), and the
  distance-along-line varying only makes sense for `GL_LINES`/`GL_LINE_STRIP`/
  `GL_LINE_LOOP` — verify it composes correctly with the *existing*
  GL_LINE-polygon-mode wireframe expansion (§0), which turns filled
  primitives into `GL_LINES` draws that stippling would then also need to
  apply to consistently.

Given the complexity and that this is used far less than the other items,
this is scoped as **do last**, and a reasonable stopping point if time runs
out is: implement it for `GL_LINES`/`GL_LINE_STRIP`/`GL_LINE_LOOP` only (not
composed with GL_LINE polygon mode), document that composition gap explicitly
if left unaddressed, matching this project's own convention of stating
deviations rather than leaving them silently incomplete.

**Test:** `tests/gtest_line_stipple.cc` — a horizontal `GL_LINES` segment with
`glLineStipple(1, 0x00FF)` (8 on, 8 off) should show alternating lit/unlit
pixels at the expected period; `glLineStipple` with `factor` > 1 should widen
the period proportionally; disabling `GL_LINE_STIPPLE` should draw solid.

### 1.6 `glLogicOp`: no entry point, and a real ceiling on what's fixable

**File:** none — doesn't exist anywhere in the tree.

Unlike the other items, this has a **hard backend-capability ceiling**: ES
3.0 core removed fixed-function logic ops entirely (no
`glLogicOp`/`GL_COLOR_LOGIC_OP` capability at all — it's not merely
"the entry point is missing", the *feature* doesn't exist on that floor), and
there is no reliable, extension-free way to read the framebuffer's current
color back inside a fragment shader on ES 3.0 core to emulate it (that needs
`GL_EXT_shader_framebuffer_fetch` or similar, not guaranteed present). Per
this project's own two-backend, no-branching invariant (`docs/backend-support.md`),
a feature that only one floor can execute doesn't get a real implementation —
see how texture residency and priorities were handled (real, honest, standards-
conforming state tracking with no forwarded rendering effect) and follow that
same pattern here:

1. Add the entry point in `state.cpp`: validate `opcode` is one of the 16 GL
   2.1 logic-op enums (`GL_CLEAR` through `GL_SET`), store it, `LIST_RECORD`
   it (it compiles into display lists per §5.4).
2. `GL_LOGIC_OP_MODE` getter reads the stored value instead of the hardcoded
   `GL_COPY`.
3. `GL_COLOR_LOGIC_OP`/`GL_INDEX_LOGIC_OP` **stay reporting disabled** — do
   NOT flip these to true, because enabling them would be a lie: nothing
   actually applies the logic op to rendering. Leave a comment at the
   `glEnable`/`glIsEnabled` case explaining this is the same class of
   deliberate, documented boundary as `GL_TEXTURE_3D`/`GL_TEXTURE_CUBE_MAP`.
4. Register in `lookup.cpp` (`GETPROC(glLogicOp, name)` + no ARB/EXT alias
   needed, `glLogicOp` has been core since GL 1.0).

**Test:** `tests/gtest_logicop.cc` — state round-trips through
`GL_LOGIC_OP_MODE`; invalid opcode raises `GL_INVALID_ENUM`; `glEnable(
GL_COLOR_LOGIC_OP)` + `glIsEnabled` still reports `GL_FALSE` (documenting the
boundary, not a bug); the value compiles into and replays from a display
list.

### 1.7 3D / Cube texture sampling in the fixed-function pipeline

**Files:** `SimpleFPEWrapper/fpe/fpe_shadergen.cpp`,
`SimpleFPEWrapper/fpe/types.h`, `getter.cpp:1623` area.

The largest item here — comparable in scope to a full new group from the
95-entry-point plan. Both backend floors (GL 3.2 core, ES 3.0) support
`sampler3D`/`samplerCube` and `glTexImage3D`/cube-face uploads natively, so
this is genuinely achievable, just sizeable:

1. **State**: extend the per-unit texture-target tracking (currently just
   `texture_2d_enable[MAX_TEX]`, `types.h:209`) to a per-unit *target enum*
   (`GL_TEXTURE_2D` / `GL_TEXTURE_3D` / `GL_TEXTURE_CUBE_MAP` — GL 2.1's rule
   is only one target may be enabled per unit at a time; the highest-priority
   enabled target wins if more than one is enabled, per spec 3.8.14 texture
   application order — implement that priority order, don't just take
   whichever was enabled last).
2. **Shader generation**: in `add_fs_uniforms` (`fpe_shadergen.cpp:1682`
   area) and wherever the sampling expression is built, branch the uniform
   type and the `texture(...)` call's coordinate arity on the active target
   per unit (`sampler2D`+`vec2`, `sampler3D`+`vec3`, `samplerCube`+`vec3`).
   Texture coordinates already carry up to 4 components
   (`fixed_function_draw_data_t::texcoord[MAX_TEX]`, `glm::vec4`) — 3D/cube
   just consume one more component than 2D does.
3. **Program key**: the per-unit target must be part of the key (it changes
   the generated shader's uniform types, not just its logic).
4. **Getter**: `GL_TEXTURE_3D`/`GL_TEXTURE_CUBE_MAP` in `glIsEnabled` move out
   of the "always false" bucket into reading the real per-unit target state.
5. **Texture upload wiring**: `glTexImage3D`/`glTexSubImage3D`/
   `glCopyTexSubImage3D`/`glCompressedTexImage3D`/`glCompressedTexSubImage3D`
   and the six cube-face `glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X+i, ...)`
   targets currently reach the backend as raw passthrough with no FPE
   bookkeeping (no proxy validation, no level/size tracking the way 2D has in
   `texture_metadata_cache_t`). At minimum, extend the metadata cache to key
   on target as well as texture name+level so `glGetTexLevelParameter*` stays
   honest for these too.
6. `glTexGen`'s `GL_REFLECTION_MAP`/`GL_NORMAL_MAP` modes (cube-map-specific
   texgen, GL 1.3/ARB_texture_cube_map) become meaningful once cube sampling
   exists — check whether the existing texgen switch already has cases for
   these that just have nothing to bind to yet, or whether they need adding
   too.

**Test:** `tests/gtest_texture_3d_cube.cc` — build both a 3D texture (a small
volume with distinguishable slices) and a cube map (six distinguishable
faces), bind each to unit 0 in turn, draw a textured quad with texture
coordinates that hit a known slice/face, and assert the sampled color. Cover
the "only the highest-priority enabled target is sampled" rule with a case
that enables both `GL_TEXTURE_2D` and `GL_TEXTURE_3D` on the same unit
simultaneously.

### 1.8 API-manifest coverage: 85 of 551 GL≤2.1 commands unlisted

**Files:** `tools/gen_api_manifest.py`, `docs/api-manifest.{json,md}`.

Lowest priority — this is a *tooling* completeness gap, not a runtime one (all
85 resolve correctly via `eglGetProcAddress`, verified empirically this
session: `total=551 null=0 resolved=551`). The manifest generator's static
analysis of `lookup.cpp` apparently can't see resolutions that don't go
through a literal `GETPROC(name, ...)` text match — likely the macro-generated
`glVertexAttrib*`/`glUniform*` families and a handful of query/object
functions (`glIsProgram`, `glGenBuffers`, etc.) that resolve some other way
(possibly backend-alias macros the tool doesn't pattern-match, or forwarding
through a different resolver path entirely).

1. Read `tools/gen_api_manifest.py` to find what patterns it recognizes.
2. For each of the 85 (list reproducible via the Python snippet below),
   determine whether it's (a) resolved by a pattern the tool doesn't
   recognize yet — teach the tool to recognize it — or (b) genuinely not
   explicitly listed in `lookup.cpp` and instead falls through to some
   default/generic resolution path — make it explicit with a real
   `GETPROC` line, both for manifest accuracy and because implicit
   resolution paths are exactly the kind of thing that silently breaks when
   `lookup.cpp` is refactored.
3. Regenerate `docs/api-manifest.{json,md}` and confirm the
   `api_manifest_current` CTest still passes.

Reproduce the missing-85 list:
```python
import xml.etree.ElementTree as ET, json
tree = ET.parse('/docsgl/specs/gl.xml'); root = tree.getroot()
gl21 = set()
for f in root.findall('feature'):
    if f.get('api') == 'gl' and float(f.get('number')) <= 2.1:
        for req in f.findall('require'):
            for c in req.findall('command'): gl21.add(c.get('name'))
        for rem in f.findall('remove'):
            for c in rem.findall('command'): gl21.discard(c.get('name'))
manifest = json.load(open('docs/api-manifest.json'))
print(sorted(gl21 - set(manifest['entries'].keys())))
```

**Test:** the existing `api_manifest_current` / `backend_profile_current`
CTest entries are the test — no new test file needed, just keep them green.

---

## §2. Explicitly NOT in scope (do not "fix" these)

- **Color-index mode** (`glIndex*`, `glClearIndex`, `glIndexMask`) — accepted,
  documented boundary from the original `plans.md` Appendix: "Color-index
  mode... conforming for an RGBA-only implementation. Leave it." The report's
  mention of these under "still commented out" is accurate but not a defect —
  it's the intended state. If closing the manifest-coverage gap (§1.8) wants
  these listed, add them as literal no-op `GETPROC` entries that resolve to
  a real (do-nothing, honest) implementation rather than actually building
  color-index framebuffer support.
- **GLX / GLU** — out of scope per `plans.md`'s existing Appendix (window-system
  binding and a separate utility library respectively, neither belongs in a
  `libGL` replacement).

---

## §3. Order of implementation

Recommended: **1.1 → 1.2 → 1.3 → 1.4 → 1.6 → 1.8 → 1.5 → 1.7**, i.e. easiest
and highest-confidence first (1.1's shadow struct already exists; 1.2 is a
copy-paste of an existing pattern), the real bug next while context from the
prior investigation is freshest (1.3), then the moderate shader work (1.4),
the honest-limitation item (1.6, cheap either way), the tooling gap (1.8,
low-effort), and the two hardest/largest items last (1.5, 1.7) — 1.7
especially is worth re-scoping into its own group-style plan if picked up in
a later session, matching how the original 95-entry-point work was
structured.

Build/test/regenerate loop is unchanged from `plans.md` §0.8-§0.10: build,
`ctest --test-dir <build> --output-on-failure`, regenerate
`docs/api-manifest.json`/`docs/backend-profile.json` if `lookup.cpp` or
`backend/loader.h` changed, commit per-item with the project's
`[Tag] (Scope): Subject` + prose-body convention, author
`BZLZHH <admin@bzlzhh.top>`, no `Co-Authored-By`, no push.
