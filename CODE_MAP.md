# MonitorMage code map

This page shows where the program starts, what calls what, and which file to open for a given job. It assumes you know
some C++ but have never written a Windows program.

Line numbers are left out on purpose because they go out of date. Search for the function name instead.

## What the program does

Every monitor has a small table, called a gamma ramp, that Windows uses to change each color value just before it goes
to the screen. MonitorMage works out a new table from your sliders and curves and gives it to Windows. That changes the
whole picture, in every program, without touching those programs.

Saturation and the channel mixer can't be done with that table, because one color channel can't affect another in it.
Those two effects use a separate Windows feature, a color matrix.

## Five ideas you need first

| Word | Meaning |
|---|---|
| **LUT** | Look-up table. A list that says "input value X becomes output value Y". |
| **Gamma ramp** | The LUT inside the graphics card: 256 entries for each of red, green and blue. In the code it is the type `Ramp`. |
| **HWND** | A number that identifies a window. Buttons and sliders are windows too. |
| **Message** | How Windows tells a window that something happened. `WM_COMMAND` means a button was used, `WM_TIMER` means a timer ran out, and so on. |
| **Window procedure** | The function that receives a window's messages. Here the two important ones are `MainWindowProc` and `CurveWindowProc`. |

A Windows program doesn't run from top to bottom. It starts up, then sits in a loop waiting for messages, and each
message calls some code. So after startup, the question to ask is always "which message starts this?"

## Where to start reading

1. `wWinMain` at the bottom of [src/workbench.cpp](src/workbench.cpp). It is 48 lines and is the whole startup.
2. `MainWindowProc` at the bottom of [src/app_main_window.cpp](src/app_main_window.cpp). Every message arrives here.
3. `OnCommand`, just above it. One `case` for each button, list and checkbox.
4. `ApplyLiveCorrections` in [src/app_corrections.cpp](src/app_corrections.cpp). This is where settings reach the display.
5. `EvaluateOutputCode` in [src/live_lut.cpp](src/live_lut.cpp). This is the color processing itself.
6. The top of [src/app_state.h](src/app_state.h). It lists the state shared by the whole program.

## Startup: what `wWinMain` calls

```text
wWinMain                                  src/workbench.cpp
├─ RunCommandLineTool                     helper and test modes that do one job, then exit
├─ ReadStartupOptions                     reads flags for modes that need the real window
├─ PreparePresetUiTest                    test runs only: uses a temporary preset folder
├─ CreateMutexW                           if another copy is running, show its window and exit
├─ RegisterWindowClasses                  connects the window types to their window procedures
├─ CreateMainWindow
│  └─ Windows sends WM_CREATE to MainWindowProc, which calls OnCreate
│     ├─ InitializePresets                app_presets.cpp: finds and loads presets
│     ├─ applications::Read               application_profiles.cpp: per-program rules
│     ├─ RefreshSources                   app_corrections.cpp: lists the displays
│     ├─ LoadAndBindHotkey                app_hotkey.cpp: the global shortcut
│     ├─ CreateEditorIfNeeded             creates all the controls (see the note below)
│     └─ InstallTray                      app_tray.cpp: the notification-area icon
├─ StartWatchingForegroundProgram         app_profiles.cpp is told when the active program changes
├─ TurnOnCorrections                      app_corrections.cpp: applies the saved look
└─ RunSelectedMode
   ├─ RunTrayStartupTest, RunUiBenchmark  test and benchmark modes
   ├─ RunInterfaceTest, RenderScreenshot  test and screenshot modes
   └─ RunMessageLoop                      normal use: runs until the program exits
```

When the program starts minimized to the tray, `CreateEditorIfNeeded` is skipped to save memory. It runs later, when
the window is first shown.

## After startup: which message runs what

`RunMessageLoop` passes each message to `MainWindowProc`. Before the `switch`, the theme code in
`ui::HandleThemeMessage` gets a chance to handle it, because it draws the title bar and the dark controls. Whatever is
left goes to the `switch`:

