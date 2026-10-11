#!/usr/bin/env python3
"""Emit bounded, address-labelled caller/IRQ-wrapper instructions from the user's ROM image.

No runtime opcode decoder or ROM data is generated. The supported forms here
are the selected graph's instruction subset. Unknown forms fail rather than guess.
Usage: python3 tools/generate_input_schedule.py /tmp/main.bin [--check]
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
parser.add_argument('--graph', choices=['input','flight','companion','weapons','host','irqwrapper','producers','callees','remaining','boot','recovery'], default='input')
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
    m = re.fullmatch(r'\$(\w+)\(PC\)', text)
    if m: return ('pc', int(m[1],16), 0)
    m = re.fullmatch(r'\$(\w+)\(PC,D(\d)\.W\)', text)
    if m:return ('pcindex',int(m[1],16),int(m[2]))
    m = re.fullmatch(r'-\(A(\d)\)', text)
    if m: return ('pre', int(m[1]), 0)
    m = re.fullmatch(r'(-?\d+)\(A(\d)\)', text)
    if m: return ('disp', int(m[2]), int(m[1]))
    m = re.fullmatch(r'\(A(\d)\)(\+?)', text)
    if m: return ('post' if m[2] else 'ind', int(m[1]), 0)
    m = re.fullmatch(r'(-?\d+)\(A(\d),D(\d)\.W\)', text)
    if m: return ('index', int(m[2]), (int(m[1]), int(m[3])))
    m = re.fullmatch(r'(-?\d+)\(A(\d),D(\d)\.L\)', text)
    if m:return ('indexlong',int(m[2]),(int(m[1]),int(m[3])))
    raise ValueError('unsupported EA ' + text)


def extensions(e, size):
    if e[0] == 'i': return 2 if size == 4 else 1
    if e[0] == 'abs': return e[2]
    return 1 if e[0] in ('disp', 'index', 'pc','pcindex','indexlong') else 0


def address(e):
    mode, reg, value = e
    if mode in ('abs','pc'): return hex(reg)
    if mode=='pcindex':return f'0x{reg:x}+int16_t(r.d[{value}])'
    if mode in ('ind', 'post', 'pre'): return f'r.a[{reg}]'
    if mode == 'disp': return f'r.a[{reg}]+({value})'
    if mode == 'index': return f'r.a[{reg}]+({value[0]})+int16_t(r.d[{value[1]}])'
    if mode=='indexlong':return f'r.a[{reg}]+({value[0]})+r.d[{value[1]}]'
    raise ValueError(e)


cpp, lua, metadata = [], [], []
ranges = [(0xb0ac, 0xb36a), (0x14976, 0x14990), (0xd0de, 0xd0e2)] if args.graph=='input' else [(0x98c0,0x9c34),(0x427c,0x4284),(0x5d68,0x5d6e),(0x60ca,0x60d0),(0x7650,0x7656),(0x18e8a,0x18e8e)]
if args.graph=='companion': ranges=[(0x9c34,0x9d2c),(0xae8c,0xb0aa),(0x77ac,0x77ae)]
if args.graph=='host': ranges=[(0x9626,0x98c0),(0xcfc4,0xd0a0),(0x9f7c,0xa030),(0x6844,0x684c),(0x14ee8,0x14ef0)]
if args.graph=='weapons': ranges=[(0x9d34,0x9dec),(0x9df8,0x9f08)]
if args.graph=='callees':
    known=[]
    source=Path('src/main/engine/omainirqservice.cpp').read_text()
    known+=re.findall(r'case 0x([0-9a-f]+):',source[source.index('OMainIrqService::Instruction OMainIrqService::caller_step'):])
    for graph in ('input','flight','companion','weapon','hostcaller','irqwrapper','producer'):
        known+=re.findall(r'case 0x([0-9a-f]+):',Path(f'src/main/engine/omain{graph}schedule.inc').read_text())
    known={int(p,16) for p in known}
    pcs=set()
    for name in ('omainirqcallee.inc','omainirqgameflow.inc'):
        pcs.update(int(p,16) for p in re.findall(r'case 0x([0-9a-f]+):',Path('src/main/engine/'+name).read_text()))
    ranges=[(p,disasm_one(rom,0,p)[0]) for p in sorted(pcs-known)]
if args.graph=='producers': ranges=[(0xb7c2,0xb7f8),(0x1487a,0x148fc),(0x1495c,0x14976),(0x18902,0x18918)]
if args.graph in ('remaining','boot','recovery'):
    pcs=[int(p,16) for p in Path('tools/oracle/main_'+args.graph+'_pcs.txt').read_text().splitlines() if p and not p.startswith('#')]
    ranges=[(p,disasm_one(rom,0,p)[0]) for p in pcs]
if args.graph=='irqwrapper': ranges=[(0x1461c,0x147c6),(0xb7f8,0xb840)]
for start, stop in ranges:
    pc = start
    while pc < stop:
        end, asm = disasm_one(rom, 0, pc)
        if 'unknown' in asm.lower(): raise ValueError(asm)
        # RTE has its own exception/stack-bank owner rather than a normal
        # instruction case. Device instructions use the joint bus endpoints.
        if args.graph=='irqwrapper' and pc in (0x1466a,0x14672):
            pc=end
            continue
        lines, phases = [], 0
        fetch = pc + 4
        offset, tail, source, branch_length, variable = 0, 0, 0, 0, 0

        def emit(line): lines.append(re.sub(r'(0x[0-9a-f]*e)\+',r'\1 + ',line))

        def prefetch(n=1):
            global fetch, phases
            for _ in range(n):
                emit(f'read16(0x{fetch:x});'); fetch += 2; phases += 1

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
            if mode == 'a': emit(f'const auto {name}=r.a[{reg}];'); return
            if mode == 'i':
                if ext: prefetch(extensions(e, size))
                emit(f'const uint32_t {name}=0x{reg:x};'); return
            if ext:
                if mode in ('index','pcindex','indexlong'): emit('idle(2);'); offset = 2
                prefetch(extensions(e, size))
            if mode=='pre': emit(f'idle(2);r.a[{reg}]-={2 if size==1 and reg==7 else size};'); offset=2
            emit(f'const auto {name}=read{size*8}({address(e)});'); phases += 2 if size == 4 else 1
            if mode == 'post': emit(f'r.a[{reg}]+={2 if size==1 and reg==7 else size};')

        def write(e, size, name='value', ext=True):
            global phases, offset
            mode, reg, _ = e
            if mode in ('d', 'a'):
                emit(f'{"byte" if size==1 else "word"}(r.d[{reg}],{name});' if size<4 and mode=='d' else f'r.{mode}[{reg}]={name};'); return
            if ext:
                if mode in ('index','pcindex','indexlong'): emit('idle(2);'); offset = 2
                prefetch(extensions(e, size))
            if size == 4:
                emit(f'write16({address(e)},{name}>>16);write16({address(e)}+2,{"uint16_t("+name+")" if args.graph!="input" else name});'); phases += 2
            else: emit(f'write{size*8}({address(e)},{name});'); phases += 1
            if mode == 'post': emit(f'r.a[{reg}]+={2 if size==1 and reg==7 else size};')

        def finish():
            global phases
            emit(f'final(0x{end+2:x});next=0x{end:x};'); phases += 1

        op, *rest = asm.split(' ', 1)
        operands = split(rest[0]) if rest else []
        name, _, suffix = op.partition('.')
        size = {'B': 1, 'W': 2, 'L': 4}.get(suffix, 0)
        if name == 'NOP':
            finish()
        elif name == 'PEA':
            src=ea(operands[0]);assert src[0]=='abs' and src[2]==2
            prefetch(2);emit('sp-=4;');emit(f'write16(sp,0x{src[1]>>16:x});write16(sp+2,0x{src[1]&65535:x});');phases+=2
            finish()
        elif name == 'MOVEQ':
            dst=ea(operands[1]); value=int(operands[0][1:])&0xffffffff
            emit(f'r.d[{dst[1]}]=0x{value:x};');flags(4,f'r.d[{dst[1]}]');finish()
        elif name == 'SWAP':
            dst=ea(operands[0]);emit(f'r.d[{dst[1]}]=(r.d[{dst[1]}]<<16)|(r.d[{dst[1]}]>>16);');flags(4,f'r.d[{dst[1]}]');finish()
        elif name=='EXG':
            src,dst=map(ea,operands);assert src[0] in ('d','a') and dst[0] in ('d','a')
            emit(f'std::swap(r.{src[0]}[{src[1]}],r.{dst[0]}[{dst[1]}]);');prefetch();emit(f'latch();idle(2);next=0x{end:x};');tail=2
        elif name=='CMPA':
            src,dst=map(ea,operands);assert dst[0]=='a'
            read(src,size,'operand');emit(f'addsub_long(r.a[{dst[1]}],{"uint32_t(int16_t(operand))" if size==2 else "operand"},true,false);')
            prefetch();emit(f'latch();idle(2);next=0x{end:x};');tail=2
        elif name in ('MOVE', 'MOVEA'):
            src, dst = map(ea, operands)
            # Indexed and memory-to-memory moves prefetch destination extensions
            # after source data. Absolute-long memory destinations split their
            # two destination extension phases around the write.
            read(src, size)
            if dst[0]=='pre':
                assert args.graph in ('irqwrapper','callees','remaining','boot','recovery') and name=='MOVE' and size in (1,2,4)
                prefetch();emit('latch();');flags(size)
                emit(f'r.a[{dst[1]}]-={2 if size==1 and dst[1]==7 else size};')
                if size==4:emit(f'write16(r.a[{dst[1]}]+2,value);write16(r.a[{dst[1]}],value>>16);');phases+=2
                else:emit(f'write{size*8}(r.a[{dst[1]}],value);');phases+=1
                emit(f'next=0x{end:x};')
            elif name == 'MOVEA':
                emit(f'r.a[{dst[1]}]={"uint32_t(int16_t(value))" if size==2 else "value"};'); finish()
            elif dst[0] == 'abs' and dst[2] == 2 and src[0] not in ('d', 'a', 'i'):
                prefetch(); flags(size); write(dst, size, ext=False); prefetch(); finish()
            else:
                if dst[0] == 'index': emit('idle(2);'); offset = 2
                prefetch(extensions(dst, size)); flags(size); write(dst, size, ext=False); finish()
        elif name == 'TAS':
            dst=ea(operands[0]);assert dst[0] in ('d','ind')
            read(dst,1,'old')
            if dst[0]=='ind':
                emit('idle(2);');offset=2
                flags(1,'old');write(dst,1,'old|0x80',ext=False)
            else:
                flags(1,'old');emit(f'byte(r.d[{dst[1]}],old|0x80);')
            finish()
        elif name == 'ST':
            dst=ea(operands[0]);assert dst[0]=='disp'
            read(dst,1,'old');emit('(void)old;');prefetch();emit('latch();')
            write(dst,1,'0xff',ext=False);emit(f'next=0x{end:x};')
        elif name in ('CLR', 'TST', 'NOT', 'NEG', 'EXT'):
            dst = ea(operands[0]); mem = dst[0] != 'd'
            if name == 'EXT':
                emit(f'word(r.d[{dst[1]}],int8_t(r.d[{dst[1]}]));' if size==2 else f'r.d[{dst[1]}]=uint32_t(int16_t(r.d[{dst[1]}]));'); flags(size, f'r.d[{dst[1]}]'); finish()
            else:
                if name != 'CLR' or mem: read(('ind',dst[1],0) if dst[0]=='post' and name!='TST' else dst, size, 'old')
                if name == 'CLR':
                    if mem: emit('(void)old;')
                    emit('const uint32_t value=0;'); flags(size)
                elif name == 'TST': flags(size, 'old')
                elif name == 'NOT': emit(f'const auto value=uint{size*8}_t(~old);'); flags(size)
                elif name == 'NEG': emit(f'const auto value=addsub_{"long" if size==4 else "byte" if size==1 else "word"}(0,old,true,true);')
                if mem and name != 'TST':
                    prefetch(); emit('latch();');
                    if name=='CLR' and size==4:
                        emit(f'write16({address(dst)}+2,0);write16({address(dst)},0);');phases+=2
                        if dst[0]=='post':emit(f'r.a[{dst[1]}]+=4;')
                    else: write(dst, size, ext=False)
                    emit(f'next=0x{end:x};')
                else:
                    if name not in ('TST',): write(dst, size, ext=False)
                    if size==4 and name in ('NEG','CLR'):
                        prefetch();emit(f'latch();idle(2);next=0x{end:x};');tail=2
                    else:finish()
        elif name in ('BTST', 'BCLR', 'BSET','BCHG'):
            src, dst = map(ea, operands)
            if dst[0]=='d' and args.graph in ('callees','remaining','boot','recovery'):
                bit=str(src[1]&31) if src[0]=='i' else f'(r.d[{src[1]}]&31)'
                emit(f'const auto bit={bit};const auto old=r.d[{dst[1]}];')
                if src[0]=='i':prefetch()
                prefetch()
                if name!='BTST':emit('if(bit&16)idle(2);')
                if name=='BCLR':emit('idle(2);')
                emit('latch();')
                emit('sr=(sr&~4)|(old&(1u<<bit)?0:4);')
                if name!='BTST':emit(f'r.d[{dst[1]}]=old'+('|' if name=='BSET' else '&~' if name=='BCLR' else '^')+'(1u<<bit);')
                emit(f'idle(2);next=0x{end:x};');tail=2
            elif dst[0]=='d':
                assert name=='BTST'
                prefetch(1);prefetch();emit(f'sr=(sr&~4)|(r.d[{dst[1]}]&0x{1<<src[1]:x}?0:4);');emit('latch();idle(2);');emit(f'next=0x{end:x};');tail=2;source=dst[1]
            else:
                if src[0]=='i':prefetch(1)
                else:emit(f'const auto bitmask=1u<<(r.d[{src[1]}]&7);')
                if dst[0] in ('index','pcindex','indexlong'):
                    emit('idle(2);');offset=2
                prefetch(extensions(dst, 1)); read(dst, 1, 'old', ext=False)
            if dst[0]!='d':
                bitmask=f'0x{1<<src[1]:x}' if src[0]=='i' else 'bitmask'
                emit(f'sr=(sr&~4)|(old&{bitmask}?0:4);')
                if name in ('BCLR','BSET','BCHG'):
                    emit(f'const auto value=old{"&~" if name=="BCLR" else "^" if name=="BCHG" else "|"}{bitmask};'); prefetch(); emit('latch();'); write(dst, 1, ext=False); emit(f'next=0x{end:x};')
                else: finish()
        elif name in ('ABCD','SBCD'):
            src,dst=map(ea,operands)
            assert (src[0],dst[0]) in (('d','d'),('pre','pre')), f'{pc:x} {asm}'
            memory=src[0]=='pre'
            emit(f'bcd_{"memory" if memory else "register"}({src[1]},{dst[1]},{"true" if name=="SBCD" else "false"});')
            phases=4 if memory else 1;offset=2 if memory else 0;tail=0 if memory else 2
        elif name=='ANDI' and operands[1]=='CCR':
            immediate=ea(operands[0])[1]
            emit(f'read16(0x{pc+4:x});idle(8);sr=(sr&0xa700)|((sr&0x1f)&0x{immediate:x});read16(0x{pc+4:x});final(0x{pc+6:x});next=0x{end:x};');phases=3
        elif name in ('ADDQ','SUBQ') and ea(operands[1])[0]=='a':
            src,dst=map(ea,operands);emit(f'r.a[{dst[1]}]{"+=" if name=="ADDQ" else "-="}{src[1]};');prefetch();emit(f'idle(2);latch();idle(2);next=0x{end:x};');tail=4
        elif name in ('ADD', 'SUB', 'CMP', 'ADDI', 'SUBI', 'CMPI', 'ADDQ', 'SUBQ', 'ANDI', 'ORI', 'OR', 'AND', 'EOR', 'EORI'):
            src, dst = map(ea, operands)
            if name not in ('ADDQ', 'SUBQ'): read(src, size, 'operand')
            else: emit(f'const uint32_t operand={src[1]};')
            read(('ind',dst[1],0) if dst[0]=='post' and not name.startswith('CMP') else dst, size, 'old')
            if name in ('ANDI','ORI','OR','AND','EOR','EORI'): emit('const auto value=old'+('&' if name in ('ANDI','AND') else '^' if name in ('EOR','EORI') else '|')+'operand;'); flags(size)
            else: emit(f'{"" if name.startswith("CMP") else "const auto value="}addsub_{'long' if size==4 else 'byte' if size==1 else 'word'}(old,operand,{"true" if name.startswith(("SUB", "CMP")) else "false"},{"false" if name.startswith("CMP") else "true"});')
            if dst[0] == 'd':
                if not name.startswith('CMP'): write(dst, size, ext=False)
                if size==4:
                    prefetch()
                    if name.startswith('CMP') or src[0] not in ('d','a','i'):
                        emit('latch();idle(2);');tail=2
                    else:emit('idle(2);latch();idle(2);');tail=4
                    emit(f'next=0x{end:x};');source=dst[1]
                else: finish()
            else:
                if name.startswith('CMP'): finish()
                else:
                    prefetch(); emit('latch();')
                    if size==4:
                        emit(f'write16({address(dst)}+2,value);write16({address(dst)},value>>16);');phases+=2
                        if dst[0]=='post':emit(f'r.a[{dst[1]}]+=4;')
                    else: write(dst, size, ext=False)
                    emit(f'next=0x{end:x};')
        elif name in ('ADDA','SUBA'):
            src,dst=map(ea,operands);assert dst[0]=='a'
            read(src,size,'operand');delta='int16_t(operand)' if size==2 else 'operand'
            emit(f'r.a[{dst[1]}]{"-=" if name=="SUBA" else "+="}{delta};');prefetch()
            extra=4 if size==2 or src[0] in ('d','a','i') else 2
            if extra==4:emit('idle(2);latch();idle(2);');tail=4
            else:emit('latch();idle(2);');tail=2
            emit(f'next=0x{end:x};');source=src[1] if src[0]=='d' else 0
        elif name == 'LEA':
            src, dst = map(ea, operands)
            if src[0] in ('index','indexlong','pcindex'):emit('idle(2);');offset=2
            emit(f'r.a[{dst[1]}]={address(src)};');prefetch(extensions(src,2))
            if src[0] in ('index','indexlong','pcindex'):emit('idle(2);')
            finish()
        elif name == 'ASL' and len(operands)==1:
            dst=ea(operands[0]);assert size==2 and dst[0]=='disp'
            read(dst,2,'old');emit('const uint16_t value=old<<1;')
            emit('sr=(sr&0xffe0)|(old&0x8000?17:0)|((old^value)&0x8000?2:0)|(value?0:4)|(value&0x8000?8:0);')
            prefetch();emit('latch();');write(dst,2,ext=False);emit(f'next=0x{end:x};')
        elif name in ('ASR','ASL'):
            src,dst=map(ea,operands);assert size in (2,4) and src[0] in ('i','d')
            count=str(src[1]) if src[0]=='i' else f'(r.d[{src[1]}]&63)'
            emit(f'{name.lower()}_{"word" if size==2 else "long"}({dst[1]},{count});');phases=1;source=dst[1]
            tail=(2 if size==2 else 4)+(2*src[1] if src[0]=='i' else 0);variable=src[1]+1 if src[0]=='d' else 0
        elif name in ('LSR','LSL','ROR','ROL'):
            if len(operands)==1:
                dst=ea(operands[0]);assert name=='ROR' and size==2 and dst[0]=='abs'
                read(dst,2,'old');emit('const uint16_t value=(old>>1)|(old<<15);sr=(sr&0xfff0)|(value?0:4)|(value&0x8000?8:0)|(old&1);')
                prefetch();emit('latch();');write(dst,2,ext=False);emit(f'next=0x{end:x};')
            else:
                src,dst=map(ea,operands);assert src[0] in ('i','d') and dst[0]=='d'
                count=str(src[1]) if src[0]=='i' else f'(r.d[{src[1]}]&63)'
                emit(f'logical_shift({dst[1]},{size},{count},{"true" if name in ("LSL","ROL") else "false"},{"true" if name in ("ROR","ROL") else "false"});')
                phases=1;tail=(4 if size==4 else 2)+(2*src[1] if src[0]=='i' else 0);source=dst[1]
        elif name in ('MULU','MULS'):
            src,dst=map(ea,operands);assert dst[0]=='d'
            if name=='MULS':assert args.graph=='recovery' and src[0]=='i'
            read(src,2,'operand');emit(f'multiply_{"unsigned" if name=="MULU" else "signed"}({dst[1]},operand,0x{end:x});');phases+=1
        elif name == 'DIVU':
            src,dst=map(ea,operands)
            if args.graph in ('callees','remaining','boot','recovery'):
                assert dst[0]=='d';read(src,2,'operand');emit(f'divide_unsigned({dst[1]},operand,0x{end:x});');phases+=1
            else:
                assert src==('d',1,0) and dst==('d',0,0);emit('decimal_divu();');phases=1
            offset=-2
        elif name == 'DBEQ':
            dst=ea(operands[0]);target=int(operands[1][1:],16)
            emit(f'if(sr&4){{idle(4);read16(0x{pc+4:x});final(0x{pc+6:x});next=0x{end:x};}}else{{')
            emit(f'const auto old=uint16_t(r.d[{dst[1]}]);idle(2);read16(0x{target:x});word(r.d[{dst[1]}],old-1);')
            emit(f'if(old){{final(0x{target+2:x});next=0x{target:x};}}else{{read16(0x{pc+4:x});final(0x{pc+6:x});next=0x{end:x};}}}}');phases=2;branch_length=end-pc;offset=-1
        elif name == 'DBF':
            dst=ea(operands[0]);target=int(operands[1][1:],16)
            emit(f'const auto old=uint16_t(r.d[{dst[1]}]);idle(2);read16(0x{target:x});word(r.d[{dst[1]}],old-1);')
            emit(f'if(old){{final(0x{target+2:x});next=0x{target:x};}}else{{read16(0x{pc+4:x});final(0x{pc+6:x});next=0x{end:x};}}')
            phases=2;offset=2;branch_length=end-pc
        elif name == 'JMP':
            target=ea(operands[0]);assert target[0] in ('ind','disp','index','abs','pc','pcindex','indexlong')
            if target[0]=='abs' and target[2]==2:prefetch()
            else:
                wait=6 if target[0] in ('index','pcindex','indexlong') else 0 if target[0]=='ind' else 2
                if wait:emit(f'idle({wait});');offset=wait
            emit(f'next={address(target)};read16(next);final(next+2);');phases+=2
        elif name == 'JSR':
            target=ea(operands[0]);assert target[0] in ('ind','disp','index','indexlong','pc','pcindex','abs')
            if target[0]=='abs' and target[2]==2:
                emit(f'read16(0x{pc+4:x});read16(0x{target[1]:x});sp-=4;write16(sp,0x{end>>16:x});write16(sp+2,0x{end&65535:x});final(0x{target[1]+2:x});next=0x{target[1]:x};');phases=5
            else:
                wait=0 if target[0]=='ind' else 6 if target[0] in ('index','indexlong','pcindex') else 2
                emit(f'const uint32_t target={address(target)};')
                if wait:emit(f'idle({wait});');offset=wait
                emit(f'read16(target);sp-=4;write16(sp,0x{end>>16:x});write16(sp+2,0x{end&65535:x});final(target+2);next=target;');phases=4
        elif name == 'RTS':
            emit('next=read32(sp);sp+=4;read16(next);final(next+2);');phases=4
        elif name == 'MOVEM':
            assert args.graph in ('irqwrapper','callees','remaining','boot','recovery') and size==4
            prefetch()
            saving=operands[1]=='-(A7)'
            assert saving or operands[0]=='(A7)+'
            registers=(operands[0] if saving else operands[1]).split('/')
            for register in registers:
                assert re.fullmatch('[DA][0-6]' if register.startswith('A') else 'D[0-7]',register)
                target=f'r.{register[0].lower()}[{register[1]}]'
                if saving: emit(f'write16(sp-2,{target});write16(sp-4,{target}>>16);sp-=4;')
                else: emit(f'{target}=read32(sp);sp+=4;')
                phases+=2
            if not saving:emit('read16(sp);');phases+=1
            finish()
        elif name == 'BSR':
            target = int(operands[0][1:],16)
            emit(f'idle(2);sp-=4;write16(sp,0x{end>>16:x});write16(sp+2,0x{end&65535:x});read16(0x{target:x});final(0x{target+2:x});next=0x{target:x};');phases=4;offset=2
        elif name.startswith('B'):
            target = int(operands[0][1:],16)
            conditions={'BT':'true','BNE':'!(sr&4)','BEQ':'sr&4','BCS':'sr&1','BCC':'!(sr&1)','BVC':'!(sr&2)','BHI':'!(sr&5)','BLE':'(sr&4)||bool(sr&8)!=bool(sr&2)','BGE':'bool(sr&8)==bool(sr&2)','BGT':'!(sr&4)&&bool(sr&8)==bool(sr&2)','BPL':'!(sr&8)','BLT':'bool(sr&8)!=bool(sr&2)','BMI':'sr&8'}
            emit(f'branch({conditions[name]},0x{target:x},{end-pc});');phases=2;offset=-1;branch_length=end-pc
        else: raise ValueError(f'{pc:x}: unsupported {asm}')
        cpp += [f'// ${pc:06X}: {asm}',f'case 0x{pc:x}: {{',*['    '+s.replace('r.a[7]','sp') for s in lines],'    break;','}']
        extra_lua=f',variable={variable}' if args.graph=='flight' else ''
        extra_cpp=f',{variable}' if args.graph=='flight' else ''
        lua.append(f' [0x{pc:x}]={{phases={phases},offset={offset},tail={tail},source={source},branch={branch_length}{extra_lua}}},')
        metadata.append(f'    {{0x{pc:x},{phases},{offset},{tail},{source},{branch_length}{extra_cpp}}},')
        pc = end

outputs = {
    'src/main/engine/omaininputschedule.inc': '// Generated by tools/generate_input_schedule.py; no ROM data.\n'+'\n'.join(cpp)+'\n',
    'tools/oracle/input_schedule.lua': '-- Generated instruction manifest; scalar addresses/timing, no ROM data.\nreturn {\n'+'\n'.join(lua)+'\n}\n',
    'src/main/engine/omaininputschedule.hpp': '#pragma once\n#include <array>\n#include <cstdint>\n// Generated instruction manifest; shared capture/coverage definitions.\nstruct OMainInputInstruction { uint32_t pc; unsigned phases; int offset; unsigned tail,source,branch; };\ninline constexpr std::array<OMainInputInstruction,'+str(len(metadata))+'> main_input_instructions{{\n'+'\n'.join(metadata)+'\n}};\ninline const OMainInputInstruction* main_input_instruction(uint32_t pc) {\n    for(const auto& i:main_input_instructions)if(i.pc==pc)return &i;return nullptr;\n}\n',
}
if args.graph=='flight':
    outputs={path.replace('input','flight'):text.replace('OMainInputInstruction','OMainFlightInstruction').replace('main_input','main_flight') for path,text in outputs.items()}
    header='src/main/engine/omainflightschedule.hpp'
    outputs[header]=outputs[header].replace('tail,source,branch;','tail,source,branch,variable;')
    core=[line for line in metadata if 0x98c0<=int(re.search(r'0x([0-9a-f]+)',line)[1],16)<0x9c34]
    returns=[line for line in metadata if line not in core]
    outputs[header]=outputs[header].replace('OMainFlightInstruction,'+str(len(metadata)),'OMainFlightInstruction,'+str(len(core)))
    outputs[header]=outputs[header].replace('\n'.join(metadata),'\n'.join(core))
    marker='inline const OMainFlightInstruction* main_flight_instruction'
    table='inline constexpr std::array<OMainFlightInstruction,'+str(len(returns))+'> main_flight_return_instructions{{\n'+'\n'.join(returns)+'\n}};\n'
    outputs[header]=outputs[header].replace(marker,table+marker)
    outputs[header]=outputs[header].replace('if(i.pc==pc)return &i;return nullptr;', 'if(i.pc==pc)return &i;\n    for(const auto& i:main_flight_return_instructions)if(i.pc==pc)return &i;return nullptr;')

if args.graph=='companion':
    outputs={path.replace('input','companion'):text.replace('OMainInputInstruction','OMainCompanionInstruction').replace('main_input','main_companion') for path,text in outputs.items()}
    header='src/main/engine/omaincompanionschedule.hpp'
    core=[line for line in metadata if int(re.search(r'0x([0-9a-f]+)',line)[1],16)!=0x77ac]
    returns=[line for line in metadata if line not in core]
    outputs[header]=outputs[header].replace('OMainCompanionInstruction,'+str(len(metadata)),'OMainCompanionInstruction,'+str(len(core)))
    outputs[header]=outputs[header].replace('\n'.join(metadata),'\n'.join(core))
    marker='inline const OMainCompanionInstruction* main_companion_instruction'
    table='inline constexpr std::array<OMainCompanionInstruction,'+str(len(returns))+'> main_companion_return_instructions{{\n'+'\n'.join(returns)+'\n}};\n'
    outputs[header]=outputs[header].replace(marker,table+marker)
    outputs[header]=outputs[header].replace('if(i.pc==pc)return &i;return nullptr;', 'if(i.pc==pc)return &i;\n    for(const auto& i:main_companion_return_instructions)if(i.pc==pc)return &i;return nullptr;')

if args.graph=='weapons':
    outputs={path.replace('input','weapon'):text.replace('OMainInputInstruction','OMainWeaponInstruction').replace('main_input','main_weapon') for path,text in outputs.items()}

if args.graph=='host':
    outputs={path.replace('input','hostcaller'):text.replace('OMainInputInstruction','OMainHostInstruction').replace('main_input','main_host') for path,text in outputs.items()}

if args.graph=='callees':
    outputs={path.replace('input','irqjointcallee'):text.replace('OMainInputInstruction','OMainJointCalleeInstruction').replace('main_input','main_irqjointcallee') for path,text in outputs.items()}
if args.graph=='remaining':
    outputs={path.replace('input','remaining'):text.replace('OMainInputInstruction','OMainRemainingInstruction').replace('main_input','main_remaining') for path,text in outputs.items()}

if args.graph=='recovery':
    outputs={path.replace('input','recovery'):text.replace('OMainInputInstruction','OMainRecoveryInstruction').replace('main_input','main_recovery') for path,text in outputs.items()}

if args.graph=='boot':
    outputs={path.replace('input','boot'):text.replace('OMainInputInstruction','OMainBootInstruction').replace('main_input','main_boot') for path,text in outputs.items()}

if args.graph=='producers':
    outputs={path.replace('input','producer'):text.replace('OMainInputInstruction','OMainProducerInstruction').replace('main_input','main_producer') for path,text in outputs.items()}

if args.graph=='irqwrapper':
    outputs={path.replace('input','irqwrapper'):text.replace('OMainInputInstruction','OMainIrqWrapperInstruction').replace('main_input','main_irqwrapper') for path,text in outputs.items()}

for path,text in outputs.items():
    if args.check:
        if Path(path).read_text()!=text:raise SystemExit('generated file differs: '+path)
    else: Path(path).write_text(text)
print(f'{len(metadata)} bounded instructions; '+('generated files match' if args.check else 'generated translation and capture manifests'))
