#include "profile_editor.h"
#include "ui_theme.h"
#include "viewport.h"
#include "ui_messages.h"
#include "icon_resources.h"
#include "programs.h"
#include <commdlg.h>
#include <algorithm>
#include <stdexcept>

namespace profile_editor
{
namespace
{
constexpr int PANEL_WIDTH = 420, PANEL_HEIGHT = 526;
constexpr int PANEL_RIGHT_EDGE = PANEL_WIDTH - 16, PANEL_BODY_WIDTH = PANEL_WIDTH - 32;
constexpr int ID_ENABLE = 201, ID_RULES = 203, ID_EXECUTABLE = 204, ID_BROWSE = 205, ID_USE_LAST_PROGRAM = 206;
constexpr int ID_ACTION = 207, ID_LOOK_CHOICE = 208, ID_SAVE_RULE = 209, ID_REMOVE_RULE = 210, ID_NEW_RULE = 211,
			  ID_APPLY = 212, ID_CLOSE = 213;
constexpr int ID_CHOOSE_RUNNING_PROGRAM = 214, ID_PROGRAM_LIST = 215, ID_SORT_PROGRAMS = 216,
			  ID_INCLUDE_BACKGROUND = 217, ID_REFRESH_PROGRAMS = 218, ID_CHOOSE_PROGRAM = 219, ID_CANCEL_PROGRAM = 220;
HWND editor_window, enabled_checkbox, return_note, rules_list, executable_edit, browse_button, use_last_button,
	action_choice, look_choice, save_rule_button, remove_rule_button, new_rule_button, apply_button, close_button, note;
HWND exe_label, action_label, look_label;
HWND friendly_name_label, run_button, picker_title, program_list, sort_programs_choice, include_background_checkbox,
	refresh_programs_button, choose_program_button, cancel_program_button, program_path, picker_note;
bool picking = false;
std::vector<programs::Program> running_programs;
HFONT ui_font;
applications::Configuration draft_configuration;
std::vector<Look> look_library;
std::filesystem::path last_application;
std::function<bool(const applications::Configuration&)> apply_changes;
std::wstring ControlText(HWND control)
{
	const int length = GetWindowTextLengthW(control);
	std::wstring text(static_cast<size_t>(length) + 1, L'\0');
	GetWindowTextW(control, text.data(), length + 1);
	text.resize(length);
	return text;
}

void SetNote(const std::wstring& text)
{
	SetWindowTextW(note, text.c_str());
}

void SetProgramFields(const std::filesystem::path& path)
{
	SetWindowTextW(executable_edit, path.c_str());
	SetWindowTextW(friendly_name_label, programs::DisplayName(path).c_str());
}

void UpdatePickerSelection()
{
	const auto row = SendMessageW(program_list, LB_GETCURSEL, 0, 0);
	const bool valid = row >= 0 && static_cast<size_t>(row) < running_programs.size();
	EnableWindow(choose_program_button, valid);
	SetWindowTextW(program_path, valid ? running_programs[row].path.c_str() : L"");
}

void RefreshProgramsList(bool enumerate)
{
	const bool memory = SendMessageW(sort_programs_choice, CB_GETCURSEL, 0, 0) == 0;
	try
	{
		if (enumerate)
			running_programs = programs::RunningPrograms(
				SendMessageW(include_background_checkbox, BM_GETCHECK, 0, 0) == BST_CHECKED, memory);
		else
			programs::SortPrograms(running_programs, memory);
		SendMessageW(program_list, WM_SETREDRAW, FALSE, 0);
		SendMessageW(program_list, LB_RESETCONTENT, 0, 0);
		for (const auto& item : running_programs)
		{
			const auto text = item.name + L"  |  " +
			                  (item.memory_known ? std::to_wstring((item.memory + 1048575) / 1048576) + L" MB"
			                                     : L"Memory unavailable");
			SendMessageW(program_list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
		}
		SendMessageW(program_list, WM_SETREDRAW, TRUE, 0);
		InvalidateRect(program_list, nullptr, FALSE);
		SetWindowTextW(picker_note,
		               (std::to_wstring(running_programs.size()) + L" programs. Memory is approximate.").c_str());
	}
	catch (const std::exception& error)
	{
		const std::string text(error.what());
		SetWindowTextW(picker_note, std::wstring(text.begin(), text.end()).c_str());
	}
	UpdatePickerSelection();
}

void SelectLookInCombo(HWND combo, const std::filesystem::path& path)
{
	int index = -1;
	for (size_t i = 0; i < look_library.size(); ++i)
		if (applications::SamePath(look_library[i].path, path))
			index = static_cast<int>(i);
	SendMessageW(combo, CB_SETCURSEL, index, 0);
}

std::filesystem::path SelectedLookPath(HWND combo)
{
	const auto index = SendMessageW(combo, CB_GETCURSEL, 0, 0);
	return index >= 0 && static_cast<size_t>(index) < look_library.size() ? look_library[index].path
	                                                                      : std::filesystem::path {};
}

void RefreshRules(int selection = -1)
{
	SendMessageW(rules_list, WM_SETREDRAW, FALSE, 0);
	SendMessageW(rules_list, LB_RESETCONTENT, 0, 0);
	for (const auto& rule : draft_configuration.Rules)
	{
		const auto text = programs::DisplayName(rule.Executable) + L"  |  " +
		                  (rule.Action == applications::Action::Look      ? rule.Look.stem().wstring()
		                   : rule.Action == applications::Action::Bypass  ? std::wstring(L"Bypass look")
		                   : rule.Action == applications::Action::Disable ? std::wstring(L"Pause MonitorMage")
		                                                                  : std::wstring(L"Pause HDR gamma"));
		SendMessageW(rules_list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
	}
	SendMessageW(rules_list, LB_SETCURSEL, selection, 0);
	SendMessageW(rules_list, WM_SETREDRAW, TRUE, 0);
	InvalidateRect(rules_list, nullptr, FALSE);
	EnableWindow(remove_rule_button, selection >= 0);
}

void UpdateActionControls()
{
	EnableWindow(look_choice, SendMessageW(action_choice, CB_GETCURSEL, 0, 0) == 0);
}

void LayoutEditor()
{
	const UINT dpi = GetDpiForWindow(editor_window);
	const auto offset = ui::ViewportOffset(editor_window);
	auto place = [&](HWND control, int x, int y, int w, int h)
	{
		MoveWindow(control, MulDiv(x, dpi, 96) - offset.x, MulDiv(y, dpi, 96) - offset.y, MulDiv(w, dpi, 96),
		           MulDiv(h, dpi, 96), TRUE);
	};
	HFONT previous = ui_font;
	ui_font = ui::CreateUiFont(dpi);
	for (HWND control : { enabled_checkbox,
	                      return_note,
	                      rules_list,
	                      executable_edit,
	                      browse_button,
	                      use_last_button,
	                      action_choice,
	                      look_choice,
	                      save_rule_button,
	                      remove_rule_button,
	                      new_rule_button,
	                      apply_button,
	                      close_button,
	                      note,
	                      exe_label,
	                      action_label,
	                      look_label,
	                      friendly_name_label,
	                      run_button,
	                      picker_title,
	                      program_list,
	                      sort_programs_choice,
	                      include_background_checkbox,
	                      refresh_programs_button,
	                      choose_program_button,
	                      cancel_program_button,
	                      program_path,
	                      picker_note })
		SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(ui_font), TRUE);
	if (previous)
		DeleteObject(previous);
	const auto width = [&](HWND control, int minimum = 68, int maximum = 240)
	{
		return ui::ContentWidth(control, ui_font, minimum, maximum);
	};
	for (HWND control :
	     { enabled_checkbox, return_note, rules_list, executable_edit, browse_button, use_last_button, action_choice,
	       look_choice, save_rule_button, remove_rule_button, new_rule_button, apply_button, close_button, note,
	       exe_label, action_label, look_label, friendly_name_label, run_button })
		ShowWindow(control, picking ? SW_HIDE : SW_SHOW);
	for (HWND control :
	     { picker_title, program_list, sort_programs_choice, include_background_checkbox, refresh_programs_button,
	       choose_program_button, cancel_program_button, program_path, picker_note })
		ShowWindow(control, picking ? SW_SHOW : SW_HIDE);
	SetWindowTextW(editor_window, picking ? L"MonitorMage - running programs" : L"MonitorMage - application profiles");
	SendMessageW(apply_button, BM_SETSTYLE, picking ? BS_PUSHBUTTON : BS_DEFPUSHBUTTON, FALSE);
	SendMessageW(choose_program_button, BM_SETSTYLE, picking ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON, FALSE);
	place(enabled_checkbox, 16, 16, PANEL_BODY_WIDTH, 28);
	place(return_note, 16, 50, PANEL_BODY_WIDTH, 34);
	place(rules_list, 16, 92, PANEL_BODY_WIDTH, 130);
	place(exe_label, 16, 234, 60, 22);
	place(friendly_name_label, 80, 234, PANEL_RIGHT_EDGE - 80, 22);
	place(executable_edit, 16, 260, PANEL_BODY_WIDTH, 26);
	const int browse_width = width(browse_button), last_width = width(use_last_button);
	place(browse_button, 16, 294, browse_width, 28);
	place(use_last_button, 28 + browse_width, 294, last_width, 28);
	place(run_button, 40 + browse_width + last_width, 294, width(run_button), 28);
	const int action_width = width(action_choice, 110, 180), look_x = 32 + action_width;
	place(action_label, 16, 330, action_width, 20);
	place(action_choice, 16, 350, action_width, 180);
	place(look_label, look_x, 330, PANEL_RIGHT_EDGE - look_x, 20);
	place(look_choice, look_x, 350, PANEL_RIGHT_EDGE - look_x, 220);
	const int store_width = width(save_rule_button), remove_width = width(remove_rule_button);
	place(save_rule_button, 16, 386, store_width, 30);
	place(remove_rule_button, 28 + store_width, 386, remove_width, 30);
	place(new_rule_button, 40 + store_width + remove_width, 386, width(new_rule_button), 30);
	const int close_width = width(close_button), apply_width = width(apply_button);
	place(note, 16, 428, PANEL_BODY_WIDTH, 44);
	place(apply_button, PANEL_RIGHT_EDGE - 12 - close_width - apply_width, 480, apply_width, 30);
	place(close_button, PANEL_RIGHT_EDGE - close_width, 480, close_width, 30);
	const int sort_width = width(sort_programs_choice, 112, 180), refresh_width = width(refresh_programs_button);
	place(picker_title, 16, 16, PANEL_BODY_WIDTH, 26);
	place(sort_programs_choice, 16, 52, sort_width, 180);
	place(include_background_checkbox, 16, 84, width(include_background_checkbox, 140, 200), 28);
	place(refresh_programs_button, PANEL_RIGHT_EDGE - refresh_width, 84, refresh_width, 28);
	const int cancel_width = width(cancel_program_button), choose_width = width(choose_program_button);
	place(program_list, 16, 122, PANEL_BODY_WIDTH, 280);
	place(program_path, 16, 414, PANEL_BODY_WIDTH, 26);
	place(picker_note, 16, 450, PANEL_BODY_WIDTH, 24);
	place(choose_program_button, PANEL_RIGHT_EDGE - 12 - cancel_width - choose_width, 480, choose_width, 30);
	place(cancel_program_button, PANEL_RIGHT_EDGE - cancel_width, 480, cancel_width, 30);
}

LRESULT CALLBACK ProfileEditorWindowProc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param)
{
	LRESULT theme_result = 0;
	if (ui::HandleThemeMessage(hwnd, message, w_param, l_param, theme_result))
		return theme_result;
	if (message == WM_MEASUREITEM)
	{
		auto* item = reinterpret_cast<MEASUREITEMSTRUCT*>(l_param);
		if (item->CtlType == ODT_LISTBOX)
		{
			item->itemHeight = MulDiv(22, GetDpiForWindow(hwnd), 96);
			return TRUE;
		}
	}
	if (message == WM_DRAWITEM)
	{
		const auto& item = *reinterpret_cast<DRAWITEMSTRUCT*>(l_param);
		if (item.CtlType == ODT_LISTBOX)
		{
			const bool selected = (item.itemState & ODS_SELECTED) != 0;
			HBRUSH fill = CreateSolidBrush(selected ? ui::THEME_SELECTED_TAB : ui::THEME_SURFACE);
			FillRect(item.hDC, &item.rcItem, fill);
			DeleteObject(fill);
			if (item.itemID == static_cast<UINT>(-1))
				return TRUE;
			const auto length = SendMessageW(item.hwndItem, LB_GETTEXTLEN, item.itemID, 0);
			if (length == LB_ERR)
				return TRUE;
			std::wstring text(static_cast<size_t>(length) + 1, L'\0');
			SendMessageW(item.hwndItem, LB_GETTEXT, item.itemID, reinterpret_cast<LPARAM>(text.data()));
			text.resize(static_cast<size_t>(length));
			const auto old = SelectObject(item.hDC, ui_font ? ui_font : GetStockObject(DEFAULT_GUI_FONT));
			SetBkMode(item.hDC, TRANSPARENT);
			SetTextColor(item.hDC, ui::THEME_TEXT);
			RECT label = item.rcItem;
			const int inset = MulDiv(6, GetDpiForWindow(hwnd), 96);
			label.left += inset;
			label.right -= inset;
			const auto split = text.rfind(L"  |  ");
			if (split != std::wstring::npos)
			{
				const auto detail = text.substr(split + 5);
				text.resize(split);
				SIZE size {};
				GetTextExtentPoint32W(item.hDC, detail.c_str(), static_cast<int>(detail.size()), &size);
				RECT right = label;
				right.left = right.right - std::min(static_cast<int>(size.cx) + inset,
				                                    static_cast<int>((label.right - label.left) * 2 / 5));
				label.right = right.left - inset;
				SetTextColor(item.hDC, ui::THEME_MUTED);
				DrawTextW(item.hDC, detail.c_str(), -1, &right,
				          DT_SINGLELINE | DT_VCENTER | DT_RIGHT | DT_END_ELLIPSIS | DT_NOPREFIX);
				SetTextColor(item.hDC, ui::THEME_TEXT);
			}
			DrawTextW(item.hDC, text.c_str(), -1, &label, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
			if (item.itemState & ODS_FOCUS)
			{
				RECT focus = item.rcItem;
				InflateRect(&focus, -1, -1);
				DrawFocusRect(item.hDC, &focus);
			}
			SelectObject(item.hDC, old);
			return TRUE;
		}
	}
	if (message == WM_SIZE)
	{
		ui::ApplyWindowFrameTheme(hwnd);
		return 0;
	}
	if (message == WM_CREATE)
	{
		ui::FixedWindowSize(hwnd, PANEL_WIDTH, PANEL_HEIGHT);
		ui::EnableViewport(hwnd, PANEL_WIDTH, PANEL_HEIGHT);
		editor_window = hwnd;
		const auto instance = reinterpret_cast<LPCREATESTRUCTW>(l_param)->hInstance;
		auto control = [&](const wchar_t* class_name, const wchar_t* text, DWORD style, int id = 0)
		{
			if (wcscmp(class_name, L"COMBOBOX") == 0)
				style |= CBS_OWNERDRAWFIXED | CBS_HASSTRINGS;
			if (wcscmp(class_name, L"EDIT") == 0)
				style &= ~WS_BORDER; // Shared rounded frame owns the entire border.
			HWND child = CreateWindowExW(0, class_name, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 1, 1, hwnd,
			                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
			ui::ApplyControlTheme(child);
			return child;
		};
		enabled_checkbox =
			control(L"BUTTON", L"Automatically select application profiles", WS_TABSTOP | BS_AUTOCHECKBOX, ID_ENABLE);
		return_note = control(L"STATIC", L"Switching away from a profiled program restores your previous look.", 0);
		rules_list = control(L"LISTBOX", L"",
		                     WS_BORDER | WS_TABSTOP | LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | WS_VSCROLL |
		                         LBS_NOINTEGRALHEIGHT,
		                     ID_RULES);
		exe_label = control(L"STATIC", L"Program", 0);
		executable_edit = control(L"EDIT", L"", WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL | ES_READONLY, ID_EXECUTABLE);
		browse_button = control(L"BUTTON", L"Browse...", WS_TABSTOP, ID_BROWSE);
		use_last_button = control(L"BUTTON", L"Last app", WS_TABSTOP, ID_USE_LAST_PROGRAM);
		friendly_name_label = control(L"STATIC", L"", SS_ENDELLIPSIS);
		run_button = control(L"BUTTON", L"Running programs...", WS_TABSTOP, ID_CHOOSE_RUNNING_PROGRAM);
		action_label = control(L"STATIC", L"Action", 0);
		action_choice = control(L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST, ID_ACTION);
		for (const auto text : { L"Use look", L"Bypass look", L"Pause MonitorMage", L"Pause HDR gamma" })
			SendMessageW(action_choice, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));
		SendMessageW(action_choice, CB_SETCURSEL, 0, 0);
		look_label = control(L"STATIC", L"Look", 0);
		look_choice = control(L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, ID_LOOK_CHOICE);
		for (const auto& entry : look_library)
			SendMessageW(look_choice, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(entry.name.c_str()));
		SendMessageW(look_choice, CB_SETCURSEL, 0, 0);
		SendMessageW(enabled_checkbox, BM_SETCHECK, draft_configuration.Enabled ? BST_CHECKED : BST_UNCHECKED, 0);
		save_rule_button = control(L"BUTTON", L"Add / update rule", WS_TABSTOP, ID_SAVE_RULE);
		remove_rule_button = control(L"BUTTON", L"Remove rule", WS_TABSTOP, ID_REMOVE_RULE);
		new_rule_button = control(L"BUTTON", L"New rule", WS_TABSTOP, ID_NEW_RULE);
		note = control(L"STATIC", L"Choose a program and an action. Click Add / update rule, then Apply to save.", 0);
		apply_button = control(L"BUTTON", L"Apply", WS_TABSTOP | BS_DEFPUSHBUTTON, ID_APPLY);
		close_button = control(L"BUTTON", L"Close", WS_TABSTOP, ID_CLOSE);
		picker_title = control(L"STATIC", L"Choose a running program", 0);
		sort_programs_choice = control(L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST, ID_SORT_PROGRAMS);
		for (const auto* text : { L"Memory usage", L"Name" })
			SendMessageW(sort_programs_choice, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));
		SendMessageW(sort_programs_choice, CB_SETCURSEL, 0, 0);
		include_background_checkbox =
			control(L"BUTTON", L"Background apps", WS_TABSTOP | BS_AUTOCHECKBOX, ID_INCLUDE_BACKGROUND);
		refresh_programs_button = control(L"BUTTON", L"Refresh", WS_TABSTOP, ID_REFRESH_PROGRAMS);
		program_list = control(L"LISTBOX", L"Running user programs",
		                       WS_BORDER | WS_TABSTOP | LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | WS_VSCROLL |
		                           LBS_NOINTEGRALHEIGHT,
		                       ID_PROGRAM_LIST);
		program_path = control(L"EDIT", L"", WS_BORDER | WS_TABSTOP | ES_READONLY | ES_AUTOHSCROLL);
		picker_note = control(L"STATIC", L"", SS_ENDELLIPSIS);
		choose_program_button = control(L"BUTTON", L"Choose", WS_TABSTOP, ID_CHOOSE_PROGRAM);
		cancel_program_button = control(L"BUTTON", L"Back", WS_TABSTOP, ID_CANCEL_PROGRAM);
		const std::pair<HWND, const wchar_t*> help[] { { enabled_checkbox, L"Automatic profile selection" },
			                                           { rules_list, L"Program rules" },
			                                           { executable_edit, L"Program location" },
			                                           { browse_button, L"Browse for a program" },
			                                           { use_last_button, L"Use last active program" },
			                                           { run_button, L"Choose a running program" },
			                                           { action_choice, L"Action for this program" },
			                                           { look_choice, L"Preset for this program" },
			                                           { save_rule_button, L"Add / update rule" },
			                                           { remove_rule_button, L"Remove selected rule" },
			                                           { new_rule_button, L"New program rule" },
			                                           { apply_button, L"Save and apply profiles" },
			                                           { close_button, L"Close profiles" },
			                                           { program_list, L"Running programs" },
			                                           { sort_programs_choice, L"Sort programs" },
			                                           { include_background_checkbox, L"Include background programs" },
			                                           { refresh_programs_button, L"Refresh running programs" },
			                                           { program_path, L"Selected program location" },
			                                           { choose_program_button, L"Choose this program" },
			                                           { cancel_program_button, L"Back to profile rules" } };
		for (const auto& item : help)
			ui::SetHelpText(item.first, item.second);
		EnableWindow(use_last_button, !last_application.empty());
		RefreshRules();
		UpdateActionControls();
		LayoutEditor();
		return 0;
	}
	if (message == WM_COMMAND)
	{
		const int id = LOWORD(w_param), event = HIWORD(w_param);
		if (id == ID_CHOOSE_RUNNING_PROGRAM)
		{
			picking = true;
			LayoutEditor();
			RefreshProgramsList(true);
			SetFocus(program_list);
			return 0;
		}
		if (id == ID_CANCEL_PROGRAM || (picking && id == IDCANCEL))
		{
			picking = false;
			LayoutEditor();
			SetFocus(run_button);
			return 0;
		}
		if (id == ID_REFRESH_PROGRAMS || id == ID_INCLUDE_BACKGROUND)
		{
			RefreshProgramsList(true);
			return 0;
		}
		if (id == ID_SORT_PROGRAMS && event == CBN_SELCHANGE)
		{
			RefreshProgramsList(false);
			return 0;
		}
		if (id == ID_PROGRAM_LIST && event == LBN_SELCHANGE)
		{
			UpdatePickerSelection();
			return 0;
		}
		if (id == ID_CHOOSE_PROGRAM || (id == ID_PROGRAM_LIST && event == LBN_DBLCLK) || (picking && id == IDOK))
		{
			const auto row = SendMessageW(program_list, LB_GETCURSEL, 0, 0);
			if (row >= 0 && static_cast<size_t>(row) < running_programs.size())
			{
				SetProgramFields(running_programs[row].path);
				picking = false;
				LayoutEditor();
				SetFocus(action_choice);
			}
			return 0;
		}
		if (id == ID_CLOSE || id == IDCANCEL)
		{
			DestroyWindow(hwnd);
			return 0;
		}
		if (id == ID_ACTION && event == CBN_SELCHANGE)
			UpdateActionControls();
		if (id == ID_USE_LAST_PROGRAM)
			SetProgramFields(last_application);
		if (id == ID_BROWSE)
		{
			wchar_t file[32768] {};
			OPENFILENAMEW dialog {};
			dialog.lStructSize = sizeof(dialog);
			dialog.hwndOwner = hwnd;
			dialog.lpstrFilter = L"Programs (*.exe)\0*.exe\0\0";
			dialog.lpstrFile = file;
			dialog.nMaxFile = static_cast<DWORD>(std::size(file));
			dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
			if (GetOpenFileNameW(&dialog))
				SetProgramFields(file);
		}
		if (id == ID_NEW_RULE)
		{
			SetProgramFields({});
			SendMessageW(rules_list, LB_SETCURSEL, static_cast<WPARAM>(-1), 0);
			EnableWindow(remove_rule_button, FALSE);
		}
		if (id == ID_RULES && event == LBN_SELCHANGE)
		{
			const auto index = SendMessageW(rules_list, LB_GETCURSEL, 0, 0);
			if (index >= 0 && static_cast<size_t>(index) < draft_configuration.Rules.size())
			{
				const auto& rule = draft_configuration.Rules[index];
				SetProgramFields(rule.Executable);
				SendMessageW(action_choice, CB_SETCURSEL, static_cast<int>(rule.Action), 0);
				SelectLookInCombo(look_choice, rule.Look);
				EnableWindow(remove_rule_button, TRUE);
				UpdateActionControls();
			}
		}
		if (id == ID_REMOVE_RULE)
		{
			const auto index = SendMessageW(rules_list, LB_GETCURSEL, 0, 0);
			if (index >= 0 && static_cast<size_t>(index) < draft_configuration.Rules.size())
			{
				draft_configuration.Rules.erase(draft_configuration.Rules.begin() + index);
				RefreshRules();
				SetProgramFields({});
			}
		}
		try
		{
			if (id == ID_SAVE_RULE)
			{
				applications::Rule rule;
				rule.Executable = ControlText(executable_edit);
				rule.Action = static_cast<applications::Action>(SendMessageW(action_choice, CB_GETCURSEL, 0, 0));
				if (rule.Action == applications::Action::Look)
					rule.Look = SelectedLookPath(look_choice);
				auto candidate = draft_configuration;
				size_t index = candidate.Rules.size();
				for (size_t i = 0; i < candidate.Rules.size(); ++i)
					if (applications::SamePath(candidate.Rules[i].Executable, rule.Executable))
						index = i;
				if (index == candidate.Rules.size())
					candidate.Rules.push_back(rule);
				else
					candidate.Rules[index] = rule;
				applications::Validate(candidate);
				draft_configuration = candidate;
				RefreshRules(static_cast<int>(index));
				SetNote(L"Rule updated. Click Apply to save your changes.");
			}
			if (id == ID_APPLY || id == IDOK)
			{
				auto candidate = draft_configuration;
				candidate.Enabled = SendMessageW(enabled_checkbox, BM_GETCHECK, 0, 0) == BST_CHECKED;
				applications::Validate(candidate);
				if (apply_changes(candidate))
				{
					draft_configuration = candidate;
					SetNote(L"Saved. A look you choose yourself lasts until you switch programs.");
				}
				else
					SetNote(L"Profiles could not be saved. Check the message in the main window.");
			}
		}
		catch (const std::exception& error)
		{
			SetNote(ui::messages::FriendlyError(error.what()));
		}
		return 0;
	}
	if (message == WM_DPICHANGED)
	{
		const auto rect = reinterpret_cast<RECT*>(l_param);
		SetWindowPos(hwnd, nullptr, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top,
		             SWP_NOZORDER | SWP_NOACTIVATE);
		LayoutEditor();
		ui::ApplyWindowFrameTheme(hwnd);
		return 0;
	}
	if (message == WM_CLOSE)
	{
		DestroyWindow(hwnd);
		return 0;
	}
	if (message == WM_DESTROY)
	{
		if (ui_font)
			DeleteObject(ui_font);
		ui_font = nullptr;
		editor_window = nullptr;
		apply_changes = {};
		running_programs.clear();
		picking = false;
		return 0;
	}
	return DefWindowProcW(hwnd, message, w_param, l_param);
}
}

void RefreshAppearance()
{
	if (editor_window)
	{
		LayoutEditor();
		ui::RefreshCaptionFont(editor_window);
		InvalidateRect(editor_window, nullptr, TRUE);
	}
}

HWND ProfileEditorHandle()
{
	return editor_window;
}

void PreviewPrograms(const std::vector<programs::Program>& entries)
{
	if (editor_window)
	{
		running_programs = entries;
		picking = true;
		LayoutEditor();
		RefreshProgramsList(false);
		SendMessageW(program_list, LB_SETCURSEL, 0, 0);
		UpdatePickerSelection();
	}
}

HWND ShowProfileEditor(HWND owner, HINSTANCE instance, const applications::Configuration& config,
                       const std::vector<Look>& looks, const std::filesystem::path& previous,
                       std::function<bool(const applications::Configuration&)> apply, bool hidden)
{
	if (editor_window)
	{
		if (!hidden)
		{
			ShowWindow(editor_window, SW_SHOW);
			SetForegroundWindow(editor_window);
		}
		return editor_window;
	}
	draft_configuration = config;
	look_library = looks;
	last_application = previous;
	apply_changes = std::move(apply);
	picking = false;
	WNDCLASSW window_class {};
	window_class.hInstance = instance;
	window_class.lpfnWndProc = ProfileEditorWindowProc;
	window_class.lpszClassName = L"LutApplicationProfiles";
	window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
	window_class.hbrBackground = ui::BackgroundBrush();
	window_class.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_PROFILE_DIAMOND));
	if (!RegisterClassW(&window_class) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
		return nullptr;
	const UINT dpi = GetDpiForWindow(owner);
	RECT rect { 0, 0, MulDiv(PANEL_WIDTH, dpi, 96), MulDiv(PANEL_HEIGHT, dpi, 96) };
	constexpr DWORD WINDOW_STYLE = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
	ui::AdjustThemedWindowRect(rect, dpi);
	RECT parent {};
	GetWindowRect(owner, &parent);
	editor_window =
		CreateWindowExW(0, window_class.lpszClassName, L"MonitorMage - application profiles", WINDOW_STYLE, parent.left,
	                    parent.top, rect.right - rect.left, rect.bottom - rect.top, owner, nullptr, instance, nullptr);
	if (editor_window)
		SendMessageW(editor_window, WM_SETICON, ICON_SMALL,
		             reinterpret_cast<LPARAM>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_PROFILE_DIAMOND), IMAGE_ICON,
		                                                 MulDiv(16, dpi, 96), MulDiv(16, dpi, 96), LR_SHARED)));
	if (editor_window && !hidden)
	{
		ShowWindow(editor_window, SW_SHOW);
		SetForegroundWindow(editor_window);
	}
	return editor_window;
}
}