| Message | What started it | What runs |
|---|---|---|
| `WM_CREATE` | The window was created | `OnCreate` |
| `WM_COMMAND` | A button, list or checkbox was used | `OnCommand`, then one handler such as `ChoosePreset` |
| `WM_HSCROLL` | A slider moved | `OnSliderMoved` |
| `WM_TIMER` | A timer ran out | `OnTimer`, then the code for that `TIMER_` |
| `WM_HOTKEY` | The global shortcut was pressed | `ToggleBypass` |
| `MESSAGE_TRAY` | The tray icon was clicked | `ShowEditor` or `ShowTrayMenu` |
| `MESSAGE_FOREGROUND` | Another program became active | `TIMER_FOREGROUND`, then `OnForegroundTimer` |
| `MESSAGE_MATRIX_CHANGED` | The matrix helper reported a change | `RefreshInterface` |
| `WM_DISPLAYCHANGE`, `WM_SETTINGCHANGE`, `WM_DEVICECHANGE` | Display setup changed | `TIMER_DISPLAY`, then `OnDisplayTimer` |
| `WM_POWERBROADCAST` | The computer is going to sleep or waking up | `OnPowerEvent` |
| `WM_SHOWWINDOW` | The window is being shown | `CreateEditorIfNeeded`, `RefreshInterface` |
| `WM_CLOSE` | The window's X was clicked | Hides to the tray, or `ConfirmExit` if there is no tray icon |
| `WM_DESTROY` | The window is closing for good | `OnDestroy` |

The curve graph is its own child window with its own procedure, `CurveWindowProc`. It handles drawing
(`PaintCurveGraph`) and mouse edits (`StartCurveDrag`, `ChangeCurvePoint`, `FinishCurveDrag`).

## Follow one slider from mouse to screen

This is the most important path in the program. Most other changes join it partway down.

```text
You drag a slider. Windows sends WM_HSCROLL.
MainWindowProc
└─ OnSliderMoved                          app_main_window.cpp
   ├─ UpdateSliderDragState
   ├─ StoreAdjustmentSliderValue          SliderParameters says which setting each slider edits
   │  └─ OnPresetEdited                   app_presets.cpp: starts TIMER_PRESET to autosave
   ├─ RefreshInterface                    app_interface.cpp: updates labels and redraws the graph
   └─ SetTimer(TIMER_APPLY, 100 ms)       waits, so a fast drag doesn't do the work every pixel

100 ms later, Windows sends WM_TIMER.
OnTimer
└─ ApplyLiveCorrections                   app_corrections.cpp
   ├─ UpdateNightLightSchedule            works out the current night light level
   ├─ SyncColorMatrix                     saturation and channel mixer (see "Helper processes")
   │  └─ matrix_session.Update            matrix_effect.cpp
   ├─ live_session.Update(live_output, CurrentSettings())    live_lut.cpp
   │  ├─ IsRampOurs                       stops if another program changed the display
   │  ├─ BuildGammaRamp                   builds the table
   │  │  └─ EvaluateOutputCode            called once per entry: 256 × 3 times
   │  │     ├─ color::ApplyContrast       color_math.h
   │  │     ├─ color::HdrGammaCurve       color_math.h
   │  │     └─ color::ApplyStageToCode    adjustments.h
   │  ├─ SetDeviceGammaRamp               Windows applies the table to the display
   │  └─ GetDeviceGammaRamp               reads it back to prove the driver kept it
   └─ SyncNightOutputs                    runs at the end, for the other displays (via NightSync)
```

`EvaluateOutputCode` runs the effects as numbered stages, in the order the user arranged them:

| Stage | Effect | Stage | Effect |
|---:|---|---:|---|
| 0 | Contrast curves | 4 | Levels |
| 1 | HDR gamma | 5 | Shadows and highlights |
| 2 | Color balance | 6 | Custom curves |
| 3 | Midtone gamma | 7 | Imported 1D LUT |

The stage numbers are the first eight values of `effect::Id` in [src/effect_list.h](src/effect_list.h). The other
effects in that list don't go through these stages: `ColorFilters` uses the Windows color filter, `NightLight` is added
at the end of `EvaluateOutputCode`, and `Saturation` and `ChannelMixer` use the color matrix.

## Other common paths

**Power button**, or Alt+P while the window has focus:

