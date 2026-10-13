#!/usr/bin/env python3
"""Build portable SDL GPU compute scalers from the retained CPU algorithms.
Requires glslangValidator and spirv-cross. No ROM-derived data is embedded.
"""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
DIR = ROOT / 'src/main/frontend/scalers'


def translate(text):
    text = re.sub(r'\buint32_t\b', 'uint', text)
    text = text.replace('inline ', '').replace('constexpr ', 'const ')
    text = text.replace('std::sqrt', 'sqrt').replace('std::clamp', 'clamp')
    text = re.sub(r'\.(yzw|zwx|wxy|xyz|yzwx|wxyz|zwxy)\(\)', r'.\1', text)
    text = text.replace('bvec2', 'vec2').replace('bvec4', 'vec4')
    return text

sai = (DIR / 'sai.hpp').read_text().split('namespace Frontend::SaI {', 1)[1].rsplit('}', 1)[0]
sai = re.sub(r'template<class Sample> inline std::array<uint32_t,4> (\w+)\(Sample get\)',
             r'uvec4 \1(int x,int y,int w,int h)', sai)
sai = re.sub(r'get\((-?\d+),\s*(-?\d+)\)', r'pixel(x+(\1),y+(\2),w,h)', sai)
sai = re.sub(r'return \{([^}]+)\};', r'return uvec4(\1);', sai)
sai = sai.replace('uint32_t){', 'uint32_t unused){')
sai = translate(sai).replace('const auto ', 'uint ').replace('(x<=1)-(y<=1)', 'int(x<=1)-int(y<=1)')
fx = (DIR / 'scalefx.hpp').read_text()
helpers = fx[fx.index('inline vec4 LE('):fx.index('inline void scale(')]
helpers = translate(helpers)
for operand in ['((a>>16)&255)', '((b>>16)&255)']:
    helpers=helpers.replace(operand+'/255.f', 'divround(float('+operand+'),255.f)')
for operand in ['float(int((a>>8)&255)-int((b>>8)&255))', 'float(int(a&255)-int(b&255))']:
    helpers=helpers.replace(operand+'/255.f', 'divround('+operand+',255.f)')
helpers=helpers.replace('return sqrt(', 'return divround(sqrt(').replace(')/3.f;', '),3.f);')
# GLSL has no implicit bool-to-float constructor conversion on all compilers.
helpers = helpers.replace('std::', '')
# int clamp is overloaded in GLSL.
body = fx[fx.index(' const auto n=w*h;'):fx.rindex('\n}\n}')]
loops = body.split(' for(int y=0;y<h;++y)for(int x=0;x<w;++x){')[1:]
assert len(loops) == 5
stages = []
for i, loop in enumerate(loops[:4]):
    loop = loop[:loop.rfind(' }')]
    loop = translate(loop).replace('const auto E=', 'uint E=')
    for j,name in enumerate(['metric','strength','flags','tags']):
        loop = re.sub(name+r'\[([^\]]+)\]', lambda m: 'fx.data['+m[1]+f'+{j}*p.w*p.h]', loop)
    if i == 3:
        loop = re.sub(r'\b((?:[EDFBH](?:0|1)?[chvo]|[DFBH]o|lvl[2-6][xyzw]))\.([xyzw])', r'(\1.\2 != 0.0)', loop)
    loop=loop.replace('(res + 2.f * hori + 4.f * vert + 8.f * orientation) / 15.f',
                      'divround(res + 2.f * hori + 4.f * vert + 8.f * orientation,15.f)')
    loop=loop.replace('(crn + 9.f * mid) / 80.f', 'divround(crn + 9.f * mid,80.f)')
    stages.append(loop)
last = loops[4][:loops[4].rfind(' }')]
last = translate(last).replace('const auto t=', 'vec4 t=').replace('const auto crn=', 'vec4 crn=')
last = last[:last.index('  for(int j=0;j<9;')]
last = last.replace('tags[y*w+x]', 'fx.data[y*w+x+3*w*h]')
last = last.replace('const int codes[]{', 'int codes[9]=int[9](').replace('};', ');')

