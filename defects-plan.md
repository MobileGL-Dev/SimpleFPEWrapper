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

### 1.2 [x] `glGetTexImage`: no 3D/cube path

**File:** `SimpleFPEWrapper/getter.cpp:2737`. Currently only `GL_TEXTURE_2D`
(with 1D silently remapped to it); everything else is `GL_INVALID_ENUM`.

Same shape as `glGetCompressedTexImage` (already fixed to forward-on-desktop /
refuse-on-ES in the prior session): GLES 3.0 has no texture readback of any
kind, full stop — this isn't specific to 3D/cube. Extend the target switch:
accept `GL_TEXTURE_3D`, `GL_TEXTURE_CUBE_MAP_POSITIVE_X` through
`_NEGATIVE_Z`, forward on desktop GL (`g_glFuncs.glGetTexImage` is in the
GL-only, ES-absent set — guard it), else `GL_INVALID_OPERATION` with a comment
matching `glGetCompressedTexImage`'s.

**Done as planned.** `glGetTexImage` was entirely absent from
`backend/loader.h`/`.cpp` (never called on the backend before — the 2D path
reads through a scratch-FBO + `glReadPixels`, not a native readback call), so
this also added the `GL_FUNC_TYPEDEF`/`GL_FUNC_DECL`/`INIT_BACKENDGL_FUNC`
triplet and a `tools/check_backend_profile.py` `NON_UNIVERSAL` entry
(`"GL only: ES has no texture readback"`, matching `glGetCompressedTexImage`'s
wording), then regenerated `docs/backend-profile.json`.

**Test:** `tests/gtest_getteximage_3d_cube.cc` — a `DesktopContextTest`
round-trip for a distinguishable 2×2×2 3D texture and a distinguishable cube
face (both against the real NVIDIA driver, not llvmpipe), and a `ContextTest`
(GLES3) case asserting `GL_INVALID_OPERATION` and a byte-for-byte untouched
output buffer for both `GL_TEXTURE_3D` and a cube face.

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

### 1.4 [x] `GL_COLOR_SUM`: secondary color never reaches the fragment

**Files:** `SimpleFPEWrapper/fpe/fpe_shadergen.cpp`, `getter.cpp:1647`,
`SimpleFPEWrapper/fpe/types.h`.

Entry points/state/queries already exist (Group F, prior session); this is
purely the shader-generator half that was explicitly deferred there.

**Done — turned out simpler than planned, no uniform needed.** Secondary
colour is not a per-draw uniform in this codebase's design; it is a proper
per-vertex generic attribute (slot 6), already flowing through the SAME
generic mechanism `add_vs_inout` uses for every other optional vertex input
(color, normal, fog coord, texcoords): the loop already declares
`layout(location=6) in vec4 SecColor;` / `out vec4 vertexSecColor;` and
wires the pass-through assignment automatically whenever attribute slot 6 is
active (`enabled_pointers` bit 6 for `glSecondaryColorPointer`, or
`sizes.data[6] > 0` for immediate-mode `glSecondaryColor3*`, which
`mglSecondaryColor` already sets). So this needed only:

1. `bool color_sum_enable` added to `fixed_function_bool_t` (`types.h`).
   Confirmed via `glstate.cpp:program_hash()` that this struct is hashed and
   cache-compared as one memcmp'd block, not field-by-field — a new bool
   here is automatically part of the program key with no other code to touch.
2. `glEnable`/`glDisable(GL_COLOR_SUM)` wired to it in `hijack_fpe_states`
   (`state.cpp`), moved out of the "unsupported" bucket it was falling into.
3. A new `scratch_t::has_secondary_color_input` flag (mirroring the existing
   `has_fog_coord_input`), set in `add_vs_inout` when attribute slot 6's
   `usage == GL_SECONDARY_COLOR_ARRAY`. Needed because `color_sum_enable`
   could be true while slot 6 was never fed (app enables GL_COLOR_SUM but
   never calls glSecondaryColor3*) — without this guard the fragment shader
   would reference an attribute add_vs_inout never declared. Gated on
   *both* flags together; the GL default secondary colour is `{0,0,0,1}`,
   so skipping the reference in that case is a correct no-op, not an
   approximation.
4. One line in `add_fs_body`: `color.rgb += vertexSecColor.rgb;` — alpha
   untouched, since secondary colour has none (GL 2.1 3.9.1) — placed after
   the specular-color addition and before the alpha test, matching the
   spec's texturing → color-sum → fog conceptual order.
5. `getter.cpp`'s `GL_COLOR_SUM` case now reads `bools.color_sum_enable`.