```text
OnCommand, case ID_TEST_CONTRAST
└─ ToggleCorrections                      app_corrections.cpp
   ├─ turning off: DisableLiveCorrections
   │  └─ live_session.Stop                the original display colors are restored
   └─ turning on: ReconcileLiveCorrections
      └─ EnableLiveCorrections
         └─ live_session.Start            saves the original colors, starts the guardian, applies the table
```

**Global shortcut:** `WM_HOTKEY` calls `ToggleBypass`, which pauses the look but keeps corrections such as HDR gamma on.

**Switching to a game that has a profile:**

```text
Windows calls ForegroundChangedCallback   app_profiles.cpp
└─ posts MESSAGE_FOREGROUND, which starts TIMER_FOREGROUND
   └─ OnForegroundTimer
      └─ CheckForegroundProgram
         └─ OnForegroundProgramChanged
            └─ ApplyApplicationProfile
               ├─ applications::ResolveForProgram   finds the rule for this program
               ├─ ReadPreset                        loads that program's look
               └─ ReconcileLiveCorrections          joins the path above
```

**Display or HDR change:** `OnDisplayTimer` calls `RefreshSources`, then `ReconcileLiveCorrections`. The HDR button is
slower: `ToggleWindowsHdrMode` turns corrections off, calls `hdr::SetHdrEnabled`, and `OnHdrTimer` waits for Windows to
finish.

**Sleep:** `OnPowerEvent` calls `DisableLiveCorrections(true)` before sleep. After waking, `TIMER_DISPLAY` turns them
back on.

**Closing:** `OnDestroy` stops every session, removes the tray icon and calls `PostQuitMessage`, which ends
`RunMessageLoop`.

## Helper processes

`MonitorMage.exe` starts extra copies of itself. `RunCommandLineTool` sends those copies to their job as soon as they
start.

```text
MonitorMage.exe                           the window you see
├─ MonitorMage.exe --live-guard           one for each active LUT session
│  └─ live::RunRecoveryGuardian           sleeps until the session ends or the main program dies,
│                                         then puts the display's original colors back
└─ MonitorMage.exe --matrix-effect-worker only while saturation or the channel mixer is in use
   └─ matrix_effect::RunWorkerProcess     loads Magnification.dll, so the main program never has to
```

The guardian is why a crash doesn't leave your display stuck with the wrong colors. The main program and its helpers
share data through a block of shared memory (`Shared` in the code).

## The files

All of these are in `src/`. A `.cpp` file listed here usually has a matching `.h` file that lists what other files can
call. `app_version.h` holds the version number and `color_limits.h` holds the saturation limit.

### Main program: the `app` namespace

These files share everything in [src/app_state.h](src/app_state.h).

| File | Job |
|---|---|
| `workbench.cpp` | Startup and the message loop |
| `app_main_window.cpp` | Creates the controls and handles the main window's messages |
| `app_interface.cpp` | Makes the controls match the current state: `RefreshInterface`, `LayoutEditorControls` |
| `app_curve_view.cpp` | The curve graph: drawing, dragging points, LUT import and export |
| `app_corrections.cpp` | Turns corrections on and off and applies them |
| `app_effects.cpp` | Turns effects on and off, orders them, and defines their sliders |
| `app_presets.cpp` | Loading, saving and choosing presets |
| `app_profiles.cpp` | Reacts when the active program changes |
| `app_tray.cpp` | The notification-area icon and its menu |
| `app_hotkey.cpp` | The global keyboard shortcut |
| `app_state.cpp` | Small helpers used everywhere, such as `ScaleForDpi` |
| `app_self_tests.cpp` | Tests that drive the real window. Only run with a `--self-test` flag |

### Color and display engine

