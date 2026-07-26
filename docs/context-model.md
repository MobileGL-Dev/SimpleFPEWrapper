# SimpleFPEWrapper Context & Threading Model

Status: implemented as of plans/07 (S7). This document is the contract;
deviations are bugs.

## How the current context is tracked

The wrapper cannot intercept `eglMakeCurrent`: applications resolve EGL
entry points from libEGL directly, and only GL symbols route through
`eglGetProcAddress`/the exported GL surface. All context awareness is
therefore **lazy reconciliation**: every context-sensitive access calls
`eglGetCurrentContext()` (a TLS read on all supported platforms) and
compares it against a thread-local cache.

Reconciliation points:

- `glstate_t::get_instance()` — the per-context FPE state aggregate.
  Thread-local one-entry cache in front of a mutex-guarded
  `unordered_map<EGLContext, unique_ptr<glstate_t>>`. Calls made with no
  current context land on a dedicated `no_context_state` instance so no
  entry point crashes; that instance's state is throwaway by design
  (desktop GL leaves GL calls without a context undefined).
- Logical shadows (current program, array-buffer binding, texture
  bindings/active texture, extension caches) — thread-local structs that
  reset when the observed context changes.
- The immediate-mode glyph batcher — pending batches are stamped with the
  collecting context; a flush observed on a different context **drops**
  the batch (vertex/texture names would be meaningless) and logs.
- The display-list vertex arena — rebuilt when the context changes.

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
