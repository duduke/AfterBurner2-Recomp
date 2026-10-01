#!/usr/bin/env python3
"""Minimal M68000 disassembler, purpose-built for reading After Burner II's
main/sub CPU ROMs. Covers the common instruction set (no MMU/FPU/68020+
extensions needed for a 68000-based arcade board). Not exhaustive, but
raises on anything unrecognized so gaps are visible rather than silently
wrong.

Usage: m68k_disasm.py <binfile> <start_addr_hex> <end_addr_hex> [--base 0]
Prints address: bytes  mnemonic operands

Note: this does purely linear disassembly (no control-flow following), so
blindly sweeping a wide address range will walk into embedded data tables
past a function's end and print "unknown opcode" for them — that's
expected, not necessarily a decoder gap. Only treat "unknown opcode" as a
real gap to fix when it happens while following an actual execution path
(reached via a BSR/JSR/Bcc from code already confirmed to be real).
"""
import sys

SIZE_CODES = {0: 'B', 1: 'W', 2: 'L'}


class Reader:
    def __init__(self, data, base, addr):
        self.data = data
        self.base = base
        self.addr = addr

    def peek16(self, off=0):
        a = self.addr + off - self.base
        return (self.data[a] << 8) | self.data[a + 1]

    def next16(self):
        v = self.peek16(0)
        self.addr += 2
        return v

    def next32(self):
        hi = self.next16()
        lo = self.next16()
        return (hi << 16) | lo

    def next_sized(self, size):
        if size == 0:
            v = self.next16()
            return v & 0xff
        if size == 1:
            return self.next16()
        return self.next32()


def signed(v, bits):
    if v & (1 << (bits - 1)):
        return v - (1 << bits)
    return v


def ea_str(reader, mode, reg, size, for_jump=False):
    """Decode an effective address; returns string. Consumes extension words
    from reader as needed."""
    if mode == 0:
        return f'D{reg}'
    if mode == 1:
        return f'A{reg}'
    if mode == 2:
        return f'(A{reg})'
    if mode == 3:
        return f'(A{reg})+'
    if mode == 4:
        return f'-(A{reg})'
    if mode == 5:
        disp = signed(reader.next16(), 16)
        return f'{disp}(A{reg})'
    if mode == 6:
        ext = reader.next16()
        idx_reg = (ext >> 12) & 7
        idx_is_a = (ext >> 15) & 1
        idx_long = (ext >> 11) & 1
        disp8 = signed(ext & 0xff, 8)
        idx = f'{"A" if idx_is_a else "D"}{idx_reg}.{"L" if idx_long else "W"}'
        return f'{disp8}(A{reg},{idx})'
    if mode == 7:
        if reg == 0:
            addr = reader.next16()
            return f'(${addr:04X}).W'
        if reg == 1:
            addr = reader.next32()
            return f'(${addr:08X}).L'
        if reg == 2:
            pc_at = reader.addr
            disp = signed(reader.next16(), 16)
            return f'${pc_at + disp:06X}(PC)'
        if reg == 3:
            pc_at = reader.addr
            ext = reader.next16()
            idx_reg = (ext >> 12) & 7
            idx_is_a = (ext >> 15) & 1
            idx_long = (ext >> 11) & 1
            disp8 = signed(ext & 0xff, 8)
            idx = f'{"A" if idx_is_a else "D"}{idx_reg}.{"L" if idx_long else "W"}'
            return f'${pc_at + disp8:06X}(PC,{idx})'
        if reg == 4:
            if for_jump:
                raise ValueError('immediate not valid as jump target')
            val = reader.next_sized(size if size is not None else 1)
            return f'#${val:X}'
    raise ValueError(f'bad EA mode={mode} reg={reg}')


COND = ['T', 'F', 'HI', 'LS', 'CC', 'CS', 'NE', 'EQ',
        'VC', 'VS', 'PL', 'MI', 'GE', 'LT', 'GT', 'LE']


