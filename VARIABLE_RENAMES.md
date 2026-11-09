# MonitorMage variable and constant renames

A record of the variable and constant changes that have been applied to the code.

| Part | What changes | Count |
|---|---|---:|
| 1 | Mystery numbers get names | 4 groups, about 320 places |
| 2 | Constants become CAPITAL, with a prefix saying what they are | 161 |
| 3 | Vague global variables get descriptive names | 39 |
| 4 | Variables with more than one meaning are split | 5 |
| 5 | Settings become PascalCase | 46 |
| 6 | Abbreviations are spelled out | 33 groups |
| 7 | Private members change to `_name` | 58 |
| 8 | camelCase becomes snake_case | 529 names |

Text stored in files is not affected: saved settings, `.lutpreset` fields, command-line flags and Windows class names stay exactly the same, so existing presets keep loading.

## Changes from the reviewed list

- `white` in `live_lut.h` stayed lowercase, because it belongs to `Output` and describes the display. It is not a user setting.
- The string buffers `s` and `r` became `utf8_text` instead of `text`, because `text` was already the name of the input.
- The `h` field also became `handle` in the `DC` and `DisposableOwner` structs, which work like `Handle`.
- The default effect orders and the effect numbers in the self-tests also use `effect::` names.

## 1. Mystery numbers

The biggest change for a new reader. These numbers mean something, but only if you already know the code.

### Effects

Used about 150 times, as in `selected == 9` and `enabled[10]`. The numbers match the order of the effect list.

```cpp
namespace effect
{
enum Id : int
{
	ContrastCurves,    // 0
	HdrGamma,          // 1
	ColorBalance,      // 2
	MidtoneGamma,      // 3
	Levels,            // 4
	ShadowsHighlights, // 5
	CustomCurves,      // 6
	Imported1dLut,     // 7
	ColorFilters,      // 8
	NightLight,        // 9
	Saturation,        // 10
	ChannelMixer,      // 11
	Count,
	FirstCreative = ColorBalance,
	FirstWindows = ColorFilters,
};
}
```

| Today | Becomes |
|---|---|
| `selected == 9` | `selected_effect == effect::NightLight` |
| `enabled[10]` | `enabled[effect::Saturation]` |
| `selected >= 2 && selected < 8` | `selected_effect >= effect::FirstCreative && selected_effect < effect::FirstWindows` |
| `std::array<bool, 12>` | `std::array<bool, effect::Count>` |

A plain `enum` inside a namespace keeps the existing arithmetic and array indexing working without casts. The channel mixer's setting is stored as `monochrome`, so that field name stays different from the effect name.

### Preset roles

Used more than 100 times, as in `CurrentPreset(0)` and `role == 1`.

```cpp
namespace preset_role
{
enum Id : int
{
	Look,       // 0
	Correction, // 1
	Count,
};
}
```

`CurrentPreset(0)` becomes `CurrentPreset(preset_role::Look)`.

### Glyphs

The `Glyph` enum already exists, but `ui_theme.cpp` converts it to a number and compares that, as in `kind == 16` for the close button. About 35 places.

The drawing functions would take a `Glyph` directly, so `kind == 16` becomes `glyph == Glyph::Close`. The two range checks (`kind >= 4 && kind <= 8`) move into one helper, `IsToolbarGlyph`.

### Adjustment slots

`balance` and `levels` are arrays where each position has a fixed meaning. About 20 places.

| Today | Becomes |
|---|---|
| `balance[0]` to `balance[2]` | `balance[BALANCE_RED]`, `BALANCE_GREEN`, `BALANCE_BLUE` |
| `balance[3]`, `balance[4]` | `BALANCE_WARMTH`, `BALANCE_TINT` |
| `levels[0]` to `levels[3]` | `INPUT_BLACK`, `INPUT_WHITE`, `OUTPUT_BLACK`, `OUTPUT_WHITE` |

Layout sizes in pixels and one-off values stay as numbers where the code around them already explains them.

## 2. Constants

Your style guide makes compile-time constants CAPITAL. Control ids get `ID_`, timers `TIMER_`, window messages `MESSAGE_` and theme colors `THEME_`. `ui::Text` cannot become `TEXT` or `ui::Error` become `ERROR`, because Windows already defines those as macros.

Enum values stay PascalCase, as your guide says (Enum.5).

### app_settings.h

| Old | New |
|---|---|
| `RunKey` | `RUN_REGISTRY_KEY` |

### color_limits.h

| Old | New |
|---|---|
| `SaturationMaximum` | `SATURATION_MAXIMUM` |

### color_math.h

