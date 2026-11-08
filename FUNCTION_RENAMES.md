# MonitorMage function renames

A record of the function renames that have been applied to the code. Line numbers refer to the code just before the rename.

- **276** functions get a new, more descriptive name.
- **259** already say what they do, and only change to PascalCase.
- `wWinMain`, `main` and `wmain` stay, because C++ and Windows require those names.

Functions called through a namespace can stay short, because the caller supplies the context: `presets::Read` reads fine. Helpers used inside one file get the fuller name.

Saved settings, `.lutpreset` fields, command-line flags and Windows class names are text, not function names, so they are not affected.

## Changes from the reviewed list

- `toggleWindowsHdr` became `ToggleWindowsHdrMode`, because `ToggleWindowsHdr` is already the menu command's id.
- `monitorGeometry` became `CalculateMonitorGeometry`, because `MonitorGeometry` is the struct it returns.
- The `read` member of the fake test API in `matrix_effect.cpp` became `Read`, to match the `Api` interface. Only the atomic helper in that file is `AtomicRead`.
- The `Read` and `Write` type aliases in `matrix_api.h` became `ReadFunction` and `WriteFunction`, so that they no longer clash with the new member functions.

## Descriptive renames

### src/adjustments.h

| Line | Old name | New name | What it does |
|---:|---|---|---|
| 141 | `adjustSignal` | `ApplyStageToSignal` | Applies one adjustment stage to an SDR-style signal |
| 170 | `adjustCode` | `ApplyStageToCode` | Applies one stage to a final output code, SDR or HDR |

### src/app_icons.h

| Line | Old name | New name | What it does |
|---:|---|---|---|
| 18 | `choose` | `ChooseState` | Picks the tray icon state from active/HDR/app flags |
| 27 | `resource` | `ResourceId` | Maps an icon state to its resource ID |
| 65 | `get` | `GetIcon` | Returns a cached icon, loading it if needed |

### src/app_settings.h

| Line | Old name | New name | What it does |
|---:|---|---|---|
| 9 | `executable` | `ExecutablePath` | Full path of this program |

### src/application_profiles.cpp

| Line | Old name | New name | What it does |
|---:|---|---|---|
| 9 | `ini` | `ReadIniValue` | Reads one value from the settings INI |
| 20 | `expanded` | `ToAbsolutePath` | Turns a stored path back into a full path |
| 28 | `portable` | `ToPortablePath` | Stores a path relative to the settings folder when possible |
| 124 | `resolve` | `ResolveForProgram` | Finds the profile rule for a program |
| 133 | `changed` | `UpdateFocusedProgram` | Records the focused program; true if it changed |

### src/application_profiles.h

| Line | Old name | New name | What it does |
|---:|---|---|---|
| 48 | `overrideLook` | `SetManualOverride` | Marks the look as chosen by hand |
| 49 | `resetOverride` | `ClearManualOverride` | Clears the manual look flag |
| 50 | `manual` | `IsManualOverride` | True if the user overrode the automatic look |
| 51 | `executable` | `FocusedProgram` | Path of the focused program |

### src/color_math.h

| Line | Old name | New name | What it does |
|---:|---|---|---|
| 19 | `contrast` | `ApplyContrast` | Applies the contrast curve to one value |
| 34 | `pqDecode` | `PqToNits` | Converts an HDR PQ code to brightness in nits |
| 41 | `pqEncode` | `NitsToPq` | Converts nits to an HDR PQ code |
| 91 | `sample` | `SampleLut` | Samples the curve into a LUT table |
| 104 | `combinedHdr` | `ApplyHdrContrastAndGamma` | Applies contrast, then HDR gamma correction |
| 125 | `orderedHdr` | `ApplyHdrEffectsInOrder` | Same, honoring the correction-first setting |

### src/effect_list.cpp

| Line | Old name | New name | What it does |
|---:|---|---|---|
| 27 | `px` | `ScaleForDpi` | Scales a size for the window's DPI |
| 32 | `item` | `RowAtPoint` | List row under a point |
| 38 | `role` | `RoleOfRow` | Which effect a list row represents |
| 43 | `select` | `SelectRow` | Selects a row and notifies the parent |
| 100 | `proc` | `EffectListWindowProc` | Message handler for the effect list |
| 253 | `get` | `GetListState` | Fetches the state stored on the list |

### src/hdr_control.cpp

