#!/usr/bin/env python3
"""Exercise the runtime scatter expression and actual fixed-function blend choice.

LDR fog must not clip an already-bright surface; zero scattering must preserve
Beer-Lambert attenuation, and zero medium must preserve the native image.
"""
import sys, re
from pathlib import Path
from types import SimpleNamespace
import numpy as np
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import check_shaders
source = Path('src/godrays_runtime.inc').read_text()
shader = check_shaders.named_string('src/godrays_runtime.inc', 'scatterFragment')
expression = re.search(r'vec3 inscatter=([^;]+);', shader).group(1)
factor = re.search(r'if \(g_godraysDebug == 0\)\s*gl::BlendFuncSeparate\((\w+), GL_SRC_ALPHA, GL_ZERO, GL_ONE\)', source).group(1)
rng = np.random.default_rng(2718)
for i in range(10000):
    ambient, shaft, lampAcc = rng.random((3,3)) * (1.5,6,3)[i % 3]
    fade = rng.random(); gain = rng.random()*24
    src = eval(expression, {'__builtins__': {}}, dict(vec3=lambda x: np.full(3,x), exp=np.exp,
        ambient=ambient,shaft=shaft,lampAcc=lampAcc,nwnLampGain=gain,
        lampGlow=1-np.exp(-lampAcc*gain),nwnShaft=SimpleNamespace(z=fade)))
    dst = np.ones(3) if i % 4 == 0 else rng.random(3)
    transmittance = rng.random()
    weight = 1-dst if factor == 'GL_ONE_MINUS_DST_COLOR' else np.ones(3)
    result = src*weight+dst*transmittance
    assert np.all(np.isfinite(result)) and np.all(result >= 0) and np.all(result <= 1+1e-7), (i, result)
# The zero-medium limit uses the same runtime expression: native RGB must survive.
zero = np.zeros(3)
scatter = eval(expression, {'__builtins__': {}}, dict(vec3=lambda x: np.full(3,x), exp=np.exp,
    ambient=zero,shaft=zero,lampAcc=zero,nwnLampGain=24,
    lampGlow=zero,nwnShaft=SimpleNamespace(z=1)))
assert np.array_equal(scatter, zero)
assert factor == 'GL_ONE_MINUS_DST_COLOR'
print('PASS: 10000 combined fog/light exposures stay in LDR range without clipping')
