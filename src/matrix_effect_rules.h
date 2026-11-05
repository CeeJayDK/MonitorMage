#pragma once
#include "matrix_effect.h"
#include "matrix_math.h"
namespace matrix_effect {
// Ownership checks are shared by the real worker and its fake-API tests.
// Never compose onto an unknown matrix or overwrite another color tool.
class Engine {
    matrix_color::Api& api_;
    matrix_color::Matrix baseline_=matrix_color::identity(),ours_=baseline_,attempted_=baseline_;
    bool owns_=false,pending_=false;
public:
    explicit Engine(matrix_color::Api& api):api_(api){}
    State stop(){
        if(!owns_)return State::Off;
        matrix_color::Matrix current{};const bool known=api_.read(current);
        if(known&&matrix_color::difference(current,baseline_)<1e-5f){owns_=pending_=false;return State::Off;}
        if(known&&matrix_color::difference(current,ours_)>1e-5f&&(!pending_||matrix_color::difference(current,attempted_)>1e-5f)){
            owns_=pending_=false;return State::ExternalChange;
        }
        api_.write(baseline_);
        if(api_.read(current)&&matrix_color::difference(current,baseline_)<1e-5f){owns_=pending_=false;return State::Off;}
        // Retain both known owned values after a transient failure. A later
        // pause/exit can retry instead of mistaking our own matrix for a rival.
        return State::Failed;
    }
    State apply(int amount){
        if(amount<0||amount==100)return stop();
        if(amount>color::SaturationMaximum)return State::Failed;
        return apply(matrix_color::saturation(amount/100.f));
    }
    State apply(const matrix_color::Matrix& target){
        if(pending_){const auto restored=stop();if(restored!=State::Off)return restored;}
        matrix_color::Matrix current{};if(!api_.read(current))return State::Failed;
        if(owns_&&matrix_color::difference(current,ours_)>1e-5f&&matrix_color::difference(current,baseline_)>1e-5f){
            owns_=pending_=false;return State::ExternalChange;
        }
        if(!owns_){
            if(matrix_color::difference(current,matrix_color::identity())>1e-5f)return State::OtherTool;
            baseline_=current;
            ours_=baseline_;
        }
        if(owns_&&matrix_color::difference(current,target)<1e-5f){ours_=target;return State::Active;}
        // Track both old and attempted values until readback verifies the new
        // one, so a failed read or write cannot lose recovery ownership.
        attempted_=target;pending_=owns_=true;
        api_.write(target);matrix_color::Matrix readback{};
        if(!api_.read(readback)||matrix_color::difference(readback,target)>1e-5f){
            const auto restored=stop();return restored==State::ExternalChange?restored:State::Failed;
        }
        ours_=target;pending_=false;return State::Active;
    }
};
}