| Old | New |
|---|---|
| `pqM1` | `PQ_M1` |
| `pqM2` | `PQ_M2` |
| `pqC1` | `PQ_C1` |
| `pqC2` | `PQ_C2` |
| `pqC3` | `PQ_C3` |
| `srgbCodeKnee` | `SRGB_CODE_KNEE` |
| `srgbLinearKnee` | `SRGB_LINEAR_KNEE` |

### effect_list.h

| Old | New |
|---|---|
| `RowHeight` | `ROW_HEIGHT` |
| `Names` | `EFFECT_NAMES` |

### live_lut.cpp

| Old | New |
|---|---|
| `LeaseName` | `WRITER_LEASE_NAME` |

### lut1d.cpp

| Old | New |
|---|---|
| `MaxBytes` | `MAX_FILE_BYTES` |
| `MaxSource` | `MAX_SOURCE_ENTRIES` |
| `MaxStored` | `MAX_STORED_ENTRIES` |
| `MaxPixels` | `MAX_PIXELS` |

### matrix_effect.cpp

| Old | New |
|---|---|
| `Magic` | `SHARED_MEMORY_MAGIC` |

### matrix_effect.h

| Old | New |
|---|---|
| `Changed` | `MESSAGE_MATRIX_CHANGED` |

### matrix_math.h

| Old | New |
|---|---|
| `weights` | `LUMINANCE_WEIGHTS` |

### matrix_preview.cpp

| Old | New |
|---|---|
| `Magic` | `SHARED_MEMORY_MAGIC` |
| `Timer` | `TIMER_PREVIEW` |
| `Mode` | `ID_MODE` |
| `Duration` | `ID_DURATION` |
| `Start` | `ID_START` |
| `Stop` | `ID_STOP` |
| `Reports` | `ID_REPORTS` |
| `Status` | `ID_STATUS` |
| `Note` | `ID_NOTE` |
| `Swatches` | `ID_SWATCHES` |
| `Shortcut` | `ID_SHORTCUT` |
| `SetShortcut` | `ID_SET_SHORTCUT` |
| `ShortcutLabel` | `ID_SHORTCUT_LABEL` |
| `HotkeyA` | `HOTKEY_A` |
| `HotkeyB` | `HOTKEY_B` |

### matrix_recovery.h

| Old | New |
|---|---|
| `delays` | `RECOVERY_DELAYS` |

### night_light.h

| Old | New |
|---|---|
| `day` | `SECONDS_PER_DAY` |

### profile_editor.cpp

| Old | New |
|---|---|
| `PanelWidth` | `PANEL_WIDTH` |
| `PanelHeight` | `PANEL_HEIGHT` |
| `Right` | `PANEL_RIGHT_EDGE` |
| `BodyWidth` | `PANEL_BODY_WIDTH` |
| `Enable` | `ID_ENABLE` |
| `Rules` | `ID_RULES` |
| `Executable` | `ID_EXECUTABLE` |
| `Browse` | `ID_BROWSE` |
| `Last` | `ID_USE_LAST_PROGRAM` |
| `Action` | `ID_ACTION` |
| `LookChoice` | `ID_LOOK_CHOICE` |
| `Store` | `ID_SAVE_RULE` |
| `Remove` | `ID_REMOVE_RULE` |
| `NewRule` | `ID_NEW_RULE` |
| `Apply` | `ID_APPLY` |
| `Close` | `ID_CLOSE` |
| `Running` | `ID_CHOOSE_RUNNING_PROGRAM` |
| `ProgramList` | `ID_PROGRAM_LIST` |
| `SortPrograms` | `ID_SORT_PROGRAMS` |
| `Background` | `ID_INCLUDE_BACKGROUND` |
| `RefreshPrograms` | `ID_REFRESH_PROGRAMS` |
| `ChooseProgram` | `ID_CHOOSE_PROGRAM` |
| `CancelProgram` | `ID_CANCEL_PROGRAM` |
| `style` | `WINDOW_STYLE` |

### rgb_plot.h

| Old | New |
|---|---|
| `colours` | `CHANNEL_COLORS` |

### tray_popup.cpp

| Old | New |
|---|---|
| `Choose` | `MESSAGE_ITEM_CHOSEN` |

### ui_messages.h

| Old | New |
|---|---|
| `messages` | `MESSAGES` |

### ui_theme.cpp

| Old | New |
|---|---|
| `GeometryTimer` | `TIMER_GEOMETRY` |
| `CaptionTipTimer` | `TIMER_CAPTION_TIP` |

### ui_theme.h

