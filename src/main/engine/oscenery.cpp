#include "engine/oscenery.hpp"

namespace
{
    constexpr uint32_t work(uint16_t a) { return 0xffff0000u | a; }
    int16_t s(uint16_t v) { return static_cast<int16_t>(v); }
    uint16_t asr(uint16_t v, unsigned n) { return static_cast<uint16_t>(s(v) >> n); }
}

void OScenery::byte_tiles(MainBus& b, uint32_t src, uint32_t dst)
{
    // Callers load D0/D1/D2 from the six-byte ROM header. $018902:
    // each row restores A1 from A2+128; MOVE.B preserves D2's high byte.
    const uint16_t columns=b.read16(src), rows=b.read16(src+2);
    uint16_t d2=b.read16(src+4);
    src+=6;
    for (uint32_t row=0;row<=rows;++row,dst+=128)
        for (uint32_t col=0;col<=columns;++col)
        {
            d2=static_cast<uint16_t>((d2&0xff00)|b.read8(src++));
            b.write16(dst+col*2,d2);
        }
}

void OScenery::init_view(MainBus& b)
{
    // $01871A-$018736: alternate background page and scroll registers.
    b.write16(work(0x902a),0); b.write16(work(0x902e),0);
    b.write16(0xd0e82,0x1111); b.write16(0xd0e94,0); b.write16(0xd0e96,0);
    b.write16(0xd0e84,0x2222);
    byte_tiles(b,0x3122e,0xd01ee); // $01873E-$018750
    for (unsigned i=0;i<5;++i) b.write32(0xd0f80+i*4,0x80008000); // $018754-$018768
    const uint16_t stage=b.read16(0x29c09c);
    if (stage==15 || stage==16) byte_tiles(b,0x188d6,0xd0cb2); // $01876A-$018792
}

void OScenery::stage_view(MainBus& b)
{
    // $018798: stage scenery visibility transition, bit 6 in the high byte.
    if (b.read8(0x29c200)&0x40)
    {
        if (!(b.read8(work(0x9048))&0x40)) init_view(b);
        // $0187B0-$0187E0: bank history selects a ROM upload record.
        uint16_t d1=b.read16(0x29c630)&0x0e;
        d1=b.read16(0x29c620+s(d1))&0xfc;
        const uint32_t src=b.read32(0x31252+s(d1));
        byte_tiles(b,src,0xc216c);
        // $0187E4-$018814: all arithmetic stays word-sized before ASR.
        uint16_t d0=b.read16(0x29c08e), original=d0;
        d0=static_cast<uint16_t>(d0+original); d0=static_cast<uint16_t>(d0+original);
        d0=static_cast<uint16_t>(asr(d0,5)-4);
        b.write16(0xd0e9c,d0&0x1ff);
        d0=b.read16(0x29c08c); original=d0;
        d0=static_cast<uint16_t>(d0<<3); d0=static_cast<uint16_t>(d0-original);
        d0=static_cast<uint16_t>(asr(d0,6)+4);
        b.write16(0xd0e94,d0&0xff);
    }
    else if (b.read8(work(0x9048))&0x40)
    {
        // $01881E-$01884E: clear previously visible text/tile scenery.
        byte_tiles(b,0x31240,0xd01ee); byte_tiles(b,0x31352,0xc216c);
    }
    // $018852: remember flags, then stage 15/16's segmented display.
    b.write16(work(0x9048),b.read16(0x29c200));
    const uint16_t stage=b.read16(0x29c09c);
    if (stage!=15 && stage!=16) return;
    uint32_t dst=0xc1cbe, src=0x188c8;
    uint16_t d0=b.read16(src), d7=b.read16(src+2); src+=4;
    uint16_t d1=b.read16(work(0x9046)), d2=d1&7;
    d1>>=3;
    // $018888-$018890: DBEQ stops without decrementing D7 when D1 hits 0.
    while (d1!=0)
    {
        d0=static_cast<uint16_t>((d0&0xff00)|b.read8(src++));
        b.write16(dst,d0); dst+=2; d1=static_cast<uint16_t>(d1-1);
        if (d1==0) break;
        d7=static_cast<uint16_t>(d7-1); if (d7==0xffff) break;
    }
    // $018894-$01889E: partial segment keeps the attribute byte.
    if (d2!=0)
    {
        d0=static_cast<uint16_t>((d0&0xff00)|b.read8(src++));
        d0=static_cast<uint16_t>(d0-8+d2); b.write16(dst,d0); dst+=2;
    }
    // $0188A0-$0188A2: DBF is inclusive, including an initial $FFFF.
    do { b.write16(dst,0); dst+=2; } while (--d7!=0xffff);
}

