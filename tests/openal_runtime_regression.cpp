// Exercise the production runtime loader and SDL fallback without importing OpenAL.
#include "frontend/audio_output.hpp"
#include <iostream>
#include <stdexcept>

void require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
int main(int argc,char** argv)try{
    require(argc==2,"supply a valid OpenAL Soft runtime for retry, or --missing-only");
    SDL_SetEnvironmentVariable(SDL_GetEnvironment(),"SDL_AUDIODRIVER","dummy",true);
    const char* bad[]={"/definitely-missing-ab2-openal-runtime",nullptr};
    for(const char* path:bad){
        // An existing incompatible library exercises symbol resolution failure.
        const char* incompatible=SDL_getenv("AB2_TEST_INCOMPATIBLE_RUNTIME");
        if(!path && !incompatible)continue;
        SDL_SetEnvironmentVariable(SDL_GetEnvironment(),"AB2_OPENAL_RUNTIME",path?path:incompatible,true);
        Frontend::AudioOutput output("");
        for(int i=0;i<400 && !output.status().ready;++i)SDL_Delay(5);
        require(output.status().ready&&!Frontend::OpenALRuntime::api().module,
            "original SDL audio tried to load OpenAL");
        Frontend::OutputSettings settings;settings.profile=Frontend::AudioProfile::headphones;
        auto generation=output.status().configuration;output.configure(settings);
        for(int i=0;i<400 && output.status().configuration<=generation;++i)SDL_Delay(5);
        const auto status=output.status();
        require(status.configuration>generation&&status.ready&&!status.openal,"missing/incompatible runtime did not fall back to SDL");
        require(status.text.find("OpenAL")!=std::string::npos,"fallback reason missing");
        require(!Frontend::OpenALRuntime::api().module,"failed module retained");
    }
    if(std::string(argv[1])=="--missing-only"){
        std::cout<<"Optional OpenAL: original SDL and missing/incompatible runtime fallback passed.\n";
        return 0;
    }
    // A successful retry in the same process must work after failed loads.
    SDL_SetEnvironmentVariable(SDL_GetEnvironment(),"AB2_OPENAL_RUNTIME",argv[1],true);
    Frontend::OpenALRuntime::api().load();
    require(Frontend::OpenALRuntime::api().module,"valid runtime retry failed");
    std::cout<<"Optional OpenAL: missing/incompatible fallback, failed-module cleanup and valid retry passed.\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
