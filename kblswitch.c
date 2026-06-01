#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <tchar.h>
#include <shellapi.h>
#include <dwmapi.h>
#include "resource.h"

// === Глобальные переменные и константы ===
#define APP_CLASS_NAME L"KeyboardLayoutSwitcher"
#define APP_GUID L"1kblswitch"
#define MENU_EXIT 1001
#define MENU_ABOUT 1002
#define OSD_TIMER_ID 1003
#define OSD_FADE_TIMER_ID 1004
#define MENU_ALWAYS_SHOW_OSD 1005
#define OSD_SHOW_TIMER_ID 1006
#define LAYOUT_CHECK_TIMER_ID 1007
#define OSD_CLASS_NAME L"OSDWindowClass"
#define REG_KEY_PATH   L"Software\\KBLSwitch"
#define WORD_BUFFER_SIZE 128
#define TRAILING_BUFFER_SIZE 16

const UINT WM_TRAYICON = WM_USER + 100;

// === Глобальные переменные ===
TCHAR	g_prog_dir[MAX_PATH];
DWORD	g_prog_dir_len;
HHOOK	g_khook = NULL;
HANDLE  g_hEvent = NULL;
UINT	g_key = VK_RMENU;
BOOL    g_modCtrl = FALSE;
BOOL    g_modShift = FALSE;
BOOL    g_modAlt = FALSE;
BOOL    g_alwaysShowOsd = FALSE;
HWND    g_hWnd = NULL; // Главное невидимое окно
HWND    g_hOsdWnd = NULL; // Окно OSD
HMENU   g_hMenu = NULL;
HICON   g_hIcon = NULL;
UINT    WM_TASKBARCREATED = 0;
UINT_PTR g_hExitCheckTimer = 0; // ID таймера проверки завершения
BYTE    g_osdAlpha = 255; // Текущая прозрачность OSD
BYTE    g_osdConfigAlpha = 220; // Прозрачность из INI
TCHAR   g_osdText[64] = {0}; // Текст для OSD
COLORREF g_osdColor = RGB(127, 127, 127); // Цвет фона OSD
UINT    g_fixRuToEnKey = VK_F8; // Исправить последнее слово: русская раскладка -> английская
UINT    g_fixEnToRuKey = VK_F9; // Исправить последнее слово: английская раскладка -> русская
WCHAR   g_currentWord[WORD_BUFFER_SIZE] = {0};
int     g_currentWordLen = 0;
WCHAR   g_lastWord[WORD_BUFFER_SIZE] = {0};
int     g_lastWordLen = 0;
WCHAR   g_trailingText[TRAILING_BUFFER_SIZE] = {0};
int     g_trailingTextLen = 0;

// === Прототипы функций ===
void CleanupResources();
void ShowFatalError(const TCHAR* message, BOOL showSystemError);
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK OSDWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK KbdHook(int nCode, WPARAM wParam, LPARAM lParam);
void AddTrayIcon(HWND hWnd);
void RemoveTrayIcon();
void ShowTrayMenu(HWND hWnd);
void GetLayoutName(TCHAR* buffer, int bufferSize);
void ShowOsdWindow(HINSTANCE hInstance);
BOOL InitApplication(HINSTANCE hInstance);
void LoadSettingsFromIni();
BOOL CorrectLastWord(BOOL ruToEn);
void TrackTypedKey(KBDLLHOOKSTRUCT* ks);
void ResetWordBuffers();
void SaveOsdPosition(int x, int y);
BOOL LoadOsdPosition(int* x, int* y);
void RunMessageLoop();
void xMain();

// === Функции ===

// Вывод критической ошибки и завершение работы
void ShowFatalError(const TCHAR* message, BOOL showSystemError) {
    TCHAR* systemMessage = NULL;
    TCHAR finalMessage[1024];

    if (showSystemError) {
        DWORD errorCode = GetLastError();
        FormatMessage(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            NULL, errorCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            (LPTSTR)&systemMessage, 0, NULL);
    }

    if (systemMessage) {
        _sntprintf_s(finalMessage, _countof(finalMessage), _TRUNCATE, L"%s\n\nSystem Error: %s", message, systemMessage);
        LocalFree(systemMessage);
    } else {
        _tcsncpy_s(finalMessage, _countof(finalMessage), message, _TRUNCATE);
    }

    MessageBox(NULL, finalMessage, L"kblswitch Error", MB_OK | MB_ICONERROR);
    CleanupResources();
    ExitProcess(1);
}

// === Исправление последнего слова RU<->EN ===
static const WCHAR EN_LOWER_MAP[] = L"`qwertyuiop[]asdfghjkl;'zxcvbnm,./";
static const WCHAR EN_UPPER_MAP[] = L"~QWERTYUIOP{}ASDFGHJKL:\"ZXCVBNM<>?";
static const WCHAR RU_LOWER_MAP[] = L"ёйцукенгшщзхъфывапролджэячсмитьбю.";
static const WCHAR RU_UPPER_MAP[] = L"ЁЙЦУКЕНГШЩЗХЪФЫВАПРОЛДЖЭЯЧСМИТЬБЮ,";

