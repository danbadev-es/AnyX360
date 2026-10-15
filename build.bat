@echo off
setlocal
rem Usage: build.bat [debug|release]   (default: release)
set CONFIG=%1
if "%CONFIG%"=="" set CONFIG=release

for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath`) do set VSDIR=%%i
if not defined VSDIR (
  echo Visual Studio with the "Desktop development with C++" workload and Clang tools is required.
  exit /b 1
)
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1

cmake --preset windows-clang || exit /b 1
cmake --build --preset %CONFIG% || exit /b 1
echo.
echo Build finished: build\tools\x360tool\%CONFIG%\x360tool.exe
