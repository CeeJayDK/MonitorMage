# Building MonitorMage

The beta download is ready to run. These steps are for building the source.

## Requirements

- Windows, with an x64 build environment.
- Visual Studio 2022 or newer, or its Build Tools, with Desktop development with C++
  and a Windows SDK installed.
- CMake available on PATH.

## Build and test

1. Download or clone this repository.
2. Open a command prompt in its root folder.
3. Run `build.cmd`.

The script finds Visual Studio with vswhere, configures a Release build in
`build-native`, builds the targets and runs CTest. The main app is
`build-native/MonitorMage.exe`. Copy `presets` beside it for the example looks.

From an x64 Visual Studio developer command prompt, the equivalent commands are:

```bat
cmake -S . -B build-native -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build-native
ctest --test-dir build-native --output-on-failure
```

The main CMake target is internally named `LutWorkbench`; its output is
`MonitorMage.exe`. The other executables are development tools and tests,
not required by users of the app. The app uses the static MSVC runtime.

`build.cmd` makes a release build. `build.cmd developer` makes a developer build,
where **Save diagnostic logs** on the Settings page starts turned on. With that
setting on, each correction session writes a log to the temp folder and keeps it.

The supplied ICO files are build inputs. Node.js and Python are not needed for
a normal build. `tools/build_shared_icons.cjs` can regenerate the current icons
with Node.js and the optional `sharp` package. `tools/make_lut_fixtures.py` can
regenerate format-test images with Python and Pillow.

## Tests and hardware

Tests cover color math, LUT formats, presets, profiles, keyboard navigation,
window sizing, rendering and isolated helper-process recovery. Fake display APIs
avoid changing screen colors in automated recovery tests.

Some checks need access to a normal Windows desktop, global hotkeys or an
isolated registry key. At beta preparation, 26 of 27 checks passed in the
restricted development environment; `startup-registration` was denied registry
access with error 5. Hardware validation is still needed for actual color output,
startup at sign-in, sleep/resume and display-mode changes. See BETA_CHECKLIST.txt.

`MonitorMageDdcProbe` is a development-only hardware research tool. Its inspect
mode only reads monitor information; its explicitly selected power-test modes
can switch a monitor off. It is not included in the beta download.

## Package versions

The first beta package is 0.111.2; its unchanged app executable is 0.111.0.
A package revision can update documentation without rebuilding the executable.

## Sample LUTs and licenses

The example look presets are in `presets`. Optional import samples are in `luts`;
that folder includes the separate CC0 and third-party notices for those data.
The application license is LICENSE.txt.