BOOL FindMappedChar(WCHAR ch, const WCHAR* from, const WCHAR* to, WCHAR* mapped) {
    for (int i = 0; from[i] != L'\0' && to[i] != L'\0'; ++i) {
        if (from[i] == ch) {
            *mapped = to[i];
            return TRUE;
        }
    }
    return FALSE;
}

BOOL MapCharRuToEn(WCHAR ch, WCHAR* mapped) {
    return FindMappedChar(ch, RU_LOWER_MAP, EN_LOWER_MAP, mapped) ||
           FindMappedChar(ch, RU_UPPER_MAP, EN_UPPER_MAP, mapped);
}

BOOL MapCharEnToRu(WCHAR ch, WCHAR* mapped) {
    return FindMappedChar(ch, EN_LOWER_MAP, RU_LOWER_MAP, mapped) ||
           FindMappedChar(ch, EN_UPPER_MAP, RU_UPPER_MAP, mapped);
}

BOOL IsConvertibleChar(WCHAR ch) {
    WCHAR unused;
    return MapCharRuToEn(ch, &unused) || MapCharEnToRu(ch, &unused);
}

BOOL ConvertWordLayout(const WCHAR* source, int sourceLen, WCHAR* target, int targetSize, BOOL ruToEn) {
    if (!source || !target || sourceLen <= 0 || targetSize <= sourceLen) return FALSE;

    for (int i = 0; i < sourceLen; ++i) {
        WCHAR mapped = 0;
        BOOL ok = ruToEn ? MapCharRuToEn(source[i], &mapped) : MapCharEnToRu(source[i], &mapped);
        if (!ok) return FALSE;
        target[i] = mapped;
    }
    target[sourceLen] = L'\0';
    return TRUE;
}

void ResetWordBuffers() {
    g_currentWordLen = 0;
    g_currentWord[0] = L'\0';
    g_lastWordLen = 0;
    g_lastWord[0] = L'\0';
    g_trailingTextLen = 0;
    g_trailingText[0] = L'\0';
}

void AppendCurrentWordChar(WCHAR ch) {
    if (g_currentWordLen == 0) {
        g_trailingTextLen = 0;
        g_trailingText[0] = L'\0';
    }

    if (g_currentWordLen < WORD_BUFFER_SIZE - 1) {
        g_currentWord[g_currentWordLen++] = ch;
        g_currentWord[g_currentWordLen] = L'\0';
    } else {
        ResetWordBuffers();
    }
}

void TrackSeparatorChar(WCHAR ch) {
    if (g_currentWordLen > 0) {
        wcsncpy_s(g_lastWord, _countof(g_lastWord), g_currentWord, _TRUNCATE);
        g_lastWordLen = g_currentWordLen;
        g_currentWordLen = 0;
        g_currentWord[0] = L'\0';
        g_trailingTextLen = 0;
        g_trailingText[0] = L'\0';
    }

    if (g_lastWordLen > 0 && g_trailingTextLen < TRAILING_BUFFER_SIZE - 1) {
        g_trailingText[g_trailingTextLen++] = ch;
        g_trailingText[g_trailingTextLen] = L'\0';
    } else if (g_lastWordLen == 0) {
        ResetWordBuffers();
    }
}

void TrackBackspace() {
    if (g_currentWordLen > 0) {
        g_currentWord[--g_currentWordLen] = L'\0';
        return;
    }

    if (g_trailingTextLen > 0) {
        g_trailingText[--g_trailingTextLen] = L'\0';
        if (g_trailingTextLen == 0 && g_lastWordLen > 0) {
            wcsncpy_s(g_currentWord, _countof(g_currentWord), g_lastWord, _TRUNCATE);
            g_currentWordLen = g_lastWordLen;
            g_lastWordLen = 0;
            g_lastWord[0] = L'\0';
        }
        return;
    }

    ResetWordBuffers();
}

BOOL HasEditingModifier() {
    return (GetAsyncKeyState(VK_CONTROL) & 0x8000) ||
           (GetAsyncKeyState(VK_MENU) & 0x8000) ||
           (GetAsyncKeyState(VK_LWIN) & 0x8000) ||
           (GetAsyncKeyState(VK_RWIN) & 0x8000);
}

HKL GetForegroundKeyboardLayout() {
    HWND fgWnd = GetForegroundWindow();
    if (!fgWnd) return GetKeyboardLayout(0);
    DWORD threadId = GetWindowThreadProcessId(fgWnd, NULL);
    return GetKeyboardLayout(threadId);
}

