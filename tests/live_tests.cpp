#include "live_lut.h"
#include "processing_state.h"
#include "night_outputs.h"
#include <iostream>
#include <stdexcept>

namespace {
void check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
struct NightProbe {
    inline static int starts=0,stops=0;
    bool running=false;live::Output output;
    ~NightProbe(){stop();}
    bool active()const{return running;}
    bool sameMode(const live::Output& next)const{return running&&output.identity==next.identity;}
    void inheritCarryOver(const NightProbe&){}
    void begin(const live::Output& next,const live::Settings& settings){check(!running,"Night light must restore before recapturing");check(settings.bypass&&!settings.hdrFix&&settings.nightWarmth>0,"Additional displays must receive only Night light");output=next;running=true;++starts;}
    void update(const live::Output& next,const live::Settings&){check(sameMode(next),"A changed mode needs a new capture");}
    int stop(){if(running){running=false;++stops;}return 0;}
};
void nightTests(){
    night::Settings config;config.enabled=true;config.strength=100;
    check(night::phase(config,12*3600).level==1&&night::phase(config,0).nextMs==0,"Manual Night light must not need a polling timer");
    config.scheduled=true;config.fade=10;
    check(night::phase(config,12*3600).level==0,"Daytime must be neutral");
    check(night::phase(config,12*3600).nextMs==9*3600000u,"Daytime must sleep until the evening boundary");
    check(night::phase(config,21*3600).level==0&&night::phase(config,21*3600).nextMs==5000,"Fade starts at zero and requests bounded updates");
    check(std::abs(night::phase(config,21*3600+300).level-.5)<1e-9,"Evening fade midpoint");
    check(night::phase(config,0).level==1&&night::phase(config,0).nextMs>3600000,"Overnight plateau must sleep");
    check(std::abs(night::phase(config,7*3600-300).level-.5)<1e-9&&night::phase(config,7*3600).level==0,"Morning fade and exact end");
    config.start=8*60;config.end=9*60;config.fade=60;
    check(night::phase(config,8.5*3600).level==1,"Short windows must clamp overlapping fades");
    config.enabled=false;check(night::phase(config,8.5*3600).nextMs==0,"Disabled schedule must have no wakeups");
    check(night::parseTime(L"23:59")==1439&&night::parseTime(L"24:00")==-1&&night::parseTime(L"9:00")==-1,"Time validation");
    const auto keyA=std::wstring(L"panel-A:0:0\n"),keyB=std::wstring(L"panel-B:0:0\n");
    night::include(config,keyA,false);
    check(!night::includes(config,L"panel-A:1:1\n")&&night::includes(config,keyB),"Display exclusions must survive HDR changes");
    check(!night::includes(config,keyB+keyA),"Clone groups must honor excluded physical members");
    night::include(config,keyB+keyA,true);check(config.excluded.empty(),"Reincluding a clone group includes its members");
    live::Output a,b;a.name=L"A";a.identity=keyA;b.name=L"B";b.identity=keyB;
    night::OutputSessions<NightProbe> sessions;sessions.update({a,b},a,config,.5);
    check(NightProbe::starts==1,"Do not apply a second ramp to the selected display");
    b.identity=L"panel-B:1:1\n";sessions.update({a,b},a,config,.6);
    check(NightProbe::stops==1&&NightProbe::starts==2,"Mode transition must restore before recapturing");
    sessions.release(b);check(!sessions.active(),"Release auxiliary ramp before the editor takes ownership");
    sessions.update({a,b},b,config,.5);check(sessions.active(),"Switching the selected group must keep other displays warm");
    night::include(config,a.identity,false);sessions.update({a,b},b,config,.5);
    check(!sessions.active(),"Excluding a display restores its captured ramp");
}
}
int main() {
    try {
        check(live::writerLeaseSelfTest()==0,"Independent output sessions share writer ownership and preserve recovery exclusion");
        nightTests();
        live::Ramp baseline{}, identity{};
        for (size_t i = 0; i < 256; ++i) {
            baseline[0][i] = static_cast<std::uint16_t>(i * 240);
            baseline[1][i] = static_cast<std::uint16_t>(100 + i * 230);
            baseline[2][i] = static_cast<std::uint16_t>(200 + i * 220);
            for (auto& channel : identity) channel[i] = static_cast<std::uint16_t>(i * 257);
        }
        live::Output sdr; live::Settings off; off.hdrFix = false;
        live::ProcessingState state;
        check(!state.active(false,sdr,off),"No session must appear paused");
        check(!state.active(true,sdr,off),"Neutral session must appear ready");
        check(!state.active(true,sdr,off),"Cached neutral result must be stable");
        auto changed=off;changed.contrast=.2;
        check(state.active(true,sdr,changed),"Changed contrast must invalidate neutral cache");
        changed.bypass=true;
        check(!state.active(true,sdr,changed),"Bypass must invalidate active cache");
        changed=off;changed.adjustments.balance[0]=80;
        check(state.active(true,sdr,changed),"RGB settings must invalidate neutral cache");
        changed.adjustments.balanceEnabled=false;
        check(!state.active(true,sdr,changed),"Layer disable must invalidate active cache");
        auto hdrState=sdr;hdrState.hdr=true;changed=off;changed.hdrFix=true;
        check(state.active(true,hdrState,changed),"HDR transition must invalidate cache");
        check(!state.active(false,hdrState,changed),"Ending a session must clear active state");
        check(live::makeRamp(baseline, sdr, off) == baseline, "SDR identity must preserve captured calibration exactly");
        live::Output hdr; hdr.hdr = true; hdr.white = 280;
        for(double gamma:{2.2,2.4}) {
            live::Settings correction;correction.gamma=gamma;
            check(live::makeRamp(baseline,sdr,correction)==baseline,"SDR gamma correction must be opt-in");
            const auto originalHdr=live::makeRamp(identity,hdr,correction);
            correction.applyInSdr=true;
            check(live::makeRamp(identity,hdr,correction)==originalHdr,"SDR opt-in must not change the HDR mapping");
            const auto adjusted=live::makeRamp(identity,sdr,correction);
            check(adjusted!=identity && adjusted[0][0]==0 && adjusted[0][255]==65535,"SDR compensation must alter tones and retain endpoints");
            for(size_t i=1;i<256;++i)check(adjusted[0][i]>=adjusted[0][i-1],"SDR compensation must remain monotone");
            const double x=.1,expected=color::srgbEncode(std::pow(x,gamma));
            check(std::abs(live::evaluate(x,0,sdr,correction)-expected)<1e-14,"SDR correction must encode the selected power response through sRGB");
            correction.bypass=true;check(live::makeRamp(identity,sdr,correction)==adjusted,"Creative bypass must retain monitor correction in SDR");
            correction.hdrFix=false;check(live::makeRamp(baseline,sdr,correction)==baseline,"Correction's single enable switch must control both modes");
        }
        check(live::makeRamp(baseline, hdr, off) == baseline, "Disabled HDR effects must preserve baseline exactly");
        live::Settings contrast; contrast.contrast = 0.2;
        const auto creative = live::makeRamp(baseline, sdr, contrast);
        for (size_t c = 0; c < 3; ++c) {
            check(creative[c][0] == baseline[c][0] && creative[c][255] == baseline[c][255], "Calibration endpoints must remain intact");
            check(creative[c][64] < baseline[c][64] && creative[c][192] > baseline[c][192], "Creative curve must precede monitor calibration");
        }
        const auto gammaOnly = live::makeRamp(identity, hdr, live::Settings{});
        hdr.identity=L"LG-device:3:2\n";
        auto previousSdr=sdr;previousSdr.identity=L"LG-device:0:0\n";
        // An already-loaded fix is replaced, not used as the calibration into
        // which a second copy of the fix would be composed.
        for(double gamma:{2.2,2.4}) {
            live::Settings existing;existing.gamma=gamma;
            const auto loaded=live::makeRamp(identity,hdr,existing);
            const auto clean=live::compositionBaseline(loaded,hdr);
            check(clean==identity,"Known HDR correction must be removed from composition baseline");
            check(live::makeRamp(clean,hdr,existing)==loaded,"Taking over the same fix must not double it");
            auto desired=existing;desired.contrast=.15;desired.gamma=2.4;
            check(live::makeRamp(clean,hdr,desired)==live::makeRamp(identity,hdr,desired),"Taking over must support changing fix and adding effects");
            auto quantized=loaded;quantized[0][64]+=2;
            check(live::compositionBaseline(quantized,hdr)==identity,"Known fix must tolerate two readback units");
            auto unknown=loaded;unknown[1][64]+=32;
            check(live::compositionBaseline(unknown,hdr)==unknown,"Unknown channel calibration must be preserved");
            check(live::compositionBaseline(loaded,sdr)==loaded,"SDR curves must not be mistaken for an HDR fix");
        }
        auto staleSettings=off;staleSettings.contrast=.2;
        const auto stale=live::makeRamp(identity,previousSdr,staleSettings);
        check(live::compositionBaseline(stale,hdr,&stale,&identity,&previousSdr)==identity,"Own stale correction must recover using known neutral calibration across modes");
        const auto calibratedStale=live::makeRamp(baseline,previousSdr,staleSettings);
        auto sameSdr=previousSdr;sameSdr.identity=L"LG-device:8:0\n";
        check(live::compositionBaseline(calibratedStale,sameSdr,&calibratedStale,&baseline,&previousSdr)==baseline,"Same-mode recovery must preserve non-neutral calibration");
        auto unrelated=hdr;unrelated.identity=L"Samsung-device:0:0\n";
        check(live::compositionBaseline(stale,unrelated,&stale,&identity,&previousSdr)==stale,"Coincidentally equal ramp on a different monitor must be preserved");
        auto external=stale;external[0][64]+=32;
        check(live::compositionBaseline(external,hdr,&stale,&identity,&previousSdr)==external,"External changes must not be replaced with a saved baseline");
        bool ambiguous=false;
        try{live::compositionBaseline(calibratedStale,hdr,&calibratedStale,&baseline,&previousSdr);}catch(const std::runtime_error&){ambiguous=true;}
        check(ambiguous,"Non-neutral calibration must not be guessed across SDR/HDR modes");
        // Explicit mode-transition policy: reset calibration, then compile the
        // same enabled effects for the new mode, without retaining the HDR fix.
        auto nextSdr=previousSdr;
        auto settingsAfterSwitch=live::Settings{};settingsAfterSwitch.contrast=.2;
        const auto staleHdr=live::makeRamp(baseline,hdr,settingsAfterSwitch);
        const auto reset=live::compositionBaseline(staleHdr,nextSdr,nullptr,nullptr,nullptr,true);
        check(reset==identity,"Automatic HDR-to-SDR transition must use identity even after nonneutral calibration");
        const auto resumedSdr=live::makeRamp(reset,nextSdr,settingsAfterSwitch);
        auto contrastOnly=settingsAfterSwitch;contrastOnly.hdrFix=false;
        check(resumedSdr==live::makeRamp(identity,nextSdr,contrastOnly),"SDR resume must skip HDR gamma and keep contrast");
        check(resumedSdr!=identity,"SDR contrast must remain active after the mode switch");
        settingsAfterSwitch.contrast=0;
        check(live::makeRamp(reset,nextSdr,settingsAfterSwitch)==identity,"No SDR effects still requires clearing the stale HDR ramp");
        settingsAfterSwitch.applyInSdr=true;
        check(live::makeRamp(reset,nextSdr,settingsAfterSwitch)!=identity,"An explicit SDR gamma opt-in must survive the transition");
        settingsAfterSwitch.applyInSdr=false;
        const auto resetHdr=live::compositionBaseline(resumedSdr,hdr,nullptr,nullptr,nullptr,true);
        check(live::makeRamp(resetHdr,hdr,settingsAfterSwitch)==gammaOnly,"Returning to HDR must reapply its enabled gamma fix once");
        check(live::sameDisplays(hdr,nextSdr),"Mode detection must match the same physical monitor across HDR/SDR");
        check(!live::sameDisplays(hdr,unrelated),"Mode detection must not reset an unrelated monitor");
        auto clone=hdr;clone.identity+=L"Samsung-device:0:0\n";
        check(!live::sameDisplays(hdr,clone),"A changed clone group must not be confused with a simple mode switch");
        // Anchors observed in the verified LG HDR path, allowing one WORD of rounding.
        check(std::abs(static_cast<int>(gammaOnly[0][64]) - 15286) <= 1, "Live HDR curve must match verified shadow anchor");
        check(std::abs(static_cast<int>(gammaOnly[0][128]) - 33028) <= 1, "Live HDR curve must match verified midtone anchor");
        const auto combined = live::makeRamp(identity, hdr, contrast);
        check(combined[0][32] < gammaOnly[0][32], "HDR creative contrast must deepen SDR-range shadows");
        auto paused = contrast; paused.hdrFix = false;
        const auto creativeHdr = live::makeRamp(baseline, hdr, paused);
        check(creativeHdr != baseline && creativeHdr != live::makeRamp(baseline, hdr, contrast), "HDR-only pause must retain contrast while removing gamma correction");
        // Highlights above SDR white and captured calibration stay intact during the pause.
        for (size_t c = 0; c < 3; ++c) for (size_t i = 192; i < 256; ++i)
            check(creativeHdr[c][i] == baseline[c][i], "HDR-only pause must preserve highlights and calibration");
        paused.bypass = true;
        check(live::makeRamp(baseline, hdr, paused) == baseline, "HDR pause plus creative bypass must preserve captured calibration exactly");
        auto reversed = contrast; reversed.correctionFirst = true;
        const auto reverseRamp = live::makeRamp(baseline, hdr, reversed);
        check(reverseRamp != live::makeRamp(baseline, hdr, contrast), "Reordering must change the compiled HDR ramp, not just the list");
        for (size_t c = 0; c < 3; ++c) for (size_t i = 192; i < 256; ++i)
            check(reverseRamp[c][i] == baseline[c][i], "Reordered effects must preserve HDR highlights and captured calibration");
        reversed.hdrFix = false;
        check(live::makeRamp(baseline, hdr, reversed) == creativeHdr, "Order must have no effect when gamma correction is disabled");
        reversed.hdrFix = true; reversed.bypass = true;
        auto gammaCalibrated = live::Settings{};
        check(live::makeRamp(baseline, hdr, reversed) == live::makeRamp(baseline, hdr, gammaCalibrated), "Creative bypass must preserve gamma and calibration in either order");
        contrast.bypass = true;
        check(live::makeRamp(identity, hdr, contrast) == gammaOnly, "Bypass must retain HDR correction");
        check(live::makeRamp(baseline, sdr, contrast) == baseline, "SDR bypass must restore captured calibration");
        auto warm=off;warm.nightWarmth=.7;warm.bypass=true;
        for(const auto& output:{sdr,hdr}){
            const auto result=live::makeRamp(identity,output,warm);
            check(state.active(true,output,warm),"Night light must update active-state cache");
            check(result[0]==identity[0]&&result[2][200]<result[1][200]&&result[1][200]<result[0][200],"Night light reduces blue and green without boosting or clipping red");
            for(const auto& channel:result)for(size_t i=1;i<256;++i)check(channel[i]>=channel[i-1],"Night light must stay monotonic in SDR and HDR");
            warm.nightWarmth=0;check(live::makeRamp(baseline,output,warm)==baseline,"Night light off preserves calibration during look bypass");warm.nightWarmth=.7;
        }
        std::cout << "Live ramp composition and Night light scheduling passed without display API calls.\n"; return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