def disasm_one(data, base, addr):
    """Disassemble one instruction at `addr` (absolute). Returns (next_addr, text)."""
    r = Reader(data, base, addr)
    op = r.next16()

    def mode_reg_src():
        return (op >> 3) & 7, op & 7

    # --- MOVE / MOVEA ---
    top2 = (op >> 12) & 0xf
    if top2 in (1, 2, 3):  # MOVE.B/L/W
        size = {1: 0, 3: 1, 2: 2}[top2]
        src_mode, src_reg = (op >> 3) & 7, op & 7
        dst_reg = (op >> 9) & 7
        dst_mode = (op >> 6) & 7
        src = ea_str(r, src_mode, src_reg, size)
        if dst_mode == 1:
            return r.addr, f'MOVEA.{SIZE_CODES[size]} {src},A{dst_reg}'
        dst = ea_str(r, dst_mode, dst_reg, size)
        return r.addr, f'MOVE.{SIZE_CODES[size]} {src},{dst}'

    if op == 0x4E71:
        return r.addr, 'NOP'
    if op == 0x4E70:
        return r.addr, 'RESET'
    if op == 0x4E73:
        return r.addr, 'RTE'
    if op == 0x4E75:
        return r.addr, 'RTS'
    if op == 0x4E77:
        return r.addr, 'RTR'
    if op == 0x4E76:
        return r.addr, 'TRAPV'

    if (op & 0xFFC0) == 0x4E80:  # JSR
        mode, reg = mode_reg_src()
        ea = ea_str(r, mode, reg, None, for_jump=True)
        return r.addr, f'JSR {ea}'
    if (op & 0xFFC0) == 0x4EC0:  # JMP
        mode, reg = mode_reg_src()
        ea = ea_str(r, mode, reg, None, for_jump=True)
        return r.addr, f'JMP {ea}'

    if (op & 0xFF00) == 0x6100:  # BSR
        disp = op & 0xff
        if disp == 0:
            d = signed(r.next16(), 16)
        elif disp == 0xff:
            d = signed(r.next32(), 32)
        else:
            d = signed(disp, 8)
        return r.addr, f'BSR ${addr + 2 + d:06X}'
    if (op & 0xF000) == 0x6000:  # Bcc
        cc = (op >> 8) & 0xf
        disp = op & 0xff
        if disp == 0:
            d = signed(r.next16(), 16)
        elif disp == 0xff:
            d = signed(r.next32(), 32)
        else:
            d = signed(disp, 8)
        return r.addr, f'B{COND[cc]} ${addr + 2 + d:06X}'

    if (op & 0xF0C0) == 0x50C0 and ((op >> 3) & 7) == 1:  # DBcc
        cc = (op >> 8) & 0xf
        reg = op & 7
        d = signed(r.next16(), 16)
        return r.addr, f'DB{COND[cc]} D{reg},${addr + 2 + d:06X}'

    if (op & 0xF000) == 0x7000 and not (op & 0x0100):  # MOVEQ
        reg = (op >> 9) & 7
        val = signed(op & 0xff, 8)
        return r.addr, f'MOVEQ #{val},D{reg}'

    if (op & 0xFF00) == 0x4A00:  # TST
        size = (op >> 6) & 3
        if size == 3:
            raise ValueError('not TST')
        mode, reg = mode_reg_src()
        ea = ea_str(r, mode, reg, size)
        return r.addr, f'TST.{SIZE_CODES[size]} {ea}'

    if (op & 0xFFF8) == 0x4E50:  # LINK
        reg = op & 7
        d = signed(r.next16(), 16)
        return r.addr, f'LINK A{reg},#{d}'
    if (op & 0xFFF8) == 0x4E58:  # UNLK
        reg = op & 7
        return r.addr, f'UNLK A{reg}'

    if (op & 0xF1C0) == 0x41C0:  # LEA
        reg = (op >> 9) & 7
        mode, m_reg = mode_reg_src()
        ea = ea_str(r, mode, m_reg, 2)
        return r.addr, f'LEA {ea},A{reg}'

    if (op & 0xF1C0) == 0x4180:  # CHK (word)
        reg = (op >> 9) & 7
        mode, m_reg = mode_reg_src()
        ea = ea_str(r, mode, m_reg, 1)
        return r.addr, f'CHK {ea},D{reg}'

    if (op & 0xFFB8) == 0x4880:  # EXT.W/EXT.L Dn (MOVEM's encoding with a Dn operand)
        return r.addr, f'EXT.{"L" if op & 0x40 else "W"} D{op & 7}'

    if (op & 0xFFC0) in (0x4880, 0x48C0, 0x4C80, 0x4CC0):  # MOVEM reg->mem / mem->reg
        direction = (op >> 10) & 1
        size = 2 if (op & 0x40) else 1
        mask = r.next16()
        mode, reg = mode_reg_src()
        ea = ea_str(r, mode, reg, size)
        regs = []
        if mode == 4:  # predecrement: mask order is A7..A0,D7..D0
            names = [f'A{7-i}' for i in range(8)] + [f'D{7-i}' for i in range(8)]
        else:
            names = [f'D{i}' for i in range(8)] + [f'A{i}' for i in range(8)]
        for i in range(16):
            if mask & (1 << i):
                regs.append(names[i])
        reglist = '/'.join(regs)
        if direction:
            return r.addr, f'MOVEM.{SIZE_CODES[size]} {ea},{reglist}'
        return r.addr, f'MOVEM.{SIZE_CODES[size]} {reglist},{ea}'

    if (op & 0xF000) == 0x0000 and ((op >> 8) & 0xf) in (0, 2, 4, 6, 0xA, 0xC) and not (op & 0x0100):
        # ORI/ANDI/SUBI/ADDI/EORI/CMPI #imm,EA  (bits 11-8: 0=ORI,2=ANDI,4=SUBI,6=ADDI,A=EORI,C=CMPI)
        opc = (op >> 8) & 0xf
        names = {0: 'ORI', 2: 'ANDI', 4: 'SUBI', 6: 'ADDI', 0xA: 'EORI', 0xC: 'CMPI'}
        size = (op >> 6) & 3
        if size == 3:
            raise ValueError('bad size')
        mode, reg = mode_reg_src()
        if mode == 7 and reg == 4 and opc in (0, 2, 0xA):
            # ORI/ANDI/EORI #imm,CCR (byte) or #imm,SR (word) — mode7/reg4 here
            # is the special CCR/SR destination encoding, not a real EA, so it
            # must not be decoded via ea_str (which would consume a second,
            # nonexistent immediate word).
            imm = r.next_sized(size)
            dest = 'SR' if size == 1 else 'CCR'
            return r.addr, f'{names[opc]} #${imm:X},{dest}'
        imm = r.next_sized(size)
        ea = ea_str(r, mode, reg, size)
        return r.addr, f'{names[opc]}.{SIZE_CODES[size]} #${imm:X},{ea}'

    # Generic register/EA ALU ops: ADD/SUB/AND/OR/CMP/EOR (data-register form + address form)
    fam = (op >> 12) & 0xf
    if fam in (0x8, 0x9, 0xB, 0xC, 0xD):
        names = {0x8: 'OR', 0x9: 'SUB', 0xB: 'CMP/EOR', 0xC: 'AND', 0xD: 'ADD'}
        reg = (op >> 9) & 7
        opmode = (op >> 6) & 7
        mode, m_reg = mode_reg_src()
        if opmode in (0, 1, 2):
            size = opmode
            ea = ea_str(r, mode, m_reg, size)
            base_name = {0x8: 'OR', 0x9: 'SUB', 0xB: 'CMP', 0xC: 'AND', 0xD: 'ADD'}[fam]
            return r.addr, f'{base_name}.{SIZE_CODES[size]} {ea},D{reg}'
        if opmode in (4, 5, 6) and mode in (0, 1) and not (fam == 0xB and mode == 0):
            # The "Dn,<ea>" slot with a register <ea> isn't a legal ALU destination,
            # so the encoding is reused: ABCD/SBCD, EXG, ADDX/SUBX, CMPM.
            size = opmode - 4
            src = f'-(A{m_reg})' if mode else f'D{m_reg}'
            dst = f'-(A{reg})' if mode else f'D{reg}'
            if fam in (0x8, 0xC) and opmode == 4:
                return r.addr, f'{"SBCD" if fam == 0x8 else "ABCD"} {src},{dst}'
            if fam == 0xC and opmode == 5:
                return r.addr, f'EXG {"A" if mode else "D"}{reg},{"A" if mode else "D"}{m_reg}'
            if fam == 0xC and opmode == 6 and mode == 1:
                return r.addr, f'EXG D{reg},A{m_reg}'
            if fam in (0x9, 0xD):
                return r.addr, f'{"SUBX" if fam == 0x9 else "ADDX"}.{SIZE_CODES[size]} {src},{dst}'
            if fam == 0xB:
                return r.addr, f'CMPM.{SIZE_CODES[size]} (A{m_reg})+,(A{reg})+'
            raise ValueError(f'illegal ALU register form {op:04X}')
        if opmode in (4, 5, 6):
            size = opmode - 4
            ea = ea_str(r, mode, m_reg, size)
            base_name = {0x8: 'OR', 0x9: 'SUB', 0xB: 'EOR', 0xC: 'AND', 0xD: 'ADD'}[fam]
            return r.addr, f'{base_name}.{SIZE_CODES[size]} D{reg},{ea}'
        if opmode in (3, 7) and fam in (0x9, 0xB, 0xD):
            size = 1 if opmode == 3 else 2
            ea = ea_str(r, mode, m_reg, size)
            base_name = {0x9: 'SUBA', 0xB: 'CMPA', 0xD: 'ADDA'}[fam]
            return r.addr, f'{base_name}.{SIZE_CODES[size]} {ea},A{reg}'
        if opmode in (3, 7) and fam in (0x8, 0xC):
            # word-size-only: 0x8=OR family -> DIVU/DIVS, 0xC=AND family -> MULU/MULS
            ea = ea_str(r, mode, m_reg, 1)
            name = {(0x8, 3): 'DIVU', (0x8, 7): 'DIVS', (0xC, 3): 'MULU', (0xC, 7): 'MULS'}[(fam, opmode)]
            return r.addr, f'{name}.W {ea},D{reg}'

    if (op & 0xF138) == 0x0108:  # MOVEP (rare on this era of code, but include)
        raise ValueError('MOVEP not implemented')

    if (op & 0xFF00) == 0x4200:  # CLR
        size = (op >> 6) & 3
        if size == 3:
            raise ValueError('not CLR')
        mode, reg = mode_reg_src()
        ea = ea_str(r, mode, reg, size)
        return r.addr, f'CLR.{SIZE_CODES[size]} {ea}'

    if (op & 0xFFC0) == 0x4840 and ((op >> 3) & 7) not in (0, 1):  # PEA
        mode, reg = mode_reg_src()
        ea = ea_str(r, mode, reg, 2, for_jump=True)
        return r.addr, f'PEA {ea}'

    if (op & 0xFFF0) == 0x4E40:  # TRAP
        vec = op & 0xf
        return r.addr, f'TRAP #{vec}'

    if (op & 0xFF00) == 0x4400:  # NEG
        size = (op >> 6) & 3
        mode, reg = mode_reg_src()
        ea = ea_str(r, mode, reg, size)
        return r.addr, f'NEG.{SIZE_CODES[size]} {ea}'

    if (op & 0xFF00) == 0x4600:  # NOT
        size = (op >> 6) & 3
        mode, reg = mode_reg_src()
        ea = ea_str(r, mode, reg, size)
        return r.addr, f'NOT.{SIZE_CODES[size]} {ea}'

    if (op & 0xFFF8) == 0x4840:  # SWAP
        reg = op & 7
        return r.addr, f'SWAP D{reg}'

    if (op & 0xF000) == 0xE000 and ((op >> 6) & 3) != 3:  # shift/rotate, register form
        size = (op >> 6) & 3
        direction = 'L' if (op >> 8) & 1 else 'R'
        op_kind = ['AS', 'LS', 'ROX', 'RO'][(op >> 3) & 3]
        reg = op & 7
        if (op >> 5) & 1:
            count_str = f'D{(op >> 9) & 7}'
        else:
            count = (op >> 9) & 7
            count = 8 if count == 0 else count
            count_str = f'#{count}'
        return r.addr, f'{op_kind}{direction}.{SIZE_CODES[size]} {count_str},D{reg}'

    if (op & 0xF000) == 0xE000 and ((op >> 6) & 3) == 3:  # shift/rotate, memory form (word, count=1)
        direction = 'L' if (op >> 8) & 1 else 'R'
        op_kind = ['AS', 'LS', 'ROX', 'RO'][(op >> 9) & 3]
        mode, reg = mode_reg_src()
        ea = ea_str(r, mode, reg, 1)
        return r.addr, f'{op_kind}{direction}.W {ea}'

    if (op & 0xFFC0) == 0x4AC0:  # TAS
        mode, reg = mode_reg_src()
        ea = ea_str(r, mode, reg, 0)
        return r.addr, f'TAS {ea}'

    if (op & 0xF0C0) == 0x50C0:  # Scc
        cc = (op >> 8) & 0xf
        mode, reg = mode_reg_src()
        ea = ea_str(r, mode, reg, 0)
        return r.addr, f'S{COND[cc]} {ea}'

    if (op & 0xF100) == 0x5000 or (op & 0xF100) == 0x5100:  # ADDQ/SUBQ
        data = (op >> 9) & 7
        data = 8 if data == 0 else data
        is_sub = (op >> 8) & 1
        size = (op >> 6) & 3
        if size == 3:
            raise ValueError('not ADDQ/SUBQ (that is Scc/DBcc range)')
        mode, reg = mode_reg_src()
        ea = ea_str(r, mode, reg, size)
        return r.addr, f'{"SUBQ" if is_sub else "ADDQ"}.{SIZE_CODES[size]} #{data},{ea}'

    # BTST/BCHG/BCLR/BSET (register form) 0000 rrr opc mmm rrr
    if (op & 0xF000) == 0x0000 and (op & 0x38) != 0x08:
        sub = (op >> 6) & 3
        if op & 0x100:
            reg = (op >> 9) & 7
            mode, m_reg = mode_reg_src()
            names = ['BTST', 'BCHG', 'BCLR', 'BSET']
            size = 0 if mode != 0 else 2
            ea = ea_str(r, mode, m_reg, size)
            return r.addr, f'{names[sub]} D{reg},{ea}'

    if (op & 0xFFC0) == 0x0800 or (op & 0xFFC0) == 0x0840 or (op & 0xFFC0) == 0x0880 or (op & 0xFFC0) == 0x08C0:
        sub = (op >> 6) & 3
        names = ['BTST', 'BCHG', 'BCLR', 'BSET']
        mode, m_reg = mode_reg_src()
        size = 0 if mode != 0 else 2
        bitnum = r.next16() & 0xff
        ea = ea_str(r, mode, m_reg, size)
        return r.addr, f'{names[sub]} #{bitnum},{ea}'

    raise ValueError(f'unknown opcode {op:04X}')


def main():
    path = sys.argv[1]
    start = int(sys.argv[2], 16)
    end = int(sys.argv[3], 16)
    base = 0
    if '--base' in sys.argv:
        base = int(sys.argv[sys.argv.index('--base') + 1], 16)

    with open(path, 'rb') as f:
        data = f.read()

    addr = start
    while addr < end:
        start_addr = addr
        try:
            next_addr, text = disasm_one(data, base, addr)
        except Exception as e:
            word = (data[addr - base] << 8) | data[addr - base + 1]
            print(f'{start_addr:06X}: {word:04X}      ??? ({e})')
            addr += 2
            continue
        nbytes = next_addr - start_addr
        raw = data[start_addr - base: next_addr - base]
        hexstr = ' '.join(f'{b:02X}' for b in raw)
        print(f'{start_addr:06X}: {hexstr:<20} {text}')
        addr = next_addr


if __name__ == '__main__':
    main()