| File | Job |
|---|---|
| `live_lut.cpp` | Finds displays, builds gamma ramps, writes them safely, and recovers after a crash |
| `color_math.h` | Pure math: sRGB, PQ, contrast, HDR gamma. It never touches a display |
| `adjustments.h` | The effect stages and the settings they use |
| `recovery_rules.h` | The `Ramp` type and how to tell whether a ramp is still ours |
| `processing_state.h` | Works out whether the current settings would change the picture at all |
| `matrix_effect.cpp`, `matrix_effect_rules.h`, `matrix_recovery.h` | The saturation and channel mixer effect |
| `matrix_math.h`, `matrix_api.h` | Color matrix math and the wrapper around Magnification.dll |
| `matrix_preview.cpp` | A separate preview window for the color matrix |
| `night_light.h`, `night_outputs.h` | The night light schedule, and applying it to the other displays |
| `hdr_control.cpp` | Reads and switches the Windows HDR setting |
| `windows_controls.cpp` | The Windows color filter setting |

### Files and settings

| File | Job |
|---|---|
| `presets.cpp` | The `.lutpreset` file format |
| `lut1d.cpp` | Reading and writing LUT files (see [LUT_FORMATS.md](LUT_FORMATS.md)) |
| `application_profiles.cpp` | Per-program rules and which rule applies to a program |
| `programs.cpp` | Lists the running programs |
| `app_settings.h` | Starting with Windows, the path of the program file, and quoting command-line arguments |

User settings live in `MonitorMage.ini`, next to the program.

### Look and feel

| File | Job |
|---|---|
| `ui_theme.cpp` | Custom title bar and dark controls: `HandleThemeMessage`, `HandleCustomDraw` |
| `effect_list.cpp` | The effect list, with its on/off toggles and drag to reorder |
| `tray_popup.cpp` | The themed tray menu |
| `profile_editor.cpp` | The application profiles window |
| `unsaved_dialog.cpp` | The "unsaved changes" dialog |
| `viewport.cpp` | Scrolling when a window doesn't fit on screen |
| `rgb_plot.h`, `paint_buffer.h` | Smooth curve drawing and flicker-free painting |
| `app_icons.h`, `icon_resources.h` | Choosing and loading the program's icons |
| `ui_messages.h` | Turns technical errors into friendly text |
| `window_geometry.h` | Keeps windows a sensible size when the screen changes |

## Where do I change...?

| I want to change... | Go to |
|---|---|
| What a button does | Its `case` in `OnCommand` |
| Which button exists | `ID_` constant in `app_state.h`, `CreateEditorIfNeeded`, `OnCommand`, `LayoutEditorControls` |
| An effect's sliders | `SliderParameters` |
| The color math | `color_math.h` or `ApplyStageToSignal` in `adjustments.h` |
| The status text | `BuildStatusText` or the `live_message` text in `app_corrections.cpp` |
| What the graph draws | `PaintCurveGraph` and the `Draw...` functions above it |
| The theme colors | The `THEME_` constants in `ui_theme.h` |
| The tray menu | `TrayMenuItems` |
| Which `--render-` screenshots exist | `ReadStartupOptions` and `PrepareRenderScene` |

Saved settings and `.lutpreset` field names must keep their current spelling, or existing users lose their settings.

## Names that can mislead you

- The build target `LutWorkbench` produces `MonitorMage.exe`. Its entry point is in `workbench.cpp`.
- `src/main.cpp` is **not** the main program. It is `LutDiagnostic`, a separate command-line tool for testing displays.
- `ID_TEST_CONTRAST` is the id of the main power button. The name is left over from an earlier version.
- `effect` holds the effect ids. `effects` is the list control that displays them.
- `corrections_requested` means the user wants corrections on. `live_session.IsActive` means they are on right now.
  The two differ during a display change or while the computer sleeps.
- `render_only` means "don't touch the real display". Tests and screenshots set it.

## Tests

Build and run everything with `build.cmd` (see [BUILDING.md](BUILDING.md)). The tests come in three kinds:

| Kind | Where | Needs a display? |
|---|---|---|
| Plain logic tests, such as color math and file formats | `tests/` | No |
| `--self-test-...` flags that drive the real program | `app_self_tests.cpp` and the `SelfTest` functions | Many create real windows, but they never change display colors |
| `--render-...` screenshots, useful for comparing before and after | `RenderScreenshot` | They create real windows, but never change display colors |

The `--test` modes of `LutDiagnostic` are different: they do change the display's colors for a short time.

`effect-chain-editor` needs the selected display to be in SDR. In Windows HDR it fails at its white-slider step, because
that slider is disabled in HDR.