void OScenery::tile_list(MainBus& b, uint32_t src, uint32_t dst)
{
    // $01866C-$0186C8: each eight-byte entry has a signed tile offset and
    // a pointer to byte tiles (attribute word) or $FF00 + full-word tiles.
    const uint16_t count=b.read16(src); src+=2;
    for (uint32_t entry=0;entry<=count;++entry)
    {
        uint32_t output=dst+s(b.read16(src+entry*8));
        uint32_t input=b.read32(src+entry*8+2);
        uint16_t d1=b.read16(input); input+=2;
        const bool words=d1==0xff00;
        const uint8_t columns=b.read8(input++), rows=b.read8(input++);
        for (uint32_t row=0;row<=rows;++row,output+=128)
            for (uint32_t col=0;col<=columns;++col)
            {
                if (words) { d1=b.read16(input); input+=2; }
                else d1=static_cast<uint16_t>((d1&0xff00)|b.read8(input++));
                b.write16(output+col*2,d1);
            }
    }
}

void OScenery::scroll(MainBus& b)
{
    // $01854C-$018578: ROM jump table, whose unused entries simply return.
    const unsigned mode=b.read8(work(0x9000))&0x1c;
    if (mode==0 || mode>=0x10) return;
    bool setup=false, palette=false;
    if (mode==4)
    {
        // $01857A: SUBQ.W sets carry on underflow, not on a zero result.
        const uint16_t old=b.read16(work(0x9002)); b.write16(work(0x9002),old-1);
        if (old==0) { b.write8(work(0x9000),b.read8(work(0x9000))+4); setup=true; }
    }
    else if (mode==8)
    {
        const uint16_t old=b.read16(work(0x9030)); b.write16(work(0x9030),old-1);
        if (old==0)
        {
            const uint8_t frame=b.read8(work(0x9034)); b.write8(work(0x9034),frame-1);
            if (frame!=0) palette=true;
            else
            {
                const uint8_t repeat=b.read8(work(0x9001)); b.write8(work(0x9001),repeat-1);
                if (repeat==0) b.write8(work(0x9000),b.read8(work(0x9000))+4); // $0185E0
                else setup=true;
            }
        }
    }
    else if (mode==12)
    {
        const uint16_t old=b.read16(work(0x9004)); b.write16(work(0x9004),old-1);
        if (old==0) b.write8(work(0x9000),0);
    }
    if (setup) // $01859A-$0185A4, falls through into the palette frame.
    {
        b.write16(work(0x9032),0);
        b.write8(work(0x9034),b.read8(work(0x9020))-1); palette=true;
    }
    if (palette)
    {
        // $0185A8-$0185DE: palette script, negative word terminates it.
        b.write16(work(0x9030),b.read16(work(0x9006)));
        uint32_t src=b.read32(work(0x9020));
        const uint16_t frame=b.read16(work(0x9032)); b.write16(work(0x9032),frame+0x10);
        for (;;)
        {
            const uint16_t count=b.read16(src); src+=2; if (s(count)<0) break;
            const uint16_t offset=b.read16(src); src+=2;
            const uint32_t table=b.read32(src); src+=4;
            uint32_t output=0x123800+s(offset); // $0185CA: outside the DBF loop
            for (uint32_t i=0;i<=count;++i)
            {
                uint32_t input=table+s(frame); // $0185CE: source resets, destination advances
                for (unsigned j=0;j<4;++j,output+=4,input+=4) b.write32(output,b.read32(input));
            }
        }
    }
    // $0185F0-$018664: foreground pages and primary/alternate scroll.
    b.write32(0xd0e80,b.read32(work(0x9008)));
    constexpr uint16_t accum[4]={0x9028,0x902c,0x902a,0x902e};
    constexpr uint16_t speeds[4]={0x9010,0x9012,0x9018,0x901a};
    constexpr uint16_t bases[4]={0x900c,0x900e,0x9014,0x9016};
    constexpr uint32_t regs[4]={0xd0e98,0xd0e90,0xd0e9a,0xd0e92};
    for (unsigned i=0;i<4;++i)
    {
        uint16_t v=static_cast<uint16_t>(b.read16(work(accum[i]))+b.read16(work(speeds[i])));
        b.write16(work(accum[i]),v);
        v=static_cast<uint16_t>(asr(v,4)+b.read16(work(bases[i])));
        b.write16(regs[i],v&(i&1 ? 0x1ff : 0x3ff));
    }
}

