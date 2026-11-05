#pragma once
#include "live_lut.h"

namespace live {
// Key every transform input so preset, bypass, order and HDR changes cannot
// leave a stale active/paused indication. The expensive scan runs on changes.
class ProcessingState {
    Settings settings_;
    bool valid_=false,hdr_=false,result_=false;
    double white_=0;
public:
    bool active(bool session,const Output& output,const Settings& settings){
        if(!session){valid_=false;return false;}
        if(valid_ && hdr_==output.hdr && white_==output.white &&
           settings_.contrast==settings.contrast && settings_.pivot==settings.pivot &&
           settings_.gamma==settings.gamma && settings_.hdrFix==settings.hdrFix &&
           settings_.bypass==settings.bypass && settings_.correctionFirst==settings.correctionFirst &&
           settings_.applyInSdr==settings.applyInSdr && settings_.order==settings.order &&
           settings_.nightWarmth==settings.nightWarmth &&
           settings_.adjustments==settings.adjustments)return result_;
        settings_=settings;hdr_=output.hdr;white_=output.white;valid_=true;result_=false;
        for(int c=0;c<3 && !result_;++c)for(int i=0;i<256;++i){
            const double x=i/255.0;
            if(std::abs(evaluate(x,c,output,settings)-x)>1e-7){result_=true;break;}
        }
        return result_;
    }
};
}