| Old | New |
|---|---|
| `Background` | `THEME_BACKGROUND` |
| `Surface` | `THEME_SURFACE` |
| `Sidebar` | `THEME_SIDEBAR` |
| `Text` | `THEME_TEXT` |
| `Muted` | `THEME_MUTED` |
| `Border` | `THEME_BORDER` |
| `FocusBorder` | `THEME_FOCUS_BORDER` |
| `Accent` | `THEME_ACCENT` |
| `Error` | `THEME_ERROR` |
| `CloseRed` | `THEME_CLOSE_RED` |
| `SelectedTab` | `THEME_SELECTED_TAB` |
| `SelectionLight` | `THEME_SELECTION_LIGHT` |
| `Settings` | `LABEL_SETTINGS` |
| `ApplicationProfiles` | `LABEL_APPLICATION_PROFILES` |
| `TextSize` | `TEXT_SIZE` |
| `SecondaryTextSize` | `SECONDARY_TEXT_SIZE` |
| `CaptionTextSize` | `CAPTION_TEXT_SIZE` |
| `CornerRadius` | `CORNER_RADIUS` |
| `EffectToggle` | `EFFECT_TOGGLE` |

### viewport.cpp

| Old | New |
|---|---|
| `Property` | `VIEWPORT_PROPERTY` |

### windows_controls.cpp

| Old | New |
|---|---|
| `Key` | `COLOR_FILTER_REGISTRY_KEY` |

### windows_controls.h

| Old | New |
|---|---|
| `FilterNames` | `FILTER_NAMES` |

### workbench.cpp

| Old | New |
|---|---|
| `SidebarWidth` | `SIDEBAR_WIDTH` |
| `PaneOffset` | `PANE_OFFSET` |
| `ClientWidth` | `CLIENT_WIDTH` |
| `ClientHeight` | `CLIENT_HEIGHT` |
| `TrayMessage` | `MESSAGE_TRAY` |
| `ForegroundMessage` | `MESSAGE_FOREGROUND` |
| `Layers` | `ID_EFFECT_LIST` |
| `Contrast` | `ID_CONTRAST` |
| `Pivot` | `ID_PIVOT` |
| `White` | `ID_WHITE` |
| `Gamma` | `ID_GAMMA` |
| `Reset` | `ID_RESET` |
| `Export` | `ID_EXPORT` |
| `Open` | `ID_OPEN` |
| `Exit` | `ID_EXIT` |
| `GammaOther` | `ID_GAMMA_OTHER` |
| `SourceChoice` | `ID_SOURCE_CHOICE` |
| `TestContrast` | `ID_TEST_CONTRAST` |
| `Bypass` | `ID_BYPASS` |
| `HdrFix` | `ID_HDR_FIX` |
| `Shortcut` | `ID_SHORTCUT` |
| `SetShortcut` | `ID_SET_SHORTCUT` |
| `PresetChoice` | `ID_PRESET_CHOICE` |
| `SavePreset` | `ID_SAVE_PRESET` |
| `SavePresetAs` | `ID_SAVE_PRESET_AS` |
| `OpenPreset` | `ID_OPEN_PRESET` |
| `Autosave` | `ID_AUTOSAVE` |
| `RevertPreset` | `ID_REVERT_PRESET` |
| `ApplicationProfiles` | `ID_APPLICATION_PROFILES` |
| `ActiveLook` | `ID_ACTIVE_LOOK` |
| `SettingsPage` | `ID_SETTINGS_PAGE` |
| `FontChoice` | `ID_FONT_CHOICE` |
| `CurveChannel` | `ID_CURVE_CHANNEL` |
| `Import1D` | `ID_IMPORT_1D` |
| `Export1D` | `ID_EXPORT_1D` |
| `AdjustmentBase` | `ID_ADJUSTMENT_BASE` |
| `ToggleWindowsHdr` | `ID_TOGGLE_WINDOWS_HDR` |
| `MonitorStatus` | `ID_MONITOR_STATUS` |
| `OutputInfo` | `ID_OUTPUT_INFO` |
| `ColorFilterChoice` | `ID_COLOR_FILTER_CHOICE` |
| `ColorFilterSettings` | `ID_COLOR_FILTER_SETTINGS` |
| `ColorFilterTimer` | `TIMER_COLOR_FILTER` |
| `NightTimer` | `TIMER_NIGHT` |
| `MatrixRecoveryTimer` | `TIMER_MATRIX_RECOVERY` |
| `NightSchedule` | `ID_NIGHT_SCHEDULE` |
| `NightStart` | `ID_NIGHT_START` |
| `NightEnd` | `ID_NIGHT_END` |
| `NightInclude` | `ID_NIGHT_INCLUDE` |
| `NightDisplay` | `ID_NIGHT_DISPLAY` |
| `MonitorMode` | `ID_MONITOR_MODE` |
| `MonitorProfile` | `ID_MONITOR_PROFILE` |
| `WebsiteLink` | `ID_WEBSITE_LINK` |
| `StartWithWindows` | `ID_START_WITH_WINDOWS` |
| `StartMinimized` | `ID_START_MINIMIZED` |
| `CorrectionLayers` | `ID_CORRECTION_LAYERS` |
| `OrderHint` | `ID_ORDER_HINT` |
| `SelectedEffectToggle` | `ID_SELECTED_EFFECT_TOGGLE` |
| `WindowsCorrectionLayers` | `ID_WINDOWS_CORRECTION_LAYERS` |
| `SaturationReport` | `ID_SATURATION_REPORT` |
| `ApplyTimer` | `TIMER_APPLY` |
| `DisplayTimer` | `TIMER_DISPLAY` |
| `IdleTimer` | `TIMER_IDLE` |
| `PresetTimer` | `TIMER_PRESET` |
| `ForegroundTimer` | `TIMER_FOREGROUND` |
| `HdrTimer` | `TIMER_HDR` |
| `rgb` | `CHANNEL_COLORS` |
| `parameterHelp` | `PARAMETER_HELP` |
| `style` | `WINDOW_STYLE` |

