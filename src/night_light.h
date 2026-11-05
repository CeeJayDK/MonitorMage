#pragma once
#include <algorithm>
#include <cmath>
#include <string>
#include <set>
#include <sstream>
#include <iomanip>
#include <cstdint>
namespace night {
struct Settings {
    bool enabled=false,scheduled=false;
    int strength=35,start=21*60,end=7*60,fade=10;
    std::set<std::wstring> excluded;
};
struct Phase {double level=0;unsigned nextMs=0;};
// Local clock seconds. Only fades need repeated updates; plateaus sleep until
// their next boundary. The OS time-change notification handles clock edits.
inline Phase phase(const Settings& s,double seconds){
    if(!s.enabled||s.strength<=0)return {};
    if(!s.scheduled)return {s.strength/100.,0};
    constexpr double day=86400;
    const double duration=std::fmod((s.end-s.start)*60.+day,day);
    if(duration==0)return {};
    const double elapsed=std::fmod(seconds-s.start*60.+day,day);
    if(elapsed>=duration)return {0,static_cast<unsigned>(std::max(1000.,std::ceil((day-elapsed)*1000)))};
    const double fade=std::min(s.fade*60.,duration/2);
    const double scale=fade>0?std::min({1.,elapsed/fade,(duration-elapsed)/fade}):1.;
    const bool fading=fade>0&&(elapsed<fade||elapsed>=duration-fade);
    const double next=fading?5:std::max(1.,duration-fade-elapsed);
    return {s.strength/100.*scale,static_cast<unsigned>(std::ceil(next*1000))};
}
inline std::wstring timeText(int minute){wchar_t text[6]{};swprintf_s(text,L"%02d:%02d",minute/60,minute%60);return text;}
inline int parseTime(const std::wstring& text){
    if(text.size()!=5||text[2]!=L':')return -1;
    for(int i:{0,1,3,4})if(text[i]<L'0'||text[i]>L'9')return -1;
    const int h=(text[0]-L'0')*10+text[1]-L'0',m=(text[3]-L'0')*10+text[4]-L'0';
    return h<24&&m<60?h*60+m:-1;
}
// Stable physical target IDs, independent of HDR/SDR mode and clone membership.
inline std::set<std::wstring> targets(const std::wstring& identity){
    std::set<std::wstring> keys;std::wistringstream lines(identity);std::wstring line;
    while(std::getline(lines,line)){
        const auto last=line.rfind(L':');const auto flags=last==std::wstring::npos?last:line.rfind(L':',last-1);
        if(flags==std::wstring::npos)continue;line.resize(flags);
        uint64_t hash=14695981039346656037ull;for(wchar_t c:line){hash^=static_cast<uint16_t>(c);hash*=1099511628211ull;}
        std::wostringstream out;out<<std::hex<<std::setw(16)<<std::setfill(L'0')<<hash;keys.insert(out.str());
    }
    return keys;
}
inline bool includes(const Settings& s,const std::wstring& identity){for(const auto& key:targets(identity))if(s.excluded.count(key))return false;return true;}
inline void include(Settings& s,const std::wstring& identity,bool value){for(const auto& key:targets(identity)){if(value)s.excluded.erase(key);else s.excluded.insert(key);}}
}