| Line | Old name | New name | What it does |
|---:|---|---|---|
| 14 | `win10` | `IsWindows10` | Checks the Windows version |
| 24 | `inspect` | `ReadHdrState` | Reads the Windows HDR state of a display |
| 90 | `set` | `SetHdrEnabled` | Turns Windows HDR on or off |

### src/live_lut.cpp

| Line | Old name | New name | What it does |
|---:|---|---|---|
| 119 | `quote` | `QuoteArgument` | Wraps a command-line argument in quotes |
| 126 | `save` | `SaveSnapshot` | Writes the recovery snapshot |
| 135 | `load` | `LoadSnapshot` | Reads the recovery snapshot |
| 149 | `matching` | `FindOutputForSnapshot` | Finds the display a snapshot belongs to |
| 157 | `restore` | `RestoreSnapshot` | Puts the display's original gamma ramp back |
| 183 | `tempFile` | `NewTempFilePath` | Creates a unique temp file path |
| 194 | `executable` | `ExecutablePath` | Full path of this program |
| 202 | `identity` | `IdentityRamp` | Builds a ramp that changes nothing |
| 211 | `targets` | `DisplayTargetsFromIdentity` | Extracts physical monitors from an identity string |
| 233 | `outputs` | `ActiveOutputs` | Lists active displays |
| 340 | `evaluate` | `EvaluateOutputCode` | Computes one ramp value with all effects applied |
| 388 | `makeRamp` | `BuildGammaRamp` | Builds the full gamma ramp for a display |
| 477 | `active` | `IsActive` | True while a session is running |
| 482 | `sameMode` | `HasSameDisplayMode` | True if the display mode is unchanged |
| 497 | `begin` | `Start` | Starts a live correction session |
| 644 | `guardian` | `RunRecoveryGuardian` | Helper process that restores colors if the app dies |
| 675 | `recover` | `RecoverFromSnapshotFile` | Restores colors from a snapshot file |

### src/lut1d.cpp

| Line | Old name | New name | What it does |
|---:|---|---|---|
| 21 | `fail` | `ThrowLutError` | Throws a LUT error |
| 26 | `ensure` | `Require` | Fails with a message if a condition is false |
| 32 | `clean` | `StripBomAndComments` | Removes the byte-order mark and # comments from LUT text |
| 56 | `tokens` | `SplitTokens` | Splits a line into tokens |
| 68 | `number` | `ParseNumber` | Parses a finite number |
| 79 | `count` | `ParseSampleCount` | Parses and checks a LUT length |
| 86 | `sourceValid` | `ValidateSource` | Checks imported channel data |
| 102 | `normalized` | `NormalizeChannels` | Resamples channels onto the standard range |
| 150 | `lower` | `ToLower` | Lowercases text |
| 157 | `textFile` | `ReadTextFile` | Reads a whole text file |
| 171 | `hr` | `ThrowIfFailed` | Throws if a Windows HRESULT reports failure |
| 197 | `imageFile` | `ReadImageFile` | Decodes an image using WIC |
| 275 | `fromImage` | `LutFromImage` | Extracts LUT data from an image |
| 399 | `decode` | `DecodeCube` | Parses .cube LUT text |

### src/main.cpp

| Line | Old name | New name | What it does |
|---:|---|---|---|
| 54 | `init` | `InitDeviceInfoHeader` | Fills a display-config request header |
| 60 | `paths` | `ActiveDisplayPaths` | Lists active display paths |
| 101 | `signature` | `DisplaySignature` | Identifies a display's current setup |
| 135 | `displayDefaults` | `PrintDisplayProfileDefaults` | Prints default color profiles |
| 163 | `driverModel` | `PrintDriverModel` | Prints the display driver's WDDM version |
| 234 | `capabilities` | `PrintCalibrationCapabilities` | Prints MHC2/VCGT capability info |
| 262 | `report` | `PrintDiagnosticReport` | Prints the read-only display report |
| 360 | `curve` | `ContrastCurve` | One-line wrapper over contrast |
| 423 | `compose` | `ComposeContrastRamp` | Applies contrast on top of a base ramp |
| 438 | `selfTest` | `RunCurveSelfTest` | Tests curve math |
| 479 | `save` | `SaveSnapshot` | Writes the recovery snapshot |
| 488 | `load` | `LoadSnapshot` | Reads the recovery snapshot |
| 502 | `restore` | `RestoreSnapshot` | Puts the original ramp back |
| 519 | `quote` | `QuoteArgument` | Wraps a command-line argument in quotes |
| 528 | `consoleControl` | `ConsoleCtrlHandler` | Handles Ctrl+C |
| 538 | `guardian` | `RunRecoveryGuardian` | Helper process that restores colors if the tool dies |
| 556 | `recoveryTest` | `RunRecoverySelfTest` | Tests recovery messaging |
| 600 | `hdrRamp` | `BuildHdrRamp` | Builds the HDR correction ramp |
| 619 | `hdrWhite` | `QuerySdrWhiteNits` | Reads the SDR white level in HDR mode |
| 646 | `test` | `RunTimedLutTest` | Applies a LUT briefly, then restores it |
| 770 | `usage` | `PrintUsage` | Prints command-line help |
| 792 | `number` | `ParseNumber` | Parses a command-line number |

