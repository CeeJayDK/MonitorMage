#include "live_lut.h"
#include "color_math.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <stdexcept>
#include <new>
#include <mutex>

namespace live {
namespace {
constexpr const wchar_t* LeaseName = L"Local\\LutWorkbench-LutWriter";
struct Handle {
    HANDLE h = nullptr;
    explicit Handle(HANDLE value = nullptr) : h(value) {}
    ~Handle() { if (h && h != INVALID_HANDLE_VALUE) CloseHandle(h); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};
// A process may own several output sessions. Share one writer lease between
// them, but keep other processes excluded until every recovery helper exits.
struct WriterLease {
    Handle handle;
    DWORD thread=GetCurrentThreadId();
    explicit WriterLease(const wchar_t* name){
        handle.h=CreateMutexW(nullptr,TRUE,name);const DWORD error=GetLastError();
        if(!handle.h || error==ERROR_ALREADY_EXISTS)throw std::runtime_error("Another LUT session or timed test is active");
    }
    ~WriterLease(){ReleaseMutex(handle.h);}
};
struct WriterLeases {
    std::mutex mutex;
    std::weak_ptr<WriterLease> current;
    std::shared_ptr<WriterLease> acquire(const wchar_t* name){
        std::lock_guard<std::mutex> lock(mutex);
        if(auto lease=current.lock()){
            if(lease->thread!=GetCurrentThreadId())throw std::runtime_error("LUT sessions must use the same owner thread");
            return lease;
        }
        auto lease=std::make_shared<WriterLease>(name);current=lease;return lease;
    }
};
WriterLeases writerLeases;
struct DC {
    HDC h;
    explicit DC(const std::wstring& name) : h(CreateDCW(L"DISPLAY", name.c_str(), nullptr, nullptr)) {}
    ~DC() { if (h) DeleteDC(h); }
};
// ABI from Windows SDK wingdi.h. Query dynamically so Win10 can reject this
// newer request without importing a newer Windows entry point.
struct AdvancedColor2 {
    DISPLAYCONFIG_DEVICE_INFO_HEADER header;
    UINT32 flags = 0, encoding = 0, bits = 0, mode = 0;
};
bool legacyHdrFlagReliable() {
    using Version = LONG (WINAPI*)(OSVERSIONINFOW*);
    const auto query = reinterpret_cast<Version>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"));
    OSVERSIONINFOW info{}; info.dwOSVersionInfoSize = sizeof(info);
    return query && query(&info) == 0 && info.dwBuildNumber < 22000;
}
struct Snapshot {
    DWORD magic = 0x3254554c;
    wchar_t source[32]{}, identity[2048]{};
    Ramp baseline{};
};
struct Shared {
    volatile LONG applied = 0, pending = 0;
    Ramp last{}, candidate{};
};
struct View {
    Shared* p = nullptr;
    explicit View(HANDLE mapping) : p(static_cast<Shared*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Shared)))) {}
    ~View() { if (p) UnmapViewOfFile(p); }
};
std::wstring quote(const std::wstring& value) {
    if (value.find(L'"') != std::wstring::npos) throw std::runtime_error("Invalid quote in recovery argument");
    return L"\"" + value + L"\"";
}
void save(const std::wstring& file, const Snapshot& snapshot) {
    Handle h(CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
    DWORD count = 0;
    if (h.h == INVALID_HANDLE_VALUE || !WriteFile(h.h, &snapshot, sizeof(snapshot), &count, nullptr)
        || count != sizeof(snapshot) || !FlushFileBuffers(h.h)) throw std::runtime_error("Cannot save recovery baseline");
}
Snapshot load(const wchar_t* file) {
    Snapshot snapshot; DWORD count = 0; LARGE_INTEGER length{};
    Handle h(CreateFileW(file, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (h.h == INVALID_HANDLE_VALUE || !GetFileSizeEx(h.h, &length) || length.QuadPart != sizeof(snapshot)
        || !ReadFile(h.h, &snapshot, sizeof(snapshot), &count, nullptr) || count != sizeof(snapshot)
        || snapshot.magic != 0x3254554c || snapshot.source[31] || snapshot.identity[2047]
        || !snapshot.source[0] || !snapshot.identity[0]) throw std::runtime_error("Invalid live recovery snapshot");
    return snapshot;
}
Output matching(const Snapshot& snapshot) {
    for (const auto& output : outputs()) if (output.identity == snapshot.identity) return output;
    return {};
}
int restore(const Snapshot& snapshot, Shared* shared) {
    if (shared && InterlockedCompareExchange(&shared->applied, 0, 0) == 0
        && InterlockedCompareExchange(&shared->pending, 0, 0) == 0) return 0;
    const auto output = matching(snapshot);
    if (output.name.empty()) return 11;
    DC dc(output.name); Ramp current{};
    if (!dc.h || !GetDeviceGammaRamp(dc.h, &current)) return 12;
    if (shared) {
        const bool pending = InterlockedCompareExchange(&shared->pending, 0, 0) != 0;
        const bool applied = InterlockedCompareExchange(&shared->applied, 0, 0) != 0;
        if (!ownedRamp(current, snapshot.baseline, shared->last, shared->candidate, applied, pending)) return 10;
    }
    if (!SetDeviceGammaRamp(dc.h, const_cast<Ramp*>(&snapshot.baseline))) return 13;
    if (!GetDeviceGammaRamp(dc.h, &current) || difference(current, snapshot.baseline) > 2) return 14;
    return 0;
}
std::wstring tempFile() {
    static LONG serial=0;
    wchar_t folder[MAX_PATH]{};
    const DWORD size = GetTempPathW(MAX_PATH, folder);
    if (!size || size >= MAX_PATH) throw std::runtime_error("Cannot locate recovery folder");
    return std::wstring(folder) + L"LutWorkbench-live-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()) + L"-"+std::to_wstring(InterlockedIncrement(&serial))+L".bin";
}
std::wstring executable() {
    wchar_t file[32768]{};
    if (!GetModuleFileNameW(nullptr, file, static_cast<DWORD>(std::size(file)))) throw std::runtime_error("Cannot locate recovery executable");
    return file;
}
Ramp identity() {
    Ramp ramp{}; for (auto& channel : ramp) for (size_t i = 0; i < 256; ++i) channel[i] = static_cast<std::uint16_t>(i * 257);
    return ramp;
}
std::wstring targets(const std::wstring& mode) {
    // Each identity line ends in :advanced-color-flags:mode. Keep the physical
    // monitor paths, including clone-group membership, and discard only mode.
    std::wstring result;
    for(size_t start=0;start<mode.size();) {
        const auto end=mode.find(L'\n',start);auto line=mode.substr(start,end-start);
        const auto modeAt=line.rfind(L':');
        const auto flagsAt=modeAt==std::wstring::npos?std::wstring::npos:line.rfind(L':',modeAt-1);
        if(flagsAt==std::wstring::npos)return {};
        result+=line.substr(0,flagsAt)+L"\n";
        if(end==std::wstring::npos)break;start=end+1;
    }
    return result;
}
}
std::vector<Output> outputs() {
    for (int attempt = 0; attempt < 5; ++attempt) {
        UINT32 pc = 0, mc = 0;
        if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pc, &mc) != ERROR_SUCCESS) throw std::runtime_error("Cannot enumerate outputs");
        std::vector<DISPLAYCONFIG_PATH_INFO> paths(pc); std::vector<DISPLAYCONFIG_MODE_INFO> modes(mc);
        const LONG result = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pc, paths.data(), &mc, modes.data(), nullptr);
        if (result == ERROR_INSUFFICIENT_BUFFER) continue;
        if (result != ERROR_SUCCESS) throw std::runtime_error("Cannot read display configuration");
        std::vector<Output> list;
        std::vector<std::vector<std::wstring>> identities;
        for (UINT32 i = 0; i < pc; ++i) {
            const auto& p = paths[i];
            DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
            source.header = {DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME, sizeof(source), p.sourceInfo.adapterId, p.sourceInfo.id};
            DISPLAYCONFIG_TARGET_DEVICE_NAME target{};
            target.header = {DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME, sizeof(target), p.targetInfo.adapterId, p.targetInfo.id};
            if (DisplayConfigGetDeviceInfo(&source.header) || DisplayConfigGetDeviceInfo(&target.header)) throw std::runtime_error("Cannot identify display output");
            DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO color{};
            color.header = {DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO, sizeof(color), p.targetInfo.adapterId, p.targetInfo.id};
            const bool colorKnown = DisplayConfigGetDeviceInfo(&color.header) == ERROR_SUCCESS;
            AdvancedColor2 modern{};
            modern.header = {static_cast<DISPLAYCONFIG_DEVICE_INFO_TYPE>(15), sizeof(modern), p.targetInfo.adapterId, p.targetInfo.id};
            const bool modernKnown = DisplayConfigGetDeviceInfo(&modern.header) == ERROR_SUCCESS;
            static const bool reliableLegacy = legacyHdrFlagReliable();
            const bool targetHdr = modernKnown ? modern.mode == 2 : reliableLegacy && colorKnown && color.advancedColorEnabled;
            const bool modeKnown = modernKnown ? modern.mode == 0 || modern.mode == 2
                                              : colorKnown && (!color.advancedColorEnabled || reliableLegacy);
            auto at = std::find_if(list.begin(), list.end(), [&](const Output& o) { return o.name == source.viewGdiDeviceName; });
            if (at == list.end()) { list.push_back({}); list.back().name = source.viewGdiDeviceName; identities.push_back({}); at = list.end() - 1; }
            const auto index = static_cast<size_t>(at - list.begin());
            if (!at->label.empty()) at->label += L" + "; at->label += target.monitorFriendlyDeviceName;
            identities[index].push_back(std::wstring(target.monitorDevicePath) + L":" + std::to_wstring(color.value)
                                       + L":" + std::to_wstring(modernKnown ? modern.mode : targetHdr ? 2u : 0u));
            at->supported = at->supported && colorKnown && modeKnown && target.monitorDevicePath[0];
            at->hdr = at->hdr || targetHdr;
            at->safe = at->safe && modeKnown && !targetHdr;
            if (targetHdr) {
                DISPLAYCONFIG_SDR_WHITE_LEVEL white{};
                white.header = {DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL, sizeof(white), p.targetInfo.adapterId, p.targetInfo.id};
                if (DisplayConfigGetDeviceInfo(&white.header) == ERROR_SUCCESS) at->white = white.SDRWhiteLevel * 0.08;
                else at->supported = false;
            }
        }
        // Enumerate adapters once for the entire topology, rather than once
        // for each independent output group.
        std::vector<std::wstring> nvidiaSources;
        for(DWORD a=0;;++a){DISPLAY_DEVICEW adapter{};adapter.cb=sizeof(adapter);
            if(!EnumDisplayDevicesW(nullptr,a,&adapter,0))break;
            if(std::wstring(adapter.DeviceString).find(L"NVIDIA")!=std::wstring::npos)nvidiaSources.emplace_back(adapter.DeviceName);
        }
        for (size_t i = 0; i < list.size(); ++i) {
            auto& output = list[i]; auto& ids = identities[i]; std::sort(ids.begin(), ids.end());
            for (const auto& id : ids) output.identity += id + L"\n";
            output.nvidia=std::find(nvidiaSources.begin(),nvidiaSources.end(),output.name)!=nvidiaSources.end();
            if (output.hdr) output.supported = output.supported && output.nvidia && ids.size() == 1 && output.white >= 1 && output.white <= 10000;
        }
        return list;
    }
    throw std::runtime_error("Display configuration is changing");
}
bool sameDisplays(const Output& a,const Output& b) {
    const auto key=targets(a.identity);return !key.empty() && key==targets(b.identity);
}
double evaluate(double code,int channel,const Output& output,const Settings& settings) {
    const auto warm=[&](double value){
        if(settings.nightWarmth<=0)return value;
        const double gain=std::exp2(-settings.nightWarmth*.5*channel);
        return output.hdr?color::pqEncode(color::pqDecode(value)*gain):color::srgbEncode(color::srgbDecode(value)*gain);
    };
    if(settings.order.empty()) {
        const double amount=settings.bypass?0:settings.contrast;
        const bool sdrFix=!output.hdr && settings.hdrFix && settings.applyInSdr;
        if(sdrFix && settings.correctionFirst)code=color::srgbEncode(std::pow(code,settings.gamma));
        code=output.hdr?color::orderedHdr(code,output.white,settings.gamma,amount,settings.pivot,settings.hdrFix,settings.correctionFirst):color::contrast(code,amount,settings.pivot);
        if(sdrFix && !settings.correctionFirst)code=color::srgbEncode(std::pow(code,settings.gamma));
        if(!settings.bypass)for(int stage=2;stage<=7;++stage)code=color::adjustCode(code,channel,stage,settings.adjustments,output.hdr,output.white);
        return warm(code);
    }
    for(int stage:settings.order) {
        if(stage==1 && settings.hdrFix) {if(output.hdr)code=color::HdrGammaCurve(output.white,settings.gamma).map(code);else if(settings.applyInSdr)code=color::srgbEncode(std::pow(code,settings.gamma));}
        else if(!settings.bypass) {
            if(stage==0) code=output.hdr?color::combinedHdr(code,output.white,settings.gamma,settings.contrast,settings.pivot,false):color::contrast(code,settings.contrast,settings.pivot);
            else code=color::adjustCode(code,channel,stage,settings.adjustments,output.hdr,output.white);
        }
    }
    return warm(code);
}
Ramp makeRamp(const Ramp& baseline, const Output& output, const Settings& settings) {
    color::requireRange(settings.nightWarmth,0,1);
    color::requireRange(settings.contrast,-.25,.25);color::requireRange(settings.pivot,.2,.8);
    color::requireRange(settings.gamma,1,4);color::validate(settings.adjustments);
    if(!settings.order.empty()) {
        std::array<bool,8> seen{};
        for(int stage:settings.order) {if(stage<0||stage>7||seen[stage])throw std::invalid_argument("Invalid effect order");seen[stage]=true;}
        if(settings.order.size()!=8)throw std::invalid_argument("Incomplete effect order");
    }
    Ramp ramp{};
    for(size_t c=0;c<3;++c)for(size_t i=0;i<256;++i) {
        const double at=std::clamp(evaluate(static_cast<double>(i)/255,static_cast<int>(c),output,settings),0.0,1.0)*255;
        const auto lo=static_cast<size_t>(std::floor(at)),hi=std::min(lo+1,size_t{255});
        ramp[c][i]=static_cast<std::uint16_t>(std::lround(baseline[c][lo]+(static_cast<int>(baseline[c][hi])-baseline[c][lo])*(at-lo)));
    }
    return ramp;
}

