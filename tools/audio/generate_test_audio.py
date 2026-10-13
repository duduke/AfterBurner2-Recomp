#!/usr/bin/env python3
"""Copyright-free WAV and verbatim FLAC fixtures; retained under ignored build/."""
from pathlib import Path
import hashlib, math, struct, wave
p=Path('build/oracle/music-fixtures');p.mkdir(parents=True,exist_ok=True)
frames=3840
samples=[int(8192*math.sin(2*math.pi*440*n/48000)) for n in range(frames)]
raw=b''.join(struct.pack('<hh',v,-v) for v in samples)
with wave.open(str(p/'B tone.WAV'),'wb') as w:w.setnchannels(2);w.setsampwidth(2);w.setframerate(48000);w.writeframes(raw)
def crc(data,bits,polynomial):
    result=0
    for byte in data:
        result ^= byte << (bits-8)
        for _ in range(8):result=((result<<1)^polynomial if result & (1<<(bits-1)) else result<<1)&((1<<bits)-1)
    return result
info=struct.pack('>HH',1920,1920)+bytes(6)+((48000<<44)|(1<<41)|(15<<36)|frames).to_bytes(8,'big')+hashlib.md5(raw).digest()
flac=b'fLaC'+b'\x80\x00\x00\x22'+info
for number in range(2):
    header=b'\xff\xf8\x70\x18'+bytes([number])+struct.pack('>H',1919)
    frame=header+bytes([crc(header,8,7)])
    for sign in [1,-1]:frame+=b'\x02'+b''.join(struct.pack('>h',sign*v) for v in samples[number*1920:(number+1)*1920])
    flac+=frame+struct.pack('>H',crc(frame,16,0x8005))
(p/'C tone.FLAC').write_bytes(flac)
(p/'A invalid.wav').write_text('not audio')
(p/'ignore.txt').write_text('not audio')
print('Generated WAV/FLAC fixtures: 3840 stereo frames, 48000 Hz')

extra=p/'formats';extra.mkdir(exist_ok=True)
with wave.open(str(extra/'mastered.wav'),'wb') as w:
    w.setnchannels(2);w.setsampwidth(2);w.setframerate(48000)
    # Full-scale mastered fixture: the previous mixer clips overlapping effects.
    w.writeframes(struct.pack('<hh',32767,-32767)*frames)
unknown=bytearray(flac)
packed=int.from_bytes(unknown[18:26],'big') & ~((1<<36)-1)
unknown[18:26]=packed.to_bytes(8,'big')
(extra/'unknown-count.flac').write_bytes(unknown)
with wave.open(str(extra/'mono24.wav'),'wb') as w:
    w.setnchannels(1);w.setsampwidth(3);w.setframerate(44100)
    w.writeframes(b''.join(int(2097152*math.sin(2*math.pi*220*n/44100)).to_bytes(3,'little',signed=True) for n in range(4410)))
