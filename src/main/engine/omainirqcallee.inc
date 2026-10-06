// Instruction-by-instruction IRQ4 callee translations. Tables remain runtime ROM reads.
        case 0xb84c: // $00B84C: TST.B ($8902).W
            unary(false,1,{W,0,0xffff8902}); pc=0xb850;break;
        case 0xb850: // $00B850: BEQ $00B87C
            if(branch(EQ,0x00b87c,true)) continue; pc=0xb854;break;
        case 0xb854: // $00B854: CMPI.W #$1,($0029C09C).L
            alu(CMP,2,{I,0,0x1},{L,0,0x29c09c},true,false); pc=0xb85c;break;
        case 0xb85c: // $00B85C: BEQ $00B884
            if(branch(EQ,0x00b884,true)) continue; pc=0xb860;break;
        case 0xb860: // $00B860: MOVE.W ($8908).W,D1
            move(2,{W,0,0xffff8908},{D,1},false); pc=0xb864;break;
        case 0xb864: // $00B864: LEA ($890C).W,A0
            lea({W,0,0xffff890c},0); pc=0xb868;break;
        case 0xb868: // $00B868: MOVE.B D0,0(A0,D1.W)
            move(1,{D,0},{X,0,0,1},false); pc=0xb86c;break;
        case 0xb86c: // $00B86C: ADDQ.W #1,D1
            alu(ADD,2,{I,0,0x1},{D,1},false,true); pc=0xb86e;break;
        case 0xb86e: // $00B86E: ANDI.W #$3F,D1
            alu(AND,2,{I,0,0x3f},{D,1},true,false); pc=0xb872;break;
        case 0xb872: // $00B872: MOVE.W D1,($8908).W
            move(2,{D,1},{W,0,0xffff8908},false); pc=0xb876;break;
        case 0xb876: // $00B876: ADDQ.B #1,($8906).W
            alu(ADD,1,{I,0,0x1},{W,0,0xffff8906},false,true); pc=0xb87a;break;
        case 0xb87a: // $00B87A: RTS
            if(ret()) return returned;continue; pc=0xb87c;break;
        case 0xb87c: // $00B87C: CLR.W ($8908).W
            unary(true,2,{W,0,0xffff8908}); pc=0xb880;break;
        case 0xb880: // $00B880: CLR.B ($8906).W
            unary(true,1,{W,0,0xffff8906}); pc=0xb884;break;
        case 0xb884: // $00B884: RTS
            if(ret()) return returned;continue; pc=0xb886;break;
        case 0xcfc4: // $00CFC4: CMPI.W #$F,($0029C09C).L
            alu(CMP,2,{I,0,0xf},{L,0,0x29c09c},true,false); pc=0xcfcc;break;
        case 0xcfcc: // $00CFCC: BLT $00D060
            if(branch(LT,0x00d060,true)) continue; pc=0xcfd0;break;
        case 0xcfd0: // $00CFD0: CMPI.W #$12,($0029C09C).L
            alu(CMP,2,{I,0,0x12},{L,0,0x29c09c},true,false); pc=0xcfd8;break;
        case 0xcfd8: // $00CFD8: BGT $00D060
            if(branch(GT,0x00d060,true)) continue; pc=0xcfdc;break;
        case 0xcfdc: // $00CFDC: MOVE.W ($8D66).W,D0
            move(2,{W,0,0xffff8d66},{D,0},false); pc=0xcfe0;break;
        case 0xcfe0: // $00CFE0: CMP.W ($0003218A).L,D0
            alu(CMP,2,{L,0,0x3218a},{D,0},false,false); pc=0xcfe6;break;
        case 0xcfe6: // $00CFE6: BLE $00CFF0
            if(branch(LE,0x00cff0,true)) continue; pc=0xcfea;break;
        case 0xcfea: // $00CFEA: MOVE.W ($0003218A).L,D0
            move(2,{L,0,0x3218a},{D,0},false); pc=0xcff0;break;
        case 0xcff0: // $00CFF0: LEA ($000D0D62).L,A0
            lea({L,0,0xd0d62},0); pc=0xcff6;break;
        case 0xcff6: // $00CFF6: LEA (A0),A1
            lea({M,0},1); pc=0xcff8;break;
        case 0xcff8: // $00CFF8: MOVE.W #$20,D1
            move(2,{I,0,0x20},{D,1},false); pc=0xcffc;break;
        case 0xcffc: // $00CFFC: MOVEQ #13,D2
            moveq({I,0,0xd},2); pc=0xcffe;break;
        case 0xcffe: // $00CFFE: MOVE.W D1,-128(A1)
            move(2,{D,1},{O,1,-128},false); pc=0xd002;break;
        case 0xd002: // $00D002: MOVE.W D1,(A1)+
            move(2,{D,1},{P,1},false); pc=0xd004;break;
        case 0xd004: // $00D004: DBF D2,$00CFFE
            if(db(F,2,0x00cffe)) continue; pc=0xd008;break;
        case 0xd008: // $00D008: TST.W D0
            unary(false,2,{D,0}); pc=0xd00a;break;
        case 0xd00a: // $00D00A: BLE $00D060
            if(branch(LE,0x00d060,true)) continue; pc=0xd00e;break;
        case 0xd00e: // $00D00E: JSR ($0000D062).L
            call({L,0,0xd062},0xd014,false);continue; pc=0xd014;break;
        case 0xd014: // $00D014: MOVE.W D0,D1
            move(2,{D,0},{D,1},false); pc=0xd016;break;
        case 0xd016: // $00D016: ANDI.W #$F0,D1
            alu(AND,2,{I,0,0xf0},{D,1},true,false); pc=0xd01a;break;
        case 0xd01a: // $00D01A: ROR.W #4,D1
            shift(ROR,2,{I,0,0x4},1); pc=0xd01c;break;
        case 0xd01c: // $00D01C: CMPI.W #$100,D0
            alu(CMP,2,{I,0,0x100},{D,0},true,false); pc=0xd020;break;
        case 0xd020: // $00D020: BLT $00D026
            if(branch(LT,0x00d026,false)) continue; pc=0xd022;break;
        case 0xd022: // $00D022: ADDI.W #$A,D1
            alu(ADD,2,{I,0,0xa},{D,1},true,false); pc=0xd026;break;
        case 0xd026: // $00D026: SUBQ.W #1,D1
            alu(SUB,2,{I,0,0x1},{D,1},false,true); pc=0xd028;break;
        case 0xd028: // $00D028: BMI $00D03A
            if(branch(MI,0x00d03a,true)) continue; pc=0xd02c;break;
        case 0xd02c: // $00D02C: MOVE.W #$8BFD,-128(A0)
            move(2,{I,0,0x8bfd},{O,0,-128},false); pc=0xd032;break;
        case 0xd032: // $00D032: MOVE.W #$8BFE,(A0)+
            move(2,{I,0,0x8bfe},{P,0},false); pc=0xd036;break;
        case 0xd036: // $00D036: DBF D1,$00D02C
            if(db(F,1,0x00d02c)) continue; pc=0xd03a;break;
        case 0xd03a: // $00D03A: ANDI.W #$F,D0
            alu(AND,2,{I,0,0xf},{D,0},true,false); pc=0xd03e;break;
        case 0xd03e: // $00D03E: BEQ $00D060
            if(branch(EQ,0x00d060,true)) continue; pc=0xd042;break;
        case 0xd042: // $00D042: SUBQ.W #2,D0
            alu(SUB,2,{I,0,0x2},{D,0},false,true); pc=0xd044;break;
        case 0xd044: // $00D044: BMI $00D05C
            if(branch(MI,0x00d05c,true)) continue; pc=0xd048;break;
        case 0xd048: // $00D048: MOVE.W D0,D1
            move(2,{D,0},{D,1},false); pc=0xd04a;break;
        case 0xd04a: // $00D04A: ASR.W #1,D0
            shift(ASR,2,{I,0,0x1},0); pc=0xd04c;break;
        case 0xd04c: // $00D04C: MOVE.W #$8BFC,(A0)+
            move(2,{I,0,0x8bfc},{P,0},false); pc=0xd050;break;
        case 0xd050: // $00D050: DBF D0,$00D04C
            if(db(F,0,0x00d04c)) continue; pc=0xd054;break;
        case 0xd054: // $00D054: BTST #0,D1
            bit(BTST,{I,0,0x0},{D,1}); pc=0xd058;break;
        case 0xd058: // $00D058: BEQ $00D060
            if(branch(EQ,0x00d060,true)) continue; pc=0xd05c;break;
        case 0xd05c: // $00D05C: MOVE.W #$8BFB,(A0)+
            move(2,{I,0,0x8bfb},{P,0},false); pc=0xd060;break;
        case 0xd060: // $00D060: RTS
            if(ret()) return returned;continue; pc=0xd062;break;
        case 0xd062: // $00D062: MOVEQ #0,D3
            moveq({I,0,0x0},3); pc=0xd064;break;
        case 0xd064: // $00D064: ANDI.L #$FFFF,D0
            alu(AND,4,{I,0,0xffff},{D,0},true,false); pc=0xd06a;break;
        case 0xd06a: // $00D06A: MOVE.W #$2710,D1
            move(2,{I,0,0x2710},{D,1},false); pc=0xd06e;break;
        case 0xd06e: // $00D06E: BSR $00D090
            call({C,0,0x00d090},0xd072,true);continue; pc=0xd072;break;
        case 0xd072: // $00D072: MOVE.W #$3E8,D1
            move(2,{I,0,0x3e8},{D,1},false); pc=0xd076;break;
        case 0xd076: // $00D076: BSR $00D090
            call({C,0,0x00d090},0xd07a,true);continue; pc=0xd07a;break;
        case 0xd07a: // $00D07A: MOVE.W #$64,D1
            move(2,{I,0,0x64},{D,1},false); pc=0xd07e;break;
        case 0xd07e: // $00D07E: BSR $00D090
            call({C,0,0x00d090},0xd082,true);continue; pc=0xd082;break;
        case 0xd082: // $00D082: MOVE.W #$A,D1
            move(2,{I,0,0xa},{D,1},false); pc=0xd086;break;
        case 0xd086: // $00D086: BSR $00D090
            call({C,0,0x00d090},0xd08a,true);continue; pc=0xd08a;break;
        case 0xd08a: // $00D08A: OR.B D0,D3
            alu(OR,1,{D,0},{D,3},false,false); pc=0xd08c;break;
        case 0xd08c: // $00D08C: MOVE.L D3,D0
            move(4,{D,3},{D,0},false); pc=0xd08e;break;
        case 0xd08e: // $00D08E: RTS
            if(ret()) return returned;continue; pc=0xd090;break;
        case 0xd090: // $00D090: DIVU.W D1,D0
            divu({D,1},0); pc=0xd092;break;
        case 0xd092: // $00D092: OR.B D0,D3
            alu(OR,1,{D,0},{D,3},false,false); pc=0xd094;break;
        case 0xd094: // $00D094: LSL.L #4,D3
            shift(LSL,4,{I,0,0x4},3); pc=0xd096;break;
        case 0xd096: // $00D096: ANDI.L #$FFFF0000,D0
            alu(AND,4,{I,0,0xffff0000},{D,0},true,false); pc=0xd09c;break;
        case 0xd09c: // $00D09C: SWAP D0
            swap(0); pc=0xd09e;break;
        case 0xd09e: // $00D09E: RTS
            if(ret()) return returned;continue; pc=0xd0a0;break;
        case 0x13a74: // $013A74: LEA ($000C0000).L,A0
            lea({L,0,0xc0000},0); pc=0x13a7a;break;
        case 0x13a7a: // $013A7A: MOVE.W #$17F,D1
            move(2,{I,0,0x17f},{D,1},false); pc=0x13a7e;break;
        case 0x13a7e: // $013A7E: MOVE.L #$200020,D0
            move(4,{I,0,0x200020},{D,0},false); pc=0x13a84;break;
        case 0x13a84: // $013A84: MOVE.L D0,(A0)+
            move(4,{D,0},{P,0},false); pc=0x13a86;break;
        case 0x13a86: // $013A86: MOVE.L D0,(A0)+
            move(4,{D,0},{P,0},false); pc=0x13a88;break;
        case 0x13a88: // $013A88: MOVE.L D0,(A0)+
            move(4,{D,0},{P,0},false); pc=0x13a8a;break;
        case 0x13a8a: // $013A8A: MOVE.L D0,(A0)+
            move(4,{D,0},{P,0},false); pc=0x13a8c;break;
        case 0x13a8c: // $013A8C: MOVE.L D0,(A0)+
            move(4,{D,0},{P,0},false); pc=0x13a8e;break;
        case 0x13a8e: // $013A8E: MOVE.L D0,(A0)+
            move(4,{D,0},{P,0},false); pc=0x13a90;break;
        case 0x13a90: // $013A90: MOVE.L D0,(A0)+
            move(4,{D,0},{P,0},false); pc=0x13a92;break;
        case 0x13a92: // $013A92: MOVE.L D0,(A0)+
            move(4,{D,0},{P,0},false); pc=0x13a94;break;
        case 0x13a94: // $013A94: DBF D1,$013A84
            if(db(F,1,0x013a84)) continue; pc=0x13a98;break;
        case 0x13a98: // $013A98: RTS
            if(ret()) return returned;continue; pc=0x13a9a;break;
        case 0x13e5c: // $013E5C: BTST #1,($0029C54C).L
            bit(BTST,{I,0,0x1},{L,0,0x29c54c}); pc=0x13e64;break;
        case 0x13e64: // $013E64: BEQ $013E8C
            if(branch(EQ,0x013e8c,true)) continue; pc=0x13e68;break;
        case 0x13e68: // $013E68: BSR $014424
            call({C,0,0x014424},0x13e6c,true);continue; pc=0x13e6c;break;
        case 0x13e6c: // $013E6C: ADDQ.B #1,($8C2C).W
            alu(ADD,1,{I,0,0x1},{W,0,0xffff8c2c},false,true); pc=0x13e70;break;
        case 0x13e70: // $013E70: MOVE.B ($8C2C).W,D0
            move(1,{W,0,0xffff8c2c},{D,0},false); pc=0x13e74;break;
        case 0x13e74: // $013E74: MOVE.B D0,D1
            move(1,{D,0},{D,1},false); pc=0x13e76;break;
        case 0x13e76: // $013E76: SUBQ.B #1,D1
            alu(SUB,1,{I,0,0x1},{D,1},false,true); pc=0x13e78;break;
        case 0x13e78: // $013E78: EOR.B D1,D0
            alu(EOR,1,{D,1},{D,0},false,false); pc=0x13e7a;break;
        case 0x13e7a: // $013E7A: BTST #2,D0
            bit(BTST,{I,0,0x2},{D,0}); pc=0x13e7e;break;
        case 0x13e7e: // $013E7E: BEQ $013E8C
            if(branch(EQ,0x013e8c,false)) continue; pc=0x13e80;break;
        case 0x13e80: // $013E80: ADDQ.W #1,($8C2A).W
            alu(ADD,2,{I,0,0x1},{W,0,0xffff8c2a},false,true); pc=0x13e84;break;
        case 0x13e84: // $013E84: CMPI.W #$20,($8C2A).W
            alu(CMP,2,{I,0,0x20},{W,0,0xffff8c2a},true,false); pc=0x13e8a;break;
        case 0x13e8a: // $013E8A: BGE $013E8E
            if(branch(GE,0x013e8e,false)) continue; pc=0x13e8c;break;
        case 0x13e8c: // $013E8C: RTS
            if(ret()) return returned;continue; pc=0x13e8e;break;
        case 0x13e8e: // $013E8E: BCLR #1,($0029C54C).L
            bit(BCLR,{I,0,0x1},{L,0,0x29c54c}); pc=0x13e96;break;
        case 0x13e96: // $013E96: BCLR #0,($8C2E).W
            bit(BCLR,{I,0,0x0},{W,0,0xffff8c2e}); pc=0x13e9c;break;
        case 0x13e9c: // $013E9C: CLR.W ($8C2A).W
            unary(true,2,{W,0,0xffff8c2a}); pc=0x13ea0;break;
        case 0x13ea0: // $013EA0: CMPI.W #$3,($0029C09C).L
            alu(CMP,2,{I,0,0x3},{L,0,0x29c09c},true,false); pc=0x13ea8;break;
        case 0x13ea8: // $013EA8: BEQ $013EAC
            if(branch(EQ,0x013eac,true)) continue; pc=0x13eac;break;
        case 0x13eac: // $013EAC: RTS
            if(ret()) return returned;continue; pc=0x13eae;break;
        case 0x13f92: // $013F92: TST.B ($000CDD05).L
            unary(false,1,{L,0,0xcdd05}); pc=0x13f98;break;
        case 0x13f98: // $013F98: BNE $0140D8
            if(branch(NE,0x0140d8,true)) continue; pc=0x13f9c;break;
        case 0x13f9c: // $013F9C: MOVE.W ($8C2A).W,D0
            move(2,{W,0,0xffff8c2a},{D,0},false); pc=0x13fa0;break;
        case 0x13fa0: // $013FA0: BEQ $014062
            if(branch(EQ,0x014062,true)) continue; pc=0x13fa4;break;
        case 0x13fa4: // $013FA4: BSR $0142DC
            call({C,0,0x0142dc},0x13fa8,true);continue; pc=0x13fa8;break;
        case 0x13fa8: // $013FA8: MOVE.W ($8C2A).W,D0
            move(2,{W,0,0xffff8c2a},{D,0},false); pc=0x13fac;break;
        case 0x13fac: // $013FAC: ASR.W #2,D0
            shift(ASR,2,{I,0,0x2},0); pc=0x13fae;break;
        case 0x13fae: // $013FAE: ASL.W #5,D0
            shift(ASL,2,{I,0,0x5},0); pc=0x13fb0;break;
        case 0x13fb0: // $013FB0: LEA ($0001413A).L,A1
            lea({L,0,0x1413a},1); pc=0x13fb6;break;
        case 0x13fb6: // $013FB6: MOVE.W ($8D30).W,D1
            move(2,{W,0,0xffff8d30},{D,1},false); pc=0x13fba;break;
        case 0x13fba: // $013FBA: MOVEA.L 0(A1,D1.W),A1
            move(4,{X,1,0,1},{A,1},true); pc=0x13fbe;break;
        case 0x13fbe: // $013FBE: LEA 2(A1,D0.W),A1
            lea({X,1,2,0},1); pc=0x13fc2;break;
        case 0x13fc2: // $013FC2: LEA ($00121D22).L,A0
            lea({L,0,0x121d22},0); pc=0x13fc8;break;
        case 0x13fc8: // $013FC8: ADDA.W D0,A0
            adda(2,{D,0},0,false); pc=0x13fca;break;
        case 0x13fca: // $013FCA: MOVE.L (A1)+,(A0)+
            move(4,{P,1},{P,0},false); pc=0x13fcc;break;
        case 0x13fcc: // $013FCC: MOVE.L (A1)+,(A0)+
            move(4,{P,1},{P,0},false); pc=0x13fce;break;
        case 0x13fce: // $013FCE: MOVE.L (A1)+,(A0)+
            move(4,{P,1},{P,0},false); pc=0x13fd0;break;
        case 0x13fd0: // $013FD0: MOVE.L (A1)+,(A0)+
            move(4,{P,1},{P,0},false); pc=0x13fd2;break;
        case 0x13fd2: // $013FD2: MOVE.L (A1)+,(A0)+
            move(4,{P,1},{P,0},false); pc=0x13fd4;break;
        case 0x13fd4: // $013FD4: MOVE.L (A1)+,(A0)+
            move(4,{P,1},{P,0},false); pc=0x13fd6;break;
        case 0x13fd6: // $013FD6: MOVE.L (A1)+,(A0)+
            move(4,{P,1},{P,0},false); pc=0x13fd8;break;
        case 0x13fd8: // $013FD8: MOVE.W ($8C2A).W,D0
            move(2,{W,0,0xffff8c2a},{D,0},false); pc=0x13fdc;break;
        case 0x13fdc: // $013FDC: ASR.W #2,D0
            shift(ASR,2,{I,0,0x2},0); pc=0x13fde;break;
        case 0x13fde: // $013FDE: ASL.W #5,D0
            shift(ASL,2,{I,0,0x5},0); pc=0x13fe0;break;
        case 0x13fe0: // $013FE0: LEA ($00014196).L,A1
            lea({L,0,0x14196},1); pc=0x13fe6;break;
        case 0x13fe6: // $013FE6: MOVE.W ($8D30).W,D1
            move(2,{W,0,0xffff8d30},{D,1},false); pc=0x13fea;break;
        case 0x13fea: // $013FEA: MOVEA.L 0(A1,D1.W),A1
            move(4,{X,1,0,1},{A,1},true); pc=0x13fee;break;
        case 0x13fee: // $013FEE: LEA 2(A1,D0.W),A1
            lea({X,1,2,0},1); pc=0x13ff2;break;
        case 0x13ff2: // $013FF2: LEA ($00121EA2).L,A0
            lea({L,0,0x121ea2},0); pc=0x13ff8;break;
        case 0x13ff8: // $013FF8: ADDA.W D0,A0
            adda(2,{D,0},0,false); pc=0x13ffa;break;
        case 0x13ffa: // $013FFA: MOVE.L (A1)+,(A0)+
            move(4,{P,1},{P,0},false); pc=0x13ffc;break;
        case 0x13ffc: // $013FFC: MOVE.L (A1)+,(A0)+
            move(4,{P,1},{P,0},false); pc=0x13ffe;break;
        case 0x13ffe: // $013FFE: MOVE.L (A1)+,(A0)+
            move(4,{P,1},{P,0},false); pc=0x14000;break;
        case 0x14000: // $014000: MOVE.L (A1)+,(A0)+
            move(4,{P,1},{P,0},false); pc=0x14002;break;
        case 0x14002: // $014002: MOVE.L (A1)+,(A0)+
            move(4,{P,1},{P,0},false); pc=0x14004;break;
        case 0x14004: // $014004: MOVE.L (A1)+,(A0)+
            move(4,{P,1},{P,0},false); pc=0x14006;break;
        case 0x14006: // $014006: MOVE.L (A1)+,(A0)+
            move(4,{P,1},{P,0},false); pc=0x14008;break;
        case 0x14008: // $014008: MOVE.W ($8C2A).W,D0
            move(2,{W,0,0xffff8c2a},{D,0},false); pc=0x1400c;break;
        case 0x1400c: // $01400C: ASR.W #2,D0
            shift(ASR,2,{I,0,0x2},0); pc=0x1400e;break;
        case 0x1400e: // $01400E: ASL.W #5,D0
            shift(ASL,2,{I,0,0x5},0); pc=0x14010;break;
        case 0x14010: // $014010: LEA ($000141F2).L,A1
            lea({L,0,0x141f2},1); pc=0x14016;break;
        case 0x14016: // $014016: MOVE.W ($8D30).W,D1
            move(2,{W,0,0xffff8d30},{D,1},false); pc=0x1401a;break;
        case 0x1401a: // $01401A: MOVEA.L 0(A1,D1.W),A1
            move(4,{X,1,0,1},{A,1},true); pc=0x1401e;break;
        case 0x1401e: // $01401E: LEA 2(A1,D0.W),A1
            lea({X,1,2,0},1); pc=0x14022;break;
        case 0x14022: // $014022: LEA ($00121E42).L,A0
            lea({L,0,0x121e42},0); pc=0x14028;break;
        case 0x14028: // $014028: MOVE.L (A1)+,(A0)+
            move(4,{P,1},{P,0},false); pc=0x1402a;break;
        case 0x1402a: // $01402A: MOVE.L (A1)+,(A0)+
            move(4,{P,1},{P,0},false); pc=0x1402c;break;
        case 0x1402c: // $01402C: MOVE.L (A1)+,(A0)+
            move(4,{P,1},{P,0},false); pc=0x1402e;break;
        case 0x1402e: // $01402E: MOVE.L (A1)+,(A0)+
            move(4,{P,1},{P,0},false); pc=0x14030;break;
        case 0x14030: // $014030: MOVE.L (A1)+,(A0)+
            move(4,{P,1},{P,0},false); pc=0x14032;break;
        case 0x14032: // $014032: MOVE.L (A1)+,(A0)+
            move(4,{P,1},{P,0},false); pc=0x14034;break;
        case 0x14034: // $014034: MOVE.L (A1)+,(A0)+
            move(4,{P,1},{P,0},false); pc=0x14036;break;
        case 0x14036: // $014036: CMPI.W #$1E,($8C2A).W
            alu(CMP,2,{I,0,0x1e},{W,0,0xffff8c2a},true,false); pc=0x1403c;break;
        case 0x1403c: // $01403C: BNE $014062
            if(branch(NE,0x014062,true)) continue; pc=0x14040;break;
        case 0x14040: // $014040: LEA ($0002EA16).L,A1
            lea({L,0,0x2ea16},1); pc=0x14046;break;
        case 0x14046: // $014046: MOVE.W ($8D30).W,D0
            move(2,{W,0,0xffff8d30},{D,0},false); pc=0x1404a;break;
        case 0x1404a: // $01404A: ASL.W #3,D0
            shift(ASL,2,{I,0,0x3},0); pc=0x1404c;break;
        case 0x1404c: // $01404C: ADDA.W D0,A1
            adda(2,{D,0},1,false); pc=0x1404e;break;
        case 0x1404e: // $01404E: LEA ($00121E22).L,A0
            lea({L,0,0x121e22},0); pc=0x14054;break;
        case 0x14054: // $014054: MOVE.L (A1)+,(A0)+
            move(4,{P,1},{P,0},false); pc=0x14056;break;
        case 0x14056: // $014056: MOVE.L (A1)+,(A0)+
            move(4,{P,1},{P,0},false); pc=0x14058;break;
        case 0x14058: // $014058: MOVE.L (A1)+,(A0)+
            move(4,{P,1},{P,0},false); pc=0x1405a;break;
        case 0x1405a: // $01405A: MOVE.L (A1)+,(A0)+
            move(4,{P,1},{P,0},false); pc=0x1405c;break;
        case 0x1405c: // $01405C: MOVE.L (A1)+,(A0)+
            move(4,{P,1},{P,0},false); pc=0x1405e;break;
        case 0x1405e: // $01405E: MOVE.L (A1)+,(A0)+
            move(4,{P,1},{P,0},false); pc=0x14060;break;
        case 0x14060: // $014060: MOVE.L (A1)+,(A0)+
            move(4,{P,1},{P,0},false); pc=0x14062;break;
        case 0x14062: // $014062: MOVE.W ($0029C0D2).L,D0
            move(2,{L,0,0x29c0d2},{D,0},false); pc=0x14068;break;
        case 0x14068: // $014068: BPL $01406C
            if(branch(PL,0x01406c,false)) continue; pc=0x1406a;break;
        case 0x1406a: // $01406A: MOVEQ #0,D0
            moveq({I,0,0x0},0); pc=0x1406c;break;
        case 0x1406c: // $01406C: ASR.W #4,D0
            shift(ASR,2,{I,0,0x4},0); pc=0x1406e;break;
        case 0x1406e: // $01406E: MOVE.W ($8B5A).W,D1
            move(2,{W,0,0xffff8b5a},{D,1},false); pc=0x14072;break;
        case 0x14072: // $014072: ANDI.W #$7,D1
            alu(AND,2,{I,0,0x7},{D,1},true,false); pc=0x14076;break;
        case 0x14076: // $014076: CMPI.W #$3,D1
            alu(CMP,2,{I,0,0x3},{D,1},true,false); pc=0x1407a;break;
        case 0x1407a: // $01407A: BLE $014082
            if(branch(LE,0x014082,true)) continue; pc=0x1407e;break;
        case 0x1407e: // $01407E: EORI.W #$7,D1
            alu(EOR,2,{I,0,0x7},{D,1},true,false); pc=0x14082;break;
        case 0x14082: // $014082: ADD.W D1,D0
            alu(ADD,2,{D,1},{D,0},false,false); pc=0x14084;break;
        case 0x14084: // $014084: ADD.W D0,D0
            alu(ADD,2,{D,0},{D,0},false,false); pc=0x14086;break;
        case 0x14086: // $014086: MOVE.W D0,D1
            move(2,{D,0},{D,1},false); pc=0x14088;break;
        case 0x14088: // $014088: ADD.W D0,D0
            alu(ADD,2,{D,0},{D,0},false,false); pc=0x1408a;break;
        case 0x1408a: // $01408A: ADD.W D1,D0
            alu(ADD,2,{D,1},{D,0},false,false); pc=0x1408c;break;
        case 0x1408c: // $01408C: LEA ($0001424E).L,A1
            lea({L,0,0x1424e},1); pc=0x14092;break;
        case 0x14092: // $014092: MOVE.W ($8D30).W,D1
            move(2,{W,0,0xffff8d30},{D,1},false); pc=0x14096;break;
        case 0x14096: // $014096: MOVEA.L 0(A1,D1.W),A1
            move(4,{X,1,0,1},{A,1},true); pc=0x1409a;break;
        case 0x1409a: // $01409A: LEA 0(A1,D0.W),A1
            lea({X,1,0,0},1); pc=0x1409e;break;
        case 0x1409e: // $01409E: LEA ($00121E42).L,A0
            lea({L,0,0x121e42},0); pc=0x140a4;break;
        case 0x140a4: // $0140A4: MOVE.L (A1)+,(A0)+
            move(4,{P,1},{P,0},false); pc=0x140a6;break;
        case 0x140a6: // $0140A6: ADDQ.W #2,A0
            alu(ADD,2,{I,0,0x2},{A,0},false,true); pc=0x140a8;break;
        case 0x140a8: // $0140A8: MOVE.W (A1),(A0)
            move(2,{M,1},{M,0},false); pc=0x140aa;break;
        case 0x140aa: // $0140AA: MOVE.W ($8B5A).W,D0
            move(2,{W,0,0xffff8b5a},{D,0},false); pc=0x140ae;break;
        case 0x140ae: // $0140AE: ANDI.W #$3,D0
            alu(AND,2,{I,0,0x3},{D,0},true,false); pc=0x140b2;break;
        case 0x140b2: // $0140B2: ASL.W #5,D0
            shift(ASL,2,{I,0,0x5},0); pc=0x140b4;break;
        case 0x140b4: // $0140B4: LEA ($0002ECF6).L,A1
            lea({L,0,0x2ecf6},1); pc=0x140ba;break;
        case 0x140ba: // $0140BA: ADDA.W D0,A1
            adda(2,{D,0},1,false); pc=0x140bc;break;
        case 0x140bc: // $0140BC: LEA ($00121FA2).L,A0
            lea({L,0,0x121fa2},0); pc=0x140c2;break;
        case 0x140c2: // $0140C2: MOVE.L (A1)+,(A0)+
            move(4,{P,1},{P,0},false); pc=0x140c4;break;
        case 0x140c4: // $0140C4: MOVE.L (A1)+,(A0)+
            move(4,{P,1},{P,0},false); pc=0x140c6;break;
        case 0x140c6: // $0140C6: LEA ($0002ED76).L,A1
            lea({L,0,0x2ed76},1); pc=0x140cc;break;
        case 0x140cc: // $0140CC: ADDA.W D0,A1
            adda(2,{D,0},1,false); pc=0x140ce;break;
        case 0x140ce: // $0140CE: LEA ($00121FC2).L,A0
            lea({L,0,0x121fc2},0); pc=0x140d4;break;
        case 0x140d4: // $0140D4: MOVE.L (A1)+,(A0)+
            move(4,{P,1},{P,0},false); pc=0x140d6;break;
        case 0x140d6: // $0140D6: MOVE.L (A1)+,(A0)+
            move(4,{P,1},{P,0},false); pc=0x140d8;break;
        case 0x140d8: // $0140D8: RTS
            if(ret()) return returned;continue; pc=0x140da;break;
        case 0x142dc: // $0142DC: MOVE.B ($000CDD08).L,D7
            move(1,{L,0,0xcdd08},{D,7},false); pc=0x142e2;break;
        case 0x142e2: // $0142E2: BTST #0,D7
            bit(BTST,{I,0,0x0},{D,7}); pc=0x142e6;break;
        case 0x142e6: // $0142E6: BEQ $0143A8
            if(branch(EQ,0x0143a8,true)) continue; pc=0x142ea;break;
        case 0x142ea: // $0142EA: MOVE.W #$240,($000CDD08).L
            move(2,{I,0,0x240},{L,0,0xcdd08},false); pc=0x142f2;break;
        case 0x142f2: // $0142F2: BSET #1,($0029C54C).L
            bit(BSET,{I,0,0x1},{L,0,0x29c54c}); pc=0x142fa;break;
        case 0x142fa: // $0142FA: LEA ($000CC000).L,A0
            lea({L,0,0xcc000},0); pc=0x14300;break;
        case 0x14300: // $014300: LEA (A0),A1
            lea({M,0},1); pc=0x14302;break;
        case 0x14302: // $014302: LEA 20(A1),A2
            lea({O,1,20},2); pc=0x14306;break;
        case 0x14306: // $014306: LEA 36(A1),A3
            lea({O,1,36},3); pc=0x1430a;break;
        case 0x1430a: // $01430A: MOVEQ #2,D7
            moveq({I,0,0x2},7); pc=0x1430c;break;
        case 0x1430c: // $01430C: BSR $014324
            call({C,0,0x014324},0x14310,true);continue; pc=0x14310;break;
        case 0x14310: // $014310: LEA ($000140DA).L,A0
            lea({L,0,0x140da},0); pc=0x14316;break;
        case 0x14316: // $014316: ADDA.W ($8D30).W,A0
            adda(2,{W,0,0xffff8d30},0,false); pc=0x1431a;break;
        case 0x1431a: // $01431A: MOVEA.L (A0),A0
            move(4,{M,0},{A,0},true); pc=0x1431c;break;
        case 0x1431c: // $01431C: ADDQ.W #2,A0
            alu(ADD,2,{I,0,0x2},{A,0},false,true); pc=0x1431e;break;
        case 0x1431e: // $01431E: LEA 32(A0),A2
            lea({O,0,32},2); pc=0x14322;break;
        case 0x14322: // $014322: MOVEQ #13,D7
            moveq({I,0,0xd},7); pc=0x14324;break;
        case 0x14324: // $014324: MOVEQ #31,D4
            moveq({I,0,0x1f},4); pc=0x14326;break;
        case 0x14326: // $014326: MOVE.W (A0)+,D0
            move(2,{P,0},{D,0},false); pc=0x14328;break;
        case 0x14328: // $014328: MOVE.W D0,(A1)+
            move(2,{D,0},{P,1},false); pc=0x1432a;break;
        case 0x1432a: // $01432A: MOVE.W D0,D2
            move(2,{D,0},{D,2},false); pc=0x1432c;break;
        case 0x1432c: // $01432C: ADD.W D2,D2
            alu(ADD,2,{D,2},{D,2},false,false); pc=0x1432e;break;
        case 0x1432e: // $01432E: BTST #12,D0
            bit(BTST,{I,0,0xc},{D,0}); pc=0x14332;break;
        case 0x14332: // $014332: BEQ $014336
            if(branch(EQ,0x014336,false)) continue; pc=0x14334;break;
        case 0x14334: // $014334: ADDQ.W #1,D2
            alu(ADD,2,{I,0,0x1},{D,2},false,true); pc=0x14336;break;
        case 0x14336: // $014336: AND.W D4,D2
            alu(AND,2,{D,4},{D,2},false,false); pc=0x14338;break;
        case 0x14338: // $014338: MOVE.W (A2)+,D1
            move(2,{P,2},{D,1},false); pc=0x1433a;break;
        case 0x1433a: // $01433A: MOVE.W D1,D3
            move(2,{D,1},{D,3},false); pc=0x1433c;break;
        case 0x1433c: // $01433C: ADD.W D3,D3
            alu(ADD,2,{D,3},{D,3},false,false); pc=0x1433e;break;
        case 0x1433e: // $01433E: BTST #12,D1
            bit(BTST,{I,0,0xc},{D,1}); pc=0x14342;break;
        case 0x14342: // $014342: BEQ $014346
            if(branch(EQ,0x014346,false)) continue; pc=0x14344;break;
        case 0x14344: // $014344: ADDQ.W #1,D3
            alu(ADD,2,{I,0,0x1},{D,3},false,true); pc=0x14346;break;
        case 0x14346: // $014346: AND.W D4,D3
            alu(AND,2,{D,4},{D,3},false,false); pc=0x14348;break;
        case 0x14348: // $014348: SUB.W D2,D3
            alu(SUB,2,{D,2},{D,3},false,false); pc=0x1434a;break;
        case 0x1434a: // $01434A: ASL.W #2,D3
            shift(ASL,2,{I,0,0x2},3); pc=0x1434c;break;
        case 0x1434c: // $01434C: MOVE.W D3,(A3)+
            move(2,{D,3},{P,3},false); pc=0x1434e;break;
        case 0x1434e: // $01434E: LSL.W #8,D2
            shift(LSL,2,{I,0,0x8},2); pc=0x14350;break;
        case 0x14350: // $014350: MOVE.W D2,(A3)+
            move(2,{D,2},{P,3},false); pc=0x14352;break;
        case 0x14352: // $014352: LSR.W #4,D0
            shift(LSR,2,{I,0,0x4},0); pc=0x14354;break;
        case 0x14354: // $014354: MOVE.W D0,D2
            move(2,{D,0},{D,2},false); pc=0x14356;break;
        case 0x14356: // $014356: ADD.W D2,D2
            alu(ADD,2,{D,2},{D,2},false,false); pc=0x14358;break;
        case 0x14358: // $014358: BTST #9,D0
            bit(BTST,{I,0,0x9},{D,0}); pc=0x1435c;break;
        case 0x1435c: // $01435C: BEQ $014360
            if(branch(EQ,0x014360,false)) continue; pc=0x1435e;break;
        case 0x1435e: // $01435E: ADDQ.W #1,D2
            alu(ADD,2,{I,0,0x1},{D,2},false,true); pc=0x14360;break;
        case 0x14360: // $014360: AND.W D4,D2
            alu(AND,2,{D,4},{D,2},false,false); pc=0x14362;break;
        case 0x14362: // $014362: LSR.W #4,D1
            shift(LSR,2,{I,0,0x4},1); pc=0x14364;break;
        case 0x14364: // $014364: MOVE.W D1,D3
            move(2,{D,1},{D,3},false); pc=0x14366;break;
        case 0x14366: // $014366: ADD.W D3,D3
            alu(ADD,2,{D,3},{D,3},false,false); pc=0x14368;break;
        case 0x14368: // $014368: BTST #9,D1
            bit(BTST,{I,0,0x9},{D,1}); pc=0x1436c;break;
        case 0x1436c: // $01436C: BEQ $014370
            if(branch(EQ,0x014370,false)) continue; pc=0x1436e;break;
        case 0x1436e: // $01436E: ADDQ.W #1,D3
            alu(ADD,2,{I,0,0x1},{D,3},false,true); pc=0x14370;break;
        case 0x14370: // $014370: AND.W D4,D3
            alu(AND,2,{D,4},{D,3},false,false); pc=0x14372;break;
        case 0x14372: // $014372: SUB.W D2,D3
            alu(SUB,2,{D,2},{D,3},false,false); pc=0x14374;break;
        case 0x14374: // $014374: ASL.W #2,D3
            shift(ASL,2,{I,0,0x2},3); pc=0x14376;break;
        case 0x14376: // $014376: MOVE.W D3,(A3)+
            move(2,{D,3},{P,3},false); pc=0x14378;break;
        case 0x14378: // $014378: LSL.W #8,D2
            shift(LSL,2,{I,0,0x8},2); pc=0x1437a;break;
        case 0x1437a: // $01437A: MOVE.W D2,(A3)+
            move(2,{D,2},{P,3},false); pc=0x1437c;break;
        case 0x1437c: // $01437C: LSR.W #4,D0
            shift(LSR,2,{I,0,0x4},0); pc=0x1437e;break;
        case 0x1437e: // $01437E: ADD.W D0,D0
            alu(ADD,2,{D,0},{D,0},false,false); pc=0x14380;break;
        case 0x14380: // $014380: BTST #7,D0
            bit(BTST,{I,0,0x7},{D,0}); pc=0x14384;break;
        case 0x14384: // $014384: BEQ $014388
            if(branch(EQ,0x014388,false)) continue; pc=0x14386;break;
        case 0x14386: // $014386: ADDQ.W #1,D0
            alu(ADD,2,{I,0,0x1},{D,0},false,true); pc=0x14388;break;
        case 0x14388: // $014388: AND.W D4,D0
            alu(AND,2,{D,4},{D,0},false,false); pc=0x1438a;break;
        case 0x1438a: // $01438A: LSR.W #4,D1
            shift(LSR,2,{I,0,0x4},1); pc=0x1438c;break;
        case 0x1438c: // $01438C: ADD.W D1,D1
            alu(ADD,2,{D,1},{D,1},false,false); pc=0x1438e;break;
        case 0x1438e: // $01438E: BTST #7,D1
            bit(BTST,{I,0,0x7},{D,1}); pc=0x14392;break;
        case 0x14392: // $014392: BEQ $014396
            if(branch(EQ,0x014396,false)) continue; pc=0x14394;break;
        case 0x14394: // $014394: ADDQ.W #1,D1
            alu(ADD,2,{I,0,0x1},{D,1},false,true); pc=0x14396;break;
        case 0x14396: // $014396: AND.W D4,D1
            alu(AND,2,{D,4},{D,1},false,false); pc=0x14398;break;
        case 0x14398: // $014398: SUB.W D0,D1
            alu(SUB,2,{D,0},{D,1},false,false); pc=0x1439a;break;
        case 0x1439a: // $01439A: ASL.W #2,D1
            shift(ASL,2,{I,0,0x2},1); pc=0x1439c;break;
        case 0x1439c: // $01439C: MOVE.W D1,(A3)+
            move(2,{D,1},{P,3},false); pc=0x1439e;break;
        case 0x1439e: // $01439E: LSL.W #8,D0
            shift(LSL,2,{I,0,0x8},0); pc=0x143a0;break;
        case 0x143a0: // $0143A0: MOVE.W D0,(A3)+
            move(2,{D,0},{P,3},false); pc=0x143a2;break;
        case 0x143a2: // $0143A2: DBF D7,$014326
            if(db(F,7,0x014326)) continue; pc=0x143a6;break;
        case 0x143a6: // $0143A6: RTS
            if(ret()) return returned;continue; pc=0x143a8;break;
        case 0x143a8: // $0143A8: BTST #1,D7
            bit(BTST,{I,0,0x1},{D,7}); pc=0x143ac;break;
        case 0x143ac: // $0143AC: BEQ $014422
            if(branch(EQ,0x014422,false)) continue; pc=0x143ae;break;
        case 0x143ae: // $0143AE: LEA ($000CC000).L,A1
            lea({L,0,0xcc000},1); pc=0x143b4;break;
        case 0x143b4: // $0143B4: LEA 36(A1),A0
            lea({O,1,36},0); pc=0x143b8;break;
        case 0x143b8: // $0143B8: MOVEQ #16,D7
            moveq({I,0,0x10},7); pc=0x143ba;break;
        case 0x143ba: // $0143BA: MOVEQ #15,D2
            moveq({I,0,0xf},2); pc=0x143bc;break;
        case 0x143bc: // $0143BC: MOVE.W #$F0,D3
            move(2,{I,0,0xf0},{D,3},false); pc=0x143c0;break;
        case 0x143c0: // $0143C0: MOVE.W #$F00,D4
            move(2,{I,0,0xf00},{D,4},false); pc=0x143c4;break;
        case 0x143c4: // $0143C4: MOVEQ #0,D1
            moveq({I,0,0x0},1); pc=0x143c6;break;
        case 0x143c6: // $0143C6: MOVE.W (A0)+,D0
            move(2,{P,0},{D,0},false); pc=0x143c8;break;
        case 0x143c8: // $0143C8: ADD.W D0,(A0)+
            alu(ADD,2,{D,0},{P,0},false,false); pc=0x143ca;break;
        case 0x143ca: // $0143CA: MOVE.B -2(A0),D0
            move(1,{O,0,-2},{D,0},false); pc=0x143ce;break;
        case 0x143ce: // $0143CE: BTST #0,D0
            bit(BTST,{I,0,0x0},{D,0}); pc=0x143d2;break;
        case 0x143d2: // $0143D2: BEQ $0143D8
            if(branch(EQ,0x0143d8,false)) continue; pc=0x143d4;break;
        case 0x143d4: // $0143D4: BSET #12,D1
            bit(BSET,{I,0,0xc},{D,1}); pc=0x143d8;break;
        case 0x143d8: // $0143D8: LSR.B #1,D0
            shift(LSR,1,{I,0,0x1},0); pc=0x143da;break;
        case 0x143da: // $0143DA: AND.W D2,D0
            alu(AND,2,{D,2},{D,0},false,false); pc=0x143dc;break;
        case 0x143dc: // $0143DC: MOVE.B D0,D1
            move(1,{D,0},{D,1},false); pc=0x143de;break;
        case 0x143de: // $0143DE: MOVE.W (A0)+,D0
            move(2,{P,0},{D,0},false); pc=0x143e0;break;
        case 0x143e0: // $0143E0: ADD.W D0,(A0)+
            alu(ADD,2,{D,0},{P,0},false,false); pc=0x143e2;break;
        case 0x143e2: // $0143E2: MOVE.B -2(A0),D0
            move(1,{O,0,-2},{D,0},false); pc=0x143e6;break;
        case 0x143e6: // $0143E6: BTST #0,D0
            bit(BTST,{I,0,0x0},{D,0}); pc=0x143ea;break;
        case 0x143ea: // $0143EA: BEQ $0143F0
            if(branch(EQ,0x0143f0,false)) continue; pc=0x143ec;break;
        case 0x143ec: // $0143EC: BSET #13,D1
            bit(BSET,{I,0,0xd},{D,1}); pc=0x143f0;break;
        case 0x143f0: // $0143F0: LSL.B #3,D0
            shift(LSL,1,{I,0,0x3},0); pc=0x143f2;break;
        case 0x143f2: // $0143F2: AND.W D3,D0
            alu(AND,2,{D,3},{D,0},false,false); pc=0x143f4;break;
        case 0x143f4: // $0143F4: OR.B D0,D1
            alu(OR,1,{D,0},{D,1},false,false); pc=0x143f6;break;
        case 0x143f6: // $0143F6: MOVE.W (A0)+,D0
            move(2,{P,0},{D,0},false); pc=0x143f8;break;
        case 0x143f8: // $0143F8: ADD.W (A0),D0
            alu(ADD,2,{M,0},{D,0},false,false); pc=0x143fa;break;
        case 0x143fa: // $0143FA: MOVE.W D0,(A0)+
            move(2,{D,0},{P,0},false); pc=0x143fc;break;
        case 0x143fc: // $0143FC: BTST #8,D0
            bit(BTST,{I,0,0x8},{D,0}); pc=0x14400;break;
        case 0x14400: // $014400: BEQ $014406
            if(branch(EQ,0x014406,false)) continue; pc=0x14402;break;
        case 0x14402: // $014402: BSET #14,D1
            bit(BSET,{I,0,0xe},{D,1}); pc=0x14406;break;
        case 0x14406: // $014406: LSR.W #1,D0
            shift(LSR,2,{I,0,0x1},0); pc=0x14408;break;
        case 0x14408: // $014408: AND.W D4,D0
            alu(AND,2,{D,4},{D,0},false,false); pc=0x1440a;break;
        case 0x1440a: // $01440A: OR.W D0,D1
            alu(OR,2,{D,0},{D,1},false,false); pc=0x1440c;break;
        case 0x1440c: // $01440C: MOVE.W D1,(A1)+
            move(2,{D,1},{P,1},false); pc=0x1440e;break;
        case 0x1440e: // $01440E: DBF D7,$0143C4
            if(db(F,7,0x0143c4)) continue; pc=0x14412;break;
        case 0x14412: // $014412: SUBQ.B #1,($000CDD09).L
            alu(SUB,1,{I,0,0x1},{L,0,0xcdd09},false,true); pc=0x14418;break;
        case 0x14418: // $014418: BNE $014422
            if(branch(NE,0x014422,false)) continue; pc=0x1441a;break;
        case 0x1441a: // $01441A: BCLR #1,($000CDD08).L
            bit(BCLR,{I,0,0x1},{L,0,0xcdd08}); pc=0x14422;break;
        case 0x14422: // $014422: RTS
            if(ret()) return returned;continue; pc=0x14424;break;
        case 0x14424: // $014424: LEA ($000CC000).L,A0
            lea({L,0,0xcc000},0); pc=0x1442a;break;
        case 0x1442a: // $01442A: MOVE.W (A0)+,D0
            move(2,{P,0},{D,0},false); pc=0x1442c;break;
        case 0x1442c: // $01442C: MOVE.W D0,D1
            move(2,{D,0},{D,1},false); pc=0x1442e;break;
        case 0x1442e: // $01442E: SWAP D0
            swap(0); pc=0x14430;break;
        case 0x14430: // $014430: MOVE.W D1,D0
            move(2,{D,1},{D,0},false); pc=0x14432;break;
        case 0x14432: // $014432: LEA ($00122F00).L,A2
            lea({L,0,0x122f00},2); pc=0x14438;break;
        case 0x14438: // $014438: MOVE.W D0,(A2)+
            move(2,{D,0},{P,2},false); pc=0x1443a;break;
        case 0x1443a: // $01443A: BTST #0,($8C2E).W
            bit(BTST,{I,0,0x0},{W,0,0xffff8c2e}); pc=0x14440;break;
        case 0x14440: // $014440: BNE $014488
            if(branch(NE,0x014488,true)) continue; pc=0x14444;break;
        case 0x14444: // $014444: LEA ($00122E40).L,A1
            lea({L,0,0x122e40},1); pc=0x1444a;break;
        case 0x1444a: // $01444A: MOVE.L D0,(A1)+
            move(4,{D,0},{P,1},false); pc=0x1444c;break;
        case 0x1444c: // $01444C: MOVE.L D0,(A1)+
            move(4,{D,0},{P,1},false); pc=0x1444e;break;
        case 0x1444e: // $01444E: MOVE.L D0,(A1)+
            move(4,{D,0},{P,1},false); pc=0x14450;break;
        case 0x14450: // $014450: MOVE.L D0,(A1)+
            move(4,{D,0},{P,1},false); pc=0x14452;break;
        case 0x14452: // $014452: MOVE.L D0,(A1)+
            move(4,{D,0},{P,1},false); pc=0x14454;break;
        case 0x14454: // $014454: MOVE.L D0,(A1)+
            move(4,{D,0},{P,1},false); pc=0x14456;break;
        case 0x14456: // $014456: MOVE.L D0,(A1)+
            move(4,{D,0},{P,1},false); pc=0x14458;break;
        case 0x14458: // $014458: MOVE.L D0,(A1)+
            move(4,{D,0},{P,1},false); pc=0x1445a;break;
        case 0x1445a: // $01445A: LEA ($00122E00).L,A1
            lea({L,0,0x122e00},1); pc=0x14460;break;
        case 0x14460: // $014460: MOVE.W (A0)+,D0
            move(2,{P,0},{D,0},false); pc=0x14462;break;
        case 0x14462: // $014462: MOVE.W D0,D1
            move(2,{D,0},{D,1},false); pc=0x14464;break;
        case 0x14464: // $014464: SWAP D0
            swap(0); pc=0x14466;break;
        case 0x14466: // $014466: MOVE.W D1,D0
            move(2,{D,1},{D,0},false); pc=0x14468;break;
        case 0x14468: // $014468: MOVE.L D0,(A1)+
            move(4,{D,0},{P,1},false); pc=0x1446a;break;
        case 0x1446a: // $01446A: MOVE.L D0,(A1)+
            move(4,{D,0},{P,1},false); pc=0x1446c;break;
        case 0x1446c: // $01446C: MOVE.L D0,(A1)+
            move(4,{D,0},{P,1},false); pc=0x1446e;break;
        case 0x1446e: // $01446E: MOVE.L D0,(A1)+
            move(4,{D,0},{P,1},false); pc=0x14470;break;
        case 0x14470: // $014470: ADDQ.W #2,A0
            alu(ADD,2,{I,0,0x2},{A,0},false,true); pc=0x14472;break;
        case 0x14472: // $014472: LEA ($00121D02).L,A1
            lea({L,0,0x121d02},1); pc=0x14478;break;
        case 0x14478: // $014478: MOVE.L (A0)+,(A1)+
            move(4,{P,0},{P,1},false); pc=0x1447a;break;
        case 0x1447a: // $01447A: MOVE.L (A0)+,(A1)+
            move(4,{P,0},{P,1},false); pc=0x1447c;break;
        case 0x1447c: // $01447C: MOVE.L (A0)+,(A1)+
            move(4,{P,0},{P,1},false); pc=0x1447e;break;
        case 0x1447e: // $01447E: MOVE.L (A0)+,(A1)+
            move(4,{P,0},{P,1},false); pc=0x14480;break;
        case 0x14480: // $014480: MOVE.L (A0)+,(A1)+
            move(4,{P,0},{P,1},false); pc=0x14482;break;
        case 0x14482: // $014482: MOVE.L (A0)+,(A1)+
            move(4,{P,0},{P,1},false); pc=0x14484;break;
        case 0x14484: // $014484: MOVE.L (A0)+,(A1)+
            move(4,{P,0},{P,1},false); pc=0x14486;break;
        case 0x14486: // $014486: RTS
            if(ret()) return returned;continue; pc=0x14488;break;
        case 0x14488: // $014488: MOVE.L (A0)+,($00122E60).L
            move(4,{P,0},{L,0,0x122e60},false); pc=0x1448e;break;
        case 0x1448e: // $01448E: MOVE.L (A0)+,(A2)+
            move(4,{P,0},{P,2},false); pc=0x14490;break;
        case 0x14490: // $014490: MOVE.L (A0)+,(A2)+
            move(4,{P,0},{P,2},false); pc=0x14492;break;
        case 0x14492: // $014492: MOVE.L (A0)+,(A2)+
            move(4,{P,0},{P,2},false); pc=0x14494;break;
        case 0x14494: // $014494: MOVE.L (A0)+,(A2)+
            move(4,{P,0},{P,2},false); pc=0x14496;break;
        case 0x14496: // $014496: MOVE.L (A0)+,(A2)+
            move(4,{P,0},{P,2},false); pc=0x14498;break;
        case 0x14498: // $014498: MOVE.L (A0)+,(A2)+
            move(4,{P,0},{P,2},false); pc=0x1449a;break;
        case 0x1449a: // $01449A: MOVE.L (A0)+,(A2)+
            move(4,{P,0},{P,2},false); pc=0x1449c;break;
        case 0x1449c: // $01449C: RTS
            if(ret()) return returned;continue; pc=0x1449e;break;
        case 0x14a86: // $014A86: BCLR #0,($000CDD07).L
            bit(BCLR,{I,0,0x0},{L,0,0xcdd07}); pc=0x14a8e;break;
        case 0x14a8e: // $014A8E: BEQ $014ADA
            if(branch(EQ,0x014ada,false)) continue; pc=0x14a90;break;
        case 0x14a90: // $014A90: MOVE.B ($000CDD04).L,D0
            move(1,{L,0,0xcdd04},{D,0},false); pc=0x14a96;break;
        case 0x14a96: // $014A96: ANDI.B #$1F,D0
            alu(AND,1,{I,0,0x1f},{D,0},true,false); pc=0x14a9a;break;
        case 0x14a9a: // $014A9A: EORI.B #$1F,D0
            alu(EOR,1,{I,0,0x1f},{D,0},true,false); pc=0x14a9e;break;
        case 0x14a9e: // $014A9E: MOVE.B D0,($000CDD05).L
            move(1,{D,0},{L,0,0xcdd05},false); pc=0x14aa4;break;
        case 0x14aa4: // $014AA4: CLR.B ($0029C54C).L
            unary(true,1,{L,0,0x29c54c}); pc=0x14aaa;break;
        case 0x14aaa: // $014AAA: CLR.B ($000CDD08).L
            unary(true,1,{L,0,0xcdd08}); pc=0x14ab0;break;
        case 0x14ab0: // $014AB0: MOVEA.L #$CDC00,A1
            move(4,{I,0,0xcdc00},{A,1},true); pc=0x14ab6;break;
        case 0x14ab6: // $014AB6: MOVE.W ($00122F00).L,(A1)+
            move(2,{L,0,0x122f00},{P,1},false); pc=0x14abc;break;
        case 0x14abc: // $014ABC: MOVEA.L #$122E00,A0
            move(4,{I,0,0x122e00},{A,0},true); pc=0x14ac2;break;
        case 0x14ac2: // $014AC2: MOVEQ #15,D1
            moveq({I,0,0xf},1); pc=0x14ac4;break;
        case 0x14ac4: // $014AC4: MOVE.L (A0)+,(A1)+
            move(4,{P,0},{P,1},false); pc=0x14ac6;break;
        case 0x14ac6: // $014AC6: DBF D1,$014AC4
            if(db(F,1,0x014ac4)) continue; pc=0x14aca;break;
        case 0x14aca: // $014ACA: MOVE.L ($00122E40).L,(A1)+
            move(4,{L,0,0x122e40},{P,1},false); pc=0x14ad0;break;
        case 0x14ad0: // $014AD0: MOVE.L ($00122E60).L,(A1)+
            move(4,{L,0,0x122e60},{P,1},false); pc=0x14ad6;break;
        case 0x14ad6: // $014AD6: MOVEQ #0,D0
            moveq({I,0,0x0},0); pc=0x14ad8;break;
        case 0x14ad8: // $014AD8: BT $014AE4
            if(branch(T,0x014ae4,false)) continue; pc=0x14ada;break;
        case 0x14ada: // $014ADA: MOVE.W ($000CDD02).L,D0
            move(2,{L,0,0xcdd02},{D,0},false); pc=0x14ae0;break;
        case 0x14ae0: // $014AE0: BEQ $014B2A
            if(branch(EQ,0x014b2a,true)) continue; pc=0x14ae4;break;
        case 0x14ae4: // $014AE4: ANDI.W #$3,D0
            alu(AND,2,{I,0,0x3},{D,0},true,false); pc=0x14ae8;break;
        case 0x14ae8: // $014AE8: MOVE.W D0,D1
            move(2,{D,0},{D,1},false); pc=0x14aea;break;
        case 0x14aea: // $014AEA: MOVEQ #11,D2
            moveq({I,0,0xb},2); pc=0x14aec;break;
        case 0x14aec: // $014AEC: LSL.W D2,D1
            shift(LSL,2,{D,2},1); pc=0x14aee;break;
        case 0x14aee: // $014AEE: MOVEA.L #$120002,A0
            move(4,{I,0,0x120002},{A,0},true); pc=0x14af4;break;
        case 0x14af4: // $014AF4: ADDA.W D1,A0
            adda(2,{D,1},0,false); pc=0x14af6;break;
        case 0x14af6: // $014AF6: MOVE.W D0,D1
            move(2,{D,0},{D,1},false); pc=0x14af8;break;
        case 0x14af8: // $014AF8: MULU.W #$700,D1
            mulu({I,0,0x700},1); pc=0x14afc;break;
        case 0x14afc: // $014AFC: ADDI.L #$CC000,D1
            alu(ADD,4,{I,0,0xcc000},{D,1},true,false); pc=0x14b02;break;
        case 0x14b02: // $014B02: MOVEA.L D1,A1
            move(4,{D,1},{A,1},true); pc=0x14b04;break;
        case 0x14b04: // $014B04: ADDQ.W #1,D0
            alu(ADD,2,{I,0,0x1},{D,0},false,true); pc=0x14b06;break;
        case 0x14b06: // $014B06: ANDI.W #$3,D0
            alu(AND,2,{I,0,0x3},{D,0},true,false); pc=0x14b0a;break;
        case 0x14b0a: // $014B0A: MOVE.W D0,($000CDD02).L
            move(2,{D,0},{L,0,0xcdd02},false); pc=0x14b10;break;
        case 0x14b10: // $014B10: MOVE.W #$3F,D0
            move(2,{I,0,0x3f},{D,0},false); pc=0x14b14;break;
        case 0x14b14: // $014B14: MOVE.L (A0)+,(A1)+
            move(4,{P,0},{P,1},false); pc=0x14b16;break;
        case 0x14b16: // $014B16: MOVE.L (A0)+,(A1)+
            move(4,{P,0},{P,1},false); pc=0x14b18;break;
        case 0x14b18: // $014B18: MOVE.L (A0)+,(A1)+
            move(4,{P,0},{P,1},false); pc=0x14b1a;break;
        case 0x14b1a: // $014B1A: MOVE.L (A0)+,(A1)+
            move(4,{P,0},{P,1},false); pc=0x14b1c;break;
        case 0x14b1c: // $014B1C: MOVE.L (A0)+,(A1)+
            move(4,{P,0},{P,1},false); pc=0x14b1e;break;
        case 0x14b1e: // $014B1E: MOVE.L (A0)+,(A1)+
            move(4,{P,0},{P,1},false); pc=0x14b20;break;
        case 0x14b20: // $014B20: MOVE.L (A0)+,(A1)+
            move(4,{P,0},{P,1},false); pc=0x14b22;break;
        case 0x14b22: // $014B22: ADDQ.L #4,A0
            alu(ADD,4,{I,0,0x4},{A,0},false,true); pc=0x14b24;break;
        case 0x14b24: // $014B24: DBF D0,$014B14
            if(db(F,0,0x014b14)) continue; pc=0x14b28;break;
        case 0x14b28: // $014B28: RTS
            if(ret()) return returned;continue; pc=0x14b2a;break;
        case 0x14b2a: // $014B2A: TST.B ($000CDD05).L
            unary(false,1,{L,0,0xcdd05}); pc=0x14b30;break;
        case 0x14b30: // $014B30: BEQ $014C34
            if(branch(EQ,0x014c34,true)) continue; pc=0x14b34;break;
        case 0x14b34: // $014B34: MOVE.W ($000CDD00).L,D0
            move(2,{L,0,0xcdd00},{D,0},false); pc=0x14b3a;break;
        case 0x14b3a: // $014B3A: MOVE.W D0,D1
            move(2,{D,0},{D,1},false); pc=0x14b3c;break;
        case 0x14b3c: // $014B3C: MOVEQ #10,D2
            moveq({I,0,0xa},2); pc=0x14b3e;break;
        case 0x14b3e: // $014B3E: LSL.W D2,D1
            shift(LSL,2,{D,2},1); pc=0x14b40;break;
        case 0x14b40: // $014B40: MOVEA.L #$120002,A1
            move(4,{I,0,0x120002},{A,1},true); pc=0x14b46;break;
        case 0x14b46: // $014B46: ADDA.W D1,A1
            adda(2,{D,1},1,false); pc=0x14b48;break;
        case 0x14b48: // $014B48: MOVE.W D0,D1
            move(2,{D,0},{D,1},false); pc=0x14b4a;break;
        case 0x14b4a: // $014B4A: MULU.W #$380,D1
            mulu({I,0,0x380},1); pc=0x14b4e;break;
        case 0x14b4e: // $014B4E: ADDI.L #$CC000,D1
            alu(ADD,4,{I,0,0xcc000},{D,1},true,false); pc=0x14b54;break;
        case 0x14b54: // $014B54: MOVEA.L D1,A0
            move(4,{D,1},{A,0},true); pc=0x14b56;break;
        case 0x14b56: // $014B56: MOVEQ #31,D1
            moveq({I,0,0x1f},1); pc=0x14b58;break;
        case 0x14b58: // $014B58: MOVE.L (A0)+,(A1)+
            move(4,{P,0},{P,1},false); pc=0x14b5a;break;
        case 0x14b5a: // $014B5A: MOVE.L (A0)+,(A1)+
            move(4,{P,0},{P,1},false); pc=0x14b5c;break;
        case 0x14b5c: // $014B5C: MOVE.L (A0)+,(A1)+
            move(4,{P,0},{P,1},false); pc=0x14b5e;break;
        case 0x14b5e: // $014B5E: MOVE.L (A0)+,(A1)+
            move(4,{P,0},{P,1},false); pc=0x14b60;break;
        case 0x14b60: // $014B60: MOVE.L (A0)+,(A1)+
            move(4,{P,0},{P,1},false); pc=0x14b62;break;
        case 0x14b62: // $014B62: MOVE.L (A0)+,(A1)+
            move(4,{P,0},{P,1},false); pc=0x14b64;break;
        case 0x14b64: // $014B64: MOVE.L (A0)+,(A1)+
            move(4,{P,0},{P,1},false); pc=0x14b66;break;
        case 0x14b66: // $014B66: ADDQ.L #4,A1
            alu(ADD,4,{I,0,0x4},{A,1},false,true); pc=0x14b68;break;
        case 0x14b68: // $014B68: DBF D1,$014B58
            if(db(F,1,0x014b58)) continue; pc=0x14b6c;break;
        case 0x14b6c: // $014B6C: ADDQ.W #1,D0
            alu(ADD,2,{I,0,0x1},{D,0},false,true); pc=0x14b6e;break;
        case 0x14b6e: // $014B6E: ANDI.W #$7,D0
            alu(AND,2,{I,0,0x7},{D,0},true,false); pc=0x14b72;break;
        case 0x14b72: // $014B72: MOVE.W D0,($000CDD00).L
            move(2,{D,0},{L,0,0xcdd00},false); pc=0x14b78;break;
        case 0x14b78: // $014B78: BNE $014BB2
            if(branch(NE,0x014bb2,true)) continue; pc=0x14b7c;break;
        case 0x14b7c: // $014B7C: LEA ($000CD960).L,A2
            lea({L,0,0xcd960},2); pc=0x14b82;break;
        case 0x14b82: // $014B82: LEA ($00122F00).L,A1
            lea({L,0,0x122f00},1); pc=0x14b88;break;
        case 0x14b88: // $014B88: MOVE.W (A0)+,(A1)+
            move(2,{P,0},{P,1},false); pc=0x14b8a;break;
        case 0x14b8a: // $014B8A: MOVE.L (A2)+,(A1)+
            move(4,{P,2},{P,1},false); pc=0x14b8c;break;
        case 0x14b8c: // $014B8C: MOVE.L (A2)+,(A1)+
            move(4,{P,2},{P,1},false); pc=0x14b8e;break;
        case 0x14b8e: // $014B8E: MOVE.L (A2)+,(A1)+
            move(4,{P,2},{P,1},false); pc=0x14b90;break;
        case 0x14b90: // $014B90: MOVE.L (A2)+,(A1)+
            move(4,{P,2},{P,1},false); pc=0x14b92;break;
        case 0x14b92: // $014B92: MOVE.L (A2)+,(A1)+
            move(4,{P,2},{P,1},false); pc=0x14b94;break;
        case 0x14b94: // $014B94: MOVE.L (A2)+,(A1)+
            move(4,{P,2},{P,1},false); pc=0x14b96;break;
        case 0x14b96: // $014B96: MOVE.L (A2)+,(A1)+
            move(4,{P,2},{P,1},false); pc=0x14b98;break;
        case 0x14b98: // $014B98: MOVEA.L #$122E00,A1
            move(4,{I,0,0x122e00},{A,1},true); pc=0x14b9e;break;
        case 0x14b9e: // $014B9E: MOVEQ #15,D1
            moveq({I,0,0xf},1); pc=0x14ba0;break;
        case 0x14ba0: // $014BA0: MOVE.L (A0)+,(A1)+
            move(4,{P,0},{P,1},false); pc=0x14ba2;break;
        case 0x14ba2: // $014BA2: DBF D1,$014BA0
            if(db(F,1,0x014ba0)) continue; pc=0x14ba6;break;
        case 0x14ba6: // $014BA6: MOVE.L (A0)+,($00122E40).L
            move(4,{P,0},{L,0,0x122e40},false); pc=0x14bac;break;
        case 0x14bac: // $014BAC: MOVE.L (A0)+,($00122E60).L
            move(4,{P,0},{L,0,0x122e60},false); pc=0x14bb2;break;
        case 0x14bb2: // $014BB2: MOVE.W D0,D1
            move(2,{D,0},{D,1},false); pc=0x14bb4;break;
        case 0x14bb4: // $014BB4: MULU.W #$380,D0
            mulu({I,0,0x380},0); pc=0x14bb8;break;
        case 0x14bb8: // $014BB8: ADDI.L #$CC000,D0
            alu(ADD,4,{I,0,0xcc000},{D,0},true,false); pc=0x14bbe;break;
        case 0x14bbe: // $014BBE: MOVEA.L D0,A0
            move(4,{D,0},{A,0},true); pc=0x14bc0;break;
        case 0x14bc0: // $014BC0: MOVE.B ($000CDD06).L,D6
            move(1,{L,0,0xcdd06},{D,6},false); pc=0x14bc6;break;
        case 0x14bc6: // $014BC6: MOVEQ #31,D4
            moveq({I,0,0x1f},4); pc=0x14bc8;break;
        case 0x14bc8: // $014BC8: CMPI.W #$7,D1
            alu(CMP,2,{I,0,0x7},{D,1},true,false); pc=0x14bcc;break;
        case 0x14bcc: // $014BCC: BNE $014BD0
            if(branch(NE,0x014bd0,false)) continue; pc=0x14bce;break;
        case 0x14bce: // $014BCE: ADDQ.W #3,D4
            alu(ADD,2,{I,0,0x3},{D,4},false,true); pc=0x14bd0;break;
        case 0x14bd0: // $014BD0: MOVEQ #13,D3
            moveq({I,0,0xd},3); pc=0x14bd2;break;
        case 0x14bd2: // $014BD2: MOVE.B D6,D5
            move(1,{D,6},{D,5},false); pc=0x14bd4;break;
        case 0x14bd4: // $014BD4: MOVEQ #0,D2
            moveq({I,0,0x0},2); pc=0x14bd6;break;
        case 0x14bd6: // $014BD6: MOVE.W (A0),D0
            move(2,{M,0},{D,0},false); pc=0x14bd8;break;
        case 0x14bd8: // $014BD8: LSR.B #1,D5
            shift(LSR,1,{I,0,0x1},5); pc=0x14bda;break;
        case 0x14bda: // $014BDA: BCS $014BEE
            if(branch(CS,0x014bee,false)) continue; pc=0x14bdc;break;
        case 0x14bdc: // $014BDC: MOVE.W D0,D1
            move(2,{D,0},{D,1},false); pc=0x14bde;break;
        case 0x14bde: // $014BDE: ANDI.W #$100F,D1
            alu(AND,2,{I,0,0x100f},{D,1},true,false); pc=0x14be2;break;
        case 0x14be2: // $014BE2: BEQ $014BEE
            if(branch(EQ,0x014bee,false)) continue; pc=0x14be4;break;
        case 0x14be4: // $014BE4: BCHG #12,D1
            bit(BCHG,{I,0,0xc},{D,1}); pc=0x14be8;break;
        case 0x14be8: // $014BE8: BNE $014BEC
            if(branch(NE,0x014bec,false)) continue; pc=0x14bea;break;
        case 0x14bea: // $014BEA: SUBQ.W #1,D1
            alu(SUB,2,{I,0,0x1},{D,1},false,true); pc=0x14bec;break;
        case 0x14bec: // $014BEC: MOVE.W D1,D2
            move(2,{D,1},{D,2},false); pc=0x14bee;break;
        case 0x14bee: // $014BEE: LSR.B #1,D5
            shift(LSR,1,{I,0,0x1},5); pc=0x14bf0;break;
        case 0x14bf0: // $014BF0: BCS $014C06
            if(branch(CS,0x014c06,false)) continue; pc=0x14bf2;break;
        case 0x14bf2: // $014BF2: MOVE.W D0,D1
            move(2,{D,0},{D,1},false); pc=0x14bf4;break;
        case 0x14bf4: // $014BF4: ANDI.W #$20F0,D1
            alu(AND,2,{I,0,0x20f0},{D,1},true,false); pc=0x14bf8;break;
        case 0x14bf8: // $014BF8: BEQ $014C06
            if(branch(EQ,0x014c06,false)) continue; pc=0x14bfa;break;
        case 0x14bfa: // $014BFA: BCHG #13,D1
            bit(BCHG,{I,0,0xd},{D,1}); pc=0x14bfe;break;
        case 0x14bfe: // $014BFE: BNE $014C04
            if(branch(NE,0x014c04,false)) continue; pc=0x14c00;break;
        case 0x14c00: // $014C00: SUBI.W #$10,D1
            alu(SUB,2,{I,0,0x10},{D,1},true,false); pc=0x14c04;break;
        case 0x14c04: // $014C04: OR.W D1,D2
            alu(OR,2,{D,1},{D,2},false,false); pc=0x14c06;break;
        case 0x14c06: // $014C06: LSR.B #1,D5
            shift(LSR,1,{I,0,0x1},5); pc=0x14c08;break;
        case 0x14c08: // $014C08: BCS $014C1C
            if(branch(CS,0x014c1c,false)) continue; pc=0x14c0a;break;
        case 0x14c0a: // $014C0A: ANDI.W #$4F00,D0
            alu(AND,2,{I,0,0x4f00},{D,0},true,false); pc=0x14c0e;break;
        case 0x14c0e: // $014C0E: BEQ $014C1C
            if(branch(EQ,0x014c1c,false)) continue; pc=0x14c10;break;
        case 0x14c10: // $014C10: BCHG #14,D0
            bit(BCHG,{I,0,0xe},{D,0}); pc=0x14c14;break;
        case 0x14c14: // $014C14: BNE $014C1A
            if(branch(NE,0x014c1a,false)) continue; pc=0x14c16;break;
        case 0x14c16: // $014C16: SUBI.W #$100,D0
            alu(SUB,2,{I,0,0x100},{D,0},true,false); pc=0x14c1a;break;
        case 0x14c1a: // $014C1A: OR.W D0,D2
            alu(OR,2,{D,0},{D,2},false,false); pc=0x14c1c;break;
        case 0x14c1c: // $014C1C: MOVE.W D2,(A0)+
            move(2,{D,2},{P,0},false); pc=0x14c1e;break;
        case 0x14c1e: // $014C1E: DBF D3,$014BD2
            if(db(F,3,0x014bd2)) continue; pc=0x14c22;break;
        case 0x14c22: // $014C22: DBF D4,$014BD0
            if(db(F,4,0x014bd0)) continue; pc=0x14c26;break;
        case 0x14c26: // $014C26: TST.W ($000CDD00).L
            unary(false,2,{L,0,0xcdd00}); pc=0x14c2c;break;
        case 0x14c2c: // $014C2C: BNE $014C34
            if(branch(NE,0x014c34,false)) continue; pc=0x14c2e;break;
        case 0x14c2e: // $014C2E: SUBQ.B #1,($000CDD05).L
            alu(SUB,1,{I,0,0x1},{L,0,0xcdd05},false,true); pc=0x14c34;break;
        case 0x14c34: // $014C34: RTS
            if(ret()) return returned;continue; pc=0x14c36;break;
        case 0x16c8a: // $016C8A: BTST #1,($9102).W
            bit(BTST,{I,0,0x1},{W,0,0xffff9102}); pc=0x16c90;break;
        case 0x16c90: // $016C90: BNE $016D22
            if(branch(NE,0x016d22,true)) continue; pc=0x16c94;break;
        case 0x16c94: // $016C94: BTST #0,($9102).W
            bit(BTST,{I,0,0x0},{W,0,0xffff9102}); pc=0x16c9a;break;
        case 0x16c9a: // $016C9A: BEQ $016CD8
            if(branch(EQ,0x016cd8,true)) continue; pc=0x16c9e;break;
        case 0x16c9e: // $016C9E: MOVE.W ($0003218A).L,D1
            move(2,{L,0,0x3218a},{D,1},false); pc=0x16ca4;break;
        case 0x16ca4: // $016CA4: MOVE.W ($8D66).W,D0
            move(2,{W,0,0xffff8d66},{D,0},false); pc=0x16ca8;break;
        case 0x16ca8: // $016CA8: ADD.W ($0003218C).L,D0
            alu(ADD,2,{L,0,0x3218c},{D,0},false,false); pc=0x16cae;break;
        case 0x16cae: // $016CAE: ADD.W ($9106).W,D0
            alu(ADD,2,{W,0,0xffff9106},{D,0},false,false); pc=0x16cb2;break;
        case 0x16cb2: // $016CB2: CLR.W ($9106).W
            unary(true,2,{W,0,0xffff9106}); pc=0x16cb6;break;
        case 0x16cb6: // $016CB6: CMP.W D1,D0
            alu(CMP,2,{D,1},{D,0},false,false); pc=0x16cb8;break;
        case 0x16cb8: // $016CB8: BLE $016CBE
            if(branch(LE,0x016cbe,true)) continue; pc=0x16cbc;break;
        case 0x16cbc: // $016CBC: MOVE.W D1,D0
            move(2,{D,1},{D,0},false); pc=0x16cbe;break;
        case 0x16cbe: // $016CBE: SUB.W ($8D66).W,D0
            alu(SUB,2,{W,0,0xffff8d66},{D,0},false,false); pc=0x16cc2;break;
        case 0x16cc2: // $016CC2: MOVE.W D0,($9100).W
            move(2,{D,0},{W,0,0xffff9100},false); pc=0x16cc6;break;
        case 0x16cc6: // $016CC6: MOVE.B #$0,($9102).W
            move(1,{I,0,0x0},{W,0,0xffff9102},false); pc=0x16ccc;break;
        case 0x16ccc: // $016CCC: MOVE.B #$FF,($90FF).W
            move(1,{I,0,0xff},{W,0,0xffff90ff},false); pc=0x16cd2;break;
        case 0x16cd2: // $016CD2: MOVE.B #$E0,($9103).W
            move(1,{I,0,0xe0},{W,0,0xffff9103},false); pc=0x16cd8;break;
        case 0x16cd8: // $016CD8: TST.B ($90FF).W
            unary(false,1,{W,0,0xffff90ff}); pc=0x16cdc;break;
        case 0x16cdc: // $016CDC: BEQ $016D20
            if(branch(EQ,0x016d20,true)) continue; pc=0x16ce0;break;
        case 0x16ce0: // $016CE0: MOVE.B ($9103).W,D0
            move(1,{W,0,0xffff9103},{D,0},false); pc=0x16ce4;break;
        case 0x16ce4: // $016CE4: CMPI.B #$1,D0
            alu(CMP,1,{I,0,0x1},{D,0},true,false); pc=0x16ce8;break;
        case 0x16ce8: // $016CE8: BLT $016D1C
            if(branch(LT,0x016d1c,true)) continue; pc=0x16cec;break;
        case 0x16cec: // $016CEC: MOVE.W ($9100).W,D0
            move(2,{W,0,0xffff9100},{D,0},false); pc=0x16cf0;break;
        case 0x16cf0: // $016CF0: BLE $016D16
            if(branch(LE,0x016d16,true)) continue; pc=0x16cf4;break;
        case 0x16cf4: // $016CF4: SUBQ.W #1,($9100).W
            alu(SUB,2,{I,0,0x1},{W,0,0xffff9100},false,true); pc=0x16cf8;break;
        case 0x16cf8: // $016CF8: ADDQ.W #1,($8D66).W
            alu(ADD,2,{I,0,0x1},{W,0,0xffff8d66},false,true); pc=0x16cfc;break;
        case 0x16cfc: // $016CFC: MOVE.W #$9B,D0
            move(2,{I,0,0x9b},{D,0},false); pc=0x16d00;break;
        case 0x16d00: // $016D00: JSR ($0000B84C).L
            call({L,0,0xb84c},0x16d06,false);continue; pc=0x16d06;break;
        case 0x16d06: // $016D06: JSR ($0000CFC4).L
            call({L,0,0xcfc4},0x16d0c,false);continue; pc=0x16d0c;break;
        case 0x16d0c: // $016D0C: MOVE.B #$0,($9103).W
            move(1,{I,0,0x0},{W,0,0xffff9103},false); pc=0x16d12;break;
        case 0x16d12: // $016D12: BT $016D20
            if(branch(T,0x016d20,true)) continue; pc=0x16d16;break;
        case 0x16d16: // $016D16: MOVE.B #$0,($90FF).W
            move(1,{I,0,0x0},{W,0,0xffff90ff},false); pc=0x16d1c;break;
        case 0x16d1c: // $016D1C: ADDQ.B #1,($9103).W
            alu(ADD,1,{I,0,0x1},{W,0,0xffff9103},false,true); pc=0x16d20;break;
        case 0x16d20: // $016D20: RTS
            if(ret()) return returned;continue; pc=0x16d22;break;
        case 0x16d22: // $016D22: MOVE.W ($0003218C).L,D1
            move(2,{L,0,0x3218c},{D,1},false); pc=0x16d28;break;
        case 0x16d28: // $016D28: MOVE.W ($8D66).W,D0
            move(2,{W,0,0xffff8d66},{D,0},false); pc=0x16d2c;break;
        case 0x16d2c: // $016D2C: ADD.W ($0003218C).L,D0
            alu(ADD,2,{L,0,0x3218c},{D,0},false,false); pc=0x16d32;break;
        case 0x16d32: // $016D32: CMP.W D1,D0
            alu(CMP,2,{D,1},{D,0},false,false); pc=0x16d34;break;
        case 0x16d34: // $016D34: BLE $016D3A
            if(branch(LE,0x016d3a,true)) continue; pc=0x16d38;break;
        case 0x16d38: // $016D38: MOVE.W D1,D0
            move(2,{D,1},{D,0},false); pc=0x16d3a;break;
        case 0x16d3a: // $016D3A: SUB.W ($8D66).W,D0
            alu(SUB,2,{W,0,0xffff8d66},{D,0},false,false); pc=0x16d3e;break;
        case 0x16d3e: // $016D3E: MOVE.W D0,($9100).W
            move(2,{D,0},{W,0,0xffff9100},false); pc=0x16d42;break;
        case 0x16d42: // $016D42: MOVE.B #$0,($9102).W
            move(1,{I,0,0x0},{W,0,0xffff9102},false); pc=0x16d48;break;
        case 0x16d48: // $016D48: MOVE.B #$FF,($90FF).W
            move(1,{I,0,0xff},{W,0,0xffff90ff},false); pc=0x16d4e;break;
        case 0x16d4e: // $016D4E: MOVE.B #$0,($9103).W
            move(1,{I,0,0x0},{W,0,0xffff9103},false); pc=0x16d54;break;
        case 0x16d54: // $016D54: BT $016CD8
            if(branch(T,0x016cd8,false)) continue; pc=0x16d56;break;
        case 0x18444: // $018444: MOVE.W ($9044).W,D0
            move(2,{W,0,0xffff9044},{D,0},false); pc=0x18448;break;
        case 0x18448: // $018448: BNE $01848E
            if(branch(NE,0x01848e,true)) continue; pc=0x1844c;break;
        case 0x1844c: // $01844C: MOVE.W ($0029C09C).L,D0
            move(2,{L,0,0x29c09c},{D,0},false); pc=0x18452;break;
        case 0x18452: // $018452: CMPI.W #$5,D0
            alu(CMP,2,{I,0,0x5},{D,0},true,false); pc=0x18456;break;
        case 0x18456: // $018456: BEQ $0184A0
            if(branch(EQ,0x0184a0,false)) continue; pc=0x18458;break;
        case 0x18458: // $018458: CMPI.W #$D,D0
            alu(CMP,2,{I,0,0xd},{D,0},true,false); pc=0x1845c;break;
        case 0x1845c: // $01845C: BEQ $0184A0
            if(branch(EQ,0x0184a0,false)) continue; pc=0x1845e;break;
        case 0x1845e: // $01845E: CMPI.W #$1,D0
            alu(CMP,2,{I,0,0x1},{D,0},true,false); pc=0x18462;break;
        case 0x18462: // $018462: BEQ $018798
            if(branch(EQ,0x018798,true)) continue; pc=0x18466;break;
        case 0x18466: // $018466: CMPI.W #$7,D0
            alu(CMP,2,{I,0,0x7},{D,0},true,false); pc=0x1846a;break;
        case 0x1846a: // $01846A: BEQ $018798
            if(branch(EQ,0x018798,true)) continue; pc=0x1846e;break;
        case 0x1846e: // $01846E: CMPI.W #$9,D0
            alu(CMP,2,{I,0,0x9},{D,0},true,false); pc=0x18472;break;
        case 0x18472: // $018472: BEQ $018798
            if(branch(EQ,0x018798,true)) continue; pc=0x18476;break;
        case 0x18476: // $018476: CMPI.W #$F,D0
            alu(CMP,2,{I,0,0xf},{D,0},true,false); pc=0x1847a;break;
        case 0x1847a: // $01847A: BEQ $018798
            if(branch(EQ,0x018798,true)) continue; pc=0x1847e;break;
        case 0x1847e: // $01847E: CMPI.W #$10,D0
            alu(CMP,2,{I,0,0x10},{D,0},true,false); pc=0x18482;break;
        case 0x18482: // $018482: BEQ $018798
            if(branch(EQ,0x018798,true)) continue; pc=0x18486;break;
        case 0x18486: // $018486: CMPI.W #$14,D0
            alu(CMP,2,{I,0,0x14},{D,0},true,false); pc=0x1848a;break;
        case 0x1848a: // $01848A: BEQ $0184A0
            if(branch(EQ,0x0184a0,false)) continue; pc=0x1848c;break;
        case 0x1848c: // $01848C: RTS
            if(ret()) return returned;continue; pc=0x1848e;break;
        case 0x1848e: // $01848E: CLR.W ($9044).W
            unary(true,2,{W,0,0xffff9044}); pc=0x18492;break;
        case 0x18492: // $018492: CMPI.W #$7,D0
            alu(CMP,2,{I,0,0x7},{D,0},true,false); pc=0x18496;break;
        case 0x18496: // $018496: BGE $013A74
            if(branch(GE,0x013a74,true)) continue; pc=0x1849a;break;
        case 0x1849a: // $01849A: ADDQ.W #4,D0
            alu(ADD,2,{I,0,0x4},{D,0},false,true); pc=0x1849c;break;
        case 0x1849c: // $01849C: MOVE.W D0,($9040).W
            move(2,{D,0},{W,0,0xffff9040},false); pc=0x184a0;break;
        case 0x184a0: // $0184A0: MOVE.W ($9040).W,D0
            move(2,{W,0,0xffff9040},{D,0},false); pc=0x184a4;break;
        case 0x184a4: // $0184A4: BMI $01854A
            if(branch(MI,0x01854a,true)) continue; pc=0x184a8;break;
        case 0x184a8: // $0184A8: BSET #0,($9040).W
            bit(BSET,{I,0,0x0},{W,0,0xffff9040}); pc=0x184ae;break;
        case 0x184ae: // $0184AE: BNE $01853A
            if(branch(NE,0x01853a,true)) continue; pc=0x184b2;break;
        case 0x184b2: // $0184B2: CMPI.W #$10,D0
            alu(CMP,2,{I,0,0x10},{D,0},true,false); pc=0x184b6;break;
        case 0x184b6: // $0184B6: BGE $018544
            if(branch(GE,0x018544,true)) continue; pc=0x184ba;break;
        case 0x184ba: // $0184BA: CMPI.W #$B,D0
            alu(CMP,2,{I,0,0xb},{D,0},true,false); pc=0x184be;break;
        case 0x184be: // $0184BE: BEQ $018544
            if(branch(EQ,0x018544,true)) continue; pc=0x184c2;break;
        case 0x184c2: // $0184C2: CMPI.W #$C,D0
            alu(CMP,2,{I,0,0xc},{D,0},true,false); pc=0x184c6;break;
        case 0x184c6: // $0184C6: BEQ $018544
            if(branch(EQ,0x018544,true)) continue; pc=0x184ca;break;
        case 0x184ca: // $0184CA: CMP.B ($9035).W,D0
            alu(CMP,1,{W,0,0xffff9035},{D,0},false,false); pc=0x184ce;break;
        case 0x184ce: // $0184CE: BEQ $01853A
            if(branch(EQ,0x01853a,true)) continue; pc=0x184d2;break;
        case 0x184d2: // $0184D2: MOVE.B D0,($9035).W
            move(1,{D,0},{W,0,0xffff9035},false); pc=0x184d6;break;
        case 0x184d6: // $0184D6: LEA ($00220000).L,A0
            lea({L,0,0x220000},0); pc=0x184dc;break;
        case 0x184dc: // $0184DC: ASL.W #4,D0
            shift(ASL,2,{I,0,0x4},0); pc=0x184de;break;
        case 0x184de: // $0184DE: LEA 0(A0,D0.W),A0
            lea({X,0,0,0},0); pc=0x184e2;break;
        case 0x184e2: // $0184E2: LEA ($9000).W,A4
            lea({W,0,0xffff9000},4); pc=0x184e6;break;
        case 0x184e6: // $0184E6: MOVEA.L (A0)+,A1
            move(4,{P,0},{A,1},true); pc=0x184e8;break;
        case 0x184e8: // $0184E8: MOVE.L (A1)+,(A4)+
            move(4,{P,1},{P,4},false); pc=0x184ea;break;
        case 0x184ea: // $0184EA: MOVE.L (A1)+,(A4)+
            move(4,{P,1},{P,4},false); pc=0x184ec;break;
        case 0x184ec: // $0184EC: MOVE.L (A1)+,(A4)+
            move(4,{P,1},{P,4},false); pc=0x184ee;break;
        case 0x184ee: // $0184EE: MOVE.L (A1)+,(A4)+
            move(4,{P,1},{P,4},false); pc=0x184f0;break;
        case 0x184f0: // $0184F0: MOVE.L (A1)+,(A4)+
            move(4,{P,1},{P,4},false); pc=0x184f2;break;
        case 0x184f2: // $0184F2: MOVE.L (A1)+,(A4)+
            move(4,{P,1},{P,4},false); pc=0x184f4;break;
        case 0x184f4: // $0184F4: MOVE.L (A1)+,(A4)+
            move(4,{P,1},{P,4},false); pc=0x184f6;break;
        case 0x184f6: // $0184F6: MOVE.L (A1)+,(A4)+
            move(4,{P,1},{P,4},false); pc=0x184f8;break;
        case 0x184f8: // $0184F8: MOVE.L (A0)+,(A4)+
            move(4,{P,0},{P,4},false); pc=0x184fa;break;
        case 0x184fa: // $0184FA: MOVE.L (A0)+,(A4)+
            move(4,{P,0},{P,4},false); pc=0x184fc;break;
        case 0x184fc: // $0184FC: MOVEQ #0,D0
            moveq({I,0,0x0},0); pc=0x184fe;break;
        case 0x184fe: // $0184FE: MOVE.L D0,(A4)+
            move(4,{D,0},{P,4},false); pc=0x18500;break;
        case 0x18500: // $018500: MOVE.L D0,(A4)+
            move(4,{D,0},{P,4},false); pc=0x18502;break;
        case 0x18502: // $018502: MOVEA.L ($9024).W,A4
            move(4,{W,0,0xffff9024},{A,4},true); pc=0x18506;break;
        case 0x18506: // $018506: MOVE.W (A4)+,D0
            move(2,{P,4},{D,0},false); pc=0x18508;break;
        case 0x18508: // $018508: BMI $01853A
            if(branch(MI,0x01853a,false)) continue; pc=0x1850a;break;
        case 0x1850a: // $01850A: LEA ($000C0000).L,A1
            lea({L,0,0xc0000},1); pc=0x18510;break;
        case 0x18510: // $018510: ADDA.W (A4)+,A1
            adda(2,{P,4},1,false); pc=0x18512;break;
        case 0x18512: // $018512: MOVEQ #0,D7
            moveq({I,0,0x0},7); pc=0x18514;break;
        case 0x18514: // $018514: TST.B D0
            unary(false,1,{D,0}); pc=0x18516;break;
        case 0x18516: // $018516: BEQ $01852A
            if(branch(EQ,0x01852a,false)) continue; pc=0x18518;break;
        case 0x18518: // $018518: BPL $01851E
            if(branch(PL,0x01851e,false)) continue; pc=0x1851a;break;
        case 0x1851a: // $01851A: MOVE.L ($901C).W,D7
            move(4,{W,0,0xffff901c},{D,7},false); pc=0x1851e;break;
        case 0x1851e: // $01851E: LEA (A1),A0
            lea({M,1},0); pc=0x18520;break;
        case 0x18520: // $018520: MOVE.W #$3FF,D6
            move(2,{I,0,0x3ff},{D,6},false); pc=0x18524;break;
        case 0x18524: // $018524: MOVE.L D7,(A0)+
            move(4,{D,7},{P,0},false); pc=0x18526;break;
        case 0x18526: // $018526: DBF D6,$018524
            if(db(F,6,0x018524)) continue; pc=0x1852a;break;
        case 0x1852a: // $01852A: MOVEA.L (A4)+,A0
            move(4,{P,4},{A,0},true); pc=0x1852c;break;
        case 0x1852c: // $01852C: MOVEM.L A4,-(A7)
            movem(true); pc=0x18530;break;
        case 0x18530: // $018530: JSR $01866C(PC)
            call({C,0,0x01866c},0x18534,false);continue; pc=0x18534;break;
        case 0x18534: // $018534: MOVEM.L (A7)+,A4
            movem(false); pc=0x18538;break;
        case 0x18538: // $018538: BT $018506
            if(branch(T,0x018506,false)) continue; pc=0x1853a;break;
        case 0x1853a: // $01853A: JSR $01854C(PC)
            call({C,0,0x01854c},0x1853e,false);continue; pc=0x1853e;break;
        case 0x1853e: // $01853E: TST.B ($9000).W
            unary(false,1,{W,0,0xffff9000}); pc=0x18542;break;
        case 0x18542: // $018542: BNE $01854A
            if(branch(NE,0x01854a,false)) continue; pc=0x18544;break;
        case 0x18544: // $018544: MOVE.B #$81,($9040).W
            move(1,{I,0,0x81},{W,0,0xffff9040},false); pc=0x1854a;break;
        case 0x1854a: // $01854A: RTS
            if(ret()) return returned;continue; pc=0x1854c;break;
        case 0x1854c: // $01854C: MOVE.B ($9000).W,D0
            move(1,{W,0,0xffff9000},{D,0},false); pc=0x18550;break;
        case 0x18550: // $018550: ANDI.W #$1C,D0
            alu(AND,2,{I,0,0x1c},{D,0},true,false); pc=0x18554;break;
        case 0x18554: // $018554: JMP $018558(PC,D0.W)
            jump({J,0,0x018558,0});continue; pc=0x18558;break;
        case 0x18558: // $018558: JMP $018578(PC)
            jump({C,0,0x018578});continue; pc=0x1855c;break;
        case 0x1855c: // $01855C: JMP $01857A(PC)
            jump({C,0,0x01857a});continue; pc=0x18560;break;
        case 0x18560: // $018560: JMP $018588(PC)
            jump({C,0,0x018588});continue; pc=0x18564;break;
        case 0x18564: // $018564: JMP $0185E6(PC)
            jump({C,0,0x0185e6});continue; pc=0x18568;break;
        case 0x18568: // $018568: JMP $0185F0(PC)
            jump({C,0,0x0185f0});continue; pc=0x1856c;break;
        case 0x1856c: // $01856C: JMP $018578(PC)
            jump({C,0,0x018578});continue; pc=0x18570;break;
        case 0x18570: // $018570: JMP $018578(PC)
            jump({C,0,0x018578});continue; pc=0x18574;break;
        case 0x18574: // $018574: JMP $018578(PC)
            jump({C,0,0x018578});continue; pc=0x18578;break;
        case 0x18578: // $018578: RTS
            if(ret()) return returned;continue; pc=0x1857a;break;
        case 0x1857a: // $01857A: SUBQ.W #1,($9002).W
            alu(SUB,2,{I,0,0x1},{W,0,0xffff9002},false,true); pc=0x1857e;break;
        case 0x1857e: // $01857E: BCC $0185F0
            if(branch(CC,0x0185f0,true)) continue; pc=0x18582;break;
        case 0x18582: // $018582: ADDQ.B #4,($9000).W
            alu(ADD,1,{I,0,0x4},{W,0,0xffff9000},false,true); pc=0x18586;break;
        case 0x18586: // $018586: BT $01859A
            if(branch(T,0x01859a,false)) continue; pc=0x18588;break;
        case 0x18588: // $018588: SUBQ.W #1,($9030).W
            alu(SUB,2,{I,0,0x1},{W,0,0xffff9030},false,true); pc=0x1858c;break;
        case 0x1858c: // $01858C: BCC $0185F0
            if(branch(CC,0x0185f0,false)) continue; pc=0x1858e;break;
        case 0x1858e: // $01858E: SUBQ.B #1,($9034).W
            alu(SUB,1,{I,0,0x1},{W,0,0xffff9034},false,true); pc=0x18592;break;
        case 0x18592: // $018592: BCC $0185A8
            if(branch(CC,0x0185a8,false)) continue; pc=0x18594;break;
        case 0x18594: // $018594: SUBQ.B #1,($9001).W
            alu(SUB,1,{I,0,0x1},{W,0,0xffff9001},false,true); pc=0x18598;break;
        case 0x18598: // $018598: BCS $0185E0
            if(branch(CS,0x0185e0,false)) continue; pc=0x1859a;break;
        case 0x1859a: // $01859A: CLR.W ($9032).W
            unary(true,2,{W,0,0xffff9032}); pc=0x1859e;break;
        case 0x1859e: // $01859E: MOVE.B ($9020).W,D0
            move(1,{W,0,0xffff9020},{D,0},false); pc=0x185a2;break;
        case 0x185a2: // $0185A2: SUBQ.B #1,D0
            alu(SUB,1,{I,0,0x1},{D,0},false,true); pc=0x185a4;break;
        case 0x185a4: // $0185A4: MOVE.B D0,($9034).W
            move(1,{D,0},{W,0,0xffff9034},false); pc=0x185a8;break;
        case 0x185a8: // $0185A8: MOVE.W ($9006).W,($9030).W
            move(2,{W,0,0xffff9006},{W,0,0xffff9030},false); pc=0x185ae;break;
        case 0x185ae: // $0185AE: MOVEA.L ($9020).W,A0
            move(4,{W,0,0xffff9020},{A,0},true); pc=0x185b2;break;
        case 0x185b2: // $0185B2: MOVE.W ($9032).W,D0
            move(2,{W,0,0xffff9032},{D,0},false); pc=0x185b6;break;
        case 0x185b6: // $0185B6: ADDI.W #$10,($9032).W
            alu(ADD,2,{I,0,0x10},{W,0,0xffff9032},true,false); pc=0x185bc;break;
        case 0x185bc: // $0185BC: LEA ($00123800).L,A4
            lea({L,0,0x123800},4); pc=0x185c2;break;
        case 0x185c2: // $0185C2: MOVE.W (A0)+,D1
            move(2,{P,0},{D,1},false); pc=0x185c4;break;
        case 0x185c4: // $0185C4: BMI $0185F0
            if(branch(MI,0x0185f0,false)) continue; pc=0x185c6;break;
        case 0x185c6: // $0185C6: MOVE.W (A0)+,D2
            move(2,{P,0},{D,2},false); pc=0x185c8;break;
        case 0x185c8: // $0185C8: MOVEA.L (A0)+,A1
            move(4,{P,0},{A,1},true); pc=0x185ca;break;
        case 0x185ca: // $0185CA: LEA 0(A4,D2.W),A3
            lea({X,4,0,2},3); pc=0x185ce;break;
        case 0x185ce: // $0185CE: LEA 0(A1,D0.W),A2
            lea({X,1,0,0},2); pc=0x185d2;break;
        case 0x185d2: // $0185D2: MOVE.L (A2)+,(A3)+
            move(4,{P,2},{P,3},false); pc=0x185d4;break;
        case 0x185d4: // $0185D4: MOVE.L (A2)+,(A3)+
            move(4,{P,2},{P,3},false); pc=0x185d6;break;
        case 0x185d6: // $0185D6: MOVE.L (A2)+,(A3)+
            move(4,{P,2},{P,3},false); pc=0x185d8;break;
        case 0x185d8: // $0185D8: MOVE.L (A2)+,(A3)+
            move(4,{P,2},{P,3},false); pc=0x185da;break;
        case 0x185da: // $0185DA: DBF D1,$0185CE
            if(db(F,1,0x0185ce)) continue; pc=0x185de;break;
        case 0x185de: // $0185DE: BT $0185C2
            if(branch(T,0x0185c2,false)) continue; pc=0x185e0;break;
        case 0x185e0: // $0185E0: ADDQ.B #4,($9000).W
            alu(ADD,1,{I,0,0x4},{W,0,0xffff9000},false,true); pc=0x185e4;break;
        case 0x185e4: // $0185E4: BT $0185F0
            if(branch(T,0x0185f0,false)) continue; pc=0x185e6;break;
        case 0x185e6: // $0185E6: SUBQ.W #1,($9004).W
            alu(SUB,2,{I,0,0x1},{W,0,0xffff9004},false,true); pc=0x185ea;break;
        case 0x185ea: // $0185EA: BCC $0185F0
            if(branch(CC,0x0185f0,false)) continue; pc=0x185ec;break;
        case 0x185ec: // $0185EC: CLR.B ($9000).W
            unary(true,1,{W,0,0xffff9000}); pc=0x185f0;break;
        case 0x185f0: // $0185F0: MOVE.L ($9008).W,D0
            move(4,{W,0,0xffff9008},{D,0},false); pc=0x185f4;break;
        case 0x185f4: // $0185F4: MOVE.L D0,($000D0E80).L
            move(4,{D,0},{L,0,0xd0e80},false); pc=0x185fa;break;
        case 0x185fa: // $0185FA: MOVE.W ($9028).W,D0
            move(2,{W,0,0xffff9028},{D,0},false); pc=0x185fe;break;
        case 0x185fe: // $0185FE: ADD.W ($9010).W,D0
            alu(ADD,2,{W,0,0xffff9010},{D,0},false,false); pc=0x18602;break;
        case 0x18602: // $018602: MOVE.W D0,($9028).W
            move(2,{D,0},{W,0,0xffff9028},false); pc=0x18606;break;
        case 0x18606: // $018606: ASR.W #4,D0
            shift(ASR,2,{I,0,0x4},0); pc=0x18608;break;
        case 0x18608: // $018608: ADD.W ($900C).W,D0
            alu(ADD,2,{W,0,0xffff900c},{D,0},false,false); pc=0x1860c;break;
        case 0x1860c: // $01860C: ANDI.W #$3FF,D0
            alu(AND,2,{I,0,0x3ff},{D,0},true,false); pc=0x18610;break;
        case 0x18610: // $018610: MOVE.W D0,($000D0E98).L
            move(2,{D,0},{L,0,0xd0e98},false); pc=0x18616;break;
        case 0x18616: // $018616: MOVE.W ($902C).W,D0
            move(2,{W,0,0xffff902c},{D,0},false); pc=0x1861a;break;
        case 0x1861a: // $01861A: ADD.W ($9012).W,D0
            alu(ADD,2,{W,0,0xffff9012},{D,0},false,false); pc=0x1861e;break;
        case 0x1861e: // $01861E: MOVE.W D0,($902C).W
            move(2,{D,0},{W,0,0xffff902c},false); pc=0x18622;break;
        case 0x18622: // $018622: ASR.W #4,D0
            shift(ASR,2,{I,0,0x4},0); pc=0x18624;break;
        case 0x18624: // $018624: ADD.W ($900E).W,D0
            alu(ADD,2,{W,0,0xffff900e},{D,0},false,false); pc=0x18628;break;
        case 0x18628: // $018628: ANDI.W #$1FF,D0
            alu(AND,2,{I,0,0x1ff},{D,0},true,false); pc=0x1862c;break;
        case 0x1862c: // $01862C: MOVE.W D0,($000D0E90).L
            move(2,{D,0},{L,0,0xd0e90},false); pc=0x18632;break;
        case 0x18632: // $018632: MOVE.W ($902A).W,D0
            move(2,{W,0,0xffff902a},{D,0},false); pc=0x18636;break;
        case 0x18636: // $018636: ADD.W ($9018).W,D0
            alu(ADD,2,{W,0,0xffff9018},{D,0},false,false); pc=0x1863a;break;
        case 0x1863a: // $01863A: MOVE.W D0,($902A).W
            move(2,{D,0},{W,0,0xffff902a},false); pc=0x1863e;break;
        case 0x1863e: // $01863E: ASR.W #4,D0
            shift(ASR,2,{I,0,0x4},0); pc=0x18640;break;
        case 0x18640: // $018640: ADD.W ($9014).W,D0
            alu(ADD,2,{W,0,0xffff9014},{D,0},false,false); pc=0x18644;break;
        case 0x18644: // $018644: ANDI.W #$3FF,D0
            alu(AND,2,{I,0,0x3ff},{D,0},true,false); pc=0x18648;break;
        case 0x18648: // $018648: MOVE.W D0,($000D0E9A).L
            move(2,{D,0},{L,0,0xd0e9a},false); pc=0x1864e;break;
        case 0x1864e: // $01864E: MOVE.W ($902E).W,D0
            move(2,{W,0,0xffff902e},{D,0},false); pc=0x18652;break;
        case 0x18652: // $018652: ADD.W ($901A).W,D0
            alu(ADD,2,{W,0,0xffff901a},{D,0},false,false); pc=0x18656;break;
        case 0x18656: // $018656: MOVE.W D0,($902E).W
            move(2,{D,0},{W,0,0xffff902e},false); pc=0x1865a;break;
        case 0x1865a: // $01865A: ASR.W #4,D0
            shift(ASR,2,{I,0,0x4},0); pc=0x1865c;break;
        case 0x1865c: // $01865C: ADD.W ($9016).W,D0
            alu(ADD,2,{W,0,0xffff9016},{D,0},false,false); pc=0x18660;break;
        case 0x18660: // $018660: ANDI.W #$1FF,D0
            alu(AND,2,{I,0,0x1ff},{D,0},true,false); pc=0x18664;break;
        case 0x18664: // $018664: MOVE.W D0,($000D0E92).L
            move(2,{D,0},{L,0,0xd0e92},false); pc=0x1866a;break;
        case 0x1866a: // $01866A: RTS
            if(ret()) return returned;continue; pc=0x1866c;break;
        case 0x1866c: // $01866C: MOVE.W (A0)+,D7
            move(2,{P,0},{D,7},false); pc=0x1866e;break;
        case 0x1866e: // $01866E: CLR.W D0
            unary(true,2,{D,0}); pc=0x18670;break;
        case 0x18670: // $018670: LEA (A1),A4
            lea({M,1},4); pc=0x18672;break;
        case 0x18672: // $018672: MOVE.W 0(A0,D0.W),D1
            move(2,{X,0,0,0},{D,1},false); pc=0x18676;break;
        case 0x18676: // $018676: ADDA.W D1,A1
            adda(2,{D,1},1,false); pc=0x18678;break;
        case 0x18678: // $018678: MOVEA.L 2(A0,D0.W),A2
            move(4,{X,0,2,0},{A,2},true); pc=0x1867c;break;
        case 0x1867c: // $01867C: CLR.W D2
            unary(true,2,{D,2}); pc=0x1867e;break;
        case 0x1867e: // $01867E: CLR.W D3
            unary(true,2,{D,3}); pc=0x18680;break;
        case 0x18680: // $018680: MOVE.W (A2)+,D1
            move(2,{P,2},{D,1},false); pc=0x18682;break;
        case 0x18682: // $018682: CMPI.W #$FF00,D1
            alu(CMP,2,{I,0,0xff00},{D,1},true,false); pc=0x18686;break;
        case 0x18686: // $018686: BEQ $0186AA
            if(branch(EQ,0x0186aa,false)) continue; pc=0x18688;break;
        case 0x18688: // $018688: MOVE.B (A2)+,D2
            move(1,{P,2},{D,2},false); pc=0x1868a;break;
        case 0x1868a: // $01868A: MOVE.B (A2)+,D3
            move(1,{P,2},{D,3},false); pc=0x1868c;break;
        case 0x1868c: // $01868C: LEA (A1),A3
            lea({M,1},3); pc=0x1868e;break;
        case 0x1868e: // $01868E: MOVE.W D2,D4
            move(2,{D,2},{D,4},false); pc=0x18690;break;
        case 0x18690: // $018690: MOVE.B (A2)+,D1
            move(1,{P,2},{D,1},false); pc=0x18692;break;
        case 0x18692: // $018692: MOVE.W D1,(A1)+
            move(2,{D,1},{P,1},false); pc=0x18694;break;
        case 0x18694: // $018694: DBF D4,$018690
            if(db(F,4,0x018690)) continue; pc=0x18698;break;
        case 0x18698: // $018698: LEA 128(A3),A1
            lea({O,3,128},1); pc=0x1869c;break;
        case 0x1869c: // $01869C: DBF D3,$01868C
            if(db(F,3,0x01868c)) continue; pc=0x186a0;break;
        case 0x186a0: // $0186A0: ADDQ.W #8,D0
            alu(ADD,2,{I,0,0x8},{D,0},false,true); pc=0x186a2;break;
        case 0x186a2: // $0186A2: LEA (A4),A1
            lea({M,4},1); pc=0x186a4;break;
        case 0x186a4: // $0186A4: DBF D7,$018670
            if(db(F,7,0x018670)) continue; pc=0x186a8;break;
        case 0x186a8: // $0186A8: RTS
            if(ret()) return returned;continue; pc=0x186aa;break;
        case 0x186aa: // $0186AA: MOVE.B (A2)+,D2
            move(1,{P,2},{D,2},false); pc=0x186ac;break;
        case 0x186ac: // $0186AC: MOVE.B (A2)+,D3
            move(1,{P,2},{D,3},false); pc=0x186ae;break;
        case 0x186ae: // $0186AE: LEA (A1),A3
            lea({M,1},3); pc=0x186b0;break;
        case 0x186b0: // $0186B0: MOVE.W D2,D4
            move(2,{D,2},{D,4},false); pc=0x186b2;break;
        case 0x186b2: // $0186B2: MOVE.W (A2)+,(A1)+
            move(2,{P,2},{P,1},false); pc=0x186b4;break;
        case 0x186b4: // $0186B4: DBF D4,$0186B2
            if(db(F,4,0x0186b2)) continue; pc=0x186b8;break;
        case 0x186b8: // $0186B8: LEA 128(A3),A1
            lea({O,3,128},1); pc=0x186bc;break;
        case 0x186bc: // $0186BC: DBF D3,$0186AE
            if(db(F,3,0x0186ae)) continue; pc=0x186c0;break;
        case 0x186c0: // $0186C0: ADDQ.W #8,D0
            alu(ADD,2,{I,0,0x8},{D,0},false,true); pc=0x186c2;break;
        case 0x186c2: // $0186C2: LEA (A4),A1
            lea({M,4},1); pc=0x186c4;break;
        case 0x186c4: // $0186C4: DBF D7,$018670
            if(db(F,7,0x018670)) continue; pc=0x186c8;break;
        case 0x186c8: // $0186C8: RTS
            if(ret()) return returned;continue; pc=0x186ca;break;
        case 0x1871a: // $01871A: CLR.W ($902A).W
            unary(true,2,{W,0,0xffff902a}); pc=0x1871e;break;
        case 0x1871e: // $01871E: CLR.W ($902E).W
            unary(true,2,{W,0,0xffff902e}); pc=0x18722;break;
        case 0x18722: // $018722: MOVE.W #$1111,($000D0E82).L
            move(2,{I,0,0x1111},{L,0,0xd0e82},false); pc=0x1872a;break;
        case 0x1872a: // $01872A: CLR.W ($000D0E94).L
            unary(true,2,{L,0,0xd0e94}); pc=0x18730;break;
        case 0x18730: // $018730: CLR.W ($000D0E96).L
            unary(true,2,{L,0,0xd0e96}); pc=0x18736;break;
        case 0x18736: // $018736: MOVE.W #$2222,($000D0E84).L
            move(2,{I,0,0x2222},{L,0,0xd0e84},false); pc=0x1873e;break;
        case 0x1873e: // $01873E: MOVEA.L #$D01EE,A1
            move(4,{I,0,0xd01ee},{A,1},true); pc=0x18744;break;
        case 0x18744: // $018744: LEA ($0003122E).L,A0
            lea({L,0,0x3122e},0); pc=0x1874a;break;
        case 0x1874a: // $01874A: MOVE.W (A0)+,D0
            move(2,{P,0},{D,0},false); pc=0x1874c;break;
        case 0x1874c: // $01874C: MOVE.W (A0)+,D1
            move(2,{P,0},{D,1},false); pc=0x1874e;break;
        case 0x1874e: // $01874E: MOVE.W (A0)+,D2
            move(2,{P,0},{D,2},false); pc=0x18750;break;
        case 0x18750: // $018750: BSR $018902
            call({C,0,0x018902},0x18754,true);continue; pc=0x18754;break;
        case 0x18754: // $018754: LEA ($000D0F80).L,A0
            lea({L,0,0xd0f80},0); pc=0x1875a;break;
        case 0x1875a: // $01875A: MOVE.L #$80008000,D0
            move(4,{I,0,0x80008000},{D,0},false); pc=0x18760;break;
        case 0x18760: // $018760: MOVE.L D0,(A0)+
            move(4,{D,0},{P,0},false); pc=0x18762;break;
        case 0x18762: // $018762: MOVE.L D0,(A0)+
            move(4,{D,0},{P,0},false); pc=0x18764;break;
        case 0x18764: // $018764: MOVE.L D0,(A0)+
            move(4,{D,0},{P,0},false); pc=0x18766;break;
        case 0x18766: // $018766: MOVE.L D0,(A0)+
            move(4,{D,0},{P,0},false); pc=0x18768;break;
        case 0x18768: // $018768: MOVE.L D0,(A0)+
            move(4,{D,0},{P,0},false); pc=0x1876a;break;
        case 0x1876a: // $01876A: MOVE.W ($0029C09C).L,D0
            move(2,{L,0,0x29c09c},{D,0},false); pc=0x18770;break;
        case 0x18770: // $018770: CMPI.W #$F,D0
            alu(CMP,2,{I,0,0xf},{D,0},true,false); pc=0x18774;break;
        case 0x18774: // $018774: BEQ $018780
            if(branch(EQ,0x018780,true)) continue; pc=0x18778;break;
        case 0x18778: // $018778: CMPI.W #$10,D0
            alu(CMP,2,{I,0,0x10},{D,0},true,false); pc=0x1877c;break;
        case 0x1877c: // $01877C: BNE $018796
            if(branch(NE,0x018796,true)) continue; pc=0x18780;break;
        case 0x18780: // $018780: LEA ($000D0CB2).L,A1
            lea({L,0,0xd0cb2},1); pc=0x18786;break;
        case 0x18786: // $018786: LEA ($000188D6).L,A0
            lea({L,0,0x188d6},0); pc=0x1878c;break;
        case 0x1878c: // $01878C: MOVE.W (A0)+,D0
            move(2,{P,0},{D,0},false); pc=0x1878e;break;
        case 0x1878e: // $01878E: MOVE.W (A0)+,D1
            move(2,{P,0},{D,1},false); pc=0x18790;break;
        case 0x18790: // $018790: MOVE.W (A0)+,D2
            move(2,{P,0},{D,2},false); pc=0x18792;break;
        case 0x18792: // $018792: JSR $018902(PC)
            call({C,0,0x018902},0x18796,false);continue; pc=0x18796;break;
        case 0x18796: // $018796: RTS
            if(ret()) return returned;continue; pc=0x18798;break;
        case 0x18798: // $018798: BTST #6,($0029C200).L
            bit(BTST,{I,0,0x6},{L,0,0x29c200}); pc=0x187a0;break;
        case 0x187a0: // $0187A0: BEQ $01881E
            if(branch(EQ,0x01881e,true)) continue; pc=0x187a4;break;
        case 0x187a4: // $0187A4: BTST #6,($9048).W
            bit(BTST,{I,0,0x6},{W,0,0xffff9048}); pc=0x187aa;break;
        case 0x187aa: // $0187AA: BNE $0187B0
            if(branch(NE,0x0187b0,false)) continue; pc=0x187ac;break;
        case 0x187ac: // $0187AC: BSR $01871A
            call({C,0,0x01871a},0x187b0,true);continue; pc=0x187b0;break;
        case 0x187b0: // $0187B0: LEA ($0029C630).L,A0
            lea({L,0,0x29c630},0); pc=0x187b6;break;
        case 0x187b6: // $0187B6: MOVE.W (A0),D1
            move(2,{M,0},{D,1},false); pc=0x187b8;break;
        case 0x187b8: // $0187B8: ANDI.W #$E,D1
            alu(AND,2,{I,0,0xe},{D,1},true,false); pc=0x187bc;break;
        case 0x187bc: // $0187BC: LEA ($0029C620).L,A1
            lea({L,0,0x29c620},1); pc=0x187c2;break;
        case 0x187c2: // $0187C2: MOVE.W 0(A1,D1.W),D1
            move(2,{X,1,0,1},{D,1},false); pc=0x187c6;break;
        case 0x187c6: // $0187C6: ANDI.W #$FC,D1
            alu(AND,2,{I,0,0xfc},{D,1},true,false); pc=0x187ca;break;
        case 0x187ca: // $0187CA: LEA ($00031252).L,A0
            lea({L,0,0x31252},0); pc=0x187d0;break;
        case 0x187d0: // $0187D0: MOVEA.L 0(A0,D1.W),A0
            move(4,{X,0,0,1},{A,0},true); pc=0x187d4;break;
        case 0x187d4: // $0187D4: MOVE.W (A0)+,D0
            move(2,{P,0},{D,0},false); pc=0x187d6;break;
        case 0x187d6: // $0187D6: MOVE.W (A0)+,D1
            move(2,{P,0},{D,1},false); pc=0x187d8;break;
        case 0x187d8: // $0187D8: MOVE.W (A0)+,D2
            move(2,{P,0},{D,2},false); pc=0x187da;break;
        case 0x187da: // $0187DA: MOVEA.L #$C216C,A1
            move(4,{I,0,0xc216c},{A,1},true); pc=0x187e0;break;
        case 0x187e0: // $0187E0: BSR $018902
            call({C,0,0x018902},0x187e4,true);continue; pc=0x187e4;break;
        case 0x187e4: // $0187E4: MOVE.W ($0029C08E).L,D0
            move(2,{L,0,0x29c08e},{D,0},false); pc=0x187ea;break;
        case 0x187ea: // $0187EA: MOVE.W D0,D1
            move(2,{D,0},{D,1},false); pc=0x187ec;break;
        case 0x187ec: // $0187EC: ADD.W D1,D0
            alu(ADD,2,{D,1},{D,0},false,false); pc=0x187ee;break;
        case 0x187ee: // $0187EE: ADD.W D1,D0
            alu(ADD,2,{D,1},{D,0},false,false); pc=0x187f0;break;
        case 0x187f0: // $0187F0: ASR.W #5,D0
            shift(ASR,2,{I,0,0x5},0); pc=0x187f2;break;
        case 0x187f2: // $0187F2: ADDI.W #$FFFC,D0
            alu(ADD,2,{I,0,0xfffc},{D,0},true,false); pc=0x187f6;break;
        case 0x187f6: // $0187F6: ANDI.W #$1FF,D0
            alu(AND,2,{I,0,0x1ff},{D,0},true,false); pc=0x187fa;break;
        case 0x187fa: // $0187FA: MOVE.W D0,($000D0E9C).L
            move(2,{D,0},{L,0,0xd0e9c},false); pc=0x18800;break;
        case 0x18800: // $018800: MOVE.W ($0029C08C).L,D0
            move(2,{L,0,0x29c08c},{D,0},false); pc=0x18806;break;
        case 0x18806: // $018806: MOVE.W D0,D1
            move(2,{D,0},{D,1},false); pc=0x18808;break;
        case 0x18808: // $018808: ASL.W #3,D0
            shift(ASL,2,{I,0,0x3},0); pc=0x1880a;break;
        case 0x1880a: // $01880A: SUB.W D1,D0
            alu(SUB,2,{D,1},{D,0},false,false); pc=0x1880c;break;
        case 0x1880c: // $01880C: ASR.W #6,D0
            shift(ASR,2,{I,0,0x6},0); pc=0x1880e;break;
        case 0x1880e: // $01880E: ADDQ.W #4,D0
            alu(ADD,2,{I,0,0x4},{D,0},false,true); pc=0x18810;break;
        case 0x18810: // $018810: ANDI.W #$FF,D0
            alu(AND,2,{I,0,0xff},{D,0},true,false); pc=0x18814;break;
        case 0x18814: // $018814: MOVE.W D0,($000D0E94).L
            move(2,{D,0},{L,0,0xd0e94},false); pc=0x1881a;break;
        case 0x1881a: // $01881A: BT $018852
            if(branch(T,0x018852,true)) continue; pc=0x1881e;break;
        case 0x1881e: // $01881E: BTST #6,($9048).W
            bit(BTST,{I,0,0x6},{W,0,0xffff9048}); pc=0x18824;break;
        case 0x18824: // $018824: BEQ $018852
            if(branch(EQ,0x018852,false)) continue; pc=0x18826;break;
        case 0x18826: // $018826: MOVEA.L #$D01EE,A1
            move(4,{I,0,0xd01ee},{A,1},true); pc=0x1882c;break;
        case 0x1882c: // $01882C: LEA ($00031240).L,A0
            lea({L,0,0x31240},0); pc=0x18832;break;
        case 0x18832: // $018832: MOVE.W (A0)+,D0
            move(2,{P,0},{D,0},false); pc=0x18834;break;
        case 0x18834: // $018834: MOVE.W (A0)+,D1
            move(2,{P,0},{D,1},false); pc=0x18836;break;
        case 0x18836: // $018836: MOVE.W (A0)+,D2
            move(2,{P,0},{D,2},false); pc=0x18838;break;
        case 0x18838: // $018838: BSR $018902
            call({C,0,0x018902},0x1883c,true);continue; pc=0x1883c;break;
        case 0x1883c: // $01883C: MOVEA.L #$C216C,A1
            move(4,{I,0,0xc216c},{A,1},true); pc=0x18842;break;
        case 0x18842: // $018842: LEA ($00031352).L,A0
            lea({L,0,0x31352},0); pc=0x18848;break;
        case 0x18848: // $018848: MOVE.W (A0)+,D0
            move(2,{P,0},{D,0},false); pc=0x1884a;break;
        case 0x1884a: // $01884A: MOVE.W (A0)+,D1
            move(2,{P,0},{D,1},false); pc=0x1884c;break;
        case 0x1884c: // $01884C: MOVE.W (A0)+,D2
            move(2,{P,0},{D,2},false); pc=0x1884e;break;
        case 0x1884e: // $01884E: BSR $018902
            call({C,0,0x018902},0x18852,true);continue; pc=0x18852;break;
        case 0x18852: // $018852: MOVE.W ($0029C200).L,($9048).W
            move(2,{L,0,0x29c200},{W,0,0xffff9048},false); pc=0x1885a;break;
        case 0x1885a: // $01885A: MOVE.W ($0029C09C).L,D0
            move(2,{L,0,0x29c09c},{D,0},false); pc=0x18860;break;
        case 0x18860: // $018860: CMPI.W #$F,D0
            alu(CMP,2,{I,0,0xf},{D,0},true,false); pc=0x18864;break;
        case 0x18864: // $018864: BEQ $018870
            if(branch(EQ,0x018870,true)) continue; pc=0x18868;break;
        case 0x18868: // $018868: CMPI.W #$10,D0
            alu(CMP,2,{I,0,0x10},{D,0},true,false); pc=0x1886c;break;
        case 0x1886c: // $01886C: BNE $0188A6
            if(branch(NE,0x0188a6,true)) continue; pc=0x18870;break;
        case 0x18870: // $018870: LEA ($000C1CBE).L,A0
            lea({L,0,0xc1cbe},0); pc=0x18876;break;
        case 0x18876: // $018876: LEA $0188C8(PC),A4
            lea({C,0,0x0188c8},4); pc=0x1887a;break;
        case 0x1887a: // $01887A: MOVE.W (A4)+,D0
            move(2,{P,4},{D,0},false); pc=0x1887c;break;
        case 0x1887c: // $01887C: MOVE.W (A4)+,D7
            move(2,{P,4},{D,7},false); pc=0x1887e;break;
        case 0x1887e: // $01887E: MOVE.W ($9046).W,D1
            move(2,{W,0,0xffff9046},{D,1},false); pc=0x18882;break;
        case 0x18882: // $018882: MOVEQ #7,D2
            moveq({I,0,0x7},2); pc=0x18884;break;
        case 0x18884: // $018884: AND.W D1,D2
            alu(AND,2,{D,1},{D,2},false,false); pc=0x18886;break;
        case 0x18886: // $018886: LSR.W #3,D1
            shift(LSR,2,{I,0,0x3},1); pc=0x18888;break;
        case 0x18888: // $018888: BEQ $018894
            if(branch(EQ,0x018894,false)) continue; pc=0x1888a;break;
        case 0x1888a: // $01888A: MOVE.B (A4)+,D0
            move(1,{P,4},{D,0},false); pc=0x1888c;break;
        case 0x1888c: // $01888C: MOVE.W D0,(A0)+
            move(2,{D,0},{P,0},false); pc=0x1888e;break;
        case 0x1888e: // $01888E: SUBQ.W #1,D1
            alu(SUB,2,{I,0,0x1},{D,1},false,true); pc=0x18890;break;
        case 0x18890: // $018890: DBEQ D7,$018888
            if(db(EQ,7,0x018888)) continue; pc=0x18894;break;
        case 0x18894: // $018894: TST.W D2
            unary(false,2,{D,2}); pc=0x18896;break;
        case 0x18896: // $018896: BEQ $0188A0
            if(branch(EQ,0x0188a0,false)) continue; pc=0x18898;break;
        case 0x18898: // $018898: MOVE.B (A4)+,D0
            move(1,{P,4},{D,0},false); pc=0x1889a;break;
        case 0x1889a: // $01889A: SUBQ.W #8,D0
            alu(SUB,2,{I,0,0x8},{D,0},false,true); pc=0x1889c;break;
        case 0x1889c: // $01889C: ADD.W D2,D0
            alu(ADD,2,{D,2},{D,0},false,false); pc=0x1889e;break;
        case 0x1889e: // $01889E: MOVE.W D0,(A0)+
            move(2,{D,0},{P,0},false); pc=0x188a0;break;
        case 0x188a0: // $0188A0: CLR.W (A0)+
            unary(true,2,{P,0}); pc=0x188a2;break;
        case 0x188a2: // $0188A2: DBF D7,$0188A0
            if(db(F,7,0x0188a0)) continue; pc=0x188a6;break;
        case 0x188a6: // $0188A6: RTS
            if(ret()) return returned;continue; pc=0x188a8;break;
        case 0x18902: // $018902: LEA (A1),A2
            lea({M,1},2); pc=0x18904;break;
        case 0x18904: // $018904: MOVE.W D0,D4
            move(2,{D,0},{D,4},false); pc=0x18906;break;
        case 0x18906: // $018906: MOVE.B (A0)+,D2
            move(1,{P,0},{D,2},false); pc=0x18908;break;
        case 0x18908: // $018908: MOVE.W D2,(A1)+
            move(2,{D,2},{P,1},false); pc=0x1890a;break;
        case 0x1890a: // $01890A: DBF D4,$018906
            if(db(F,4,0x018906)) continue; pc=0x1890e;break;
        case 0x1890e: // $01890E: LEA 128(A2),A1
            lea({O,2,128},1); pc=0x18912;break;
        case 0x18912: // $018912: DBF D1,$018902
            if(db(F,1,0x018902)) continue; pc=0x18916;break;
        case 0x18916: // $018916: RTS
            if(ret()) return returned;continue; pc=0x18918;break;
