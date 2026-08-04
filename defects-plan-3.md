# defects-plan-3.md — third audit pass

Source: a 3-item audit of "明确存在的 GL 2.1 语义缺口" (clearly-existing GL 2.1
semantic gaps), presented directly in conversation rather than a file. Same
verification discipline as `defects-plan.md`'s §0 and `defects-plan-2.md`:
every claim checked against source before acting.

## Verification verdict

All 3 items confirmed accurate by direct code read. Items 1 and 2 are real,
fixable defects; item 3 was correctly identified by the audit itself as an
already-documented, intentional boundary rather than an oversight - the
audit's own wording flagged this ("颜色索引模式本身属于已知限制" /
"color-index mode itself is a known limitation").

## Scope decisions for this round

**Implementing:**
- 3.1 `glCopyPixels` — went straight to `glBlitFramebuffer`, applying none
  of GL_*_SCALE/BIAS/GL_MAP_COLOR/GL_MAP_STENCIL
- 3.2 `glReadPixels`'s ordinary (non-`ARB_imaging`) path — forwarded
  straight to the backend, same gap

**Not changing (already correct, verified):**
- 3.3 Color-index mode entirely unsimulated (`getter.cpp`'s `GL_INDEX_MODE`
  hardcoded `GL_FALSE`) — the audit itself identified this as the
  project's documented, intentional RGBA-only boundary, not a missed
  implementation. Confirmed unchanged from `defects-plan.md`'s and
  `defects-plan-2.md`'s own boundary notes on the same subject.

---

## 3.1/3.2 [x] `glCopyPixels` and `glReadPixels` basic pixel transfer

**The actual gap, more complete than the audit's framing.** GL 2.1 3.6.3
defines pixel transfer as two tiers: a *basic* tier every implementation
runs (RGBA/depth scale+bias, then `GL_MAP_COLOR`/`GL_MAP_STENCIL` table
lookups) and, only for implementations advertising `GL_ARB_imaging`, a
further tier of color tables/convolution/histogram/min-max. `glDrawPixels`
(the write direction, `pixelops.cpp`) already runs the basic tier
unconditionally in its own per-pixel decode loop. The read direction had
*none* of it:

- `glCopyPixels(GL_COLOR)` only ran `sfpewImagingActive()`'s ARB_imaging
  stages (color table/convolution/histogram/minmax), and — checked
  directly — `sfpewImagingTransfer` itself starts at color-table lookup;
  it never ran the basic scale/bias/map step even when ARB_imaging state
  was active. `glCopyPixels(GL_DEPTH)` and `glCopyPixels(GL_STENCIL)` went
  straight to `glBlitFramebuffer` with no transfer at all.
- `glReadPixels`'s ordinary path (`ordered_passthrough.cpp`) forwarded
  straight to the backend after only checking `sfpewImagingReadPixels`
  (same ARB_imaging-only, same missing basic step).

So an app that set `GL_RED_SCALE` or `GL_DEPTH_BIAS` without ever touching
a color table/convolution/histogram got it silently ignored on every
readback path — a strictly worse gap than "skipped when ARB_imaging is
inactive," since the basic step was absent from the pipeline full stop,
regardless of ARB_imaging state.

**Design constraint that ruled out the simplest fix.** Broadening
`sfpewImagingActive()`'s gate to also cover the basic tier (the obvious
one-line fix) would make `glDrawPixels` double-apply the basic transfer
whenever ARB_imaging also happens to be active, since it already applies
it unconditionally in its own decode loop before any imaging stage runs.
Fixed instead by adding new, additive functions used only by the two
requested entry points, leaving `sfpewImagingActive`/`sfpewImagingTransfer`/
`sfpewImagingReadRgba`/`sfpewImagingReadPixels`/`sfpewImagingDecodePixels`/
`sfpewPrepareImagingUpload` — and every `glTexImage2D`/`glCopyTexImage2D`/
`glCopyTexSubImage2D` caller of them — completely untouched.

