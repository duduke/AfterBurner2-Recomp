#!/usr/bin/env python3
"""Emit bounded, address-labelled sub sprite/road instructions from the user's ROM image.

No runtime opcode decoder or ROM data is generated. The supported forms here
are the selected graph's instruction subset. Unknown forms fail rather than guess.
Usage: python3 tools/generate_sub_schedule.py /tmp/sub.bin [--check]
"""
from pathlib import Path
import argparse
import re
import sys
sys.dont_write_bytecode = True
from m68k_disasm import disasm_one

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('image', type=Path)
parser.add_argument('--check', action='store_true')
parser.add_argument('--graph', choices=['sub'], default='sub')
args = parser.parse_args()
rom = args.image.read_bytes()


def split(text):
    return re.split(r',(?=[^)]*(?:\(|$))', text)  # commas outside addressing parentheses


def ea(text):
    if re.fullmatch(r'D[0-7]', text): return ('d', int(text[1]), 0)
    if re.fullmatch(r'A[0-7]', text): return ('a', int(text[1]), 0)
    if text.startswith('#$'): return ('i', int(text[2:], 16), 0)
    if text.startswith('#'): return ('i', int(text[1:]), 0)
    m = re.fullmatch(r'\(\$(\w+)\)\.([WL])', text)
    if m:
        value = int(m[1], 16)
        if m[2] == 'W' and value & 0x8000: value |= 0xffff0000
        return ('abs', value, 1 if m[2] == 'W' else 2)
    m = re.fullmatch(r'(-?\d+)\(A(\d)\)', text)
    if m: return ('disp', int(m[2]), int(m[1]))
    m = re.fullmatch(r'\(A(\d)\)(\+?)', text)
    if m: return ('post' if m[2] else 'ind', int(m[1]), 0)
    m = re.fullmatch(r'(-?\d+)\(A(\d),D(\d)\.W\)', text)
    if m: return ('index', int(m[2]), (int(m[1]), int(m[3])))
    m=re.fullmatch(r'\$([0-9A-Fa-f]+)\(PC\)',text)
    if m:return ('pc',int(m[1],16),0)
    m=re.fullmatch(r'\$([0-9A-Fa-f]+)\(PC,D(\d)\.W\)',text)
    if m:return ('pcindex',int(m[1],16),int(m[2]))
    m=re.fullmatch(r'-\(A(\d)\)',text)
    if m:return ('pre',int(m[1]),0)
    raise ValueError('unsupported EA ' + text)


def extensions(e, size):
    if e[0] == 'i': return 2 if size == 4 else 1
    if e[0] == 'abs': return e[2]
    return 1 if e[0] in ('disp','index','pc','pcindex') else 0


def address(e):
    mode, reg, value = e
    if mode in ('abs','pc'): return hex(reg)
    if mode=='pcindex':return f'0x{reg:x} + int16_t(r.d[{value}])'
    if mode=='pre':return f'(r.a[{reg}]-{2})'
    if mode in ('ind', 'post'): return ('sp' if reg==7 else f'r.a[{reg}]')
    if mode == 'disp': return f'{"sp" if reg==7 else f"r.a[{reg}]"}+({value})'
    if mode == 'index': return f'r.a[{reg}]+({value[0]})+int16_t(r.d[{value[1]}])'
    raise ValueError(e)


