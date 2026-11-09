@echo off
setlocal
set "DEVELOPER=OFF"
if /i "%~1"=="developer" set "DEVELOPER=ON"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo Visual Studio C++ Build Tools are required.
  exit /b 1
)
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "LUT_VS=%%i"
if not defined LUT_VS exit /b 1
call "%LUT_VS%\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
cmake -S "%~dp0." -B "%~dp0build-native" -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release -DMONITORMAGE_DEVELOPER_BUILD=%DEVELOPER% -DCMAKE_MAKE_PROGRAM="%VCToolsInstallDir%bin\Hostx64\x64\nmake.exe" %*
if errorlevel 1 exit /b 1
cmake --build "%~dp0build-native"
if errorlevel 1 exit /b 1
ctest --test-dir "%~dp0build-native" --output-on-failure
exit /b %ERRORLEVEL%
