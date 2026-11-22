"""Build an isolated material fixture from the real injected GLSL strings."""
import ast
import re
import sys
from pathlib import Path
s=Path(sys.argv[1]).read_text()
def strings(text):
    # Only C++ literal lines; quoted words in comments are not shader source.
    return "".join(ast.literal_eval(m[1]) for m in re.finditer(
        r'^\s*("(?:[^"\\]|\\.)*")', text, re.M))
a=s.index('static const char* kDeclarations ='); b=s.index('source.insert(mainPos',a)
d=strings(s[a:b])
a=s.index('static const char* kBody =',b); b=s.index('source.insert(shiftedOutput',a)
body=strings(s[a:b]); body=body[:body.index('if (nwnWeatherSurfaceClass > 0.5 && nwnWeatherSurfaceClass < 1.5')]
prefix='''#version 330 core
#define POSITION_VIEW 1
#define VERTEX_NORMAL 1
#define FOG 0
#define texture2D texture
uniform mat4 m_view, m_view_inv;
uniform vec2 pixelParams;
uniform float farClip;
uniform int areaFlags;
out vec4 result;
'''
main='''void main() {
 vec3 vPosView=vec3((gl_FragCoord.xy*pixelParams-0.5)*80.0,-20.0);
 vec3 vSurfaceNormal=vec3(0,0,1); vec3 vFragmentNormal=vec3(0,0,1);
 vec4 FragmentColor=vec4(0.4,0.45,0.5,1);
'''
output=Path(sys.argv[2])
shader=prefix+d+main+body+'\n#endif\nresult=FragmentColor;\n}\n'
output.write_text(shader)
# An edge-on projected noise footprint must not divide by zero.
output.with_name('degenerate.frag').write_text(shader.replace(
    'float nwnRainNoiseHeight =',
    'nwnRainNoiseCoord = vec2(nwnRainNoiseCoord.x, 0.0);\n    float nwnRainNoiseHeight ='))
# Exercise the real gradient helper with a known affine height field.
for label,coord in [('gradient','p'), ('gradient-degenerate','vec2(p.x,0.0)')]:
    output.with_name(label+'.frag').write_text(prefix+d+"""
void main(){vec2 p=(gl_FragCoord.xy*pixelParams-0.5)*80.0;
vec2 c=COORD;float h=1.0+0.25*c.x-0.5*c.y;
result=vec4(nwnRainFilmGradient(c,h),0,1);}
""".replace('COORD',coord))
# Count iterations in the actual trace loop, including GL's default uniform.
counted = d.replace('        travel += clamp', '        nwnTestSteps++;\n        travel += clamp')
for label in ['trace10', 'trace5', 'trace3', 'trace-default']:
    output.with_name(label+'.frag').write_text(prefix+'int nwnTestSteps=0;\n'+counted+"""
void main(){vec4 hit=nwnRainTraceReflection(vec3(0,0,-20),vec3(0,0,1));
result=vec4(float(nwnTestSteps),0,0,1);}
""")
output.with_name('surface-puddles-off.frag').write_text(shader)
# Observe the real material call site: the off setting must execute neither
# the reflection trace nor the refraction branch, while the on case does.
optics_declarations = d.replace(
    'vec4 nwnRainTraceReflectionBudget(vec3 origin, vec3 ray, int iterations) {',
    'vec4 nwnRainTraceReflectionBudget(vec3 origin, vec3 ray, int iterations) {nwnTestOptics++;')
optics_body = body.replace('        vec3 nwnRainRefractPos =',
                          '        nwnTestOptics++;\n        vec3 nwnRainRefractPos =')
for label in ['optics-on', 'optics-puddles-off']:
    output.with_name(label+'.frag').write_text(
        prefix+'int nwnTestOptics=0;\n'+optics_declarations+main+optics_body+
        '\n#endif\nresult=vec4(float(nwnTestOptics),0,0,1);\n}\n')

for label in ['surface-film-off-puddles-off','surface-film-off']:
    output.with_name(label+'.frag').write_text(shader)
# Instrument field entry, rather than merely inspecting the chosen uniform.
field_declarations = d.replace(
    'float nwnRainGroundNoiseHeight(vec2 p, float time) {',
    'float nwnRainGroundNoiseHeight(vec2 p, float time) {nwnTestFilmFields++;')
field_declarations = field_declarations.replace(
    'vec3 nwnRainRippleField(vec2 worldXY, float time, float rippleScale) {',
    'vec3 nwnRainRippleField(vec2 worldXY, float time, float rippleScale) {nwnTestFilmFields++;')
field_declarations = field_declarations.replace(
    'vec3 nwnRainFilmRippleField(vec2 worldXY, float time) {',
    'vec3 nwnRainFilmRippleField(vec2 worldXY, float time) {nwnTestFilmFields++;')
for label in ['film-fields-puddles-off','film-fields-film-off-puddles-off']:
    output.with_name(label+'.frag').write_text(prefix+'int nwnTestFilmFields=0;\n'+field_declarations+
        main+body+'\n#endif\nresult=vec4(float(nwnTestFilmFields),0,0,1);\n}\n')
# Compare the bounded production ripple with its original unbounded formula.
reference = d.replace('            if (q*q > 0.5) continue;\n','')
for scale in ['0.5','1.0']:
    for kind,decl in [('reference',reference),('bounded',d)]:
        output.with_name('ripple-'+kind+'-'+scale+'.frag').write_text(prefix+decl+"""
void main(){vec2 p=(gl_FragCoord.xy*pixelParams-0.5)*80.0;
result=vec4(nwnRainRippleField(p,nwnWeatherTime,SCALE),1);}
""".replace('SCALE',scale))

# Compare the actual compact film field against the preserved general kernel.
# Sweep separated phases and signed world coordinates; puddles still use the
# original nine-cell field and its stricter existing regression above.
for phase in ['0.0','1.3','11.7','4095.0']:
    for kind,call in [('reference','nwnRainRippleField(p,TIME,0.5)'),
                      ('compact','nwnRainFilmRippleField(p,TIME)')]:
        output.with_name('ripple-film-'+kind+'-'+phase+'.frag').write_text(prefix+d+"""
void main(){vec2 p=(gl_FragCoord.xy*pixelParams-0.5)*80.0;
result=vec4(CALL,1);}
""".replace('CALL',call.replace('TIME',phase)))

# Observe actual native-probe component entries independently.
profile_decl=d.replace('const int nwnWetFilmProbe = 0;', 'uniform int nwnWetFilmProbe;')
for signature,counter in [
    ('float nwnRainGroundNoiseHeight(vec2 p, float time) {','nwnTestNoise'),
    ('vec3 nwnRainFilmRippleField(vec2 worldXY, float time) {','nwnTestImpacts'),
    ('float nwnWeatherPrecipitationExposure(vec3 worldPos) {','nwnTestExposure')]:
    profile_decl=profile_decl.replace(signature,signature+counter+'++;')
for mode in range(6):
    label='profile-'+str(mode)+('-film-off' if mode==5 else '')+'-puddles-off.frag'
    output.with_name(label).write_text(prefix+'int nwnTestNoise=0,nwnTestImpacts=0,nwnTestExposure=0;\n'+profile_decl+
        main+body+'\n#endif\nresult=vec4(nwnTestNoise,nwnTestImpacts,nwnTestExposure,1);\n}\n')