### src/matrix_api.h

| Line | Old name | New name | What it does |
|---:|---|---|---|
| 39 | `zoom` | `ReadZoom` | Reads the current Magnifier zoom |

### src/matrix_effect.cpp

| Line | Old name | New name | What it does |
|---:|---|---|---|
| 56 | `executable` | `ExecutablePath` | Full path of this program |
| 65 | `object` | `SharedObjectName` | Builds a named Windows object's name |
| 70 | `validToken` | `IsValidToken` | Validates a session token |
| 80 | `read` | `AtomicRead` | Thread-safe read of a shared value |
| 90 | `read` | `Read` | Fake test API: reads the current matrix |
| 129 | `getMessage` | `WaitForMessageOrWorkerExit` | Message loop wait that also watches the worker |
| 160 | `state` | `CurrentState` | Current state of the effect worker |
| 240 | `narrow` | `ToUtf8` | Converts wide text to UTF-8 |
| 322 | `report` | `WriteReport` | Writes a diagnostic report |
| 361 | `worker` | `RunWorkerProcess` | Helper process that applies the color matrix |

### src/matrix_math.h

| Line | Old name | New name | What it does |
|---:|---|---|---|
| 10 | `identity` | `IdentityMatrix` | Matrix that changes nothing |
| 20 | `saturation` | `SaturationMatrix` | Builds a saturation matrix |
| 32 | `preview` | `PreviewMatrix` | Matrix for a preview mode |
| 39 | `monochrome` | `MonochromeMatrix` | Builds a channel-mix matrix |
| 57 | `multiply` | `MultiplyMatrices` | Multiplies two matrices |
| 67 | `difference` | `MaxDifference` | Largest element difference |
| 94 | `restore` | `RestoreBaseline` | Restores the original matrix if still ours |

### src/matrix_preview.cpp

| Line | Old name | New name | What it does |
|---:|---|---|---|
| 93 | `executable` | `ExecutablePath` | Full path of this program |
| 107 | `object` | `SharedObjectName` | Builds a named Windows object's name |
| 112 | `validToken` | `IsValidToken` | Validates a session token |
| 122 | `quote` | `QuoteArgument` | Wraps a command-line argument in quotes |
| 157 | `matrixLog` | `LogMatrix` | Writes a matrix to the log |
| 168 | `displayLog` | `LogDisplayInfo` | Logs display details |
| 202 | `cpu` | `ProcessCpuTime` | CPU time used by this process |
| 219 | `busy` | `IsBusy` | True while the worker runs |
| 230 | `launch` | `LaunchWorker` | Starts the preview worker |
| 271 | `windowState` | `GetWindowState` | Fetches state stored on the window |
| 276 | `status` | `SetStatusText` | Updates the status line |
| 281 | `bindShortcut` | `BindPreviewHotkey` | Registers the preview hotkey |
| 303 | `buttons` | `UpdateButtons` | Enables or disables controls |
| 315 | `finish` | `FinishPreview` | Cleans up after a preview ends |
| 345 | `layout` | `LayoutPreviewWindow` | Positions the window's controls |
| 375 | `proc` | `PreviewWindowProc` | Message handler for the preview window |
| 606 | `worker` | `RunWorkerProcess` | Helper process that runs the preview |
| 756 | `show` | `ShowPreviewWindow` | Creates or shows the window |
| 790 | `window` | `PreviewWindowHandle` | Handle of the preview window |
| 795 | `dialogMessage` | `HandleDialogMessage` | Passes keyboard navigation to the dialog |
| 801 | `run` | `RunStandalone` | Runs the preview as its own program |

