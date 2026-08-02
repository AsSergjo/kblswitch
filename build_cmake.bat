@echo off
setlocal EnableExtensions

set "SCRIPT_DIR=%~dp0"
cd /d "%SCRIPT_DIR%" || (
    echo ERROR: Cannot open project directory: %SCRIPT_DIR%
    exit /b 1
)

REM Stop a running instance so the linker can replace kblswitch.exe.
echo Stopping running kblswitch instance...
taskkill /IM kblswitch.exe /F >nul 2>&1
for /L %%I in (1,1,10) do (
    tasklist /FI "IMAGENAME eq kblswitch.exe" /NH 2>nul | find /I "kblswitch.exe" >nul
    if errorlevel 1 goto process_stopped
    timeout /T 1 /NOBREAK >nul
)

echo ERROR: Cannot stop kblswitch.exe. Run build_cmake.bat as administrator.
exit /b 1

:process_stopped
echo Building kblswitch with CMake...
echo.

REM Check for CMake.
where cmake >nul 2>nul
if errorlevel 1 (
    echo ERROR: CMake is not installed or not in PATH.
    echo Please install CMake from https://cmake.org/download/
    if /I not "%CI%"=="true" pause
    exit /b 1
)

REM Let CMake select the installed Visual Studio version and edition.
echo Configuring project...
if exist "build\CMakeCache.txt" (
    echo Reusing the existing CMake generator and platform...
    cmake -S . -B build
) else (
    cmake -S . -B build -A x64
)
if errorlevel 1 (
    echo ERROR: CMake configuration failed.
    echo Install Visual Studio or Build Tools with "Desktop development with C++".
    echo If the installed Visual Studio version changed, remove the build folder and try again.
    if /I not "%CI%"=="true" pause
    exit /b 1
)

REM Build the Release configuration. CMake copies app.ico and language files.
echo Building project...
cmake --build build --config Release
if errorlevel 1 (
    echo ERROR: Build failed.
    if /I not "%CI%"=="true" pause
    exit /b 1
)

echo.
echo Build completed successfully!
echo Executable: build\bin\Release\kblswitch.exe
echo.
echo To run the application:
echo   build\bin\Release\kblswitch.exe
echo.
if /I not "%CI%"=="true" pause
exit /b 0