### icon_preview.cpp

| Old | New |
|---|---|
| `TrayMessage` | `MESSAGE_TRAY` |
| `SampleCount` | `SAMPLE_COUNT` |
| `Sizes` | `SIZES` |
| `Files` | `FILES` |
| `Labels` | `LABELS` |
| `style` | `WINDOW_STYLE` |

## 3. Vague global variables

These live for the whole program, so a name like `last` or `store` tells the reader almost nothing.

### workbench.cpp

| Old | New | What it is |
|---|---|---|
| `window` | `main_window` | The application's main window |
| `layers` | `effect_list` | The list of creative effects |
| `correctionLayers` | `correction_list` | The list holding the HDR correction |
| `windowsLayers` | `windows_effect_list` | The list holding color filters and night light |
| `listLabel` | `corrections_heading` | The "Corrections" heading above the list |
| `canvas` | `curve_canvas` | The control that draws the curve |
| `title` | `effect_title` | Heading of the selected effect |
| `description` | `effect_description` | Help text of the selected effect |
| `status` | `status_label` | Status line drawn over the curve |
| `selected` | `selected_effect` | Number of the effect being edited |
| `sources` | `display_sources` | The displays that can be corrected |
| `tray` | `tray_icon` | Notification-area icon data |
| `instance` | `app_instance` | The Windows handle of this program |
| `font` | `ui_font` | Font used by the interface |
| `autosave` | `autosave_enabled` | Whether preset edits save themselves |
| `explorerRestart` | `taskbar_created_message` | Message sent when Explorer restarts |
| `closing` | `is_closing` | The window is shutting down |
| `appConfig` | `profile_configuration` | Rules for application profiles |
| `appFocus` | `foreground_program` | Tracks the program in the foreground |

### profile_editor.cpp

| Old | New | What it is |
|---|---|---|
| `editor` | `editor_window` | The application profiles window |
| `enabled` | `enabled_checkbox` | "Automatic profile selection" |
| `rules` | `rules_list` | "Program rules" list |
| `executable` | `executable_edit` | "Program location" box |
| `browse` | `browse_button` | "Browse for a program" |
| `last` | `use_last_button` | "Use last active program" |
| `action` | `action_choice` | "Action for this program" |
| `look` | `look_choice` | "Preset for this program" |
| `store` | `save_rule_button` | "Add / update rule" |
| `remove` | `remove_rule_button` | "Remove selected rule" |
| `fresh` | `new_rule_button` | "New program rule" |
| `friendly` | `friendly_name_label` | Readable name of the chosen program |
| `background` | `include_background_checkbox` | "Background apps" |
| `sortPrograms` | `sort_programs_choice` | How the running programs are sorted |
| `refreshPrograms` | `refresh_programs_button` | Reloads the running programs |
| `chooseProgram` | `choose_program_button` | Confirms the highlighted program |
| `cancelProgram` | `cancel_program_button` | "Back" out of the picker |
| `font` | `ui_font` | Font used by the window |
| `draft` | `draft_configuration` | Rules being edited, not saved yet |
| `library` | `look_library` | Presets offered for a rule |

## 4. Variables with more than one meaning

`role` means a preset type in some functions and an effect in others, which is the most confusing naming problem left.