### src/night_light.h

| Line | Old name | New name | What it does |
|---:|---|---|---|
| 26 | `phase` | `CurrentPhase` | Night-light strength for a time of day |
| 46 | `timeText` | `FormatTime` | Minutes to HH:MM text |
| 65 | `targets` | `DisplayKeys` | Keys identifying a display's monitors |
| 90 | `includes` | `IsDisplayIncluded` | True if night light covers this display |
| 98 | `include` | `SetDisplayIncluded` | Adds or removes a display |

### src/night_outputs.h

| Line | Old name | New name | What it does |
|---:|---|---|---|
| 23 | `active` | `IsActive` | True if any night-light session runs |

### src/paint_buffer.h

| Line | Old name | New name | What it does |
|---:|---|---|---|
| 58 | `buffered` | `IsBuffered` | True if drawing to memory |
| 59 | `dc` | `DeviceContext` | The drawing target |
| 67 | `bytes` | `ByteCount` | Size of the bitmap |

### src/presets.cpp

| Line | Old name | New name | What it does |
|---:|---|---|---|
| 21 | `hundredths` | `ParseHundredths` | Parses a decimal into integer hundredths |

### src/presets.h

| Line | Old name | New name | What it does |
|---:|---|---|---|
| 32 | `inactive` | `InactivePreset` | A preset with every effect off |
| 54 | `dirty` | `IsDirty` | True if there are unsaved edits |

### src/processing_state.h

| Line | Old name | New name | What it does |
|---:|---|---|---|
| 15 | `active` | `IsProcessing` | True if the settings change the picture |

### src/profile_editor.cpp

| Line | Old name | New name | What it does |
|---:|---|---|---|
| 42 | `message` | `SetNote` | Sets the note text |
| 47 | `setExecutable` | `SetProgramFields` | Fills in the path and friendly name |
| 53 | `pickerSelection` | `UpdatePickerSelection` | Reacts to the running-program selection |
| 92 | `choose` | `SelectLookInCombo` | Selects a look in a combo box |
| 101 | `selectedLook` | `SelectedLookPath` | Path of the look chosen in a combo |
| 126 | `actionControls` | `UpdateActionControls` | Enables the look picker for the action |
| 131 | `layout` | `LayoutEditor` | Positions the editor's controls |
| 199 | `proc` | `ProfileEditorWindowProc` | Message handler for the profile editor |
| 537 | `window` | `ProfileEditorHandle` | Handle of the editor window |
| 555 | `show` | `ShowProfileEditor` | Creates or shows the editor |

### src/programs.cpp

| Line | Old name | New name | What it does |
|---:|---|---|---|
| 10 | `compare` | `CompareIgnoringCase` | Case-insensitive text comparison |
| 22 | `description` | `FileDescription` | Reads a program's description |
| 66 | `visibleWindow` | `CollectVisibleWindow` | Window-enumeration callback |
| 80 | `name` | `DisplayName` | Friendly name for a program |
| 95 | `under` | `IsUnderFolder` | True if a path is inside a folder |
| 105 | `sort` | `SortPrograms` | Sorts the program list |
| 121 | `running` | `RunningPrograms` | Lists running programs |

### src/recovery_rules.h

| Line | Old name | New name | What it does |
|---:|---|---|---|
| 10 | `difference` | `MaxRampDifference` | Largest difference between two ramps |
| 19 | `ownedRamp` | `IsRampOurs` | True if the current ramp is one we set |

### src/rgb_plot.h

| Line | Old name | New name | What it does |
|---:|---|---|---|
| 39 | `ensure` | `EnsureSize` | Recreates the bitmap if the size changed |
| 65 | `dc` | `DeviceContext` | The scratch drawing target |
| 66 | `bitmap` | `BitmapHandle` | The scratch bitmap |
| 68 | `bytes` | `ByteCount` | Size of the pixel buffer |
| 73 | `rgb` | `DrawRgbCurves` | Draws the antialiased curves |

### src/tray_popup.cpp

| Line | Old name | New name | What it does |
|---:|---|---|---|
| 26 | `px` | `ScaleForDpi` | Scales a size for the popup's DPI |
| 31 | `choose` | `ChooseSelectedRow` | Acts on the selected row |
| 54 | `listProc` | `PopupListProc` | Message handler for the popup's list |
| 107 | `paint` | `PaintPopup` | Draws the popup |
| 131 | `proc` | `TrayPopupWindowProc` | Message handler for the popup |
| 359 | `create` | `CreatePopupWindow` | Creates the popup window |
| 387 | `popup` | `ShowTrayPopup` | Shows the popup and waits for a choice |