**Fix**, in `imaging.cpp`/`imaging.h` (color reuses this file's existing
encode/decode/`readFramebuffer` machinery; depth and stencil get their own
minimal handling since GLES/desktop have no shared "arbitrary pixel
format" encoder for those the way color tables do):

- `sfpewFullColorReadRgba`/`sfpewFullColorReadPixels` — the full GL 2.1
  pipeline in spec order: basic scale/bias/map first, then (unchanged)
  whatever ARB_imaging stages are active. Supersedes
  `sfpewImagingReadPixels` for `glReadPixels`/`glCopyPixels(GL_COLOR)`
  specifically; `sfpewImagingReadRgba`/`ReadPixels` themselves stay in
  place backing `glCopyTexImage2D`/`glCopyTexSubImage2D`'s own hook,
  which this fix does not extend (out of scope: only `glCopyPixels` and
  `glReadPixels` were requested).
- `sfpewReadTransformedDepth`/`sfpewDepthPixelTransferReadPixels` —
  `GL_DEPTH_SCALE`/`GL_DEPTH_BIAS` applied to a raw depth readback.
  `glCopyPixels(GL_DEPTH)` feeds the transformed buffer into the existing
  depth quad-drawer (`drawQuad(..., quad_mode_t::depth, ...)`) instead of
  a blit; `glReadPixels` encodes it into the caller's requested type.
- `glCopyPixels(GL_STENCIL)` gets a new `GL_MAP_STENCIL`-aware path
  (`pixelops.cpp`) reusing `defects-plan-2.md`'s §2.5
  `drawStencilBitplanes` bit-plane writer and the established
  `GL_PIXEL_MAP_S_TO_S` raw-index-lookup convention
  (`index = map[index & (map.size() - 1)]`, same as `drawStencilPixels`).

Every new path reads fully into a CPU buffer *before* writing anything to
the destination — unlike the blit path, this makes all three inherently
safe when source and destination overlap in the same framebuffer, with no
separate scratch-FBO copy needed (a side benefit, not something
separately requested).

Each new path is gated on an `*Active()` check (`sfpewBasicColorTransferActive`,
`sfpewDepthPixelTransferActive`, a `pixelops.cpp`-local
`sfpewStencilPixelTransferActive`) so a caller sitting at the default
(no-op) scale/bias/map values — the overwhelming common case — keeps
using the exact same fast backend/blit path as before; only a caller that
actually set a non-default transfer pays for the CPU round trip.

`GL_INDEX_SHIFT`/`GL_INDEX_OFFSET` stay unapplied everywhere in this fix,
matching the same project-wide color-index-mode boundary
`defects-plan-2.md`'s §2.5 already inherited from `state.cpp`'s
`glPixelTransfer`.

**Two more bugs found and fixed while implementing this, before any test
was written against them:**

- `sfpewDepthPixelTransferReadPixels` initially gated only on `type`
  (`GL_UNSIGNED_BYTE`/`GL_FLOAT`/etc.), not `format`. Since those same
  type enums are equally legal for `GL_RGBA` reads, a `glReadPixels
  (GL_RGBA, GL_UNSIGNED_BYTE, ...)` call made while `GL_DEPTH_SCALE`/
  `GL_DEPTH_BIAS` happened to be non-default (set earlier, for an
  unrelated depth read elsewhere) would have been silently hijacked into
  writing reinterpreted depth data over the caller's requested color
  output — a correctness regression across every format, not specific to
  depth. Fixed by adding an explicit `format != GL_DEPTH_COMPONENT` gate
  inside the function itself, not left to caller discipline. Covered by
  `PixelTransferReadbackTest.ReadPixelsIgnoresUnrelatedDepthTransfer`.
- The new raw backend depth/stencil reads (`sfpewReadTransformedDepth` in
  `imaging.cpp`, `readStencilIndices` in `pixelops.cpp`) never drained the
  backend's own error state after their `g_glFuncs.glReadPixels(...)`
  call. This project's own dev/test driver rejects a raw
  `GL_DEPTH_COMPONENT` read outright regardless of framebuffer (already
  documented in `gtest_copypixels_depth.cc`'s `DepthAt()` comment) — left
  undrained, that stray backend `GL_INVALID_OPERATION` leaked into the
  *caller's* next `glGetError()`, exactly the failure mode `imaging.cpp`'s
  existing `drainBackendErrors()` (and `pixelops.cpp`'s existing
  `drainFailedMapError()`) already exist to prevent elsewhere in these
  same files. Fixed by calling the appropriate drain helper right after
  each raw call. Caught by `CopyPixelsDepthTest.DepthCopyAppliesBias`
  failing with a leaked `GL_INVALID_OPERATION` before this fix.

**Test.** `tests/gtest_pixel_transfer_readback.cc` (color, both entry
points): default transfer leaves `glReadPixels` byte-for-byte unchanged
(regression guard); `GL_RED_SCALE`/`GL_RED_BIAS` transform one channel and
leave the others alone; `GL_MAP_COLOR` applies after scale/bias through a
real table; a `glReadPixels(GL_RGBA)` call is unaffected by an unrelated
`GL_DEPTH_SCALE`/`GL_DEPTH_BIAS` (the format-gating regression above);
`glCopyPixels(GL_COLOR)` applies scale/bias and the default (untransferred)
path stays raw. `tests/gtest_copypixels_depth.cc` (depth/stencil, next to
its existing FBO/probe-quad fixture): `glCopyPixels(GL_DEPTH)` +
`GL_DEPTH_BIAS`, checked indirectly via the same depth-tested probe-quad
technique the file's pre-existing depth test already established (this
project's dev driver rejects a direct `GL_DEPTH_COMPONENT` readback
regardless of framebuffer, so a precise-value assertion isn't available in
this environment either way — the probe distinguishes "bias applied" from
"bias silently skipped," which is the actual regression risk);
`glCopyPixels(GL_STENCIL)` + `GL_MAP_STENCIL`, with the default (map
disabled) path checked in the same test to prove the fast blit stays
untouched.

**One pre-existing test updated, not a regression in the fix.**
`gtest_pixel_maps.cc`'s `MapColorTransformsDrawPixelsAfterScaleAndBias`
left `GL_MAP_COLOR` enabled through its own verification `glReadPixels`
call. Before this fix that was silently ignored on read, so the test's
expected values were implicitly "what `glDrawPixels` wrote, mapped once."
Per GL 2.1 4.3.1, `glReadPixels` must apply the same transfer as any other
pixel path — with the gap fixed, the test's own readback now runs the
already-mapped stored pixel through the table a *second* time, which is
correct per spec but not what the test was checking. Fixed by disabling
`GL_MAP_COLOR` before the verification read, isolating what the test
name says it's about (what `glDrawPixels` wrote) from the read-side
transfer this round adds.

Full relevant-suite result: all tests across
`gtest_pixel_transfer_readback.cc`, `gtest_copypixels_depth.cc`,
`gtest_pixel_maps.cc`, `gtest_drawpixels_formats.cc`,
`gtest_drawpixels_stencil.cc`, `gtest_imaging_*.cc`, and `gtest_read_buffer.cc`
pass together.