| Old | New | Where |
|---|---|---|
| `role (0 or 1)` | `preset_role` | Preset functions: `CurrentPreset`, `FindPreset`, `ReadPreset` and others |
| `role (0 to 11)` | `effect` | Effect functions: `ToggleEffect`, `MoveEffect`, `EffectListRow`, `effect_list.cpp` |
| `selected` | `selected_effect` | Everywhere in `workbench.cpp` |
| `kind` | `glyph` | Glyph drawing in `ui_theme.cpp` |
| `index (into sources)` | `source_index` | `workbench.cpp` |

## 5. Settings

Fields the user can change become PascalCase, as your guide asks. For example `look.contrast` becomes `look.Contrast`. Only the C++ names change; the text saved in files does not.

- **adjustments.h**: `balance`, `midtone`, `levels`, `shadows`, `highlights`, `balanceEnabled`, `gammaEnabled`, `levelsEnabled`, `toneEnabled`, `curvesEnabled`, `importEnabled`, `curves`, `imported`
- **presets.h**: `contrast`, `pivot`, `gamma`, `hdrFix`, `applyInSdr`, `contrastEnabled`, `saturation`, `saturationEnabled`, `monochromeEnabled`, `monochrome`, `adjustments`
- **live_lut.h**: `contrast`, `pivot`, `gamma`, `hdrFix`, `bypass`, `correctionFirst`, `adjustments`, `order`, `applyInSdr`, `nightWarmth`
- **night_light.h**: `enabled`, `scheduled`, `strength`, `start`, `end`, `fade`, `excluded`
- **application_profiles.h**: `executable`, `look`, `action`, `enabled`, `rules`

## 6. Abbreviations

| Old | New | Where | Note |
|---|---|---|---|
| `wp, wParam` | `w_param` | Window procedures | The first message parameter, named after the Windows term |
| `lp, lParam` | `l_param` | Window procedures | The second message parameter |
| `msg` | `message` | Window procedures, message loops |  |
| `cls (WNDCLASSW)` | `window_class` | Window registration |  |
| `cls (text)` | `class_name` | `CreateChildControl`, `control`, class name buffers |  |
| `pc, mc` | `path_count, mode_count` | Display queries | Counts filled in by Windows |
| `p (Shared*)` | `shared` | Shared-memory views | So `view->p->state` becomes `view->shared->state` |
| `h (Handle)` | `handle` | `Handle` structs | Only the field: `h` for height stays |
| `p (display path)` | `path` | Loops over display paths |  |
| `o (Output)` | `output` | `live_lut.cpp`, `ddc_probe.cpp` |  |
| `m (Matrix)` | `matrix` | Matrix code |  |
| `m (display mode)` | `source_mode` | `matrix_effect.cpp` |  |
| `s (settings)` | `settings` | `CurrentPhase`, `CurrentSettings`, tests |  |
| `s, r (byte buffer)` | `utf8_text` | `matrix_effect.cpp`, `ddc_probe.cpp` | String conversion helpers |
| `v` | `value` | Everywhere except `viewport.cpp` | There `h` and `v` become `needs_horizontal` and `needs_vertical` |
| `c (color loop)` | `channel` | Loops over 3 color channels |  |
| `r, c (matrix loop)` | `row, column` | Loops over matrix cells |  |
| `c, e, k, u (FILETIME)` | `creation_time, exit_time, kernel_time, user_time` | CPU time helpers |  |
| `a, b and k, u (ULARGE_INTEGER)` | `kernel_ticks, user_ticks` | `matrix_preview.cpp`, `workbench.cpp` |  |
| `rn, dn, mn` | `ready_name, done_name, mutex_name` | `live_lut.cpp` | Names of Windows events |
| `lo, hi` | `lower, upper` | Curve sampling | The two table entries around a position |
| `op, ob` | `old_pen, old_brush` | `ui_theme.cpp` drawing |  |
| `mr, vr` | `mhc_result, vcgt_result` | `main.cpp` | Results of the color-capability queries |
| `ext` | `extension` | `lut1d.cpp` |  |
| `sat` | `saturation` | `matrix_effect.cpp` |  |
| `gdi` | `gdi32` | `main.cpp` | The loaded system library |
| `rtl` | `get_windows_version` | `main.cpp` | Pointer to `RtlGetVersion` |
| `g` | `geometry` | `CalculateMonitorGeometry` |  |
| `a (adjustments)` | `adjustments` | `workbench.cpp`, `presets` |  |
| `ms` | `elapsed_ms` | `workbench.cpp` timing lambda |  |
| `d` | `diameter` | `ui_theme.cpp` tooltip corners |  |
| `a (angle), r (radius)` | `angle, spoke_radius` | `ui_theme.cpp` sun glyph |  |
| `a, b, c, d` | `x1, y1, x2, y2` | `line` lambda in `ui_theme.cpp` | The ends of a line segment |