**Test:** `tests/gtest_color_sum.cc` — enabled sums red+green into yellow at
the framebuffer, disabled (both before enabling and after re-disabling)
shows red only; alpha is confirmed untouched by the sum (writes through
from the primary color's alpha alone); `GL_COLOR_SUM` enabled with
`glSecondaryColor3*` never called still renders correctly (the
has_secondary_color_input guard). The existing
`gtest_secondary_color.cc::AliasesAndExistingQueriesCoverTheWholeSurface`
assertion that `GL_COLOR_SUM` reads disabled by default still holds — it
never enables it, so the (now real, not hardcoded) query still reports
`false`.

### 1.5 [x] (scope reduced, documented) `glLineStipple`: rasterization now implemented for immediate-mode lines

**Files:** new `SimpleFPEWrapper/fpe/linestipple.cpp` (+ declaration in
`fpe.hpp`), one interception branch in `drawImmediateVertices`
(`drawing1x.cpp`), comment fix in `state.cpp`.

Went with neither of the two originally-sketched approaches. CPU segment
expansion (rewriting the vertex/index buffer into just the "on" dashes)
adds real geometry-topology risk; folding a stipple varying into the
generated uber-shader needs `noperspective` interpolation to get
screen-space-linear distance, which GLSL ES 3.00 core does **not**
guarantee (desktop GL 3.2 core has it since GLSL 1.30, but that asymmetry
is exactly the kind of thing the two-backend-no-branching invariant rules
out).

**What it does instead:** a completely separate, dedicated shader program
(same pattern as `pixelops.cpp`'s quad drawer, `quad_drawer_t`/`drawer()`) -
outside the FPE program cache entirely, so no program-key or uber-shader
change was needed at all. For each vertex in the immediate-mode run, the
CPU (which already holds the fully assembled interleaved float stream at
this point, same as `drawImmediateVertices`' own selection-mode parsing)
projects position through the current MVP + viewport to get a window-space
position, then computes a **plain per-vertex scalar** - cumulative
window-space distance since the stipple counter's last reset - which a
*regular* (non-flat, non-`noperspective`) interpolated varying carries
correctly with no cross-backend risk, because the two endpoints' values are
computed exactly on the CPU and only the interior needs interpolating,
same as any other per-vertex scalar attribute. The dedicated fragment
shader does `bit = uint(distance / factor) & 15u` and discards where the
pattern bit is 0.

Per GL 2.1 spec 3.4.2, the counter resets every segment for `GL_LINES` but
is continuous across a whole `GL_LINE_STRIP`/`GL_LINE_LOOP` - implemented
exactly: `GL_LINES` gets `distance=0` at every even vertex;
`GL_LINE_STRIP` accumulates from vertex 0; `GL_LINE_LOOP` accumulates the
same way and appends one extra vertex (a copy of vertex 0, carrying the
total loop length) so the closing segment continues the count forward
instead of interpolating backward to 0 - the draw call becomes
`GL_LINE_STRIP` with `vertexCount+1` vertices, the same rasterized shape a
`GL_LINE_LOOP` of `vertexCount` produces.

**Scope, deliberately reduced from the original sketch** (documented in
`linestipple.cpp`'s header, not left implicit):
- **Immediate-mode only** (`glBegin(GL_LINES/GL_LINE_STRIP/GL_LINE_LOOP)
  ... glEnd()`) - the historically dominant use of this legacy feature
  (debug grids, wireframe helpers), and the one place the wrapper already
  has the assembled per-vertex stream on the CPU pre-upload without new
  plumbing. Vertex-array-sourced stippled lines
  (`glDrawArrays`/`glDrawElements` with `GL_LINE_STIPPLE` enabled) still
  draw solid - a known, undone gap, not a silent one.
- **No lighting/texturing/fog** on stippled lines - the dedicated program
  only consumes position and color. Real `GL_LINE_STIPPLE` usage is
  overwhelmingly flat-colored debug/UI lines; folding this into the
  uber-shader generator to cover the rare lit/textured stippled line would
  cost far more risk than the case is worth.
- **Not composed with GL_LINE polygon mode** (§0's wireframe expansion) -
  a stippled outline of a filled primitive still draws as a solid outline.
  This was the explicit fallback the original plan text allowed for.

The interception is a single gated branch inserted in
`drawImmediateVertices`, right after the existing selection/feedback
early-return and before display-list-replay/`init_fpe()` (the dedicated
drawer needs neither): `if (line_stipple_enable && primitive is one of the
three) { sfpewDrawStippledLines(...); return; }`. `line_stipple_enable`
defaults to `false`, so every draw that isn't a stippled line evaluates one
extra boolean check and takes the existing path completely unchanged.

**Test:** `tests/gtest_line_stipple.cc` - on/off alternation at chosen
sample points for `glLineStipple(1, 0x00FF)`; disabling returns to solid;
`glLineStipple(8, 0x0001)` widens the dash to 8px then stays dark for the
rest of a 64px line; a `GL_LINE_STRIP` bend test constructed so continuous
distance accumulation and a (wrong) per-segment reset disagree at a
specific sample point, by design (see the test's own comment for the
arithmetic) - proving the continuity, not just the existence, of
`GL_LINE_STRIP` support; a `GL_LINE_LOOP` sanity check that the closing
segment actually renders rather than being silently dropped by the
extra-vertex handling.

### 1.6 [x] `glLogicOp`: no entry point, and a real ceiling on what's fixable

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

**Done as planned.** `logic_op_mode` lives in `fixed_function_state_t`
(`types.h`, next to `alpha_func`); `glLogicOp` validates via the enum range
`GL_CLEAR..GL_SET` (contiguous, 0x1500-0x150F), stores, and `LIST_RECORD`s.
Declared in `fpe/state.h` (redundant with `<GL/gl.h>`'s own declaration,
already pulled in transitively by `lookup.cpp` via `backend/loader.h`, but
matches this file's existing pattern of re-declaring every wrapper-owned
standard GL function). Registered in `lookup.cpp`.

**Test:** `tests/gtest_logicop.cc` — state round-trips through
`GL_LOGIC_OP_MODE` for several opcodes including the two boundary values
(`GL_CLEAR`, `GL_SET`); an invalid opcode raises `GL_INVALID_ENUM` and
leaves the previously-stored mode untouched; `glEnable(GL_COLOR_LOGIC_OP)` +
`glIsEnabled` still reports `GL_FALSE` (documenting the boundary, not a
regression); the value compiles into and replays from a display list
(mirroring `TextureResidencyTest`'s existing list pattern); resolves via
`eglGetProcAddress` with no context (`LibraryTest`).

### 1.7 [x] 3D / Cube texture sampling in the fixed-function pipeline

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

**Done, scope narrowed from the original 6-point sketch to items 1-4 (state,
shader generation, program-key, getter); items 5-6 explicitly deferred, see
below:**

1. **State**: rather than a per-unit target *enum*, added two more per-unit
   bool arrays alongside the existing `texture_2d_enable[MAX_TEX]` —
   `texture_3d_enable[MAX_TEX]` and `texture_cube_enable[MAX_TEX]`
   (`types.h`'s `fixed_function_bool_t`). GL 2.1 doesn't actually forbid more
   than one target being simultaneously enabled on a unit (it just says which
   one wins, 3.8.14) — an enum would have had to reject/reorder a second
   `glEnable` that real drivers accept, so three independent bools plus a
   priority-resolving helper is both simpler and closer to the spec's actual
   words. `hijack_fpe_states` (`state.cpp`) now writes
   `bools->texture_3d_enable[unit]`/`texture_cube_enable[unit]` for
   `GL_TEXTURE_3D`/`GL_TEXTURE_CUBE_MAP`. All three bool arrays live inside
   `fixed_function_bool_t`, which `glstate.cpp`'s `program_hash()` already
   memcmp's/hashes as one block (see the technical-notes precedent from 1.4),
   so no separate program-key work was needed beyond keeping the three
   `texture_2d_enable[i]`-only gates in `glstate.cpp` consistent (below) —
   the cache key itself was correct automatically.
   The priority order (cube beats 3D beats 2D beats 1D, GL 2.1 3.8.14) is
   resolved once by a shared helper: `texture_target_kind_t` enum
   (`none`/`tex2d`/`tex3d`/`cube`) and `active_texture_target(state, unit)`,
   declared in `fpe_shadergen.h` (so both the shader generator and
   `glstate.cpp` can call it) and defined in `fpe_shadergen.cpp`.
2. **Shader generation**: `add_fs_uniforms` now emits `samplerCube` /
   `sampler3D` / `sampler2D` per unit based on `active_texture_target`
   (previously always `sampler2D`). The main texturing loop in `add_fs_body`
   gates on `active_texture_target(...) != none` instead of
   `texture_2d_enable[i]`, and swizzles the already-`vec4` `texCoord{i}`
   attribute as `.xyz` for 3D/cube vs `.xy` for 2D/1D — no vertex-shader
   change needed, since `texCoord{i}` was already declared `vec4` for every
   unit regardless of arity (`glTexCoord2f` just leaves `.z`/`.w` at their
   defaults). `combine_argument()`'s `GL_TEXTUREn` source resolution was
   updated the same way, so `GL_COMBINE` stages can reference a 3D/cube unit
   as a source.
   **Deliberately unchanged**: `unit_uses_texgen`/`texgen_needs_eye`/
   `texgen_needs_normal` still gate on `texture_2d_enable` only. Texgen
   composed with a 3D or cube target (`GL_REFLECTION_MAP`/`GL_NORMAL_MAP`'s
   natural use, item 6 below) remains a documented gap, not a silent one —
   texgen simply never activates for a 3D/cube-only unit, and the unit
   samples its plain vertex texcoord attribute instead of a generated one.
3. **Program key / cache consistency**: found and fixed three more
   `texture_2d_enable[i]`-only sites in `glstate.cpp` that had to move to
   `active_texture_target(...) != none` for correctness, not just cache
   hygiene:
   - `send_uniforms`'s sampler-uniform-index upload gate (`glUniform1i` for
     `Sampler{i}`) — without this fix a 3D/cube-only unit's shader-declared
     sampler would never get bound to its texture unit index and would
     default to reading unit 0.
   - `program_hash`'s cache-fast-path COMBINE-staleness check.
   - `program_hash`'s hash-building COMBINE-parameter digest.
   `glstate.cpp` didn't previously include `fpe_shadergen.h`; added.
4. **Getter**: `GL_TEXTURE_3D`/`GL_TEXTURE_CUBE_MAP` moved out of
   `glIsEnabled`'s "always false" bucket (`getter.cpp` ~1623) into reading
   `bools.texture_3d_enable[unit]`/`texture_cube_enable[unit]` directly,
   right next to the existing `GL_TEXTURE_2D`/`GL_TEXTURE_1D` cases.
   `textureBindingQuery()` needed **no changes at all** — it already handled
   both targets generically before this item started (confirmed by reading
   it), so `sfpewLogicalTextureBinding(GL_TEXTURE_3D)`/`(GL_TEXTURE_CUBE_MAP)`
   were already fully functional.

**Deferred, not done — explicit scope cuts:**

- **Item 5 (texture-upload metadata wiring)**: `texture_metadata_cache_t`
  (`getter.cpp`) still keys on 2D-only level metadata; `glTexImage3D`/cube
  faces don't populate it. This was already a known, already-documented gap
  from 1.2's `glGetTexImage` work (see the comment above
  `glGetTexImage`'s `GL_TEXTURE_3D`/cube-face case in `getter.cpp`) — 1.7
  doesn't extend it. Consequence: `glGetTexLevelParameter*` on a 3D/cube
  texture still can't answer size queries the way the 2D path can; this is
  a read-back/introspection gap, not a rendering one, and out of scope for
  "3D/cube texture *sampling*."
- **Item 6 (`GL_REFLECTION_MAP`/`GL_NORMAL_MAP` texgen)**: not implemented;
  see the "deliberately unchanged" note under shader generation above. A
  3D/cube unit samples its plain texcoord attribute; texgen for that unit is
  a no-op regardless of `GL_TEXTURE_GEN_S/T/R` state.

**Test:** `tests/gtest_texture_3d_cube.cc` — `Texture3DCubeTest` fixture on
`ContextTest(GLES3, 16)`. Four tests: a 3D texture with two distinguishably-
colored slices sampled at each slice's center z; a cube map with two
distinguishably-colored faces (+X, -X) sampled by direction vector;
`glIsEnabled` tracking `GL_TEXTURE_3D`/`GL_TEXTURE_CUBE_MAP` independently of
each other and of `GL_TEXTURE_2D` and of each other's enable/disable calls;
and the priority-order case enabling both `GL_TEXTURE_2D` (solid blue) and
`GL_TEXTURE_CUBE_MAP` (solid red +X face) on the same unit, confirming the
cube map wins and the 2D texture is never sampled.

### 1.8 [x] (code complete, regeneration pending final verification pass) API-manifest coverage: 85 of 551 GL≤2.1 commands unlisted

**Files:** `tools/gen_api_manifest.py`, `docs/api-manifest.{json,md}`.

Root cause, confirmed by reading the tool and `lookup.cpp`'s
`eglGetProcAddress`: not a regex gap. After every `GETPROC`/alias check
fails, `eglGetProcAddress` falls through to
`g_eglFuncs.eglGetProcAddress(name)` - asking the backend's own resolver for
the SAME name unchanged. That is the correct, working, intentional
resolution path for standard commands the wrapper does not need to
intercept (`glUniform*`, `glGenBuffers`, `glIsProgram`, ...); adding an
explicit `GETPROC(name, name)` line for each of the ~85 would be pure
boilerplate against the fallback's own purpose. There is no per-symbol text
in `lookup.cpp` for a regex to find here, by design - option (a) from the
original plan ("teach the tool to recognize a pattern") doesn't apply
because there is no pattern, and option (b) ("add explicit GETPROC lines")
would fight the architecture rather than describe it.

Fixed by giving the tool a **reference set** instead: `GL21_CORE_COMMANDS`,
the full 551-name GL≤2.1 core command list, frozen as a literal directly in
`gen_api_manifest.py` (no runtime dependency on `gl.xml`/`/docsgl` - this
tool must keep working in any build environment). After computing the
regex-derived `entries` as before, anything in `GL21_CORE_COMMANDS` still
unaccounted for is added with `status: "backend-fallthrough"`. This is a
computed set difference, not a frozen list of "today's 85" - it self-updates
as `lookup.cpp` gains real entries over time. Confirmed self-consistent
already: `glGetTexImage` and `glLogicOp` were both in the original 85 and
both gained real `GETPROC` lines this session (§1.2, §1.6), so they now
resolve via the regex path instead and never reach the fallthrough
classification - no edit to the reference set was needed for either.
`docs/api-manifest.md`'s header now explains what `backend-fallthrough`
means for a reader who has not seen this reasoning.

**Not yet done: regenerating `docs/api-manifest.{json,md}` and reconfirming
`api_manifest_current`.** Per this session's build-pacing instruction, no
build/test runs happen until every §1.x item is written; this is the last
step of the final verification pass (`python3 tools/gen_api_manifest.py .
docs`, then `ctest -R api_manifest_current`).

**Test:** the existing `api_manifest_current` / `backend_profile_current`
CTest entries are the test — no new test file needed, just keep them green
once regenerated.

---

## §1.9 Bugs found by the wrap-up verification pass

Running the eight suites above end to end (after all eight items were
written) surfaced two more pre-existing defects, neither part of the
original 8-item scope but fixed on the spot since the new tests exercised
them directly:

- **`glEnable`/`glDisable` (`state.cpp`) never called `sfpewEnsureBackend()`
  before their raw-passthrough tail.** Every other backend passthrough in
  the wrapper guards with `if (!sfpewEnsureBackend() || g_glFuncs.X ==
  nullptr) return;` before touching `g_glFuncs`; these two were the one
  exception. Harmless as long as some earlier call in the same context
  already forced the lazy backend load - which is true for almost every
  real test, since `glClear`/`glReadPixels`/etc. all guard correctly - but
  `LogicOpTest.EnablingTheCapabilityStillReportsDisabled` (1.6's new test)
  calls `glEnable(GL_COLOR_LOGIC_OP)` as its very first GL call. That cap
  isn't recognized by `hijack_fpe_states` or `backendEnableRedundant`, so it
  fell straight through to `g_glFuncs.glEnable(cap)` with an all-zero
  `g_glFuncs` (nothing had loaded the backend yet) - a segfault, not a test
  failure. Fixed by adding the same guard both functions' backend tails were
  missing.
- **`tests/gtest_line_stipple.cc`'s `kMidY` was off by one.** A horizontal
  line at NDC `y=0.0` maps to window `y=32.0` exactly on a 64px-tall
  viewport - the boundary between pixel rows 31 and 32, not the interior of
  either. This driver's rasterizer places that exact-boundary line in row
  31, confirmed by probing every row around the midpoint with plain,
  unstippled rendering. `kMidY` was `kSize / 2` (32); changed to `kSize / 2
  - 1` (31). Not a production bug - the dedicated stipple shader in
  `linestipple.cpp` was already computing the right distances; the test was
  reading the wrong row of its own output. (Ironically, one test in the
  file - the `GL_LINE_STRIP` bend/continuity case - happened to pass anyway
  before this fix, because its assertion direction was "must be unlit,"
  which is trivially true when literally nothing is rendering.)

Full-suite result after both fixes: 263/265 non-piglit CTest entries pass
(`ctest -E piglit`); the 2 remaining failures
(`ClearDepthDrawBufferTest.SignedQueryObjectOnGles` and
`ClearDepthDrawBufferDesktopTest.SignedQueryObjectOnDesktopCore`) are a
pre-existing, unrelated query-object-availability driver quirk in this
environment - not touched by, and not caused by, any of the eight items
here.

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