void OScenery::stage_events(MainBus& b)
{
    // $018444-$01848C: select scenery routine from current stage.
    uint16_t d0=b.read16(work(0x9044));
    if (d0==0)
    {
        d0=b.read16(0x29c09c);
        if (d0==1 || d0==7 || d0==9 || d0==15 || d0==16) { stage_view(b); return; }
        if (d0!=5 && d0!=13 && d0!=20) return;
    }
    else
    {
        b.write16(work(0x9044),0);
        if (s(d0)>=7)
        {
            // $013A74-$013A94 tail call: 384 x eight longs of blank tiles.
            for (uint32_t a=0xc0000;a<0xc3000;a+=4) b.write32(a,0x00200020);
            return;
        }
        d0=static_cast<uint16_t>(d0+4); b.write16(work(0x9040),d0);
    }
    // $0184A0: the flag is bit 0 of the word's HIGH byte, not bit 0 of D0.
    d0=b.read16(work(0x9040)); if (s(d0)<0) return;
    const uint8_t flag=b.read8(work(0x9040)); b.write8(work(0x9040),flag|1);
    if (!(flag&1))
    {
        if (s(d0)>=16 || d0==11 || d0==12) { b.write8(work(0x9040),0x81); return; }
        if (static_cast<uint8_t>(d0)!=b.read8(work(0x9035)))
        {
            b.write8(work(0x9035),static_cast<uint8_t>(d0));
            // $0184D6-$018500: stage record and its 32-byte event settings.
            uint32_t record=0x220000+s(static_cast<uint16_t>(d0<<4));
            uint32_t settings=b.read32(record); record+=4;
            for (unsigned i=0;i<8;++i,settings+=4) b.write32(work(0x9000)+i*4,b.read32(settings));
            b.write32(work(0x9020),b.read32(record)); b.write32(work(0x9024),b.read32(record+4));
            b.write32(work(0x9028),0); b.write32(work(0x902c),0);
            uint32_t script=b.read32(work(0x9024));
            for (;;)
            {
                const uint16_t command=b.read16(script); script+=2; if (s(command)<0) break;
                const uint32_t dst=0xc0000+s(b.read16(script)); script+=2;
                uint32_t fill=0;
                if (static_cast<uint8_t>(command)!=0)
                {
                    if (static_cast<int8_t>(command)<0) fill=b.read32(work(0x901c));
                    for (unsigned i=0;i<0x400;++i) b.write32(dst+i*4,fill);
                }
                const uint32_t src=b.read32(script); script+=4;
                tile_list(b,src,dst); // $018530, stack save/restore omitted
            }
        }
    }
    scroll(b); // $01853A
    if (b.read8(work(0x9000))==0) b.write8(work(0x9040),0x81); // $01853E-$018548
}
