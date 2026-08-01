# Сборка kblswitch с помощью CMake

> Проект компилируется как **C++17**, требует Windows 10/11.

## Требования

- CMake 3.10 или выше
- Visual Studio 2022 (или Build Tools) с компонентом C++ (MSVC)

## Быстрая сборка (Windows)

### Командная строка

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Исполняемый файл будет создан в `build/bin/Release/kblswitch.exe` (вместе с `app.ico` и папкой `language\`).

### Visual Studio Developer Command Prompt

```cmd
mkdir build
cd build
cmake -G "Visual Studio 17 2022" -A x64 ..
cmake --build . --config Release
```

### Готовый скрипт

Просто запустите `build_cmake.bat`.

## Структура проекта

- `CMakeLists.txt` - основной файл конфигурации CMake
- `kblswitch.cpp` (+ `input.cpp`, `overlay.cpp`, `tray_menu.cpp`, `settings_apply.cpp`, `setting.cpp`, `lng_manager.cpp`) - исходный код
- `kblswitch.h`, `settings.h`, `lng_manager.h` - заголовки
- `resource.h`, `resource.rc` - ресурсы (иконка, версия 2.0.0.0)
- `app.ico` - иконка приложения
- `language/` - языковые файлы (rus.lng, eng.lng)

## Доступные цели сборки

- `kblswitch` - основная цель (исполняемый файл)
- `release` - сборка в режиме Release
- `clean-all` - полная очистка директории сборки

## Настройки компиляции

Проект использует следующие настройки:
- Unicode поддержка (`UNICODE`, `_UNICODE`)
- Стандарт C++17 (`/std:c++17`)
- Кодировка UTF-8 (`/utf-8`)
- Подсистема Windows (без консольного окна)

## Подключенные библиотеки

- `user32` - Windows User API
- `gdi32` - Graphics Device Interface
- `gdiplus` - GDI+ (окно настроек)
- `shell32` - Shell API (иконка трея)
- `advapi32` - Advanced API (реестр)
- `dwmapi` - Desktop Window Manager API
- `comctl32` - Common Controls
- `comdlg32` - Common Dialogs

## Примечания

1. Файл `kblswitch.ini` создаётся автоматически при первом изменении настроек.
2. Иконка `app.ico` и языковые файлы автоматически копируются в выходную директорию.
3. При сборке в Debug режиме добавляются отладочные символы.
