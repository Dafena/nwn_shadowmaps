#!/usr/bin/env python3
"""A surface hidden beyond authored fog end must not extend the added volume."""
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import check_shaders
shader=check_shaders.named_string('godrays_runtime.inc','scatterFragment')
import re
match=re.search(r'float nwnFogMarchEnd\([^)]*\)\{return ([^;]+);\}',shader)
assert match, 'Volume continues to hidden surface depth after native fog is fully opaque'
condition, branches=match.group(1).split('?');yes,no=branches.split(':')
def end(sceneDist,farLimit,fogEnd,authored,viewPerMetre):
 values=locals();scope=dict(min=min,max=max)
 expr=yes if eval(condition,scope,values) else no
 return eval(expr,scope,values)
for angle in (1.0,.8,.2):
 cutoff=45/angle
 assert end(cutoff+1,250,45,1,angle)==end(cutoff+20,250,45,1,angle)
 assert end(10,250,45,1,angle)==10
 assert end(70,250,45,0,angle)==70 # No authored fog: extra atmosphere still works.
assert 'float marchLength=nwnFogMarchEnd(' in shader
print('PASS: authored opaque boundary stops hidden-depth-dependent volume')
