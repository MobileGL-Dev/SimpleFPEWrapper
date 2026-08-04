# defects-plan-2.md — second audit pass

Source: `/tmp/sfpew-latest-audit.txt` (8 "仍缺/不完整" items + 1 boundary
note + 1 side finding). Verified every claim against source before acting,
same discipline as `defects-plan.md`'s §0.

## Verification verdict

7 of 8 items confirmed accurate by direct code read. Item 1 (`glReadBuffer`)
has the right conclusion but a wrong stated reason — corrected below. The
boundary note and the `check_backend_profile.py` side finding are addressed
inline in item 1's writeup, since they share its false premise.

## Scope decisions for this round

**Implementing:**
- 2.1 `glReadBuffer` — genuinely unimplemented (not a GLES floor gap; see below)
- 2.2 `GL_POINT_SPRITE` / `GL_COORD_REPLACE` — real ES3-capable feature via `gl_PointCoord`
- 2.3 `GL_POINT_FADE_THRESHOLD_SIZE` — real ES3-capable feature, alpha fade math
- 2.4 `GL_POINT_SMOOTH` — real, cheap approximation once 2.2's `gl_PointCoord`/"is this a point draw" infrastructure exists
- 2.5 `glDrawPixels(GL_STENCIL_INDEX, ...)` — multi-pass bit-plane technique, same dedicated-drawer family as `pixelops.cpp`'s depth quad (§1.3 last round)
- 2.6 3D/cube texgen composition (`GL_REFLECTION_MAP`/`GL_NORMAL_MAP` for 3D/cube units) — extends last round's §1.7
- 2.7 `texture_metadata_cache_t` keyed by target, for `glGetTexLevelParameter*` on 3D/cube — extends last round's §1.2/§1.7

**Not changing (already correct, verified):**
- `GL_LINE_SMOOTH`, `GL_POLYGON_SMOOTH` — real edge-AA needs per-primitive
  geometry processing (line thickening, polygon edge distance) this
  wrapper's native-rasterizer draw path does not have. Neither backend
  floor gives a working fixed-function line/polygon smooth in a core
  profile either (desktop GL 3.2 core dropped it same as ES). Same
  honest-state-no-fake-effect situation as `glLogicOp` from last round;
  the codebase's own established answer to that situation (hardcoded
  `false`, no forwarded render effect) is the *correct* answer here too,
  not an oversight. `GL_POINT_SMOOTH` is different only because a cheap,
  real per-point radial falloff is achievable (2.4) — line/polygon edges
  are not, without a geometry-expansion rewrite disproportionate to the
  feature's real-world usage in the mobile games this wrapper targets.
- `GL_POLYGON_OFFSET_LINE`, `GL_POLYGON_OFFSET_POINT` — same reasoning.
  ES 3.0 core only has `GL_POLYGON_OFFSET_FILL`; a spec-accurate offset
  for the other two needs the *original triangle's* depth slope re-applied
  while emitting the wireframe-expansion's line/point geometry, which
  `sfpewDrawMixedPolygonMode` does not currently carry through. Honest
  `false`, no change.
- GLES 3D/cube texture readback (`glGetTexImage` refusing on GLES) —
  permanent GLES limitation (no texture readback at all on that floor),
  already correctly handled since last round's §1.2. The audit's item 6
  conflated this with the texgen gap (2.6, fixable) and the metadata-cache
  gap (2.7, fixable) in one bullet; only the readback half is untouchable.
- selection/feedback skipped when a user program is current, `GL_AUTO_NORMAL`
  finite-difference approximation (item 8) — both already-documented,
  permanent design boundaries (can't introspect an arbitrary user shader;
  finite differences are the spec-sanctioned approximation), not defects.
- Color-index mode, GLX/GLU (boundary note) — explicit non-goals, confirmed
  unchanged from last round's §2.

---

## 2.1 [x] `glReadBuffer`

