#include "wide_ground_reference.hpp"
#include <iostream>
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main()try{
    std::vector<uint16_t> before(398*224),after(before.size());std::vector<uint8_t> priority(before.size());
    Presentation::WideScene::View p{},q{};p.width=q.width=398;p.origin=q.origin=39;
    p.ground_indices=before;q.ground_indices=after;p.protection=priority;
    auto clip=std::make_unique<Presentation::SceneClip>();
    for(unsigned fault=0;fault<6;++fault){
        std::fill(before.begin(),before.end(),0x1720);std::fill(priority.begin(),priority.end(),0);
        *clip={};clip->top=100;clip->bottom=199;clip->palette[0x1702]=clip->palette[0x1704]=0x7fff;
        before[180*398+100]=0x1702;before[180*398+101]=0x1704;
        if(fault==1)clip->road[0xc00+180*2+1]=1;
        if(fault==2)clip->palette[0x1704]=0;
        if(fault==3)clip->bottom=160;
        if(fault==4)priority[180*398+100]=priority[180*398+101]=4;
        if(fault==5)clip->road[180*2]=8;
        std::copy(before.begin(),before.end(),after.begin());after[150*398+200]=0x1702;
        const auto witness=wide_ground_fill_witness(p,std::span(clip.get(),1));
        require(valid_wide_ground_change(p,q,150*398+200,witness)==(fault==0),"ground reference accepted an invalid cross-row donor or rejected its baseline");
        after[150*398+200]=0x1707;
        require(!valid_wide_ground_change(p,q,150*398+200,witness),"ground reference invented an unobserved pen");
    }
    std::cout<<"Ground donor reference: positive cross-row witness, color control, palette, clip, priority, solid fill and observed-pen rejection passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
