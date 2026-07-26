# Vendored piglit shader_test subset

These 656 `.shader_test` files are copied verbatim from the
[piglit](https://gitlab.freedesktop.org/mesa/piglit) test suite
(`tests/spec/glsl-1.10/{execution,linker}` and
`tests/spec/glsl-1.20/{execution,linker}`), selected for compatibility
with the command subset implemented by `tests/piglit_runner.c`
(draw rect [tex], probes, uniforms, ortho, clear, link success/error,
texture rgbw/checkerboard/miptree, texparameter min/mag).
File names encode their origin directory
(`glsl-1.10__builtins_...`, `glsl-1.20__linker__...`).

piglit is distributed under an MIT-style license; see the piglit
repository's COPYING file. The files are test DATA driven through
SimpleFPEWrapper's GLSL translation pipeline - each shader compiles via
the wrapper's glShaderSource/glCompileShader interception and renders on
a real GLES3 device in a 250x250 pbuffer (piglit's default window size).

`KNOWN_DEVIATIONS.txt` lists the tests that fail by design on the
glslang-based pipeline, each with the root cause; CMake marks them
WILL_FAIL so an accidental regression elsewhere stays visible and a
newly-passing deviation demands delisting.

To extend the subset, re-run the selection filter against a piglit
checkout (see git history for the filter script) and add files here.
