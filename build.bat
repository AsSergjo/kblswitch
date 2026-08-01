@echo off
setlocal

REM Stop a running instance so the exe can be overwritten
taskkill /IM kblswitch.exe /F >nul 2>nul

REM Output folder
set "SCRIPT_DIR=%~dp0"
set "OUTDIR=%SCRIPT_DIR%build\bin\Release"
if not exist "%OUTDIR%" mkdir "%OUTDIR%"

REM Try to set up 64-bit build environment
echo Setting up 64-bit environment...
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" 2>nul
if errorlevel 1 (
    call "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat" 2>nul
    if errorlevel 1 (
        call "C:\Program Files (x86)\Microsoft Visual Studio\2017\Community\VC\Auxiliary\Build\vcvars64.bat" 2>nul
        if errorlevel 1 (
            echo ERROR: Cannot setup 64-bit Visual Studio environment
            echo Please run from "x64 Native Tools Command Prompt for VS"
            exit /b 1
        )
    )
)

echo Compiling resources...
rc.exe /nologo /fo "%OUTDIR%\resource.res" resource.rc
if errorlevel 1 (
    echo RESOURCE COMPILATION FAILED
    exit /b 1
)

echo Compiling kblswitch...
cl.exe /c /nologo /DUNICODE /D_UNICODE /utf-8 /std:c++17 /EHsc /Fo"%OUTDIR%\kblswitch.obj" kblswitch.cpp
if errorlevel 1 (
    echo COMPILATION FAILED
    exit /b 1
)

echo Compiling input module...
cl.exe /c /nologo /DUNICODE /D_UNICODE /utf-8 /std:c++17 /EHsc /Fo"%OUTDIR%\input.obj" input.cpp
if errorlevel 1 (
    echo INPUT COMPILATION FAILED
    exit /b 1
)

echo Compiling overlay module...
cl.exe /c /nologo /DUNICODE /D_UNICODE /utf-8 /std:c++17 /EHsc /Fo"%OUTDIR%\overlay.obj" overlay.cpp
if errorlevel 1 (
    echo OVERLAY COMPILATION FAILED
    exit /b 1
)

echo Compiling tray menu module...
cl.exe /c /nologo /DUNICODE /D_UNICODE /utf-8 /std:c++17 /EHsc /Fo"%OUTDIR%\tray_menu.obj" tray_menu.cpp
if errorlevel 1 (
    echo TRAY MENU COMPILATION FAILED
    exit /b 1
)

echo Compiling settings apply module...
cl.exe /c /nologo /DUNICODE /D_UNICODE /utf-8 /std:c++17 /EHsc /Fo"%OUTDIR%\settings_apply.obj" settings_apply.cpp
if errorlevel 1 (
    echo SETTINGS APPLY COMPILATION FAILED
    exit /b 1
)

echo Compiling settings window...
cl.exe /c /nologo /DUNICODE /D_UNICODE /utf-8 /std:c++17 /EHsc /Fo"%OUTDIR%\setting.obj" setting.cpp
if errorlevel 1 (
    echo SETTINGS COMPILATION FAILED
    exit /b 1
)

echo Compiling language manager...
cl.exe /c /nologo /DUNICODE /D_UNICODE /utf-8 /std:c++17 /EHsc /Fo"%OUTDIR%\lng_manager.obj" lng_manager.cpp
if errorlevel 1 (
    echo LNG MANAGER COMPILATION FAILED
    exit /b 1
)

echo Linking...
link.exe "%OUTDIR%\kblswitch.obj" "%OUTDIR%\input.obj" "%OUTDIR%\overlay.obj" "%OUTDIR%\tray_menu.obj" "%OUTDIR%\settings_apply.obj" "%OUTDIR%\setting.obj" "%OUTDIR%\lng_manager.obj" "%OUTDIR%\resource.res" /SUBSYSTEM:WINDOWS /OUT:"%OUTDIR%\kblswitch.exe" user32.lib gdi32.lib shell32.lib advapi32.lib gdiplus.lib comctl32.lib comdlg32.lib
if errorlevel 1 (
    echo LINK FAILED
    exit /b 1
)

REM Copy ini and icon next to the exe (app reads ini from its folder)
if exist "%SCRIPT_DIR%kblswitch.ini" copy /Y "%SCRIPT_DIR%kblswitch.ini" "%OUTDIR%\kblswitch.ini" >nul
if exist "%SCRIPT_DIR%app.ico" copy /Y "%SCRIPT_DIR%app.ico" "%OUTDIR%\app.ico" >nul

REM Copy language files (rus.lng / eng.lng)
if exist "%SCRIPT_DIR%language" xcopy /Y /E /I "%SCRIPT_DIR%language" "%OUTDIR%\language" >nul

echo.
echo BUILD SUCCESSFUL!
echo Output: %OUTDIR%\kblswitch.exe
exit /b 0