### src/ui_messages.h

| Line | Old name | New name | What it does |
|---:|---|---|---|
| 10 | `error` | `FriendlyError` | Turns an error into a plain-language message |

### src/ui_theme.cpp

| Line | Old name | New name | What it does |
|---:|---|---|---|
| 266 | `scale` | `ScaleForDpi` | Scales a size for the window's DPI |
| 271 | `rounded` | `FillRoundedRect` | Draws a filled rounded rectangle |
| 300 | `outline` | `DrawFocusOutline` | Draws a border that shows focus or hover |
| 311 | `gear` | `DrawGear` | Draws the settings gear |
| 594 | `glyph` | `DrawGlyph` | Draws a toolbar symbol |
| 689 | `actions` | `GetCaptionActions` | Title-bar actions stored on a window |
| 694 | `action` | `CaptionActionAtHit` | Title-bar action for a hit-test code |
| 922 | `popupProc` | `PopupListProc` | Message handler for dropdown lists |
| 1010 | `inputProc` | `InputControlProc` | Message handler for text inputs |
| 1105 | `captionHit` | `CaptionHitTest` | What part of the title bar is under a point |
| 1276 | `printWindow` | `PaintWindowToDc` | Draws a whole window into a DC |
| 1536 | `proc` | `ThemedControlProc` | Message handler for themed controls |
| 1639 | `helpFont` | `UpdateHelpFont` | Keeps the tooltip font current |
| 1650 | `helpOwner` | `HelpOwnerProc` | Message handler that owns tooltips |
| 1664 | `setHelp` | `SetHelpRegion` | Attaches help text to a region |
| 1725 | `help` | `SetHelpText` | Attaches help text to a control |
| 1735 | `helpText` | `HelpTextForRegion` | Looks up help text |
| 1792 | `background` | `BackgroundBrush` | The app background brush |
| 1849 | `captionPower` | `SetCaptionPowerButton` | Adds the title-bar power button |
| 1872 | `captionSettings` | `SetCaptionSettingsButton` | Adds the title-bar settings button |
| 1878 | `iconButton` | `SetIconButton` | Assigns a glyph to a button |
| 1899 | `sliderDetails` | `ConfigureSlider` | Sets a slider's double-click default and thumb letter |
| 1995 | `makeFont` | `CreateUiFont` | Creates the interface font |
| 2036 | `adjustWindowRect` | `AdjustThemedWindowRect` | Window size for a client area |
| 2044 | `control` | `ApplyControlTheme` | Applies the theme to one control |
| 2079 | `frame` | `ApplyWindowFrameTheme` | Applies dark styling to a window frame |
| 2106 | `message` | `HandleThemeMessage` | Handles themed window messages |

### src/unsaved_dialog.cpp

| Line | Old name | New name | What it does |
|---:|---|---|---|
| 27 | `theme` | `ThemeSubclassProc` | Applies the theme to the dialog |
| 36 | `layout` | `LayoutDialog` | Positions the dialog's controls |
| 149 | `run` | `RunDialog` | Shows the dialog and waits |
| 162 | `preview` | `PreviewDialog` | Shows the dialog for screenshots |

### src/viewport.cpp

| Line | Old name | New name | What it does |
|---:|---|---|---|
| 21 | `state` | `GetViewState` | State stored on the scrolling window |
| 26 | `scroll` | `ScrollTo` | Moves the viewport |
| 50 | `update` | `UpdateScrollbars` | Recalculates scrolling after a resize |
| 91 | `proc` | `ViewportProc` | Message handler for scrolling |

### src/windows_controls.cpp

| Line | Old name | New name | What it does |
|---:|---|---|---|
| 15 | `read` | `ReadRegistryDword` | Reads a registry number |
| 28 | `broker` | `ColorFilterBrokerPath` | Path of the Windows color filter helper |
| 37 | `write` | `WriteRegistryDword` | Writes a registry number |
| 43 | `undo` | `RestoreRegistryValue` | Puts a registry value back |
| 52 | `inspect` | `ReadColorFilterState` | Reads the Windows color filter state |
| 110 | `completion` | `CheckCompletion` | Decides if a filter change finished |

