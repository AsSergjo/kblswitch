// kblswitch.h - общий заголовок KBLSWITCH: константы, общие глобальные
// переменные (extern) и прототипы функций, используемые во всех модулях.

#ifndef KBLSWITCH_H
#define KBLSWITCH_H

#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <windowsx.h>
#include <tchar.h>
#include <stdio.h>
#include <string>
#include <shellapi.h>
#include <dwmapi.h>

#include "resource.h"
#include "settings.h"
#include "lng_manager.h"

// === Константы ===
#define APP_CLASS_NAME L"KeyboardLayoutSwitcher"
#define APP_GUID L"1kblswitch"
#define MENU_EXIT 1001
#define MENU_ABOUT 1002
#define MENU_SETTINGS 1010
#define OSD_TIMER_ID 1003
#define OSD_FADE_TIMER_ID 1004
#define MENU_ALWAYS_SHOW_OSD 1005
#define MENU_TOGGLE_CARET_IND 1006
#define OSD_SHOW_TIMER_ID 1006
#define LAYOUT_CHECK_TIMER_ID 1007
#define CARET_CHECK_TIMER_ID 1008
#define OSD_CLASS_NAME L"OSDWindowClass"
#define LAYOUT_IND_CLASS_NAME L"LayoutIndicatorClass"
#define LAYOUT_IND_SIZE 32
#define INDICATOR_HIDE_AFTER_CHARS 2
#define REG_KEY_PATH L"Software\\KBLSwitch"
#define WORD_BUFFER_SIZE 128
#define TRAILING_BUFFER_SIZE 16
#define ENGLISH_LAYOUT_ID L"00000409"
#define RUSSIAN_LAYOUT_ID L"00000419"

// Сообщение иконки трея
constexpr UINT WM_TRAYICON = WM_USER + 100;

// === Пункт контекстного меню (owner-draw) ===
struct MenuItemData {
    UINT id;           // Идентификатор команды
    WCHAR glyph;       // Код глифа из шрифта Segoe Fluent Icons
    std::wstring text; // Локализованный текст пункта
};

// === Общие глобальные переменные ===
extern HHOOK    g_khook;            // Низкоуровневый хук клавиатуры
extern UINT     g_key;              // Клавиша переключения раскладки
extern BOOL     g_modCtrl, g_modShift, g_modAlt;
extern BOOL     g_alwaysShowOsd;    // Постоянно показывать OSD
extern HWND     g_hWnd;             // Главное (скрытое) окно
extern HWND     g_hOsdWnd;          // Окно OSD
extern HMENU    g_hCtxMenu;         // Контекстное меню (трей и OSD)
extern HICON    g_hIcon;
extern BYTE     g_osdConfigAlpha;   // Прозрачность OSD из INI
extern COLORREF g_osdColor;         // Цвет фона OSD
extern HWND     g_hLayoutIndWnd;    // Окно-индикатор у курсора
extern BOOL     g_showCaretIndicator;
extern int      g_indicatorTypedChars;
extern DWORD    g_indicatorTimeoutMs;
extern DWORD    g_indicatorShowTick;
extern UINT     g_fixRuToEnKey;     // Исправить слово: RU -> EN
extern UINT     g_fixEnToRuKey;     // Исправить слово: EN -> RU

// === Прототипы функций ===
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK OSDWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK LayoutIndicatorWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK KbdHook(int nCode, WPARAM wParam, LPARAM lParam);

void AddTrayIcon(HWND hWnd);
void RemoveTrayIcon();
void ShowTrayMenu(HWND hWnd);
void ShowAppContextMenu(POINT pt);
void BuildContextMenu();
void UpdateTrayTip();
void DrawMenuItem(HWND hwnd, LPDRAWITEMSTRUCT di);
HFONT CreateMenuFont();

void GetLayoutName(TCHAR* buffer, int bufferSize);
void ShowOsdWindow(HINSTANCE hInstance);
void UpdateLayoutIndicator();
void HideLayoutIndicator();

BOOL InitApplication(HINSTANCE hInstance);
void LoadSettingsFromIni();
void RunMessageLoop();
void xMain();

#endif // KBLSWITCH_H