BOOL TryGetTypedChar(KBDLLHOOKSTRUCT* ks, WCHAR* ch) {
    BYTE keyboardState[256];
    WCHAR buffer[8] = {0};

    if (!ks || !ch) return FALSE;
    if (!GetKeyboardState(keyboardState)) return FALSE;

    keyboardState[VK_SHIFT] = (GetAsyncKeyState(VK_SHIFT) & 0x8000) ? 0x80 : 0;
    keyboardState[VK_LSHIFT] = (GetAsyncKeyState(VK_LSHIFT) & 0x8000) ? 0x80 : 0;
    keyboardState[VK_RSHIFT] = (GetAsyncKeyState(VK_RSHIFT) & 0x8000) ? 0x80 : 0;
    keyboardState[VK_CAPITAL] = (GetKeyState(VK_CAPITAL) & 0x0001) ? 0x01 : 0;

#ifndef TO_UNICODE_NO_STATE_CHANGE
#define TO_UNICODE_NO_STATE_CHANGE 0x04
#endif

    int chars = ToUnicodeEx(ks->vkCode, ks->scanCode, keyboardState, buffer,
                            _countof(buffer), TO_UNICODE_NO_STATE_CHANGE,
                            GetForegroundKeyboardLayout());
    if (chars == 1) {
        *ch = buffer[0];
        return TRUE;
    }

    return FALSE;
}

void TrackTypedKey(KBDLLHOOKSTRUCT* ks) {
    WCHAR ch = 0;

    if (!ks) return;

    if (ks->vkCode == VK_BACK) {
        TrackBackspace();
        return;
    }

    if (HasEditingModifier()) {
        ResetWordBuffers();
        return;
    }

    switch (ks->vkCode) {
        case VK_LEFT:
        case VK_RIGHT:
        case VK_UP:
        case VK_DOWN:
        case VK_HOME:
        case VK_END:
        case VK_PRIOR:
        case VK_NEXT:
        case VK_DELETE:
        case VK_TAB:
        case VK_ESCAPE:
            ResetWordBuffers();
            return;
        default:
            break;
    }

    if (!TryGetTypedChar(ks, &ch)) return;

    if (IsConvertibleChar(ch)) {
        AppendCurrentWordChar(ch);
    } else {
        TrackSeparatorChar(ch);
    }
}

BOOL SendBackspaces(int count) {
    INPUT input[2];
    ZeroMemory(input, sizeof(input));

    input[0].type = INPUT_KEYBOARD;
    input[0].ki.wVk = VK_BACK;
    input[1].type = INPUT_KEYBOARD;
    input[1].ki.wVk = VK_BACK;
    input[1].ki.dwFlags = KEYEVENTF_KEYUP;

    for (int i = 0; i < count; ++i) {
        if (SendInput(2, input, sizeof(INPUT)) != 2) return FALSE;
    }
    return TRUE;
}

BOOL SendUnicodeText(const WCHAR* text, int len) {
    INPUT input[2];

    for (int i = 0; i < len; ++i) {
        ZeroMemory(input, sizeof(input));
        input[0].type = INPUT_KEYBOARD;
        input[0].ki.dwFlags = KEYEVENTF_UNICODE;
        input[0].ki.wScan = text[i];
        input[1].type = INPUT_KEYBOARD;
        input[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
        input[1].ki.wScan = text[i];

        if (SendInput(2, input, sizeof(INPUT)) != 2) return FALSE;
    }
    return TRUE;
}

BOOL CorrectLastWord(BOOL ruToEn) {
    WCHAR converted[WORD_BUFFER_SIZE] = {0};
    WCHAR trailingCopy[TRAILING_BUFFER_SIZE] = {0};
    const WCHAR* source = NULL;
    int sourceLen = 0;
    int trailingLen = 0;
    BOOL usingCurrentWord = FALSE;

    if (g_currentWordLen > 0) {
        source = g_currentWord;
        sourceLen = g_currentWordLen;
        usingCurrentWord = TRUE;
    } else if (g_lastWordLen > 0) {
        source = g_lastWord;
        sourceLen = g_lastWordLen;
        trailingLen = g_trailingTextLen;
        wcsncpy_s(trailingCopy, _countof(trailingCopy), g_trailingText, _TRUNCATE);
    } else {
        return FALSE;
    }

    if (!ConvertWordLayout(source, sourceLen, converted, _countof(converted), ruToEn)) {
        return FALSE;
    }

    if (!SendBackspaces(sourceLen + trailingLen)) return FALSE;
    if (!SendUnicodeText(converted, sourceLen)) return FALSE;
    if (trailingLen > 0 && !SendUnicodeText(trailingCopy, trailingLen)) return FALSE;

    if (usingCurrentWord) {
        wcsncpy_s(g_currentWord, _countof(g_currentWord), converted, _TRUNCATE);
        g_currentWordLen = sourceLen;
        g_lastWordLen = 0;
        g_lastWord[0] = L'\0';
        g_trailingTextLen = 0;
        g_trailingText[0] = L'\0';
    } else {
        wcsncpy_s(g_lastWord, _countof(g_lastWord), converted, _TRUNCATE);
        g_lastWordLen = sourceLen;
        wcsncpy_s(g_trailingText, _countof(g_trailingText), trailingCopy, _TRUNCATE);
        g_trailingTextLen = trailingLen;
        g_currentWordLen = 0;
        g_currentWord[0] = L'\0';
    }

    return TRUE;
}

// Низкоуровневый хук клавиатуры
LRESULT CALLBACK KbdHook(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        KBDLLHOOKSTRUCT* ks = (KBDLLHOOKSTRUCT*)lParam;
        if (!(ks->flags & LLKHF_INJECTED)) {
            // автоповтор
            static BOOL bKeyProcessed = FALSE;
            static BOOL bFixRuToEnProcessed = FALSE;
            static BOOL bFixEnToRuProcessed = FALSE;

            if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP) {
                if (ks->vkCode == g_key) bKeyProcessed = FALSE;
                if (ks->vkCode == g_fixRuToEnKey) bFixRuToEnProcessed = FALSE;
                if (ks->vkCode == g_fixEnToRuKey) bFixEnToRuProcessed = FALSE;
                return CallNextHookEx(g_khook, nCode, wParam, lParam);
            }

            if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
                if (g_fixRuToEnKey != 0 && ks->vkCode == g_fixRuToEnKey && !bFixRuToEnProcessed) {
                    bFixRuToEnProcessed = TRUE;
                    CorrectLastWord(TRUE);
                    return 1;
                }

                if (g_fixEnToRuKey != 0 && ks->vkCode == g_fixEnToRuKey && !bFixEnToRuProcessed) {
                    bFixEnToRuProcessed = TRUE;
                    CorrectLastWord(FALSE);
                    return 1;
                }

                if (ks->vkCode == g_key && !bKeyProcessed) {
                    bKeyProcessed = TRUE;
                    ResetWordBuffers();
                    // Нажимаем модификаторы в соответствии с настройками
                    if (g_modCtrl)  keybd_event(VK_CONTROL, 0, 0, 0);
                    if (g_modShift) keybd_event(VK_SHIFT,   0, 0, 0);
                    if (g_modAlt)   keybd_event(VK_MENU,    0, 0, 0);

                    // Отпускаем в обратном порядке
                    if (g_modAlt)   keybd_event(VK_MENU,    0, KEYEVENTF_KEYUP, 0);
                    if (g_modShift) keybd_event(VK_SHIFT,   0, KEYEVENTF_KEYUP, 0);
                    if (g_modCtrl)  keybd_event(VK_CONTROL, 0, KEYEVENTF_KEYUP, 0);

                    SetTimer(g_hWnd, OSD_SHOW_TIMER_ID, 100, NULL);

                    return 1;
                }

                TrackTypedKey(ks);
            }
        }
    }
    return CallNextHookEx(g_khook, nCode, wParam, lParam);
}

