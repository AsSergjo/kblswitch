@echo off
setlocal EnableExtensions

set "SCRIPT_DIR=%~dp0"
cd /d "%SCRIPT_DIR%" || (
    echo ERROR: Cannot open project directory: %SCRIPT_DIR%
    exit /b 1
)

REM Stop a running instance and wait until the executable is no longer locked.
REM taskkill returns an error when no instance exists, so verify with tasklist.
echo Stopping running kblswitch instance...
taskkill /IM kblswitch.exe /F >nul 2>&1
for /L %%I in (1,1,10) do (
    tasklist /FI "IMAGENAME eq kblswitch.exe" /NH 2>nul | find /I "kblswitch.exe" >nul
    if errorlevel 1 goto process_stopped
    timeout /T 1 /NOBREAK >nul
)

echo ERROR: Cannot stop kblswitch.exe. Run build.bat as administrator.
exit /b 1

:process_stopped

REM Output folder
set "OUTDIR=%SCRIPT_DIR%build\bin\Release"
if not exist "%OUTDIR%" mkdir "%OUTDIR%"

REM Use an existing developer environment or locate any supported Visual Studio
REM edition, including Build Tools, through vswhere.
echo Setting up 64-bit environment...
where cl.exe >nul 2>&1
if not errorlevel 1 where rc.exe >nul 2>&1
if not errorlevel 1 goto toolchain_ready

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"

if exist "%VSWHERE%" (
    for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%I"
)

if defined VSINSTALL call "%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat"

where cl.exe >nul 2>&1
if errorlevel 1 goto toolchain_missing
where rc.exe >nul 2>&1
if errorlevel 1 goto toolchain_missing

:toolchain_ready
echo Compiling resources...
rc.exe /nologo /fo "%OUTDIR%\resource.res" resource.rc
if errorlevel 1 goto resource_failed

for %%S in (kblswitch input overlay tray_menu settings_apply setting lng_manager) do (
    echo Compiling %%S.cpp...
    cl.exe /c /nologo /DUNICODE /D_UNICODE /utf-8 /std:c++17 /EHsc /Fo"%OUTDIR%\%%S.obj" "%%S.cpp"
    if errorlevel 1 goto compile_failed
)

echo Linking...
link.exe "%OUTDIR%\kblswitch.obj" "%OUTDIR%\input.obj" "%OUTDIR%\overlay.obj" "%OUTDIR%\tray_menu.obj" "%OUTDIR%\settings_apply.obj" "%OUTDIR%\setting.obj" "%OUTDIR%\lng_manager.obj" "%OUTDIR%\resource.res" /SUBSYSTEM:WINDOWS /OUT:"%OUTDIR%\kblswitch.exe" user32.lib gdi32.lib shell32.lib advapi32.lib dwmapi.lib gdiplus.lib comctl32.lib comdlg32.lib
if errorlevel 1 goto link_failed

REM Copy ini and icon next to the exe (app reads ini from its folder)
if exist "%SCRIPT_DIR%kblswitch.ini" copy /Y "%SCRIPT_DIR%kblswitch.ini" "%OUTDIR%\kblswitch.ini" >nul
if exist "%SCRIPT_DIR%app.ico" copy /Y "%SCRIPT_DIR%app.ico" "%OUTDIR%\app.ico" >nul

REM Copy language files (rus.lng / eng.lng)
if exist "%SCRIPT_DIR%language" xcopy /Y /E /I "%SCRIPT_DIR%language" "%OUTDIR%\language" >nul

echo.
echo BUILD SUCCESSFUL!
echo Output: %OUTDIR%\kblswitch.exe
exit /b 0

:toolchain_missing
echo ERROR: MSVC C++ build tools were not found.
echo Install Visual Studio or Build Tools with "Desktop development with C++".
echo You can also run this script from "x64 Native Tools Command Prompt for VS".
exit /b 1

:resource_failed
echo ERROR: Resource compilation failed.
exit /b 1

:compile_failed
echo ERROR: C++ compilation failed.
exit /b 1

:link_failed
echo ERROR: Linking failed.
exit /b 1
