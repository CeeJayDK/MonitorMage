// The curve graph: drawing it, dragging its points, and LUT export and import.
#include "app_state.h"

namespace app
{
void ExportCurve()
{
	wchar_t file[32768] {};
	wcscpy_s(file, selected_effect == effect::ContrastCurves ? L"creative-curves.csv" : L"compiled-curves.csv");
	OPENFILENAMEW dialog {};
	dialog.lStructSize = sizeof(dialog);
	dialog.hwndOwner = main_window;
	dialog.lpstrFilter = L"Curve CSV (*.csv)\0*.csv\0\0";
	dialog.lpstrFile = file;
	dialog.nMaxFile = static_cast<DWORD>(std::size(file));
	dialog.lpstrDefExt = L"csv";
	dialog.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
	if (!GetSaveFileNameW(&dialog))
		return;
	HANDLE handle = CreateFileW(file, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (handle == INVALID_HANDLE_VALUE)
	{
		MessageBoxW(main_window, L"Choose a different filename to keep the existing file.", L"Export curve",
		            MB_OK | MB_ICONINFORMATION);
		return;
	}
	std::ostringstream csv;
	csv.imbue(std::locale::classic());
	csv << std::setprecision(17);
	csv << "input,output_red,output_green,output_blue,model,contrast,pivot,sdr_white_nits,gamma,hdr_fix,bypass,contrast_enabled,correction_first\n";
	const auto output = OutputForPreview();
	const auto settings = CurrentSettings();
	const bool hdr_model = output.hdr;
	for (int i = 0; i < 1024; ++i)
	{
		const double x = i / 1023.0;
		csv << x << ',' << live::EvaluateOutputCode(x, 0, output, settings) << ','
			<< live::EvaluateOutputCode(x, 1, output, settings) << ','
			<< live::EvaluateOutputCode(x, 2, output, settings) << ','
			<< (hdr_model ? (live_session.IsActive() ? "combined_hdr_pq" : "hdr_pq") : "creative_1d") << ',';
		csv << ContrastAmount() << ',' << ContrastPivot() << ',';
		if (hdr_model)
			csv << (live_session.IsActive() ? live_output.white : white_position) << ',' << HdrGamma();
		else
			csv << ',';
		csv << ',' << IsHdrFixActive() << ',' << IsLookBypassed() << ',' << contrast_enabled << ',' << correction_first
			<< '\n';
	}
	const auto data = csv.str();
	DWORD written = 0;
	const bool ok =
		WriteFile(handle, data.data(), static_cast<DWORD>(data.size()), &written, nullptr) && written == data.size();
	CloseHandle(handle);
	MessageBoxW(main_window,
	            ok ? L"Curve exported." : L"The curve could not be saved completely. Check free space and try again.",
	            L"Export curve", MB_OK | (ok ? MB_ICONINFORMATION : MB_ICONERROR));
}

void ImportOrExportLut(bool importing)
{
	if (render_only)
		return;
	wchar_t file[32768] {};
	if (!importing)
		wcscpy_s(file, L"MonitorMage-look");
	OPENFILENAMEW dialog { sizeof(dialog) };
	dialog.hwndOwner = main_window;
	dialog.lpstrFile = file;
	dialog.nMaxFile = static_cast<DWORD>(std::size(file));
	dialog.lpstrFilter =
		importing
			? L"All supported LUTs\0*.cube;*.spi1d;*.cal;*.csv;*.png;*.tif;*.tiff\0Cube 1D (*.cube)\0*.cube\0OpenColorIO (*.spi1d)\0*.spi1d\0Argyll display curves (*.cal)\0*.cal\0RGB CSV (*.csv)\0*.csv\0PNG LUT image (*.png)\0*.png\0TIFF LUT image (*.tif;*.tiff)\0*.tif;*.tiff\0\0"
			: L"Cube 1D (*.cube)\0*.cube\0OpenColorIO (*.spi1d)\0*.spi1d\0Argyll curves (*.cal)\0*.cal\0RGB CSV (*.csv)\0*.csv\0PNG 16-bit ramp (*.png)\0*.png\0TIFF 16-bit ramp (*.tif)\0*.tif\0\0";
	dialog.nFilterIndex = 1;
	dialog.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR | (importing ? OFN_FILEMUSTEXIST : OFN_OVERWRITEPROMPT);
	if (!(importing ? GetOpenFileNameW(&dialog) : GetSaveFileNameW(&dialog)))
		return;
	try
	{
		std::filesystem::path path(file);
		if (importing)
		{
			auto loaded = lut1d::Read(path);
			CurrentPreset(preset_role::Look).current.Adjustments.Imported = std::move(loaded.channels);
			OnPresetEdited(preset_role::Look);
			preset_message = L"Color curves imported.";
			ApplyLiveCorrections();
		}
		else
		{
			if (!path.has_extension())
			{
				const wchar_t* extensions[] { L".cube", L".spi1d", L".cal", L".csv", L".png", L".tif" };
				path += extensions[std::clamp(static_cast<int>(dialog.nFilterIndex) - 1, 0, 5)];
				if (std::filesystem::exists(path) &&
				    MessageBoxW(main_window, L"Replace the existing LUT file?", L"Export look LUT",
				                MB_YESNO | MB_ICONQUESTION) != IDYES)
					return;
			}
			auto settings = CurrentSettings();
			settings.HdrFix = false;
			settings.Bypass = false;
			settings.NightWarmth = 0;
			Source output;
			lut1d::Channels channels;
			for (int i = 0; i < 1024; ++i)
				for (int c = 0; c < 3; ++c)
					channels[c].push_back(live::EvaluateOutputCode(i / 1023.0, c, output, settings));
			lut1d::Write(path, channels);
			preset_message =
				L"Color curves exported. Saturation, Channel mixer and monitor corrections are not included.";
		}
	}
	catch (const std::exception& error)
	{
		ShowPresetError(error);
	}
	RefreshInterface();
}

MonitorGeometry CalculateMonitorGeometry(HWND hwnd)
{
	RECT client {};
	GetClientRect(hwnd, &client);
	MonitorGeometry geometry {};
	geometry.body = { 0, 0, client.right, client.bottom - ScaleForDpi(30) };
	geometry.screen = geometry.body;
	InflateRect(&geometry.screen, -ScaleForDpi(6), -ScaleForDpi(6));
	// Six DIP of internal padding plus a two-DIP inset gives an eight-DIP
	// visible corner gap (previously sixteen), retaining rounded screen corners.
	geometry.size = ScaleForDpi(24);
	const int inset = ScaleForDpi(2);
	geometry.left = geometry.screen.left + inset;
	geometry.right = geometry.screen.right - inset - geometry.size;
	geometry.top = geometry.screen.top + inset;
	geometry.bottom = geometry.screen.bottom - inset - geometry.size;
	// Reserve a small axis-label strip above the separate profile row.
	geometry.plot = { geometry.left + geometry.size / 2 + ScaleForDpi(20),
		              geometry.top + geometry.size / 2 + ScaleForDpi(20),
		              geometry.right + geometry.size / 2 - ScaleForDpi(20),
		              geometry.bottom + geometry.size / 2 - ScaleForDpi(32) };
	const int center = client.right / 2;
	geometry.foot = { center - ScaleForDpi(57), client.bottom - ScaleForDpi(15), center + ScaleForDpi(57),
		              client.bottom - ScaleForDpi(3) };
	return geometry;
}

RECT CurvePlotRect(HWND hwnd)
{
	return CalculateMonitorGeometry(hwnd).plot;
}

RECT GraphUpdateBounds(HWND hwnd)
{
	auto area = CurvePlotRect(hwnd);
	InflateRect(&area, ScaleForDpi(8), ScaleForDpi(8));
	RECT client {}, visible {};
	GetClientRect(hwnd, &client);
	IntersectRect(&visible, &area, &client);
	return visible;
}

static RECT GraphWarningBounds(HWND hwnd, const wchar_t* text)
{
	const auto g = CalculateMonitorGeometry(hwnd);
	RECT area { g.left + g.size + ScaleForDpi(12), g.top, g.right - ScaleForDpi(12), g.bottom }, measured = area;
	HDC dc = GetDC(hwnd);
	const auto old = SelectObject(dc, ui_font);
	DrawTextW(dc, text, -1, &measured, DT_CENTER | DT_WORDBREAK | DT_CALCRECT | DT_NOPREFIX);
	SelectObject(dc, old);
	ReleaseDC(hwnd, dc);
	area.top += std::max(0, (g.size - static_cast<int>(measured.bottom - measured.top)) / 2);
	area.bottom = std::min<LONG>(g.bottom, area.top + measured.bottom - measured.top);
	return area;
}

void InvalidateGraph()
{
	if (!curve_canvas || !ui_font)
		return;
	if (render_only)
		graph_audit_count = 0;
	const auto queue = [&](const RECT& area)
	{
		if (render_only && graph_audit_count < graph_audit_damage.size())
			graph_audit_damage[graph_audit_count++] = area;
		InvalidateRect(curve_canvas, &area, FALSE);
	};
	const RECT plot = GraphUpdateBounds(curve_canvas);
	queue(plot);
	static std::wstring previous;
	static RECT old_bounds {};
	static bool had_warning = false;
	static unsigned previous_layout = 0;
	wchar_t text[2048] {};
	if (graph_status_visible)
		GetWindowTextW(status_label, text, 2048);
	if (had_warning != graph_status_visible || previous != text || previous_layout != graph_layout_version)
	{
		if (had_warning)
			queue(old_bounds);
		if (graph_status_visible)
		{
			old_bounds = GraphWarningBounds(curve_canvas, text);
			queue(old_bounds);
		}
		previous = text;
		had_warning = graph_status_visible;
		previous_layout = graph_layout_version;
	}
}

static void ChangeCurvePoint(HWND hwnd, LPARAM point)
{
	if (selected_effect != effect::CustomCurves)
		return;
	const RECT plot = CurvePlotRect(hwnd);
	auto& curve = CurrentPreset(preset_role::Look).current.Adjustments.Curves[editing_channel];
	if (curve.empty())
	{
		for (int i = 0; i < 9; ++i)
			curve.push_back(i / 8.0);
	}
	if (dragged_point < 0)
		dragged_point = std::clamp(
			static_cast<int>(std::lround((GET_X_LPARAM(point) - plot.left) * 8.0 / (plot.right - plot.left))), 0, 8);
	keyboard_point = dragged_point;
	const auto index = static_cast<size_t>(dragged_point);
	const double low = index ? curve[index - 1] : 0, high = index + 1 < curve.size() ? curve[index + 1] : 1;
	curve[index] =
		std::clamp((plot.bottom - GET_Y_LPARAM(point)) / static_cast<double>(plot.bottom - plot.top), low, high);
	OnPresetEdited(preset_role::Look);
	RefreshInterface();
	if (corrections_requested)
		SetTimer(main_window, TIMER_APPLY, 100, nullptr);
}

LRESULT CALLBACK CurveWindowProc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param)
{
	if (message == WM_DRAWITEM && reinterpret_cast<DRAWITEMSTRUCT*>(l_param)->hwndItem == website_link)
	{
		const auto& item = *reinterpret_cast<DRAWITEMSTRUCT*>(l_param);
		const int saved = SaveDC(item.hDC);
		IntersectClipRect(item.hDC, item.rcItem.left, item.rcItem.top, item.rcItem.right, item.rcItem.bottom);
		HBRUSH back = CreateSolidBrush(RGB(80, 87, 96));
		FillRect(item.hDC, &item.rcItem, back);
		DeleteObject(back);
		LOGFONTW style {};
		GetObjectW(ui_font, sizeof(style), &style);
		style.lfHeight = -ScaleForDpi(8);
		style.lfUnderline = GetPropW(website_link, L"LutProfileHover") != nullptr;
		HFONT small = CreateFontIndirectW(&style);
		const auto old = SelectObject(item.hDC, small ? small : ui_font);
		SetBkMode(item.hDC, TRANSPARENT);
		SetTextColor(item.hDC, RGB(0, 0, 0));
		RECT area = item.rcItem;
		DrawTextW(item.hDC, L"CeeJay.dk", -1, &area, DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
		if (GetFocus() == website_link)
			DrawFocusRect(item.hDC, &area);
		SelectObject(item.hDC, old);
		if (small)
			DeleteObject(small);
		if (saved)
			RestoreDC(item.hDC, saved);
		return TRUE;
	}
	if (message == WM_SETCURSOR && reinterpret_cast<HWND>(w_param) == hwnd && LOWORD(l_param) == HTCLIENT &&
	    selected_effect == effect::CustomCurves && !settings_page)
	{
		POINT point {};
		GetCursorPos(&point);
		ScreenToClient(hwnd, &point);
		const auto plot = CurvePlotRect(hwnd);
		if (PtInRect(&plot, point) || dragged_point >= 0)
		{
			SetCursor(LoadCursorW(nullptr, IDC_CROSS));
			return TRUE;
		}
	}
	if (message == WM_DRAWITEM && (reinterpret_cast<DRAWITEMSTRUCT*>(l_param)->hwndItem == monitor_mode ||
	                               reinterpret_cast<DRAWITEMSTRUCT*>(l_param)->hwndItem == monitor_profile))
	{
		const auto& item = *reinterpret_cast<DRAWITEMSTRUCT*>(l_param);
		const bool profile = item.hwndItem == monitor_profile;
		const int saved = SaveDC(item.hDC);
		IntersectClipRect(item.hDC, item.rcItem.left, item.rcItem.top, item.rcItem.right, item.rcItem.bottom);
		HBRUSH back = profile ? ui::BackgroundBrush() : CreateSolidBrush(RGB(7, 9, 12));
		FillRect(item.hDC, &item.rcItem, back);
		if (!profile)
			DeleteObject(back);
		const auto old = SelectObject(item.hDC, reinterpret_cast<HFONT>(SendMessageW(item.hwndItem, WM_GETFONT, 0, 0)));
		SetBkMode(item.hDC, TRANSPARENT);
		SetTextColor(item.hDC, ui::THEME_MUTED);
		wchar_t text[2048] {};
		GetWindowTextW(item.hwndItem, text, 2048);
		RECT area = item.rcItem;
		if (profile)
		{
			const bool hover = GetPropW(monitor_profile, L"LutProfileHover") != nullptr;
			SetTextColor(item.hDC, hover || GetFocus() == monitor_profile ? ui::THEME_TEXT : ui::THEME_MUTED);
			RECT measured {};
			DrawTextW(item.hDC, text, -1, &measured, DT_SINGLELINE | DT_CALCRECT | DT_NOPREFIX);
			const int icon = ScaleForDpi(12), gap = ScaleForDpi(5),
					  width = std::min(static_cast<int>(measured.right - measured.left) + ScaleForDpi(4),
			                           static_cast<int>(area.right - area.left) - icon - gap);
			const int start = area.right - width - icon - gap, mid = (area.top + area.bottom) / 2;
			ui::PaintMenuGlyph(monitor_profile, item.hDC, { start, mid - icon / 2, start + icon, mid + (icon + 1) / 2 },
			                   ui::Glyph::Diamond, application_context_active && !foreground_program.IsManualOverride(),
			                   true, ui::THEME_BACKGROUND);
			area.left = start + icon + gap;
			area.right = area.left + width;
			DrawTextW(item.hDC, text, -1, &area, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
			if (hover)
			{
				HPEN pen = CreatePen(PS_SOLID, 1, ui::THEME_FOCUS_BORDER);
				const auto previous = SelectObject(item.hDC, pen);
				const int y = mid + ScaleForDpi(8);
				MoveToEx(item.hDC, area.left, y, nullptr);
				LineTo(item.hDC, area.right, y);
				SelectObject(item.hDC, previous);
				DeleteObject(pen);
			}
			if (GetFocus() == monitor_profile)
				DrawFocusRect(item.hDC, &area);
		}
		else
			DrawTextW(item.hDC, text, -1, &area,
			          DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX | DT_CENTER);
		SelectObject(item.hDC, old);
		if (saved)
			RestoreDC(item.hDC, saved);
		return TRUE;
	}
	LRESULT themed = 0;
	if ((message == WM_NOTIFY || message == WM_DRAWITEM || message == WM_MEASUREITEM ||
	     (message >= WM_CTLCOLORMSGBOX && message <= WM_CTLCOLORSTATIC)) &&
	    ui::HandleThemeMessage(hwnd, message, w_param, l_param, themed))
		return themed;
	if (message == WM_COMMAND)
		return SendMessageW(GetParent(hwnd), message, w_param, l_param);
	if (message == WM_NOTIFY)
		return SendMessageW(GetParent(hwnd), message, w_param, l_param);
	if (message == WM_SIZE)
	{
		LayoutMonitorControls();
		InvalidateRect(hwnd, nullptr, FALSE);
		return 0;
	}
	if (message == WM_SETFOCUS || message == WM_KILLFOCUS)
	{
		InvalidateGraph();
		return 0;
	}
	if (selected_effect == effect::CustomCurves)
	{
		if (message == WM_GETDLGCODE)
			return DLGC_WANTARROWS;
		if (message == WM_LBUTTONDBLCLK)
		{
			const auto plot = CurvePlotRect(hwnd);
			POINT point { GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param) };
			auto hit = plot;
			InflateRect(&hit, ScaleForDpi(6), ScaleForDpi(6));
			if (PtInRect(&hit, point))
			{
				auto& curve = CurrentPreset(preset_role::Look).current.Adjustments.Curves[editing_channel];
				if (!curve.empty())
				{
					const auto i = static_cast<size_t>(std::clamp(
						static_cast<int>(std::lround((point.x - plot.left) * 8.0 / (plot.right - plot.left))), 0, 8));
					curve[i] = std::clamp(i / 8.0, i ? curve[i - 1] : 0, i + 1 < curve.size() ? curve[i + 1] : 1);
					keyboard_point = static_cast<int>(i);
					dragged_point = -1;
					slider_dragging = false;
					if (GetCapture() == hwnd)
						ReleaseCapture();
					OnPresetEdited(preset_role::Look);
					ApplyLiveCorrections();
					RefreshInterface();
				}
			}
			return 0;
		}
		if (message == WM_LBUTTONDOWN)
		{
			const auto plot = CurvePlotRect(hwnd);
			POINT point { GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param) };
			if (PtInRect(&plot, point))
			{
				SetFocus(hwnd);
				look_dirty_before_drag = CurrentPreset(preset_role::Look).IsDirty();
				slider_dragging = true;
				KillTimer(main_window, TIMER_PRESET);
				dragged_point = -1;
				SetCapture(hwnd);
				ChangeCurvePoint(hwnd, l_param);
			}
			return 0;
		}
		if (message == WM_MOUSEMOVE && GetCapture() == hwnd)
		{
			ChangeCurvePoint(hwnd, l_param);
			return 0;
		}
		if (message == WM_LBUTTONUP || message == WM_CAPTURECHANGED || message == WM_CANCELMODE)
		{
			if (dragged_point >= 0)
			{
				dragged_point = -1;
				slider_dragging = false;
				if (GetCapture() == hwnd)
					ReleaseCapture();
				OnPresetEdited(preset_role::Look);
				RefreshInterface();
			}
			return 0;
		}
		if (message == WM_RBUTTONUP)
		{
			CurrentPreset(preset_role::Look).current.Adjustments.Curves[editing_channel].clear();
			OnPresetEdited(preset_role::Look);
			ApplyLiveCorrections();
			RefreshInterface();
			return 0;
		}
		if (message == WM_KEYDOWN &&
		    (w_param == VK_UP || w_param == VK_DOWN || w_param == VK_LEFT || w_param == VK_RIGHT))
		{
			auto& curve = CurrentPreset(preset_role::Look).current.Adjustments.Curves[editing_channel];
			if (curve.empty())
				for (int i = 0; i < 9; ++i)
					curve.push_back(i / 8.0);
			if (w_param == VK_LEFT || w_param == VK_RIGHT)
				keyboard_point = std::clamp(keyboard_point + (w_param == VK_RIGHT ? 1 : -1), 0, 8);
			else
			{
				const size_t i = static_cast<size_t>(keyboard_point);
				curve[i] = std::clamp(curve[i] + (w_param == VK_UP ? .01 : -.01), i ? curve[i - 1] : 0,
				                      i + 1 < curve.size() ? curve[i + 1] : 1);
				OnPresetEdited(preset_role::Look);
				if (corrections_requested)
					SetTimer(main_window, TIMER_APPLY, 100, nullptr);
			}
			RefreshInterface();
			return 0;
		}
	}
	if (message == WM_ERASEBKGND)
		return 1;
	if (message != WM_PAINT && message != WM_PRINTCLIENT)
		return DefWindowProcW(hwnd, message, w_param, l_param);
	PAINTSTRUCT paint {};
	HDC dc = message == WM_PAINT ? BeginPaint(hwnd, &paint) : reinterpret_cast<HDC>(w_param);
	RECT client {};
	GetClientRect(hwnd, &client);
	// Buffer only while painting; no persistent frame loop or render resources.
	paint::Buffer frame(dc, client);
	// Keep the previous image if off-screen allocation fails; never fall
	// back to drawing the graph directly into the visible window.
	if (!frame || !frame.IsBuffered())
	{
		frame.Finish();
		if (message == WM_PAINT)
			EndPaint(hwnd, &paint);
		return 0;
	}
	if (render_only)
		last_graph_paint_bytes = frame.ByteCount();
	HDC buffer = frame.DeviceContext();
	FillRect(buffer, &client, ui::BackgroundBrush());
	const auto geometry = CalculateMonitorGeometry(hwnd);
	const RECT body = geometry.body;
	auto rounded = [&](RECT rect, COLORREF fill, COLORREF outline, int radius)
	{
		HBRUSH brush = CreateSolidBrush(fill);
		HPEN pen = CreatePen(PS_SOLID, 1, outline);
		auto old_brush = SelectObject(buffer, brush), old_pen = SelectObject(buffer, pen);
		RoundRect(buffer, rect.left, rect.top, rect.right, rect.bottom, ScaleForDpi(radius * 2),
		          ScaleForDpi(radius * 2));
		SelectObject(buffer, old_brush);
		SelectObject(buffer, old_pen);
		DeleteObject(brush);
		DeleteObject(pen);
	};
	const int center = client.right / 2;
	RECT neck { center - ScaleForDpi(17), body.bottom - ScaleForDpi(2), center + ScaleForDpi(17),
		        client.bottom - ScaleForDpi(7) };
	const RECT foot = geometry.foot;
	rounded(neck, RGB(65, 70, 77), RGB(86, 92, 100), 2);
	rounded(foot, RGB(80, 87, 96), RGB(112, 120, 130), 3);
	rounded(body, RGB(82, 89, 98), RGB(120, 128, 138), 7);
	const RECT screen = geometry.screen;
	rounded(screen, RGB(7, 9, 12), RGB(32, 36, 42), 3);
	const auto plot_rect = CurvePlotRect(hwnd);
	const int left = plot_rect.left, right = plot_rect.right, top = plot_rect.top, bottom = plot_rect.bottom;
	HPEN grid = CreatePen(PS_SOLID, 1, RGB(58, 62, 69));
	HGDIOBJ old_pen = SelectObject(buffer, grid);
	for (int i = 0; i <= 4; ++i)
	{
		const int x = left + (right - left) * i / 4, y = bottom - (bottom - top) * i / 4;
		MoveToEx(buffer, x, top, nullptr);
		LineTo(buffer, x, bottom);
		MoveToEx(buffer, left, y, nullptr);
		LineTo(buffer, right, y);
	}
	HPEN identity = CreatePen(PS_DOT, 1, ui::THEME_BORDER);
	SelectObject(buffer, identity);
	MoveToEx(buffer, left, bottom, nullptr);
	LineTo(buffer, right, top);
	const auto settings = CurrentSettings();
	const auto output = OutputForPreview();
	const COLORREF inks[] { RGB(255, 0, 0), RGB(0, 255, 0), RGB(0, 0, 255) };
	plot::Paths paths;
	std::vector<POINT> editable_path;
	bool needs_guide = false;
	for (auto& path : paths)
		path.reserve(513);
	if (selected_effect == effect::CustomCurves)
		editable_path.reserve(513);
	const auto position = [&](double x, double y)
	{
		return POINT { left + static_cast<int>(std::lround((right - left) * x)),
			           bottom - static_cast<int>(std::lround((bottom - top) * y)) };
	};
	for (int i = 0; i <= 512; ++i)
	{
		const double x = i / 512.0;
		POINT editable {};
		if (selected_effect == effect::CustomCurves)
		{
			editable = position(x, color::SampleSpline(settings.Adjustments.Curves[editing_channel], x));
			editable_path.push_back(editable);
		}
		for (int c = 0; c < 3; ++c)
		{
			// In the editor show the Master + RGB transform even when its
			// switch is off, so editing remains possible before enabling it.
			const double y = selected_effect == effect::CustomCurves
			                     ? color::SampleSpline(settings.Adjustments.Curves[c + 1],
			                                           color::SampleSpline(settings.Adjustments.Curves[0], x))
			                     : live::EvaluateOutputCode(x, c, output, settings);
			paths[c].push_back(position(x, y));
			if (selected_effect == effect::CustomCurves && (editing_channel == 0 || editing_channel == c + 1) &&
			    std::abs(paths[c].back().y - editable.y) > 1)
				needs_guide = true;
		}
	}
	if (selected_effect == effect::CustomCurves && needs_guide)
	{
		HPEN guide = CreatePen(PS_DOT, 1, ui::THEME_MUTED);
		SelectObject(buffer, guide);
		Polyline(buffer, editable_path.data(), static_cast<int>(editable_path.size()));
		SelectObject(buffer, old_pen);
		DeleteObject(guide);
	}
	plot::DrawRgbCurves(buffer, client.right, client.bottom, paths, ScaleForDpi(2), &curve_scratch);
	if (selected_effect == effect::CustomCurves)
	{
		const auto& editable = settings.Adjustments.Curves[editing_channel];
		HPEN handles =
			CreatePen(PS_SOLID, ScaleForDpi(2), editing_channel ? inks[editing_channel - 1] : RGB(255, 255, 255));
		SelectObject(buffer, handles);
		const auto previous_brush = SelectObject(buffer, GetStockObject(BLACK_BRUSH));
		for (int i = 0; i < 9; ++i)
		{
			const auto at = position(i / 8.0, color::SampleSpline(editable, i / 8.0));
			const int x = at.x, y = at.y;
			Ellipse(buffer, x - ScaleForDpi(3), y - ScaleForDpi(3), x + ScaleForDpi(4), y + ScaleForDpi(4));
			if (GetFocus() == hwnd && i == keyboard_point)
			{
				RECT focus { x - ScaleForDpi(6), y - ScaleForDpi(6), x + ScaleForDpi(7), y + ScaleForDpi(7) };
				DrawFocusRect(buffer, &focus);
			}
		}
		SelectObject(buffer, previous_brush);
		SelectObject(buffer, old_pen);
		DeleteObject(handles);
	}
	SelectObject(buffer, old_pen);
	DeleteObject(grid);
	DeleteObject(identity);
	LOGFONTW axis_style {};
	GetObjectW(ui_font, sizeof(axis_style), &axis_style);
	axis_style.lfHeight = -ScaleForDpi(ui::style::SECONDARY_TEXT_SIZE);
	HFONT axis_font = CreateFontIndirectW(&axis_style);
	HGDIOBJ old_font = SelectObject(buffer, axis_font ? axis_font : ui_font);
	SetBkMode(buffer, TRANSPARENT);
	SetTextColor(buffer, ui::THEME_TEXT);
	const auto axis = [&](const wchar_t* value, int cx, int cy)
	{
		RECT area { cx - ScaleForDpi(8), cy - ScaleForDpi(8), cx + ScaleForDpi(8), cy + ScaleForDpi(8) };
		DrawTextW(buffer, value, 1, &area, DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
	};
	const int lower_center = geometry.bottom + geometry.size / 2, side_center = geometry.left + geometry.size / 2;
	axis(L"0", left, lower_center);
	axis(L"1", right, lower_center);
	axis(L"1", side_center, top);
	axis(L"0", side_center, bottom);
	axis_style.lfHeight = -ScaleForDpi(ui::style::CAPTION_TEXT_SIZE);
	HFONT tone_font = CreateFontIndirectW(&axis_style);
	SelectObject(buffer, tone_font ? tone_font : ui_font);
	SetTextColor(buffer, ui::THEME_MUTED);
	const wchar_t* tones[] { L"Shadows", L"Midtones", L"Highlights" };
	for (int i = 0; i < 3; ++i)
	{
		RECT label { left + (right - left) * i / 3, bottom + ScaleForDpi(2), left + (right - left) * (i + 1) / 3,
			         bottom + ScaleForDpi(14) };
		DrawTextW(buffer, tones[i], -1, &label,
		          DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX |
		              (i == 0   ? DT_LEFT
		               : i == 1 ? DT_CENTER
		                        : DT_RIGHT));
	}
	SelectObject(buffer, old_font);
	if (tone_font)
		DeleteObject(tone_font);
	if (axis_font)
		DeleteObject(axis_font);
	// Paint last, over the graph itself: long warnings may wrap below the
	// header row without a child window clipping their second line.
	wchar_t status_text[2048] {};
	GetWindowTextW(status_label, status_text, 2048);
	RECT status_area { geometry.left + geometry.size + ScaleForDpi(12), geometry.top, geometry.right - ScaleForDpi(12),
		               geometry.bottom };
	RECT measured = status_area;
	const auto status_font = SelectObject(buffer, ui_font);
	SetBkMode(buffer, TRANSPARENT);
	SetTextColor(buffer, status_error ? ui::THEME_ERROR : ui::THEME_MUTED);
	DrawTextW(buffer, status_text, -1, &measured, DT_CENTER | DT_WORDBREAK | DT_CALCRECT | DT_NOPREFIX);
	status_area.top += std::max(0, (geometry.size - static_cast<int>(measured.bottom - measured.top)) / 2);
	if (graph_status_visible)
		DrawTextW(buffer, status_text, -1, &status_area, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
	SelectObject(buffer, status_font);
	frame.Finish();
	if (message == WM_PAINT)
		EndPaint(hwnd, &paint);
	return 0;
}
}
