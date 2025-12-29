# Сборка kblswitch с помощью CMake

## Требования

- CMake 3.10 или выше
- Компилятор C (MSVC для Windows, MinGW или другой совместимый)
- Для Windows: Visual Studio или Build Tools

## Быстрая сборка (Windows)

### Способ 1: Использование CMake GUI
1. Установите CMake с официального сайта
2. Запустите CMake GUI
3. Укажите исходную директорию (где находится CMakeLists.txt)
4. Укажите директорию для сборки (например, `build`)
5. Нажмите Configure, выберите генератор (Visual Studio, MinGW и т.д.)
6. Нажмите Generate
7. Откройте сгенерированный проект в выбранной IDE или соберите через командную строку

### Способ 2: Командная строка
```bash
# Создать директорию для сборки
mkdir build
cd build

# Сгенерировать проект
cmake ..

# Собрать проект
cmake --build . --config Release
```

Исполняемый файл будет создан в `build/bin/kblswitch.exe`

### Способ 3: Использование Visual Studio Developer Command Prompt
```cmd
mkdir build
cd build
cmake -G "Visual Studio 17 2022" ..
cmake --build . --config Release
```

## Сборка с MinGW
```bash
mkdir build
cd build
cmake -G "MinGW Makefiles" ..
cmake --build .
```

## Структура проекта

- `CMakeLists.txt` - основной файл конфигурации CMake
- `kblswitch.c` - исходный код приложения
- `resource.h`, `resource.rc` - файлы ресурсов
- `app.ico` - иконка приложения
- `kblswitch.ini` - файл конфигурации

## Доступные цели сборки

- `kblswitch` - основная цель (исполняемый файл)
- `release` - сборка в режиме Release
- `clean-all` - полная очистка директории сборки

## Настройки компиляции

Проект использует следующие настройки:
- Unicode поддержка (`UNICODE`, `_UNICODE`)
- Стандарт C11
- Кодировка UTF-8
- Подсистема Windows (без консольного окна)

## Подключенные библиотеки

- `user32` - Windows User API
- `gdi32` - Graphics Device Interface
- `shell32` - Shell API
- `dwmapi` - Desktop Window Manager API
- `comctl32` - Common Controls

## Примечания

1. Для корректной работы приложения файл `kblswitch.ini` должен находиться в той же директории, что и исполняемый файл
2. Иконка `app.ico` автоматически копируется в выходную директорию
3. При сборке в Debug режиме добавляются отладочные символы