"""Exercise modern JSON scenes with independent pixel/color checks, without ROMs."""
from pathlib import Path
import json
import os
import re
import struct
import subprocess
import sys
import zlib

root = Path(__file__).resolve().parents[1]
binary = (root / sys.argv[1]).resolve()
out = root / 'build/oracle/custom-stage-json'
out.mkdir(parents=True, exist_ok=True)
env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')

def png(path, width, height, pixel):
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind+data))
    raw = b''.join(b'\0' + bytes(v for x in range(width) for v in pixel(x, y)) for y in range(height))
    path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b''))

png(out/'wide.png', 1024, 4, lambda x, y: (x % 256, (x//4) % 256, (x*13) % 256, 255))
png(out/'tile.png', 2, 2, lambda x, y: (53, 111, 173, 255 if x == y else 100))
layers = [{'name':'backdrop', 'order':0, 'elements':[{'type':'rectangle','x':40,'y':78,'width':880,'height':392,'color':[10,20,30,255]}]}]
for i in range(32):
    layers.append({'name':f'layer-{i}', 'order':i+1, 'elements':[{'type':'rectangle','x':50+i*10,'y':85,'width':8,'height':8,'color':[i*7,37,211,255]}]})
layers += [
    {'order':100, 'elements':[{'type':'image','asset':'wide.png','x':60,'y':110,'width':256,'height':16}]},
    {'order':150, 'elements':[{'type':'image','asset':'tile.png','x':400,'y':110,'width':4,'height':4,'repeat':[80,60]}]},
    {'order':200, 'elements':[{'type':'image','asset':'tile.png','x':70+(i%60)*4,'y':150+(i//60)*4,'width':3,'height':3,'color':[i%256,103,219,255]} for i in range(600)]},
    # Deliberately reverse document order; numeric order must control blending.
    {'order':275, 'elements':[{'type':'rectangle','x':100,'y':230,'width':20,'height':20,'color':[221,91,37,128]}]},
    {'order':250, 'elements':[{'type':'rectangle','x':100,'y':230,'width':20,'height':20,'color':[20,40,60,255]}]},
    {'order':999.5,'elements':[{'type':'rectangle','x':10,'y':40,'width':10,'height':10,'color':[255,0,0,255]}]},
    {'order':2000,'elements':[{'type':'rectangle','x':10,'y':10,'width':10,'height':10,'color':[213,71,139,255]}]},
]
stage = {'version':1,'name':'MODERN RGBA SCENE','presentation':{'rom_environment':False},'layers':layers}
path = out/'modern.json'
path.write_text(json.dumps(stage))
image = out/'modern.bmp'

def run(path, good=True, screenshot=None):
    args = [str(binary),'--no-sprites','--stage-file',str(path),'--demo','--frames','1']
    if screenshot: args += ['--screenshot',str(screenshot)]
    result = subprocess.run(args,cwd=root,env=env,text=True,capture_output=True,timeout=60)
    assert (result.returncode == 0) == good, result.stdout+result.stderr
    return result.stdout

result = run(path,screenshot=image)
assert int(re.search(r'custom_layers=(\d+)',result)[1]) > 32, result
assert int(re.search(r'custom_drawn=(\d+)',result)[1]) > 5400, result
assert 'custom_tiles=4800' in result, result
b = image.read_bytes()
offset = struct.unpack_from('<I',b,10)[0]
w,h = struct.unpack_from('<ii',b,18)
step = struct.unpack_from('<H',b,28)[0]//8
stride = ((w*step+3)//4)*4
assert (w,abs(h)) == (1152,720)
def rgb(x,y):
    x,y = round(x*1.2),round(y*1.2)
    row = 719-y if h > 0 else y
    p = offset+row*stride+x*step
    return tuple(b[p:p+3][::-1])
expected = tuple(round((src*128+dst*127)/255) for src,dst in zip((221,91,37),(20,40,60)))
assert all(abs(a-c)<=2 for a,c in zip(rgb(110,240),expected)), (rgb(110,240),expected)
assert rgb(15,45) == (10,23,29), 'Fractional scene layer crossed the HUD boundary'
assert rgb(15,15) == (213,71,139), 'Custom layer cannot draw above HUD/outside world clip'
assert len({rgb(x,118) for x in range(61,315)}) > 128, 'True-color texture was quantized to indexed pens'
for i,bad in enumerate([
    {'elements':[{'type':'image','asset':'missing.png'}]},
    {'elements':[{'type':'rectangle','repeat':[1.5,2]}]},
    {'elements':[{'type':'rectangle','color':[0,0,0,256]}]},
    {'elements':[{'type':'rectangle','color':[0,0,0,0.5]}]},
    {'elements':[{'type':'rectangle','width':0}]},
    {'elements':[], 'space':'hardware'},
    {'elements':[], 'sprite_slots':256},
]):
    invalid=out/f'invalid-layer-{i}.json'
    invalid.write_text(json.dumps({'version':1,'layers':[bad]}))
    run(invalid,False)
print('JSON scenes: 40 layers, 600 sprites, 4800 tiles, 1024px true-color PNG, alpha/order, above-HUD placement and invalid resource/schema checks passed')