### src/windows_controls.h

| Line | Old name | New name | What it does |
|---:|---|---|---|
| 43 | `busy` | `IsBusy` | True while a change is in progress |

### src/workbench.cpp

| Line | Old name | New name | What it does |
|---:|---|---|---|
| 223 | `presetRole` | `SelectedPresetRole` | Look or correction, for the selected row |
| 228 | `processingRow` | `ProcessingOrderIndex` | Position of an effect in processing order |
| 242 | `effectControl` | `EffectListForRole` | Which list shows an effect |
| 247 | `effectRow` | `EffectListRow` | List row of an effect |
| 257 | `px` | `ScaleForDpi` | Scales a size for the current DPI |
| 262 | `shortcutPath` | `SettingsFilePath` | Path of MonitorMage.ini |
| 312 | `presetKind` | `PresetKindForRole` | Maps a role to a preset kind |
| 327 | `presetError` | `ShowPresetError` | Stores a preset error message |
| 430 | `indexPreset` | `AddPresetToLibrary` | Adds a preset if it is not listed |
| 600 | `setText` | `SetTextIfChanged` | Updates a control only when the text differs |
| 609 | `setEnabled` | `SetControlEnabled` | Enables a control if changed |
| 615 | `setVisible` | `SetControlVisible` | Shows a control if changed |
| 705 | `presetEdited` | `OnPresetEdited` | Stores edits and schedules autosave |
| 773 | `allowExit` | `ConfirmExit` | Saves and asks about unsaved presets |
| 785 | `shortcutModifiers` | `HotkeyModifiers` | Modifier flags of a hotkey |
| 792 | `validShortcut` | `IsValidHotkey` | Checks a hotkey is usable |
| 800 | `shortcutName` | `HotkeyName` | Readable hotkey text |
| 820 | `bindShortcut` | `BindHotkey` | Registers a hotkey with Windows |
| 844 | `bindPreferredShortcut` | `BindPreferredHotkey` | Registers the saved hotkey |
| 862 | `startShortcut` | `LoadAndBindHotkey` | Reads the saved hotkey and registers it |
| 869 | `shortcutSelfTest` | `RunHotkeySelfTest` | Tests hotkey conflict handling |
| 924 | `processFaults` | `ProcessPageFaults` | Page faults for this process |
| 932 | `processCpu` | `ProcessCpuTime` | CPU time for this process |
| 945 | `amount` | `ContrastAmount` | Contrast slider as a number |
| 950 | `pivot` | `ContrastPivot` | Contrast pivot as a number |
| 955 | `gamma` | `HdrGamma` | Selected HDR gamma |
| 961 | `previewOutput` | `OutputForPreview` | Display used for the preview |
| 975 | `place` | `PlaceControl` | Positions a control |
| 987 | `decimal` | `FormatDecimal` | Number to fixed-decimal text |
| 1061 | `lookBypassed` | `IsLookBypassed` | True if the look is turned off |
| 1066 | `effectiveHdrFix` | `IsHdrFixActive` | True if the HDR fix applies now |
| 1079 | `matrixWanted` | `IsColorMatrixNeeded` | True if the color matrix should run |
| 1086 | `syncMatrix` | `SyncColorMatrix` | Starts or stops the color matrix to match settings |
| 1106 | `settleMatrix` | `ScheduleColorMatrixRecovery` | Schedules re-checks of the color matrix |
| 1121 | `iconState` | `CurrentIconState` | Which tray icon applies now |
| 1128 | `iconState` | `CurrentIconState` | Which tray icon applies now |
| 1133 | `stateIcon` | `IconForState` | Loads the icon for a state |
| 1139 | `stateIcon` | `IconForState` | Loads the icon for a state |
| 1162 | `nightClock` | `UpdateNightLightSchedule` | Updates night light and schedules its next change |
| 1220 | `disableLive` | `DisableLiveCorrections` | Stops corrections and restores colors |
| 1262 | `applyLive` | `ApplyLiveCorrections` | Pushes current settings to the display |
| 1347 | `enableLive` | `EnableLiveCorrections` | Starts live corrections |
| 1407 | `reconcileLive` | `ReconcileLiveCorrections` | Makes corrections match the requested state |
| 1457 | `startMaster` | `TurnOnCorrections` | Handles the main power switch turning on |
| 1565 | `acceptForeground` | `OnForegroundProgramChanged` | Applies profiles when the program changes |
| 1574 | `inspectForeground` | `CheckForegroundProgram` | Finds which program is in front |
| 1604 | `foregroundEvent` | `ForegroundChangedCallback` | Windows callback for focus changes |
| 1767 | `parameters` | `SliderParameters` | Sliders for the selected effect |
| 1797 | `tabStops` | `TabStopControls` | Keyboard tab order |
| 2006 | `labels` | `RefreshInterface` | Refreshes the whole editor UI and the tray |
| 2410 | `layout` | `LayoutEditorControls` | Positions all editor controls |
| 2538 | `trayItems` | `TrayMenuItems` | Builds the tray menu entries |
| 2552 | `trayMenu` | `ShowTrayMenu` | Shows the tray menu |
| 2629 | `transfer1D` | `ImportOrExportLut` | Runs the 1D LUT file dialog |
| 2721 | `curvePlot` | `CurvePlotRect` | Rectangle of the curve graph |
| 2916 | `curveProc` | `CurveWindowProc` | Message handler for the curve graph |
| 3312 | `renderPreview` | `RenderWindowToFile` | Saves a window image for tests |
| 3359 | `control` | `CreateChildControl` | Creates a child control |
| 3369 | `ensureEditor` | `CreateEditorIfNeeded` | Builds the editor UI on first use |
| 3603 | `windowProc` | `MainWindowProc` | Message handler for the main window |

