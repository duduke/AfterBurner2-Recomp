// Literal IRQ4 game-flow instructions; all table data stays in runtime ROM.
        case 0xb840: // $00B840: TST.B ($8902).W
            unary(false,1,{W,0,0xffff8902}); pc=0xb844;break;
        case 0xb844: // $00B844: BEQ $00B87C
            if(branch(EQ,0xb87c,true)) continue; pc=0xb848;break;
        case 0xb848: // $00B848: BT $00B860
            if(branch(T,0xb860,true)) continue; pc=0xb84c;break;
        case 0xc6f8: // $00C6F8: LEA ($8052).W,A3
            lea({W,0,0xffff8052},3); pc=0xc6fc;break;
        case 0xc6fc: // $00C6FC: MOVE.B ($0029C074).L,D0
            move(1,{L,0,0x29c074},{D,0},false); pc=0xc702;break;
        case 0xc702: // $00C702: MOVE.B ($0029C075).L,D2
            move(1,{L,0,0x29c075},{D,2},false); pc=0xc708;break;
        case 0xc708: // $00C708: MOVEQ #2,D1
            moveq({I,0,2},1); pc=0xc70a;break;
        case 0xc70a: // $00C70A: BTST D1,D0
            bit(BTST,{D,1},{D,0}); pc=0xc70c;break;
        case 0xc70c: // $00C70C: BEQ $00C720
            if(branch(EQ,0xc720,true)) continue; pc=0xc710;break;
        case 0xc710: // $00C710: BTST D1,D2
            bit(BTST,{D,1},{D,2}); pc=0xc712;break;
        case 0xc712: // $00C712: BNE $00C720
            if(branch(NE,0xc720,true)) continue; pc=0xc716;break;
        case 0xc716: // $00C716: MOVEQ #1,D1
            moveq({I,0,1},1); pc=0xc718;break;
        case 0xc718: // $00C718: BSR $00C8B0
            call({L,0,0x00c8b0},0xc71c,true);continue;
        case 0xc71c: // $00C71C: ADD.W D1,($810C).W
            alu(ADD,2,{D,1},{W,0,0xffff810c},false,false); pc=0xc720;break;
        case 0xc720: // $00C720: LEA ($8106).W,A4
            lea({W,0,0xffff8106},4); pc=0xc724;break;
        case 0xc724: // $00C724: MOVEQ #6,D1
            moveq({I,0,6},1); pc=0xc726;break;
        case 0xc726: // $00C726: LEA 2(A3),A0
            lea({O,3,2},0); pc=0xc72a;break;
        case 0xc72a: // $00C72A: LEA (A0),A1
            lea({M,0},1); pc=0xc72c;break;
        case 0xc72c: // $00C72C: BSR $00C762
            call({L,0,0x00c762},0xc730,true);continue;
        case 0xc730: // $00C730: MOVEQ #16,D1
            moveq({I,0,16},1); pc=0xc732;break;
        case 0xc732: // $00C732: BSR $00C934
            call({L,0,0x00c934},0xc736,true);continue;
        case 0xc736: // $00C736: LEA ($8108).W,A4
            lea({W,0,0xffff8108},4); pc=0xc73a;break;
        case 0xc73a: // $00C73A: MOVEQ #7,D1
            moveq({I,0,7},1); pc=0xc73c;break;
        case 0xc73c: // $00C73C: MOVE.B 7(A3),D3
            move(1,{O,3,7},{D,3},false); pc=0xc740;break;
        case 0xc740: // $00C740: CMP.B 2(A3),D3
            alu(CMP,1,{O,3,2},{D,3},false,false); pc=0xc744;break;
        case 0xc744: // $00C744: BNE $00C750
            if(branch(NE,0xc750,true)) continue; pc=0xc748;break;
        case 0xc748: // $00C748: LEA 2(A3),A0
            lea({O,3,2},0); pc=0xc74c;break;
        case 0xc74c: // $00C74C: BT $00C762
            if(branch(T,0xc762,true)) continue; pc=0xc750;break;
        case 0xc750: // $00C750: LEA 7(A3),A0
            lea({O,3,7},0); pc=0xc754;break;
        case 0xc754: // $00C754: LEA (A0),A1
            lea({M,0},1); pc=0xc756;break;
        case 0xc756: // $00C756: BSR $00C762
            call({L,0,0x00c762},0xc75a,true);continue;
        case 0xc75a: // $00C75A: MOVEQ #8,D1
            moveq({I,0,8},1); pc=0xc75c;break;
        case 0xc75c: // $00C75C: BSR $00C934
            call({L,0,0x00c934},0xc760,true);continue;
        case 0xc760: // $00C760: RTS
            if(ret()) return returned;continue;
        case 0xc762: // $00C762: BTST D1,D0
            bit(BTST,{D,1},{D,0}); pc=0xc764;break;
        case 0xc764: // $00C764: BNE $00C778
            if(branch(NE,0xc778,true)) continue; pc=0xc768;break;
        case 0xc768: // $00C768: BTST D1,D2
            bit(BTST,{D,1},{D,2}); pc=0xc76a;break;
        case 0xc76a: // $00C76A: BEQ $00C778
            if(branch(EQ,0xc778,true)) continue; pc=0xc76e;break;
        case 0xc76e: // $00C76E: ADDQ.W #1,(A4)
            alu(ADD,2,{I,0,1},{M,4},false,true); pc=0xc770;break;
        case 0xc770: // $00C770: ADDQ.B #1,4(A0)
            alu(ADD,1,{I,0,1},{O,0,4},false,true); pc=0xc774;break;
        case 0xc774: // $00C774: BT $00C77A
            if(branch(T,0xc77a,true)) continue; pc=0xc778;break;
        case 0xc778: // $00C778: RTS
            if(ret()) return returned;continue;
        case 0xc77a: // $00C77A: MOVEM.L A1/D0,-(A7)
            movem_mask(true,0x201); pc=0xc77e;break;
        case 0xc77e: // $00C77E: MOVE.L A0,-(A7)
            move(4,{A,0},{N,7},false); pc=0xc780;break;
        case 0xc780: // $00C780: CMPI.W #$E,($0029C09C).L
            alu(CMP,2,{I,0,14},{L,0,0x29c09c},true,false); pc=0xc788;break;
        case 0xc788: // $00C788: BGE $00C79C
            if(branch(GE,0xc79c,true)) continue; pc=0xc78c;break;
        case 0xc78c: // $00C78C: TST.B ($8052).W
            unary(false,1,{W,0,0xffff8052}); pc=0xc790;break;
        case 0xc790: // $00C790: BNE $00C79C
            if(branch(NE,0xc79c,true)) continue; pc=0xc794;break;
        case 0xc794: // $00C794: MOVE.B #$FF,D0
            move(1,{I,0,255},{D,0},false); pc=0xc798;break;
        case 0xc798: // $00C798: BSR $00B84C
            call({L,0,0x00b84c},0xc79c,true);continue;
        case 0xc79c: // $00C79C: MOVE.B #$7E,D0
            move(1,{I,0,126},{D,0},false); pc=0xc7a0;break;
        case 0xc7a0: // $00C7A0: BSR $00B840
            call({L,0,0x00b840},0xc7a4,true);continue;
        case 0xc7a4: // $00C7A4: MOVEA.L (A7)+,A0
            move(4,{P,7},{A,0},true); pc=0xc7a6;break;
        case 0xc7a6: // $00C7A6: MOVEQ #0,D0
            moveq({I,0,0},0); pc=0xc7a8;break;
        case 0xc7a8: // $00C7A8: MOVEQ #0,D1
            moveq({I,0,0},1); pc=0xc7aa;break;
        case 0xc7aa: // $00C7AA: MOVE.B (A0)+,D0
            move(1,{P,0},{D,0},false); pc=0xc7ac;break;
        case 0xc7ac: // $00C7AC: LSL.W #1,D0
            shift(LSL,2,{I,0,1},0); pc=0xc7ae;break;
        case 0xc7ae: // $00C7AE: CMPI.B #$14,D0
            alu(CMP,1,{I,0,20},{D,0},true,false); pc=0xc7b2;break;
        case 0xc7b2: // $00C7B2: BCC $00C848
            if(branch(CC,0xc848,true)) continue; pc=0xc7b6;break;
        case 0xc7b6: // $00C7B6: LEA ($0000C7C0).L,A1
            lea({L,0,0xc7c0},1); pc=0xc7bc;break;
        case 0xc7bc: // $00C7BC: JMP 0(A1,D0.W)
            jump({X,1,0,0});continue;
        case 0xc7c0: // $00C7C0: BT $00C7D8
            if(branch(T,0xc7d8,false)) continue; pc=0xc7c2;break;
        case 0xc7c2: // $00C7C2: BT $00C7D6
            if(branch(T,0xc7d6,false)) continue; pc=0xc7c4;break;
        case 0xc7c4: // $00C7C4: BT $00C7D4
            if(branch(T,0xc7d4,false)) continue; pc=0xc7c6;break;
        case 0xc7c6: // $00C7C6: BT $00C806
            if(branch(T,0xc806,false)) continue; pc=0xc7c8;break;
        case 0xc7c8: // $00C7C8: BT $00C812
            if(branch(T,0xc812,false)) continue; pc=0xc7ca;break;
        case 0xc7ca: // $00C7CA: BT $00C81E
            if(branch(T,0xc81e,false)) continue; pc=0xc7cc;break;
        case 0xc7cc: // $00C7CC: BT $00C7E2
            if(branch(T,0xc7e2,false)) continue; pc=0xc7ce;break;
        case 0xc7ce: // $00C7CE: BT $00C7EE
            if(branch(T,0xc7ee,false)) continue; pc=0xc7d0;break;
        case 0xc7d0: // $00C7D0: BT $00C7FA
            if(branch(T,0xc7fa,false)) continue; pc=0xc7d2;break;
        case 0xc7d2: // $00C7D2: BT $00C82A
            if(branch(T,0xc82a,false)) continue; pc=0xc7d4;break;
        case 0xc7d4: // $00C7D4: ADDQ.W #1,D1
            alu(ADD,2,{I,0,1},{D,1},false,true); pc=0xc7d6;break;
        case 0xc7d6: // $00C7D6: ADDQ.W #1,D1
            alu(ADD,2,{I,0,1},{D,1},false,true); pc=0xc7d8;break;
        case 0xc7d8: // $00C7D8: ADDQ.W #1,D1
            alu(ADD,2,{I,0,1},{D,1},false,true); pc=0xc7da;break;
        case 0xc7da: // $00C7DA: BSR $00C8B0
            call({L,0,0x00c8b0},0xc7de,true);continue;
        case 0xc7de: // $00C7DE: BT $00C882
            if(branch(T,0xc882,true)) continue; pc=0xc7e2;break;
        case 0xc7e2: // $00C7E2: CMPI.B #$1,(A0)
            alu(CMP,1,{I,0,1},{M,0},true,false); pc=0xc7e6;break;
        case 0xc7e6: // $00C7E6: BCC $00C838
            if(branch(CC,0xc838,true)) continue; pc=0xc7ea;break;
        case 0xc7ea: // $00C7EA: BT $00C832
            if(branch(T,0xc832,true)) continue; pc=0xc7ee;break;
        case 0xc7ee: // $00C7EE: CMPI.B #$2,(A0)
            alu(CMP,1,{I,0,2},{M,0},true,false); pc=0xc7f2;break;
        case 0xc7f2: // $00C7F2: BCC $00C838
            if(branch(CC,0xc838,true)) continue; pc=0xc7f6;break;
        case 0xc7f6: // $00C7F6: BT $00C832
            if(branch(T,0xc832,true)) continue; pc=0xc7fa;break;
        case 0xc7fa: // $00C7FA: CMPI.B #$3,(A0)
            alu(CMP,1,{I,0,3},{M,0},true,false); pc=0xc7fe;break;
        case 0xc7fe: // $00C7FE: BCC $00C838
            if(branch(CC,0xc838,true)) continue; pc=0xc802;break;
        case 0xc802: // $00C802: BT $00C832
            if(branch(T,0xc832,true)) continue; pc=0xc806;break;
        case 0xc806: // $00C806: CMPI.B #$4,(A0)
            alu(CMP,1,{I,0,4},{M,0},true,false); pc=0xc80a;break;
        case 0xc80a: // $00C80A: BCC $00C838
            if(branch(CC,0xc838,true)) continue; pc=0xc80e;break;
        case 0xc80e: // $00C80E: BT $00C832
            if(branch(T,0xc832,true)) continue; pc=0xc812;break;
        case 0xc812: // $00C812: CMPI.B #$5,(A0)
            alu(CMP,1,{I,0,5},{M,0},true,false); pc=0xc816;break;
        case 0xc816: // $00C816: BCC $00C838
            if(branch(CC,0xc838,true)) continue; pc=0xc81a;break;
        case 0xc81a: // $00C81A: BT $00C832
            if(branch(T,0xc832,true)) continue; pc=0xc81e;break;
        case 0xc81e: // $00C81E: CMPI.B #$6,(A0)
            alu(CMP,1,{I,0,6},{M,0},true,false); pc=0xc822;break;
        case 0xc822: // $00C822: BCC $00C838
            if(branch(CC,0xc838,true)) continue; pc=0xc826;break;
        case 0xc826: // $00C826: BT $00C832
            if(branch(T,0xc832,true)) continue; pc=0xc82a;break;
        case 0xc82a: // $00C82A: CMPI.B #$1,(A0)
            alu(CMP,1,{I,0,1},{M,0},true,false); pc=0xc82e;break;
        case 0xc82e: // $00C82E: BCC $00C844
            if(branch(CC,0xc844,true)) continue; pc=0xc832;break;
        case 0xc832: // $00C832: ADDQ.B #1,(A0)+
            alu(ADD,1,{I,0,1},{P,0},false,true); pc=0xc834;break;
        case 0xc834: // $00C834: BT $00C882
            if(branch(T,0xc882,true)) continue; pc=0xc838;break;
        case 0xc838: // $00C838: MOVEQ #1,D1
            moveq({I,0,1},1); pc=0xc83a;break;
        case 0xc83a: // $00C83A: CLR.B (A0)+
            unary(true,1,{P,0}); pc=0xc83c;break;
        case 0xc83c: // $00C83C: BSR $00C8B0
            call({L,0,0x00c8b0},0xc840,true);continue;
        case 0xc840: // $00C840: BT $00C882
            if(branch(T,0xc882,true)) continue; pc=0xc844;break;
        case 0xc844: // $00C844: MOVEQ #3,D1
            moveq({I,0,3},1); pc=0xc846;break;
        case 0xc846: // $00C846: BT $00C83A
            if(branch(T,0xc83a,false)) continue; pc=0xc848;break;
        case 0xc848: // $00C848: SUBI.W #$14,D0
            alu(SUB,2,{I,0,20},{D,0},true,false); pc=0xc84c;break;
        case 0xc84c: // $00C84C: LEA ($0000C888).L,A1
            lea({L,0,0xc888},1); pc=0xc852;break;
        case 0xc852: // $00C852: MOVE.W 0(A1,D0.W),D0
            move(2,{X,1,0,0},{D,0},false); pc=0xc856;break;
        case 0xc856: // $00C856: MOVE.B (A0),D1
            move(1,{M,0},{D,1},false); pc=0xc858;break;
        case 0xc858: // $00C858: LEA ($0000C892).L,A1
            lea({L,0,0xc892},1); pc=0xc85e;break;
        case 0xc85e: // $00C85E: CMP.B 0(A1,D0.W),D1
            alu(CMP,1,{X,1,0,0},{D,1},false,false); pc=0xc862;break;
        case 0xc862: // $00C862: BCS $00C868
            if(branch(CS,0xc868,true)) continue; pc=0xc866;break;
        case 0xc866: // $00C866: CLR.B (A0)
            unary(true,1,{M,0}); pc=0xc868;break;
        case 0xc868: // $00C868: MOVE.B (A0),D1
            move(1,{M,0},{D,1},false); pc=0xc86a;break;
        case 0xc86a: // $00C86A: ADDQ.B #1,D1
            alu(ADD,1,{I,0,1},{D,1},false,true); pc=0xc86c;break;
        case 0xc86c: // $00C86C: MOVE.B D1,(A0)
            move(1,{D,1},{M,0},false); pc=0xc86e;break;
        case 0xc86e: // $00C86E: ADD.W D1,D0
            alu(ADD,2,{D,1},{D,0},false,false); pc=0xc870;break;
        case 0xc870: // $00C870: LEA ($0000C892).L,A1
            lea({L,0,0xc892},1); pc=0xc876;break;
        case 0xc876: // $00C876: MOVE.B 0(A1,D0.W),D1
            move(1,{X,1,0,0},{D,1},false); pc=0xc87a;break;
        case 0xc87a: // $00C87A: BEQ $00C882
            if(branch(EQ,0xc882,true)) continue; pc=0xc87e;break;
        case 0xc87e: // $00C87E: BSR $00C8B0
            call({L,0,0x00c8b0},0xc882,true);continue;
        case 0xc882: // $00C882: MOVEM.L (A7)+,D0/A1
            movem_mask(false,0x201); pc=0xc886;break;
        case 0xc886: // $00C886: RTS
            if(ret()) return returned;continue;
        case 0xc8b0: // $00C8B0: ANDI.W #$FF,D1
            alu(AND,2,{I,0,255},{D,1},true,false); pc=0xc8b4;break;
        case 0xc8b4: // $00C8B4: MOVE.B (A3),D3
            move(1,{M,3},{D,3},false); pc=0xc8b6;break;
        case 0xc8b6: // $00C8B6: BTST #4,($8070).W
            bit(BTST,{I,0,4},{W,0,0xffff8070}); pc=0xc8bc;break;
        case 0xc8bc: // $00C8BC: BEQ $00C900
            if(branch(EQ,0xc900,false)) continue; pc=0xc8be;break;
        case 0xc8be: // $00C8BE: CMPI.B #$99,D3
            alu(CMP,1,{I,0,153},{D,3},true,false); pc=0xc8c2;break;
        case 0xc8c2: // $00C8C2: BEQ $00C930
            if(branch(EQ,0xc930,true)) continue; pc=0xc8c6;break;
        case 0xc8c6: // $00C8C6: ANDI #$EF,CCR
            x=false;idle(20); pc=0xc8ca;break;
        case 0xc8ca: // $00C8CA: ABCD D3,D1
            bcd(false,3,1); pc=0xc8cc;break;
        case 0xc8cc: // $00C8CC: BCC $00C8D2
            if(branch(CC,0xc8d2,false)) continue; pc=0xc8ce;break;
        case 0xc8ce: // $00C8CE: MOVE.W #$99,D1
            move(2,{I,0,153},{D,1},false); pc=0xc8d2;break;
        case 0xc8d2: // $00C8D2: MOVEM.L A1/A0/D2/D1/D0,-(A7)
            movem_mask(true,0x307); pc=0xc8d6;break;
        case 0xc8d6: // $00C8D6: SBCD D3,D1
            bcd(true,3,1); pc=0xc8d8;break;
        case 0xc8d8: // $00C8D8: MOVE.B ($8070).W,D0
            move(1,{W,0,0xffff8070},{D,0},false); pc=0xc8dc;break;
        case 0xc8dc: // $00C8DC: BTST #3,D0
            bit(BTST,{I,0,3},{D,0}); pc=0xc8e0;break;
        case 0xc8e0: // $00C8E0: BEQ $00C8EC
            if(branch(EQ,0xc8ec,true)) continue; pc=0xc8e4;break;
        case 0xc8e4: // $00C8E4: MULU.W #$4,D1
            mulu({I,0,4},1); pc=0xc8e8;break;
        case 0xc8e8: // $00C8E8: BT $00C8F0
            if(branch(T,0xc8f0,true)) continue; pc=0xc8ec;break;
        case 0xc8ec: // $00C8EC: MULU.W #$3,D1
            mulu({I,0,3},1); pc=0xc8f0;break;
        case 0xc8f0: // $00C8F0: ADD.W D1,($88E0).W
            alu(ADD,2,{D,1},{W,0,0xffff88e0},false,false); pc=0xc8f4;break;
        case 0xc8f4: // $00C8F4: BSR $00CF46
            call({L,0,0x00cf46},0xc8f8,true);continue;
        case 0xc8f8: // $00C8F8: MOVEM.L (A7)+,D0/D1/D2/A0/A1
            movem_mask(false,0x307); pc=0xc8fc;break;
        case 0xc8fc: // $00C8FC: BT $00C914
            if(branch(T,0xc914,true)) continue; pc=0xc900;break;
        case 0xc900: // $00C900: CMPI.B #$9,D3
            alu(CMP,1,{I,0,9},{D,3},true,false); pc=0xc904;break;
        case 0xc904: // $00C904: BEQ $00C930
            if(branch(EQ,0xc930,true)) continue; pc=0xc908;break;
        case 0xc908: // $00C908: ABCD D3,D1
            bcd(false,3,1); pc=0xc90a;break;
        case 0xc90a: // $00C90A: CMPI.B #$A,D1
            alu(CMP,1,{I,0,10},{D,1},true,false); pc=0xc90e;break;
        case 0xc90e: // $00C90E: BCS $00C914
            if(branch(CS,0xc914,false)) continue; pc=0xc910;break;
        case 0xc910: // $00C910: MOVE.B #$9,D1
            move(1,{I,0,9},{D,1},false); pc=0xc914;break;
        case 0xc914: // $00C914: MOVE.B D1,(A3)
            move(1,{D,1},{M,3},false); pc=0xc916;break;
        case 0xc916: // $00C916: ANDI #$EF,CCR
            x=false;idle(20); pc=0xc91a;break;
        case 0xc91a: // $00C91A: SBCD D3,D1
            bcd(true,3,1); pc=0xc91c;break;
        case 0xc91c: // $00C91C: ADD.W D1,($810A).W
            alu(ADD,2,{D,1},{W,0,0xffff810a},false,false); pc=0xc920;break;
        case 0xc920: // $00C920: MOVEM.L A1/A0/D2/D1/D0,-(A7)
            movem_mask(true,0x307); pc=0xc924;break;
        case 0xc924: // $00C924: BSR $00C980
            call({L,0,0x00c980},0xc928,true);continue;
        case 0xc928: // $00C928: MOVEM.L (A7)+,D0/D1/D2/A0/A1
            movem_mask(false,0x307); pc=0xc92c;break;
        case 0xc92c: // $00C92C: BT $00C932
            if(branch(T,0xc932,true)) continue; pc=0xc930;break;
        case 0xc930: // $00C930: CLR.W D1
            unary(true,2,{D,1}); pc=0xc932;break;
        case 0xc932: // $00C932: RTS
            if(ret()) return returned;continue;
        case 0xc934: // $00C934: TST.B 2(A1)
            unary(false,1,{O,1,2}); pc=0xc938;break;
        case 0xc938: // $00C938: BNE $00C960
            if(branch(NE,0xc960,true)) continue; pc=0xc93c;break;
        case 0xc93c: // $00C93C: TST.B 3(A1)
            unary(false,1,{O,1,3}); pc=0xc940;break;
        case 0xc940: // $00C940: BNE $00C97A
            if(branch(NE,0xc97a,true)) continue; pc=0xc944;break;
        case 0xc944: // $00C944: TST.B 4(A1)
            unary(false,1,{O,1,4}); pc=0xc948;break;
        case 0xc948: // $00C948: BEQ $00C97E
            if(branch(EQ,0xc97e,true)) continue; pc=0xc94c;break;
        case 0xc94c: // $00C94C: SUBQ.B #1,4(A1)
            alu(SUB,1,{I,0,1},{O,1,4},false,true); pc=0xc950;break;
        case 0xc950: // $00C950: MOVE.B #$6,2(A1)
            move(1,{I,0,6},{O,1,2},false); pc=0xc956;break;
        case 0xc956: // $00C956: OR.B ($8B6D).W,D1
            alu(OR,1,{W,0,0xffff8b6d},{D,1},false,false); pc=0xc95a;break;
        case 0xc95a: // $00C95A: MOVE.B D1,($8B6D).W
            move(1,{D,1},{W,0,0xffff8b6d},false); pc=0xc95e;break;
        case 0xc95e: // $00C95E: RTS
            if(ret()) return returned;continue;
        case 0xc960: // $00C960: SUBQ.B #1,2(A1)
            alu(SUB,1,{I,0,1},{O,1,2},false,true); pc=0xc964;break;
        case 0xc964: // $00C964: BNE $00C97E
            if(branch(NE,0xc97e,true)) continue; pc=0xc968;break;
        case 0xc968: // $00C968: MOVE.B #$6,3(A1)
            move(1,{I,0,6},{O,1,3},false); pc=0xc96e;break;
        case 0xc96e: // $00C96E: NOT.W D1
            negate(true,2,{D,1}); pc=0xc970;break;
        case 0xc970: // $00C970: AND.B ($8B6D).W,D1
            alu(AND,1,{W,0,0xffff8b6d},{D,1},false,false); pc=0xc974;break;
        case 0xc974: // $00C974: MOVE.B D1,($8B6D).W
            move(1,{D,1},{W,0,0xffff8b6d},false); pc=0xc978;break;
        case 0xc978: // $00C978: RTS
            if(ret()) return returned;continue;
        case 0xc97a: // $00C97A: SUBQ.B #1,3(A1)
            alu(SUB,1,{I,0,1},{O,1,3},false,true); pc=0xc97e;break;
        case 0xc97e: // $00C97E: RTS
            if(ret()) return returned;continue;
        case 0xc980: // $00C980: MOVEA.W #$8100,A0
            move(2,{I,0,33024},{A,0},true); pc=0xc984;break;
        case 0xc984: // $00C984: MOVE.W #$37,D1
            move(2,{I,0,55},{D,1},false); pc=0xc988;break;
        case 0xc988: // $00C988: MOVEQ #0,D0
            moveq({I,0,0},0); pc=0xc98a;break;
        case 0xc98a: // $00C98A: MOVE.W (A0)+,D2
            move(2,{P,0},{D,2},false); pc=0xc98c;break;
        case 0xc98c: // $00C98C: EOR.W D2,D0
            alu(EOR,2,{D,2},{D,0},false,false); pc=0xc98e;break;
        case 0xc98e: // $00C98E: LSR.W #1,D0
            shift(LSR,2,{I,0,1},0); pc=0xc990;break;
        case 0xc990: // $00C990: BCC $00C996
            if(branch(CC,0xc996,false)) continue; pc=0xc992;break;
        case 0xc992: // $00C992: EORI.W #$8810,D0
            alu(EOR,2,{I,0,34832},{D,0},true,false); pc=0xc996;break;
        case 0xc996: // $00C996: DBF D1,$00C98A
            if(db(F,1,0xc98a)) continue; pc=0xc99a;break;
        case 0xc99a: // $00C99A: MOVE.W D0,14(A0)
            move(2,{D,0},{O,0,14},false); pc=0xc99e;break;
        case 0xc99e: // $00C99E: LEA ($8100).W,A0
            lea({W,0,0xffff8100},0); pc=0xc9a2;break;
        case 0xc9a2: // $00C9A2: LEA ($8180).W,A1
            lea({W,0,0xffff8180},1); pc=0xc9a6;break;
        case 0xc9a6: // $00C9A6: MOVE.W #$1F,D0
            move(2,{I,0,31},{D,0},false); pc=0xc9aa;break;
        case 0xc9aa: // $00C9AA: MOVE.L (A0)+,(A1)+
            move(4,{P,0},{P,1},false); pc=0xc9ac;break;
        case 0xc9ac: // $00C9AC: DBF D0,$00C9AA
            if(db(F,0,0xc9aa)) continue; pc=0xc9b0;break;
        case 0xc9b0: // $00C9B0: RTS
            if(ret()) return returned;continue;
        case 0xcf46: // $00CF46: MOVE.W ($0029C09C).L,D0
            move(2,{L,0,0x29c09c},{D,0},false); pc=0xcf4c;break;
        case 0xcf4c: // $00CF4C: CMPI.W #$F,D0
            alu(CMP,2,{I,0,15},{D,0},true,false); pc=0xcf50;break;
        case 0xcf50: // $00CF50: BLT $00CFC2
            if(branch(LT,0xcfc2,true)) continue; pc=0xcf54;break;
        case 0xcf54: // $00CF54: CMPI.W #$12,D0
            alu(CMP,2,{I,0,18},{D,0},true,false); pc=0xcf58;break;
        case 0xcf58: // $00CF58: BGT $00CFC2
            if(branch(GT,0xcfc2,true)) continue; pc=0xcf5c;break;
        case 0xcf5c: // $00CF5C: MOVE.W ($88E0).W,D0
            move(2,{W,0,0xffff88e0},{D,0},false); pc=0xcf60;break;
        case 0xcf60: // $00CF60: CMPI.W #$1E,D0
            alu(CMP,2,{I,0,30},{D,0},true,false); pc=0xcf64;break;
        case 0xcf64: // $00CF64: BLE $00CF6C
            if(branch(LE,0xcf6c,true)) continue; pc=0xcf68;break;
        case 0xcf68: // $00CF68: MOVE.W #$1E,D0
            move(2,{I,0,30},{D,0},false); pc=0xcf6c;break;
        case 0xcf6c: // $00CF6C: LEA ($000D0D34).L,A0
            lea({L,0,0xd0d34},0); pc=0xcf72;break;
        case 0xcf72: // $00CF72: LEA (A0),A1
            lea({M,0},1); pc=0xcf74;break;
        case 0xcf74: // $00CF74: MOVE.W #$20,D1
            move(2,{I,0,32},{D,1},false); pc=0xcf78;break;
        case 0xcf78: // $00CF78: MOVEQ #12,D2
            moveq({I,0,12},2); pc=0xcf7a;break;
        case 0xcf7a: // $00CF7A: MOVE.W D1,(A1)+
            move(2,{D,1},{P,1},false); pc=0xcf7c;break;
        case 0xcf7c: // $00CF7C: DBF D2,$00CF7A
            if(db(F,2,0xcf7a)) continue; pc=0xcf80;break;
        case 0xcf80: // $00CF80: CMPI.W #$1,D0
            alu(CMP,2,{I,0,1},{D,0},true,false); pc=0xcf84;break;
        case 0xcf84: // $00CF84: BLE $00CFC2
            if(branch(LE,0xcfc2,true)) continue; pc=0xcf88;break;
        case 0xcf88: // $00CF88: CMPI.W #$8,($9124).W
            alu(CMP,2,{I,0,8},{W,0,0xffff9124},true,false); pc=0xcf8e;break;
        case 0xcf8e: // $00CF8E: BGE $00CF94
            if(branch(GE,0xcf94,true)) continue; pc=0xcf92;break;
        case 0xcf92: // $00CF92: SUBQ.W #1,D0
            alu(SUB,2,{I,0,1},{D,0},false,true); pc=0xcf94;break;
        case 0xcf94: // $00CF94: BSR $00D062
            call({L,0,0x00d062},0xcf98,true);continue;
        case 0xcf98: // $00CF98: MOVE.W D0,D1
            move(2,{D,0},{D,1},false); pc=0xcf9a;break;
        case 0xcf9a: // $00CF9A: ANDI.W #$F0,D1
            alu(AND,2,{I,0,240},{D,1},true,false); pc=0xcf9e;break;
        case 0xcf9e: // $00CF9E: ROR.W #4,D1
            shift(ROR,2,{I,0,4},1); pc=0xcfa0;break;
        case 0xcfa0: // $00CFA0: SUBQ.W #1,D1
            alu(SUB,2,{I,0,1},{D,1},false,true); pc=0xcfa2;break;
        case 0xcfa2: // $00CFA2: BMI $00CFB0
            if(branch(MI,0xcfb0,true)) continue; pc=0xcfa6;break;
        case 0xcfa6: // $00CFA6: MOVE.L #$8BF98BFA,(A0)+
            move(4,{I,0,2348387322},{P,0},false); pc=0xcfac;break;
        case 0xcfac: // $00CFAC: DBF D1,$00CFA6
            if(db(F,1,0xcfa6)) continue; pc=0xcfb0;break;
        case 0xcfb0: // $00CFB0: ANDI.W #$F,D0
            alu(AND,2,{I,0,15},{D,0},true,false); pc=0xcfb4;break;
        case 0xcfb4: // $00CFB4: SUBQ.W #1,D0
            alu(SUB,2,{I,0,1},{D,0},false,true); pc=0xcfb6;break;
        case 0xcfb6: // $00CFB6: BMI $00CFC2
            if(branch(MI,0xcfc2,true)) continue; pc=0xcfba;break;
        case 0xcfba: // $00CFBA: MOVE.W #$8BF8,(A0)+
            move(2,{I,0,35832},{P,0},false); pc=0xcfbe;break;
        case 0xcfbe: // $00CFBE: DBF D0,$00CFBA
            if(db(F,0,0xcfba)) continue; pc=0xcfc2;break;
        case 0xcfc2: // $00CFC2: RTS
            if(ret()) return returned;continue;
        case 0x137e2: // $0137E2: CLR.W D1
            unary(true,2,{D,1}); pc=0x137e4;break;
        case 0x137e4: // $0137E4: CLR.W D2
            unary(true,2,{D,2}); pc=0x137e6;break;
        case 0x137e6: // $0137E6: MOVE.W (A0)+,D0
            move(2,{P,0},{D,0},false); pc=0x137e8;break;
        case 0x137e8: // $0137E8: LEA ($000D0000).L,A1
            lea({L,0,0xd0000},1); pc=0x137ee;break;
        case 0x137ee: // $0137EE: ADDA.W D0,A1
            adda(2,{D,0},1,false); pc=0x137f0;break;
        case 0x137f0: // $0137F0: MOVE.B (A0)+,D2
            move(1,{P,0},{D,2},false); pc=0x137f2;break;
        case 0x137f2: // $0137F2: ROR.W #7,D2
            shift(ROR,2,{I,0,7},2); pc=0x137f4;break;
        case 0x137f4: // $0137F4: ORI.W #$8100,D2
            alu(OR,2,{I,0,33024},{D,2},true,false); pc=0x137f8;break;
        case 0x137f8: // $0137F8: MOVE.B (A0)+,D1
            move(1,{P,0},{D,1},false); pc=0x137fa;break;
        case 0x137fa: // $0137FA: CLR.W D0
            unary(true,2,{D,0}); pc=0x137fc;break;
        case 0x137fc: // $0137FC: MOVE.B (A0)+,D0
            move(1,{P,0},{D,0},false); pc=0x137fe;break;
        case 0x137fe: // $0137FE: CMPI.B #$20,D0
            alu(CMP,1,{I,0,32},{D,0},true,false); pc=0x13802;break;
        case 0x13802: // $013802: BNE $01380A
            if(branch(NE,0x1380a,false)) continue; pc=0x13804;break;
        case 0x13804: // $013804: CLR.W D0
            unary(true,2,{D,0}); pc=0x13806;break;
        case 0x13806: // $013806: MOVE.W D0,(A1)+
            move(2,{D,0},{P,1},false); pc=0x13808;break;
        case 0x13808: // $013808: BT $013812
            if(branch(T,0x13812,false)) continue; pc=0x1380a;break;
        case 0x1380a: // $01380A: ADD.W D0,D0
            alu(ADD,2,{D,0},{D,0},false,false); pc=0x1380c;break;
        case 0x1380c: // $01380C: ADD.W D2,D0
            alu(ADD,2,{D,2},{D,0},false,false); pc=0x1380e;break;
        case 0x1380e: // $01380E: MOVE.W D0,(A1)+
            move(2,{D,0},{P,1},false); pc=0x13810;break;
        case 0x13810: // $013810: ADDQ.W #1,D0
            alu(ADD,2,{I,0,1},{D,0},false,true); pc=0x13812;break;
        case 0x13812: // $013812: MOVE.W D0,126(A1)
            move(2,{D,0},{O,1,126},false); pc=0x13816;break;
        case 0x13816: // $013816: DBF D1,$0137FA
            if(db(F,1,0x137fa)) continue; pc=0x1381a;break;
        case 0x1381a: // $01381A: RTS
            if(ret()) return returned;continue;
        case 0x13a2c: // $013A2C: LEA ($000D0000).L,A0
            lea({L,0,0xd0000},0); pc=0x13a32;break;
        case 0x13a32: // $013A32: MOVE.W #$6F,D1
            move(2,{I,0,111},{D,1},false); pc=0x13a36;break;
        case 0x13a36: // $013A36: MOVE.L #$200020,D0
            move(4,{I,0,2097184},{D,0},false); pc=0x13a3c;break;
        case 0x13a3c: // $013A3C: MOVE.L D0,(A0)+
            move(4,{D,0},{P,0},false); pc=0x13a3e;break;
        case 0x13a3e: // $013A3E: MOVE.L D0,(A0)+
            move(4,{D,0},{P,0},false); pc=0x13a40;break;
        case 0x13a40: // $013A40: MOVE.L D0,(A0)+
            move(4,{D,0},{P,0},false); pc=0x13a42;break;
        case 0x13a42: // $013A42: MOVE.L D0,(A0)+
            move(4,{D,0},{P,0},false); pc=0x13a44;break;
        case 0x13a44: // $013A44: MOVE.L D0,(A0)+
            move(4,{D,0},{P,0},false); pc=0x13a46;break;
        case 0x13a46: // $013A46: MOVE.L D0,(A0)+
            move(4,{D,0},{P,0},false); pc=0x13a48;break;
        case 0x13a48: // $013A48: MOVE.L D0,(A0)+
            move(4,{D,0},{P,0},false); pc=0x13a4a;break;
        case 0x13a4a: // $013A4A: MOVE.L D0,(A0)+
            move(4,{D,0},{P,0},false); pc=0x13a4c;break;
        case 0x13a4c: // $013A4C: DBF D1,$013A3C
            if(db(F,1,0x13a3c)) continue; pc=0x13a50;break;
        case 0x13a50: // $013A50: RTS
            if(ret()) return returned;continue;
        case 0x17146: // $017146: CMPI.W #$3,($0029C09C).L
            alu(CMP,2,{I,0,3},{L,0,0x29c09c},true,false); pc=0x1714e;break;
        case 0x1714e: // $01714E: BEQ $018206
            if(branch(EQ,0x18206,true)) continue; pc=0x17152;break;
        case 0x17152: // $017152: CMPI.W #$1A,($0029C09C).L
            alu(CMP,2,{I,0,26},{L,0,0x29c09c},true,false); pc=0x1715a;break;
        case 0x1715a: // $01715A: BEQ $01718A
            if(branch(EQ,0x1718a,true)) continue; pc=0x1715e;break;
        case 0x1715e: // $01715E: RTS
            if(ret()) return returned;continue;
        case 0x1718a: // $01718A: MOVE.L #$F000,D6
            move(4,{I,0,61440},{D,6},false); pc=0x17190;break;
        case 0x17190: // $017190: LEA ($8400).W,A5
            lea({W,0,0xffff8400},5); pc=0x17194;break;
        case 0x17194: // $017194: TST.B ($8052).W
            unary(false,1,{W,0,0xffff8052}); pc=0x17198;break;
        case 0x17198: // $017198: BEQ $0171CC
            if(branch(EQ,0x171cc,true)) continue; pc=0x1719c;break;
        case 0x1719c: // $01719C: TST.B 20(A5)
            unary(false,1,{O,5,20}); pc=0x171a0;break;
        case 0x171a0: // $0171A0: BNE $0171B6
            if(branch(NE,0x171b6,true)) continue; pc=0x171a4;break;
        case 0x171a4: // $0171A4: BSR $017608
            call({L,0,0x017608},0x171a8,true);continue;
        case 0x171a8: // $0171A8: CMPI.B #$14,22(A5)
            alu(CMP,1,{I,0,20},{O,5,22},true,false); pc=0x171ae;break;
        case 0x171ae: // $0171AE: BGE $0171B6
            if(branch(GE,0x171b6,true)) continue; pc=0x171b2;break;
        case 0x171b2: // $0171B2: BSR $01762A
            call({L,0,0x01762a},0x171b6,true);continue;
        case 0x171b6: // $0171B6: MOVE.W #$0,($0029C09C).L
            move(2,{I,0,0},{L,0,0x29c09c},false); pc=0x171be;break;
        case 0x171be: // $0171BE: CLR.W ($8864).W
            unary(true,2,{W,0,0xffff8864}); pc=0x171c2;break;
        case 0x171c2: // $0171C2: CLR.W ($8860).W
            unary(true,2,{W,0,0xffff8860}); pc=0x171c6;break;
        case 0x171c6: // $0171C6: CLR.W ($880A).W
            unary(true,2,{W,0,0xffff880a}); pc=0x171ca;break;
        case 0x171ca: // $0171CA: RTS
            if(ret()) return returned;continue;
        case 0x171cc: // $0171CC: MOVE.B 20(A5),D0
            move(1,{O,5,20},{D,0},false); pc=0x171d0;break;
        case 0x171d0: // $0171D0: ANDI.W #$7,D0
            alu(AND,2,{I,0,7},{D,0},true,false); pc=0x171d4;break;
        case 0x171d4: // $0171D4: ADD.W D0,D0
            alu(ADD,2,{D,0},{D,0},false,false); pc=0x171d6;break;
        case 0x171d6: // $0171D6: ADD.W D0,D0
            alu(ADD,2,{D,0},{D,0},false,false); pc=0x171d8;break;
        case 0x171d8: // $0171D8: JMP $0171DC(PC,D0.W)
            jump({J,0,0x171dc,0});continue;
        case 0x171dc: // $0171DC: BT $0171FC
            if(branch(T,0x171fc,true)) continue; pc=0x171e0;break;
        case 0x171e0: // $0171E0: BT $01723C
            if(branch(T,0x1723c,true)) continue; pc=0x171e4;break;
        case 0x171e4: // $0171E4: BT $017252
            if(branch(T,0x17252,true)) continue; pc=0x171e8;break;
        case 0x171e8: // $0171E8: BT $0172C4
            if(branch(T,0x172c4,true)) continue; pc=0x171ec;break;
        case 0x171ec: // $0171EC: BT $017320
            if(branch(T,0x17320,true)) continue; pc=0x171f0;break;
        case 0x171f0: // $0171F0: BT $017480
            if(branch(T,0x17480,true)) continue; pc=0x171f4;break;
        case 0x171f4: // $0171F4: BT $0174B0
            if(branch(T,0x174b0,true)) continue; pc=0x171f8;break;
        case 0x171f8: // $0171F8: BT $017606
            if(branch(T,0x17606,true)) continue; pc=0x171fc;break;
        case 0x171fc: // $0171FC: BSR $017608
            call({L,0,0x017608},0x17200,true);continue;
        case 0x17200: // $017200: CMPI.B #$14,22(A5)
            alu(CMP,1,{I,0,20},{O,5,22},true,false); pc=0x17206;break;
        case 0x17206: // $017206: BLT $01721A
            if(branch(LT,0x1721a,true)) continue; pc=0x1720a;break;
        case 0x1720a: // $01720A: MOVE.B #$FF,21(A5)
            move(1,{I,0,255},{O,5,21},false); pc=0x17210;break;
        case 0x17210: // $017210: MOVE.W #$4,($8860).W
            move(2,{I,0,4},{W,0,0xffff8860},false); pc=0x17216;break;
        case 0x17216: // $017216: BT $017230
            if(branch(T,0x17230,true)) continue; pc=0x1721a;break;
        case 0x1721a: // $01721A: MOVEM.L D7/D0,-(A7)
            movem_mask(true,0x81); pc=0x1721e;break;
        case 0x1721e: // $01721E: MOVE.W #$96,D0
            move(2,{I,0,150},{D,0},false); pc=0x17222;break;
        case 0x17222: // $017222: JSR ($0000B84C).L
            call({L,0,0xb84c},0x17228,false);continue;
        case 0x17228: // $017228: MOVEM.L (A7)+,D0/D7
            movem_mask(false,0x81); pc=0x1722c;break;
        case 0x1722c: // $01722C: BSR $01762A
            call({L,0,0x01762a},0x17230,true);continue;
        case 0x17230: // $017230: BSR $0176DC
            call({L,0,0x0176dc},0x17234,true);continue;
        case 0x17234: // $017234: MOVE.B #$1,20(A5)
            move(1,{I,0,1},{O,5,20},false); pc=0x1723a;break;
        case 0x1723a: // $01723A: RTS
            if(ret()) return returned;continue;
        case 0x1723c: // $01723C: BSR $018076
            call({L,0,0x018076},0x17240,true);continue;
        case 0x17240: // $017240: CMPI.B #$8,12(A5)
            alu(CMP,1,{I,0,8},{O,5,12},true,false); pc=0x17246;break;
        case 0x17246: // $017246: BLT $017250
            if(branch(LT,0x17250,true)) continue; pc=0x1724a;break;
        case 0x1724a: // $01724A: MOVE.B #$2,20(A5)
            move(1,{I,0,2},{O,5,20},false); pc=0x17250;break;
        case 0x17250: // $017250: RTS
            if(ret()) return returned;continue;
        case 0x17252: // $017252: TST.B 21(A5)
            unary(false,1,{O,5,21}); pc=0x17256;break;
        case 0x17256: // $017256: BNE $0172BE
            if(branch(NE,0x172be,true)) continue; pc=0x1725a;break;
        case 0x1725a: // $01725A: CLR.W D0
            unary(true,2,{D,0}); pc=0x1725c;break;
        case 0x1725c: // $01725C: MOVE.B 22(A5),D0
            move(1,{O,5,22},{D,0},false); pc=0x17260;break;
        case 0x17260: // $017260: MULU.W #$E,D0
            mulu({I,0,14},0); pc=0x17264;break;
        case 0x17264: // $017264: LEA 110(A5,D0.W),A0
            lea({X,5,110,0},0); pc=0x17268;break;
        case 0x17268: // $017268: LEA 4(A0),A0
            lea({O,0,4},0); pc=0x1726c;break;
        case 0x1726c: // $01726C: BSR $017822
            call({L,0,0x017822},0x17270,true);continue;
        case 0x17270: // $017270: MOVE.L A3,46(A5)
            move(4,{A,3},{O,5,46},false); pc=0x17274;break;
        case 0x17274: // $017274: MOVE.L A1,42(A5)
            move(4,{A,1},{O,5,42},false); pc=0x17278;break;
        case 0x17278: // $017278: ADDA.L #$E,A3
            adda(4,{I,0,14},3,false); pc=0x1727e;break;
        case 0x1727e: // $01727E: MOVE.L A3,52(A5)
            move(4,{A,3},{O,5,52},false); pc=0x17282;break;
        case 0x17282: // $017282: MOVE.B #$27,40(A5)
            move(1,{I,0,39},{O,5,40},false); pc=0x17288;break;
        case 0x17288: // $017288: MOVEA.L 46(A5),A3
            move(4,{O,5,46},{A,3},true); pc=0x1728c;break;
        case 0x1728c: // $01728C: MOVEQ #29,D0
            moveq({I,0,29},0); pc=0x1728e;break;
        case 0x1728e: // $01728E: LEA (A3),A2
            lea({M,3},2); pc=0x17290;break;
        case 0x17290: // $017290: SUBA.L #$F000,A2
            adda(4,{I,0,61440},2,true); pc=0x17296;break;
        case 0x17296: // $017296: MOVE.L (A3)+,D1
            move(4,{P,3},{D,1},false); pc=0x17298;break;
        case 0x17298: // $017298: ANDI.L #$1FF01FF,D1
            alu(AND,4,{I,0,33489407},{D,1},true,false); pc=0x1729e;break;
        case 0x1729e: // $01729E: ORI.L #$80008000,D1
            alu(OR,4,{I,0,2147516416},{D,1},true,false); pc=0x172a4;break;
        case 0x172a4: // $0172A4: MOVE.L D1,(A2)+
            move(4,{D,1},{P,2},false); pc=0x172a6;break;
        case 0x172a6: // $0172A6: DBF D0,$017296
            if(db(F,0,0x17296)) continue; pc=0x172aa;break;
        case 0x172aa: // $0172AA: MOVE.B #$3,20(A5)
            move(1,{I,0,3},{O,5,20},false); pc=0x172b0;break;
        case 0x172b0: // $0172B0: MOVE.W #$4D,D0
            move(2,{I,0,77},{D,0},false); pc=0x172b4;break;
        case 0x172b4: // $0172B4: JSR ($0000B84C).L
            call({L,0,0xb84c},0x172ba,false);continue;
        case 0x172ba: // $0172BA: BT $0172C2
            if(branch(T,0x172c2,true)) continue; pc=0x172be;break;
        case 0x172be: // $0172BE: BSR $017C00
            call({L,0,0x017c00},0x172c2,true);continue;
        case 0x172c2: // $0172C2: RTS
            if(ret()) return returned;continue;
        case 0x172c4: // $0172C4: MOVE.B 40(A5),D0
            move(1,{O,5,40},{D,0},false); pc=0x172c8;break;
        case 0x172c8: // $0172C8: BEQ $0172DE
            if(branch(EQ,0x172de,true)) continue; pc=0x172cc;break;
        case 0x172cc: // $0172CC: ANDI.W #$FF,D0
            alu(AND,2,{I,0,255},{D,0},true,false); pc=0x172d0;break;
        case 0x172d0: // $0172D0: ADD.W D0,D0
            alu(ADD,2,{D,0},{D,0},false,false); pc=0x172d2;break;
        case 0x172d2: // $0172D2: MOVEA.L 46(A5),A3
            move(4,{O,5,46},{A,3},true); pc=0x172d6;break;
        case 0x172d6: // $0172D6: LEA 0(A3,D0.W),A3
            lea({X,3,0,0},3); pc=0x172da;break;
        case 0x172da: // $0172DA: BT $017880
            if(branch(T,0x17880,true)) continue; pc=0x172de;break;
        case 0x172de: // $0172DE: MOVE.B #$4,20(A5)
            move(1,{I,0,4},{O,5,20},false); pc=0x172e4;break;
        case 0x172e4: // $0172E4: MOVE.B #$1E,32(A5)
            move(1,{I,0,30},{O,5,32},false); pc=0x172ea;break;
        case 0x172ea: // $0172EA: MOVE.B #$0,33(A5)
            move(1,{I,0,0},{O,5,33},false); pc=0x172f0;break;
        case 0x172f0: // $0172F0: LEA ($0003109A).L,A0
            lea({L,0,0x3109a},0); pc=0x172f6;break;
        case 0x172f6: // $0172F6: BSR $0137E2
            call({L,0,0x0137e2},0x172fa,true);continue;
        case 0x172fa: // $0172FA: LEA ($000D0BF0).L,A4
            lea({L,0,0xd0bf0},4); pc=0x17300;break;
        case 0x17300: // $017300: MOVE.W #$8B5C,(A4)+
            move(2,{I,0,35676},{P,4},false); pc=0x17304;break;
        case 0x17304: // $017304: MOVE.W #$8B5D,126(A4)
            move(2,{I,0,35677},{O,4,126},false); pc=0x1730a;break;
        case 0x1730a: // $01730A: MOVE.W #$8B74,(A4)+
            move(2,{I,0,35700},{P,4},false); pc=0x1730e;break;
        case 0x1730e: // $01730E: MOVE.W #$8B75,126(A4)
            move(2,{I,0,35701},{O,4,126},false); pc=0x17314;break;
        case 0x17314: // $017314: MOVE.W #$8B76,(A4)+
            move(2,{I,0,35702},{P,4},false); pc=0x17318;break;
        case 0x17318: // $017318: MOVE.W #$8B77,126(A4)
            move(2,{I,0,35703},{O,4,126},false); pc=0x1731e;break;
        case 0x1731e: // $01731E: RTS
            if(ret()) return returned;continue;
        case 0x17320: // $017320: LEA ($0003107C).L,A2
            lea({L,0,0x3107c},2); pc=0x17326;break;
        case 0x17326: // $017326: BSR $0179E4
            call({L,0,0x0179e4},0x1732a,true);continue;
        case 0x1732a: // $01732A: BSR $017A76
            call({L,0,0x017a76},0x1732e,true);continue;
        case 0x1732e: // $01732E: BSR $017AA8
            call({L,0,0x017aa8},0x17332,true);continue;
        case 0x17332: // $017332: BSR $017AC0
            call({L,0,0x017ac0},0x17336,true);continue;
        case 0x17336: // $017336: CMPI.B #$5,20(A5)
            alu(CMP,1,{I,0,5},{O,5,20},true,false); pc=0x1733c;break;
        case 0x1733c: // $01733C: BNE $01747E
            if(branch(NE,0x1747e,true)) continue; pc=0x17340;break;
        case 0x17340: // $017340: MOVEA.L 46(A5),A3
            move(4,{O,5,46},{A,3},true); pc=0x17344;break;
        case 0x17344: // $017344: MOVE.B #$0,40(A5)
            move(1,{I,0,0},{O,5,40},false); pc=0x1734a;break;
        case 0x1734a: // $01734A: MOVEQ #29,D0
            moveq({I,0,29},0); pc=0x1734c;break;
        case 0x1734c: // $01734C: LEA (A3),A2
            lea({M,3},2); pc=0x1734e;break;
        case 0x1734e: // $01734E: SUBA.L #$F000,A2
            adda(4,{I,0,61440},2,true); pc=0x17354;break;
        case 0x17354: // $017354: MOVE.L -128(A3),D2
            move(4,{O,3,-128},{D,2},false); pc=0x17358;break;
        case 0x17358: // $017358: MOVE.L (A3)+,D1
            move(4,{P,3},{D,1},false); pc=0x1735a;break;
        case 0x1735a: // $01735A: ANDI.L #$1FF01FF,D1
            alu(AND,4,{I,0,33489407},{D,1},true,false); pc=0x17360;break;
        case 0x17360: // $017360: ORI.L #$80008000,D1
            alu(OR,4,{I,0,2147516416},{D,1},true,false); pc=0x17366;break;
        case 0x17366: // $017366: ANDI.L #$1FF01FF,D2
            alu(AND,4,{I,0,33489407},{D,2},true,false); pc=0x1736c;break;
        case 0x1736c: // $01736C: ORI.L #$80008000,D2
            alu(OR,4,{I,0,2147516416},{D,2},true,false); pc=0x17372;break;
        case 0x17372: // $017372: MOVE.L D2,-128(A2)
            move(4,{D,2},{O,2,-128},false); pc=0x17376;break;
        case 0x17376: // $017376: MOVE.L D1,(A2)+
            move(4,{D,1},{P,2},false); pc=0x17378;break;
        case 0x17378: // $017378: DBF D0,$017354
            if(db(F,0,0x17354)) continue; pc=0x1737c;break;
        case 0x1737c: // $01737C: MOVEQ #19,D0
            moveq({I,0,19},0); pc=0x1737e;break;
        case 0x1737e: // $01737E: MOVEA.L 46(A5),A3
            move(4,{O,5,46},{A,3},true); pc=0x17382;break;
        case 0x17382: // $017382: MOVE.L #$0,(A3)+
            move(4,{I,0,0},{P,3},false); pc=0x17388;break;
        case 0x17388: // $017388: DBF D0,$017382
            if(db(F,0,0x17382)) continue; pc=0x1738c;break;
        case 0x1738c: // $01738C: MOVE.B #$0,41(A5)
            move(1,{I,0,0},{O,5,41},false); pc=0x17392;break;
        case 0x17392: // $017392: CLR.L D0
            unary(true,4,{D,0}); pc=0x17394;break;
        case 0x17394: // $017394: MOVE.B 22(A5),D0
            move(1,{O,5,22},{D,0},false); pc=0x17398;break;
        case 0x17398: // $017398: MULU.W #$E,D0
            mulu({I,0,14},0); pc=0x1739c;break;
        case 0x1739c: // $01739C: LEA 110(A5,D0.W),A0
            lea({X,5,110,0},0); pc=0x173a0;break;
        case 0x173a0: // $0173A0: LEA 4(A0),A0
            lea({O,0,4},0); pc=0x173a4;break;
        case 0x173a4: // $0173A4: MOVE.L (A0),D0
            move(4,{M,0},{D,0},false); pc=0x173a6;break;
        case 0x173a6: // $0173A6: CMPI.L #$53455820,D0
            alu(CMP,4,{I,0,1397053472},{D,0},true,false); pc=0x173ac;break;
        case 0x173ac: // $0173AC: BNE $0173E0
            if(branch(NE,0x173e0,true)) continue; pc=0x173b0;break;
        case 0x173b0: // $0173B0: MOVE.B 32(A5),D0
            move(1,{O,5,32},{D,0},false); pc=0x173b4;break;
        case 0x173b4: // $0173B4: ANDI.W #$3,D0
            alu(AND,2,{I,0,3},{D,0},true,false); pc=0x173b8;break;
        case 0x173b8: // $0173B8: MOVE.B D0,57(A5)
            move(1,{D,0},{O,5,57},false); pc=0x173bc;break;
        case 0x173bc: // $0173BC: LEA ($0003103A).L,A1
            lea({L,0,0x3103a},1); pc=0x173c2;break;
        case 0x173c2: // $0173C2: CLR.L D0
            unary(true,4,{D,0}); pc=0x173c4;break;
        case 0x173c4: // $0173C4: MOVE.B 57(A5),D0
            move(1,{O,5,57},{D,0},false); pc=0x173c8;break;
        case 0x173c8: // $0173C8: ADD.W D0,D0
            alu(ADD,2,{D,0},{D,0},false,false); pc=0x173ca;break;
        case 0x173ca: // $0173CA: ADD.W D0,D0
            alu(ADD,2,{D,0},{D,0},false,false); pc=0x173cc;break;
        case 0x173cc: // $0173CC: MOVE.L 0(A1,D0.W),(A0)
            move(4,{X,1,0,0},{M,0},false); pc=0x173d0;break;
        case 0x173d0: // $0173D0: MOVE.W #$0,6(A0)
            move(2,{I,0,0},{O,0,6},false); pc=0x173d6;break;
        case 0x173d6: // $0173D6: MOVE.B #$0,60(A5)
            move(1,{I,0,0},{O,5,60},false); pc=0x173dc;break;
        case 0x173dc: // $0173DC: BT $017452
            if(branch(T,0x17452,true)) continue; pc=0x173e0;break;
        case 0x173e0: // $0173E0: MOVE.L (A0),D0
            move(4,{M,0},{D,0},false); pc=0x173e2;break;
        case 0x173e2: // $0173E2: CMPI.L #$4B455920,D0
            alu(CMP,4,{I,0,1262836000},{D,0},true,false); pc=0x173e8;break;
        case 0x173e8: // $0173E8: BNE $0173FC
            if(branch(NE,0x173fc,true)) continue; pc=0x173ec;break;
        case 0x173ec: // $0173EC: MOVE.W #$8,6(A0)
            move(2,{I,0,8},{O,0,6},false); pc=0x173f2;break;
        case 0x173f2: // $0173F2: MOVE.B #$1,60(A5)
            move(1,{I,0,1},{O,5,60},false); pc=0x173f8;break;
        case 0x173f8: // $0173F8: BT $017452
            if(branch(T,0x17452,true)) continue; pc=0x173fc;break;
        case 0x173fc: // $0173FC: MOVE.L (A0),D0
            move(4,{M,0},{D,0},false); pc=0x173fe;break;
        case 0x173fe: // $0173FE: CMPI.L #$542E4B20,D0
            alu(CMP,4,{I,0,1412320032},{D,0},true,false); pc=0x17404;break;
        case 0x17404: // $017404: BNE $017438
            if(branch(NE,0x17438,true)) continue; pc=0x17408;break;
        case 0x17408: // $017408: TST.B ($913F).W
            unary(false,1,{W,0,0xffff913f}); pc=0x1740c;break;
        case 0x1740c: // $01740C: BEQ $017438
            if(branch(EQ,0x17438,true)) continue; pc=0x17410;break;
        case 0x17410: // $017410: MOVE.B #$FF,60(A5)
            move(1,{I,0,255},{O,5,60},false); pc=0x17416;break;
        case 0x17416: // $017416: MOVE.W #$8,6(A0)
            move(2,{I,0,8},{O,0,6},false); pc=0x1741c;break;
        case 0x1741c: // $01741C: LEA 110(A5),A0
            lea({O,5,110},0); pc=0x17420;break;
        case 0x17420: // $017420: LEA 4(A0),A0
            lea({O,0,4},0); pc=0x17424;break;
        case 0x17424: // $017424: MOVEQ #19,D1
            moveq({I,0,19},1); pc=0x17426;break;
        case 0x17426: // $017426: MOVE.L #$542E4B,(A0)
            move(4,{I,0,5516875},{M,0},false); pc=0x1742c;break;
        case 0x1742c: // $01742C: LEA 14(A0),A0
            lea({O,0,14},0); pc=0x17430;break;
        case 0x17430: // $017430: DBF D1,$017426
            if(db(F,1,0x17426)) continue; pc=0x17434;break;
        case 0x17434: // $017434: BT $017452
            if(branch(T,0x17452,true)) continue; pc=0x17438;break;
        case 0x17438: // $017438: MOVE.L (A0),D0
            move(4,{M,0},{D,0},false); pc=0x1743a;break;
        case 0x1743a: // $01743A: CMPI.L #$48495220,D0
            alu(CMP,4,{I,0,1212764704},{D,0},true,false); pc=0x17440;break;
        case 0x17440: // $017440: BNE $01746A
            if(branch(NE,0x1746a,true)) continue; pc=0x17444;break;
        case 0x17444: // $017444: MOVE.W #$AA,D0
            move(2,{I,0,170},{D,0},false); pc=0x17448;break;
        case 0x17448: // $017448: JSR ($0000B84C).L
            call({L,0,0xb84c},0x1744e,false);continue;
        case 0x1744e: // $01744E: BT $01746A
            if(branch(T,0x1746a,true)) continue; pc=0x17452;break;
        case 0x17452: // $017452: MOVE.B #$0,59(A5)
            move(1,{I,0,0},{O,5,59},false); pc=0x17458;break;
        case 0x17458: // $017458: MOVE.B #$0,58(A5)
            move(1,{I,0,0},{O,5,58},false); pc=0x1745e;break;
        case 0x1745e: // $01745E: MOVE.B #$0,56(A5)
            move(1,{I,0,0},{O,5,56},false); pc=0x17464;break;
        case 0x17464: // $017464: MOVE.B #$6,20(A5)
            move(1,{I,0,6},{O,5,20},false); pc=0x1746a;break;
        case 0x1746a: // $01746A: MOVE.W #$4F,D0
            move(2,{I,0,79},{D,0},false); pc=0x1746e;break;
        case 0x1746e: // $01746E: JSR ($0000B84C).L
            call({L,0,0xb84c},0x17474,false);continue;
        case 0x17474: // $017474: MOVE.W #$BB,D0
            move(2,{I,0,187},{D,0},false); pc=0x17478;break;
        case 0x17478: // $017478: JSR ($0000B84C).L
            call({L,0,0xb84c},0x1747e,false);continue;
        case 0x1747e: // $01747E: RTS
            if(ret()) return returned;continue;
        case 0x17480: // $017480: MOVE.B 40(A5),D0
            move(1,{O,5,40},{D,0},false); pc=0x17484;break;
        case 0x17484: // $017484: CMPI.W #$2D,D0
            alu(CMP,2,{I,0,45},{D,0},true,false); pc=0x17488;break;
        case 0x17488: // $017488: BGE $01749E
            if(branch(GE,0x1749e,true)) continue; pc=0x1748c;break;
        case 0x1748c: // $01748C: ANDI.W #$FF,D0
            alu(AND,2,{I,0,255},{D,0},true,false); pc=0x17490;break;
        case 0x17490: // $017490: ADD.W D0,D0
            alu(ADD,2,{D,0},{D,0},false,false); pc=0x17492;break;
        case 0x17492: // $017492: MOVEA.L 46(A5),A3
            move(4,{O,5,46},{A,3},true); pc=0x17496;break;
        case 0x17496: // $017496: LEA 0(A3,D0.W),A3
            lea({X,3,0,0},3); pc=0x1749a;break;
        case 0x1749a: // $01749A: BT $017904
            if(branch(T,0x17904,true)) continue; pc=0x1749e;break;
        case 0x1749e: // $01749E: MOVE.B #$7,20(A5)
            move(1,{I,0,7},{O,5,20},false); pc=0x174a4;break;
        case 0x174a4: // $0174A4: CLR.W ($8864).W
            unary(true,2,{W,0,0xffff8864}); pc=0x174a8;break;
        case 0x174a8: // $0174A8: MOVE.W #$3,($8860).W
            move(2,{I,0,3},{W,0,0xffff8860},false); pc=0x174ae;break;
        case 0x174ae: // $0174AE: RTS
            if(ret()) return returned;continue;
        case 0x174b0: // $0174B0: MOVE.B 40(A5),D0
            move(1,{O,5,40},{D,0},false); pc=0x174b4;break;
        case 0x174b4: // $0174B4: CMPI.B #$2D,D0
            alu(CMP,1,{I,0,45},{D,0},true,false); pc=0x174b8;break;
        case 0x174b8: // $0174B8: BGE $0175F4
            if(branch(GE,0x175f4,true)) continue; pc=0x174bc;break;
        case 0x174bc: // $0174BC: CMPI.B #$6,D0
            alu(CMP,1,{I,0,6},{D,0},true,false); pc=0x174c0;break;
        case 0x174c0: // $0174C0: BNE $0175B2
            if(branch(NE,0x175b2,true)) continue; pc=0x174c4;break;
        case 0x174c4: // $0174C4: BSR $017D0E
            call({L,0,0x017d0e},0x174c8,true);continue;
        case 0x174c8: // $0174C8: MOVE.B 58(A5),D0
            move(1,{O,5,58},{D,0},false); pc=0x174cc;break;
        case 0x174cc: // $0174CC: CMPI.B #$1,D0
            alu(CMP,1,{I,0,1},{D,0},true,false); pc=0x174d0;break;
        case 0x174d0: // $0174D0: BLT $0175A6
            if(branch(LT,0x175a6,true)) continue; pc=0x174d4;break;
        case 0x174d4: // $0174D4: MOVE.B 59(A5),D0
            move(1,{O,5,59},{D,0},false); pc=0x174d8;break;
        case 0x174d8: // $0174D8: BNE $0175AC
            if(branch(NE,0x175ac,true)) continue; pc=0x174dc;break;
        case 0x174dc: // $0174DC: MOVEA.L 52(A5),A3
            move(4,{O,5,52},{A,3},true); pc=0x174e0;break;
        case 0x174e0: // $0174E0: CLR.W D0
            unary(true,2,{D,0}); pc=0x174e2;break;
        case 0x174e2: // $0174E2: MOVE.B 56(A5),D0
            move(1,{O,5,56},{D,0},false); pc=0x174e6;break;
        case 0x174e6: // $0174E6: CMPI.B #$28,D0
            alu(CMP,1,{I,0,40},{D,0},true,false); pc=0x174ea;break;
        case 0x174ea: // $0174EA: BEQ $0175AC
            if(branch(EQ,0x175ac,true)) continue; pc=0x174ee;break;
        case 0x174ee: // $0174EE: CMPI.B #$0,D0
            alu(CMP,1,{I,0,0},{D,0},true,false); pc=0x174f2;break;
        case 0x174f2: // $0174F2: BNE $017502
            if(branch(NE,0x17502,true)) continue; pc=0x174f6;break;
        case 0x174f6: // $0174F6: LEA ($0003102E).L,A1
            lea({L,0,0x3102e},1); pc=0x174fc;break;
        case 0x174fc: // $0174FC: MOVE.W (A1),(A3)
            move(2,{M,1},{M,3},false); pc=0x174fe;break;
        case 0x174fe: // $0174FE: BT $01759C
            if(branch(T,0x1759c,true)) continue; pc=0x17502;break;
        case 0x17502: // $017502: LEA (A3),A4
            lea({M,3},4); pc=0x17504;break;
        case 0x17504: // $017504: ADD.W D0,D0
            alu(ADD,2,{D,0},{D,0},false,false); pc=0x17506;break;
        case 0x17506: // $017506: CMPI.B #$1E,D0
            alu(CMP,1,{I,0,30},{D,0},true,false); pc=0x1750a;break;
        case 0x1750a: // $01750A: BLT $017522
            if(branch(LT,0x17522,true)) continue; pc=0x1750e;break;
        case 0x1750e: // $01750E: CMPI.B #$24,D0
            alu(CMP,1,{I,0,36},{D,0},true,false); pc=0x17512;break;
        case 0x17512: // $017512: BGT $017522
            if(branch(GT,0x17522,true)) continue; pc=0x17516;break;
        case 0x17516: // $017516: SUBA.L #$F002,A4
            adda(4,{I,0,61442},4,true); pc=0x1751c;break;
        case 0x1751c: // $01751C: MOVE.W #$0,0(A4,D0.W)
            move(2,{I,0,0},{X,4,0,0},false); pc=0x17522;break;
        case 0x17522: // $017522: LEA ($00031030).L,A1
            lea({L,0,0x31030},1); pc=0x17528;break;
        case 0x17528: // $017528: MOVE.W (A1),-2(A3,D0.W)
            move(2,{M,1},{X,3,-2,0},false); pc=0x1752c;break;
        case 0x1752c: // $01752C: CMPI.B #$4,D0
            alu(CMP,1,{I,0,4},{D,0},true,false); pc=0x17530;break;
        case 0x17530: // $017530: BLE $01759C
            if(branch(LE,0x1759c,true)) continue; pc=0x17534;break;
        case 0x17534: // $017534: MOVE.W #$0,-6(A3,D0.W)
            move(2,{I,0,0},{X,3,-6,0},false); pc=0x1753a;break;
        case 0x1753a: // $01753A: MOVEA.L 42(A5),A2
            move(4,{O,5,42},{A,2},true); pc=0x1753e;break;
        case 0x1753e: // $01753E: LEA ($00031032).L,A1
            lea({L,0,0x31032},1); pc=0x17544;break;
        case 0x17544: // $017544: CLR.W D0
            unary(true,2,{D,0}); pc=0x17546;break;
        case 0x17546: // $017546: MOVE.B 56(A5),D0
            move(1,{O,5,56},{D,0},false); pc=0x1754a;break;
        case 0x1754a: // $01754A: ASR.W #3,D0
            shift(ASR,2,{I,0,3},0); pc=0x1754c;break;
        case 0x1754c: // $01754C: ANDI.W #$7,D0
            alu(AND,2,{I,0,7},{D,0},true,false); pc=0x17550;break;
        case 0x17550: // $017550: ADD.W D0,D0
            alu(ADD,2,{D,0},{D,0},false,false); pc=0x17552;break;
        case 0x17552: // $017552: ADD.W D0,D0
            alu(ADD,2,{D,0},{D,0},false,false); pc=0x17554;break;
        case 0x17554: // $017554: JMP $017558(PC,D0.W)
            jump({J,0,0x17558,0});continue;
        case 0x17558: // $017558: BT $017578
            if(branch(T,0x17578,true)) continue; pc=0x1755c;break;
        case 0x1755c: // $01755C: BT $017582
            if(branch(T,0x17582,true)) continue; pc=0x17560;break;
        case 0x17560: // $017560: BT $01758C
            if(branch(T,0x1758c,true)) continue; pc=0x17564;break;
        case 0x17564: // $017564: BT $017596
            if(branch(T,0x17596,true)) continue; pc=0x17568;break;
        case 0x17568: // $017568: BT $017596
            if(branch(T,0x17596,true)) continue; pc=0x1756c;break;
        case 0x1756c: // $01756C: BT $017596
            if(branch(T,0x17596,true)) continue; pc=0x17570;break;
        case 0x17570: // $017570: BT $017596
            if(branch(T,0x17596,true)) continue; pc=0x17574;break;
        case 0x17574: // $017574: BT $017596
            if(branch(T,0x17596,true)) continue; pc=0x17578;break;
        case 0x17578: // $017578: LEA -96(A2),A2
            lea({O,2,-96},2); pc=0x1757c;break;
        case 0x1757c: // $01757C: MOVE.L (A1),(A2)
            move(4,{M,1},{M,2},false); pc=0x1757e;break;
        case 0x1757e: // $01757E: BT $01759C
            if(branch(T,0x1759c,true)) continue; pc=0x17582;break;
        case 0x17582: // $017582: LEA -98(A2),A2
            lea({O,2,-98},2); pc=0x17586;break;
        case 0x17586: // $017586: MOVE.L (A1),(A2)
            move(4,{M,1},{M,2},false); pc=0x17588;break;
        case 0x17588: // $017588: BT $01759C
            if(branch(T,0x1759c,true)) continue; pc=0x1758c;break;
        case 0x1758c: // $01758C: LEA -100(A2),A2
            lea({O,2,-100},2); pc=0x17590;break;
        case 0x17590: // $017590: MOVE.L (A1),(A2)
            move(4,{M,1},{M,2},false); pc=0x17592;break;
        case 0x17592: // $017592: BT $01759C
            if(branch(T,0x1759c,true)) continue; pc=0x17596;break;
        case 0x17596: // $017596: LEA -102(A2),A2
            lea({O,2,-102},2); pc=0x1759a;break;
        case 0x1759a: // $01759A: MOVE.L (A1),(A2)
            move(4,{M,1},{M,2},false); pc=0x1759c;break;
        case 0x1759c: // $01759C: ADDQ.B #1,56(A5)
            alu(ADD,1,{I,0,1},{O,5,56},false,true); pc=0x175a0;break;
        case 0x175a0: // $0175A0: MOVE.B #$FF,58(A5)
            move(1,{I,0,255},{O,5,58},false); pc=0x175a6;break;
        case 0x175a6: // $0175A6: ADDQ.B #1,58(A5)
            alu(ADD,1,{I,0,1},{O,5,58},false,true); pc=0x175aa;break;
        case 0x175aa: // $0175AA: RTS
            if(ret()) return returned;continue;
        case 0x175ac: // $0175AC: MOVE.B #$1,59(A5)
            move(1,{I,0,1},{O,5,59},false); pc=0x175b2;break;
        case 0x175b2: // $0175B2: MOVEA.L 42(A5),A2
            move(4,{O,5,42},{A,2},true); pc=0x175b6;break;
        case 0x175b6: // $0175B6: LEA ($00031032).L,A1
            lea({L,0,0x31032},1); pc=0x175bc;break;
        case 0x175bc: // $0175BC: MOVE.B 40(A5),D0
            move(1,{O,5,40},{D,0},false); pc=0x175c0;break;
        case 0x175c0: // $0175C0: ANDI.W #$FF,D0
            alu(AND,2,{I,0,255},{D,0},true,false); pc=0x175c4;break;
        case 0x175c4: // $0175C4: MOVE.W D0,D1
            move(2,{D,0},{D,1},false); pc=0x175c6;break;
        case 0x175c6: // $0175C6: BTST #1,D1
            bit(BTST,{I,0,1},{D,1}); pc=0x175ca;break;
        case 0x175ca: // $0175CA: BEQ $0175D4
            if(branch(EQ,0x175d4,true)) continue; pc=0x175ce;break;
        case 0x175ce: // $0175CE: LEA ($00031036).L,A1
            lea({L,0,0x31036},1); pc=0x175d4;break;
        case 0x175d4: // $0175D4: MOVE.B 40(A5),D0
            move(1,{O,5,40},{D,0},false); pc=0x175d8;break;
        case 0x175d8: // $0175D8: CMPI.B #$6,D0
            alu(CMP,1,{I,0,6},{D,0},true,false); pc=0x175dc;break;
        case 0x175dc: // $0175DC: BLE $0175E6
            if(branch(LE,0x175e6,true)) continue; pc=0x175e0;break;
        case 0x175e0: // $0175E0: LEA -102(A2),A2
            lea({O,2,-102},2); pc=0x175e4;break;
        case 0x175e4: // $0175E4: MOVE.L (A1),(A2)
            move(4,{M,1},{M,2},false); pc=0x175e6;break;
        case 0x175e6: // $0175E6: ADD.W D0,D0
            alu(ADD,2,{D,0},{D,0},false,false); pc=0x175e8;break;
        case 0x175e8: // $0175E8: MOVEA.L 46(A5),A3
            move(4,{O,5,46},{A,3},true); pc=0x175ec;break;
        case 0x175ec: // $0175EC: LEA 0(A3,D0.W),A3
            lea({X,3,0,0},3); pc=0x175f0;break;
        case 0x175f0: // $0175F0: BT $017904
            if(branch(T,0x17904,true)) continue; pc=0x175f4;break;
        case 0x175f4: // $0175F4: MOVE.B #$7,20(A5)
            move(1,{I,0,7},{O,5,20},false); pc=0x175fa;break;
        case 0x175fa: // $0175FA: CLR.W ($8864).W
            unary(true,2,{W,0,0xffff8864}); pc=0x175fe;break;
        case 0x175fe: // $0175FE: MOVE.W #$3,($8860).W
            move(2,{I,0,3},{W,0,0xffff8860},false); pc=0x17604;break;
        case 0x17604: // $017604: RTS
            if(ret()) return returned;continue;
        case 0x17606: // $017606: RTS
            if(ret()) return returned;continue;
        case 0x17608: // $017608: LEA 110(A5),A0
            lea({O,5,110},0); pc=0x1760c;break;
        case 0x1760c: // $01760C: MOVEQ #0,D7
            moveq({I,0,0},7); pc=0x1760e;break;
        case 0x1760e: // $01760E: MOVEQ #19,D1
            moveq({I,0,19},1); pc=0x17610;break;
        case 0x17610: // $017610: MOVE.L ($8884).W,D0
            move(4,{W,0,0xffff8884},{D,0},false); pc=0x17614;break;
        case 0x17614: // $017614: CMP.L (A0),D0
            alu(CMP,4,{M,0},{D,0},false,false); pc=0x17616;break;
        case 0x17616: // $017616: BCC $017624
            if(branch(CC,0x17624,true)) continue; pc=0x1761a;break;
        case 0x1761a: // $01761A: ADDQ.W #1,D7
            alu(ADD,2,{I,0,1},{D,7},false,true); pc=0x1761c;break;
        case 0x1761c: // $01761C: LEA 14(A0),A0
            lea({O,0,14},0); pc=0x17620;break;
        case 0x17620: // $017620: DBF D1,$017614
            if(db(F,1,0x17614)) continue; pc=0x17624;break;
        case 0x17624: // $017624: MOVE.B D7,22(A5)
            move(1,{D,7},{O,5,22},false); pc=0x17628;break;
        case 0x17628: // $017628: RTS
            if(ret()) return returned;continue;
        case 0x1762a: // $01762A: LEA 390(A5),A0
            lea({O,5,390},0); pc=0x1762e;break;
        case 0x1762e: // $01762E: LEA 376(A5),A4
            lea({O,5,376},4); pc=0x17632;break;
        case 0x17632: // $017632: MOVE.W #$12,D1
            move(2,{I,0,18},{D,1},false); pc=0x17636;break;
        case 0x17636: // $017636: CMPI.W #$13,D7
            alu(CMP,2,{I,0,19},{D,7},true,false); pc=0x1763a;break;
        case 0x1763a: // $01763A: BEQ $01764C
            if(branch(EQ,0x1764c,true)) continue; pc=0x1763e;break;
        case 0x1763e: // $01763E: SUB.W D7,D1
            alu(SUB,2,{D,7},{D,1},false,false); pc=0x17640;break;
        case 0x17640: // $017640: MOVE.L -(A4),-(A0)
            move(4,{N,4},{N,0},false); pc=0x17642;break;
        case 0x17642: // $017642: MOVE.L -(A4),-(A0)
            move(4,{N,4},{N,0},false); pc=0x17644;break;
        case 0x17644: // $017644: MOVE.W -(A4),-(A0)
            move(2,{N,4},{N,0},false); pc=0x17646;break;
        case 0x17646: // $017646: MOVE.L -(A4),-(A0)
            move(4,{N,4},{N,0},false); pc=0x17648;break;
        case 0x17648: // $017648: DBF D1,$017640
            if(db(F,1,0x17640)) continue; pc=0x1764c;break;
        case 0x1764c: // $01764C: MOVE.L D0,(A4)+
            move(4,{D,0},{P,4},false); pc=0x1764e;break;
        case 0x1764e: // $01764E: MOVE.L #$20202020,(A4)+
            move(4,{I,0,538976288},{P,4},false); pc=0x17654;break;
        case 0x17654: // $017654: BSR $017FC6
            call({L,0,0x017fc6},0x17658,true);continue;
        case 0x17658: // $017658: MOVE.W D1,(A4)+
            move(2,{D,1},{P,4},false); pc=0x1765a;break;
        case 0x1765a: // $01765A: BSR $017662
            call({L,0,0x017662},0x1765e,true);continue;
        case 0x1765e: // $01765E: MOVE.W D2,(A4)
            move(2,{D,2},{M,4},false); pc=0x17660;break;
        case 0x17660: // $017660: RTS
            if(ret()) return returned;continue;
        case 0x17662: // $017662: CLR.L D0
            unary(true,4,{D,0}); pc=0x17664;break;
        case 0x17664: // $017664: CLR.L D1
            unary(true,4,{D,1}); pc=0x17666;break;
        case 0x17666: // $017666: CLR.L D2
            unary(true,4,{D,2}); pc=0x17668;break;
        case 0x17668: // $017668: MOVE.W ($0029C0B8).L,D1
            move(2,{L,0,0x29c0b8},{D,1},false); pc=0x1766e;break;
        case 0x1766e: // $01766E: SUB.W ($0029C0BC).L,D1
            alu(SUB,2,{L,0,0x29c0bc},{D,1},false,false); pc=0x17674;break;
        case 0x17674: // $017674: MOVE.W ($0029C0BA).L,D0
            move(2,{L,0,0x29c0ba},{D,0},false); pc=0x1767a;break;
        case 0x1767a: // $01767A: ADDQ.W #1,D1
            alu(ADD,2,{I,0,1},{D,1},false,true); pc=0x1767c;break;
        case 0x1767c: // $01767C: ADDQ.W #1,D0
            alu(ADD,2,{I,0,1},{D,0},false,true); pc=0x1767e;break;
        case 0x1767e: // $01767E: DIVU.W D1,D0
            divu({D,1},0); pc=0x17680;break;
        case 0x17680: // $017680: MOVE.W D0,D1
            move(2,{D,0},{D,1},false); pc=0x17682;break;
        case 0x17682: // $017682: ANDI.W #$F,D1
            alu(AND,2,{I,0,15},{D,1},true,false); pc=0x17686;break;
        case 0x17686: // $017686: CMPI.W #$1,D1
            alu(CMP,2,{I,0,1},{D,1},true,false); pc=0x1768a;break;
        case 0x1768a: // $01768A: BLE $0176B6
            if(branch(LE,0x176b6,true)) continue; pc=0x1768e;break;
        case 0x1768e: // $01768E: CMPI.W #$14,D0
            alu(CMP,2,{I,0,20},{D,0},true,false); pc=0x17692;break;
        case 0x17692: // $017692: BGE $0176D6
            if(branch(GE,0x176d6,true)) continue; pc=0x17696;break;
        case 0x17696: // $017696: CMPI.W #$F,D0
            alu(CMP,2,{I,0,15},{D,0},true,false); pc=0x1769a;break;
        case 0x1769a: // $01769A: BGE $0176CE
            if(branch(GE,0x176ce,true)) continue; pc=0x1769e;break;
        case 0x1769e: // $01769E: CMPI.W #$A,D0
            alu(CMP,2,{I,0,10},{D,0},true,false); pc=0x176a2;break;
        case 0x176a2: // $0176A2: BGE $0176C6
            if(branch(GE,0x176c6,true)) continue; pc=0x176a6;break;
        case 0x176a6: // $0176A6: CMPI.W #$5,D0
            alu(CMP,2,{I,0,5},{D,0},true,false); pc=0x176aa;break;
        case 0x176aa: // $0176AA: BGE $0176BE
            if(branch(GE,0x176be,true)) continue; pc=0x176ae;break;
        case 0x176ae: // $0176AE: MOVE.B #$1,D2
            move(1,{I,0,1},{D,2},false); pc=0x176b2;break;
        case 0x176b2: // $0176B2: BT $0176DA
            if(branch(T,0x176da,true)) continue; pc=0x176b6;break;
        case 0x176b6: // $0176B6: MOVE.B #$0,D2
            move(1,{I,0,0},{D,2},false); pc=0x176ba;break;
        case 0x176ba: // $0176BA: BT $0176DA
            if(branch(T,0x176da,true)) continue; pc=0x176be;break;
        case 0x176be: // $0176BE: MOVE.B #$2,D2
            move(1,{I,0,2},{D,2},false); pc=0x176c2;break;
        case 0x176c2: // $0176C2: BT $0176DA
            if(branch(T,0x176da,true)) continue; pc=0x176c6;break;
        case 0x176c6: // $0176C6: MOVE.B #$3,D2
            move(1,{I,0,3},{D,2},false); pc=0x176ca;break;
        case 0x176ca: // $0176CA: BT $0176DA
            if(branch(T,0x176da,true)) continue; pc=0x176ce;break;
        case 0x176ce: // $0176CE: MOVE.B #$4,D2
            move(1,{I,0,4},{D,2},false); pc=0x176d2;break;
        case 0x176d2: // $0176D2: BT $0176DA
            if(branch(T,0x176da,true)) continue; pc=0x176d6;break;
        case 0x176d6: // $0176D6: MOVE.B #$5,D2
            move(1,{I,0,5},{D,2},false); pc=0x176da;break;
        case 0x176da: // $0176DA: RTS
            if(ret()) return returned;continue;
        case 0x176dc: // $0176DC: CLR.W 38(A5)
            unary(true,2,{O,5,38}); pc=0x176e0;break;
        case 0x176e0: // $0176E0: MOVE.B 22(A5),D0
            move(1,{O,5,22},{D,0},false); pc=0x176e4;break;
        case 0x176e4: // $0176E4: ANDI.W #$FF,D0
            alu(AND,2,{I,0,255},{D,0},true,false); pc=0x176e8;break;
        case 0x176e8: // $0176E8: SUBQ.W #3,D0
            alu(SUB,2,{I,0,3},{D,0},false,true); pc=0x176ea;break;
        case 0x176ea: // $0176EA: BCC $0176F4
            if(branch(CC,0x176f4,true)) continue; pc=0x176ee;break;
        case 0x176ee: // $0176EE: CLR.W D0
            unary(true,2,{D,0}); pc=0x176f0;break;
        case 0x176f0: // $0176F0: BT $017700
            if(branch(T,0x17700,true)) continue; pc=0x176f4;break;
        case 0x176f4: // $0176F4: CMPI.W #$E,D0
            alu(CMP,2,{I,0,14},{D,0},true,false); pc=0x176f8;break;
        case 0x176f8: // $0176F8: BCS $017700
            if(branch(CS,0x17700,true)) continue; pc=0x176fc;break;
        case 0x176fc: // $0176FC: MOVE.W #$D,D0
            move(2,{I,0,13},{D,0},false); pc=0x17700;break;
        case 0x17700: // $017700: MOVE.W D0,38(A5)
            move(2,{D,0},{O,5,38},false); pc=0x17704;break;
        case 0x17704: // $017704: RTS
            if(ret()) return returned;continue;
        case 0x17706: // $017706: MOVEQ #6,D1
            moveq({I,0,6},1); pc=0x17708;break;
        case 0x17708: // $017708: MOVEQ #0,D0
            moveq({I,0,0},0); pc=0x1770a;break;
        case 0x1770a: // $01770A: MOVE.W 38(A5),D0
            move(2,{O,5,38},{D,0},false); pc=0x1770e;break;
        case 0x1770e: // $01770E: ADDQ.W #1,D0
            alu(ADD,2,{I,0,1},{D,0},false,true); pc=0x17710;break;
        case 0x17710: // $017710: MOVE.L D0,D2
            move(4,{D,0},{D,2},false); pc=0x17712;break;
        case 0x17712: // $017712: DIVU.W #$A,D2
            divu({I,0,10},2); pc=0x17716;break;
        case 0x17716: // $017716: BNE $017728
            if(branch(NE,0x17728,true)) continue; pc=0x1771a;break;
        case 0x1771a: // $01771A: MOVE.W #$20,D2
            move(2,{I,0,32},{D,2},false); pc=0x1771e;break;
        case 0x1771e: // $01771E: SWAP D2
            swap(2); pc=0x17720;break;
        case 0x17720: // $017720: ORI.W #$8030,D2
            alu(OR,2,{I,0,32816},{D,2},true,false); pc=0x17724;break;
        case 0x17724: // $017724: BT $017730
            if(branch(T,0x17730,true)) continue; pc=0x17728;break;
        case 0x17728: // $017728: SWAP D2
            swap(2); pc=0x1772a;break;
        case 0x1772a: // $01772A: ORI.L #$80308030,D2
            alu(OR,4,{I,0,2150662192},{D,2},true,false); pc=0x17730;break;
        case 0x17730: // $017730: MOVE.L D2,(A0)
            move(4,{D,2},{M,0},false); pc=0x17732;break;
        case 0x17732: // $017732: MOVE.W #$2E,4(A0)
            move(2,{I,0,46},{O,0,4},false); pc=0x17738;break;
        case 0x17738: // $017738: LEA 256(A0),A0
            lea({O,0,256},0); pc=0x1773c;break;
        case 0x1773c: // $01773C: ADDQ.W #1,D0
            alu(ADD,2,{I,0,1},{D,0},false,true); pc=0x1773e;break;
        case 0x1773e: // $01773E: DBF D1,$017710
            if(db(F,1,0x17710)) continue; pc=0x17742;break;
        case 0x17742: // $017742: RTS
            if(ret()) return returned;continue;
        case 0x17744: // $017744: MOVE.W 38(A5),D0
            move(2,{O,5,38},{D,0},false); pc=0x17748;break;
        case 0x17748: // $017748: MULU.W #$E,D0
            mulu({I,0,14},0); pc=0x1774c;break;
        case 0x1774c: // $01774C: LEA 110(A5,D0.W),A2
            lea({X,5,110,0},2); pc=0x17750;break;
        case 0x17750: // $017750: MOVEQ #6,D2
            moveq({I,0,6},2); pc=0x17752;break;
        case 0x17752: // $017752: LEA (A1),A0
            lea({M,1},0); pc=0x17754;break;
        case 0x17754: // $017754: LEA 16(A0),A3
            lea({O,0,16},3); pc=0x17758;break;
        case 0x17758: // $017758: MOVE.L (A2),D0
            move(4,{M,2},{D,0},false); pc=0x1775a;break;
        case 0x1775a: // $01775A: BEQ $017764
            if(branch(EQ,0x17764,true)) continue; pc=0x1775e;break;
        case 0x1775e: // $01775E: CLR.W D1
            unary(true,2,{D,1}); pc=0x17760;break;
        case 0x17760: // $017760: BSR $017DE2
            call({L,0,0x017de2},0x17764,true);continue;
        case 0x17764: // $017764: MOVE.W #$8030,(A3)
            move(2,{I,0,32816},{M,3},false); pc=0x17768;break;
        case 0x17768: // $017768: LEA 14(A2),A2
            lea({O,2,14},2); pc=0x1776c;break;
        case 0x1776c: // $01776C: LEA 256(A1),A1
            lea({O,1,256},1); pc=0x17770;break;
        case 0x17770: // $017770: DBF D2,$017752
            if(db(F,2,0x17752)) continue; pc=0x17774;break;
        case 0x17774: // $017774: RTS
            if(ret()) return returned;continue;
        case 0x17776: // $017776: MOVE.W 38(A5),D0
            move(2,{O,5,38},{D,0},false); pc=0x1777a;break;
        case 0x1777a: // $01777A: MULU.W #$E,D0
            mulu({I,0,14},0); pc=0x1777e;break;
        case 0x1777e: // $01777E: LEA 110(A5,D0.W),A1
            lea({X,5,110,0},1); pc=0x17782;break;
        case 0x17782: // $017782: MOVEQ #6,D0
            moveq({I,0,6},0); pc=0x17784;break;
        case 0x17784: // $017784: MOVE.W #$8000,D1
            move(2,{I,0,32768},{D,1},false); pc=0x17788;break;
        case 0x17788: // $017788: LEA 4(A1),A2
            lea({O,1,4},2); pc=0x1778c;break;
        case 0x1778c: // $01778C: LEA (A0),A3
            lea({M,0},3); pc=0x1778e;break;
        case 0x1778e: // $01778E: MOVEQ #3,D2
            moveq({I,0,3},2); pc=0x17790;break;
        case 0x17790: // $017790: MOVE.B (A2)+,D1
            move(1,{P,2},{D,1},false); pc=0x17792;break;
        case 0x17792: // $017792: MOVE.W D1,(A3)+
            move(2,{D,1},{P,3},false); pc=0x17794;break;
        case 0x17794: // $017794: DBF D2,$017790
            if(db(F,2,0x17790)) continue; pc=0x17798;break;
        case 0x17798: // $017798: LEA 256(A0),A0
            lea({O,0,256},0); pc=0x1779c;break;
        case 0x1779c: // $01779C: LEA 14(A1),A1
            lea({O,1,14},1); pc=0x177a0;break;
        case 0x177a0: // $0177A0: DBF D0,$017788
            if(db(F,0,0x17788)) continue; pc=0x177a4;break;
        case 0x177a4: // $0177A4: RTS
            if(ret()) return returned;continue;
        case 0x177a6: // $0177A6: MOVE.W 38(A5),D0
            move(2,{O,5,38},{D,0},false); pc=0x177aa;break;
        case 0x177aa: // $0177AA: MULU.W #$E,D0
            mulu({I,0,14},0); pc=0x177ae;break;
        case 0x177ae: // $0177AE: LEA 118(A5,D0.W),A3
            lea({X,5,118,0},3); pc=0x177b2;break;
        case 0x177b2: // $0177B2: MOVEQ #6,D2
            moveq({I,0,6},2); pc=0x177b4;break;
        case 0x177b4: // $0177B4: LEA (A1),A0
            lea({M,1},0); pc=0x177b6;break;
        case 0x177b6: // $0177B6: MOVE.W (A3),D0
            move(2,{M,3},{D,0},false); pc=0x177b8;break;
        case 0x177b8: // $0177B8: MOVE.W #$8000,D1
            move(2,{I,0,32768},{D,1},false); pc=0x177bc;break;
        case 0x177bc: // $0177BC: BSR $017D64
            call({L,0,0x017d64},0x177c0,true);continue;
        case 0x177c0: // $0177C0: LEA 256(A1),A1
            lea({O,1,256},1); pc=0x177c4;break;
        case 0x177c4: // $0177C4: LEA 14(A3),A3
            lea({O,3,14},3); pc=0x177c8;break;
        case 0x177c8: // $0177C8: DBF D2,$0177B4
            if(db(F,2,0x177b4)) continue; pc=0x177cc;break;
        case 0x177cc: // $0177CC: RTS
            if(ret()) return returned;continue;
        case 0x177ce: // $0177CE: MOVE.W 38(A5),D0
            move(2,{O,5,38},{D,0},false); pc=0x177d2;break;
        case 0x177d2: // $0177D2: MULU.W #$E,D0
            mulu({I,0,14},0); pc=0x177d6;break;
        case 0x177d6: // $0177D6: LEA 110(A5,D0.W),A1
            lea({X,5,110,0},1); pc=0x177da;break;
        case 0x177da: // $0177DA: MOVEQ #6,D0
            moveq({I,0,6},0); pc=0x177dc;break;
        case 0x177dc: // $0177DC: MOVE.W 10(A1),D1
            move(2,{O,1,10},{D,1},false); pc=0x177e0;break;
        case 0x177e0: // $0177E0: BSR $018006
            call({L,0,0x018006},0x177e4,true);continue;
        case 0x177e4: // $0177E4: LEA 256(A0),A0
            lea({O,0,256},0); pc=0x177e8;break;
        case 0x177e8: // $0177E8: LEA 14(A1),A1
            lea({O,1,14},1); pc=0x177ec;break;
        case 0x177ec: // $0177EC: DBF D0,$0177DC
            if(db(F,0,0x177dc)) continue; pc=0x177f0;break;
        case 0x177f0: // $0177F0: RTS
            if(ret()) return returned;continue;
        case 0x177f2: // $0177F2: LEA ($000C0000).L,A0
            lea({L,0,0xc0000},0); pc=0x177f8;break;
        case 0x177f8: // $0177F8: MOVE.L #$200020,D1
            move(4,{I,0,2097184},{D,1},false); pc=0x177fe;break;
        case 0x177fe: // $0177FE: MOVE.W #$3FF,D0
            move(2,{I,0,1023},{D,0},false); pc=0x17802;break;
        case 0x17802: // $017802: MOVE.L D1,(A0)+
            move(4,{D,1},{P,0},false); pc=0x17804;break;
        case 0x17804: // $017804: DBF D0,$017802
            if(db(F,0,0x17802)) continue; pc=0x17808;break;
        case 0x17808: // $017808: RTS
            if(ret()) return returned;continue;
        case 0x1780a: // $01780A: LEA ($000C1000).L,A0
            lea({L,0,0xc1000},0); pc=0x17810;break;
        case 0x17810: // $017810: MOVE.L #$80208020,D1
            move(4,{I,0,2149613600},{D,1},false); pc=0x17816;break;
        case 0x17816: // $017816: MOVE.W #$3FF,D0
            move(2,{I,0,1023},{D,0},false); pc=0x1781a;break;
        case 0x1781a: // $01781A: MOVE.L D1,(A0)+
            move(4,{D,1},{P,0},false); pc=0x1781c;break;
        case 0x1781c: // $01781C: DBF D0,$01781A
            if(db(F,0,0x1781a)) continue; pc=0x17820;break;
        case 0x17820: // $017820: RTS
            if(ret()) return returned;continue;
        case 0x17822: // $017822: MOVE.B 22(A5),D0
            move(1,{O,5,22},{D,0},false); pc=0x17826;break;
        case 0x17826: // $017826: ANDI.W #$FF,D0
            alu(AND,2,{I,0,255},{D,0},true,false); pc=0x1782a;break;
        case 0x1782a: // $01782A: CMPI.W #$3,D0
            alu(CMP,2,{I,0,3},{D,0},true,false); pc=0x1782e;break;
        case 0x1782e: // $01782E: BLT $01784A
            if(branch(LT,0x1784a,true)) continue; pc=0x17832;break;
        case 0x17832: // $017832: CMPI.W #$11,D0
            alu(CMP,2,{I,0,17},{D,0},true,false); pc=0x17836;break;
        case 0x17836: // $017836: BGE $017864
            if(branch(GE,0x17864,true)) continue; pc=0x1783a;break;
        case 0x1783a: // $01783A: LEA ($000D0752).L,A1
            lea({L,0,0xd0752},1); pc=0x17840;break;
        case 0x17840: // $017840: LEA ($000D0728).L,A3
            lea({L,0,0xd0728},3); pc=0x17846;break;
        case 0x17846: // $017846: BT $01787E
            if(branch(T,0x1787e,true)) continue; pc=0x1784a;break;
        case 0x1784a: // $01784A: LEA ($000D0452).L,A1
            lea({L,0,0xd0452},1); pc=0x17850;break;
        case 0x17850: // $017850: LEA ($000D0428).L,A3
            lea({L,0,0xd0428},3); pc=0x17856;break;
        case 0x17856: // $017856: ASL.W #8,D0
            shift(ASL,2,{I,0,8},0); pc=0x17858;break;
        case 0x17858: // $017858: LEA 0(A3,D0.W),A3
            lea({X,3,0,0},3); pc=0x1785c;break;
        case 0x1785c: // $01785C: LEA 0(A1,D0.W),A1
            lea({X,1,0,0},1); pc=0x17860;break;
        case 0x17860: // $017860: BT $01787E
            if(branch(T,0x1787e,true)) continue; pc=0x17864;break;
        case 0x17864: // $017864: LEA ($000D0A52).L,A1
            lea({L,0,0xd0a52},1); pc=0x1786a;break;
        case 0x1786a: // $01786A: LEA ($000D0A28).L,A3
            lea({L,0,0xd0a28},3); pc=0x17870;break;
        case 0x17870: // $017870: SUBI.W #$13,D0
            alu(SUB,2,{I,0,19},{D,0},true,false); pc=0x17874;break;
        case 0x17874: // $017874: ASL.W #8,D0
            shift(ASL,2,{I,0,8},0); pc=0x17876;break;
        case 0x17876: // $017876: LEA 0(A1,D0.W),A1
            lea({X,1,0,0},1); pc=0x1787a;break;
        case 0x1787a: // $01787A: LEA 0(A3,D0.W),A3
            lea({X,3,0,0},3); pc=0x1787e;break;
        case 0x1787e: // $01787E: RTS
            if(ret()) return returned;continue;
        case 0x17880: // $017880: LEA ($000310B8).L,A2
            lea({L,0,0x310b8},2); pc=0x17886;break;
        case 0x17886: // $017886: BTST #0,41(A5)
            bit(BTST,{I,0,0},{O,5,41}); pc=0x1788c;break;
        case 0x1788c: // $01788C: BNE $017894
            if(branch(NE,0x17894,true)) continue; pc=0x17890;break;
        case 0x17890: // $017890: BT $0178F4
            if(branch(T,0x178f4,true)) continue; pc=0x17894;break;
        case 0x17894: // $017894: MOVE.L (A2),(A3)+
            move(4,{M,2},{P,3},false); pc=0x17896;break;
        case 0x17896: // $017896: CMPI.B #$1D,40(A5)
            alu(CMP,1,{I,0,29},{O,5,40},true,false); pc=0x1789c;break;
        case 0x1789c: // $01789C: BGE $0178C6
            if(branch(GE,0x178c6,true)) continue; pc=0x178a0;break;
        case 0x178a0: // $0178A0: CMPI.B #$16,40(A5)
            alu(CMP,1,{I,0,22},{O,5,40},true,false); pc=0x178a6;break;
        case 0x178a6: // $0178A6: BGE $0178B4
            if(branch(GE,0x178b4,true)) continue; pc=0x178aa;break;
        case 0x178aa: // $0178AA: CMPI.B #$13,40(A5)
            alu(CMP,1,{I,0,19},{O,5,40},true,false); pc=0x178b0;break;
        case 0x178b0: // $0178B0: BGE $0178D8
            if(branch(GE,0x178d8,true)) continue; pc=0x178b4;break;
        case 0x178b4: // $0178B4: NEG.L D6
            negate(false,4,{D,6}); pc=0x178b6;break;
        case 0x178b6: // $0178B6: MOVE.W 0(A3,D6.L),D0
            move(2,{XL,3,0,6},{D,0},false); pc=0x178ba;break;
        case 0x178ba: // $0178BA: ORI.W #$8800,D0
            alu(OR,2,{I,0,34816},{D,0},true,false); pc=0x178be;break;
        case 0x178be: // $0178BE: MOVE.W D0,(A3)
            move(2,{D,0},{M,3},false); pc=0x178c0;break;
        case 0x178c0: // $0178C0: NEG.L D6
            negate(false,4,{D,6}); pc=0x178c2;break;
        case 0x178c2: // $0178C2: BT $0178E6
            if(branch(T,0x178e6,true)) continue; pc=0x178c6;break;
        case 0x178c6: // $0178C6: NEG.L D6
            negate(false,4,{D,6}); pc=0x178c8;break;
        case 0x178c8: // $0178C8: MOVE.W 0(A3,D6.L),D0
            move(2,{XL,3,0,6},{D,0},false); pc=0x178cc;break;
        case 0x178cc: // $0178CC: ORI.W #$8E00,D0
            alu(OR,2,{I,0,36352},{D,0},true,false); pc=0x178d0;break;
        case 0x178d0: // $0178D0: MOVE.W D0,(A3)
            move(2,{D,0},{M,3},false); pc=0x178d2;break;
        case 0x178d2: // $0178D2: NEG.L D6
            negate(false,4,{D,6}); pc=0x178d4;break;
        case 0x178d4: // $0178D4: BT $0178E6
            if(branch(T,0x178e6,true)) continue; pc=0x178d8;break;
        case 0x178d8: // $0178D8: LEA ($000310CC).L,A2
            lea({L,0,0x310cc},2); pc=0x178de;break;
        case 0x178de: // $0178DE: MOVE.W (A2),D0
            move(2,{M,2},{D,0},false); pc=0x178e0;break;
        case 0x178e0: // $0178E0: ORI.W #$8E00,D0
            alu(OR,2,{I,0,36352},{D,0},true,false); pc=0x178e4;break;
        case 0x178e4: // $0178E4: MOVE.W D0,(A3)
            move(2,{D,0},{M,3},false); pc=0x178e6;break;
        case 0x178e6: // $0178E6: BCLR #0,41(A5)
            bit(BCLR,{I,0,0},{O,5,41}); pc=0x178ec;break;
        case 0x178ec: // $0178EC: SUBQ.B #1,40(A5)
            alu(SUB,1,{I,0,1},{O,5,40},false,true); pc=0x178f0;break;
        case 0x178f0: // $0178F0: BT $017902
            if(branch(T,0x17902,true)) continue; pc=0x178f4;break;
        case 0x178f4: // $0178F4: MOVE.L 4(A2),(A3)+
            move(4,{O,2,4},{P,3},false); pc=0x178f8;break;
        case 0x178f8: // $0178F8: MOVE.W 8(A2),(A3)
            move(2,{O,2,8},{M,3},false); pc=0x178fc;break;
        case 0x178fc: // $0178FC: BSET #0,41(A5)
            bit(BSET,{I,0,0},{O,5,41}); pc=0x17902;break;
        case 0x17902: // $017902: RTS
            if(ret()) return returned;continue;
        case 0x17904: // $017904: LEA ($000310C2).L,A2
            lea({L,0,0x310c2},2); pc=0x1790a;break;
        case 0x1790a: // $01790A: BTST #0,41(A5)
            bit(BTST,{I,0,0},{O,5,41}); pc=0x17910;break;
        case 0x17910: // $017910: BNE $017918
            if(branch(NE,0x17918,true)) continue; pc=0x17914;break;
        case 0x17914: // $017914: BT $0179D0
            if(branch(T,0x179d0,true)) continue; pc=0x17918;break;
        case 0x17918: // $017918: MOVE.L (A2),(A3)
            move(4,{M,2},{M,3},false); pc=0x1791a;break;
        case 0x1791a: // $01791A: LEA ($000C1452).L,A0
            lea({L,0,0xc1452},0); pc=0x17920;break;
        case 0x17920: // $017920: MOVE.B 22(A5),D0
            move(1,{O,5,22},{D,0},false); pc=0x17924;break;
        case 0x17924: // $017924: ANDI.W #$FF,D0
            alu(AND,2,{I,0,255},{D,0},true,false); pc=0x17928;break;
        case 0x17928: // $017928: MULU.W #$E,D0
            mulu({I,0,14},0); pc=0x1792c;break;
        case 0x1792c: // $01792C: LEA 114(A5,D0.W),A1
            lea({X,5,114,0},1); pc=0x17930;break;
        case 0x17930: // $017930: MOVE.B 40(A5),D0
            move(1,{O,5,40},{D,0},false); pc=0x17934;break;
        case 0x17934: // $017934: CMPI.B #$16,D0
            alu(CMP,1,{I,0,22},{D,0},true,false); pc=0x17938;break;
        case 0x17938: // $017938: BEQ $017988
            if(branch(EQ,0x17988,true)) continue; pc=0x1793c;break;
        case 0x1793c: // $01793C: CMPI.B #$17,D0
            alu(CMP,1,{I,0,23},{D,0},true,false); pc=0x17940;break;
        case 0x17940: // $017940: BEQ $017994
            if(branch(EQ,0x17994,true)) continue; pc=0x17944;break;
        case 0x17944: // $017944: CMPI.B #$18,D0
            alu(CMP,1,{I,0,24},{D,0},true,false); pc=0x17948;break;
        case 0x17948: // $017948: BEQ $0179A2
            if(branch(EQ,0x179a2,true)) continue; pc=0x1794c;break;
        case 0x1794c: // $01794C: CMPI.B #$19,D0
            alu(CMP,1,{I,0,25},{D,0},true,false); pc=0x17950;break;
        case 0x17950: // $017950: BEQ $0179B0
            if(branch(EQ,0x179b0,true)) continue; pc=0x17954;break;
        case 0x17954: // $017954: CMPI.B #$21,D0
            alu(CMP,1,{I,0,33},{D,0},true,false); pc=0x17958;break;
        case 0x17958: // $017958: BGE $017970
            if(branch(GE,0x17970,true)) continue; pc=0x1795c;break;
        case 0x1795c: // $01795C: NEG.L D6
            negate(false,4,{D,6}); pc=0x1795e;break;
        case 0x1795e: // $01795E: MOVE.W -2(A3,D6.L),D0
            move(2,{XL,3,-2,6},{D,0},false); pc=0x17962;break;
        case 0x17962: // $017962: ORI.W #$8000,D0
            alu(OR,2,{I,0,32768},{D,0},true,false); pc=0x17966;break;
        case 0x17966: // $017966: MOVE.W D0,-2(A3)
            move(2,{D,0},{O,3,-2},false); pc=0x1796a;break;
        case 0x1796a: // $01796A: NEG.L D6
            negate(false,4,{D,6}); pc=0x1796c;break;
        case 0x1796c: // $01796C: BT $0179C6
            if(branch(T,0x179c6,true)) continue; pc=0x17970;break;
        case 0x17970: // $017970: NEG.L D6
            negate(false,4,{D,6}); pc=0x17972;break;
        case 0x17972: // $017972: MOVE.W -2(A3,D6.L),D0
            move(2,{XL,3,-2,6},{D,0},false); pc=0x17976;break;
        case 0x17976: // $017976: ANDI.W #$1FF,D0
            alu(AND,2,{I,0,511},{D,0},true,false); pc=0x1797a;break;
        case 0x1797a: // $01797A: ORI.W #$8E00,D0
            alu(OR,2,{I,0,36352},{D,0},true,false); pc=0x1797e;break;
        case 0x1797e: // $01797E: MOVE.W D0,-2(A3)
            move(2,{D,0},{O,3,-2},false); pc=0x17982;break;
        case 0x17982: // $017982: NEG.L D6
            negate(false,4,{D,6}); pc=0x17984;break;
        case 0x17984: // $017984: BT $0179C6
            if(branch(T,0x179c6,true)) continue; pc=0x17988;break;
        case 0x17988: // $017988: CLR.W D0
            unary(true,2,{D,0}); pc=0x1798a;break;
        case 0x1798a: // $01798A: MOVE.B (A1),D0
            move(1,{M,1},{D,0},false); pc=0x1798c;break;
        case 0x1798c: // $01798C: ORI.W #$8200,D0
            alu(OR,2,{I,0,33280},{D,0},true,false); pc=0x17990;break;
        case 0x17990: // $017990: BT $0179BA
            if(branch(T,0x179ba,true)) continue; pc=0x17994;break;
        case 0x17994: // $017994: CLR.W D0
            unary(true,2,{D,0}); pc=0x17996;break;
        case 0x17996: // $017996: MOVE.B 1(A1),D0
            move(1,{O,1,1},{D,0},false); pc=0x1799a;break;
        case 0x1799a: // $01799A: ORI.W #$8200,D0
            alu(OR,2,{I,0,33280},{D,0},true,false); pc=0x1799e;break;
        case 0x1799e: // $01799E: BT $0179BA
            if(branch(T,0x179ba,true)) continue; pc=0x179a2;break;
        case 0x179a2: // $0179A2: CLR.W D0
            unary(true,2,{D,0}); pc=0x179a4;break;
        case 0x179a4: // $0179A4: MOVE.B 2(A1),D0
            move(1,{O,1,2},{D,0},false); pc=0x179a8;break;
        case 0x179a8: // $0179A8: ORI.W #$8200,D0
            alu(OR,2,{I,0,33280},{D,0},true,false); pc=0x179ac;break;
        case 0x179ac: // $0179AC: BT $0179BA
            if(branch(T,0x179ba,true)) continue; pc=0x179b0;break;
        case 0x179b0: // $0179B0: CLR.W D0
            unary(true,2,{D,0}); pc=0x179b2;break;
        case 0x179b2: // $0179B2: MOVE.B 3(A1),D0
            move(1,{O,1,3},{D,0},false); pc=0x179b6;break;
        case 0x179b6: // $0179B6: ORI.W #$8200,D0
            alu(OR,2,{I,0,33280},{D,0},true,false); pc=0x179ba;break;
        case 0x179ba: // $0179BA: MOVE.W D0,-2(A3)
            move(2,{D,0},{O,3,-2},false); pc=0x179be;break;
        case 0x179be: // $0179BE: BCLR #0,41(A5)
            bit(BCLR,{I,0,0},{O,5,41}); pc=0x179c4;break;
        case 0x179c4: // $0179C4: RTS
            if(ret()) return returned;continue;
        case 0x179c6: // $0179C6: BCLR #0,41(A5)
            bit(BCLR,{I,0,0},{O,5,41}); pc=0x179cc;break;
        case 0x179cc: // $0179CC: BT $0179E2
            if(branch(T,0x179e2,true)) continue; pc=0x179d0;break;
        case 0x179d0: // $0179D0: MOVE.L 4(A2),(A3)+
            move(4,{O,2,4},{P,3},false); pc=0x179d4;break;
        case 0x179d4: // $0179D4: MOVE.W 8(A2),(A3)
            move(2,{O,2,8},{M,3},false); pc=0x179d8;break;
        case 0x179d8: // $0179D8: BSET #0,41(A5)
            bit(BSET,{I,0,0},{O,5,41}); pc=0x179de;break;
        case 0x179de: // $0179DE: ADDQ.B #1,40(A5)
            alu(ADD,1,{I,0,1},{O,5,40},false,true); pc=0x179e2;break;
        case 0x179e2: // $0179E2: RTS
            if(ret()) return returned;continue;
        case 0x179e4: // $0179E4: LEA ($000D0BBC).L,A4
            lea({L,0,0xd0bbc},4); pc=0x179ea;break;
        case 0x179ea: // $0179EA: MOVE.W 24(A5),D0
            move(2,{O,5,24},{D,0},false); pc=0x179ee;break;
        case 0x179ee: // $0179EE: ADD.W D0,D0
            alu(ADD,2,{D,0},{D,0},false,false); pc=0x179f0;break;
        case 0x179f0: // $0179F0: LEA 0(A4,D0.W),A4
            lea({X,4,0,0},4); pc=0x179f4;break;
        case 0x179f4: // $0179F4: MOVE.W #$8800,D1
            move(2,{I,0,34816},{D,1},false); pc=0x179f8;break;
        case 0x179f8: // $0179F8: ANDI.B #$1,0(A4)
            alu(AND,1,{I,0,1},{O,4,0},true,false); pc=0x179fe;break;
        case 0x179fe: // $0179FE: OR.W D1,0(A4)
            alu(OR,2,{D,1},{O,4,0},false,false); pc=0x17a02;break;
        case 0x17a02: // $017A02: ANDI.B #$1,128(A4)
            alu(AND,1,{I,0,1},{O,4,128},true,false); pc=0x17a08;break;
        case 0x17a08: // $017A08: OR.W D1,128(A4)
            alu(OR,2,{D,1},{O,4,128},false,false); pc=0x17a0c;break;
        case 0x17a0c: // $017A0C: CMPI.W #$38,D0
            alu(CMP,2,{I,0,56},{D,0},true,false); pc=0x17a10;break;
        case 0x17a10: // $017A10: BGE $017A26
            if(branch(GE,0x17a26,true)) continue; pc=0x17a14;break;
        case 0x17a14: // $017A14: CMPI.W #$0,D0
            alu(CMP,2,{I,0,0},{D,0},true,false); pc=0x17a18;break;
        case 0x17a18: // $017A18: BEQ $017A34
            if(branch(EQ,0x17a34,true)) continue; pc=0x17a1c;break;
        case 0x17a1c: // $017A1C: BSR $017A42
            call({L,0,0x017a42},0x17a20,true);continue;
        case 0x17a20: // $017A20: BSR $017A5C
            call({L,0,0x017a5c},0x17a24,true);continue;
        case 0x17a24: // $017A24: RTS
            if(ret()) return returned;continue;
        case 0x17a26: // $017A26: BSR $017A5C
            call({L,0,0x017a5c},0x17a2a,true);continue;
        case 0x17a2a: // $017A2A: LEA -58(A4),A4
            lea({O,4,-58},4); pc=0x17a2e;break;
        case 0x17a2e: // $017A2E: BSR $017A42
            call({L,0,0x017a42},0x17a32,true);continue;
        case 0x17a32: // $017A32: RTS
            if(ret()) return returned;continue;
        case 0x17a34: // $017A34: BSR $017A42
            call({L,0,0x017a42},0x17a38,true);continue;
        case 0x17a38: // $017A38: LEA 58(A4),A4
            lea({O,4,58},4); pc=0x17a3c;break;
        case 0x17a3c: // $017A3C: BSR $017A5C
            call({L,0,0x017a5c},0x17a40,true);continue;
        case 0x17a40: // $017A40: RTS
            if(ret()) return returned;continue;
        case 0x17a42: // $017A42: MOVE.W #$8A00,D1
            move(2,{I,0,35328},{D,1},false); pc=0x17a46;break;
        case 0x17a46: // $017A46: ANDI.B #$1,2(A4)
            alu(AND,1,{I,0,1},{O,4,2},true,false); pc=0x17a4c;break;
        case 0x17a4c: // $017A4C: OR.W D1,2(A4)
            alu(OR,2,{D,1},{O,4,2},false,false); pc=0x17a50;break;
        case 0x17a50: // $017A50: ANDI.B #$1,130(A4)
            alu(AND,1,{I,0,1},{O,4,130},true,false); pc=0x17a56;break;
        case 0x17a56: // $017A56: OR.W D1,130(A4)
            alu(OR,2,{D,1},{O,4,130},false,false); pc=0x17a5a;break;
        case 0x17a5a: // $017A5A: RTS
            if(ret()) return returned;continue;
        case 0x17a5c: // $017A5C: MOVE.W #$8A00,D1
            move(2,{I,0,35328},{D,1},false); pc=0x17a60;break;
        case 0x17a60: // $017A60: ANDI.B #$1,-2(A4)
            alu(AND,1,{I,0,1},{O,4,-2},true,false); pc=0x17a66;break;
        case 0x17a66: // $017A66: OR.W D1,-2(A4)
            alu(OR,2,{D,1},{O,4,-2},false,false); pc=0x17a6a;break;
        case 0x17a6a: // $017A6A: ANDI.B #$1,126(A4)
            alu(AND,1,{I,0,1},{O,4,126},true,false); pc=0x17a70;break;
        case 0x17a70: // $017A70: OR.W D1,126(A4)
            alu(OR,2,{D,1},{O,4,126},false,false); pc=0x17a74;break;
        case 0x17a74: // $017A74: RTS
            if(ret()) return returned;continue;
        case 0x17a76: // $017A76: CLR.W D1
            unary(true,2,{D,1}); pc=0x17a78;break;
        case 0x17a78: // $017A78: ADDQ.B #1,34(A5)
            alu(ADD,1,{I,0,1},{O,5,34},false,true); pc=0x17a7c;break;
        case 0x17a7c: // $017A7C: MOVE.W #$20,D0
            move(2,{I,0,32},{D,0},false); pc=0x17a80;break;
        case 0x17a80: // $017A80: BTST #3,34(A5)
            bit(BTST,{I,0,3},{O,5,34}); pc=0x17a86;break;
        case 0x17a86: // $017A86: BEQ $017A98
            if(branch(EQ,0x17a98,false)) continue; pc=0x17a88;break;
        case 0x17a88: // $017A88: MOVE.W 24(A5),D0
            move(2,{O,5,24},{D,0},false); pc=0x17a8c;break;
        case 0x17a8c: // $017A8C: MOVE.B 0(A2,D0.W),D0
            move(1,{X,2,0,0},{D,0},false); pc=0x17a90;break;
        case 0x17a90: // $017A90: ANDI.W #$FF,D0
            alu(AND,2,{I,0,255},{D,0},true,false); pc=0x17a94;break;
        case 0x17a94: // $017A94: ORI.W #$8800,D0
            alu(OR,2,{I,0,34816},{D,0},true,false); pc=0x17a98;break;
        case 0x17a98: // $017A98: MOVE.B 23(A5),D1
            move(1,{O,5,23},{D,1},false); pc=0x17a9c;break;
        case 0x17a9c: // $017A9C: ADD.W D1,D1
            alu(ADD,2,{D,1},{D,1},false,false); pc=0x17a9e;break;
        case 0x17a9e: // $017A9E: MOVEA.L 42(A5),A1
            move(4,{O,5,42},{A,1},true); pc=0x17aa2;break;
        case 0x17aa2: // $017AA2: MOVE.W D0,0(A1,D1.W)
            move(2,{D,0},{X,1,0,1},false); pc=0x17aa6;break;
        case 0x17aa6: // $017AA6: RTS
            if(ret()) return returned;continue;
        case 0x17aa8: // $017AA8: MOVEM.L A2/A1/A0,-(A7)
            movem_mask(true,0x700); pc=0x17aac;break;
        case 0x17aac: // $017AAC: LEA ($000D01EC).L,A0
            lea({L,0,0xd01ec},0); pc=0x17ab2;break;
        case 0x17ab2: // $017AB2: MOVE.W #$8080,D2
            move(2,{I,0,32896},{D,2},false); pc=0x17ab6;break;
        case 0x17ab6: // $017AB6: BSR $018116
            call({L,0,0x018116},0x17aba,true);continue;
        case 0x17aba: // $017ABA: MOVEM.L (A7)+,A0/A1/A2
            movem_mask(false,0x700); pc=0x17abe;break;
        case 0x17abe: // $017ABE: RTS
            if(ret()) return returned;continue;
        case 0x17ac0: // $017AC0: LEA ($0003107C).L,A2
            lea({L,0,0x3107c},2); pc=0x17ac6;break;
        case 0x17ac6: // $017AC6: MOVEQ #0,D0
            moveq({I,0,0},0); pc=0x17ac8;break;
        case 0x17ac8: // $017AC8: MOVE.B 22(A5),D0
            move(1,{O,5,22},{D,0},false); pc=0x17acc;break;
        case 0x17acc: // $017ACC: MULU.W #$E,D0
            mulu({I,0,14},0); pc=0x17ad0;break;
        case 0x17ad0: // $017AD0: LEA 110(A5,D0.W),A0
            lea({X,5,110,0},0); pc=0x17ad4;break;
        case 0x17ad4: // $017AD4: LEA 4(A0),A0
            lea({O,0,4},0); pc=0x17ad8;break;
        case 0x17ad8: // $017AD8: MOVEA.L 42(A5),A1
            move(4,{O,5,42},{A,1},true); pc=0x17adc;break;
        case 0x17adc: // $017ADC: BSR $017BC2
            call({L,0,0x017bc2},0x17ae0,true);continue;
        case 0x17ae0: // $017AE0: MOVE.W #$1C,D1
            move(2,{I,0,28},{D,1},false); pc=0x17ae4;break;
        case 0x17ae4: // $017AE4: ADD.W 24(A5),D0
            alu(ADD,2,{O,5,24},{D,0},false,false); pc=0x17ae8;break;
        case 0x17ae8: // $017AE8: BPL $017AF2
            if(branch(PL,0x17af2,false)) continue; pc=0x17aea;break;
        case 0x17aea: // $017AEA: MOVE.W D1,24(A5)
            move(2,{D,1},{O,5,24},false); pc=0x17aee;break;
        case 0x17aee: // $017AEE: BT $017AFC
            if(branch(T,0x17afc,true)) continue; pc=0x17af2;break;
        case 0x17af2: // $017AF2: CMP.W D0,D1
            alu(CMP,2,{D,0},{D,1},false,false); pc=0x17af4;break;
        case 0x17af4: // $017AF4: BCC $017AF8
            if(branch(CC,0x17af8,false)) continue; pc=0x17af6;break;
        case 0x17af6: // $017AF6: CLR.W D0
            unary(true,2,{D,0}); pc=0x17af8;break;
        case 0x17af8: // $017AF8: MOVE.W D0,24(A5)
            move(2,{D,0},{O,5,24},false); pc=0x17afc;break;
        case 0x17afc: // $017AFC: BTST #4,($0029C075).L
            bit(BTST,{I,0,4},{L,0,0x29c075}); pc=0x17b04;break;
        case 0x17b04: // $017B04: BNE $017BAC
            if(branch(NE,0x17bac,true)) continue; pc=0x17b08;break;
        case 0x17b08: // $017B08: BTST #4,($0029C074).L
            bit(BTST,{I,0,4},{L,0,0x29c074}); pc=0x17b10;break;
        case 0x17b10: // $017B10: BEQ $017BAC
            if(branch(EQ,0x17bac,true)) continue; pc=0x17b14;break;
        case 0x17b14: // $017B14: MOVE.L A0,-(A7)
            move(4,{A,0},{N,7},false); pc=0x17b16;break;
        case 0x17b16: // $017B16: MOVE.W #$F6,D0
            move(2,{I,0,246},{D,0},false); pc=0x17b1a;break;
        case 0x17b1a: // $017B1A: JSR ($0000B84C).L
            call({L,0,0xb84c},0x17b20,false);continue;
        case 0x17b20: // $017B20: MOVEA.L (A7)+,A0
            move(4,{P,7},{A,0},true); pc=0x17b22;break;
        case 0x17b22: // $017B22: MOVE.W 24(A5),D1
            move(2,{O,5,24},{D,1},false); pc=0x17b26;break;
        case 0x17b26: // $017B26: CMPI.W #$1C,D1
            alu(CMP,2,{I,0,28},{D,1},true,false); pc=0x17b2a;break;
        case 0x17b2a: // $017B2A: BNE $017B48
            if(branch(NE,0x17b48,false)) continue; pc=0x17b2c;break;
        case 0x17b2c: // $017B2C: CLR.W D0
            unary(true,2,{D,0}); pc=0x17b2e;break;
        case 0x17b2e: // $017B2E: MOVE.B 23(A5),D0
            move(1,{O,5,23},{D,0},false); pc=0x17b32;break;
        case 0x17b32: // $017B32: ADD.W D0,D0
            alu(ADD,2,{D,0},{D,0},false,false); pc=0x17b34;break;
        case 0x17b34: // $017B34: MOVE.W #$20,0(A1,D0.W)
            move(2,{I,0,32},{X,1,0,0},false); pc=0x17b3a;break;
        case 0x17b3a: // $017B3A: MOVE.B #$5,20(A5)
            move(1,{I,0,5},{O,5,20},false); pc=0x17b40;break;
        case 0x17b40: // $017B40: MOVE.W #$14,($8860).W
            move(2,{I,0,20},{W,0,0xffff8860},false); pc=0x17b46;break;
        case 0x17b46: // $017B46: RTS
            if(ret()) return returned;continue;
        case 0x17b48: // $017B48: CMPI.W #$1B,D1
            alu(CMP,2,{I,0,27},{D,1},true,false); pc=0x17b4c;break;
        case 0x17b4c: // $017B4C: BNE $017B74
            if(branch(NE,0x17b74,false)) continue; pc=0x17b4e;break;
        case 0x17b4e: // $017B4E: TST.B 23(A5)
            unary(false,1,{O,5,23}); pc=0x17b52;break;
        case 0x17b52: // $017B52: BEQ $017BAC
            if(branch(EQ,0x17bac,true)) continue; pc=0x17b56;break;
        case 0x17b56: // $017B56: CLR.W D0
            unary(true,2,{D,0}); pc=0x17b58;break;
        case 0x17b58: // $017B58: MOVE.B 23(A5),D0
            move(1,{O,5,23},{D,0},false); pc=0x17b5c;break;
        case 0x17b5c: // $017B5C: MOVE.W D0,D1
            move(2,{D,0},{D,1},false); pc=0x17b5e;break;
        case 0x17b5e: // $017B5E: ADD.W D1,D1
            alu(ADD,2,{D,1},{D,1},false,false); pc=0x17b60;break;
        case 0x17b60: // $017B60: MOVE.W #$20,0(A1,D1.W)
            move(2,{I,0,32},{X,1,0,1},false); pc=0x17b66;break;
        case 0x17b66: // $017B66: MOVE.B #$20,0(A0,D0.W)
            move(1,{I,0,32},{X,0,0,0},false); pc=0x17b6c;break;
        case 0x17b6c: // $017B6C: SUBQ.B #1,23(A5)
            alu(SUB,1,{I,0,1},{O,5,23},false,true); pc=0x17b70;break;
        case 0x17b70: // $017B70: BT $017BAC
            if(branch(T,0x17bac,true)) continue; pc=0x17b74;break;
        case 0x17b74: // $017B74: MOVE.W 24(A5),D1
            move(2,{O,5,24},{D,1},false); pc=0x17b78;break;
        case 0x17b78: // $017B78: MOVE.B 23(A5),D0
            move(1,{O,5,23},{D,0},false); pc=0x17b7c;break;
        case 0x17b7c: // $017B7C: CLR.W D2
            unary(true,2,{D,2}); pc=0x17b7e;break;
        case 0x17b7e: // $017B7E: MOVE.B 0(A2,D1.W),D2
            move(1,{X,2,0,1},{D,2},false); pc=0x17b82;break;
        case 0x17b82: // $017B82: MOVE.B D2,0(A0,D0.W)
            move(1,{D,2},{X,0,0,0},false); pc=0x17b86;break;
        case 0x17b86: // $017B86: ANDI.W #$FF,D2
            alu(AND,2,{I,0,255},{D,2},true,false); pc=0x17b8a;break;
        case 0x17b8a: // $017B8A: ORI.W #$8A00,D2
            alu(OR,2,{I,0,35328},{D,2},true,false); pc=0x17b8e;break;
        case 0x17b8e: // $017B8E: ADD.W D0,D0
            alu(ADD,2,{D,0},{D,0},false,false); pc=0x17b90;break;
        case 0x17b90: // $017B90: MOVE.W D2,0(A1,D0.W)
            move(2,{D,2},{X,1,0,0},false); pc=0x17b94;break;
        case 0x17b94: // $017B94: ADDQ.B #1,23(A5)
            alu(ADD,1,{I,0,1},{O,5,23},false,true); pc=0x17b98;break;
        case 0x17b98: // $017B98: CMPI.B #$3,23(A5)
            alu(CMP,1,{I,0,3},{O,5,23},true,false); pc=0x17b9e;break;
        case 0x17b9e: // $017B9E: BLT $017BAC
            if(branch(LT,0x17bac,false)) continue; pc=0x17ba0;break;
        case 0x17ba0: // $017BA0: MOVE.B #$5,20(A5)
            move(1,{I,0,5},{O,5,20},false); pc=0x17ba6;break;
        case 0x17ba6: // $017BA6: MOVE.W #$14,($8860).W
            move(2,{I,0,20},{W,0,0xffff8860},false); pc=0x17bac;break;
        case 0x17bac: // $017BAC: MOVE.W ($8860).W,D0
            move(2,{W,0,0xffff8860},{D,0},false); pc=0x17bb0;break;
        case 0x17bb0: // $017BB0: BGT $017BC0
            if(branch(GT,0x17bc0,true)) continue; pc=0x17bb4;break;
        case 0x17bb4: // $017BB4: MOVE.W #$14,($8860).W
            move(2,{I,0,20},{W,0,0xffff8860},false); pc=0x17bba;break;
        case 0x17bba: // $017BBA: MOVE.B #$5,20(A5)
            move(1,{I,0,5},{O,5,20},false); pc=0x17bc0;break;
        case 0x17bc0: // $017BC0: RTS
            if(ret()) return returned;continue;
        case 0x17bc2: // $017BC2: MOVE.B ($8892).W,D1
            move(1,{W,0,0xffff8892},{D,1},false); pc=0x17bc6;break;
        case 0x17bc6: // $017BC6: ANDI.W #$FF,D1
            alu(AND,2,{I,0,255},{D,1},true,false); pc=0x17bca;break;
        case 0x17bca: // $017BCA: MOVEQ #1,D0
            moveq({I,0,1},0); pc=0x17bcc;break;
        case 0x17bcc: // $017BCC: SUBI.W #$80,D1
            alu(SUB,2,{I,0,128},{D,1},true,false); pc=0x17bd0;break;
        case 0x17bd0: // $017BD0: BPL $017BD8
            if(branch(PL,0x17bd8,false)) continue; pc=0x17bd2;break;
        case 0x17bd2: // $017BD2: NEG.W D1
            negate(false,2,{D,1}); pc=0x17bd4;break;
        case 0x17bd4: // $017BD4: MOVE.W #$FFFF,D0
            move(2,{I,0,65535},{D,0},false); pc=0x17bd8;break;
        case 0x17bd8: // $017BD8: MOVEQ #0,D2
            moveq({I,0,0},2); pc=0x17bda;break;
        case 0x17bda: // $017BDA: CMPI.W #$10,D1
            alu(CMP,2,{I,0,16},{D,1},true,false); pc=0x17bde;break;
        case 0x17bde: // $017BDE: BCS $017BEA
            if(branch(CS,0x17bea,false)) continue; pc=0x17be0;break;
        case 0x17be0: // $017BE0: MOVEQ #1,D2
            moveq({I,0,1},2); pc=0x17be2;break;
        case 0x17be2: // $017BE2: CMPI.W #$30,D1
            alu(CMP,2,{I,0,48},{D,1},true,false); pc=0x17be6;break;
        case 0x17be6: // $017BE6: BCS $017BEA
            if(branch(CS,0x17bea,false)) continue; pc=0x17be8;break;
        case 0x17be8: // $017BE8: MOVEQ #5,D2
            moveq({I,0,5},2); pc=0x17bea;break;
        case 0x17bea: // $017BEA: ADD.W D2,30(A5)
            alu(ADD,2,{D,2},{O,5,30},false,false); pc=0x17bee;break;
        case 0x17bee: // $017BEE: CMPI.W #$3C,30(A5)
            alu(CMP,2,{I,0,60},{O,5,30},true,false); pc=0x17bf4;break;
        case 0x17bf4: // $017BF4: BCS $017BFC
            if(branch(CS,0x17bfc,false)) continue; pc=0x17bf6;break;
        case 0x17bf6: // $017BF6: CLR.W 30(A5)
            unary(true,2,{O,5,30}); pc=0x17bfa;break;
        case 0x17bfa: // $017BFA: BT $017BFE
            if(branch(T,0x17bfe,false)) continue; pc=0x17bfc;break;
        case 0x17bfc: // $017BFC: MOVEQ #0,D0
            moveq({I,0,0},0); pc=0x17bfe;break;
        case 0x17bfe: // $017BFE: RTS
            if(ret()) return returned;continue;
        case 0x17c00: // $017C00: ADDQ.B #1,34(A5)
            alu(ADD,1,{I,0,1},{O,5,34},false,true); pc=0x17c04;break;
        case 0x17c04: // $017C04: BTST #4,34(A5)
            bit(BTST,{I,0,4},{O,5,34}); pc=0x17c0a;break;
        case 0x17c0a: // $017C0A: BEQ $017C64
            if(branch(EQ,0x17c64,true)) continue; pc=0x17c0e;break;
        case 0x17c0e: // $017C0E: LEA ($000325CE).L,A0
            lea({L,0,0x325ce},0); pc=0x17c14;break;
        case 0x17c14: // $017C14: BSR $0137E2
            call({L,0,0x0137e2},0x17c18,true);continue;
        case 0x17c18: // $017C18: LEA ($000D0BDC).L,A0
            lea({L,0,0xd0bdc},0); pc=0x17c1e;break;
        case 0x17c1e: // $017C1E: LEA -128(A0),A1
            lea({O,0,-128},1); pc=0x17c22;break;
        case 0x17c22: // $017C22: MOVE.L #$0,(A0)+
            move(4,{I,0,0},{P,0},false); pc=0x17c28;break;
        case 0x17c28: // $017C28: MOVE.L #$0,(A0)+
            move(4,{I,0,0},{P,0},false); pc=0x17c2e;break;
        case 0x17c2e: // $017C2E: MOVE.L #$0,(A0)+
            move(4,{I,0,0},{P,0},false); pc=0x17c34;break;
        case 0x17c34: // $017C34: MOVE.L #$0,(A0)+
            move(4,{I,0,0},{P,0},false); pc=0x17c3a;break;
        case 0x17c3a: // $017C3A: MOVE.W #$0,(A0)+
            move(2,{I,0,0},{P,0},false); pc=0x17c3e;break;
        case 0x17c3e: // $017C3E: MOVE.L #$0,(A1)+
            move(4,{I,0,0},{P,1},false); pc=0x17c44;break;
        case 0x17c44: // $017C44: MOVE.L #$0,(A1)+
            move(4,{I,0,0},{P,1},false); pc=0x17c4a;break;
        case 0x17c4a: // $017C4A: MOVE.L #$0,(A1)+
            move(4,{I,0,0},{P,1},false); pc=0x17c50;break;
        case 0x17c50: // $017C50: MOVE.L #$0,(A1)+
            move(4,{I,0,0},{P,1},false); pc=0x17c56;break;
        case 0x17c56: // $017C56: MOVE.W #$0,(A1)+
            move(2,{I,0,0},{P,1},false); pc=0x17c5a;break;
        case 0x17c5a: // $017C5A: CMPI.W #$2,($8860).W
            alu(CMP,2,{I,0,2},{W,0,0xffff8860},true,false); pc=0x17c60;break;
        case 0x17c60: // $017C60: BGT $017C96
            if(branch(GT,0x17c96,true)) continue; pc=0x17c64;break;
        case 0x17c64: // $017C64: LEA ($000325C0).L,A0
            lea({L,0,0x325c0},0); pc=0x17c6a;break;
        case 0x17c6a: // $017C6A: BSR $0137E2
            call({L,0,0x0137e2},0x17c6e,true);continue;
        case 0x17c6e: // $017C6E: LEA ($000D0BDC).L,A0
            lea({L,0,0xd0bdc},0); pc=0x17c74;break;
        case 0x17c74: // $017C74: LEA 16(A0),A1
            lea({O,0,16},1); pc=0x17c78;break;
        case 0x17c78: // $017C78: LEA -128(A1),A3
            lea({O,1,-128},3); pc=0x17c7c;break;
        case 0x17c7c: // $017C7C: MOVE.L ($8884).W,D0
            move(4,{W,0,0xffff8884},{D,0},false); pc=0x17c80;break;
        case 0x17c80: // $017C80: BEQ $017C8E
            if(branch(EQ,0x17c8e,true)) continue; pc=0x17c84;break;
        case 0x17c84: // $017C84: CLR.W D1
            unary(true,2,{D,1}); pc=0x17c86;break;
        case 0x17c86: // $017C86: MOVE.W #$8100,D1
            move(2,{I,0,33024},{D,1},false); pc=0x17c8a;break;
        case 0x17c8a: // $017C8A: BSR $017EBC
            call({L,0,0x017ebc},0x17c8e,true);continue;
        case 0x17c8e: // $017C8E: MOVE.W #$8161,(A1)
            move(2,{I,0,33121},{M,1},false); pc=0x17c92;break;
        case 0x17c92: // $017C92: MOVE.W #$8160,(A3)
            move(2,{I,0,33120},{M,3},false); pc=0x17c96;break;
        case 0x17c96: // $017C96: CMPI.W #$0,($8860).W
            alu(CMP,2,{I,0,0},{W,0,0xffff8860},true,false); pc=0x17c9c;break;
        case 0x17c9c: // $017C9C: BGT $017CA6
            if(branch(GT,0x17ca6,true)) continue; pc=0x17ca0;break;
        case 0x17ca0: // $017CA0: MOVE.B #$7,20(A5)
            move(1,{I,0,7},{O,5,20},false); pc=0x17ca6;break;
        case 0x17ca6: // $017CA6: RTS
            if(ret()) return returned;continue;
        case 0x17ca8: // $017CA8: LEA ($000D0340).L,A0
            lea({L,0,0xd0340},0); pc=0x17cae;break;
        case 0x17cae: // $017CAE: LEA ($000325A4).L,A1
            lea({L,0,0x325a4},1); pc=0x17cb4;break;
        case 0x17cb4: // $017CB4: CLR.L D0
            unary(true,4,{D,0}); pc=0x17cb6;break;
        case 0x17cb6: // $017CB6: MOVE.B (A1)+,D0
            move(1,{P,1},{D,0},false); pc=0x17cb8;break;
        case 0x17cb8: // $017CB8: CMPI.B #$0,D0
            alu(CMP,1,{I,0,0},{D,0},true,false); pc=0x17cbc;break;
        case 0x17cbc: // $017CBC: BEQ $017CC8
            if(branch(EQ,0x17cc8,true)) continue; pc=0x17cc0;break;
        case 0x17cc0: // $017CC0: ORI.W #$8400,D0
            alu(OR,2,{I,0,33792},{D,0},true,false); pc=0x17cc4;break;
        case 0x17cc4: // $017CC4: MOVE.W D0,(A0)+
            move(2,{D,0},{P,0},false); pc=0x17cc6;break;
        case 0x17cc6: // $017CC6: BT $017CB4
            if(branch(T,0x17cb4,false)) continue; pc=0x17cc8;break;
        case 0x17cc8: // $017CC8: LEA ($000D0148).L,A0
            lea({L,0,0xd0148},0); pc=0x17cce;break;
        case 0x17cce: // $017CCE: LEA 128(A0),A2
            lea({O,0,128},2); pc=0x17cd2;break;
        case 0x17cd2: // $017CD2: LEA 128(A2),A3
            lea({O,2,128},3); pc=0x17cd6;break;
        case 0x17cd6: // $017CD6: LEA ($00030FFA).L,A1
            lea({L,0,0x30ffa},1); pc=0x17cdc;break;
        case 0x17cdc: // $017CDC: CLR.L D0
            unary(true,4,{D,0}); pc=0x17cde;break;
        case 0x17cde: // $017CDE: MOVE.B (A1)+,D0
            move(1,{P,1},{D,0},false); pc=0x17ce0;break;
        case 0x17ce0: // $017CE0: BEQ $017CEC
            if(branch(EQ,0x17cec,true)) continue; pc=0x17ce4;break;
        case 0x17ce4: // $017CE4: ORI.W #$8400,D0
            alu(OR,2,{I,0,33792},{D,0},true,false); pc=0x17ce8;break;
        case 0x17ce8: // $017CE8: MOVE.W D0,(A0)+
            move(2,{D,0},{P,0},false); pc=0x17cea;break;
        case 0x17cea: // $017CEA: BT $017CDC
            if(branch(T,0x17cdc,false)) continue; pc=0x17cec;break;
        case 0x17cec: // $017CEC: CLR.L D0
            unary(true,4,{D,0}); pc=0x17cee;break;
        case 0x17cee: // $017CEE: MOVE.B (A1)+,D0
            move(1,{P,1},{D,0},false); pc=0x17cf0;break;
        case 0x17cf0: // $017CF0: BEQ $017CFC
            if(branch(EQ,0x17cfc,true)) continue; pc=0x17cf4;break;
        case 0x17cf4: // $017CF4: ORI.W #$8400,D0
            alu(OR,2,{I,0,33792},{D,0},true,false); pc=0x17cf8;break;
        case 0x17cf8: // $017CF8: MOVE.W D0,(A2)+
            move(2,{D,0},{P,2},false); pc=0x17cfa;break;
        case 0x17cfa: // $017CFA: BT $017CEC
            if(branch(T,0x17cec,false)) continue; pc=0x17cfc;break;
        case 0x17cfc: // $017CFC: CLR.L D0
            unary(true,4,{D,0}); pc=0x17cfe;break;
        case 0x17cfe: // $017CFE: MOVE.B (A1)+,D0
            move(1,{P,1},{D,0},false); pc=0x17d00;break;
        case 0x17d00: // $017D00: BEQ $017D0C
            if(branch(EQ,0x17d0c,true)) continue; pc=0x17d04;break;
        case 0x17d04: // $017D04: ORI.W #$8400,D0
            alu(OR,2,{I,0,33792},{D,0},true,false); pc=0x17d08;break;
        case 0x17d08: // $017D08: MOVE.W D0,(A3)+
            move(2,{D,0},{P,3},false); pc=0x17d0a;break;
        case 0x17d0a: // $017D0A: BT $017CFC
            if(branch(T,0x17cfc,false)) continue; pc=0x17d0c;break;
        case 0x17d0c: // $017D0C: RTS
            if(ret()) return returned;continue;
        case 0x17d0e: // $017D0E: CLR.W D1
            unary(true,2,{D,1}); pc=0x17d10;break;
        case 0x17d10: // $017D10: ADDQ.B #1,34(A5)
            alu(ADD,1,{I,0,1},{O,5,34},false,true); pc=0x17d14;break;
        case 0x17d14: // $017D14: MOVE.B 60(A5),D1
            move(1,{O,5,60},{D,1},false); pc=0x17d18;break;
        case 0x17d18: // $017D18: BNE $017D26
            if(branch(NE,0x17d26,true)) continue; pc=0x17d1c;break;
        case 0x17d1c: // $017D1C: LEA ($0003104A).L,A1
            lea({L,0,0x3104a},1); pc=0x17d22;break;
        case 0x17d22: // $017D22: BT $017D3A
            if(branch(T,0x17d3a,true)) continue; pc=0x17d26;break;
        case 0x17d26: // $017D26: BPL $017D34
            if(branch(PL,0x17d34,true)) continue; pc=0x17d2a;break;
        case 0x17d2a: // $017D2A: LEA ($00031062).L,A1
            lea({L,0,0x31062},1); pc=0x17d30;break;
        case 0x17d30: // $017D30: BT $017D3A
            if(branch(T,0x17d3a,true)) continue; pc=0x17d34;break;
        case 0x17d34: // $017D34: LEA ($00031070).L,A1
            lea({L,0,0x31070},1); pc=0x17d3a;break;
        case 0x17d3a: // $017D3A: BTST #4,34(A5)
            bit(BTST,{I,0,4},{O,5,34}); pc=0x17d40;break;
        case 0x17d40: // $017D40: BEQ $017D48
            if(branch(EQ,0x17d48,false)) continue; pc=0x17d42;break;
        case 0x17d42: // $017D42: LEA ($00031054).L,A1
            lea({L,0,0x31054},1); pc=0x17d48;break;
        case 0x17d48: // $017D48: MOVEA.L 42(A5),A2
            move(4,{O,5,42},{A,2},true); pc=0x17d4c;break;
        case 0x17d4c: // $017D4C: LEA -132(A2),A2
            lea({O,2,-132},2); pc=0x17d50;break;
        case 0x17d50: // $017D50: MOVE.B (A1)+,D0
            move(1,{P,1},{D,0},false); pc=0x17d52;break;
        case 0x17d52: // $017D52: BEQ $017D62
            if(branch(EQ,0x17d62,true)) continue; pc=0x17d56;break;
        case 0x17d56: // $017D56: ANDI.W #$FF,D0
            alu(AND,2,{I,0,255},{D,0},true,false); pc=0x17d5a;break;
        case 0x17d5a: // $017D5A: ORI.W #$8800,D0
            alu(OR,2,{I,0,34816},{D,0},true,false); pc=0x17d5e;break;
        case 0x17d5e: // $017D5E: MOVE.W D0,(A2)+
            move(2,{D,0},{P,2},false); pc=0x17d60;break;
        case 0x17d60: // $017D60: BT $017D50
            if(branch(T,0x17d50,false)) continue; pc=0x17d62;break;
        case 0x17d62: // $017D62: RTS
            if(ret()) return returned;continue;
        case 0x17d64: // $017D64: MOVEM.L A2/D7/D6/D5/D4/D3/D2,-(A7)
            movem_mask(true,0x4fc); pc=0x17d68;break;
        case 0x17d68: // $017D68: LSL.W #8,D1
            shift(LSL,2,{I,0,8},1); pc=0x17d6a;break;
        case 0x17d6a: // $017D6A: ROR.W #7,D1
            shift(ROR,2,{I,0,7},1); pc=0x17d6c;break;
        case 0x17d6c: // $017D6C: ORI.W #$8000,D1
            alu(OR,2,{I,0,32768},{D,1},true,false); pc=0x17d70;break;
        case 0x17d70: // $017D70: MOVE.W #$30,D6
            move(2,{I,0,48},{D,6},false); pc=0x17d74;break;
        case 0x17d74: // $017D74: OR.W D1,D6
            alu(OR,2,{D,1},{D,6},false,false); pc=0x17d76;break;
        case 0x17d76: // $017D76: MOVE.W D0,D2
            move(2,{D,0},{D,2},false); pc=0x17d78;break;
        case 0x17d78: // $017D78: CLR.L D3
            unary(true,4,{D,3}); pc=0x17d7a;break;
        case 0x17d7a: // $017D7A: MOVE.W D2,D3
            move(2,{D,2},{D,3},false); pc=0x17d7c;break;
        case 0x17d7c: // $017D7C: ANDI.W #$F000,D3
            alu(AND,2,{I,0,61440},{D,3},true,false); pc=0x17d80;break;
        case 0x17d80: // $017D80: ROL.W #4,D3
            shift(ROL,2,{I,0,4},3); pc=0x17d82;break;
        case 0x17d82: // $017D82: OR.W D6,D3
            alu(OR,2,{D,6},{D,3},false,false); pc=0x17d84;break;
        case 0x17d84: // $017D84: SWAP D3
            swap(3); pc=0x17d86;break;
        case 0x17d86: // $017D86: MOVE.W D2,D3
            move(2,{D,2},{D,3},false); pc=0x17d88;break;
        case 0x17d88: // $017D88: ANDI.W #$F00,D3
            alu(AND,2,{I,0,3840},{D,3},true,false); pc=0x17d8c;break;
        case 0x17d8c: // $017D8C: LSR.W #8,D3
            shift(LSR,2,{I,0,8},3); pc=0x17d8e;break;
        case 0x17d8e: // $017D8E: OR.W D6,D3
            alu(OR,2,{D,6},{D,3},false,false); pc=0x17d90;break;
        case 0x17d90: // $017D90: CLR.L D4
            unary(true,4,{D,4}); pc=0x17d92;break;
        case 0x17d92: // $017D92: MOVE.W D2,D4
            move(2,{D,2},{D,4},false); pc=0x17d94;break;
        case 0x17d94: // $017D94: ANDI.W #$F0,D4
            alu(AND,2,{I,0,240},{D,4},true,false); pc=0x17d98;break;
        case 0x17d98: // $017D98: LSR.W #4,D4
            shift(LSR,2,{I,0,4},4); pc=0x17d9a;break;
        case 0x17d9a: // $017D9A: OR.W D6,D4
            alu(OR,2,{D,6},{D,4},false,false); pc=0x17d9c;break;
        case 0x17d9c: // $017D9C: SWAP D4
            swap(4); pc=0x17d9e;break;
        case 0x17d9e: // $017D9E: MOVE.W D2,D4
            move(2,{D,2},{D,4},false); pc=0x17da0;break;
        case 0x17da0: // $017DA0: ANDI.W #$F,D4
            alu(AND,2,{I,0,15},{D,4},true,false); pc=0x17da4;break;
        case 0x17da4: // $017DA4: OR.W D6,D4
            alu(OR,2,{D,6},{D,4},false,false); pc=0x17da6;break;
        case 0x17da6: // $017DA6: MOVE.W #$8020,D2
            move(2,{I,0,32800},{D,2},false); pc=0x17daa;break;
        case 0x17daa: // $017DAA: SWAP D3
            swap(3); pc=0x17dac;break;
        case 0x17dac: // $017DAC: CMP.W D6,D3
            alu(CMP,2,{D,6},{D,3},false,false); pc=0x17dae;break;
        case 0x17dae: // $017DAE: BNE $017DCE
            if(branch(NE,0x17dce,true)) continue; pc=0x17db2;break;
        case 0x17db2: // $017DB2: MOVE.W D2,D3
            move(2,{D,2},{D,3},false); pc=0x17db4;break;
        case 0x17db4: // $017DB4: SWAP D3
            swap(3); pc=0x17db6;break;
        case 0x17db6: // $017DB6: CMP.W D6,D3
            alu(CMP,2,{D,6},{D,3},false,false); pc=0x17db8;break;
        case 0x17db8: // $017DB8: BNE $017DD6
            if(branch(NE,0x17dd6,true)) continue; pc=0x17dbc;break;
        case 0x17dbc: // $017DBC: MOVE.W D2,D3
            move(2,{D,2},{D,3},false); pc=0x17dbe;break;
        case 0x17dbe: // $017DBE: SWAP D4
            swap(4); pc=0x17dc0;break;
        case 0x17dc0: // $017DC0: CMP.W D6,D4
            alu(CMP,2,{D,6},{D,4},false,false); pc=0x17dc2;break;
        case 0x17dc2: // $017DC2: BNE $017DD4
            if(branch(NE,0x17dd4,true)) continue; pc=0x17dc6;break;
        case 0x17dc6: // $017DC6: MOVE.W D2,D4
            move(2,{D,2},{D,4},false); pc=0x17dc8;break;
        case 0x17dc8: // $017DC8: SWAP D4
            swap(4); pc=0x17dca;break;
        case 0x17dca: // $017DCA: BT $017DD6
            if(branch(T,0x17dd6,true)) continue; pc=0x17dce;break;
        case 0x17dce: // $017DCE: SWAP D3
            swap(3); pc=0x17dd0;break;
        case 0x17dd0: // $017DD0: BT $017DD6
            if(branch(T,0x17dd6,true)) continue; pc=0x17dd4;break;
        case 0x17dd4: // $017DD4: SWAP D4
            swap(4); pc=0x17dd6;break;
        case 0x17dd6: // $017DD6: LEA (A0),A2
            lea({M,0},2); pc=0x17dd8;break;
        case 0x17dd8: // $017DD8: MOVE.L D3,(A2)+
            move(4,{D,3},{P,2},false); pc=0x17dda;break;
        case 0x17dda: // $017DDA: MOVE.L D4,(A2)+
            move(4,{D,4},{P,2},false); pc=0x17ddc;break;
        case 0x17ddc: // $017DDC: MOVEM.L (A7)+,D2/D3/D4/D5/D6/D7/A2
            movem_mask(false,0x4fc); pc=0x17de0;break;
        case 0x17de0: // $017DE0: RTS
            if(ret()) return returned;continue;
        case 0x17de2: // $017DE2: MOVEM.L A2/D7/D6/D5/D4/D3/D2,-(A7)
            movem_mask(true,0x4fc); pc=0x17de6;break;
        case 0x17de6: // $017DE6: LSL.W #8,D1
            shift(LSL,2,{I,0,8},1); pc=0x17de8;break;
        case 0x17de8: // $017DE8: ROR.W #7,D1
            shift(ROR,2,{I,0,7},1); pc=0x17dea;break;
        case 0x17dea: // $017DEA: ORI.W #$8000,D1
            alu(OR,2,{I,0,32768},{D,1},true,false); pc=0x17dee;break;
        case 0x17dee: // $017DEE: MOVE.W #$30,D6
            move(2,{I,0,48},{D,6},false); pc=0x17df2;break;
        case 0x17df2: // $017DF2: OR.W D1,D6
            alu(OR,2,{D,1},{D,6},false,false); pc=0x17df4;break;
        case 0x17df4: // $017DF4: MOVE.L D0,D2
            move(4,{D,0},{D,2},false); pc=0x17df6;break;
        case 0x17df6: // $017DF6: SWAP D2
            swap(2); pc=0x17df8;break;
        case 0x17df8: // $017DF8: MOVE.W D2,D3
            move(2,{D,2},{D,3},false); pc=0x17dfa;break;
        case 0x17dfa: // $017DFA: ANDI.W #$F000,D3
            alu(AND,2,{I,0,61440},{D,3},true,false); pc=0x17dfe;break;
        case 0x17dfe: // $017DFE: ROL.W #4,D3
            shift(ROL,2,{I,0,4},3); pc=0x17e00;break;
        case 0x17e00: // $017E00: ADD.W D6,D3
            alu(ADD,2,{D,6},{D,3},false,false); pc=0x17e02;break;
        case 0x17e02: // $017E02: SWAP D3
            swap(3); pc=0x17e04;break;
        case 0x17e04: // $017E04: MOVE.W D2,D3
            move(2,{D,2},{D,3},false); pc=0x17e06;break;
        case 0x17e06: // $017E06: ANDI.W #$F00,D3
            alu(AND,2,{I,0,3840},{D,3},true,false); pc=0x17e0a;break;
        case 0x17e0a: // $017E0A: LSR.W #8,D3
            shift(LSR,2,{I,0,8},3); pc=0x17e0c;break;
        case 0x17e0c: // $017E0C: ADD.W D6,D3
            alu(ADD,2,{D,6},{D,3},false,false); pc=0x17e0e;break;
        case 0x17e0e: // $017E0E: MOVE.W D2,D4
            move(2,{D,2},{D,4},false); pc=0x17e10;break;
        case 0x17e10: // $017E10: ANDI.W #$F0,D4
            alu(AND,2,{I,0,240},{D,4},true,false); pc=0x17e14;break;
        case 0x17e14: // $017E14: LSR.W #4,D4
            shift(LSR,2,{I,0,4},4); pc=0x17e16;break;
        case 0x17e16: // $017E16: ADD.W D6,D4
            alu(ADD,2,{D,6},{D,4},false,false); pc=0x17e18;break;
        case 0x17e18: // $017E18: SWAP D4
            swap(4); pc=0x17e1a;break;
        case 0x17e1a: // $017E1A: MOVE.W D2,D4
            move(2,{D,2},{D,4},false); pc=0x17e1c;break;
        case 0x17e1c: // $017E1C: ANDI.W #$F,D4
            alu(AND,2,{I,0,15},{D,4},true,false); pc=0x17e20;break;
        case 0x17e20: // $017E20: ADD.W D6,D4
            alu(ADD,2,{D,6},{D,4},false,false); pc=0x17e22;break;
        case 0x17e22: // $017E22: SWAP D2
            swap(2); pc=0x17e24;break;
        case 0x17e24: // $017E24: MOVE.W D2,D5
            move(2,{D,2},{D,5},false); pc=0x17e26;break;
        case 0x17e26: // $017E26: ANDI.W #$F000,D5
            alu(AND,2,{I,0,61440},{D,5},true,false); pc=0x17e2a;break;
        case 0x17e2a: // $017E2A: ROL.W #4,D5
            shift(ROL,2,{I,0,4},5); pc=0x17e2c;break;
        case 0x17e2c: // $017E2C: ADD.W D6,D5
            alu(ADD,2,{D,6},{D,5},false,false); pc=0x17e2e;break;
        case 0x17e2e: // $017E2E: SWAP D5
            swap(5); pc=0x17e30;break;
        case 0x17e30: // $017E30: MOVE.W D2,D5
            move(2,{D,2},{D,5},false); pc=0x17e32;break;
        case 0x17e32: // $017E32: ANDI.W #$F00,D5
            alu(AND,2,{I,0,3840},{D,5},true,false); pc=0x17e36;break;
        case 0x17e36: // $017E36: LSR.W #8,D5
            shift(LSR,2,{I,0,8},5); pc=0x17e38;break;
        case 0x17e38: // $017E38: ADD.W D6,D5
            alu(ADD,2,{D,6},{D,5},false,false); pc=0x17e3a;break;
        case 0x17e3a: // $017E3A: MOVE.W D2,D7
            move(2,{D,2},{D,7},false); pc=0x17e3c;break;
        case 0x17e3c: // $017E3C: ANDI.W #$F0,D7
            alu(AND,2,{I,0,240},{D,7},true,false); pc=0x17e40;break;
        case 0x17e40: // $017E40: LSR.W #4,D7
            shift(LSR,2,{I,0,4},7); pc=0x17e42;break;
        case 0x17e42: // $017E42: ADD.W D6,D7
            alu(ADD,2,{D,6},{D,7},false,false); pc=0x17e44;break;
        case 0x17e44: // $017E44: SWAP D7
            swap(7); pc=0x17e46;break;
        case 0x17e46: // $017E46: MOVE.W D2,D7
            move(2,{D,2},{D,7},false); pc=0x17e48;break;
        case 0x17e48: // $017E48: ANDI.W #$F,D7
            alu(AND,2,{I,0,15},{D,7},true,false); pc=0x17e4c;break;
        case 0x17e4c: // $017E4C: ADD.W D6,D7
            alu(ADD,2,{D,6},{D,7},false,false); pc=0x17e4e;break;
        case 0x17e4e: // $017E4E: MOVE.W #$8020,D2
            move(2,{I,0,32800},{D,2},false); pc=0x17e52;break;
        case 0x17e52: // $017E52: SWAP D3
            swap(3); pc=0x17e54;break;
        case 0x17e54: // $017E54: CMP.W D6,D3
            alu(CMP,2,{D,6},{D,3},false,false); pc=0x17e56;break;
        case 0x17e56: // $017E56: BNE $017E9E
            if(branch(NE,0x17e9e,true)) continue; pc=0x17e5a;break;
        case 0x17e5a: // $017E5A: MOVE.W D2,D3
            move(2,{D,2},{D,3},false); pc=0x17e5c;break;
        case 0x17e5c: // $017E5C: SWAP D3
            swap(3); pc=0x17e5e;break;
        case 0x17e5e: // $017E5E: CMP.W D6,D3
            alu(CMP,2,{D,6},{D,3},false,false); pc=0x17e60;break;
        case 0x17e60: // $017E60: BNE $017EAC
            if(branch(NE,0x17eac,true)) continue; pc=0x17e64;break;
        case 0x17e64: // $017E64: MOVE.W D2,D3
            move(2,{D,2},{D,3},false); pc=0x17e66;break;
        case 0x17e66: // $017E66: SWAP D4
            swap(4); pc=0x17e68;break;
        case 0x17e68: // $017E68: CMP.W D6,D4
            alu(CMP,2,{D,6},{D,4},false,false); pc=0x17e6a;break;
        case 0x17e6a: // $017E6A: BNE $017EAA
            if(branch(NE,0x17eaa,true)) continue; pc=0x17e6e;break;
        case 0x17e6e: // $017E6E: MOVE.W D2,D4
            move(2,{D,2},{D,4},false); pc=0x17e70;break;
        case 0x17e70: // $017E70: SWAP D4
            swap(4); pc=0x17e72;break;
        case 0x17e72: // $017E72: CMP.W D6,D4
            alu(CMP,2,{D,6},{D,4},false,false); pc=0x17e74;break;
        case 0x17e74: // $017E74: BNE $017EAC
            if(branch(NE,0x17eac,true)) continue; pc=0x17e78;break;
        case 0x17e78: // $017E78: MOVE.W D2,D4
            move(2,{D,2},{D,4},false); pc=0x17e7a;break;
        case 0x17e7a: // $017E7A: SWAP D5
            swap(5); pc=0x17e7c;break;
        case 0x17e7c: // $017E7C: CMP.W D6,D5
            alu(CMP,2,{D,6},{D,5},false,false); pc=0x17e7e;break;
        case 0x17e7e: // $017E7E: BNE $017EA4
            if(branch(NE,0x17ea4,true)) continue; pc=0x17e82;break;
        case 0x17e82: // $017E82: MOVE.W D2,D5
            move(2,{D,2},{D,5},false); pc=0x17e84;break;
        case 0x17e84: // $017E84: SWAP D5
            swap(5); pc=0x17e86;break;
        case 0x17e86: // $017E86: CMP.W D6,D5
            alu(CMP,2,{D,6},{D,5},false,false); pc=0x17e88;break;
        case 0x17e88: // $017E88: BNE $017EAC
            if(branch(NE,0x17eac,true)) continue; pc=0x17e8c;break;
        case 0x17e8c: // $017E8C: MOVE.W D2,D5
            move(2,{D,2},{D,5},false); pc=0x17e8e;break;
        case 0x17e8e: // $017E8E: SWAP D7
            swap(7); pc=0x17e90;break;
        case 0x17e90: // $017E90: CMP.W D6,D7
            alu(CMP,2,{D,6},{D,7},false,false); pc=0x17e92;break;
        case 0x17e92: // $017E92: BNE $017E98
            if(branch(NE,0x17e98,true)) continue; pc=0x17e96;break;
        case 0x17e96: // $017E96: MOVE.W D2,D7
            move(2,{D,2},{D,7},false); pc=0x17e98;break;
        case 0x17e98: // $017E98: SWAP D7
            swap(7); pc=0x17e9a;break;
        case 0x17e9a: // $017E9A: BT $017EAC
            if(branch(T,0x17eac,true)) continue; pc=0x17e9e;break;
        case 0x17e9e: // $017E9E: SWAP D3
            swap(3); pc=0x17ea0;break;
        case 0x17ea0: // $017EA0: BT $017EAC
            if(branch(T,0x17eac,true)) continue; pc=0x17ea4;break;
        case 0x17ea4: // $017EA4: SWAP D5
            swap(5); pc=0x17ea6;break;
        case 0x17ea6: // $017EA6: BT $017EAC
            if(branch(T,0x17eac,true)) continue; pc=0x17eaa;break;
        case 0x17eaa: // $017EAA: SWAP D4
            swap(4); pc=0x17eac;break;
        case 0x17eac: // $017EAC: LEA (A0),A2
            lea({M,0},2); pc=0x17eae;break;
        case 0x17eae: // $017EAE: MOVE.L D3,(A2)+
            move(4,{D,3},{P,2},false); pc=0x17eb0;break;
        case 0x17eb0: // $017EB0: MOVE.L D4,(A2)+
            move(4,{D,4},{P,2},false); pc=0x17eb2;break;
        case 0x17eb2: // $017EB2: MOVE.L D5,(A2)+
            move(4,{D,5},{P,2},false); pc=0x17eb4;break;
        case 0x17eb4: // $017EB4: MOVE.L D7,(A2)+
            move(4,{D,7},{P,2},false); pc=0x17eb6;break;
        case 0x17eb6: // $017EB6: MOVEM.L (A7)+,D2/D3/D4/D5/D6/D7/A2
            movem_mask(false,0x4fc); pc=0x17eba;break;
        case 0x17eba: // $017EBA: RTS
            if(ret()) return returned;continue;
        case 0x17ebc: // $017EBC: MOVEM.L A2/D7/D6/D5/D4/D3/D2,-(A7)
            movem_mask(true,0x4fc); pc=0x17ec0;break;
        case 0x17ec0: // $017EC0: MOVE.W #$30,D6
            move(2,{I,0,48},{D,6},false); pc=0x17ec4;break;
        case 0x17ec4: // $017EC4: OR.W D1,D6
            alu(OR,2,{D,1},{D,6},false,false); pc=0x17ec6;break;
        case 0x17ec6: // $017EC6: MOVE.L D0,D2
            move(4,{D,0},{D,2},false); pc=0x17ec8;break;
        case 0x17ec8: // $017EC8: SWAP D2
            swap(2); pc=0x17eca;break;
        case 0x17eca: // $017ECA: MOVE.W D2,D3
            move(2,{D,2},{D,3},false); pc=0x17ecc;break;
        case 0x17ecc: // $017ECC: ANDI.W #$F000,D3
            alu(AND,2,{I,0,61440},{D,3},true,false); pc=0x17ed0;break;
        case 0x17ed0: // $017ED0: ROL.W #4,D3
            shift(ROL,2,{I,0,4},3); pc=0x17ed2;break;
        case 0x17ed2: // $017ED2: ADD.W D6,D3
            alu(ADD,2,{D,6},{D,3},false,false); pc=0x17ed4;break;
        case 0x17ed4: // $017ED4: ADD.B D3,D3
            alu(ADD,1,{D,3},{D,3},false,false); pc=0x17ed6;break;
        case 0x17ed6: // $017ED6: SWAP D3
            swap(3); pc=0x17ed8;break;
        case 0x17ed8: // $017ED8: MOVE.W D2,D3
            move(2,{D,2},{D,3},false); pc=0x17eda;break;
        case 0x17eda: // $017EDA: ANDI.W #$F00,D3
            alu(AND,2,{I,0,3840},{D,3},true,false); pc=0x17ede;break;
        case 0x17ede: // $017EDE: LSR.W #8,D3
            shift(LSR,2,{I,0,8},3); pc=0x17ee0;break;
        case 0x17ee0: // $017EE0: ADD.W D6,D3
            alu(ADD,2,{D,6},{D,3},false,false); pc=0x17ee2;break;
        case 0x17ee2: // $017EE2: ADD.B D3,D3
            alu(ADD,1,{D,3},{D,3},false,false); pc=0x17ee4;break;
        case 0x17ee4: // $017EE4: MOVE.W D2,D4
            move(2,{D,2},{D,4},false); pc=0x17ee6;break;
        case 0x17ee6: // $017EE6: ANDI.W #$F0,D4
            alu(AND,2,{I,0,240},{D,4},true,false); pc=0x17eea;break;
        case 0x17eea: // $017EEA: LSR.W #4,D4
            shift(LSR,2,{I,0,4},4); pc=0x17eec;break;
        case 0x17eec: // $017EEC: ADD.W D6,D4
            alu(ADD,2,{D,6},{D,4},false,false); pc=0x17eee;break;
        case 0x17eee: // $017EEE: ADD.B D4,D4
            alu(ADD,1,{D,4},{D,4},false,false); pc=0x17ef0;break;
        case 0x17ef0: // $017EF0: SWAP D4
            swap(4); pc=0x17ef2;break;
        case 0x17ef2: // $017EF2: MOVE.W D2,D4
            move(2,{D,2},{D,4},false); pc=0x17ef4;break;
        case 0x17ef4: // $017EF4: ANDI.W #$F,D4
            alu(AND,2,{I,0,15},{D,4},true,false); pc=0x17ef8;break;
        case 0x17ef8: // $017EF8: ADD.W D6,D4
            alu(ADD,2,{D,6},{D,4},false,false); pc=0x17efa;break;
        case 0x17efa: // $017EFA: ADD.B D4,D4
            alu(ADD,1,{D,4},{D,4},false,false); pc=0x17efc;break;
        case 0x17efc: // $017EFC: SWAP D2
            swap(2); pc=0x17efe;break;
        case 0x17efe: // $017EFE: MOVE.W D2,D5
            move(2,{D,2},{D,5},false); pc=0x17f00;break;
        case 0x17f00: // $017F00: ANDI.W #$F000,D5
            alu(AND,2,{I,0,61440},{D,5},true,false); pc=0x17f04;break;
        case 0x17f04: // $017F04: ROL.W #4,D5
            shift(ROL,2,{I,0,4},5); pc=0x17f06;break;
        case 0x17f06: // $017F06: ADD.W D6,D5
            alu(ADD,2,{D,6},{D,5},false,false); pc=0x17f08;break;
        case 0x17f08: // $017F08: ADD.B D5,D5
            alu(ADD,1,{D,5},{D,5},false,false); pc=0x17f0a;break;
        case 0x17f0a: // $017F0A: SWAP D5
            swap(5); pc=0x17f0c;break;
        case 0x17f0c: // $017F0C: MOVE.W D2,D5
            move(2,{D,2},{D,5},false); pc=0x17f0e;break;
        case 0x17f0e: // $017F0E: ANDI.W #$F00,D5
            alu(AND,2,{I,0,3840},{D,5},true,false); pc=0x17f12;break;
        case 0x17f12: // $017F12: LSR.W #8,D5
            shift(LSR,2,{I,0,8},5); pc=0x17f14;break;
        case 0x17f14: // $017F14: ADD.W D6,D5
            alu(ADD,2,{D,6},{D,5},false,false); pc=0x17f16;break;
        case 0x17f16: // $017F16: ADD.B D5,D5
            alu(ADD,1,{D,5},{D,5},false,false); pc=0x17f18;break;
        case 0x17f18: // $017F18: MOVE.W D2,D7
            move(2,{D,2},{D,7},false); pc=0x17f1a;break;
        case 0x17f1a: // $017F1A: ANDI.W #$F0,D7
            alu(AND,2,{I,0,240},{D,7},true,false); pc=0x17f1e;break;
        case 0x17f1e: // $017F1E: LSR.W #4,D7
            shift(LSR,2,{I,0,4},7); pc=0x17f20;break;
        case 0x17f20: // $017F20: ADD.W D6,D7
            alu(ADD,2,{D,6},{D,7},false,false); pc=0x17f22;break;
        case 0x17f22: // $017F22: ADD.B D7,D7
            alu(ADD,1,{D,7},{D,7},false,false); pc=0x17f24;break;
        case 0x17f24: // $017F24: SWAP D7
            swap(7); pc=0x17f26;break;
        case 0x17f26: // $017F26: MOVE.W D2,D7
            move(2,{D,2},{D,7},false); pc=0x17f28;break;
        case 0x17f28: // $017F28: ANDI.W #$F,D7
            alu(AND,2,{I,0,15},{D,7},true,false); pc=0x17f2c;break;
        case 0x17f2c: // $017F2C: ADD.W D6,D7
            alu(ADD,2,{D,6},{D,7},false,false); pc=0x17f2e;break;
        case 0x17f2e: // $017F2E: ADD.B D7,D7
            alu(ADD,1,{D,7},{D,7},false,false); pc=0x17f30;break;
        case 0x17f30: // $017F30: MOVE.W #$8180,D2
            move(2,{I,0,33152},{D,2},false); pc=0x17f34;break;
        case 0x17f34: // $017F34: MOVE.W #$8160,D1
            move(2,{I,0,33120},{D,1},false); pc=0x17f38;break;
        case 0x17f38: // $017F38: SWAP D3
            swap(3); pc=0x17f3a;break;
        case 0x17f3a: // $017F3A: CMP.W D1,D3
            alu(CMP,2,{D,1},{D,3},false,false); pc=0x17f3c;break;
        case 0x17f3c: // $017F3C: BNE $017F84
            if(branch(NE,0x17f84,true)) continue; pc=0x17f40;break;
        case 0x17f40: // $017F40: MOVE.W D2,D3
            move(2,{D,2},{D,3},false); pc=0x17f42;break;
        case 0x17f42: // $017F42: SWAP D3
            swap(3); pc=0x17f44;break;
        case 0x17f44: // $017F44: CMP.W D1,D3
            alu(CMP,2,{D,1},{D,3},false,false); pc=0x17f46;break;
        case 0x17f46: // $017F46: BNE $017F92
            if(branch(NE,0x17f92,true)) continue; pc=0x17f4a;break;
        case 0x17f4a: // $017F4A: MOVE.W D2,D3
            move(2,{D,2},{D,3},false); pc=0x17f4c;break;
        case 0x17f4c: // $017F4C: SWAP D4
            swap(4); pc=0x17f4e;break;
        case 0x17f4e: // $017F4E: CMP.W D1,D4
            alu(CMP,2,{D,1},{D,4},false,false); pc=0x17f50;break;
        case 0x17f50: // $017F50: BNE $017F90
            if(branch(NE,0x17f90,true)) continue; pc=0x17f54;break;
        case 0x17f54: // $017F54: MOVE.W D2,D4
            move(2,{D,2},{D,4},false); pc=0x17f56;break;
        case 0x17f56: // $017F56: SWAP D4
            swap(4); pc=0x17f58;break;
        case 0x17f58: // $017F58: CMP.W D1,D4
            alu(CMP,2,{D,1},{D,4},false,false); pc=0x17f5a;break;
        case 0x17f5a: // $017F5A: BNE $017F92
            if(branch(NE,0x17f92,true)) continue; pc=0x17f5e;break;
        case 0x17f5e: // $017F5E: MOVE.W D2,D4
            move(2,{D,2},{D,4},false); pc=0x17f60;break;
        case 0x17f60: // $017F60: SWAP D5
            swap(5); pc=0x17f62;break;
        case 0x17f62: // $017F62: CMP.W D1,D5
            alu(CMP,2,{D,1},{D,5},false,false); pc=0x17f64;break;
        case 0x17f64: // $017F64: BNE $017F8A
            if(branch(NE,0x17f8a,true)) continue; pc=0x17f68;break;
        case 0x17f68: // $017F68: MOVE.W D2,D5
            move(2,{D,2},{D,5},false); pc=0x17f6a;break;
        case 0x17f6a: // $017F6A: SWAP D5
            swap(5); pc=0x17f6c;break;
        case 0x17f6c: // $017F6C: CMP.W D1,D5
            alu(CMP,2,{D,1},{D,5},false,false); pc=0x17f6e;break;
        case 0x17f6e: // $017F6E: BNE $017F92
            if(branch(NE,0x17f92,true)) continue; pc=0x17f72;break;
        case 0x17f72: // $017F72: MOVE.W D2,D5
            move(2,{D,2},{D,5},false); pc=0x17f74;break;
        case 0x17f74: // $017F74: SWAP D7
            swap(7); pc=0x17f76;break;
        case 0x17f76: // $017F76: CMP.W D1,D7
            alu(CMP,2,{D,1},{D,7},false,false); pc=0x17f78;break;
        case 0x17f78: // $017F78: BNE $017F7E
            if(branch(NE,0x17f7e,true)) continue; pc=0x17f7c;break;
        case 0x17f7c: // $017F7C: MOVE.W D2,D7
            move(2,{D,2},{D,7},false); pc=0x17f7e;break;
        case 0x17f7e: // $017F7E: SWAP D7
            swap(7); pc=0x17f80;break;
        case 0x17f80: // $017F80: BT $017F92
            if(branch(T,0x17f92,true)) continue; pc=0x17f84;break;
        case 0x17f84: // $017F84: SWAP D3
            swap(3); pc=0x17f86;break;
        case 0x17f86: // $017F86: BT $017F92
            if(branch(T,0x17f92,true)) continue; pc=0x17f8a;break;
        case 0x17f8a: // $017F8A: SWAP D5
            swap(5); pc=0x17f8c;break;
        case 0x17f8c: // $017F8C: BT $017F92
            if(branch(T,0x17f92,true)) continue; pc=0x17f90;break;
        case 0x17f90: // $017F90: SWAP D4
            swap(4); pc=0x17f92;break;
        case 0x17f92: // $017F92: LEA -128(A0),A2
            lea({O,0,-128},2); pc=0x17f96;break;
        case 0x17f96: // $017F96: MOVE.L D3,(A2)+
            move(4,{D,3},{P,2},false); pc=0x17f98;break;
        case 0x17f98: // $017F98: MOVE.L D4,(A2)+
            move(4,{D,4},{P,2},false); pc=0x17f9a;break;
        case 0x17f9a: // $017F9A: MOVE.L D5,(A2)+
            move(4,{D,5},{P,2},false); pc=0x17f9c;break;
        case 0x17f9c: // $017F9C: MOVE.L D7,(A2)+
            move(4,{D,7},{P,2},false); pc=0x17f9e;break;
        case 0x17f9e: // $017F9E: LEA (A0),A2
            lea({M,0},2); pc=0x17fa0;break;
        case 0x17fa0: // $017FA0: ADDI.L #$10001,D3
            alu(ADD,4,{I,0,65537},{D,3},true,false); pc=0x17fa6;break;
        case 0x17fa6: // $017FA6: ADDI.L #$10001,D4
            alu(ADD,4,{I,0,65537},{D,4},true,false); pc=0x17fac;break;
        case 0x17fac: // $017FAC: ADDI.L #$10001,D5
            alu(ADD,4,{I,0,65537},{D,5},true,false); pc=0x17fb2;break;
        case 0x17fb2: // $017FB2: ADDI.L #$10001,D7
            alu(ADD,4,{I,0,65537},{D,7},true,false); pc=0x17fb8;break;
        case 0x17fb8: // $017FB8: MOVE.L D3,(A2)+
            move(4,{D,3},{P,2},false); pc=0x17fba;break;
        case 0x17fba: // $017FBA: MOVE.L D4,(A2)+
            move(4,{D,4},{P,2},false); pc=0x17fbc;break;
        case 0x17fbc: // $017FBC: MOVE.L D5,(A2)+
            move(4,{D,5},{P,2},false); pc=0x17fbe;break;
        case 0x17fbe: // $017FBE: MOVE.L D7,(A2)+
            move(4,{D,7},{P,2},false); pc=0x17fc0;break;
        case 0x17fc0: // $017FC0: MOVEM.L (A7)+,D2/D3/D4/D5/D6/D7/A2
            movem_mask(false,0x4fc); pc=0x17fc4;break;
        case 0x17fc4: // $017FC4: RTS
            if(ret()) return returned;continue;
        case 0x17fc6: // $017FC6: CLR.L D0
            unary(true,4,{D,0}); pc=0x17fc8;break;
        case 0x17fc8: // $017FC8: MOVE.W ($0029C0BA).L,D0
            move(2,{L,0,0x29c0ba},{D,0},false); pc=0x17fce;break;
        case 0x17fce: // $017FCE: DIVU.W #$3E8,D0
            divu({I,0,1000},0); pc=0x17fd2;break;
        case 0x17fd2: // $017FD2: ANDI.W #$F,D0
            alu(AND,2,{I,0,15},{D,0},true,false); pc=0x17fd6;break;
        case 0x17fd6: // $017FD6: ROR.W #4,D0
            shift(ROR,2,{I,0,4},0); pc=0x17fd8;break;
        case 0x17fd8: // $017FD8: MOVE.W D0,D1
            move(2,{D,0},{D,1},false); pc=0x17fda;break;
        case 0x17fda: // $017FDA: CLR.W D0
            unary(true,2,{D,0}); pc=0x17fdc;break;
        case 0x17fdc: // $017FDC: SWAP D0
            swap(0); pc=0x17fde;break;
        case 0x17fde: // $017FDE: DIVU.W #$64,D0
            divu({I,0,100},0); pc=0x17fe2;break;
        case 0x17fe2: // $017FE2: ANDI.W #$F,D0
            alu(AND,2,{I,0,15},{D,0},true,false); pc=0x17fe6;break;
        case 0x17fe6: // $017FE6: ROL.W #8,D0
            shift(ROL,2,{I,0,8},0); pc=0x17fe8;break;
        case 0x17fe8: // $017FE8: MOVE.W D0,D2
            move(2,{D,0},{D,2},false); pc=0x17fea;break;
        case 0x17fea: // $017FEA: CLR.W D0
            unary(true,2,{D,0}); pc=0x17fec;break;
        case 0x17fec: // $017FEC: SWAP D0
            swap(0); pc=0x17fee;break;
        case 0x17fee: // $017FEE: DIVU.W #$A,D0
            divu({I,0,10},0); pc=0x17ff2;break;
        case 0x17ff2: // $017FF2: ANDI.W #$F,D0
            alu(AND,2,{I,0,15},{D,0},true,false); pc=0x17ff6;break;
        case 0x17ff6: // $017FF6: ROL.W #4,D0
            shift(ROL,2,{I,0,4},0); pc=0x17ff8;break;
        case 0x17ff8: // $017FF8: MOVE.W D0,D3
            move(2,{D,0},{D,3},false); pc=0x17ffa;break;
        case 0x17ffa: // $017FFA: SWAP D0
            swap(0); pc=0x17ffc;break;
        case 0x17ffc: // $017FFC: MOVE.W D0,D4
            move(2,{D,0},{D,4},false); pc=0x17ffe;break;
        case 0x17ffe: // $017FFE: OR.W D2,D1
            alu(OR,2,{D,2},{D,1},false,false); pc=0x18000;break;
        case 0x18000: // $018000: OR.W D3,D1
            alu(OR,2,{D,3},{D,1},false,false); pc=0x18002;break;
        case 0x18002: // $018002: OR.W D4,D1
            alu(OR,2,{D,4},{D,1},false,false); pc=0x18004;break;
        case 0x18004: // $018004: RTS
            if(ret()) return returned;continue;
        case 0x18006: // $018006: LEA (A0),A3
            lea({M,0},3); pc=0x18008;break;
        case 0x18008: // $018008: ANDI.W #$F,D1
            alu(AND,2,{I,0,15},{D,1},true,false); pc=0x1800c;break;
        case 0x1800c: // $01800C: SUBQ.W #1,D1
            alu(SUB,2,{I,0,1},{D,1},false,true); pc=0x1800e;break;
        case 0x1800e: // $01800E: BLT $018042
            if(branch(LT,0x18042,true)) continue; pc=0x18012;break;
        case 0x18012: // $018012: CMPI.W #$6,D1
            alu(CMP,2,{I,0,6},{D,1},true,false); pc=0x18016;break;
        case 0x18016: // $018016: BGE $018046
            if(branch(GE,0x18046,true)) continue; pc=0x1801a;break;
        case 0x1801a: // $01801A: LEA ($000310CE).L,A2
            lea({L,0,0x310ce},2); pc=0x18020;break;
        case 0x18020: // $018020: CMPI.W #$1A,($0029C09C).L
            alu(CMP,2,{I,0,26},{L,0,0x29c09c},true,false); pc=0x18028;break;
        case 0x18028: // $018028: BNE $018030
            if(branch(NE,0x18030,false)) continue; pc=0x1802a;break;
        case 0x1802a: // $01802A: LEA ($000310E2).L,A2
            lea({L,0,0x310e2},2); pc=0x18030;break;
        case 0x18030: // $018030: CLR.W D2
            unary(true,2,{D,2}); pc=0x18032;break;
        case 0x18032: // $018032: CLR.W D3
            unary(true,2,{D,3}); pc=0x18034;break;
        case 0x18034: // $018034: MOVE.W (A2)+,D2
            move(2,{P,2},{D,2},false); pc=0x18036;break;
        case 0x18036: // $018036: MOVE.W (A2)+,D3
            move(2,{P,2},{D,3},false); pc=0x18038;break;
        case 0x18038: // $018038: MOVE.W D3,-128(A0)
            move(2,{D,3},{O,0,-128},false); pc=0x1803c;break;
        case 0x1803c: // $01803C: MOVE.W D2,(A0)+
            move(2,{D,2},{P,0},false); pc=0x1803e;break;
        case 0x1803e: // $01803E: DBF D1,$018034
            if(db(F,1,0x18034)) continue; pc=0x18042;break;
        case 0x18042: // $018042: LEA (A3),A0
            lea({M,3},0); pc=0x18044;break;
        case 0x18044: // $018044: RTS
            if(ret()) return returned;continue;
        case 0x18046: // $018046: MOVE.W #$3,D1
            move(2,{I,0,3},{D,1},false); pc=0x1804a;break;
        case 0x1804a: // $01804A: LEA ($00031106).L,A2
            lea({L,0,0x31106},2); pc=0x18050;break;
        case 0x18050: // $018050: CLR.W D2
            unary(true,2,{D,2}); pc=0x18052;break;
        case 0x18052: // $018052: CLR.W D3
            unary(true,2,{D,3}); pc=0x18054;break;
        case 0x18054: // $018054: CMPI.W #$1A,($0029C09C).L
            alu(CMP,2,{I,0,26},{L,0,0x29c09c},true,false); pc=0x1805c;break;
        case 0x1805c: // $01805C: BNE $018064
            if(branch(NE,0x18064,false)) continue; pc=0x1805e;break;
        case 0x1805e: // $01805E: LEA ($000310F6).L,A2
            lea({L,0,0x310f6},2); pc=0x18064;break;
        case 0x18064: // $018064: MOVE.W (A2)+,D2
            move(2,{P,2},{D,2},false); pc=0x18066;break;
        case 0x18066: // $018066: MOVE.W (A2)+,D3
            move(2,{P,2},{D,3},false); pc=0x18068;break;
        case 0x18068: // $018068: MOVE.W D3,-128(A0)
            move(2,{D,3},{O,0,-128},false); pc=0x1806c;break;
        case 0x1806c: // $01806C: MOVE.W D2,(A0)+
            move(2,{D,2},{P,0},false); pc=0x1806e;break;
        case 0x1806e: // $01806E: DBF D1,$018064
            if(db(F,1,0x18064)) continue; pc=0x18072;break;
        case 0x18072: // $018072: LEA (A3),A0
            lea({M,3},0); pc=0x18074;break;
        case 0x18074: // $018074: RTS
            if(ret()) return returned;continue;
        case 0x18076: // $018076: LEA ($8400).W,A5
            lea({W,0,0xffff8400},5); pc=0x1807a;break;
        case 0x1807a: // $01807A: MOVE.B 12(A5),D0
            move(1,{O,5,12},{D,0},false); pc=0x1807e;break;
        case 0x1807e: // $01807E: ANDI.W #$C,D0
            alu(AND,2,{I,0,12},{D,0},true,false); pc=0x18082;break;
        case 0x18082: // $018082: JMP $018086(PC,D0.W)
            jump({J,0,0x18086,0});continue;
        case 0x18086: // $018086: BT $018092
            if(branch(T,0x18092,true)) continue; pc=0x1808a;break;
        case 0x1808a: // $01808A: BT $0180DA
            if(branch(T,0x180da,true)) continue; pc=0x1808e;break;
        case 0x1808e: // $01808E: BT $018114
            if(branch(T,0x18114,true)) continue; pc=0x18092;break;
        case 0x18092: // $018092: BSR $013A2C
            call({L,0,0x013a2c},0x18096,true);continue;
        case 0x18096: // $018096: BSR $0177F2
            call({L,0,0x0177f2},0x1809a,true);continue;
        case 0x1809a: // $01809A: BSR $01780A
            call({L,0,0x01780a},0x1809e,true);continue;
        case 0x1809e: // $01809E: BSR $017CA8
            call({L,0,0x017ca8},0x180a2,true);continue;
        case 0x180a2: // $0180A2: MOVE.W #$1111,($000D0E80).L
            move(2,{I,0,4369},{L,0,0xd0e80},false); pc=0x180aa;break;
        case 0x180aa: // $0180AA: MOVE.W #$0,($000D0E82).L
            move(2,{I,0,0},{L,0,0xd0e82},false); pc=0x180b2;break;
        case 0x180b2: // $0180B2: MOVE.W #$0,($000D0E90).L
            move(2,{I,0,0},{L,0,0xd0e90},false); pc=0x180ba;break;
        case 0x180ba: // $0180BA: MOVE.W #$0,($000D0E98).L
            move(2,{I,0,0},{L,0,0xd0e98},false); pc=0x180c2;break;
        case 0x180c2: // $0180C2: MOVE.W #$0,($000D0E92).L
            move(2,{I,0,0},{L,0,0xd0e92},false); pc=0x180ca;break;
        case 0x180ca: // $0180CA: MOVE.W #$BC,($000D0E9A).L
            move(2,{I,0,188},{L,0,0xd0e9a},false); pc=0x180d2;break;
        case 0x180d2: // $0180D2: MOVE.B #$4,12(A5)
            move(1,{I,0,4},{O,5,12},false); pc=0x180d8;break;
        case 0x180d8: // $0180D8: RTS
            if(ret()) return returned;continue;
        case 0x180da: // $0180DA: LEA ($000D0438).L,A0
            lea({L,0,0xd0438},0); pc=0x180e0;break;
        case 0x180e0: // $0180E0: BSR $017706
            call({L,0,0x017706},0x180e4,true);continue;
        case 0x180e4: // $0180E4: LEA ($000D043C).L,A1
            lea({L,0,0xd043c},1); pc=0x180ea;break;
        case 0x180ea: // $0180EA: BSR $017744
            call({L,0,0x017744},0x180ee,true);continue;
        case 0x180ee: // $0180EE: LEA ($000D0452).L,A0
            lea({L,0,0xd0452},0); pc=0x180f4;break;
        case 0x180f4: // $0180F4: BSR $017776
            call({L,0,0x017776},0x180f8,true);continue;
        case 0x180f8: // $0180F8: LEA ($000D045E).L,A1
            lea({L,0,0xd045e},1); pc=0x180fe;break;
        case 0x180fe: // $0180FE: BSR $0177A6
            call({L,0,0x0177a6},0x18102,true);continue;
        case 0x18102: // $018102: LEA ($000D046A).L,A0
            lea({L,0,0xd046a},0); pc=0x18108;break;
        case 0x18108: // $018108: BSR $0177CE
            call({L,0,0x0177ce},0x1810c,true);continue;
        case 0x1810c: // $01810C: MOVE.B #$8,12(A5)
            move(1,{I,0,8},{O,5,12},false); pc=0x18112;break;
        case 0x18112: // $018112: RTS
            if(ret()) return returned;continue;
        case 0x18114: // $018114: RTS
            if(ret()) return returned;continue;
        case 0x18116: // $018116: CLR.W D0
            unary(true,2,{D,0}); pc=0x18118;break;
        case 0x18118: // $018118: MOVE.B 32(A5),D0
            move(1,{O,5,32},{D,0},false); pc=0x1811c;break;
        case 0x1811c: // $01811C: ADDQ.B #1,33(A5)
            alu(ADD,1,{I,0,1},{O,5,33},false,true); pc=0x18120;break;
        case 0x18120: // $018120: CMPI.B #$28,33(A5)
            alu(CMP,1,{I,0,40},{O,5,33},true,false); pc=0x18126;break;
        case 0x18126: // $018126: BLE $018138
            if(branch(LE,0x18138,true)) continue; pc=0x1812a;break;
        case 0x1812a: // $01812A: MOVE.B #$0,33(A5)
            move(1,{I,0,0},{O,5,33},false); pc=0x18130;break;
        case 0x18130: // $018130: SUBQ.B #1,32(A5)
            alu(SUB,1,{I,0,1},{O,5,32},false,true); pc=0x18134;break;
        case 0x18134: // $018134: BLT $018174
            if(branch(LT,0x18174,true)) continue; pc=0x18138;break;
        case 0x18138: // $018138: CLR.W D1
            unary(true,2,{D,1}); pc=0x1813a;break;
        case 0x1813a: // $01813A: CMPI.W #$A,D0
            alu(CMP,2,{I,0,10},{D,0},true,false); pc=0x1813e;break;
        case 0x1813e: // $01813E: BLT $01814A
            if(branch(LT,0x1814a,true)) continue; pc=0x18142;break;
        case 0x18142: // $018142: ADDQ.W #1,D1
            alu(ADD,2,{I,0,1},{D,1},false,true); pc=0x18144;break;
        case 0x18144: // $018144: SUBI.W #$A,D0
            alu(SUB,2,{I,0,10},{D,0},true,false); pc=0x18148;break;
        case 0x18148: // $018148: BT $01813A
            if(branch(T,0x1813a,false)) continue; pc=0x1814a;break;
        case 0x1814a: // $01814A: ORI.W #$30,D0
            alu(OR,2,{I,0,48},{D,0},true,false); pc=0x1814e;break;
        case 0x1814e: // $01814E: ADD.W D0,D0
            alu(ADD,2,{D,0},{D,0},false,false); pc=0x18150;break;
        case 0x18150: // $018150: ORI.W #$100,D0
            alu(OR,2,{I,0,256},{D,0},true,false); pc=0x18154;break;
        case 0x18154: // $018154: MOVE.W D0,2(A0)
            move(2,{D,0},{O,0,2},false); pc=0x18158;break;
        case 0x18158: // $018158: ADDQ.B #1,D0
            alu(ADD,1,{I,0,1},{D,0},false,true); pc=0x1815a;break;
        case 0x1815a: // $01815A: MOVE.W D0,130(A0)
            move(2,{D,0},{O,0,130},false); pc=0x1815e;break;
        case 0x1815e: // $01815E: ORI.W #$30,D1
            alu(OR,2,{I,0,48},{D,1},true,false); pc=0x18162;break;
        case 0x18162: // $018162: ADD.W D1,D1
            alu(ADD,2,{D,1},{D,1},false,false); pc=0x18164;break;
        case 0x18164: // $018164: ORI.W #$100,D1
            alu(OR,2,{I,0,256},{D,1},true,false); pc=0x18168;break;
        case 0x18168: // $018168: MOVE.W D1,0(A0)
            move(2,{D,1},{O,0,0},false); pc=0x1816c;break;
        case 0x1816c: // $01816C: ADDQ.B #1,D1
            alu(ADD,1,{I,0,1},{D,1},false,true); pc=0x1816e;break;
        case 0x1816e: // $01816E: MOVE.W D1,128(A0)
            move(2,{D,1},{O,0,128},false); pc=0x18172;break;
        case 0x18172: // $018172: RTS
            if(ret()) return returned;continue;
        case 0x18174: // $018174: MOVE.B #$5,20(A5)
            move(1,{I,0,5},{O,5,20},false); pc=0x1817a;break;
        case 0x1817a: // $01817A: RTS
            if(ret()) return returned;continue;
        case 0x18206: // $018206: MOVE.L #$F000,D6
            move(4,{I,0,61440},{D,6},false); pc=0x1820c;break;
        case 0x1820c: // $01820C: LEA ($8400).W,A5
            lea({W,0,0xffff8400},5); pc=0x18210;break;
        case 0x18210: // $018210: MOVE.B 12(A5),D0
            move(1,{O,5,12},{D,0},false); pc=0x18214;break;
        case 0x18214: // $018214: ANDI.W #$C,D0
            alu(AND,2,{I,0,12},{D,0},true,false); pc=0x18218;break;
        case 0x18218: // $018218: JMP $01821C(PC,D0.W)
            jump({J,0,0x1821c,0});continue;
        case 0x1821c: // $01821C: BT $018228
            if(branch(T,0x18228,true)) continue; pc=0x18220;break;
        case 0x18220: // $018220: BT $018280
            if(branch(T,0x18280,true)) continue; pc=0x18224;break;
        case 0x18224: // $018224: BT $0182BE
            if(branch(T,0x182be,true)) continue; pc=0x18228;break;
        case 0x18228: // $018228: JSR ($00013A2C).L
            call({L,0,0x13a2c},0x1822e,false);continue;
        case 0x1822e: // $01822E: JSR $0182EC(PC)
            call({C,0,0x182ec},0x18232,false);continue;
        case 0x18232: // $018232: JSR $01831E(PC)
            call({C,0,0x1831e},0x18236,false);continue;
        case 0x18236: // $018236: LEA ($000C0438).L,A0
            lea({L,0,0xc0438},0); pc=0x1823c;break;
        case 0x1823c: // $01823C: JSR $017706(PC)
            call({C,0,0x17706},0x18240,false);continue;
        case 0x18240: // $018240: LEA ($000C043C).L,A1
            lea({L,0,0xc043c},1); pc=0x18246;break;
        case 0x18246: // $018246: JSR $017744(PC)
            call({C,0,0x17744},0x1824a,false);continue;
        case 0x1824a: // $01824A: LEA ($000C0452).L,A0
            lea({L,0,0xc0452},0); pc=0x18250;break;
        case 0x18250: // $018250: JSR $017776(PC)
            call({C,0,0x17776},0x18254,false);continue;
        case 0x18254: // $018254: LEA ($000C045E).L,A1
            lea({L,0,0xc045e},1); pc=0x1825a;break;
        case 0x1825a: // $01825A: JSR $0177A6(PC)
            call({C,0,0x177a6},0x1825e,false);continue;
        case 0x1825e: // $01825E: LEA ($000C046A).L,A0
            lea({L,0,0xc046a},0); pc=0x18264;break;
        case 0x18264: // $018264: JSR $0177CE(PC)
            call({C,0,0x177ce},0x18268,false);continue;
        case 0x18268: // $018268: MOVE.B #$4,12(A5)
            move(1,{I,0,4},{O,5,12},false); pc=0x1826e;break;
        case 0x1826e: // $01826E: MOVE.W #$F380,($8F00).W
            move(2,{I,0,62336},{W,0,0xffff8f00},false); pc=0x18274;break;
        case 0x18274: // $018274: MOVE.W #$20,($8F10).W
            move(2,{I,0,32},{W,0,0xffff8f10},false); pc=0x1827a;break;
        case 0x1827a: // $01827A: CLR.W ($8F22).W
            unary(true,2,{W,0,0xffff8f22}); pc=0x1827e;break;
        case 0x1827e: // $01827E: RTS
            if(ret()) return returned;continue;
        case 0x18280: // $018280: JSR $01838A(PC)
            call({C,0,0x1838a},0x18284,false);continue;
        case 0x18284: // $018284: TST.W ($8F20).W
            unary(false,2,{W,0,0xffff8f20}); pc=0x18288;break;
        case 0x18288: // $018288: BEQ $0182B6
            if(branch(EQ,0x182b6,true)) continue; pc=0x1828c;break;
        case 0x1828c: // $01828C: MOVE.W ($8F22).W,D0
            move(2,{W,0,0xffff8f22},{D,0},false); pc=0x18290;break;
        case 0x18290: // $018290: MOVEA.W D0,A4
            move(2,{D,0},{A,4},true); pc=0x18292;break;
        case 0x18292: // $018292: ADD.W D0,D0
            alu(ADD,2,{D,0},{D,0},false,false); pc=0x18294;break;
        case 0x18294: // $018294: ADD.W A4,D0
            alu(ADD,2,{A,4},{D,0},false,false); pc=0x18296;break;
        case 0x18296: // $018296: LEA $0182C0(PC,D0.W),A0
            lea({J,0,0x182c0,0},0); pc=0x1829a;break;
        case 0x1829a: // $01829A: MOVE.W (A0)+,D1
            move(2,{P,0},{D,1},false); pc=0x1829c;break;
        case 0x1829c: // $01829C: BEQ $0182BC
            if(branch(EQ,0x182bc,false)) continue; pc=0x1829e;break;
        case 0x1829e: // $01829E: CMP.W ($8F00).W,D1
            alu(CMP,2,{W,0,0xffff8f00},{D,1},false,false); pc=0x182a2;break;
        case 0x182a2: // $0182A2: BHI $0182BC
            if(branch(HI,0x182bc,false)) continue; pc=0x182a4;break;
        case 0x182a4: // $0182A4: LEA -28926(A4),A1
            lea({O,4,-28926},1); pc=0x182a8;break;
        case 0x182a8: // $0182A8: MOVE.W (A0)+,(A1)
            move(2,{P,0},{M,1},false); pc=0x182aa;break;
        case 0x182aa: // $0182AA: MOVE.W (A0)+,16(A1)
            move(2,{P,0},{O,1,16},false); pc=0x182ae;break;
        case 0x182ae: // $0182AE: ADDQ.W #2,A4
            alu(ADD,2,{I,0,2},{A,4},false,true); pc=0x182b0;break;
        case 0x182b0: // $0182B0: MOVE.W A4,($8F22).W
            move(2,{A,4},{W,0,0xffff8f22},false); pc=0x182b4;break;
        case 0x182b4: // $0182B4: BT $0182BC
            if(branch(T,0x182bc,false)) continue; pc=0x182b6;break;
        case 0x182b6: // $0182B6: MOVE.B #$8,12(A5)
            move(1,{I,0,8},{O,5,12},false); pc=0x182bc;break;
        case 0x182bc: // $0182BC: RTS
            if(ret()) return returned;continue;
        case 0x182be: // $0182BE: RTS
            if(ret()) return returned;continue;
        case 0x182ec: // $0182EC: LEA ($000C1148).L,A0
            lea({L,0,0xc1148},0); pc=0x182f2;break;
        case 0x182f2: // $0182F2: LEA 128(A0),A2
            lea({O,0,128},2); pc=0x182f6;break;
        case 0x182f6: // $0182F6: LEA 128(A2),A3
            lea({O,2,128},3); pc=0x182fa;break;
        case 0x182fa: // $0182FA: LEA ($00030FFA).L,A1
            lea({L,0,0x30ffa},1); pc=0x18300;break;
        case 0x18300: // $018300: MOVE.W #$8000,D0
            move(2,{I,0,32768},{D,0},false); pc=0x18304;break;
        case 0x18304: // $018304: MOVE.B (A1)+,D0
            move(1,{P,1},{D,0},false); pc=0x18306;break;
        case 0x18306: // $018306: BEQ $01830C
            if(branch(EQ,0x1830c,false)) continue; pc=0x18308;break;
        case 0x18308: // $018308: MOVE.W D0,(A0)+
            move(2,{D,0},{P,0},false); pc=0x1830a;break;
        case 0x1830a: // $01830A: BT $018304
            if(branch(T,0x18304,false)) continue; pc=0x1830c;break;
        case 0x1830c: // $01830C: MOVE.B (A1)+,D0
            move(1,{P,1},{D,0},false); pc=0x1830e;break;
        case 0x1830e: // $01830E: BEQ $018314
            if(branch(EQ,0x18314,false)) continue; pc=0x18310;break;
        case 0x18310: // $018310: MOVE.W D0,(A2)+
            move(2,{D,0},{P,2},false); pc=0x18312;break;
        case 0x18312: // $018312: BT $01830C
            if(branch(T,0x1830c,false)) continue; pc=0x18314;break;
        case 0x18314: // $018314: MOVE.B (A1)+,D0
            move(1,{P,1},{D,0},false); pc=0x18316;break;
        case 0x18316: // $018316: BEQ $01831C
            if(branch(EQ,0x1831c,false)) continue; pc=0x18318;break;
        case 0x18318: // $018318: MOVE.W D0,(A3)+
            move(2,{D,0},{P,3},false); pc=0x1831a;break;
        case 0x1831a: // $01831A: BT $018314
            if(branch(T,0x18314,false)) continue; pc=0x1831c;break;
        case 0x1831c: // $01831C: RTS
            if(ret()) return returned;continue;
        case 0x1831e: // $01831E: LEA ($000C1340).L,A0
            lea({L,0,0xc1340},0); pc=0x18324;break;
        case 0x18324: // $018324: LEA ($000325A4).L,A1
            lea({L,0,0x325a4},1); pc=0x1832a;break;
        case 0x1832a: // $01832A: MOVE.W #$8000,D0
            move(2,{I,0,32768},{D,0},false); pc=0x1832e;break;
        case 0x1832e: // $01832E: MOVE.B (A1)+,D0
            move(1,{P,1},{D,0},false); pc=0x18330;break;
        case 0x18330: // $018330: BEQ $018336
            if(branch(EQ,0x18336,false)) continue; pc=0x18332;break;
        case 0x18332: // $018332: MOVE.W D0,(A0)+
            move(2,{D,0},{P,0},false); pc=0x18334;break;
        case 0x18334: // $018334: BT $01832E
            if(branch(T,0x1832e,false)) continue; pc=0x18336;break;
        case 0x18336: // $018336: RTS
            if(ret()) return returned;continue;
        case 0x1838a: // $01838A: MOVEQ #0,D1
            moveq({I,0,0},1); pc=0x1838c;break;
        case 0x1838c: // $01838C: MOVE.W ($8F00).W,D0
            move(2,{W,0,0xffff8f00},{D,0},false); pc=0x18390;break;
        case 0x18390: // $018390: BEQ $0183A8
            if(branch(EQ,0x183a8,false)) continue; pc=0x18392;break;
        case 0x18392: // $018392: ADD.W ($8F10).W,D0
            alu(ADD,2,{W,0,0xffff8f10},{D,0},false,false); pc=0x18396;break;
        case 0x18396: // $018396: MOVE.W D0,($8F00).W
            move(2,{D,0},{W,0,0xffff8f00},false); pc=0x1839a;break;
        case 0x1839a: // $01839A: ASR.W #4,D0
            shift(ASR,2,{I,0,4},0); pc=0x1839c;break;
        case 0x1839c: // $01839C: ANDI.W #$FF,D0
            alu(AND,2,{I,0,255},{D,0},true,false); pc=0x183a0;break;
        case 0x183a0: // $0183A0: MOVE.W D0,($000D0E92).L
            move(2,{D,0},{L,0,0xd0e92},false); pc=0x183a6;break;
        case 0x183a6: // $0183A6: ADDQ.W #1,D1
            alu(ADD,2,{I,0,1},{D,1},false,true); pc=0x183a8;break;
        case 0x183a8: // $0183A8: LEA ($8F02).W,A0
            lea({W,0,0xffff8f02},0); pc=0x183ac;break;
        case 0x183ac: // $0183AC: LEA ($8F12).W,A1
            lea({W,0,0xffff8f12},1); pc=0x183b0;break;
        case 0x183b0: // $0183B0: LEA ($000D0FA6).L,A2
            lea({L,0,0xd0fa6},2); pc=0x183b6;break;
        case 0x183b6: // $0183B6: MOVEQ #6,D7
            moveq({I,0,6},7); pc=0x183b8;break;
        case 0x183b8: // $0183B8: MOVE.W (A0),D0
            move(2,{M,0},{D,0},false); pc=0x183ba;break;
        case 0x183ba: // $0183BA: BEQ $0183CE
            if(branch(EQ,0x183ce,false)) continue; pc=0x183bc;break;
        case 0x183bc: // $0183BC: ADD.W (A1),D0
            alu(ADD,2,{M,1},{D,0},false,false); pc=0x183be;break;
        case 0x183be: // $0183BE: MOVE.W D0,(A0)
            move(2,{D,0},{M,0},false); pc=0x183c0;break;
        case 0x183c0: // $0183C0: ASR.W #4,D0
            shift(ASR,2,{I,0,4},0); pc=0x183c2;break;
        case 0x183c2: // $0183C2: ANDI.W #$3FF,D0
            alu(AND,2,{I,0,1023},{D,0},true,false); pc=0x183c6;break;
        case 0x183c6: // $0183C6: MOVE.W D0,(A2)
            move(2,{D,0},{M,2},false); pc=0x183c8;break;
        case 0x183c8: // $0183C8: MOVE.W D0,2(A2)
            move(2,{D,0},{O,2,2},false); pc=0x183cc;break;
        case 0x183cc: // $0183CC: ADDQ.W #1,D1
            alu(ADD,2,{I,0,1},{D,1},false,true); pc=0x183ce;break;
        case 0x183ce: // $0183CE: ADDQ.W #2,A0
            alu(ADD,2,{I,0,2},{A,0},false,true); pc=0x183d0;break;
        case 0x183d0: // $0183D0: ADDQ.W #2,A1
            alu(ADD,2,{I,0,2},{A,1},false,true); pc=0x183d2;break;
        case 0x183d2: // $0183D2: SUBQ.W #4,A2
            alu(SUB,2,{I,0,4},{A,2},false,true); pc=0x183d4;break;
        case 0x183d4: // $0183D4: DBF D7,$0183B8
            if(db(F,7,0x183b8)) continue; pc=0x183d8;break;
        case 0x183d8: // $0183D8: MOVE.W D1,($8F20).W
            move(2,{D,1},{W,0,0xffff8f20},false); pc=0x183dc;break;
        case 0x183dc: // $0183DC: RTS
            if(ret()) return returned;continue;