**These stay short**, because programmers read them instantly: `i`, `j`, `k`, `n`, `x`, `y`, `w`, `h` (height), `dx`, `dy`, `cx`, `cy`, `t` in curve math, `a` and `b` in comparisons, `r`, `g`, `b` for color, `hwnd`, `dc`, `dpi`, `id`.

## 7. Private members

Your guide uses `_name` for private members, so `impl_` becomes `_impl` and `previousOutput_` becomes `_previous_output`.

`active_`, `api_`, `area_`, `attempted_`, `attempts_`, `baseline_`, `bitmap_`, `checkCarryOver_`, `committed_`, `current_`, `dc_`, `deadline_`, `dll_`, `entries_`, `expectedFilter_`, `expectedOn_`, `gamma_`, `get_`, `hdr_`, `height_`, `impl_`, `initialize_`, `initialized_`, `manual_`, `memory_`, `next_`, `notice_`, `ours_`, `owns_`, `pending_`, `pixels_`, `previousBaseline_`, `previousCorrection_`, `previousOutput_`, `previous_`, `process_`, `result_`, `saved_`, `set_`, `settings_`, `slots_`, `target_`, `uninitialize_`, `valid_`, `whiteCode_`, `whiteNits_`, `white_`, `width_`, `zoom_`

## 8. camelCase to snake_case

These already say what they are, and only the form changes: `hdrPending` becomes `hdr_pending`.

**src/app_settings.h**: `_wcsicmp`, `keyPath`

**src/application_profiles.cpp**: `countText`

**src/color_math.h**: `correctGamma`, `correctedNits`, `correctionFirst`, `relativeLight`, `relativeOdds`, `whiteNits`

**src/effect_list.cpp**: `heightChanged`, `lineInk`, `oldFont`, `selectedRole`, `sourceRole`, `subFont`

**src/effect_list.h**: `hdrPaused`, `selectedRole`

**src/live_lut.cpp**: `carriedBaseline`, `carriedCorrection`, `carriedOutput`, `colorKnown`, `crashParent`, `currentOutput`, `doneName`, `flagsAt`, `mapName`, `modeAt`, `modeKnown`, `modernKnown`, `nvidiaSources`, `ownCarryOver`, `ownerCommand`, `ownerProcess`, `ownerStart`, `parentId`, `readyName`, `reliableLegacy`, `resetToIdentity`, `resultFile`, `sdrFix`, `targetHdr`, `writerLeases`

**src/live_lut.h**: `carriedBaseline`, `carriedCorrection`, `carriedOutput`, `crashParent`, `resetToIdentity`

**src/lut1d.cpp**: `hasHeader`, `shortName`

**src/main.cpp**: `cancelEvent`, `colorResult`, `doneName`, `getDefault`, `leaseError`, `parentId`, `readyName`, `readyOk`, `startWait`, `stillWaiting`, `tempLength`

**src/matrix_effect.cpp**: `exitReported`, `foregroundName`, `observedProcess`, `orphanWorker`, `ownerCommand`, `ownerInfo`, `ownerPid`, `parentId`, `previousAmount`, `previousPid`, `processCopy`, `readOnly`, `stopStart`, `workerCommand`, `workerInfo`

**src/matrix_preview.cpp**: `appliedAt`, `blockedShortcut`, `cancelReason`, `cpuStart`, `dropWrite`, `falseFlag`, `falseReturn`, `firstReadFails`, `hotkeyId`, `hotkeyRegistered`, `parentId`, `previewWindow`

**src/night_light.h**: `nextMs`

**src/night_outputs.h**: `lastLevel`, `wasActive`

**src/profile_editor.cpp**: `actionLabel`, `actionWidth`, `applyButton`, `applyChanges`, `applyWidth`, `browseWidth`, `cancelWidth`, `chooseWidth`, `closeButton`, `closeWidth`, `exeLabel`, `lastApplication`, `lastWidth`, `lookLabel`, `lookX`, `pickerNote`, `pickerTitle`, `programList`, `programPath`, `refreshWidth`, `removeWidth`, `returnNote`, `runButton`, `runningPrograms`, `sortWidth`, `storeWidth`, `themeResult`

**src/programs.cpp**: `byMemory`, `includeBackground`

**src/programs.h**: `byMemory`, `includeBackground`, `memoryKnown`

**src/tray_popup.cpp**: `ignoreOpeningRelease`, `newRow`, `oldA`, `oldB`, `oldBrush`, `oldFont`, `oldPen`, `oldRow`, `paintingList`, `rowHeight`

**src/tray_popup.h**: `separatorBefore`