### tests/live_tests.cpp

| Line | Old name | New name | What it does |
|---:|---|---|---|
| 21 | `active` | `IsActive` | Test fake, must match the real session |
| 22 | `sameMode` | `HasSameDisplayMode` | Test fake, must match the real session |
| 24 | `begin` | `Start` | Test fake, must match the real session |

### tools/ddc_probe.cpp

| Line | Old name | New name | What it does |
|---:|---|---|---|
| 23 | `utf8` | `ToUtf8` | Converts wide text to UTF-8 |
| 165 | `quote` | `QuoteArgument` | Wraps an argument in quotes |

### tools/icon_preview.cpp

| Line | Old name | New name | What it does |
|---:|---|---|---|
| 26 | `px` | `ScaleForDpi` | Scales a size for DPI |
| 133 | `proc` | `IconPreviewWindowProc` | Message handler for the preview window |

## PascalCase only

**src/adjustments.h**: `validate`, `balanceGains`, `extendedSrgbDecode`, `extendedSrgbEncode`, `sampleCurve`, `sampleSpline`

**src/app_settings.h**: `registryError`, `startupCommand`, `startsWithWindows`, `startWithWindows`

**src/application_profiles.cpp**: `samePath`, `validate`, `read`, `encode`

**src/color_math.h**: `requireRange`, `srgbDecode`, `srgbEncode`, `whiteNits`, `gamma`, `whiteCode`, `map`

**src/effect_list.cpp**: `refreshHelp`, `redrawRow`, `paintList`, `attach`, `update`, `drawRow`, `draw`

**src/live_lut.cpp**: `acquire`, `legacyHdrFlagReliable`, `sameDisplays`, `compositionBaseline`, `finish`, `inheritCarryOver`, `update`, `stop`, `writerLeaseSelfTest`, `recoverySelfTest`

**src/lut1d.cpp**: `writeImage`, `decodeSpi`, `decodeCal`, `decodeCsv`, `read`, `write`

**src/main.cpp**: `profileTags`, `exportHdr`, `compareHdr`, `rampDifference`

**src/matrix_api.h**: `open`, `read`, `write`, `close`

**src/matrix_effect.cpp**: `write`, `stop`, `update`, `processName`, `displayReport`, `selfTest`, `benchmark`

**src/matrix_effect_rules.h**: `stop`, `apply`, `apply`

**src/matrix_preview.cpp**: `reportFolder`, `read`, `write`, `writeLog`, `stop`, `selfTest`, `read`, `write`

**src/matrix_recovery.h**: `restart`, `nextDelay`

**src/night_light.h**: `parseTime`

**src/night_outputs.h**: `release`, `stop`, `update`

**src/paint_buffer.h**: `bitmapSize`, `commit`, `finish`

**src/presets.cpp**: `trim`, `validate`, `fileError`, `close`, `encode`, `decode`, `read`, `write`, `writeBytes`

**src/presets.h**: `canAutosave`, `markSaved`

