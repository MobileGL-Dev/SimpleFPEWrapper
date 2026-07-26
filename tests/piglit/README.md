# Vendored piglit shader_test subset

These `.shader_test` files are copied verbatim from the
[piglit](https://gitlab.freedesktop.org/mesa/piglit) test suite
(`tests/spec/glsl-1.10/execution` and `tests/spec/glsl-1.20/execution`),
selected for compatibility with the command subset implemented by
`tests/piglit_runner.c`. File names encode their origin directory.

piglit is distributed under an MIT-style license; see the piglit
repository's COPYING file. The files are test DATA driven through
SimpleFPEWrapper's GLSL translation pipeline - each shader compiles via
the wrapper's glShaderSource/glCompileShader interception and renders on
a real GLES3 device.

To extend the subset, re-run the selection filter against a piglit
checkout (see git history for the filter script) and add files here.
