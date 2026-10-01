#include <array>
#include <cstdlib>
#include <iostream>
#include "engine/mainbus.hpp"

#define CHECK(x) do { if (!(x)) { std::cerr << "line " << __LINE__ << ": " << #x << '\n'; std::exit(1); } } while (false)

int main()
{
    MainBus::Memory memory;
    const std::array<uint8_t,4> mainrom{0x12,0x34,0x56,0x78}, subrom{0xab,0xcd,0xef,0x01};
    MainBus bus(mainrom.data(),mainrom.size(),subrom.data(),subrom.size(),memory);
    CHECK(bus.read32(0x400000)==0x12345678);
    CHECK(bus.read32(0x200000)==0xabcdef01);
    bus.write32(0,0); CHECK(bus.read32(0)==0x12345678);
    CHECK(bus.read16(0x180000)==0xffff);
    CHECK(bus.read8(0x180000)==0xff);
    // Absolute-short sign extension, the high work mirrors and all chip aliases.
    bus.write32(0xffff8890,0x12345678);
    CHECK(bus.read32(0x088890)==0x12345678);
    bus.write16(0x09c000,0x1122); CHECK(bus.read16(0xffff8000)==0x1122);
    bus.write16(0x0bc000,0x3344); CHECK(bus.read16(0xffffc000)==0x3344);
    CHECK(bus.read16(0x29c000)==0); // shared RAM is not the main's NVRAM
    bus.write32(0x29c070,0x55555555); CHECK(bus.sub.read32(0x9c070)==0x55555555);
    bus.sub.write16(0xbffff & ~1u,0x7788); CHECK(bus.read16(0x2a3ffe)==0x7788);
    for (const uint32_t a : {0xc0000u,0xd0000u,0x100000u,0x120000u,0x2ec000u})
    {
        bus.write16(a,0xabcd); bus.write8(a,0x12); bus.write8(a+1,0x34);
        CHECK(bus.read16(a)==0x1234);
    }
    CHECK(bus.read16(0x0df000)==0x1234);
    CHECK(bus.read16(0x10f000)==0x1234);
    CHECK(bus.read16(0x12c000)==0x1234);
    CHECK(bus.read16(0x2ed000)==0x1234);
    CHECK(bus.sub.read16(0xec000)==0x1234);
    bus.sub.write16(0xed002,0xaabb); CHECK(bus.read16(0x2ec002)==0xaabb);

    bus.write16(0xe3ff8,0xfffe); bus.write8(0xe0003,3);
    CHECK(bus.read32(0xe0004)==0xfffffffa);
    bus.write16(0x2e0000,0xfffd); bus.sub.write16(0xe0002,4);
    CHECK(bus.read32(0x2e0004)==0xfffffff4);
    bus.write32(0xe4000,100); bus.write16(0xe4014,7);
    CHECK(bus.read16(0xe4008)==14 && bus.read16(0xe400a)==2);
    bus.write32(0x2e4000,101); bus.sub.write16(0xe4014,7);
    CHECK(bus.read16(0x2e4008)==14 && bus.read16(0x2e400a)==3);

    bus.write16(0x2e8000,0xff80); bus.write16(0x2e8002,0x7f);
    bus.sub.write16(0xe8004,0xff00);
    CHECK(bus.read16(0x2ebff6)==0x8000);
    CHECK(bus.read16(0x2e800e)==0xff80);
    bus.write16(0xe8000,1); bus.write16(0xe8002,10);
    bus.write16(0xe8004,5); CHECK(bus.read16(0xe8008)==1);
    bus.write16(0xe800c,20); CHECK(bus.read16(0xe8008)==1);
    CHECK(bus.read16(0xe8006)==0x4000);
    bus.write16(0xe8008,0xffff); CHECK(bus.read16(0xe8008)==0);
    auto state=bus.compare_timer.state();
    state.counter=0xffe; state.exck=false;
    bus.compare_timer.load(state); bus.write16(0xe8018,0xaffe); bus.write16(0xe801c,1);
    bus.compare_timer.clock(true); CHECK(!bus.compare_timer.state().irq);
    bus.compare_timer.clock(true); CHECK(bus.compare_timer.state().counter==0xfff); // no duplicate edge
    bus.compare_timer.clock(false); bus.compare_timer.clock(true);
    CHECK(bus.compare_timer.state().irq && bus.compare_timer.state().counter==0xffe);
    CHECK(bus.compare_timer.read(13,false)==0xffff && bus.compare_timer.state().irq);
    CHECK(bus.read8(0xe801b)==0xff && !bus.compare_timer.state().irq);
    state=bus.compare_timer.state(); state.counter=0xfff; state.regs[10]=0; state.exck=false;
    bus.compare_timer.load(state); bus.compare_timer.clock(true);
    CHECK(bus.compare_timer.state().irq); // terminal count interrupts even disabled
    bus.write16(0xe8012,0); CHECK(!bus.compare_timer.state().irq);
    unsigned commands=0; uint8_t command=0;
    bus.compare_timer.sound_command=[&](uint8_t v){ ++commands; command=v; };
    bus.write8(0xe801f,0xab); CHECK(command==0xab && bus.compare_timer.state().nmi);
    CHECK(bus.compare_timer.sound_read(false)==0xab && bus.compare_timer.state().nmi);
    CHECK(bus.compare_timer.sound_read()==0xab && !bus.compare_timer.state().nmi);
    bus.write8(0xe8016,0x12); CHECK(command==0 && commands==2); // high-byte bus data's low byte is zero
    bus.write16(0xe801e,0x1234); CHECK(bus.compare_timer.sound_read()==0x34);

    unsigned adc_reads=0, adc_writes=0;
    bus.endpoints.adc_read=[&]{ ++adc_reads; return 0x67; };
    bus.endpoints.adc_write=[&](uint8_t d){ ++adc_writes; CHECK(d==0x89); };
    CHECK(bus.read8(0x130000)==0xff && adc_reads==0);
    CHECK(bus.read16(0x13fffe)==0xff67 && adc_reads==1);
    bus.write8(0x130000,0x89); CHECK(adc_writes==0);
    bus.write16(0x130000,0x8989); CHECK(adc_writes==1);
    bus.io[0].input=[](unsigned p){ return p==0 ? 0xa5 : 0xff; };
    bus.write16(0x14000c,0x0001); bus.write8(0x140001,0x3c);
    CHECK(bus.read16(0x14fff0)==0xff35); // low nibble input, high nibble output
    bus.write8(0x140000,0); CHECK(bus.read8(0x140001)==0x35);
    bus.write8(0x14000f,0x05); bus.write8(0x140009,0xff);
    CHECK(bus.read8(0x140009)==0x0f);
    CHECK(bus.read16(0x14000c)==0xff00); // direction registers are write-only

    unsigned swaps=0;
    bus.endpoints.sprite_draw=[&]{ ++swaps; };
    bus.write16(0x100000,0x4444); bus.write8(0x11ffff,0);
    CHECK(bus.read16(0x100000)==0 && memory.sprite_buffer[0]==0x44 && swaps==1);
    bus.write16(0x110000,0); CHECK(bus.read16(0x100000)==0x4444 && swaps==2);
    bus.write16(0x2ec000,0x5555); CHECK(bus.read16(0x2ee000)==0xffff);
    CHECK(bus.sub.read16(0xec000)==0 && memory.road_buffer[0]==0x55);
    bus.sub.read16(0xee000); CHECK(bus.read16(0x2ec000)==0x5555);
    bus.write8(0x2ee000,7); CHECK(memory.road_control==0);
    bus.sub.write8(0xee001,0xff); CHECK(memory.road_control==7);
    bus.write16(0x2efffe,4); CHECK(memory.road_control==4);

    std::array<uint8_t,MainBus::MEMORY_BYTES> reads{},writes{};
    unsigned accesses=0;
    MainBus::Trace trace{reads.data(),writes.data(),[&](bool,uint32_t,uint16_t,uint16_t){ ++accesses; }};
    bus.trace=&trace;
    bus.write8(0xffff8891,0x99); CHECK(writes[0x891] && !writes[0x890]);
    bus.read8(0xffff8890); CHECK(reads[0x890] && !reads[0x891]);
    CHECK(accesses==2);
    std::cout << "Main bus/chip regression checks passed\n";
}