**src/ui_theme.cpp**: `childSaved`, `clientClip`, `colorAt`, `defaultValue`, `doubleClick`, `edgeColor`, `framedList`, `hoverChanged`, `labelFont`, `labelStyle`, `oldBrush`, `oldFont`, `oldPen`, `powerLabel`, `priorBrush`, `priorPen`, `suppressUp`, `thumbLetter`, `trackSelection`

**src/ui_theme.h**: `clientHeight`, `clientWidth`, `defaultValue`, `offKnob`, `offTrack`, `onKnob`, `onTrack`, `thumbLetter`, `unpoweredTrack`

**src/windows_controls.cpp**: `selectingFilter`

**src/windows_controls.h**: `helperCode`, `helperExited`, `requestedFilter`, `requestedOn`, `selectingFilter`

**src/workbench.cpp**: `_wcsicmp`, `activeHotkeyId`, `activeLookLabel`, `activeShortcut`, `activeTest`, `activeText`, `adjustmentLabels`, `adjustmentPage`, `adjustmentSliders`, `adjustmentValues`, `alignTrack`, `alignedRow`, `amountLabel`, `amountValue`, `applicationContextActive`, `argumentCount`, `autosaveControl`, `axisFont`, `axisStyle`, `badIndex`, `beforeFolderImport`, `beforeMatrix`, `benchmarkReport`, `blueOn`, `bypassButton`, `canPower`, `captionMenuOkay`, `captionTest`, `captionTip`, `changingAdjustmentPage`, `choiceWidth`, `chooseLook`, `comboInfo`, `comboResult`, `contrastBeforePause`, `contrastEnabled`, `contrastPosition`, `contrastSlider`, `controlBounds`, `cornerBitmap`, `cornerDC`, `cornerOld`, `correctionBeforePause`, `correctionFirst`, `correctionNote`, `correctionPowered`, `correctionsRequested`, `cpuMs`, `currentPath`, `curveChannel`, `curveScratch`, `darkTail`, `defaultValue`, `deferEditor`, `dirtyBufferBytes`, `dirtyFaultStart`, `dirtyFaults`, `dirtyMs`, `dirtySaved`, `displayAttempts`, `displayRetryPending`, `draggedPoint`, `editContrast`, `editShape`, `editablePath`, `editingChannel`, `editorReady`, `editorSyncDepth`, `editorVisible`, `effectBounds`, `effectHeight`, `effectLayoutValid`, `effectOrder`, `effectRows`, `effectUiTest`, `effectsLabel`, `export1DButton`, `exportButton`, `failureText`, `filterChoice`, `filterController`, `filterError`, `filterMessage`, `filterSettingsButton`, `filterState`, `firstCanvas`, `firstFaultStart`, `firstFaults`, `firstInput`, `firstMs`, `fontChoice`, `fontLabel`, `foregroundHook`, `fullBufferBytes`, `fullBytes`, `gammaChoice`, `gammaLabel`, `gammaOther`, `gammaPosition`, `gammaWidth`, `graphAuditCount`, `graphAuditDamage`, `graphLayoutVersion`, `graphStatusVisible`, `graphTop`, `grayOn`, `hadWarning`, `hdrAttempts`, `hdrButton`, `hdrDesired`, `hdrFixControl`, `hdrFixEnabled`, `hdrMenuItem`, `hdrModel`, `hdrPending`, `hdrResume`, `hdrRules`, `hdrSource`, `hdrState`, `hdrTransition`, `helpEditor`, `helperMs`, `hintFont`, `hintStyle`, `hotkeyError`, `hotkeyMessage`, `hotkeyRegistered`, `iconCache`, `idleCpuStart`, `idleFaultStart`, `idleReport`, `idleStart`, `importButton`, `importWidth`, `infoButton`, `inputShape`, `invalidRejected`, `keyName`, `keyboardPoint`, `lParam`, `largeIcon`, `lastGraphPaintBytes`, `lastPresetError`, `lastToolbar`, `liveError`, `liveMessage`, `liveOutput`, `liveSession`, `lookDirtyBeforeDrag`, `lookName`, `lookState`, `lowerCenter`, `matrixMessage`, `matrixRecovery`, `matrixSession`, `minimizedCheck`, `modeGone`, `modeResetPending`, `monitorIndicator`, `monitorInfo`, `monitorMode`, `monitorProfile`, `monitorSettingsButton`, `monitorTips`, `mutexName`, `needsGuide`, `nextId`, `nextTip`, `nightConfig`, `nightDisplay`, `nightEnd`, `nightEndLabel`, `nightInclude`, `nightIndex`, `nightLevel`, `nightMessage`, `nightOutputs`, `nightPowered`, `nightSchedule`, `nightStart`, `nightStartLabel`, `nightStopped`, `nightSync`, `nightValid`, `normalFill`, `normalSize`, `normalStyle`, `oldBounds`, `oldBrush`, `oldEnabled`, `oldError`, `oldFont`, `oldIndex`, `oldLook`, `oldMessage`, `oldPen`, `oldPosition`, `oldReleased`, `openMs`, `openPresetButton`, `openStart`, `orderHint`, `originalCorrection`, `originalData`, `originalHeight`, `originalIndex`, `originalLook`, `otherPoints`, `paintBackground`, `pathBefore`, `pendingForeground`, `pivotLabel`, `pivotPosition`, `pivotSlider`, `pivotValue`, `plotRect`, `popupBounds`, `popupHeight`, `popupShape`, `popupWidth`, `powerButton`, `powerSuspended`, `preferredShortcut`, `presetChoice`, `presetIndex`, `presetLabel`, `presetLibrary`, `presetMessage`, `presetUiTest`, `previewOnly`, `previousBrush`, `previousFamily`, `previousKeys`, `previousLayout`, `previousLive`, `previousPage`, `previousPresetMessage`, `previousSettings`, `previousText`, `previousValues`, `primaryStopped`, `priorPower`, `profileAction`, `profileActive`, `profileBounds`, `profileMessage`, `profilePausedIdentity`, `profileText`, `profileUiTest`, `profilesButton`, `queryBatch`, `regularLookIndex`, `renderApplication`, `renderFile`, `renderHdr`, `renderMode`, `renderOnly`, `renderProfiles`, `renderTray`, `repaintFaultStart`, `repaintFaults`, `repaintMs`, `resetButton`, `resetPoints`, `resumeAfterWake`, `retainRequest`, `revertPresetButton`, `rgbOk`, `roundedEdit`, `roundedPopup`, `roundedShortcut`, `rowTop`, `runtimeReady`, `sameCornerInk`, `sameHoverFill`, `saturationReportButton`, `saveAs`, `saveAsButton`, `savePresetButton`, `savedBypass`, `savedCorrection`, `savedFilter`, `savedFocus`, `savedHdrState`, `savedKeys`, `savedLook`, `savedMixed`, `savedNight`, `savedOrder`, `savedPage`, `savedPath`, `savedSelected`, `secondLine`, `selectedEffectToggle`, `selectedSource`, `selectingFilter`, `settingsButton`, `settingsPage`, `setupFaultStart`, `setupFaults`, `setupMs`, `setupStart`, `shiftBound`, `shortcutButton`, `shortcutControl`, `shortcutLabel`, `shortcutText`, `shutdownMs`, `sideCenter`, `sliderDragging`, `sliderPoint`, `smallIcon`, `sourceChoice`, `sourceIndex`, `startMinimized`, `startMinimizedControl`, `startupCheck`, `startupControl`, `startupEnabled`, `statusArea`, `statusError`, `statusFont`, `statusText`, `storedLook`, `surfaceUiTest`, `switchLook`, `tabResult`, `testNote`, `testSettingsPath`, `textBounds`, `textHeight`, `tileBytes`, `tipFont`, `toneFont`, `trayBenchmark`, `trayBenchmarkReport`, `trayExitTest`, `trayInstalled`, `trayStartupTest`, `unavailableRejected`, `unicodePath`, `unsavedResult`, `versionLabel`, `visibleControl`, `wParam`, `websiteLink`, `whiteLabel`, `whitePosition`, `whiteSlider`, `whiteValue`, `windowDpi`

**tests/live_tests.cpp**: `calibratedStale`, `contrastOnly`, `creativeHdr`, `gammaCalibrated`, `gammaOnly`, `hdrState`, `keyA`, `keyB`, `nextSdr`, `originalHdr`, `previousSdr`, `resetHdr`, `resumedSdr`, `reverseRamp`, `sameSdr`, `settingsAfterSwitch`, `staleHdr`, `staleSettings`

**tests/preset_tests.cpp**: `correctionText`, `disabledLook`, `lookText`, `matrixLook`, `matrixText`, `mixerLook`, `mixerText`, `sdrCorrection`, `strongText`

**tests/render_tests.cpp**: `tallDC`, `tallInfo`, `tallPixels`

**tools/ddc_probe.cpp**: `powerOff`, `readyName`

**tools/icon_preview.cpp**: `allButton`, `closeButton`, `showWindow`, `taskbarCreated`, `trayIcons`, `traySize`

**tools/matrix_probe.cpp**: `initError`, `readError`, `zoomRead`

**tools/performance_probe.cpp**: `peakPrivate`, `peakWorking`