common = '''#version 450
// Generated from the CPU reference. ScaleFX MIT and SaI Snes9x license notices
// are retained in scalefx.hpp, sai.hpp and SNES9X-LICENSE.txt.
layout(local_size_x=8,local_size_y=8,local_size_z=1) in;
layout(set=0,binding=0,std430) readonly buffer Input { uint data[]; } src;
layout(set=1,binding=1,std430) buffer Intermediate { vec4 data[]; } fx;
layout(set=1,binding=0,rgba8) uniform writeonly image2D output_image;
layout(set=2,binding=0,std140) uniform Params { int w; int h; int filter; int factor; } p;
// Refine reciprocal division with an explicit fused residual. Metal's
// approximate constant division otherwise changes ScaleFX's exact tie decisions.
float divround(float a,float b) { precise float q=a/b; precise float e=fma(-q,b,a); return fma(e,1.0/b,q); }
vec4 divround(vec4 a,float b) { return vec4(divround(a.x,b),divround(a.y,b),divround(a.z,b),divround(a.w,b)); }
uint pixel(int x,int y,int w,int h) { return src.data[w*h+clamp(y,0,h-1)*w+clamp(x,0,w-1)]; }
'''
license_notice = fx[fx.index('/*'):fx.index('*/')+2]
common = common.replace('#version 450', '#version 450\n'+license_notice+'\n// SaI adapted from Snes9x; see SNES9X-LICENSE.txt.')
common += helpers + sai
for i in range(4):
    stages[i] = re.sub(r'input\[index\(([^\]]+)\)\]', r'pixel(\1)', stages[i])
    stages[i] = stages[i].replace('input[y*w+x]', 'pixel(x,y,w,h)')

