@echo off
setlocal
cd /d "%~dp0"
set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Release"
if /I not "%CONFIG%"=="Debug" if /I not "%CONFIG%"=="Release" exit /b 2
if not defined VULKAN_SDK for /f "tokens=2,*" %%a in ('reg query "HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Environment" /v VULKAN_SDK 2^>nul') do set "VULKAN_SDK=%%b"
set "VSROOT="
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%i"
if not defined VSROOT set "VSROOT=%ProgramFiles%\Microsoft Visual Studio\2022\Community"
call "%VSROOT%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
set "VOX_DRIVE="
rem The VS 2022 CMake build crashes in compiler discovery on some CJK paths.
for %%d in (V U T S R Q P) do if not defined VOX_DRIVE if not exist %%d:\ set "VOX_DRIVE=%%d:"
if not defined VOX_DRIVE exit /b 1
subst %VOX_DRIVE% "%CD%" || exit /b 1
pushd %VOX_DRIVE%\
call :compile
set "VOX_RESULT=%errorlevel%"
popd
subst %VOX_DRIVE% /D
exit /b %VOX_RESULT%
:compile
cmake -S . -B "build/%CONFIG%-%VOX_DRIVE:~0,1%" -G Ninja -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl -DCMAKE_BUILD_TYPE=%CONFIG% || exit /b 1
cmake --build "build/%CONFIG%-%VOX_DRIVE:~0,1%" --parallel 6 || exit /b 1
ctest --test-dir "build/%CONFIG%-%VOX_DRIVE:~0,1%" --output-on-failure
exit /b %errorlevel%
