#pragma once
#include "../../third_party/ymfm/ymfm_opm.h"
// Read-only output taps after the original chip clock/generate operation.
// Never clock operators a second time or modify the original DAC output.
class PresentationYm2151 : public ymfm::ym2151 {
public:
    using ymfm::ym2151::ym2151;
    output_data channels(uint32_t mask) const {
        output_data out; m_fm.output(out.clear(),0,32767,mask);out.roundtrip_fp();return out;
    }
};
