# SimpleFPEWrapper Context & Threading Model

Status: implemented as of plans/07 (S7). This document is the contract;
deviations are bugs.

## How the current context is tracked

The wrapper cannot intercept `eglMakeCurrent`: applications resolve EGL
entry points from libEGL directly, and only GL symbols route through
`eglGetProcAddress`/the exported GL surface. All context awareness is
therefore **lazy reconciliation** — but reconciliation is *per exported
entry point*, not per access: `eglGetCurrentContext()` is NOT a cheap TLS
read everywhere (on glvnd desktops one call costs ~425ns: a getpid-based
fork check plus a dispatch mutex), so hammering it per state access made
the wrapper CPU-bound.

The resolve discipline (`fpe/types.h`, `fpe/fpe.cpp`):

- `glstate_t::get_instance()` — **strict resolve**. Calls
  `eglGetCurrentContext()`, refreshes the thread-local snapshot
  (context pointer + `glstate_t*`), and returns the per-context state.
  Every context-sensitive exported entry point performs **exactly one**
  strict resolve as its first context access (either its first `g_glstate`
  use or an explicit `(void)g_glstate;` anchor).
- `glstate_t::current()` (`g_glstate_c`) — **relaxed accessor**. Returns
  the snapshot with a TLS read. Correct anywhere downstream of the entry's
  strict resolve, because the current context cannot change in the middle
  of a single GL call on the calling thread.
- `glstate_t::current_vertex_data()` — **Begin/End pin** for the
  vertex-data entries (`glVertex*`, `glColor*`, `glTexCoord*`,
  `glNormal*`, `glMultiTexCoord*`). While a batch is collecting
  (`fpe_draw.primitive != GL_NONE`) it returns the snapshot without any
  strict resolve: a context switch mid-Begin/End is undefined behavior
  that we define as "the batch belongs to the Begin context". Outside a
  batch it degrades to a strict resolve.
- `glstate_t::cached_context()` — the EGLContext observed by the last
  strict resolve on this thread. Logical shadows reconcile against it
  instead of calling `eglGetCurrentContext()` themselves; their freshness
  is provided by the calling entry's strict resolve.

Reconciliation points:

- `glstate_t::get_instance()` — the per-context FPE state aggregate.
  Thread-local snapshot in front of a mutex-guarded
  `unordered_map<EGLContext, unique_ptr<glstate_t>>`. Calls made with no
  current context land on a dedicated `no_context_state` instance so no
  entry point crashes; that instance's state is throwaway by design
  (desktop GL leaves GL calls without a context undefined).
- Logical shadows (current program, array-buffer binding, texture
  bindings/active texture, extension caches) — thread-local structs that
  reset when the *snapshot* context changes (see `cached_context()`
  above). Cold-path caches (evaluators, pixel-path quad drawer, accum
  state, translator target detection) still strict-resolve on their own;
  they are not on any hot path.
- The immediate-mode glyph batcher — pending batches are stamped with the
  collecting context; `flushPendingImmediateDraws()` performs its own
  strict resolve when a batch is pending (it is reached from passthrough
  entries that never resolve), and a flush observed on a different
  context **drops** the batch (vertex/texture names would be meaningless)
  and logs. With no batch pending the flush is a TLS read and returns.
- The display-list vertex arena — rebuilt when the context changes,
  compared against the snapshot (allocation/release run under anchored
  entries; captured-command destruction strict-resolves itself).

The current-program shadow additionally re-reads `GL_CURRENT_PROGRAM`
every 256 queries, so callers that bypass the `glUseProgram` wrapper
(JNI direct dispatch, layered wrappers) cannot permanently desynchronize
the FPE interception decision.

## State ownership

| State | Scope |
|---|---|
| Matrix stacks, lights, materials, texenv/texgen, fog/alpha/clip planes, raster pos | per context (`glstate_t`) |
| FPE-owned GL objects (fpe_vao/vbo/ibo, immediate ring, program cache) | per context (`glstate_t`), created lazily on first draw in that context |
| Attribute stacks (`glPushAttrib`/client) | per context |
| Client array declarations + captured `GL_ARRAY_BUFFER` bindings | per context |
| Wrapper GL error slot | per context |
| Display-list **definitions** (`DisplayListManager`) | process-global (see limits) |
| Backend function tables (`g_glFuncs`/`g_eglFuncs`) | process-global (same driver for all contexts) |

## Threading contract

Same as GL itself: a context may be current on at most one thread at a
time, and the wrapper adds no locking to protect a single context against
concurrent misuse. Cross-thread safety exists exactly where cross-thread
sharing exists: the per-context instance map is mutex-guarded, and each
thread's shadows are thread-local.

## Known limits (accepted, documented)

1. **Context destruction is unobservable.** `eglDestroyContext` does not
   pass through the wrapper, so the CPU-side `glstate_t` for a dead
   context persists for the process lifetime. The GL objects it names die
   with the context; the leak is bounded by the number of contexts a
   process ever creates (1–2 for Minecraft-era launchers).
2. **Share groups are invisible.** `eglCreateContext`'s share_context
   argument never reaches us, so display-list definitions stay
   process-global. This matches how launcher-era apps actually use shared
   contexts (record on one thread, replay on the render thread) but means
   two deliberately un-shared contexts would still see each other's list
   IDs. List *replay* always uses the current context's FPE state and
   GL objects, so rendering stays correct.
3. **No-context calls are no-ops with throwaway state.** They cannot
   crash, but nothing done without a current context transfers into any
   real context later.
4. **Context switches are observed at entry granularity.** A context
   switch performed between two GL calls is observed by the next
   context-sensitive entry's strict resolve — never mid-call. Vertex-data
   calls inside a Begin/End batch deliberately do not observe switches at
   all (the batch is pinned to the Begin context; glEnd re-observes and
   routes a switched-away batch into the unmatched-glEnd error path).
   The first strict resolve that observes the switch also clears any
   Begin/End batch left open on the outgoing snapshot (the batch was
   abandoned mid-collection; dropping it keeps the vertex-data pin from
   routing later calls made on other contexts into stale state). The
   evaluator entries (glMap*, glEvalCoord*, glEvalPoint*, glEvalMesh*)
   are pin-exempt: they anchor strictly at entry so the evaluator cache
   and the vertex sink always agree on one context.
5. **`SFPEW_RELAXED_CONTEXT=1` (opt-in).** The app promises each thread
   uses at most one EGL context for the process lifetime (true for
   Minecraft-era launchers). Strict resolves then trust the snapshot after
   a thread's first successful resolve and skip `eglGetCurrentContext()`
   entirely — on glvnd desktops that call costs ~425ns (getpid fork check
   plus a dispatch mutex), which otherwise dominates high-frequency
   entries (matrix ops ~450ns → ~25ns/call, getters ~430ns → ~8ns).
   With the promise violated, context switches go unobserved and state
   lands on the wrong context. Default: off.
