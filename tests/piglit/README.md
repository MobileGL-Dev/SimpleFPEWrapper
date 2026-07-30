# Vendored piglit shader_test subset

These 2856 `.shader_test` files are copied verbatim from the
[piglit](https://gitlab.freedesktop.org/mesa/piglit) test suite,
selected for compatibility with the command subset implemented by
`tests/piglit_runner.c` (draw rect [tex|ortho], point/rect/all/relative
probes, uniforms including all matNxM shapes, ortho, clear,
link success/error, texture rgbw/checkerboard/miptree, texparameter
min/mag, GL_MAX_VARYING_COMPONENTS requirements). Sources:

- `tests/spec/glsl-1.10/**` and `tests/spec/glsl-1.20/**` (execution,
  linker, preprocessor, compiler subdirectories);
- `tests/shaders/` (the generic shader_test pool);
- `generated_tests/spec/glsl-1.{10,20}/execution/` output of piglit's
  generators (`gen_builtin_uniform_tests.py`,
  `gen_variable_index_{read,write}_tests.py`,
  `gen_interpolation_tests.py`) - built-in function and
  variable-indexing coverage across both stages.

File names encode their origin directory relative to the GLSL spec root,
with the `execution/` level elided for brevity
(`glsl-1.10__builtins_...`, `glsl-1.20__linker__...`,
`glsl-1.10__built-in-functions_...`, `shaders__ssa_...`).

piglit is distributed under an MIT-style license; see the piglit
repository's COPYING file. The files are test DATA driven through
SimpleFPEWrapper's GLSL translation pipeline - each shader compiles via
the wrapper's glShaderSource/glCompileShader interception and renders on
a real GLES3 device in a 250x250 pbuffer (piglit's default window size).

`KNOWN_DEVIATIONS.txt` lists the tests that fail by design on the
glslang-based pipeline, each with the root cause; CMake marks them
WILL_FAIL so an accidental regression elsewhere stays visible and a
newly-passing deviation demands delisting.

To extend the subset, re-run `tools/select_piglit_subset.py
<piglit-checkout> copy` - it whitelists exactly the runner's command
language, maps names to this directory's convention and skips files
already vendored.
Generated tests require running the piglit generators first
(`python3 gen_builtin_uniform_tests.py` etc. inside `generated_tests/`,
with `mako` and `numpy` installed).