cpp, lua, metadata = [], [], []
ranges=[(0x45e,0x556),(0x594,0x62e),(0x648,0x9e4),(0xc0a,0x11d4),(0x16d4,0x1750),(0x1758,0x19e8)]
for start, stop in ranges:
    pc = start
    while pc < stop:
        end, asm = disasm_one(rom, 0, pc)
        if 'unknown' in asm.lower(): raise ValueError(asm)
        lines, phases = [], 0
        fetch = pc + 4
        offset, tail, source, branch_length, variable = 0, 0, 0, 0, 0

        def emit(line): lines.append(line)

        def prefetch(n=1, selection=False, after=False):
            global fetch, phases
            for _ in range(n):
                emit(f'{"after" if after else "final" if selection else "read16"}(0x{fetch:x});'); fetch += 2; phases += 1

        def flags(size, value='value'):
            if size == 2: emit(f'nz({value});')
            else:
                sign = '0x80' if size == 1 else '0x80000000'
                cast = 'uint8_t' if size == 1 else 'uint32_t'
                emit(f'sr=(sr&0xfff0)|({cast}({value})?0:4)|({value}&{sign}?8:0);')

        def read(e, size, name='value', ext=True):
            global phases, offset
            mode, reg, _ = e
            if mode == 'd': emit(f'const auto {name}={"uint8_t" if size==1 else "uint16_t" if size==2 else "uint32_t"}(r.d[{reg}]);'); return
            if mode == 'a': emit(f'const auto {name}={"sp" if reg==7 else f"r.a[{reg}]"};'); return
            if mode == 'i':
                if ext: prefetch(extensions(e, size))
                emit(f'const uint32_t {name}=0x{reg:x};'); return
            if ext:
                if mode == 'index': emit('idle(2);'); offset = 2 if phases==0 else offset
                prefetch(extensions(e, size))
            emit(f'const auto {name}=read{size*8}({address(e)});'); phases += 2 if size == 4 else 1
            if mode == 'post': emit(f'{"sp" if reg==7 else f"r.a[{reg}]"}+={2 if size==1 and reg==7 else size};')

        def write(e, size, name='value', ext=True):
            global phases, offset
            mode, reg, _ = e
            if mode == 'pre':
                target='sp' if reg==7 else f'r.a[{reg}]';emit(f'{target}-={size};');
                if size==4:emit(f'write16({target}+2,{name});write16({target},{name}>>16);');phases+=2
                else:emit(f'write{size*8}({target},{name});');phases+=1
                return
            if mode in ('d', 'a'):
                emit(f'{"byte" if size==1 else "word"}(r.d[{reg}],{name});' if size<4 and mode=='d' else (f'sp={name};' if mode=='a' and reg==7 else f'r.{mode}[{reg}]={name};')); return
            if ext:
                if mode == 'index': emit('idle(2);'); offset = 2 if phases==0 else offset
                prefetch(extensions(e, size))
            if size == 4:
                emit(f'write16({address(e)},{name}>>16);write16({address(e)}+2,{"uint16_t("+name+")" if args.graph!="input" else name});'); phases += 2
            else: emit(f'write{size*8}({address(e)},{name});'); phases += 1
            if mode == 'post': emit(f'{"sp" if reg==7 else f"r.a[{reg}]"}+={2 if size==1 and reg==7 else size};')

        def finish(after=False):
            global phases
            emit(f'{"after" if after else "final"}(0x{end+2:x});next=0x{end:x};'); phases += 1

        op, *rest = asm.split(' ', 1)
        operands = split(rest[0]) if rest else []
        name, _, suffix = op.partition('.')
        size = {'B': 1, 'W': 2, 'L': 4}.get(suffix, 0)
        if name == 'MOVEQ':
            dst=ea(operands[1]); value=int(operands[0][1:])&0xffffffff
            emit(f'r.d[{dst[1]}]=0x{value:x};');flags(4,f'r.d[{dst[1]}]');finish()
        elif name == 'SWAP':
            dst=ea(operands[0]);emit(f'r.d[{dst[1]}]=(r.d[{dst[1]}]<<16)|(r.d[{dst[1]}]>>16);');flags(4,f'r.d[{dst[1]}]');finish()
        elif name in ('MOVE', 'MOVEA'):
            src, dst = map(ea, operands)
            # Indexed and memory-to-memory moves prefetch destination extensions
            # after source data. Absolute-long memory destinations split their
            # two destination extension phases around the write.
            read(src, size)
            if name == 'MOVEA':
                emit(f'r.a[{dst[1]}]={"uint32_t(int16_t(value))" if size==2 else "value"};'); finish()
            elif dst[0]=='pre':
                prefetch();emit('latch();');flags(size);write(dst,size,ext=False);emit(f'next=0x{end:x};')
            elif dst[0] == 'abs' and dst[2] == 2 and src[0] not in ('d', 'a', 'i'):
                prefetch(); flags(size); write(dst, size, ext=False); prefetch(); finish()
            else:
                if dst[0] == 'index': emit('idle(2);'); offset = 2 if phases==0 else offset if phases==0 else offset
                prefetch(extensions(dst, size)); flags(size); write(dst, size, ext=False); finish()
        elif name in ('CLR', 'TST', 'NOT', 'NEG', 'EXT'):
            dst = ea(operands[0]); mem = dst[0] != 'd'
            if name == 'EXT':
                emit(f'word(r.d[{dst[1]}],int8_t(r.d[{dst[1]}]));' if size==2 else f'r.d[{dst[1]}]=uint32_t(int16_t(r.d[{dst[1]}]));'); flags(size, f'r.d[{dst[1]}]'); finish()
            else:
                if name != 'CLR' or mem: read(dst, size, 'old')
                if name == 'CLR':
                    if mem: emit('(void)old;')
                    emit('const uint32_t value=0;'); flags(size)
                elif name == 'TST': flags(size, 'old')
                elif name == 'NOT': emit(f'const auto value=uint{size*8}_t(~old);'); flags(size)
                elif name == 'NEG': emit('const auto value=addsub_word(0,old,true,true);')
                if mem and name != 'TST':
                    prefetch(after=True)
                    if size==4:emit(f'write16({address(dst)}+2,value);write16({address(dst)},value>>16);');phases+=2
                    else:write(dst,size,ext=False)
                    emit(f'next=0x{end:x};')
                else:
                    if name not in ('TST',): write(dst, size, ext=False)
                    finish()
        elif name in ('BTST', 'BCLR', 'BSET'):
            src, dst = map(ea, operands)
            bit_size=4 if dst[0]=='d' else 1
            prefetch(1); prefetch(extensions(dst,1));read(dst,bit_size,'old',ext=False)
            emit(f'sr=(sr&~4)|(old&0x{1<<src[1]:x}?0:4);')
            if dst[0]=='d':
                if name!='BTST':write(dst,4,f'old{ "&~" if name=="BCLR" else "|"}0x{1<<src[1]:x}',ext=False)
                tail=2 if name=='BTST' else (2 if name=='BSET' else 4)+(2 if src[1]&16 else 0)
                finish(after=True);emit(f'idle({tail});')
            elif name in ('BCLR','BSET'):
                emit(f'const auto value=old{"&~" if name=="BCLR" else "|"}0x{1<<src[1]:x};');prefetch(after=True);write(dst,1,ext=False);emit(f'next=0x{end:x};')
            else:finish()
        elif name in ('ADD', 'SUB', 'CMP', 'ADDI', 'SUBI', 'CMPI', 'ADDQ', 'SUBQ', 'ANDI', 'ORI','AND','OR','SUBX'):
            src, dst = map(ea, operands)
            if name not in ('ADDQ', 'SUBQ'): read(src, size, 'operand')
            else: emit(f'const uint32_t operand={src[1]};')
            if dst[0]=='a':
                emit(f'{"sp" if dst[1]==7 else f"r.a[{dst[1]}]"}+={"-" if name=='SUBQ' else ""}operand;');prefetch(after=True);emit('idle(4);');emit(f'next=0x{end:x};');tail=4
                cpp += [f'// ${pc:06X}: {asm}',f'case 0x{pc:x}: {{',*['    '+s for s in lines],'    break;','}']
                lua.append(f' [0x{pc:x}]={{phases={phases},offset={offset},tail={tail},source=0,branch=0,variable=0}},')
                metadata.append(f'    {{0x{pc:x},{phases},{offset},{tail},0,0,0}},');pc=end;continue
            read(('ind',dst[1],0) if dst[0]=='post' else dst,size,'old')
            if name in ('ANDI','ORI','AND','OR'): emit('const auto value=old'+('&' if name in ('ANDI','AND') else '|')+'operand;'); flags(size)
            elif name=='SUBX':emit('const auto value=subx_word(old,operand);')
            else: emit(f'{"" if name.startswith("CMP") else "const auto value="}addsub_{'long' if size==4 else 'byte' if size==1 else 'word'}(old,operand,{"true" if name.startswith(("SUB", "CMP")) else "false"},{"false" if name.startswith("CMP") else "true"});')
            if dst[0] == 'd':
                if not name.startswith('CMP'): write(dst, size, ext=False)
                if size==4:
                    prefetch(after=True);emit('idle(2);' if name.startswith('CMP') else 'idle(4);');emit(f'next=0x{end:x};');tail=2 if name.startswith('CMP') else 4;source=dst[1]
                else: finish()
            else:
                if name.startswith('CMP'): finish()
                else:
                    prefetch(after=True)
                    if size==4:
                        emit(f'write16({address(dst)}+2,value);write16({address(dst)},value>>16);');phases+=2
                        if dst[0]=='post':emit(f'r.a[{dst[1]}]+=4;')
                    else:write(dst,size,ext=False)
                    emit(f'next=0x{end:x};')
        elif name in ('ADDA','SUBA'):
            src,dst=map(ea,operands);read(src,size)
            emit(f'r.a[{dst[1]}]+={"-" if name=='SUBA' else ""}int16_t(value);');prefetch(after=True);emit('idle(4);');emit(f'next=0x{end:x};');tail=4
        elif name == 'LEA':
            src, dst = map(ea, operands);emit(f'r.a[{dst[1]}]={address(src)};');
            if src[0] in ('index','pcindex'):emit('idle(2);');offset=2
            prefetch(extensions(src,2));
            if src[0] in ('index','pcindex'):emit('idle(2);')
            finish()
        elif name in ('ASR','ASL','LSR','LSL','ROR','ROL'):
            src,dst=map(ea,operands);assert size in (2,4) and src[0] in ('i','d')
            count=str(src[1]) if src[0]=='i' else f'(r.d[{src[1]}]&63)'
            emit(f'shift({dst[1]},{count},{size},Shift::{name});');phases=1;source=dst[1]
            tail=(2 if size==2 else 4)+(2*src[1] if src[0]=='i' else 0);variable=src[1]+1 if src[0]=='d' else 0
        elif name=='EXG':
            src,dst=map(ea,operands);emit(f'std::swap(r.d[{src[1]}],r.d[{dst[1]}]);');finish(after=True);emit('idle(2);');tail=2
        elif name=='DBF':
            reg=ea(operands[0])[1];target=int(operands[1][1:],16)
            emit(f'idle(2);read16(0x{target:x});const auto count=uint16_t(r.d[{reg}]-1);word(r.d[{reg}],count);')
            emit(f'if(count!=0xffff){{final(0x{target+2:x});next=0x{target:x};}}else{{read16(0x{end:x});final(0x{end+2:x});next=0x{end:x};}}');phases=3;offset=2;branch_length=-2;source=reg
        elif name=='PEA':
            src=ea(operands[0]);prefetch();prefetch();emit(f'sp-=4;write16(sp,0x{src[1]>>16:x});latch();write16(sp+2,0x{src[1]&65535:x});next=0x{end:x};');phases=4
        elif name=='JMP':
            src=ea(operands[0]);emit('idle(6);');emit(f'next={address(src)};read16(next);final(next+2);');offset=6;phases=2
        elif name=='TAS':
            dst=ea(operands[0]);read(dst,1,'old');flags(1,'old');emit('idle(2);');write(dst,1,'old|0x80',ext=False);finish()
        elif name == 'RTS':
            emit('next=read32(sp);sp+=4;read16(next);final(next+2);');phases=4
        elif name == 'BSR':
            target = int(operands[0][1:],16)
            emit(f'idle(2);sp-=4;write16(sp,0x{end>>16:x});write16(sp+2,0x{end&65535:x});read16(0x{target:x});final(0x{target+2:x});next=0x{target:x};');phases=4;offset=2
        elif name.startswith('B'):
            target = int(operands[0][1:],16)
            conditions={'BT':'true','BNE':'!(sr&4)','BEQ':'sr&4','BLE':'(sr&4)||bool(sr&8)!=bool(sr&2)','BGE':'bool(sr&8)==bool(sr&2)','BGT':'!(sr&4)&&bool(sr&8)==bool(sr&2)','BPL':'!(sr&8)','BMI':'sr&8','BLT':'bool(sr&8)!=bool(sr&2)','BCC':'!(sr&1)','BCS':'sr&1'}
            emit(f'branch({conditions[name]},0x{target:x},{end-pc});');phases=2;offset=-1;branch_length=end-pc
        else: raise ValueError(f'{pc:x}: unsupported {asm}')
        cpp += [f'// ${pc:06X}: {asm}',f'case 0x{pc:x}: {{',*['    '+s for s in lines],'    break;','}']
        extra_lua=f',variable={variable}'
        extra_cpp=f',{variable}'
        lua.append(f' [0x{pc:x}]={{phases={phases},offset={offset},tail={tail},source={source},branch={branch_length},condition="{name}",next=0x{end:x}{extra_lua}}},')
        metadata.append(f'    {{0x{pc:x},{phases},{offset},{tail},{source},{branch_length}{extra_cpp}}},')
        pc = end

outputs={
'src/main/engine/osubprogress.inc':'// Generated by tools/generate_sub_schedule.py; no ROM data.\n'+'\n'.join(cpp)+'\n',
 'tools/oracle/sub_schedule.lua':'-- Generated bounded instruction manifest; no ROM data.\nreturn {\n'+'\n'.join(lua)+'\n}\n',
 'src/main/engine/osubprogress_manifest.hpp':'#pragma once\n#include <array>\n#include <cstdint>\nstruct OSubInstruction { uint32_t pc; unsigned phases; int offset; unsigned tail,source; int branch; unsigned variable; };\ninline constexpr std::array<OSubInstruction,'+str(len(metadata))+'> sub_instructions{{\n'+'\n'.join(metadata)+'\n}};\ninline const OSubInstruction* sub_instruction(uint32_t pc){for(const auto& i:sub_instructions)if(i.pc==pc)return &i;return nullptr;}\n'
}
for path,text in outputs.items():
 if args.check:
  if Path(path).read_text()!=text:raise SystemExit('generated file differs: '+path)
 else:Path(path).write_text(text)
print(f'{len(metadata)} bounded sub instructions; '+('generated files match' if args.check else 'generated translation and capture manifests'))
