#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <tchar.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <windowsx.h>
#include "resource.h"

// === Константы ===
#define APP_CLASS_NAME      L"KeyboardLayoutSwitcher"
#define APP_GUID            L"1kblswitch"
#define OSD_CLASS_NAME      L"OSDWindowClass"

// Команды меню
#define MENU_EXIT           1001
#define MENU_ABOUT          1002
#define MENU_ALWAYS_SHOW_OSD 1005

// ID таймеров
#define OSD_TIMER_ID          1003  // Таймер до начала затухания OSD
#define OSD_FADE_TIMER_ID     1004  // Таймер шага затухания OSD
#define OSD_SHOW_TIMER_ID     1006  // Задержка перед показом OSD после переключения
#define LAYOUT_CHECK_TIMER_ID 1007  // Опрос смены раскладки (для внешних переключений, напр. Ctrl+Shift)

// Задержка перед показом OSD (мс): даём системе время применить смену раскладки
#define OSD_SWITCH_DELAY_MS 100

const UINT WM_TRAYICON = WM_USER + 100;

// === Глобальные переменные ===
HINSTANCE g_hInstance   = NULL; // Дескриптор модуля, сохраняется один раз при старте
HHOOK     g_khook       = NULL;
HANDLE    g_hEvent      = NULL;
UINT      g_key         = VK_RMENU;
BOOL      g_modCtrl     = FALSE;
BOOL      g_modShift    = FALSE;
BOOL      g_modAlt      = FALSE;
BOOL      g_alwaysShowOsd = FALSE;
HWND      g_hWnd        = NULL; // Главное невидимое окно
HWND      g_hOsdWnd     = NULL; // Окно OSD
HMENU     g_hMenu       = NULL;
HICON     g_hIcon       = NULL;
UINT      WM_TASKBARCREATED = 0;
BYTE      g_osdAlpha    = 255;   // Текущая прозрачность OSD
BYTE      g_osdConfigAlpha = 220; // Прозрачность из INI
TCHAR     g_osdText[64] = {0};   // Текст для OSD
COLORREF  g_osdColor    = RGB(127, 127, 127); // Цвет фона OSD

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
void ShowOsdWindow();
BOOL InitApplication();
void LoadSettingsFromIni();
void RunMessageLoop();
void GetIniPath(TCHAR* buf, int bufSize);
void xMain();

// === Функции ===

// Формирование пути к INI-файлу рядом с exe
void GetIniPath(TCHAR* buf, int bufSize) {
    GetModuleFileName(NULL, buf, bufSize);
    TCHAR* slash = _tcsrchr(buf, L'\\');
    if (slash) *(slash + 1) = L'\0';
    _tcscat_s(buf, bufSize, L"kblswitch.ini");
}

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
        _sntprintf_s(finalMessage, _countof(finalMessage), _TRUNCATE,
                     L"%s\n\nSystem Error: %s", message, systemMessage);
        LocalFree(systemMessage);
    } else {
        _tcsncpy_s(finalMessage, _countof(finalMessage), message, _TRUNCATE);
    }

    MessageBox(NULL, finalMessage, L"kblswitch Error", MB_OK | MB_ICONERROR);
    CleanupResources();
    ExitProcess(1);
}

// Низкоуровневый хук клавиатуры
LRESULT CALLBACK KbdHook(int nCode, WPARAM wParam, LPARAM lParam) {
    // Объявляем на уровне функции — не внутри условного блока
    static BOOL bKeyProcessed = FALSE;

    if (nCode == HC_ACTION) {
        KBDLLHOOKSTRUCT* ks = (KBDLLHOOKSTRUCT*)lParam;
        if (!(ks->flags & LLKHF_INJECTED)) {
            // Сбрасываем флаг при отпускании клавиши (защита от автоповтора)
            if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP) {
                if (ks->vkCode == g_key) bKeyProcessed = FALSE;
                return CallNextHookEx(g_khook, nCode, wParam, lParam);
            }

            if (ks->vkCode == g_key && !bKeyProcessed &&
                (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN)) {
                bKeyProcessed = TRUE;

                // Нажимаем модификаторы согласно настройкам
                if (g_modCtrl)  keybd_event(VK_CONTROL, 0, 0, 0);
                if (g_modShift) keybd_event(VK_SHIFT,   0, 0, 0);
                if (g_modAlt)   keybd_event(VK_MENU,    0, 0, 0);

                // Отпускаем в обратном порядке
                if (g_modAlt)   keybd_event(VK_MENU,    0, KEYEVENTF_KEYUP, 0);
                if (g_modShift) keybd_event(VK_SHIFT,   0, KEYEVENTF_KEYUP, 0);
                if (g_modCtrl)  keybd_event(VK_CONTROL, 0, KEYEVENTF_KEYUP, 0);

                // Показываем OSD с задержкой, чтобы система успела применить раскладку
                SetTimer(g_hWnd, OSD_SHOW_TIMER_ID, OSD_SWITCH_DELAY_MS, NULL);
                return 1;
            }
        }
    }
    return CallNextHookEx(g_khook, nCode, wParam, lParam);
}