simple = '''
uint simple(int x,int y,int sx,int sy,int w,int h,int filter) {
 if(filter==0)return pixel(x,y,w,h);
 uint E=pixel(x,y,w,h),B=pixel(x,y-1,w,h),D=pixel(x-1,y,w,h),F=pixel(x+1,y,w,h),H=pixel(x,y+1,w,h);
 if(filter==3 || filter==4) { uvec4 q=filter==3?sai(x,y,w,h):super_sai(x,y,w,h); return q[sy*2+sx]; }
 if(B==H || D==F) return E;
 if(filter==1) { if(sy==0)return sx==0?(D==B?D:E):(B==F?F:E); return sx==0?(D==H?D:E):(H==F?F:E); }
 uint A=pixel(x-1,y-1,w,h),C=pixel(x+1,y-1,w,h),G=pixel(x-1,y+1,w,h),I=pixel(x+1,y+1,w,h);
 uint q[9]=uint[9](D==B?D:E,((D==B && E!=C)||(B==F && E!=A))?B:E,B==F?F:E,
 ((D==B && E!=G)||(D==H && E!=A))?D:E,E,((B==F && E!=I)||(H==F && E!=C))?F:E,
 D==H?D:E,((D==H && E!=I)||(H==F && E!=G))?H:E,H==F?F:E);
 return q[sy*3+sx];
}
'''
compose = '''
 int ox=int(gl_GlobalInvocationID.x),oy=int(gl_GlobalInvocationID.y);
 int w=p.w,h=p.h;
 if(ox>=w*p.factor || oy>=h*p.factor)return;
 int x=ox/p.factor,y=oy/p.factor,sx=ox%p.factor,sy=oy%p.factor;
 uint fg=0;
 if(p.filter==5) {
''' + last + '''
 const int dx[9]=int[9](0,-1,-2,1,2,0,0,0,0),dy[9]=int[9](0,0,0,0,0,-1,-2,1,2);
 int k=codes[sy*3+sx];fg=pixel(x+dx[k],y+dy[k],w,h);
 } else fg=simple(x,y,sx,sy,w,h,p.filter);
 uint bg=src.data[y*w+x],alpha=fg>>24,color;
 if(src.data[2*w*h+y*w+x]!=0 || alpha==0)color=bg;
 else if(alpha==255)color=fg;
 else {
 color=0xff000000u;
 for(uint shift=0;shift<24;shift+=8) {
 uint v=((fg>>shift)&255u)+((((bg>>shift)&255u)*(255u-alpha)+127u)/255u);
 color|=min(v,255u)<<shift;
 }
 }
 imageStore(output_image,ivec2(ox,oy),vec4(float((color>>16)&255u),float((color>>8)&255u),float(color&255u),float(color>>24))/255.0);
'''
# The compute passes all use identical binding layouts, including final composition.
with tempfile.TemporaryDirectory() as temporary:
    entries=[]
    for i in range(5):
        name=f'sprite_pass{i}'
        code=common+simple+'\nvoid main(){\n'
        if i<4:
            code+='int x=int(gl_GlobalInvocationID.x),y=int(gl_GlobalInvocationID.y),w=p.w,h=p.h; if(x>=w || y>=h)return;\n{\n'+stages[i]+'\n}'
        else: code+=compose
        code+='\n}\n'
        code=re.sub(r'\bfilter\b', 'mode', code)
        code='\n'.join(line.rstrip() for line in code.splitlines())+'\n'
        source=Path(temporary)/f'{name}.comp';source.write_text(code)
        (DIR/f'{name}.comp').write_text(code)
        spv=source.with_suffix('.spv');msl=source.with_suffix('.metal')
        subprocess.run(['glslangValidator','-V','--target-env','vulkan1.0',str(source),'-o',str(spv)],check=True)
        subprocess.run(['spirv-cross',str(spv),'--msl','--msl-version','20100','--output',str(msl)],check=True)
        metal=msl.read_text().replace('sqrt(', 'precise::sqrt(').replace('using namespace metal;', 'using namespace metal;\n#pragma clang fp contract(off)\n#pragma clang fp reassociate(off)')
        for resource,slot in [('p',0),('src',1),('fx',2)]:
            metal=re.sub(r'\b'+resource+r' \[\[buffer\(\d+\)\]\]', resource+f' [[buffer({slot})]]',metal)
        metal='\n'.join(line.rstrip() for line in metal.splitlines())+'\n'
        msl.write_text(metal)
        (DIR/f'{name}.comp').write_text(code)
        entries.append(f'inline constexpr unsigned char {name}_spv[]={{'+','.join(str(v) for v in spv.read_bytes())+'};\n')
        entries.append(f'inline constexpr char {name}_msl[]=R"MSL('+msl.read_text()+')MSL";\n')
    scene_code="""#version 450
layout(local_size_x=8,local_size_y=8,local_size_z=1) in;
layout(set=0,binding=0,std430) readonly buffer LayerInput { uint data[]; } src;
layout(set=1,binding=0,std430) buffer Colors { uint data[]; } fx;
layout(set=2,binding=0,std140) uniform Params { int w; int h; int clouds; int trails; int explosions; int afterburner; } p;
void main(){
 uint x=gl_GlobalInvocationID.x,y=gl_GlobalInvocationID.y;
 if(x>=uint(p.w)||y>=uint(p.h))return;
 uint n=uint(p.w*p.h),i=y*uint(p.w)+x,head=src.data[i],trans=255u,alpha=0u;
 uvec3 rgb=uvec3(0u);
 while(head!=0xffffffffu && trans!=0u){
  uint j=n+head*4u,color=src.data[j],kind=src.data[j+2u];
  // Tile/shadow actions terminate the sprite stack without introducing
  // road or text colors into the foreground scaler's neighborhood.
  if(kind==3u || kind==4u){if(kind==4u)fx.data[i]=color;break;}
  uint a=kind==1u?uint(p.clouds):kind==2u?uint(p.trails):kind==5u?uint(p.explosions):kind==6u?uint(p.afterburner):255u;
  uint coverage=(a*trans+127u)/255u;alpha+=coverage;
  rgb+=(uvec3(color&255u,(color>>8)&255u,(color>>16)&255u)*coverage+127u)/255u;
  trans=(trans*(255u-a)+127u)/255u;head=src.data[j+1u];
 }
 rgb=min(rgb,uvec3(255u));fx.data[n+i]=(min(alpha,255u)<<24)|(rgb.z<<16)|(rgb.y<<8)|rgb.x;
}
"""
    source=Path(temporary)/'sprite_scene.comp';source.write_text(scene_code)
    spv=source.with_suffix('.spv');msl=source.with_suffix('.metal')
    subprocess.run(['glslangValidator','-V',str(source),'-o',str(spv)],check=True)
    subprocess.run(['spirv-cross',str(spv),'--msl','--msl-version','20100','--output',str(msl)],check=True)
    metal=msl.read_text()
    for resource,slot in [('p',0),('src',1),('fx',2)]:
        metal=re.sub(r'\b'+resource+r' \[\[buffer\(\d+\)\]\]',resource+f' [[buffer({slot})]]',metal)
    (DIR/'sprite_scene.comp').write_text(scene_code)
    entries.append('inline constexpr unsigned char sprite_scene_spv[]={'+','.join(str(v) for v in spv.read_bytes())+'};\n')
    entries.append('inline constexpr char sprite_scene_msl[]=R"MSL('+metal+')MSL";\n')
    (DIR/'gpu_shaders.hpp').write_text('// Generated by tools/scalers/build_gpu_shaders.py; shader data only.\n'+license_notice+'\n// SaI adaptations: Snes9x license, see SNES9X-LICENSE.txt.\n#pragma once\nnamespace Frontend::GPUShaders {\n'+''.join(entries)+'}\n')
