// settings.h - общий интерфейс настроек и окна настроек KBLSWITCH.
// Весь проект компилируется как C++17.

#ifndef KBLSWITCH_SETTINGS_H
#define KBLSWITCH_SETTINGS_H

#include <windows.h>

#define SETTINGS_INI_NAME L"kblswitch.ini"

// Все настройки приложения (секция [Settings] файла kblswitch.ini)
typedef struct Settings {
    UINT     key;                 // Клавиша переключения раскладки (VK-код)
    BOOL     modCtrl;             // Эмулировать Ctrl
    BOOL     modShift;            // Эмулировать Shift
    BOOL     modAlt;              // Эмулировать Alt
    UINT     fixRuToEnKey;        // Исправить слово: RU -> EN (VK-код, 0 = выкл.)
    UINT     fixEnToRuKey;        // Исправить слово: EN -> RU (VK-код, 0 = выкл.)
    BOOL     alwaysShowOsd;       // Постоянно показывать OSD
    BYTE     osdAlpha;            // Прозрачность OSD (0..255)
    BOOL     showCaretIndicator;  // Индикатор раскладки у курсора ввода
    DWORD    indicatorTimeoutMs;  // Таймаут автозакрытия индикатора (мс), 0 = не закрывать
    COLORREF osdColor;            // Цвет фона OSD
    int      language;            // Язык интерфейса: 1 = русский, 2 = английский
} Settings;

// Путь к INI-файлу (рядом с исполняемым файлом)
void SettingsGetIniPath(TCHAR* buffer, int bufferSize);

// Загрузка настроек из INI (с значениями по умолчанию)
void SettingsLoad(Settings* s);

// Сохранение настроек в INI
void SettingsSave(const Settings* s);

// Применение настроек к работающему приложению.
// Реализуется в kblswitch.c, вызывается из окна настроек.
void SettingsApplyToApp(const Settings* s);

// --- Окно настроек (реализация в setting.c) ---
// Открывает модальное окно настроек (блокирует до закрытия).
void SettingsShowDialog(HWND owner, HINSTANCE hInstance);

// Открыто ли окно настроек и можно ли получить его HWND
BOOL SettingsDialogIsOpen(void);
HWND SettingsGetHwnd(void);

// --- Захват клавиш (используется хуком клавиатуры в kblswitch.c) ---
// Идёт ли сейчас захват клавиши в окне настроек
BOOL SettingsIsCapturing(void);

// Вызывается из низкоуровневого хука, когда пользователь нажал клавишу
// во время захвата. Клавиша уже должна быть "поглощена" (возврат 1 из хука).
void SettingsOnCapturedKey(UINT vk);

#endif // KBLSWITCH_SETTINGS_H