Ramp compositionBaseline(const Ramp& captured,const Output& output,
    const Ramp* carriedCorrection,const Ramp* carriedBaseline,const Output* carriedOutput,bool resetToIdentity) {
    const auto neutral=identity();
    if(resetToIdentity)return neutral;
    if(carriedCorrection && carriedBaseline && carriedOutput &&
        !targets(output.identity).empty() && targets(output.identity)==targets(carriedOutput->identity) &&
        difference(captured,*carriedCorrection)<=2) {
        // Neutral calibration has the same meaning in either mode. Nonneutral
        // calibration is reusable only within the same SDR/HDR interpretation.
        if(output.hdr==carriedOutput->hdr || difference(*carriedBaseline,neutral)<=2)return *carriedBaseline;
        throw std::runtime_error("Display mode changed with a non-neutral calibration. Its new calibration cannot be inferred from the retained correction");
    }
    if(output.hdr)for(double gamma:{2.2,2.4}) {
        const auto known=makeRamp(neutral,output,Settings{0,0.5,gamma,true,false});
        if(difference(captured,known)<=2)return neutral;
    }
    return captured;
}

struct Session::Impl {
    Snapshot snapshot;
    Ramp baseline{};
    Output output;
    Handle ready, done, mapping, child;
    std::shared_ptr<WriterLease> lease;
    std::unique_ptr<View> view;
    std::wstring file;
    bool running = false, finishing = false;
    int finish() {
        if (!running) return 0;
        finishing = true; SetEvent(done.h);
        const DWORD waited = WaitForSingleObject(child.h, 5000);
        DWORD code = 15; GetExitCodeProcess(child.h, &code);
        if (waited != WAIT_OBJECT_0) return 15;
        lease.reset();
        running = false;
        return static_cast<int>(code);
    }
    ~Impl() { finish(); }
};
Session::Session() = default;
Session::~Session() = default;
bool Session::active() const { return impl_ && impl_->running && !impl_->finishing; }
bool Session::sameMode(const Output& output) const { return active() && output.identity == impl_->snapshot.identity; }
void Session::inheritCarryOver(const Session& other){
    if(active()||!other.checkCarryOver_)return;
    previousCorrection_=other.previousCorrection_;previousBaseline_=other.previousBaseline_;
    previousOutput_=other.previousOutput_;checkCarryOver_=true;
}
void Session::begin(const Output& requested, const Settings& settings, bool resetToIdentity) {
    if (impl_ && impl_->running) throw std::runtime_error("The previous correction or its restoration is still running");
    const auto current = outputs();
    const auto found = std::find_if(current.begin(), current.end(), [&](const Output& o) { return o.identity == requested.identity; });
    if (found == current.end()) throw std::runtime_error("Display configuration changed before activation");
    const Output output = *found;
    if (!output.supported) throw std::runtime_error("This display mode is not supported by the live backend");
    auto session = std::make_unique<Impl>();
    session->lease=writerLeases.acquire(LeaseName);
    if (output.name.size() >= std::size(session->snapshot.source) || output.identity.size() >= std::size(session->snapshot.identity))
        throw std::runtime_error("Display identity is too long");
    std::copy(output.name.begin(), output.name.end(), session->snapshot.source);
    std::copy(output.identity.begin(), output.identity.end(), session->snapshot.identity);
    DC dc(output.name);
    if (!dc.h || !GetDeviceGammaRamp(dc.h, &session->snapshot.baseline)) throw std::runtime_error("Cannot capture baseline calibration");
    const Ramp captured=session->snapshot.baseline;
    session->baseline=compositionBaseline(captured,output,
        checkCarryOver_?&previousCorrection_:nullptr,checkCarryOver_?&previousBaseline_:nullptr,
        checkCarryOver_?&previousOutput_:nullptr,resetToIdentity);
    session->output=output;
    const bool ownCarryOver=checkCarryOver_ && !targets(output.identity).empty() &&
        targets(output.identity)==targets(previousOutput_.identity) && difference(captured,previousCorrection_)<=2;
    // For our own stale ramp, restore the saved clean calibration, not the
    // stale correction. For an external HDR fix, preserve exactly what we found.
    if(ownCarryOver || resetToIdentity)session->snapshot.baseline=session->baseline;
    session->file = tempFile(); save(session->file, session->snapshot);
    static LONG serial=0;
    const auto key = std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64())+L"-"+std::to_wstring(InterlockedIncrement(&serial));
    const auto readyName = L"Local\\LutLive-ready-" + key, doneName = L"Local\\LutLive-done-" + key, mapName = L"Local\\LutLive-map-" + key;
    session->ready.h = CreateEventW(nullptr, TRUE, FALSE, readyName.c_str());
    session->done.h = CreateEventW(nullptr, TRUE, FALSE, doneName.c_str());
    session->mapping.h = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(Shared), mapName.c_str());
    if (!session->ready.h || !session->done.h || !session->mapping.h) throw std::runtime_error("Cannot initialize recovery helper");
    session->view = std::make_unique<View>(session->mapping.h);
    if (!session->view->p) throw std::runtime_error("Cannot map recovery state");
    ::new (session->view->p) Shared{};
    if(ownCarryOver || resetToIdentity){session->view->p->last=captured;InterlockedExchange(&session->view->p->applied,1);}
    const auto exe = executable();
    auto command = quote(exe) + L" --live-guard " + quote(session->file) + L" " + std::to_wstring(GetCurrentProcessId()) + L" " + quote(readyName) + L" " + quote(doneName) + L" " + quote(mapName);
    STARTUPINFOW startup{}; startup.cb = sizeof(startup); startup.dwFlags=STARTF_FORCEOFFFEEDBACK; PROCESS_INFORMATION process{};
    if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process))
        throw std::runtime_error("Cannot start recovery helper");
    session->child.h = process.hProcess; CloseHandle(process.hThread);
    session->running = true;
    HANDLE wait[] = {session->ready.h, session->child.h};
    if (WaitForMultipleObjects(2, wait, FALSE, 5000) != WAIT_OBJECT_0) throw std::runtime_error("Recovery helper did not become ready");
    impl_ = std::move(session);
    checkCarryOver_ = false;
    try { update(output, settings); } catch (...) { stop(); throw; }
}
void Session::update(const Output& output, const Settings& settings) {
    if (!sameMode(output)) throw std::runtime_error("Display mode changed; re-enable after checking the new configuration");
    if (WaitForSingleObject(impl_->child.h, 0) != WAIT_TIMEOUT) throw std::runtime_error("Recovery helper stopped; disabling corrections");
    const auto currentOutput = matching(impl_->snapshot);
    if (currentOutput.name.empty() || !currentOutput.supported) throw std::runtime_error("Display configuration changed or is unsupported");
    DC dc(currentOutput.name); Ramp current{};
    if (!dc.h || !GetDeviceGammaRamp(dc.h, &current)) throw std::runtime_error("Cannot read active calibration");
    auto* shared = impl_->view->p;
    const bool applied = InterlockedCompareExchange(&shared->applied, 0, 0) != 0;
    if (!ownedRamp(current, impl_->snapshot.baseline, shared->last, shared->candidate, applied, false))
        throw std::runtime_error("Another calibration changed the ramp; its adjustment will be preserved");
    const Ramp next = makeRamp(impl_->baseline, currentOutput, settings);
    if (difference(current, next) == 0) return;
    shared->candidate = next; MemoryBarrier(); InterlockedExchange(&shared->pending, 1);
    const bool accepted=SetDeviceGammaRamp(dc.h, &shared->candidate)!=FALSE;
    // Some drivers report failure despite retaining the ramp. The verified
    // readback, rather than that return value alone, decides whether it worked.
    if (!GetDeviceGammaRamp(dc.h, &current)) throw std::runtime_error("Cannot verify the requested correction");
    if (difference(current, next) > 2) throw std::runtime_error(accepted?
        "Driver did not retain the requested correction":"Driver rejected the correction");
    shared->last = next; MemoryBarrier(); InterlockedExchange(&shared->applied, 1); InterlockedExchange(&shared->pending, 0);
    std::ofstream log(impl_->file + L".session.txt", std::ios::app);
    log.imbue(std::locale::classic());
    log << "tick=" << GetTickCount64() << " hdr=" << currentOutput.hdr << " white_nits=" << currentOutput.white
        << " contrast=" << settings.contrast << " pivot=" << settings.pivot << " gamma=" << settings.gamma
        << " hdr_fix=" << settings.hdrFix << " bypass=" << settings.bypass << " readback_max_error=" << difference(current, next) << '\n';
}
int Session::stop() {
    if (!impl_) return 0;
    const int code = impl_->finish();
    if (code == 11 && impl_->view && impl_->view->p && impl_->view->p->applied
        && difference(impl_->view->p->last, impl_->snapshot.baseline) > 2) {
        previousCorrection_ = impl_->view->p->last; previousBaseline_=impl_->baseline;
        previousOutput_=impl_->output;checkCarryOver_ = true;
    }
    if (!impl_->running) impl_.reset(); return code;
}
int guardian(const wchar_t* file, DWORD parentId, const wchar_t* readyName, const wchar_t* doneName, const wchar_t* mapName, bool probe) {
    try {
        const auto snapshot = load(file);
        Handle parent(OpenProcess(SYNCHRONIZE, FALSE, parentId));
        Handle ready(OpenEventW(EVENT_MODIFY_STATE, FALSE, readyName));
        Handle done(OpenEventW(SYNCHRONIZE, FALSE, doneName));
        Handle mapping(OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, mapName)); View view(mapping.h);
        Handle lease(probe ? nullptr : OpenMutexW(SYNCHRONIZE, FALSE, LeaseName));
        if (!parent.h || !ready.h || !done.h || !mapping.h || !view.p || (!probe && !lease.h)) return 16;
        SetEvent(ready.h); HANDLE wait[] = {parent.h, done.h};
        if (WaitForMultipleObjects(2, wait, FALSE, INFINITE) == WAIT_FAILED) return 17;
        const int code = probe ? 0 : restore(snapshot, view.p);
        const std::wstring resultFile = std::wstring(file) + L".result.txt";
        std::ofstream log(resultFile); log << "Live restoration result=" << code << "\n0=baseline restored; 10=external ramp preserved; 11=display mode changed; other=restoration error\n";
        return code;
    } catch (...) { return 18; }
}
int recover(const wchar_t* file) { try { return restore(load(file), nullptr); } catch (...) { return 18; } }