// Функция обработки сообщений окна
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    static TCHAR previousLayout[64] = {0};

    if (message == WM_TRAYICON) {
        switch (lParam) {
        case WM_RBUTTONUP:
        case WM_CONTEXTMENU:
            ShowTrayMenu(hWnd);
            break;
        case WM_LBUTTONDBLCLK:
            //DestroyWindow(hWnd);
            break;
        }
        return 0;
    }

    if (message == WM_TASKBARCREATED) {
        AddTrayIcon(hWnd);
        return 0;
    }

    switch (message) {
    case WM_TIMER:
        if (wParam == OSD_SHOW_TIMER_ID) {
            KillTimer(hWnd, OSD_SHOW_TIMER_ID);
            ShowOsdWindow(GetModuleHandle(NULL));
        } else if (wParam == LAYOUT_CHECK_TIMER_ID) {
            TCHAR currentLayout[64] = {0};
            GetLayoutName(currentLayout, _countof(currentLayout));

            if (previousLayout[0] != 0 && _tcscmp(previousLayout, currentLayout) != 0) {
                ShowOsdWindow(GetModuleHandle(NULL));
            }
             _tcscpy_s(previousLayout, _countof(previousLayout), currentLayout);
        }
        break;

    case WM_CREATE:
        WM_TASKBARCREATED = RegisterWindowMessage(_T("TaskbarCreated"));
        g_hIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_APPICON));
        g_hMenu = CreatePopupMenu();
        if (g_hMenu) {
            AppendMenu(g_hMenu, MF_STRING, MENU_ABOUT, L"О программе");
            AppendMenu(g_hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenu(g_hMenu, MF_STRING | (g_alwaysShowOsd ? MF_CHECKED : MF_UNCHECKED), MENU_ALWAYS_SHOW_OSD, L"Постоянное OSD");
            AppendMenu(g_hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenu(g_hMenu, MF_STRING, MENU_EXIT, L"Выход");
        }
        AddTrayIcon(hWnd);
        GetLayoutName(previousLayout, _countof(previousLayout));
        SetTimer(hWnd, LAYOUT_CHECK_TIMER_ID, 500, NULL);
        break;

    case WM_COMMAND:
        if (LOWORD(wParam) == MENU_ABOUT) {
            MessageBox(hWnd,
                L"KBLSWITCH - это программа, предназначенная для переключения раскладки клавиатуры ⌨️ одной клавишей с визуальным уведомлением (OSD).\n\n"
                L"Программа перехватывает нажатие удобной для вас клавиши (например, \"Правый Alt\") и эмулирует стандартную системную комбинацию (например, Ctrl+Shift), выводя при этом текущий язык ввода по центру экрана.\n\n"
                L"Подробнее: см. readme.md",
                L"About kblswitch", MB_OK | MB_ICONINFORMATION);
        } else if (LOWORD(wParam) == MENU_EXIT) {
            DestroyWindow(hWnd);
        } else if (LOWORD(wParam) == MENU_ALWAYS_SHOW_OSD) {
            g_alwaysShowOsd = !g_alwaysShowOsd;
            CheckMenuItem(g_hMenu, MENU_ALWAYS_SHOW_OSD, g_alwaysShowOsd ? MF_CHECKED : MF_UNCHECKED);

            TCHAR iniPath[MAX_PATH];
            GetModuleFileName(NULL, iniPath, MAX_PATH);
            TCHAR* slash = _tcsrchr(iniPath, L'\\');
            if (slash) *(slash + 1) = L'\0';
            _tcscat_s(iniPath, _countof(iniPath), L"kblswitch.ini");
            WritePrivateProfileString(L"Settings", L"always_show_osd", g_alwaysShowOsd ? L"1" : L"0", iniPath);

            if (g_alwaysShowOsd) {
                ShowOsdWindow(GetModuleHandle(NULL));
            } else {
                if (g_hOsdWnd) {
                    ShowWindow(g_hOsdWnd, SW_HIDE);
                }
            }
        }
        break;

    case WM_CLOSE:
        DestroyWindow(hWnd);
        break;

    case WM_DESTROY:
        RemoveTrayIcon();
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

// Добавление иконки в системный трей
void AddTrayIcon(HWND hWnd) {
    NOTIFYICONDATA nid = { .cbSize = sizeof(nid) };
    nid.hWnd = hWnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = g_hIcon;
    _tcscpy_s(nid.szTip, _countof(nid.szTip), L"Keyboard Layout Switcher");
    Shell_NotifyIcon(NIM_ADD, &nid);
}

// Обработчик сообщений для OSD окна
LRESULT CALLBACK OSDWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    // Константы для улучшения читаемости
    static const int OSD_FONT_HEIGHT = -48;           // Высота шрифта
    static const int OSD_FADE_STEP = 15;              // Шаг уменьшения прозрачности при затухании
    static const int OSD_FADE_INTERVAL = 25;          // Интервал таймера затухания (мс)

    // Статические переменные для кэширования DWM API
    static HRESULT (WINAPI *pDwmSetWindowAttribute)(HWND, DWORD, LPCVOID, DWORD) = NULL;
    static HMODULE hDwmApi = NULL;
    static BOOL dwmInitialized = FALSE;

    // Состояние перетаскивания
    static BOOL  s_dragging    = FALSE;
    static int   s_dragOffsetX = 0;
    static int   s_dragOffsetY = 0;

    switch (message) {
        case WM_CREATE: {
            // Загружаем DWM API для скругления углов (Windows 11+) при первом вызове
            if (!dwmInitialized) {
                hDwmApi = LoadLibrary(L"dwmapi.dll");
                if (hDwmApi) {
                    pDwmSetWindowAttribute = (HRESULT (WINAPI *)(HWND, DWORD, LPCVOID, DWORD))
                        GetProcAddress(hDwmApi, "DwmSetWindowAttribute");
                }
                dwmInitialized = TRUE;
            }

            if (pDwmSetWindowAttribute) {
                // DWMWA_WINDOW_CORNER_PREFERENCE = 33 (dwmapi.h)
                const DWORD DWMWA_WINDOW_CORNER_PREFERENCE = 33;
                // DWM_WINDOW_CORNER_PREFERENCE_ROUND = 2
                const DWORD DWM_WINDOW_CORNER_PREFERENCE_ROUND = 2;
                DWORD cornerPreference = DWM_WINDOW_CORNER_PREFERENCE_ROUND;
                pDwmSetWindowAttribute(hWnd, DWMWA_WINDOW_CORNER_PREFERENCE,
                                       &cornerPreference, sizeof(cornerPreference));
            }


            break;
        }

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);

            RECT rect;
            GetClientRect(hWnd, &rect);

            HBRUSH hBrush =  CreateSolidBrush(g_osdColor);
            HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, hBrush);
            // Заливаем прямоугольник фоном
            FillRect(hdc, &rect, hBrush);

            SelectObject(hdc, hOldBrush);
            DeleteObject(hBrush);

            // Рисуем текст
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(255, 255, 255));

            HFONT hFont = CreateFont(OSD_FONT_HEIGHT, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Arial");
            HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);

            DrawText(hdc, g_osdText, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            SelectObject(hdc, hOldFont);
            DeleteObject(hFont);

            EndPaint(hWnd, &ps);
            break;
        }

        case WM_TIMER: {
            if (wParam == OSD_TIMER_ID) {
                KillTimer(hWnd, OSD_TIMER_ID);
                SetTimer(hWnd, OSD_FADE_TIMER_ID, OSD_FADE_INTERVAL, NULL);
            } else if (wParam == OSD_FADE_TIMER_ID) {

                if (g_alwaysShowOsd) {

                    KillTimer(hWnd, OSD_FADE_TIMER_ID);
                    return 0;
                }

                BYTE currentAlpha = g_osdAlpha;
                if (currentAlpha > OSD_FADE_STEP) {
                    BYTE newAlpha = currentAlpha - OSD_FADE_STEP;
                    g_osdAlpha = newAlpha;
                    SetLayeredWindowAttributes(hWnd, 0, newAlpha, LWA_ALPHA);
                } else {
                    KillTimer(hWnd, OSD_FADE_TIMER_ID);
                    ShowWindow(hWnd, SW_HIDE);
                }
            }
            break;
        }

        case WM_LBUTTONDOWN:
            if (g_alwaysShowOsd) {
                // Начинаем ручное перетаскивание
                POINT pt;
                GetCursorPos(&pt);
                RECT rc;
                GetWindowRect(hWnd, &rc);
                s_dragOffsetX = pt.x - rc.left;
                s_dragOffsetY = pt.y - rc.top;
                s_dragging    = TRUE;
                SetCapture(hWnd);
                SetCursor(LoadCursor(NULL, IDC_SIZEALL));
            }
            break;

        case WM_MOUSEMOVE:
            if (s_dragging) {
                POINT pt;
                GetCursorPos(&pt);
                int newX = pt.x - s_dragOffsetX;
                int newY = pt.y - s_dragOffsetY;

                // Зажимаем позицию в пределах виртуального рабочего стола (все мониторы)
                int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
                int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
                int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
                int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
                RECT rc;
                GetWindowRect(hWnd, &rc);
                int ww = rc.right  - rc.left;
                int wh = rc.bottom - rc.top;
                if (newX < vx)          newX = vx;
                if (newY < vy)          newY = vy;
                if (newX + ww > vx + vw) newX = vx + vw - ww;
                if (newY + wh > vy + vh) newY = vy + vh - wh;

                SetWindowPos(hWnd, HWND_TOPMOST, newX, newY, 0, 0,
                             SWP_NOSIZE | SWP_NOACTIVATE);
                SetCursor(LoadCursor(NULL, IDC_SIZEALL));
            } else {
                SetCursor(LoadCursor(NULL, g_alwaysShowOsd ? IDC_SIZEALL : IDC_ARROW));
            }
            break;

        case WM_LBUTTONUP:
            if (s_dragging) {
                s_dragging = FALSE;
                ReleaseCapture();
                // Сохраняем позицию в реестре
                RECT rc;
                GetWindowRect(hWnd, &rc);
                SaveOsdPosition(rc.left, rc.top);
                // Гарантируем Z-позицию поверх всех окон
                SetWindowPos(hWnd, HWND_TOPMOST, 0, 0, 0, 0,
                             SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            }
            break;

        case WM_SETCURSOR:
            // В режиме постоянного OSD - курсор перемещения, иначе - стрелка
            if (LOWORD(lParam) == HTCLIENT) {
                SetCursor(LoadCursor(NULL, g_alwaysShowOsd ? IDC_SIZEALL : IDC_ARROW));
                return TRUE;
            }
            break;

        case WM_DESTROY: {

            break;
        }

        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

// Удаление иконки из системного трея
void RemoveTrayIcon() {
    NOTIFYICONDATA nid = { .cbSize = sizeof(nid) };
    nid.hWnd = g_hWnd;
    nid.uID = 1;
    Shell_NotifyIcon(NIM_DELETE, &nid);
}

// Показать контекстное меню
void ShowTrayMenu(HWND hWnd) {
    if (!g_hMenu) return;
    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(hWnd);
    TrackPopupMenu(g_hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hWnd, NULL);
    PostMessage(hWnd, WM_NULL, 0, 0);
}

// Очистка всех выделенных ресурсов
void CleanupResources() {
    if (g_hExitCheckTimer) { KillTimer(NULL, g_hExitCheckTimer); g_hExitCheckTimer = 0; }
    if (g_khook) UnhookWindowsHookEx(g_khook);
    if (g_hIcon) DestroyIcon(g_hIcon);
    if (g_hMenu) DestroyMenu(g_hMenu);
    if (g_hEvent) CloseHandle(g_hEvent);
    if (g_hOsdWnd) DestroyWindow(g_hOsdWnd);
    g_khook = NULL;
    g_hIcon = NULL;
    g_hMenu = NULL;
    g_hEvent = NULL;
    g_hOsdWnd = NULL;
}

// Сохранение позиции OSD-окна в реестре
void SaveOsdPosition(int x, int y) {
    HKEY hKey;
    if (RegCreateKeyEx(HKEY_CURRENT_USER, REG_KEY_PATH,
                       0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        DWORD vx = (DWORD)x, vy = (DWORD)y;
        RegSetValueEx(hKey, L"OsdX", 0, REG_DWORD, (const BYTE*)&vx, sizeof(vx));
        RegSetValueEx(hKey, L"OsdY", 0, REG_DWORD, (const BYTE*)&vy, sizeof(vy));
        RegCloseKey(hKey);
    }
}

// Восстановление позиции OSD-окна из реестра
// Возвращает TRUE и заполняет *x/*y, если значения найдены
BOOL LoadOsdPosition(int* x, int* y) {
    HKEY hKey;
    if (RegOpenKeyEx(HKEY_CURRENT_USER, REG_KEY_PATH,
                     0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return FALSE;

    DWORD type, size = sizeof(DWORD), vx = 0, vy = 0;
    BOOL ok = (RegQueryValueEx(hKey, L"OsdX", NULL, &type, (BYTE*)&vx, &size) == ERROR_SUCCESS && type == REG_DWORD &&
               RegQueryValueEx(hKey, L"OsdY", NULL, &type, (BYTE*)&vy, &size) == ERROR_SUCCESS && type == REG_DWORD);
    RegCloseKey(hKey);

    if (ok) { *x = (int)vx; *y = (int)vy; }
    return ok;
}

// Загрузка настроек из INI-файла
void LoadSettingsFromIni() {
    TCHAR iniPath[MAX_PATH];
    GetModuleFileName(NULL, iniPath, MAX_PATH);
    TCHAR* slash = _tcsrchr(iniPath, L'\\');
    if (slash) *(slash + 1) = L'\0';
    _tcscat_s(iniPath, _countof(iniPath), L"kblswitch.ini");

    // Чтение клавиши переключения раскладки
    g_key = GetPrivateProfileInt(L"Settings", L"Key", VK_RMENU, iniPath);

    // Горячие клавиши исправления последнего слова. Значение 0 отключает функцию.
    g_fixRuToEnKey = GetPrivateProfileInt(L"Settings", L"FixRuToEnKey", VK_F8, iniPath);
    g_fixEnToRuKey = GetPrivateProfileInt(L"Settings", L"FixEnToRuKey", VK_F9, iniPath);

    // Чтение модификаторов
    TCHAR modifiers[64];
    GetPrivateProfileString(L"Settings", L"Modifiers", L"Ctrl+Shift", modifiers, _countof(modifiers), iniPath);
    g_modCtrl = (_tcsstr(modifiers, L"Ctrl") != NULL);
    g_modShift = (_tcsstr(modifiers, L"Shift") != NULL);
    g_modAlt = (_tcsstr(modifiers, L"Alt") != NULL);

    g_alwaysShowOsd = GetPrivateProfileInt(L"Settings", L"always_show_osd", 0, iniPath);
    g_osdConfigAlpha = (BYTE)GetPrivateProfileInt(L"Settings", L"osd_alpha", 220, iniPath);

    // Чтение цвета
    TCHAR colorStr[16];
    if (GetPrivateProfileString(L"Settings", L"Color", L"7E7E7E", colorStr, _countof(colorStr), iniPath) > 0) {
        if (_tcslen(colorStr) == 6) {
            long colorVal = _tcstol(colorStr, NULL, 16);
            int r = (colorVal >> 16) & 0xFF;
            int g = (colorVal >> 8) & 0xFF;
            int b = colorVal & 0xFF;
            g_osdColor = RGB(r, g, b);
        }
    }
}

// Получение имени текущей раскладки
void GetLayoutName(TCHAR* buffer, int bufferSize) {
    HWND fgWnd = GetForegroundWindow();
    if (!fgWnd) {
        // Нет активного окна - раскладка неизвестна
        _tcsncpy_s(buffer, bufferSize, L"??", _TRUNCATE);
        return;
    }
    DWORD threadId = GetWindowThreadProcessId(fgWnd, NULL);
    HKL hkl = GetKeyboardLayout(threadId);

    LANGID langID = LOWORD(hkl);

    LCID lcid = MAKELCID(langID, SORT_DEFAULT);

    TCHAR langCode[3] = {0};

    if (GetLocaleInfo(lcid, LOCALE_SISO639LANGNAME, langCode, 3) > 0) {
        CharUpper(langCode);
        _tcsncpy_s(buffer, bufferSize, langCode, _TRUNCATE);
        return;
    }

    if (GetLocaleInfo(lcid, LOCALE_SABBREVLANGNAME, langCode, 3) > 0) {
        CharUpper(langCode);
        _tcsncpy_s(buffer, bufferSize, langCode, _TRUNCATE);
        return;
    }

}


// Показать OSD-окно
void ShowOsdWindow(HINSTANCE hInstance) {
    int w = g_alwaysShowOsd ? 80 : 150;
    int h = g_alwaysShowOsd ? 60 : 100;

    if (!g_hOsdWnd) {
        g_hOsdWnd = CreateWindowEx(
            WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
            OSD_CLASS_NAME, L"", WS_POPUP,
            0, 0, w, h,
            NULL, NULL, hInstance, NULL);
        if (!g_hOsdWnd) return;
    }

    GetLayoutName(g_osdText, _countof(g_osdText));

    KillTimer(g_hOsdWnd, OSD_TIMER_ID);
    KillTimer(g_hOsdWnd, OSD_FADE_TIMER_ID);

    g_osdAlpha = g_osdConfigAlpha; // используем настройку из INI для обоих режимов
    SetLayeredWindowAttributes(g_hOsdWnd, 0, g_osdAlpha, LWA_ALPHA);

    // Если окно не в режиме "всегда наверху", центрируем его
    if (!g_alwaysShowOsd) {
        HMONITOR hMonitor = MonitorFromWindow(GetForegroundWindow(), MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = { .cbSize = sizeof(mi) };
        int x = 0, y = 0;
        if (GetMonitorInfo(hMonitor, &mi)) {
            x = mi.rcMonitor.left + (mi.rcMonitor.right - mi.rcMonitor.left - w) / 2;
            y = mi.rcMonitor.top + (mi.rcMonitor.bottom - mi.rcMonitor.top - h) / 2;
        }
        // HWND_TOPMOST всегда, даже если GetMonitorInfo не сработал
        SetWindowPos(g_hOsdWnd, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE);
    } else {
        // Режим «всегда на экране»: восстанавливаем сохранённую позицию (если есть)
        // и проверяем, что окно не вышло за пределы виртуального рабочего стола.
        int savedX = 0, savedY = 0;
        BOOL hasSaved = LoadOsdPosition(&savedX, &savedY);

        int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
        int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
        int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);

        if (hasSaved) {
            // Зажимаем в пределах виртуального десктопа
            if (savedX < vx)          savedX = vx;
            if (savedY < vy)          savedY = vy;
            if (savedX + w > vx + vw) savedX = vx + vw - w;
            if (savedY + h > vy + vh) savedY = vy + vh - h;
            SetWindowPos(g_hOsdWnd, HWND_TOPMOST, savedX, savedY, w, h, SWP_NOACTIVATE);
        } else {
            SetWindowPos(g_hOsdWnd, HWND_TOPMOST, 0, 0, w, h, SWP_NOMOVE | SWP_NOACTIVATE);
        }
    }

    ShowWindow(g_hOsdWnd, SW_SHOWNOACTIVATE);
    InvalidateRect(g_hOsdWnd, NULL, TRUE);
    UpdateWindow(g_hOsdWnd);

    if (!g_alwaysShowOsd) {
        SetTimer(g_hOsdWnd, OSD_TIMER_ID, 1000, NULL);
    }
}

// Инициализация приложения и его компонентов
BOOL InitApplication(HINSTANCE hInstance) {
    // Регистрация основного класса окна
    WNDCLASSEX wc = { .cbSize = sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = APP_CLASS_NAME;
    if (!RegisterClassEx(&wc)) {
        ShowFatalError(L"Failed to register window class.", TRUE);
        return FALSE;
    }

    // Регистрация класса OSD окна
    WNDCLASSEX wc_osd = { .cbSize = sizeof(wc_osd) };
    wc_osd.lpfnWndProc = OSDWndProc;
    wc_osd.hInstance = hInstance;
    wc_osd.lpszClassName = OSD_CLASS_NAME;
    wc_osd.hbrBackground = NULL;
    wc_osd.hCursor = LoadCursor(NULL, IDC_ARROW);
    if (!RegisterClassEx(&wc_osd)) {
        ShowFatalError(L"Failed to register OSD window class.", TRUE);
        return FALSE;
    }

    // Создание невидимого окна для обработки сообщений
    g_hWnd = CreateWindowEx(0, APP_CLASS_NAME, L"Keyboard Layout Switcher", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 100, 100, NULL, NULL, hInstance, NULL);
    if (!g_hWnd) {
        ShowFatalError(L"Failed to create window.", TRUE);
        return FALSE;
    }

    // Установка хука и таймера
    g_khook = SetWindowsHookEx(WH_KEYBOARD_LL, KbdHook, hInstance, 0);
    if (!g_khook) {
        ShowFatalError(L"Failed to set keyboard hook.", TRUE);
        return FALSE;
    }

    return TRUE;
}

// Главный цикл обработки сообщений
void RunMessageLoop() {
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}

// Основная логика приложения
void xMain() {
    g_hEvent = CreateEvent(NULL, TRUE, FALSE, APP_GUID);
    if (!g_hEvent) {
        ShowFatalError(L"Failed to create event object.", TRUE);
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        // Первый экземпляр продолжает работать, второй просто завершается
        CloseHandle(g_hEvent);
        g_hEvent = NULL;
        ExitProcess(0);
    }

    HINSTANCE hInstance = GetModuleHandle(NULL);
    LoadSettingsFromIni();

    if (InitApplication(hInstance)) {
        if (g_alwaysShowOsd) {
            ShowOsdWindow(hInstance);
        }
        ShowWindow(g_hWnd, SW_HIDE);
        RunMessageLoop();
    }

    CleanupResources();
    ExitProcess(0);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    (void)hInstance; (void)hPrevInstance; (void)lpCmdLine; (void)nCmdShow;
    xMain();
    return 0;
}