// Функция обработки сообщений главного окна
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    // Запоминаем предыдущую раскладку для обнаружения внешних переключений (Ctrl+Shift и др.)
    static TCHAR previousLayout[64] = {0};

    if (message == WM_TRAYICON) {
        switch (lParam) {
        case WM_RBUTTONUP:
        case WM_CONTEXTMENU:
            ShowTrayMenu(hWnd);
            break;
        }
        return 0;
    }

    if (message == WM_TASKBARCREATED) {
        AddTrayIcon(hWnd);
        return 0;
    }

    switch (message) {
    case WM_CREATE:
        WM_TASKBARCREATED = RegisterWindowMessage(_T("TaskbarCreated"));
        g_hIcon = LoadIcon(g_hInstance, MAKEINTRESOURCE(IDI_APPICON));
        g_hMenu = CreatePopupMenu();
        if (g_hMenu) {
            AppendMenu(g_hMenu, MF_STRING, MENU_ABOUT, L"О программе");
            AppendMenu(g_hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenu(g_hMenu, MF_STRING | (g_alwaysShowOsd ? MF_CHECKED : MF_UNCHECKED),
                       MENU_ALWAYS_SHOW_OSD, L"Постоянное OSD");
            AppendMenu(g_hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenu(g_hMenu, MF_STRING, MENU_EXIT, L"Выход");
        }
        AddTrayIcon(hWnd);
        GetLayoutName(previousLayout, _countof(previousLayout));
        SetTimer(hWnd, LAYOUT_CHECK_TIMER_ID, 500, NULL);
        break;

    case WM_TIMER:
        if (wParam == OSD_SHOW_TIMER_ID) {
            KillTimer(hWnd, OSD_SHOW_TIMER_ID);
            ShowOsdWindow();
        } else if (wParam == LAYOUT_CHECK_TIMER_ID) {
            // Обнаруживаем смену раскладки, вызванную внешними средствами (Ctrl+Shift и др.)
            TCHAR currentLayout[64] = {0};
            GetLayoutName(currentLayout, _countof(currentLayout));
            if (previousLayout[0] != 0 && _tcscmp(previousLayout, currentLayout) != 0) {
                ShowOsdWindow();
            }
            _tcscpy_s(previousLayout, _countof(previousLayout), currentLayout);
        }
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
            CheckMenuItem(g_hMenu, MENU_ALWAYS_SHOW_OSD,
                          g_alwaysShowOsd ? MF_CHECKED : MF_UNCHECKED);

            TCHAR iniPath[MAX_PATH];
            GetIniPath(iniPath, _countof(iniPath));
            WritePrivateProfileString(L"Settings", L"always_show_osd",
                                      g_alwaysShowOsd ? L"1" : L"0", iniPath);

            if (g_alwaysShowOsd) {
                ShowOsdWindow();
            } else if (g_hOsdWnd) {
                ShowWindow(g_hOsdWnd, SW_HIDE);
            }
        }
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

// Обработчик сообщений OSD-окна
LRESULT CALLBACK OSDWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    static const int OSD_FONT_HEIGHT  = -48; // Высота шрифта
    static const int OSD_FADE_STEP    = 15;  // Шаг уменьшения прозрачности при затухании
    static const int OSD_FADE_INTERVAL = 25; // Интервал таймера затухания (мс)

    // GDI-объекты кешируются: создаются один раз, удаляются в WM_DESTROY
    static HFONT  hCachedFont  = NULL;
    static HBRUSH hCachedBrush = NULL;

    // DWM API для скругления углов (Windows 11+), загружается один раз
    static HRESULT (WINAPI *pDwmSetWindowAttribute)(HWND, DWORD, LPCVOID, DWORD) = NULL;
    static HMODULE hDwmApi = NULL;
    static BOOL    dwmInitialized = FALSE;

    switch (message) {
        case WM_CREATE: {
            if (!dwmInitialized) {
                hDwmApi = LoadLibrary(L"dwmapi.dll");
                if (hDwmApi) {
                    pDwmSetWindowAttribute = (HRESULT (WINAPI *)(HWND, DWORD, LPCVOID, DWORD))
                        GetProcAddress(hDwmApi, "DwmSetWindowAttribute");
                }
                dwmInitialized = TRUE;
            }

            if (pDwmSetWindowAttribute) {
                // DWMWA_WINDOW_CORNER_PREFERENCE = 33, ROUND = 2
                const DWORD DWMWA_WINDOW_CORNER_PREFERENCE  = 33;
                const DWORD DWM_WINDOW_CORNER_PREFERENCE_ROUND = 2;
                DWORD pref = DWM_WINDOW_CORNER_PREFERENCE_ROUND;
                pDwmSetWindowAttribute(hWnd, DWMWA_WINDOW_CORNER_PREFERENCE,
                                       &pref, sizeof(pref));
            }

            // Создаём GDI-объекты один раз
            hCachedFont  = CreateFont(OSD_FONT_HEIGHT, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                               DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Arial");
            hCachedBrush = CreateSolidBrush(g_osdColor);
            break;
        }

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);

            RECT rect;
            GetClientRect(hWnd, &rect);

            // Используем кешированную кисть
            FillRect(hdc, &rect, hCachedBrush);

            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(255, 255, 255));

            HFONT hOldFont = (HFONT)SelectObject(hdc, hCachedFont);
            DrawText(hdc, g_osdText, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(hdc, hOldFont);

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
                if (g_osdAlpha > OSD_FADE_STEP) {
                    g_osdAlpha -= OSD_FADE_STEP;
                    SetLayeredWindowAttributes(hWnd, 0, g_osdAlpha, LWA_ALPHA);
                } else {
                    KillTimer(hWnd, OSD_FADE_TIMER_ID);
                    ShowWindow(hWnd, SW_HIDE);
                }
            }
            break;
        }

        case WM_LBUTTONDOWN:
            if (g_alwaysShowOsd) {
                // Преобразуем клиентские координаты в экранные для NC-сообщения
                POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                ClientToScreen(hWnd, &pt);
                SendMessage(hWnd, WM_NCLBUTTONDOWN, HTCAPTION, MAKELPARAM(pt.x, pt.y));
            }
            break;

        case WM_EXITSIZEMOVE:
            // После перетаскивания восстанавливаем Z-позицию поверх всех окон
            SetWindowPos(hWnd, HWND_TOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            break;

        case WM_SETCURSOR:
            // Устанавливаем курсор-стрелку; WM_SETCURSOR достаточно — WM_MOUSEMOVE не нужен
            if (LOWORD(lParam) == HTCLIENT) {
                SetCursor(LoadCursor(NULL, IDC_ARROW));
                return TRUE;
            }
            break;

        case WM_DESTROY: {
            // Освобождаем кешированные GDI-объекты
            if (hCachedFont)  { DeleteObject(hCachedFont);  hCachedFont  = NULL; }
            if (hCachedBrush) { DeleteObject(hCachedBrush); hCachedBrush = NULL; }
            // Освобождаем DWM-библиотеку
            if (hDwmApi) { FreeLibrary(hDwmApi); hDwmApi = NULL; }
            dwmInitialized = FALSE;
            pDwmSetWindowAttribute = NULL;
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

// Показать контекстное меню трея
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
    if (g_khook)   { UnhookWindowsHookEx(g_khook); g_khook  = NULL; }
    if (g_hIcon)   { DestroyIcon(g_hIcon);          g_hIcon  = NULL; }
    if (g_hMenu)   { DestroyMenu(g_hMenu);           g_hMenu  = NULL; }
    if (g_hEvent)  { CloseHandle(g_hEvent);          g_hEvent = NULL; }
    if (g_hOsdWnd) { DestroyWindow(g_hOsdWnd);       g_hOsdWnd = NULL; }
}

// Загрузка настроек из INI-файла
void LoadSettingsFromIni() {
    TCHAR iniPath[MAX_PATH];
    GetIniPath(iniPath, _countof(iniPath));

    g_key = GetPrivateProfileInt(L"Settings", L"Key", VK_RMENU, iniPath);

    TCHAR modifiers[64];
    GetPrivateProfileString(L"Settings", L"Modifiers", L"Ctrl+Shift",
                            modifiers, _countof(modifiers), iniPath);
    g_modCtrl  = (_tcsstr(modifiers, L"Ctrl")  != NULL);
    g_modShift = (_tcsstr(modifiers, L"Shift") != NULL);
    g_modAlt   = (_tcsstr(modifiers, L"Alt")   != NULL);

    g_alwaysShowOsd  = GetPrivateProfileInt(L"Settings", L"always_show_osd", 0, iniPath);
    g_osdConfigAlpha = (BYTE)GetPrivateProfileInt(L"Settings", L"osd_alpha", 220, iniPath);

    TCHAR colorStr[16];
    if (GetPrivateProfileString(L"Settings", L"Color", L"7E7E7E",
                                colorStr, _countof(colorStr), iniPath) > 0) {
        if (_tcslen(colorStr) == 6) {
            long colorVal = _tcstol(colorStr, NULL, 16);
            int r = (colorVal >> 16) & 0xFF;
            int g = (colorVal >> 8)  & 0xFF;
            int b =  colorVal        & 0xFF;
            g_osdColor = RGB(r, g, b);
        }
    }
}

// Получение имени текущей раскладки
void GetLayoutName(TCHAR* buffer, int bufferSize) {
    HWND fgWnd = GetForegroundWindow();
    if (!fgWnd) {
        _tcsncpy_s(buffer, bufferSize, L"??", _TRUNCATE);
        return;
    }

    DWORD threadId = GetWindowThreadProcessId(fgWnd, NULL);
    HKL   hkl      = GetKeyboardLayout(threadId);
    LANGID langID  = LOWORD(hkl);
    LCID   lcid    = MAKELCID(langID, SORT_DEFAULT);

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

    // Fallback: ни один из методов не вернул имя
    _tcsncpy_s(buffer, bufferSize, L"??", _TRUNCATE);
}

// Показать OSD-окно
void ShowOsdWindow() {
    int w = g_alwaysShowOsd ? 80  : 150;
    int h = g_alwaysShowOsd ? 60  : 100;

    if (!g_hOsdWnd) {
        g_hOsdWnd = CreateWindowEx(
            WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
            OSD_CLASS_NAME, L"", WS_POPUP,
            0, 0, w, h,
            NULL, NULL, g_hInstance, NULL);
        if (!g_hOsdWnd) return;
    }

    GetLayoutName(g_osdText, _countof(g_osdText));

    KillTimer(g_hOsdWnd, OSD_TIMER_ID);
    KillTimer(g_hOsdWnd, OSD_FADE_TIMER_ID);

    g_osdAlpha = g_osdConfigAlpha;
    SetLayeredWindowAttributes(g_hOsdWnd, 0, g_osdAlpha, LWA_ALPHA);

    if (!g_alwaysShowOsd) {
        HMONITOR hMonitor = MonitorFromWindow(GetForegroundWindow(), MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = { .cbSize = sizeof(mi) };
        int x = 0, y = 0;
        if (GetMonitorInfo(hMonitor, &mi)) {
            x = mi.rcMonitor.left + (mi.rcMonitor.right  - mi.rcMonitor.left - w) / 2;
            y = mi.rcMonitor.top  + (mi.rcMonitor.bottom - mi.rcMonitor.top  - h) / 2;
        }
        SetWindowPos(g_hOsdWnd, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE);
    } else {
        SetWindowPos(g_hOsdWnd, HWND_TOPMOST, 0, 0, w, h, SWP_NOMOVE | SWP_NOACTIVATE);
    }

    ShowWindow(g_hOsdWnd, SW_SHOWNOACTIVATE);
    InvalidateRect(g_hOsdWnd, NULL, TRUE);
    UpdateWindow(g_hOsdWnd);

    if (!g_alwaysShowOsd) {
        SetTimer(g_hOsdWnd, OSD_TIMER_ID, 1000, NULL);
    }
}

// Инициализация приложения
BOOL InitApplication() {
    WNDCLASSEX wc = { .cbSize = sizeof(wc) };
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = g_hInstance;
    wc.lpszClassName = APP_CLASS_NAME;
    if (!RegisterClassEx(&wc)) {
        ShowFatalError(L"Failed to register window class.", TRUE);
        return FALSE;
    }

    WNDCLASSEX wc_osd = { .cbSize = sizeof(wc_osd) };
    wc_osd.lpfnWndProc   = OSDWndProc;
    wc_osd.hInstance     = g_hInstance;
    wc_osd.lpszClassName = OSD_CLASS_NAME;
    wc_osd.hbrBackground = NULL;
    wc_osd.hCursor       = LoadCursor(NULL, IDC_ARROW);
    if (!RegisterClassEx(&wc_osd)) {
        ShowFatalError(L"Failed to register OSD window class.", TRUE);
        return FALSE;
    }

    g_hWnd = CreateWindowEx(0, APP_CLASS_NAME, L"Keyboard Layout Switcher",
                            WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                            100, 100, NULL, NULL, g_hInstance, NULL);
    if (!g_hWnd) {
        ShowFatalError(L"Failed to create window.", TRUE);
        return FALSE;
    }

    g_khook = SetWindowsHookEx(WH_KEYBOARD_LL, KbdHook, g_hInstance, 0);
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
    g_hInstance = GetModuleHandle(NULL);

    // Проверка на повторный запуск
    g_hEvent = CreateEvent(NULL, TRUE, FALSE, APP_GUID);
    DWORD createErr = GetLastError(); // Сохраняем до любых других вызовов
    if (!g_hEvent) {
        ShowFatalError(L"Failed to create event object.", TRUE);
    }
    if (createErr == ERROR_ALREADY_EXISTS) {
        CloseHandle(g_hEvent);
        g_hEvent = NULL;
        ExitProcess(0);
    }

    LoadSettingsFromIni();

    if (InitApplication()) {
        if (g_alwaysShowOsd) {
            ShowOsdWindow();
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
