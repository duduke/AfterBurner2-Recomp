#include "hwaudio/xboard_speaker.hpp"
#include <bit>
#include <fstream>
#include <iostream>
uint64_t number(std::istream& in,unsigned n){uint64_t v=0;while(n--){int c=in.get();if(c==EOF)throw std::runtime_error("truncated speaker capture");v=v*256+c;}return v;}
int main(int argc,char** argv){try{
    if(argc!=2)throw std::runtime_error("usage: oracle_speaker AUDIO_PREFIX");
    XBoardSpeaker mixer;
    for(unsigned d=0;d<2;++d){std::ifstream in(std::string(argv[1])+(d?".pcm":".fm"),std::ios::binary);char tag[8];
        if(!in.read(tag,8)||std::string(tag,8)!="AB2AST01")throw std::runtime_error("missing raw stream capture");
        while(in.peek()!=EOF){number(in,8);auto first=number(in,8),count=number(in,4);std::vector<XBoardPcm::Sample> samples;
            while(count--){int32_t l=number(in,4),r=number(in,4);samples.push_back({l,r});}mixer.append(d,first,samples);}}
    std::ifstream in(std::string(argv[1])+".speaker",std::ios::binary);char tag[8];
    if(!in.read(tag,8)||std::string(tag,8)!="AB2SPK01")throw std::runtime_error("missing speaker capture");
    uint64_t batches=0,nonzero=0,clipped=0,positive=0,negative=0,asymmetric=0;
    while(in.peek()!=EOF){auto ns=number(in,8),first=number(in,8),count=number(in,4);
        if(first!=mixer.samples())throw std::runtime_error("speaker index differs");auto out=mixer.update(ns);
        if(count!=out.size())throw std::runtime_error("speaker sample count differs");
        for(unsigned i=0;i<out.size();++i)for(unsigned ch=0;ch<2;++ch){uint32_t expected=number(in,4),bits=std::bit_cast<uint32_t>(out[i][ch]);
            if(bits!=expected){std::cerr<<"speaker ns="<<ns<<" index="<<first+i<<" channel="<<ch<<" bits="<<bits<<"/"<<expected<<'\n';throw std::runtime_error("mixed speaker float differs");}
            float reference=std::bit_cast<float>(expected);
            if(XBoardSpeaker::pcm(out[i][ch])!=std::clamp(int(reference*32768),-32768,32767))throw std::runtime_error("speaker PCM conversion differs");
            clipped+=reference>=1.0f||reference<-1.0f;positive+=reference>0;negative+=reference<0;
            nonzero+=XBoardSpeaker::pcm(out[i][ch])!=0;asymmetric+=ch==0&&out[i][0]!=out[i][1];}++batches;}
    if(std::string(argv[1]).find("speaker_edges")!=std::string::npos&&(!clipped||!positive||!negative||!asymmetric))throw std::runtime_error("directed speaker coverage missing");
    if(!nonzero)throw std::runtime_error("speaker capture silent");
    std::cout<<"MAME speaker: "<<mixer.samples()<<" stereo frames, "<<batches<<" batches, "<<nonzero<<" nonzero channel samples, "<<clipped<<" clipped, "<<positive<<" positive, "<<negative<<" negative, "<<asymmetric<<" asymmetric; float bits and PCM conversion identical\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
