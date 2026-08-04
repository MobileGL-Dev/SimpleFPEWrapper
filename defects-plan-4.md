# defects-plan-4.md — fourth audit pass

Source: a 7-item Chinese-language audit list, presented directly in
conversation. Same verification discipline as `defects-plan.md`'s §0 and
every round since: every claim checked against current source before
recording anything here.

## Verification verdict

All 7 items are true against current source. All 7 are either already-
documented permanent/intentional boundaries or explicitly-scoped-at-
implementation-time deliberate gaps — none are undiscovered defects or
regressions. **Nothing was implemented this round**: this file is a
recording pass only, for the items below that had no entry anywhere in
the `defects-plan*.md` trail yet (4.1, 4.4's `glGetCompressedTexImage`
half, 4.5/4.6, 4.7a). The items that already had a record are
cross-referenced rather than duplicated.

---

## 4.1 `glLogicOp` has no rendering effect — recorded here for the first time

Real, validated, display-list-recorded state (`state.cpp:544-552`), but
`GL_COLOR_LOGIC_OP`/`GL_INDEX_LOGIC_OP` are hardcoded `capability(false)`
at `getter.cpp:1673-1699`, with the comment explaining why: GL ES 3.0 core
removed fixed-function logic ops with no extension-free way for a
fragment shader to read the framebuffer's current color to emulate one,
so — per this project's standing "no feature only one backend floor can
execute" rule — it stays honest state-only rather than a fake no-op
effect. Same code block and same rationale as 4.3 below. Previously only
findable via that `getter.cpp` comment and commit `dea92e1`'s message
("wire up ... glLogicOp"); this is its first appearance in the audit
trail. Not actionable — no fix exists within this project's backend-floor
constraints.

## 4.2 Color-index mode — already recorded

`getter.cpp:2015`, `GL_INDEX_MODE` hardcoded `scalar(0)`. Already
documented three times: `defects-plan.md` §2, `defects-plan-2.md`'s
boundary note (§2), `defects-plan-3.md` §3.3. Reconfirmed unchanged, no
new content to add.

## 4.3 `GL_LINE_SMOOTH` / `GL_POLYGON_SMOOTH` / `GL_POLYGON_OFFSET_LINE` / `GL_POLYGON_OFFSET_POINT` — already recorded

`getter.cpp:1673-1699`, same hardcoded-`false` block as 4.1. Matches
`defects-plan-2.md`'s "Not changing" section verbatim (line-smoothing and
polygon-smoothing need per-primitive geometry processing neither backend
floor's core profile supports; the two polygon-offset variants need
per-fragment depth-slope data `sfpewDrawMixedPolygonMode`'s wireframe
expansion doesn't carry through). Reconfirmed unchanged.

## 4.4 GLES `glGetTexImage` / `glGetCompressedTexImage` readback

`glGetTexImage`'s GLES readback refusal (for 3D/cube specifically) is
already recorded in `defects-plan-2.md`. **`glGetCompressedTexImage` was
never named in the trail before — recorded here for the first time.**
`getter.cpp:1401-1435`: explicit target/level validation, then the same
"GLES has no texture readback at all; fail loudly, desktop GL forwards"
handling `glGetTexImage` uses (`getter.cpp:2844-2871`) — clean
`GL_INVALID_OPERATION` on GLES, forwarded to the backend on desktop. Not
a worse-handled or distinct gap, just the same permanent GLES limitation
applied consistently to its compressed sibling. Not actionable.

## 4.5 / 4.6 `GL_LINE_STIPPLE` immediate-mode-only, and no lighting/texture/fog — recorded here for the first time

Both real, both explicitly scoped at the point of implementation, not
later-discovered debt.

`linestipple.cpp`'s file header states the scope directly: only
`glBegin`/`glEnd(GL_LINES`/`GL_LINE_STRIP`/`GL_LINE_LOOP)` is covered, via
`sfpewDrawStippledLines`. Confirmed by tracing every call site: the only
caller is `drawing1x.cpp:342`, inside `drawImmediateVertices`, gated on
`line_stipple_enable` and a `GL_LINES`/strip/loop primitive.
`drawing.cpp` — the `glDrawArrays`/`glDrawElements` FPE-conversion path —
has zero references to line stipple at all; an array/element draw with
`GL_LINE_STIPPLE` enabled renders solid, unchanged. Lighting, texturing,
and fog are likewise not applied: `sfpewDrawStippledLines` runs a
dedicated shader outside the FPE uber-shader entirely (same family as
`pixelops.cpp`'s quad drawer), with no lighting uniforms, no sampler
declarations, no fog math in it — `linestipple.cpp`'s own comment states
the reasoning: real `GL_LINE_STIPPLE` usage is "overwhelmingly simple
flat-colored debug/UI lines," and folding stipple into the uber-shader
generator to cover the rare lit/textured stippled line was judged to cost
more risk than the case is worth.

Commit `dea92e1`'s own message confirms this was the plan from the start,
not a bug found later: "Deliberately does not cover vertex-array-sourced
lines, lit/textured stippled lines, or composition with `GL_LINE` polygon
mode — documented gaps, not silent ones."

**If ever revisited**: both share one root cause (stipple bypasses the
uber-shader by design), so closing either means real shadergen + draw-path
work, not a small patch — fold stipple distance into `fpe_shadergen.cpp`
as a new per-fragment varying (the uber-shader already has lighting/
texture/fog to compose with), and add a CPU distance-accumulation
pre-pass over the array/index buffer into `drawing.cpp`'s
`glDrawArrays`/`glDrawElements` path, analogous to what
`drawImmediateVertices` already does for immediate mode.

## 4.7a `glDrawPixels(GL_COLOR_INDEX, ...)` — recorded here for the first time

`describePixelFormat` (`pixelops.cpp:880-911`) has no `GL_COLOR_INDEX`
case, falling to `default: return false;`; `glDrawPixels`
(`pixelops.cpp:1373-1377`) turns that into a clean `GL_INVALID_ENUM`, not
a crash or silent mishandling. Same root policy as 4.2's project-wide
color-index-mode boundary — this is its first appearance as its own named
`glDrawPixels`-specific entry, but no new decision: not actionable for
the same reason 4.2 isn't.

## 4.7b `glDrawPixels(GL_STENCIL_INDEX, GL_BITMAP, ...)` — already recorded

`defects-plan-2.md` §2.5: "`GL_BITMAP` as a stencil source type is a
further, documented scope cut." Reconfirmed unchanged: `readPixelStencilIndex`
(`pixelops.cpp:1069-1092`) only accepts the six integer scalar types;
`drawStencilPixels` (`pixelops.cpp:1305-1318`) rejects `GL_BITMAP` (and
`GL_FLOAT`/`GL_HALF_FLOAT`) with `GL_INVALID_ENUM` before touching
anything.

**Noted for the first time**: unlike 4.5/4.6, this one has a concrete,
low-risk path if ever prioritized. `glBitmap()` itself
(`pixelops.cpp:835-865`) already contains a complete, working 1-bit-packed
unpack loop (row bytes `ceil(width/8)`, MSB/LSB-first aware via
`pixel_store_unpack_lsb_first`) that could feed the existing
`drawStencilBitplanes` 16-pass writer directly — extracting and
generalizing that loop, then adding a `GL_BITMAP` case to
`readPixelStencilIndex`'s type switch, is a moderate, not from-scratch,
effort. Still not implemented this round; recorded as the cheapest of
the two real gaps in this list, should it ever be worth doing.
