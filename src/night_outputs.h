#pragma once
#include "live_lut.h"
#include "night_light.h"
#include <memory>
namespace night {
// The selected output uses the editor's existing combined session. These
// sessions cover only the remaining independent output groups.
template<class Session=live::Session> class OutputSessions {
    struct Slot {live::Output output;std::unique_ptr<Session> session;bool keep=false;double lastLevel=-1;bool carry=false;};
    std::vector<Slot> slots_;
public:
    bool active()const{for(const auto& slot:slots_)if(slot.session->active())return true;return false;}
    void release(const live::Output& output,Session* primary=nullptr){
        for(auto& slot:slots_)if(slot.output.name==output.name||live::sameDisplays(slot.output,output)){
            const int code=slot.session->stop();if(code!=0&&code!=10&&code!=11)throw std::runtime_error("Night light could not release the selected display");
            if(primary)primary->inheritCarryOver(*slot.session);slot.keep=false;slot.carry=false;
        }
        slots_.erase(std::remove_if(slots_.begin(),slots_.end(),[](const Slot& slot){return !slot.keep&&!slot.carry&&!slot.session->active();}),slots_.end());
    }
    int stop(){int result=0;for(auto& slot:slots_){const bool wasActive=slot.session->active();const int code=slot.session->stop();if(wasActive)slot.carry=code==11;if(code!=0&&code!=10&&code!=11)result=code;}
        slots_.erase(std::remove_if(slots_.begin(),slots_.end(),[](const Slot& slot){return !slot.carry&&!slot.session->active();}),slots_.end());return result;}
    void update(const std::vector<live::Output>& outputs,const live::Output& primary,const Settings& config,double level){
        for(auto& slot:slots_)slot.keep=false;
        for(const auto& output:outputs){
            if(output.name==primary.name||!output.supported||level<=0||!includes(config,output.identity))continue;
            auto at=std::find_if(slots_.begin(),slots_.end(),[&](const Slot& slot){return live::sameDisplays(slot.output,output);});
            if(at==slots_.end()){slots_.push_back({output,std::make_unique<Session>(),true});at=slots_.end()-1;}
            at->keep=true;
            if(at->session->active()&&!at->session->sameMode(output)){
                const int code=at->session->stop();if(code!=0&&code!=10&&code!=11)throw std::runtime_error("Night light could not restore a display after its mode changed");
            }
            live::Settings settings;settings.hdrFix=false;settings.nightWarmth=level;settings.bypass=true;
            if(at->session->active()){if(at->lastLevel!=level)at->session->update(output,settings);}else at->session->begin(output,settings);
            at->output=output;at->lastLevel=level;at->carry=false;
        }
        for(auto& slot:slots_)if(!slot.keep){const bool wasActive=slot.session->active();const int code=slot.session->stop();if(wasActive)slot.carry=code==11;if(code!=0&&code!=10&&code!=11)throw std::runtime_error("Night light could not restore a display");}
        slots_.erase(std::remove_if(slots_.begin(),slots_.end(),[](const Slot& slot){return !slot.keep&&!slot.carry&&!slot.session->active();}),slots_.end());
    }
};
using Outputs=OutputSessions<>;
}
