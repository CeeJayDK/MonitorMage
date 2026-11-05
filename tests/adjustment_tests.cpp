#include "live_lut.h"
#include "presets.h"
#include "lut1d.h"
#include <iostream>
#include <stdexcept>
#include <sstream>
#include <random>
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F> void rejects(F f){try{f();}catch(const std::exception&){return;}throw std::runtime_error("Invalid input accepted");}
}
int main(){try{
    {
        const std::vector<double> example{0,.2,.8,1};
        check(std::abs(color::sampleSpline(example,1.0/6)-.0625)<1e-14,"Editable curves must use cubic interpolation between knots");
        check(std::abs(color::sampleCurve(example,1.0/6)-.1)<1e-14,"Imported LUT interpolation must remain linear");
        check(color::sampleSpline({},.3)==.3 && color::sampleSpline({.1,.9},.5)==.5,"Empty and two-point spline cases must retain their meaning");
        std::mt19937 random(42017);
        for(int test=0;test<200;++test) {
            std::vector<double> points;
            for(int i=0;i<9;++i)points.push_back(test==0?i/8.0:test==1?.4:(random()%17)/16.0);
            std::sort(points.begin(),points.end());
            for(int i=0;i<9;++i)check(color::sampleSpline(points,i/8.0)==points[i],"Spline must pass through every control point exactly");
            double last=points.front();
            for(int i=0;i<=4096;++i) {
                const double x=i/4096.0,y=color::sampleSpline(points,x);
                const size_t segment=std::min(static_cast<size_t>(x*8),size_t(7));
                check(y>=last-1e-14 && y>=points[segment] && y<=points[segment+1],"Spline must stay monotone within each segment, including flat runs and steep steps");last=y;
                if(test==0)check(std::abs(y-x)<1e-14,"Identity spline must remain identity");
            }
            for(int i=1;i<8;++i) {
                const double x=i/8.0,e=1e-7,center=color::sampleSpline(points,x);
                const double left=(center-color::sampleSpline(points,x-e))/e,right=(color::sampleSpline(points,x+e)-center)/e;
                check(std::abs(left-right)<1e-4,"Spline slopes must join continuously at control points");
            }
            check(std::abs(color::sampleSpline(points,1.2)-(points.back()+.2))<1e-14,"Spline must preserve the existing HDR extension above reference white");
        }
        color::Adjustments curves;curves.curves[0]={0,.05,.15,.3,.5,.7,.85,.95,1};curves.curves[1]={0,.1,.2,.3,.4,.6,.8,.9,1};
        const double x=.31,expected=color::sampleSpline(curves.curves[1],color::sampleSpline(curves.curves[0],x));
        check(color::adjustSignal(x,0,6,curves)==expected,"Master and RGB curves must both use the displayed cubic interpolation");
    }
    live::Ramp identity{},baseline{};for(size_t c=0;c<3;++c)for(size_t i=0;i<256;++i){identity[c][i]=static_cast<WORD>(i*257);baseline[c][i]=static_cast<WORD>(c*73+i*(230-c*5));}
    live::Output sdr,hdr;hdr.hdr=true;hdr.white=280;live::Settings s;s.hdrFix=false;s.order={0,2,3,4,5,6,7,1};
    check(live::makeRamp(baseline,sdr,s)==baseline,"Neutral adjustments must preserve calibration exactly");
    check(live::makeRamp(baseline,hdr,s)==baseline,"Neutral HDR adjustments must preserve calibration exactly");
    {auto ordered=s;ordered.hdrFix=true;ordered.contrast=.2;auto legacy=ordered;legacy.order.clear();
        const auto now=live::makeRamp(baseline,hdr,ordered),old=live::makeRamp(baseline,hdr,legacy);
        for(size_t c=0;c<3;++c)for(size_t i=0;i<256;++i)check(std::abs(static_cast<int>(now[c][i])-old[c][i])<=1,"Neutral added stages must retain the verified HDR mapping");}
    for(int stage=2;stage<=7;++stage){
        s.adjustments={};auto& a=s.adjustments;
        if(stage==2)a.balance={130,90,80,15,-20};if(stage==3)a.midtone=150;
        if(stage==4)a.levels={5,95,8,88};if(stage==5){a.shadows=40;a.highlights=-35;}
        if(stage==6)a.curves[1]={0,.07,.17,.30,.46,.62,.76,.90,1};
        if(stage==7)a.imported=lut1d::decode("LUT_1D_SIZE 3\n0 0 0\n.3 .4 .5\n.9 .95 1\n");
        for(const auto& output:{sdr,hdr}){
            const auto ramp=live::makeRamp(baseline,output,s);check(ramp!=baseline,"Every nonneutral enabled stage must affect the ramp");
            for(size_t c=0;c<3;++c)for(size_t i=1;i<256;++i)check(ramp[c][i]>=ramp[c][i-1],"Compiled ramp must remain monotone");
            s.bypass=true;check(live::makeRamp(baseline,output,s)==baseline,"Bypass must remove every creative stage");s.bypass=false;
        }
        const double white=color::pqEncode(hdr.white),epsilon=1e-9;
        for(int c=0;c<3;++c)check(std::abs(live::evaluate(white-epsilon,c,hdr,s)-live::evaluate(white+epsilon,c,hdr,s))<1e-6,"HDR boundary must be continuous");
        presets::Data data;data.adjustments=a;const auto encoded=presets::encode(data);check(presets::decode(encoded)==data,"Every adjustment must roundtrip exactly");
        check(encoded.find("Version=3")!=std::string::npos,"Extended looks must use version 3");
        presets::Draft draft{{},data,data};draft.current.adjustments.balanceEnabled=!data.adjustments.balanceEnabled;check(draft.dirty(),"Switches must participate in preset dirtiness");
    }
    for(int shadows:{-50,0,50})for(int highlights:{-50,0,50}){
        color::Adjustments a;a.shadows=shadows;a.highlights=highlights;double last=0;
        for(int i=0;i<=10000;++i){double y=color::adjustSignal(i/10000.0,0,5,a);check(y>=last&&y<=1,"Extreme shadow/highlight curves must stay monotone and bounded");last=y;}
    }
    s.adjustments={};s.adjustments.balance[3]=40;const auto gains=color::balanceGains(s.adjustments);
    check(gains[0]>gains[2]&&*std::max_element(gains.begin(),gains.end())<=1,"Warmth must favour red without gain clipping");
    s.adjustments.balanceEnabled=false;check(live::makeRamp(baseline,sdr,s)==baseline,"Disabled balance must keep parameters without affecting output");
    s.adjustments={};s.adjustments.levels={10,90,4,95};s.contrast=.2;const auto first=live::makeRamp(identity,sdr,s);
    std::swap(s.order[0],s.order[3]);check(first!=live::makeRamp(identity,sdr,s),"Effect reordering must change compiled values");
    auto bad=s;bad.order={0,0,2,3,4,5,6,7};rejects([&]{live::makeRamp(identity,sdr,bad);});
    color::Adjustments a;a.levels={50,50,0,100};rejects([&]{color::validate(a);});
    for(const auto& text:{"LUT_3D_SIZE 2\n","LUT_1D_SIZE 2\n0 0 0\n","LUT_1D_SIZE 2\n0 0 0\n1 1 nan\n","LUT_1D_SIZE 2\n.6 0 0\n.5 1 1\n","LUT_1D_SIZE 2\nDOMAIN_MAX 2 3 2\n0 0 0\n1 1 1\n"})rejects([&]{lut1d::decode(text);});
    // Full-resolution LUT data is embedded, rather than retained as an external path.
    presets::Data dense;for(int c=0;c<3;++c)for(int i=0;i<1024;++i)dense.adjustments.imported[c].push_back(std::pow(i/1023.0,1+c*.1));
    check(presets::decode(presets::encode(dense))==dense,"1024-sample imported curves must roundtrip without precision loss");
    std::cout << "Adjustment composition, HDR continuity, monotonicity, order, bypass, presets and 1D LUT validation passed.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
