#include "cpu_visibility_capture.hpp"
int main(int argc,char** argv)try{
    CpuVisibilityCapture capture(argc>1?argv[1]:"build/oracle/caller.bin.visibility");
    capture.report();return capture.records&&capture.adc_reads&&capture.delayed&&!capture.failed?0:1;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
