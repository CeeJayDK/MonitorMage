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
	double Contrast = 0, Pivot = 0.5, Gamma = 2.2;
	bool HdrFix = true, Bypass = false;
	bool CorrectionFirst = false;
	color::Adjustments Adjustments;
	std::vector<int> Order;
	bool ApplyInSdr = false;
	double NightWarmth = 0; // Independent correction; survives creative bypass.
};

std::vector<Output> ActiveOutputs();
bool SameDisplays(const Output&, const Output&);
// The outputs to reset when a session's monitors were rearranged, such as one display joining a clone group.
// Empty when the same monitors are still together, or when the original ramp was not neutral.
std::vector<Output> OutputsAfterLayoutChange(const std::wstring& session_identity, const Ramp& baseline,
                                             const std::vector<Output>& outputs);
double EvaluateOutputCode(double code, int channel, const Output&, const Settings&);
Ramp BuildGammaRamp(const Ramp& baseline, const Output& output, const Settings& settings);
// Separate the calibration used for composition from the captured ramp that
// must be restored. Only exact known transforms may be removed automatically.
Ramp CompositionBaseline(const Ramp& captured, const Output& output, const Ramp* carried_correction = nullptr,
                         const Ramp* carried_baseline = nullptr, const Output* carried_output = nullptr,
                         bool reset_to_identity = false);
class Session
{
	struct Impl;
	std::unique_ptr<Impl> _impl;
	Ramp _previous_correction {};
	Ramp _previous_baseline {};
	Output _previous_output;
	bool _check_carry_over = false;

public:
	Session();
	~Session();
	Session(const Session&) = delete;
	Session& operator=(const Session&) = delete;
	bool IsActive() const;
	bool HasSameDisplayMode(const Output&) const;
	void InheritCarryOver(const Session&);
	void Start(const Output&, const Settings&, bool reset_to_identity = false);
	void Update(const Output&, const Settings&);
	// 0 restored, 10 external calibration preserved, 11 mode changed.
	int Stop();
};

int RunRecoveryGuardian(const wchar_t* file, DWORD parent, const wchar_t* ready, const wchar_t* done,
                        const wchar_t* mapping, bool probe = false);
// Whether sessions write a log, and keep their text files after a clean stop. Off until turned on.
void SetDiagnosticsEnabled(bool enabled);
// Deletes the log files of a finished session.
void RemoveSessionLogs(const std::wstring& log_base);
int WriterLeaseSelfTest();
int BaselineStorageSelfTest();
int RecoverySelfTest(const wchar_t* executable, bool crash_parent = false);
} // namespace live
