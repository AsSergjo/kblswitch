#ifndef UNICODE
#define UNICODE
#endif
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
#define OSD_CLASS_NAME L"OSDWindowClass"

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
BYTE    g_osdAlpha = 255; // Текущая прозрачность OSD
BYTE    g_osdConfigAlpha = 220; // Прозрачность из INI
TCHAR   g_osdText[64] = {0}; // Текст для OSD
COLORREF g_osdColor = RGB(127, 127, 127); // Цвет фона OSD

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
void ParseCommandLine();
void LoadSettingsFromIni();
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

// Низкоуровневый хук клавиатуры
LRESULT CALLBACK KbdHook(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        KBDLLHOOKSTRUCT* ks = (KBDLLHOOKSTRUCT*)lParam;
        if (!(ks->flags & LLKHF_INJECTED)) {
            if (ks->vkCode == g_key && (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN)) {
                // Нажимаем модификаторы в соответствии с настройками
                if (g_modCtrl)  keybd_event(VK_CONTROL, 0, 0, 0);
                if (g_modShift) keybd_event(VK_SHIFT,   0, 0, 0);
                if (g_modAlt)   keybd_event(VK_MENU,    0, 0, 0);
                
                // Отпускаем в обратном порядке
                if (g_modAlt)   keybd_event(VK_MENU,    0, KEYEVENTF_KEYUP, 0);
                if (g_modShift) keybd_event(VK_SHIFT,   0, KEYEVENTF_KEYUP, 0);
                if (g_modCtrl)  keybd_event(VK_CONTROL, 0, KEYEVENTF_KEYUP, 0);
                
                Sleep(25);

                PostMessage(g_hWnd, WM_USER + 1, 0, 0);

                return 1;
            }
        }
    }
    return CallNextHookEx(g_khook, nCode, wParam, lParam);
}

// Функция обработки сообщений окна
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
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
    case WM_USER + 1:
        ShowOsdWindow(GetModuleHandle(NULL));
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
                SendMessage(hWnd, WM_NCLBUTTONDOWN, HTCAPTION, lParam);
            }
            break;

        case WM_SETCURSOR:
            // Устанавливаем курсор стрелки для клиентской области
            if (LOWORD(lParam) == HTCLIENT) {
                SetCursor(LoadCursor(NULL, IDC_ARROW));
                return TRUE;
            }
            break;

        case WM_MOUSEMOVE:
            // Гарантируем, что курсор остаётся стрелкой при движении мыши
            SetCursor(LoadCursor(NULL, IDC_ARROW));
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

// Таймер для проверки сигнала завершения от другой копии
void CALLBACK TimerCb(HWND hWnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime) {
    if (WaitForSingleObject(g_hEvent, 0) == WAIT_OBJECT_0) {
        PostMessage(g_hWnd, WM_CLOSE, 0, 0);
    }
}

// Разбор аргументов командной строки (оставлен для совместимости, но не делает ничего)
void ParseCommandLine() {
    // Все настройки теперь загружаются из INI-файла
}

// Загрузка настроек из INI-файла
void LoadSettingsFromIni() {
    TCHAR iniPath[MAX_PATH];
    GetModuleFileName(NULL, iniPath, MAX_PATH);
    TCHAR* slash = _tcsrchr(iniPath, L'\\');
    if (slash) *(slash + 1) = L'\0';
    _tcscat_s(iniPath, _countof(iniPath), L"kblswitch.ini");

    // Чтение клавиши
    g_key = GetPrivateProfileInt(L"Settings", L"Key", VK_RMENU, iniPath);

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
    
    if (g_alwaysShowOsd) {
        g_osdAlpha = g_osdConfigAlpha;
    } else {
        g_osdAlpha = 225;
    }
    SetLayeredWindowAttributes(g_hOsdWnd, 0, g_osdAlpha, LWA_ALPHA);

    // Если окно не в режиме "всегда наверху", центрируем его
    if (!g_alwaysShowOsd) {
        HMONITOR hMonitor = MonitorFromWindow(GetForegroundWindow(), MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = { .cbSize = sizeof(mi) };
        if (GetMonitorInfo(hMonitor, &mi)) {
            int x = mi.rcMonitor.left + (mi.rcMonitor.right - mi.rcMonitor.left - w) / 2;
            int y = mi.rcMonitor.top + (mi.rcMonitor.bottom - mi.rcMonitor.top - h) / 2;
            SetWindowPos(g_hOsdWnd, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE);
        }
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
    if (SetTimer(NULL, 0, 500, TimerCb) == 0) {
        ShowFatalError(L"Failed to set timer.", TRUE);
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
        // Другой экземпляр уже запущен
        ShowFatalError(L"kblswitch is already running!", FALSE);
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