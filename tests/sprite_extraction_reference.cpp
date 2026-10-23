// Offline extraction reference: exercise the unchanged, MAME-verified renderer.
#include "hwvideo/hwsprites.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

int main(int argc,char** argv) try {
    if(argc!=3)throw std::runtime_error("usage: sprite_extraction_reference input output");
    std::ifstream in(argv[1],std::ios::binary);
    std::ofstream out(argv[2],std::ios::binary);
    const auto word=[&](){unsigned char b[2]{};in.read(reinterpret_cast<char*>(b),2);return unsigned(b[0])*256+b[1];};
    std::vector<uint8_t> rom(0x200000);
    in.read(reinterpret_cast<char*>(rom.data()),rom.size());
    HWSprites hw;hw.init(rom.data(),rom.size());
    const auto count=word();
    for(unsigned i=0;i<count;++i){
        const auto bank=word(),offset=word(),pitch=word(),width=word(),height=word();
        if(bank>7 || !width || width>1024 || !height || height>4096 || pitch>127)
            throw std::runtime_error("invalid reference descriptor");
        // Unscaled forward draw, top-left pixel at (0,0), then terminator.
        hw.write16(0,0x100|(bank<<9));hw.write16(2,offset);
        hw.write16(4,190|((pitch&127)<<9));hw.write16(6,0x200);
        hw.write16(8,0xe200);hw.write16(10,height-1);hw.write16(12,0);
        hw.write16(16,0x8000);hw.swap();
        std::vector<uint16_t> pixels(width*height);
        hw.render(pixels.data(),width,height);
        for(auto pen:pixels)out.put(static_cast<char>(pen));
    }
    if(!in || in.peek()!=std::char_traits<char>::eof() || !out)
        throw std::runtime_error("reference input/output failed");
    std::cout<<"Sprite extraction reference: "<<count<<" frames rendered.\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
