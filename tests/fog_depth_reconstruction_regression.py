#!/usr/bin/env python3
"""Thin foreground: four background samples must not be normalized onto it."""
import re,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import check_shaders
scatter=check_shaders.named_string('godrays_runtime.inc','scatterFragment')
composite=check_shaders.named_string('godrays_runtime.inc','compositeFragment')
pattern=r'bool nwnFogDepthMatch\(float a,float b\)\{return ([^;]+);\}'
a=re.search(pattern,scatter);b=re.search(pattern,composite)
assert a and b, 'No matched-depth/refinement contract: background always blends onto thin foreground'
assert a.group(1)==b.group(1)
def matches(a,b):return eval(re.search(pattern,scatter).group(1),dict(abs=abs,max=max,min=min),dict(a=a,b=b))
assert not any(matches(x,12) for x in (50,51,49,52))
assert matches(50,50.01) and matches(60000,60000)
assert not matches(60000,50)
assert 'if(total<=0.0)discard;' in composite
assert 'if(matched)discard;' in scatter
assert 'nwnRefinePass' in scatter and 'nwnRefineSceneDepth' in scatter
print('PASS: unmatched foreground is reserved for exact-depth fog, not background interpolation')