**Audit's stated reason is wrong**: `GL_READ_BUFFER`/`glReadBuffer` is real
GL ES 3.0 core (`backend/loader.cpp:205` loads it unconditionally, in the
same block as `glTexImage3D`/`glDrawRangeElements` — universal, not
desktop-only). The actual gap: `nm -D` on the built `.so` shows
`glReadBuffer` is not an exported symbol at all (compare `glDrawBuffer`,
which is fully wrapped in `ordered_passthrough.cpp`), and it is not in
`lookup.cpp`'s `GETPROC` table, so `eglGetProcAddress("glReadBuffer")`
falls through to the *real* backend's own `eglGetProcAddress` — bypassing
`LIST_RECORD` (no display-list capture) and every other wrapper-side
barrier. `docs/api-manifest.json`'s "backend-fallthrough" categorization
(added last round, §1.8) is describing exactly this case correctly; the
gap is real, just not for the reason given.

**Fix**: implement `glReadBuffer` in `ordered_passthrough.cpp`, mirroring
`glDrawBuffer`'s existing structure almost exactly — reuse
`isLegacyDrawBuffer`/`isAuxDrawBuffer`/`isColorAttachment`, check
`GL_READ_FRAMEBUFFER_BINDING` instead of `GL_DRAW_FRAMEBUFFER_BINDING`,
map legacy selectors to `GL_BACK` on the default framebuffer, validate
`GL_COLOR_ATTACHMENTn` against `GL_MAX_COLOR_ATTACHMENTS` on a user FBO.
One difference from `glDrawBuffer`: `GL_FRONT_AND_BACK` is legal for
`glDrawBuffer` but explicitly **illegal** for `glReadBuffer` (spec: "not a
single buffer") — `isLegacyDrawBuffer` must be paired with an explicit
`buf != GL_FRONT_AND_BACK` check for the read-buffer case, not reused
verbatim.

Add `GL_READ_BUFFER` to `getter.cpp`'s `glGetIntegerv` (same pattern as the
existing `GL_DRAW_BUFFER` case: read the real backend value directly via
`g_glFuncs.glGetIntegerv(GL_READ_BUFFER, ...)`, default `GL_BACK` if the
backend isn't loaded yet — no wrapper-side shadow copy, matching precedent).

**Test**: `tests/gtest_read_buffer.cc` — enum validation (rejects
`GL_FRONT_AND_BACK`, accepts legacy/attachment forms appropriately per
default-vs-user-FBO), `GL_READ_BUFFER` query round-trips, display-list
compile/replay captures it (LIST_RECORD parity with `glDrawBuffer`), and
an actual multi-attachment FBO render+select+read proving the selected
attachment's content is what comes back.

**Done** as sketched, no deviations: `glReadBuffer` added to
`ordered_passthrough.cpp` right after `glDrawBuffer`, reusing
`isLegacyDrawBuffer`/`isAuxDrawBuffer`/`isColorAttachment` with the
`GL_FRONT_AND_BACK` carve-out; `GL_READ_BUFFER` added to
`glGetIntegerv`; `GETPROC(glReadBuffer, name)` added to `lookup.cpp`. Five
tests in `tests/gtest_read_buffer.cc`, including the multi-attachment FBO
case (clear each of two attachments to a distinct colour by selecting it
as the sole *draw* buffer first, sidestepping the FPE uber-shader's
single-output limitation entirely, then prove `glReadBuffer` selects
between them for `glReadPixels`).

## 2.2 [x] `GL_POINT_SPRITE` / `GL_COORD_REPLACE`

Real ES3 feature: `gl_PointCoord` is a valid fragment-shader builtin on
both backend floors, upper-left origin by default matching GL's own
`GL_UPPER_LEFT` default for `GL_POINT_SPRITE_COORD_ORIGIN` (already tracked,
`state.cpp:1771`, previously dead state with no consumer).

The uber-shader is **not** primitive-keyed (no `GL_POINTS`-specific program
variant — confirmed by grep, the generator never branches on primitive
type), so whether `gl_PointCoord` should replace a unit's texture
coordinate can't be a compile-time decision; it has to be a per-draw
uniform (`IsPointPrimitive`, computed from `fpe_state.fpe_draw.primitive
== GL_POINTS` in `send_uniforms`) tested at runtime in the fragment shader.
This same uniform is reused by 2.3 and 2.4 below.

**State**: `bool point_sprite_enable = false;` (the `GL_POINT_SPRITE`
enable itself) and `bool point_sprite_coord_replace[MAX_TEX] = {false};`
(per-unit `GL_COORD_REPLACE`, since it's a `glTexEnv(GL_POINT_SPRITE, ...)`
parameter targeting the current active unit) both join
`fixed_function_bool_t` — automatic program-cache-key participation, no
extra hash-building code, per the pattern established last round.

**Entry points**: `hijack_fpe_states` gets a `GL_POINT_SPRITE` case.
`glTexEnvf`/`glTexEnvi` currently bail out immediately unless
`target == GL_TEXTURE_ENV` (with one existing carve-out for
`GL_TEXTURE_FILTER_CONTROL`/`GL_TEXTURE_LOD_BIAS`) — add a matching
carve-out for `target == GL_POINT_SPRITE && pname == GL_COORD_REPLACE`,
writing the per-unit bool.

**Shader generation**: in the fragment shader's per-unit texturing loop,
when `state.fpe_bools.point_sprite_enable &&
state.fpe_bools.point_sprite_coord_replace[i]`, branch the sample
coordinate on the new `IsPointPrimitive` uniform: `IsPointPrimitive ?
gl_PointCoord : texCoord{i}.xy` (flipping `gl_PointCoord.y` when
`point_sprite_coord_origin == GL_LOWER_LEFT`, matching GLES's fixed
upper-left builtin against GL's selectable-origin state).

**Getter**: `GL_POINT_SPRITE` moves out of `glIsEnabled`'s hardcoded-false
bucket into `capability(bools.point_sprite_enable)`. Add a
`GL_COORD_REPLACE` case to whatever `glGetTexEnviv`/`glGetTexEnvfv` use for
`GL_POINT_SPRITE`-targeted queries.

**Test**: `tests/gtest_point_sprite.cc` — render a point with a 2x2
per-quadrant texture, `GL_COORD_REPLACE` on, sample each quadrant of the
rendered point and confirm it matches the corresponding texel (not a flat
per-vertex texcoord); a control case with `GL_COORD_REPLACE` off on the
same texture proves the point renders with the ordinary (flat/absent)
texcoord instead; `GL_POINT_SPRITE_COORD_ORIGIN` flip case;
`glIsEnabled(GL_POINT_SPRITE)` round-trip.

**Done**, one scope narrowing from the sketch: `unit_has_point_sprite_replace()`
only fires for a `tex2d`-target unit (`active_texture_target(...) ==
texture_target_kind_t::tex2d`) — `GL_COORD_REPLACE` is defined in terms of
an (s,t) pair (GL 1.4 spec 3.9.1), and a 3D/cube unit's sample coordinate
has no 2-component point-sprite equivalent to substitute in without an
arbitrary (and unspecified) choice of which component `gl_PointCoord`'s
two values would land in. A 3D/cube unit with `GL_COORD_REPLACE` set
simply keeps sampling its ordinary texcoord/texgen-driven coordinate, a
documented, narrow exclusion rather than a silent one. Otherwise as
sketched: `IsPointPrimitive`/`PointSpriteLowerLeftOrigin` uniforms live in
`send_uniforms` (glstate.cpp), recomputed every draw (not just on state
change, since the same cached program can serve a `GL_POINTS` draw
immediately followed by `GL_TRIANGLES`). `tests/gtest_point_sprite.cc`
covers coord-replace on the S axis (sidesteps the origin question
entirely), the coord-origin flip on the T axis specifically (predicts
which row lands at the top under each origin from `gl_PointCoord`'s own
fixed upper-left GLSL ES semantics), the disabled-control case, and
`glIsEnabled` round-trip.

## 2.3 [x] `GL_POINT_FADE_THRESHOLD_SIZE`

Real GL 1.4 point-parameter companion to distance attenuation (GL 2.1 spec
3.3): when the derived (post-attenuation, pre-clamp) point size falls below
the threshold, alpha scales by `(derivedSize / threshold)^2` before the
size itself clamps to `PointSizeMin` for rasterization. State already
exists (`state.cpp:1757`, `getter.cpp:1705`) with no consumer.

`derivedPointSize` is already computed in the vertex shader, but only
inside the `point_attenuation_active` branch — add a `PointFadeAlpha`
varying computed there (`clamp((derivedPointSize / max(PointFadeThreshold,
1e-6))^2, 0.0, 1.0)` when `derivedPointSize < PointFadeThreshold`, else
`1.0`). **Must** gate the fragment-shader multiply
(`if (IsPointPrimitive) color.a *= PointFadeAlpha;`) behind the same
`IsPointPrimitive` uniform from 2.2 — `point_attenuation_active` is a
*state* flag, not a per-primitive one, so without the gate a triangle draw
made while point attenuation happens to be active would get its alpha
multiplied by a meaningless leftover value.

**Test**: extend `tests/gtest_point_sprite.cc` (same fixture, point
rendering) or a small dedicated file — a point whose attenuated size falls
under the threshold renders visibly faded (lower alpha) vs. the same point
with fade threshold at 0 (always full alpha); confirms non-point primitives
drawn in the same frame are unaffected.

**Done** as sketched. `sfpewInitializePointSizeMax`'s existing driver-range
query meant the "close" test case (derived size well above threshold) and
"far" case (derived size well below) both needed picking distances
carefully so the rasterized point stays several pixels wide in *both*
cases (not shrunk to a near-invisible dot for the faded case) - landed on
distances giving derived sizes 16px/4px against an 8px threshold, so
`(4/8)^2 = 0.25` alpha is comfortably distinguishable from full opacity
without either sample needing sub-pixel precision. Also updated a stale
comment on `GL_POINT_FADE_THRESHOLD_SIZE`'s state.cpp setter that
pre-dated this work ("alpha fading remains outside this group's minimum
rendering scope").

## 2.4 [x] `GL_POINT_SMOOTH`

Cheap once 2.2/2.3's `IsPointPrimitive` + `gl_PointCoord` plumbing exists:
radial soft-edge alpha falloff, the classic point-AA approximation —
`if (IsPointPrimitive && PointSmoothEnabled) { float r =
length(gl_PointCoord - 0.5) * 2.0; color.a *= 1.0 - smoothstep(0.8, 1.0,
r); }`. `bool point_smooth_enable` joins `fixed_function_bool_t`,
`hijack_fpe_states` case added, getter reads the real value.

**Test**: extend the point-sprite test file — a solid-color point with
`GL_POINT_SMOOTH` on has near-zero alpha at its extreme corner texel
(outside the inscribed circle) and full alpha at its center; off leaves
the corner fully opaque.

**Done** as sketched. Sampled a few pixels back from the true square-footprint
edge (not right at `kPointSize/2`) for rasterization-boundary safety
margin, matching this round's `GL_POINT_SPRITE`/texgen tests' general
caution about sampling exactly on a boundary - harmless here since
`smoothstep(0.8, 1.0, r)` is already fully saturated well before 100% of
the radius, so backing off costs no test power.

## 2.5 [x] `glDrawPixels(GL_STENCIL_INDEX, ...)`

Currently a hard, unconditional `GL_INVALID_OPERATION` regardless of
whether a stencil buffer exists (`pixelops.cpp:1158`) — GL 2.1 requires
this to actually write the stencil buffer when one is present (table 3.6
lists `GL_STENCIL_INDEX` as a legal `glDrawPixels` format).

No portable way to write **per-fragment-varying** stencil values in one
pass on either backend floor (the fixed-function stencil test's `ref` is a
single scalar per draw call, not a per-fragment input) — so this needs the
same multi-pass bit-plane technique real hardware-stencil-upload emulation
uses: upload the source stencil bytes as an `R8UI` integer texture, then
for each of the 8 bits, one full-coverage quad pass with
`glStencilMask(1u << bit)`, `glStencilFunc(GL_ALWAYS, 0xFF, 0xFF)`,
`glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE)`, and a dedicated fragment
shader that `discard`s wherever that bit of the sampled texel is 0 (so only
"this bit should be set" fragments survive to the `REPLACE`). 8 draws
total, same dedicated-shader-drawer family as `pixelops.cpp`'s depth quad
(§1.3 last round) and `linestipple.cpp` (§1.5) — outside the FPE uber-shader
entirely, ES3-core-only features (integer textures, `discard`, stencil
ops), no extensions.

Respects the existing raster-position/zoom/pixel-store handling already in
`glDrawPixels` — reuses the same source-row unpacking `describePixelFormat`
already does for `GL_STENCIL_INDEX`, just needs the destination changed
from "reject" to "upload as R8UI + run the 8-pass stencil write" when a
stencil buffer is actually present (check via
`glGetFramebufferAttachmentParameteriv(GL_STENCIL_ATTACHMENT, ...)` or the
same technique `attribstack.cpp`'s new depth/stencil/scissor restore code
from last round already uses to detect stencil presence).

**Test**: `tests/gtest_drawpixels_stencil.cc` — clear stencil to 0, draw a
checkerboard `GL_STENCIL_INDEX` pattern, then render a full-screen quad
gated by `glStencilFunc(GL_EQUAL, <value>, 0xFF)` for a couple of distinct
values and confirm only the matching checkerboard cells pass; a
no-stencil-buffer control case still returns the existing
`GL_INVALID_OPERATION` (this path is additive, not a relaxation of the
correct-refusal case).

**Done, three real deviations from the sketch**, each because the sketch's
first idea turned out to be unsound or unnecessary on closer look:

1. **16 passes, not 8.** A single discard-based pass per bit can only
   *set* that bit (via `REPLACE` through a 1-bit `glStencilMask`) for
   fragments whose source has it on; fragments whose source has it off get
   `discard`ed and their *existing* stencil bit is left alone. That is
   correct only if the destination is already known-zero everywhere the
   quad will touch, which is not true in general (this is a *replace this
   rectangle*, not *OR into it*, operation). Rather than pre-clearing the
   destination rectangle (which needs a temporary scissor box intersected
   with whatever the caller's own scissor state already was, restored
   afterward - real but avoidable bookkeeping), each bit gets **two**
   passes: one paints the bit to 1 wherever the source has it set
   (`discard` where clear), a second paints it to 0 wherever the source
   has it clear (`discard` where set). Together every fragment's bit ends
   up exactly right regardless of prior buffer content, with no pre-clear
   and no scissor juggling. 16 draws total - still just 16 GPU draw calls
   for the entire `glDrawPixels` invocation, not per-pixel, so the cost is
   irrelevant for this cold, rare path.
2. **`GL_R8`/`GL_RED`/`GL_UNSIGNED_BYTE`, not `R8UI` integer texture.**
   An 8-bit UNORM texture round-trips every one of the 256 possible byte
   values through upload and `texture()` sampling *exactly* (no precision
   loss), so `uint(texel.r * 255.0 + 0.5)` recovers the exact source byte
   in the shader. This avoids adding a second sampler type/upload path to
   the shared quad-drawer program (`kQuadFS` already only ever declares
   `sampler2D uTex`) for a benefit (exact integer semantics) the UNORM
   round-trip already provides for free.
3. **No explicit "does a stencil buffer exist" check, and consequently no
   `GL_INVALID_OPERATION` for the missing-buffer case anymore.** Real GL
   does not error when a fragment operation targets a plane the current
   framebuffer does not have (writing colour when there is no colour
   buffer is a silent no-op, not an error) - the *pre-existing* blanket
   refusal was actually stricter than spec, not spec-matching. Matching
   `drawQuad`'s own depth-mode precedent (force `GL_ALWAYS`/`REPLACE`,
   ignore the caller's own test state entirely - see the comment above
   `drawStencilBitplanes`), this new path just issues the 16 passes
   unconditionally; a framebuffer with no stencil attachment simply has
   nowhere for `glStencilMask`'s bits to land, exactly like the existing
   depth path already behaves when there is no depth buffer. The plan's
   own test sketch (a no-stencil-buffer case "still returns
   GL_INVALID_OPERATION") was written before this was worked through and
   is superseded - `tests/gtest_drawpixels_stencil.cc` does not have that
   case for this reason.

Also implemented, matching the existing colour path's own established
scope boundary rather than inventing a new one: `GL_INDEX_SHIFT`/
`GL_INDEX_OFFSET` are **not** applied (state.cpp's `glPixelTransfer`
already treats them as a project-wide no-op, "index shift/offset belong
to color-index mode, which this implementation does not provide" -
inherited verbatim, not special-cased away for stencil). `GL_PIXEL_MAP_S_TO_S`
**is** applied - a real, separate feature from colour-index mode, and
`pixel_map_stencil`/`g_glstate.pixel_maps[1]` already existed with nothing
consuming them. `GL_BITMAP` as a stencil source type is a further,
documented scope cut (`readPixelStencilIndex` only accepts the six
integer scalar types; `GL_FLOAT`/`GL_HALF_FLOAT` are correctly rejected
too, since GL 2.1 table 3.6 does not list them as legal for
`GL_STENCIL_INDEX` regardless).

**Test**: `tests/gtest_drawpixels_stencil.cc` - four tests: a 2x2
checkerboard of four distinct values (`0x03`/`0x50`/`0xAA`/`0xFF`, chosen
to exercise different bit patterns rather than one repeated value) landing
at their own pixels; a single-pixel write proving pixels *outside* the
drawn rectangle are provably untouched (the property the 16-pass, no-
pre-clear design exists to guarantee); `GL_PIXEL_MAP_S_TO_S` remapping
through a real (if trivial) 2-entry table; `GL_FLOAT` rejected with
`GL_INVALID_ENUM` and the buffer left untouched.

## 2.6 [x] 3D/cube texgen composition (`GL_REFLECTION_MAP`/`GL_NORMAL_MAP`)

Last round's §1.7 deliberately left `unit_uses_texgen`/`texgen_needs_eye`/
`texgen_needs_normal` gated on `texture_2d_enable` only, documented as a
scope cut. Extend all three to `active_texture_target(...) !=
texture_target_kind_t::none` (the same helper from `fpe_shadergen.h` used
throughout §1.7), so a 3D or cube unit with `GL_TEXTURE_GEN_*` enabled
actually gets generated coordinates instead of silently sampling the raw
vertex texcoord attribute.

`GL_REFLECTION_MAP`/`GL_NORMAL_MAP` texgen already compute a 3-component
eye-space vector (reflection or normal direction) — check whether the
existing texgen codegen path already emits a full `vec3` (needed for cube
sampling) or truncates to `vec2` assuming a 2D destination; a cube map
consumes the reflection/normal vector directly as its 3-component sample
coordinate (that's the entire point of these two modes — GL 1.3's
`ARB_texture_cube_map` companion), so once the gate opens this may already
produce the right vector and just needs the `needs_3_component` swizzle
path (already `.xyz`) to pick it up instead of the `.xy` 2D truncation.

**Test**: `tests/gtest_texgen_cube.cc` — a cube map with 6 distinguishable
faces, `GL_TEXTURE_GEN_S/T/R` enabled with `GL_REFLECTION_MAP` (or
`GL_NORMAL_MAP`), a vertex normal chosen so the generated coordinate points
at a known face, confirm the sampled color matches that face — proving
texgen-driven (not vertex-texcoord-driven) cube sampling actually happens.

**Done exactly as predicted** - confirmed the existing `GL_REFLECTION_MAP`/
`GL_NORMAL_MAP` codegen already emitted a full `vec3` (`tgR{i}`, or
`texgenNormal` directly for `NORMAL_MAP`) with no 2D-only truncation
anywhere in it, so relaxing the three gate functions
(`unit_uses_texgen`/`texgen_needs_eye`/`texgen_needs_normal`) to
`active_texture_target(...) != none` was the entire fix - zero changes
needed in the coordinate-generation body itself. `tests/gtest_texgen_cube.cc`
uses `GL_NORMAL_MAP` as the primary case (the generated coordinate is
*exactly* the transformed normal with no `reflect()` math to predict,
identity modelview making the prediction trivial: normal (1,0,0) must
land on the +X face) plus one `GL_REFLECTION_MAP` case with a
purpose-picked vertex/normal pair whose `reflect(eyeToVertex, normal)`
works out to a clean cube axis by construction.

This is also the item that surfaced §2.8's `NormalMat`-upload bug - not
specific to this item's own change (a 2D unit doing sphere-map texgen
without lighting was equally affected already), just the first time
anything exercised "texgen needs the normal, lighting is off" at all.

## 2.7 [x] `texture_metadata_cache_t` keyed by target (3D/cube level metadata)

`getter.cpp`'s `texture_metadata_cache_t` (added for `glGetTexLevelParameter*`
emulation, since ES 3.0 has no such query) keys only on `(texture name,
level)`, with no target axis — a 2D texture and, say, `GL_TEXTURE_3D` bound
to the same name would collide, and 3D/cube uploads never populate it at
all (already documented as a known gap in `glGetTexImage`'s 3D/cube case
comment from last round's §1.2). Extend the map key to `(name, target,
level)` and populate it from `glTexImage3D`/`glCompressedTexImage3D`/the
six cube-face `glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X+i, ...)` calls,
the same way the existing 2D path populates it. This is wrapper-tracked
metadata, not a backend capability question — unlike actual pixel readback
(2D's FBO+`glReadPixels` trick has no 3D/cube equivalent on GLES, and stays
refused there per last round's §1.2), *level parameters* (width, height,
internal format, compressed size) don't need to touch pixel data at all, so
GLES is not a floor gap here.

**Test**: extend `tests/gtest_getteximage_3d_cube.cc` or add a
`glGetTexLevelParameteriv` case — upload a 3D texture and a cube face at a
known size/format, query `GL_TEXTURE_WIDTH`/`GL_TEXTURE_HEIGHT`/
`GL_TEXTURE_INTERNAL_FORMAT` for both targets on GLES (where the 2D path
already works) and confirm correct answers; confirm a same-name 2D and 3D
metadata pair (name reuse after `glDeleteTextures`/`glGenTextures`, or a
target mismatch query) don't collide.

**Done, one scope discovery mid-implementation and one consequent cut.**
`glTexImage3D` turned out not to have *any* wrapper-side entry point at
all before this - not in `lookup.cpp`'s `GETPROC` table, so it was a
second, independent instance of exactly 2.1's "backend-fallthrough with
zero bookkeeping" gap, just for a different symbol. There was accordingly
no existing hook to extend for the 3D half of this item; a minimal
`glTexImage3D` wrapper was added to `getter.cpp` (the only file able to
reach `texture_metadata_cache_t`'s anonymous-namespace internals directly)
alongside the cache-key change. Deliberately much thinner than
`glTexImage2D`'s own wrapper: no BGRA conversion, no legacy-internal-
format rewriting, no `ARB_imaging` hook, no `LIST_RECORD` (matching
`glTexImage2D`, which - checked directly - does not record into display
lists either, so this introduces no new asymmetry) - straight passthrough
plus the metadata write this item needs. `GL_TEXTURE_DEPTH` needed a real
field added to `texture_level_t` (previously absent entirely, since only
2D ever populated this cache); other targets leave it at its default 1.

The cube-face half needed no new entry point - `glTexImage2D` already
receives cube-face uploads (`glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X+i,
...)` is how GL has always specified them), it just filtered them out with
an `if (target == GL_TEXTURE_2D)` guard before recording metadata; widening
that guard (and `glCopyTexImage2D`'s matching one, which can also target a
cube face) was enough. All six faces collapse onto one shared
`GL_TEXTURE_CUBE_MAP` cache entry per texture+level rather than being kept
separate per face - cube completeness already requires every face at a
level to share size/format, so there is nothing a per-face key would ever
distinguish.

**Cut**: `glCompressedTexImage3D` stayed unwrapped (compressed 3D textures
are a rare combination even among the already-rare 3D-texture-upload
cases this item targets); `glGetTexLevelParameteriv` on a texture uploaded
only through it correctly answers `GL_INVALID_OPERATION` (cache miss,
same "refuse rather than guess" behavior the rest of this cache already
uses for anything it was never told about) rather than crashing or
inventing a value - a real, smaller, separately trackable gap, not a
silent one.

**Test**: extended `tests/gtest_getteximage_3d_cube.cc` with
`LevelParametersAnswerThreeDAndCubeOnGles` - a 3D texture's
width/height/depth/internal-format all answered correctly on GLES
(previously impossible, since GLES has no native level-parameter query at
all and the cache never had an entry to answer from); a cube face
(deliberately not `+X`, proving the shared-entry-per-level design is
real, not an accidental single-face match) answering correctly too; and a
same-context 3D-vs-cube-map cross-check proving one query does not
clobber the other's cached entry.

---

## §2.8 Bugs found by the wrap-up verification pass

Building all seven items and running their new tests together (after all
seven were written, per the same "write everything, verify at the end"
discipline as last round) surfaced two more pre-existing bugs, fixed on
the spot, plus one test-authoring mistake worth recording since it looked
like an implementation bug at first:

- **`glReadBuffer`'s `GL_FRONT_AND_BACK` handling used the wrong error.**
  The first cut computed `legacy = isLegacyDrawBuffer(src) && src !=
  GL_FRONT_AND_BACK`, meaning `GL_FRONT_AND_BACK` fell through to the
  generic "not a selector GL recognizes" branch and raised
  `GL_INVALID_ENUM` - wrong, since it *is* a selector GL recognizes, just
  an illegal one to read from (`GL_INVALID_OPERATION`, per the spec text
  quoted in §2.1 itself). Fixed with an explicit `if (src ==
  GL_FRONT_AND_BACK)` check ahead of the general classification, rather
  than trying to fold "recognized but illegal" into the same boolean as
  "not recognized at all."
- **`NormalMat` was never uploaded unless lighting was also on.**
  `send_uniforms` (glstate.cpp) only sent a value to the `NormalMat`
  uniform location inside its `if (fpe_state.fpe_bools.lighting_enable)`
  block - but `add_vs_uniforms` declares (and `add_vs_body` consumes)
  that same uniform whenever `!lighting_enable && texgen_needs_normal(state)`
  is true too (a `SPHERE_MAP`/`NORMAL_MAP`/`REFLECTION_MAP` texgen unit
  with lighting off). Nothing before this round ever exercised that
  specific combination - the 2.6 texgen fix newly makes it reachable for
  cube units, but the bug is not specific to cube/3D at all; a 2D unit
  doing sphere-map texgen without lighting was equally affected already.
  Symptom: `TexgenCubeTest.NormalMapSamplesTheFaceMatchingTheVertexNormal`
  sampled the same face regardless of which direction the test's vertex
  normal pointed, because the shader's `NormalMat` uniform was reading
  back whatever a freshly-linked, never-written uniform defaults to
  (driver-dependent, typically all-zero), collapsing every normal to a
  degenerate vector. Fixed by moving the `NormalMat` upload out of the
  `lighting_enable` block entirely, gated only on `locations.normal >= 0`
  (a harmless no-op when the shader never declared the uniform at all,
  same as every other per-uniform `locations.X >= 0` guard in this
  function already works).
- **Not a bug**: `PointSpriteTest.FadeThresholdDimsPointsBelowThresholdSize`
  first failed because its `SetUp`-inherited identity projection clips
  anything past eye z = -1 out of the [-1,1] NDC range, and the fade test
  needs eye-space distances out to 6.0 to shrink a point below the fade
  threshold - the point was being clipped away entirely, not fading.
  Fixed by giving that one test its own local `glOrtho` (an orthographic
  projection leaves NDC.xy independent of z, so the window-centre sampling
  every other test in the file relies on still holds). Also not a bug:
  `DrawPixelsStencilTest.DistinctIndicesLandAtTheirOwnPixelsNotJustOneSharedValue`'s
  original 2x2-image form sampled two of its four texel centres exactly on
  the shared diagonal seam between the quad drawer's two triangles
  (UV (0.75,0.25) and (0.25,0.75), both summing to the boundary value 1.0)
  - a rasterizer boundary-rounding case for that specific coincidence of
  image size and sample placement, not a bit-plane logic bug (confirmed:
  the same four values written via four independent single-pixel draws,
  which cannot land on that seam, all read back correctly - that
  redesign is what the test now does). And
  `DrawPixelsStencilTest.PixelMapRemapsTheIndexBeforeItReachesTheBuffer`'s
  first failure was a test bug, not implementation: it called
  `glEnable(GL_MAP_STENCIL)`, but `GL_MAP_STENCIL` is a `glPixelTransfer`
  parameter, not a capability with an enable bit - fixed to call
  `glPixelTransferi(GL_MAP_STENCIL, 1)` instead.

Full relevant-suite result: all 20 new/extended tests across the seven
items pass.
