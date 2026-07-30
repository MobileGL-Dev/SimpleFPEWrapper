#!/usr/bin/env python3
"""筛选 piglit shader_test:只保留 tests/piglit_runner.c 能完整执行的用例"""
import os, re, sys

PIGLIT = sys.argv[1] if len(sys.argv) > 1 else '/tmp/piglit'
VENDORED = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'tests', 'piglit')

# [test] 行白名单(与 runner 的 sscanf/strcmp 逐一对应)
F = r'-?\d+(?:\.\d+)?(?:e[-+]?\d+)?'
TEST_RE = [re.compile(p) for p in [
    rf'^clear color\s+{F}\s+{F}\s+{F}\s+{F}$',
    r'^clear$',
    r'^ortho$',
    rf'^draw rect tex\s+{F}\s+{F}\s+{F}\s+{F}\s+{F}\s+{F}\s+{F}\s+{F}$',
    rf'^draw rect ortho\s+{F}\s+{F}\s+{F}\s+{F}$',
    rf'^draw rect\s+{F}\s+{F}\s+{F}\s+{F}$',
    r'^texture rgbw \d+ \( *\d+ *, *\d+ *\)( GL_RGBA8)?$',
    rf'^texture checkerboard \d+ \d+ \( *\d+ *, *\d+ *\) \( *{F} *, *{F} *, *{F} *, *{F} *\) \( *{F} *, *{F} *, *{F} *, *{F} *\)$',
    r'^texture miptree \d+$',
    r'^texparameter 2D (min|mag) (nearest|linear|nearest_mipmap_nearest|linear_mipmap_nearest|nearest_mipmap_linear|linear_mipmap_linear)$',
    rf'^relative probe rgba? \( *{F} *, *{F} *\) \( *{F} *, *{F} *, *{F}( *, *{F})? *\)$',
    rf'^probe rect rgba \( *{F} *, *{F} *, *{F} *, *{F} *\) \( *{F} *, *{F} *, *{F} *, *{F} *\)$',
    rf'^probe rect rgb \( *{F} *, *{F} *, *{F} *, *{F} *\) \( *{F} *, *{F} *, *{F} *\)$',
    rf'^probe all rgba\s+{F}\s+{F}\s+{F}\s+{F}$',
    rf'^probe all rgb\s+{F}\s+{F}\s+{F}$',
    r'^link (success|error)$',
    rf'^probe rgba\s+\d+\s+\d+\s+{F}\s+{F}\s+{F}\s+{F}$',
    rf'^probe rgb\s+\d+\s+\d+\s+{F}\s+{F}\s+{F}$',
    rf'^uniform (float|int) \S+\s+{F}$',
    rf'^uniform vec2 \S+\s+{F}\s+{F}$',
    rf'^uniform vec3 \S+\s+{F}\s+{F}\s+{F}$',
    rf'^uniform vec4 \S+\s+{F}\s+{F}\s+{F}\s+{F}$',
    rf'^uniform ivec2 \S+\s+{F}\s+{F}$',
    rf'^uniform ivec3 \S+\s+{F}\s+{F}\s+{F}$',
    rf'^uniform ivec4 \S+\s+{F}\s+{F}\s+{F}\s+{F}$',
    rf'^uniform mat2(x2)? \S+(\s+{F}){{4}}$',
    rf'^uniform mat3(x3)? \S+(\s+{F}){{9}}$',
    rf'^uniform mat4(x4)? \S+(\s+{F}){{16}}$',
    rf'^uniform mat2x3 \S+(\s+{F}){{6}}$',
    rf'^uniform mat2x4 \S+(\s+{F}){{8}}$',
    rf'^uniform mat3x2 \S+(\s+{F}){{6}}$',
    rf'^uniform mat3x4 \S+(\s+{F}){{12}}$',
    rf'^uniform mat4x2 \S+(\s+{F}){{8}}$',
    rf'^uniform mat4x3 \S+(\s+{F}){{12}}$',
]]

BAD_SECTIONS = ['[vertex shader passthrough]', '[geometry shader]', '[vertex data]',
                '[fragment program]', '[vertex program]', '[tessellation',
                '[compute shader]', '[vertex shader spirv]', '[fragment shader spirv]']

def sections(text):
    out = {}
    cur = None
    for line in text.splitlines():
        if line.startswith('['):
            cur = line.strip()
            out.setdefault(cur, []).append([])
        elif cur is not None:
            out[cur][-1].append(line)
    return out

def check(path):
    text = open(path, encoding='utf-8', errors='replace').read()
    low = text.lower()
    for bad in BAD_SECTIONS:
        if bad in low: return None
    if '[fragment shader]' not in text: return None
    if '[test]' not in text: return None
    secs = sections(text)
    for req in secs.get('[require]', [[]]):
        for line in req:
            line = line.strip()
            if not line: continue
            if re.match(r'^GLSL >= 1\.[12]0?$', line): continue
            if re.match(r'^GL >= [12]\.\d$', line): continue
            if re.match(r'^GL_MAX_VARYING_COMPONENTS >= \d+$', line): continue
            return None
    for body in secs.get('[test]', []):
        for line in body:
            line = line.strip()
            if not line or line.startswith('#'): continue
            if not any(r.match(line) for r in TEST_RE): return None
    return text

roots = [
    ('tests/spec/glsl-1.10', 'glsl-1.10'),
    ('tests/spec/glsl-1.20', 'glsl-1.20'),
    ('tests/shaders', 'shaders'),
    ('generated_tests/spec/glsl-1.10', 'glsl-1.10'),
    ('generated_tests/spec/glsl-1.20', 'glsl-1.20'),
]
have = set(os.listdir(VENDORED))
picked, rejected = [], 0
for root, prefix in roots:
    base = os.path.join(PIGLIT, root)
    for dirpath, _, files in os.walk(base):
        for fn in sorted(files):
            if not fn.endswith('.shader_test'): continue
            rel = os.path.relpath(os.path.join(dirpath, fn), base)
            # 旧命名规则:execution/ 层级剥掉,linker/ 记为 linker__,其余 / 变 _
            legacy = rel
            if legacy.startswith('execution/'):
                legacy = legacy[len('execution/'):]
            legacy = legacy.replace('linker/', 'linker__', 1).replace('/', '_')
            name = prefix + '__' + legacy
            if name in have:
                continue
            if check(os.path.join(dirpath, fn)) is None:
                rejected += 1
                continue
            picked.append((os.path.join(dirpath, fn), name))

print(f"picked={len(picked)} rejected={rejected} already={len([f for f in have if f.endswith('.shader_test')])}")
for src, name in picked[:40]:
    print(" ", name)
if len(sys.argv) > 2 and sys.argv[2] == 'copy':
    import shutil
    for src, name in picked:
        shutil.copyfile(src, os.path.join(VENDORED, name))
    print(f"copied {len(picked)}")