int writerLeaseSelfTest(){
    const auto name=L"Local\\MonitorMageLeaseTest-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());
    WriterLeases owner,other;
    try {
        auto first=owner.acquire(name.c_str()),second=owner.acquire(name.c_str());
        if(first!=second)return 1;
        const auto blocked=[&](){try{auto unexpected=other.acquire(name.c_str());return false;}catch(const std::exception&){return true;}};
        if(!blocked())return 2;
        first.reset();if(!blocked())return 3;
        Handle recovery(OpenMutexW(SYNCHRONIZE,FALSE,name.c_str()));if(!recovery.h)return 4;
        second.reset();if(!blocked())return 5;
        CloseHandle(recovery.h);recovery.h=nullptr;
        auto next=other.acquire(name.c_str());if(!next)return 6;
        return 0;
    }catch(...){return 7;}
}
int recoverySelfTest(const wchar_t* exe, bool crashParent) {
    try {
        const auto file = tempFile(); Snapshot snapshot;
        wcscpy_s(snapshot.source, L"probe-source"); wcscpy_s(snapshot.identity, L"probe-identity"); save(file, snapshot);
        const auto key = std::to_wstring(GetCurrentProcessId()) + L"-probe-" + std::to_wstring(GetTickCount64());
        const auto rn = L"Local\\" + key + L"-ready", dn = L"Local\\" + key + L"-done", mn = L"Local\\" + key + L"-map";
        Handle ready(CreateEventW(nullptr, TRUE, FALSE, rn.c_str())), done(CreateEventW(nullptr, TRUE, FALSE, dn.c_str()));
        Handle mapping(CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(Shared), mn.c_str()));
        if (!ready.h || !done.h || !mapping.h) return 1;
        Handle owner;
        DWORD parentId = GetCurrentProcessId();
        if (crashParent) {
            auto ownerCommand = quote(exe) + L" --live-probe-owner";
            STARTUPINFOW ownerStart{}; ownerStart.cb = sizeof(ownerStart); ownerStart.dwFlags=STARTF_FORCEOFFFEEDBACK; PROCESS_INFORMATION ownerProcess{};
            if (!CreateProcessW(exe, ownerCommand.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &ownerStart, &ownerProcess)) return 5;
            owner.h = ownerProcess.hProcess; CloseHandle(ownerProcess.hThread); parentId = ownerProcess.dwProcessId;
        }
        auto command = quote(exe) + L" --live-guard-probe " + quote(file) + L" " + std::to_wstring(parentId) + L" " + quote(rn) + L" " + quote(dn) + L" " + quote(mn);
        STARTUPINFOW startup{}; startup.cb = sizeof(startup); startup.dwFlags=STARTF_FORCEOFFFEEDBACK; PROCESS_INFORMATION process{};
        if (!CreateProcessW(exe, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
            if (owner.h) TerminateProcess(owner.h, 1); return 2;
        }
        Handle child(process.hProcess), thread(process.hThread); HANDLE wait[] = {ready.h, child.h};
        const bool started = WaitForMultipleObjects(2, wait, FALSE, 5000) == WAIT_OBJECT_0;
        const bool waiting = started && WaitForSingleObject(child.h, 0) == WAIT_TIMEOUT;
        // Only terminate the disposable process created by this test. Probe mode
        // exercises parent-death recovery without reading or writing any LUT.
        if (crashParent) TerminateProcess(owner.h, 1); else SetEvent(done.h);
        const DWORD finished = WaitForSingleObject(child.h, 5000); DWORD code = 19; GetExitCodeProcess(child.h, &code);
        if (finished != WAIT_OBJECT_0) SetEvent(done.h);
        DeleteFileW(file.c_str()); DeleteFileW((file + L".result.txt").c_str());
        return waiting && finished == WAIT_OBJECT_0 && code == 0 ? 0 : 3;
    } catch (...) { return 4; }
}
} // namespace live
