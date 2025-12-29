@echo off
echo Building kblswitch with CMake...
echo.

REM Проверяем наличие CMake
where cmake >nul 2>nul
if %errorlevel% neq 0 (
    echo Error: CMake is not installed or not in PATH.
    echo Please install CMake from https://cmake.org/download/
    pause
    exit /b 1
)

REM Создаем директорию для сборки
if not exist "build" mkdir build
cd build

REM Конфигурируем проект
echo Configuring project...
cmake .. -G "Visual Studio 17 2022"

if %errorlevel% neq 0 (
    echo CMake configuration failed.
    pause
    exit /b 1
)

REM Собираем проект
echo Building project...
cmake --build . --config Release

if %errorlevel% neq 0 (
    echo Build failed.
    pause
    exit /b 1
)

echo.
echo Build completed successfully!
echo Executable: build\bin\Release\kblswitch.exe
echo.
echo To run the application:
echo   build\bin\Release\kblswitch.exe
echo.
pause