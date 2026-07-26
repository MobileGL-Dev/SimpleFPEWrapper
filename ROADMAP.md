# SimpleFPEWrapper Roadmap

## Product Boundary

SimpleFPEWrapper targets a practical OpenGL 1.x fixed-function subset on an
OpenGL ES 3.x / programmable OpenGL backend. The first release goal is
correct rendering for a clearly documented subset, not a nominally complete
OpenGL 1.x implementation.

The project should treat API compatibility as a contract with three parts:

1. The symbol is exported or returned by `eglGetProcAddress`.
2. The call has the documented fixed-function behavior.
3. Unsupported calls fail predictably through the GL error path rather than
   silently doing nothing, forwarding to an incompatible backend entry point,
   or crashing.

The existing `fpe_implementation_progress.yaml` is useful as a feature
inventory. Release readiness must additionally require successful builds,
automated tests, and runtime validation.

## Guiding Decisions

- Support one explicit backend profile first: OpenGL ES 3.0 or newer.
- Initialize backend function tables and FPE resources only after a valid
  context is current. Remove process-load-time GL work.
- Keep FPE state per GL/EGL context. Define creation, context switch, and
  teardown behavior before adding more stateful features.
- Preserve caller-visible programmable-pipeline state around every wrapped
  draw: program, VAO, array/element buffers, active texture, and enabled
  vertex attributes.
- Prefer a small, tested compatibility subset over exporting declarations that
  have no implementation.

## Phase 0: Build And Safety Baseline

**Goal:** make `main` buildable and prevent basic wrapper crashes.

- Fix all syntax errors in `fpe/state.cpp`, including empty `default:` labels.
- Enable warnings for project code; remove the global `-w` suppression and
  address or explicitly scope remaining warnings.
- Add a CTest target that performs a clean configure, build, and a smoke test.
- Replace static initialization with an explicit context-aware initialization
  API or lazy initialization after context validation.
- Validate every backend function pointer required by a code path before use.
- Make initialization idempotent and release FPE-owned GL objects on context
  teardown.
- Add guards for empty matrix stacks, invalid enums, null pointer arguments,
  and invalid light/texture indices.

**Exit criteria:** Debug and Release builds succeed with GCC and Clang; CTest
has at least one executable test; loading the library without a current GL
context does not issue GL calls or terminate the process.

## Phase 1: Draw Interception And State Isolation

**Goal:** make the existing core safe to use beside programmable rendering.

- Route only recognized legacy draw paths into FPE conversion. Pass through
  modern attribute-based draws unchanged.
- Fix vertex-array normalization for zero enabled legacy arrays and all legal
  attribute scalar types.
- Preserve and restore program 0, VAO, array buffer, element buffer, active
  texture, and vertex-attribute enable state after each wrapped draw.
- Add `glDrawElements` support and define indexed `GL_QUADS` conversion.
- Handle shader compile/link failure as a failed draw with a GL error; never
  call `glUseProgram` with an invalid program.
- Add tests for client-memory arrays, VBO arrays, no-array passthrough,
  indexed draws, `GL_QUADS`, and state restoration.

**Exit criteria:** a mixed programmable/FPE integration test renders two
consecutive passes correctly, and state snapshots match before and after each
wrapped draw.

## Phase 2: Minimum Viable Fixed-Function Subset

**Goal:** define and finish the subset needed by common legacy geometry.

- Complete matrix behavior: load matrix, frustum/perspective helper coverage,
  stack overflow/underflow errors, and query behavior.
- Complete vertex, normal, color, and texture-coordinate scalar/vector
  variants through shared conversion helpers.
- Ensure `eglGetProcAddress` and exported symbols match the implemented API
  manifest.
- Finish client-state tracking for all supported texture coordinate units.
- Validate immediate mode and array mode against the same reference scenes.

**Exit criteria:** every symbol advertised in the v0.1 API manifest has unit
coverage, a `eglGetProcAddress` test, and a rendered reference image.

## Phase 3: Texturing And Fragment Operations

**Goal:** make common textured fixed-function scenes render predictably.

- Track enable state independently for each texture unit.
- Implement texture environment modes in priority order: `GL_MODULATE`,
  `GL_REPLACE`, `GL_DECAL`, `GL_ADD`, then combine modes if needed.
- Remove the shader generator's texture-unit-zero restriction and upload all
  sampler bindings used by the generated program.
- Finish fog and alpha-test validation across textured and untextured draws.
- Add texture-coordinate generation only after explicit texture environment
  behavior is stable.

**Exit criteria:** two-texture compositing, texture disable/enable transitions,
fog, and alpha test pass image-based integration tests.

## Phase 4: Lighting And Materials

**Goal:** support the conventional fixed-function lighting workflow.

- Track per-light enable state, material state, color material, and normal
  handling correctly.
- Generate vertex or fragment lighting code for ambient, diffuse, specular,
  attenuation, spot lights, two-sided lighting, and shading model behavior.
- Define coordinate-space semantics for light positions and spot directions at
  call time.
- Add material, light, and state query APIs for the supported subset.

**Exit criteria:** reference scenes cover directional and positional lights,
material/specular response, color material, flat/smooth shading, and disabled
lights.

## Phase 5: Display Lists And Compatibility Completion

**Goal:** make supported legacy commands compose correctly.

- Record every supported state-changing command, including client active
  texture and draw calls, with safe pointer ownership.
- Define nested list, recursion, compile versus compile-and-execute, and
  deletion semantics.
- Implement low-cost helpers such as `glRect*`; defer RasterPos and pixel
  operations unless a concrete consumer requires them.
- Publish unsupported categories explicitly: evaluators, feedback/selection,
  accumulation, color-index mode, and legacy pixel-transfer paths.

**Exit criteria:** display-list replay produces the same state and image as
immediate execution for every supported command.

## Quality Gates

- Run CTest on every change; keep a small headless EGL renderer for integration
  tests.
- Run AddressSanitizer and UndefinedBehaviorSanitizer for CPU-side tests.
- Maintain a generated API manifest with statuses: exported, implemented,
  tested, deferred, or unsupported.
- Keep shader source and compiler logs available in test failures.
- Test at least Mesa software rendering and one target mobile GLES driver.
- Require documentation updates when a feature moves between supported and
  unsupported states.

## Deferred Until A Concrete Need

Do not schedule evaluators, feedback/selection, accumulation buffers,
color-index rendering, bitmap/pixel-transfer emulation, or full texture
coordinate generation for the first compatible release. These features have a
large implementation cost and do not unblock the core legacy geometry,
texturing, and lighting workflow.
