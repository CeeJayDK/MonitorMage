#pragma once
#include <windows.h>
#include <memory>
#include <string>
#include <vector>
#include "recovery_rules.h"
#include "adjustments.h"

namespace live
{
struct Output
{
	std::wstring name, label, identity;
	bool safe = true; // SDR mode, retained for the timed diagnostic button.
	bool supported = true, hdr = false, nvidia = false;
	double white = 200;
};

struct Settings
{
	double contrast = 0, pivot = 0.5, gamma = 2.2;
	bool hdrFix = true, bypass = false;
	bool correctionFirst = false;
	color::Adjustments adjustments;
	std::vector<int> order;
	bool applyInSdr = false;
	double nightWarmth = 0; // Independent correction; survives creative bypass.
};

std::vector<Output> ActiveOutputs();
bool SameDisplays(const Output&, const Output&);
double EvaluateOutputCode(double code, int channel, const Output&, const Settings&);
Ramp BuildGammaRamp(const Ramp& baseline, const Output& output, const Settings& settings);
// Separate the calibration used for composition from the captured ramp that
// must be restored. Only exact known transforms may be removed automatically.
Ramp CompositionBaseline(const Ramp& captured, const Output& output, const Ramp* carriedCorrection = nullptr,
                         const Ramp* carriedBaseline = nullptr, const Output* carriedOutput = nullptr,
                         bool resetToIdentity = false);
class Session
{
	struct Impl;
	std::unique_ptr<Impl> impl_;
	Ramp previousCorrection_ {};
	Ramp previousBaseline_ {};
	Output previousOutput_;
	bool checkCarryOver_ = false;

public:
	Session();
	~Session();
	Session(const Session&) = delete;
	Session& operator=(const Session&) = delete;
	bool IsActive() const;
	bool HasSameDisplayMode(const Output&) const;
	void InheritCarryOver(const Session&);
	void Start(const Output&, const Settings&, bool resetToIdentity = false);
	void Update(const Output&, const Settings&);
	// 0 restored, 10 external calibration preserved, 11 mode changed.
	int Stop();
};

int RunRecoveryGuardian(const wchar_t* file, DWORD parent, const wchar_t* ready, const wchar_t* done,
                        const wchar_t* mapping, bool probe = false);
int RecoverFromSnapshotFile(const wchar_t* file);
int WriterLeaseSelfTest();
int RecoverySelfTest(const wchar_t* executable, bool crashParent = false);
} // namespace live