**src/profile_editor.cpp**: `controlText`, `refreshProgramsList`, `refreshRules`, `refreshAppearance`, `previewPrograms`

**src/rgb_plot.h**: `reset`, `pixels`

**src/tray_popup.cpp**: `paintList`, `pointerRow`, `previewPopup`, `popupPaintTest`, `popupSelfTest`

**src/ui_theme.cpp**: `tooltipShape`, `tooltipBounds`, `tooltipBorder`, `tooltipStyleProc`, `sliderResetProc`, `styleTooltip`, `tooltipEdgeSelfTest`, `roundBounds`, `glyphFrame`, `glyphRaster`, `actionRect`, `primaryRect`, `captionItems`, `showCaptionMenu`, `captionTip`, `hideCaptionTip`, `trackCaptionTip`, `showCaptionTip`, `popupBorder`, `singleLineEdit`, `inputBounds`, `inputOrigin`, `paintInputFrame`, `paintCaption`, `repaintCaption`, `paintControl`, `sliderPaintProc`, `helpRegion`, `fixedWindowSize`, `repairedBounds`, `windowNeedsSizeRepair`, `repairWindowSize`, `hdrTooltip`, `captionTooltip`, `previewCaptionMenu`, `setFontFamily`, `fontFamily`, `refreshCaptionFont`, `paintMenuGlyph`, `captionActions`, `paintToggle`, `toggleButton`, `sliderColour`, `contentWidth`

**src/unsaved_dialog.cpp**: `dialogTemplate`, `dialogProc`, `confirm`, `selfTest`

**src/viewport.cpp**: `viewportOffset`, `enableViewport`, `revealControl`

**src/window_geometry.h**: `fitWindowRect`

**src/windows_controls.cpp**: `stopWaiting`, `completionNotice`, `set`, `poll`, `openColorFilterSettings`

**src/windows_controls.h**: `notice`

**src/workbench.cpp**: `ensureCommonControls`, `selectedSourceIndex`, `selectSource`, `creativeOrder`, `refreshColorFilters`, `changeWindowsFilter`, `currentPreset`, `presetFolder`, `persistPresetChoices`, `findPreset`, `readPreset`, `discoverPresets`, `loadPresetControls`, `initializePresets`, `refreshPresetChoice`, `saveExistingPreset`, `savePresetAsPath`, `flushPresetAutosave`, `presetFileDialog`, `activeSources`, `refreshSources`, `lutProcessingActive`, `processingActive`, `currentSettings`, `syncNightOutputs`, `depth`, `waitForDisplay`, `toggleBypass`, `toggleCorrections`, `toggleWindowsHdr`, `manualLookOverride`, `applyApplicationProfile`, `applyProfileConfiguration`, `showProfiles`, `adjustmentEnabled`, `toggleEffect`, `moveEffect`, `moveCreative`, `nextTabStop`, `navigateTab`, `layoutEffectControls`, `updateTray`, `showEditor`, `installTray`, `exportCurve`, `monitorGeometry`, `graphUpdateBounds`, `graphWarningBounds`, `invalidateGraph`, `layoutMonitorControls`, `changeCurvePoint`, `profileLinkProc`, `monitorLabelPaintProc`, `preparePresetUiTest`, `runPresetUiTest`, `runProfileUiTest`, `runEffectUiTest`, `countSurfaceChanges`, `runTabUiTest`, `partialGraphMatchesFull`, `clippedListMatchesFull`, `runSurfaceUiTest`, `cleanPresetUiTest`

**tests/adjustment_tests.cpp**: `check`, `rejects`

**tests/application_tests.cpp**: `check`, `rejects`, `writeConfig`

**tests/color_tests.cpp**: `check`, `near`, `rejects`, `tests`

**tests/live_tests.cpp**: `check`, `inheritCarryOver`, `update`, `stop`, `nightTests`

**tests/lut_format_tests.cpp**: `check`, `rejects`, `assertNear`, `compare`

**tests/preset_tests.cpp**: `check`, `rejects`, `changed`

**tests/programs_tests.cpp**: `check`

**tests/settings_tests.cpp**: `check`

**tools/ddc_probe.cpp**: `line`, `topology`, `inspect`, `findSamsung`, `openSamsung`

**tools/icon_preview.cpp**: `removeTray`, `installTray`, `freeIcons`, `loadIcons`, `layout`, `show`

**tools/matrix_probe.cpp**: `memory`, `displays`
