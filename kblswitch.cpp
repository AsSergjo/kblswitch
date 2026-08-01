// kblswitch.cpp - главный модуль KBLSWITCH: определение общих глобальных
// переменных, главное (скрытое) окно и жизненный цикл приложения.
// Логика вынесена в модули: input.cpp (хук + исправление слова),
// overlay.cpp (OSD и индикатор), tray_menu.cpp (трей и меню),
// settings_apply.cpp (применение настроек).

#include "kblswitch.h"

// === Определения общих глобальных переменных (объявлены extern в kblswitch.h) ===
HHOOK    g_khook = NULL;
UINT     g_key = VK_RMENU;
BOOL     g_modCtrl = FALSE;
BOOL     g_modShift = FALSE;
BOOL     g_modAlt = FALSE;
BOOL     g_alwaysShowOsd = FALSE;
HWND     g_hWnd = NULL;          // Главное (скрытое) окно
HWND     g_hOsdWnd = NULL;       // Окно OSD
HMENU    g_hCtxMenu = NULL;      // Контекстное меню (трей и OSD)
HICON    g_hIcon = NULL;
BYTE     g_osdConfigAlpha = 220; // Прозрачность из INI
COLORREF g_osdColor = RGB(127, 127, 127); // Цвет фона OSD
HWND     g_hLayoutIndWnd = NULL; // Окно-индикатор у курсора
BOOL     g_showCaretIndicator = TRUE;
int      g_indicatorTypedChars = 0;
DWORD    g_indicatorTimeoutMs = 3000;
DWORD    g_indicatorShowTick = 0;
UINT     g_fixRuToEnKey = VK_F8; // Исправить слово: RU -> EN
UINT     g_fixEnToRuKey = VK_F9; // Исправить слово: EN -> RU
TCHAR    g_currentLang[8] = {0}; // Отслеживаемый текущий язык (обход stale-HKL)

// === Локальные переменные (используются только в этом модуле) ===
static HANDLE   g_hEvent = NULL;          // Событие единственного экземпляра
static UINT     WM_TASKBARCREATED = 0;

// Предварительное объявление (используется в ShowFatalError)
static void CleanupResources();

// === Функции ===

// Вывод критической ошибки и завершение работы
[[noreturn]] static void ShowFatalError(const TCHAR* message) {
    TCHAR* systemMessage = NULL;
    TCHAR finalMessage[1024];
    DWORD errorCode = GetLastError();

    FormatMessage(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL, errorCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPTSTR)&systemMessage, 0, NULL);

    if (systemMessage) {
        _sntprintf_s(finalMessage, _countof(finalMessage), _TRUNCATE, L"%s\n\nSystem Error: %s", message, systemMessage);
        LocalFree(systemMessage);
    } else {
        _tcsncpy_s(finalMessage, _countof(finalMessage), message, _TRUNCATE);
    }

    MessageBox(NULL, finalMessage, Lang(L"error_title").c_str(), MB_OK | MB_ICONERROR);
    CleanupResources();
    ExitProcess(1);
}

// Очистка всех выделенных ресурсов
static void CleanupResources() {
    if (g_khook) UnhookWindowsHookEx(g_khook);
    if (g_hIcon) DestroyIcon(g_hIcon);
    if (g_hCtxMenu) DestroyMenu(g_hCtxMenu);
    if (g_hEvent) CloseHandle(g_hEvent);
    if (g_hOsdWnd) DestroyWindow(g_hOsdWnd);
    if (g_hLayoutIndWnd) DestroyWindow(g_hLayoutIndWnd);
    g_khook = NULL;
    g_hIcon = NULL;
    g_hCtxMenu = NULL;
    g_hEvent = NULL;
    g_hOsdWnd = NULL;
    g_hLayoutIndWnd = NULL;
}

// Обработчик скрытого окна: таймеры, трей и команды меню.
static LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    static TCHAR previousLayout[64] = {0};

    if (message == WM_TRAYICON) {
        if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) {
            ShowTrayMenu(hWnd);
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
            ShowOsdWindow();
        } else if (wParam == LAYOUT_CHECK_TIMER_ID) {
            TCHAR currentLayout[64] = {0};
            GetLayoutName(currentLayout, _countof(currentLayout));

            if (previousLayout[0] != 0 && _tcscmp(previousLayout, currentLayout) != 0) {
                // HKL реально изменился - внешнее переключение (панель языка,
                // не-TSF приложения). Обновляем отслеживаемый язык и показываем OSD.
                _tcscpy_s(g_currentLang, _countof(g_currentLang), currentLayout);
                ShowOsdWindow();
                // Показываем индикатор актуальной раскладки у каретки (или у мыши)
                if (g_showCaretIndicator) {
                    g_indicatorTypedChars = 0;
                    g_indicatorShowTick = GetTickCount();
                }
            }
            _tcscpy_s(previousLayout, _countof(previousLayout), currentLayout);
        } else if (wParam == CARET_CHECK_TIMER_ID) {
            // Следим за текстовым курсором и показываем индикатор раскладки
            UpdateLayoutIndicator();
        }
        break;

    case WM_CREATE:
        WM_TASKBARCREATED = RegisterWindowMessage(_T("TaskbarCreated"));
        g_hIcon = (HICON)LoadImage(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_APPICON),
                                  IMAGE_ICON, 0, 0, LR_DEFAULTSIZE);

        // Контекстное меню (используется и в трее, и в OSD-индикаторе):
        // Настройки / О программе / Выход (с учётом текущего языка)
        BuildContextMenu();

        AddTrayIcon(hWnd);
        GetLayoutName(previousLayout, _countof(previousLayout));
        SetTimer(hWnd, LAYOUT_CHECK_TIMER_ID, 500, NULL);
        SetTimer(hWnd, CARET_CHECK_TIMER_ID, 150, NULL);
        break;

    case WM_COMMAND:
        if (LOWORD(wParam) == MENU_SETTINGS) {
            SettingsShowDialog(hWnd);
        } else if (LOWORD(wParam) == MENU_ABOUT) {
            std::wstring aboutText = Lang(L"about_text") + L"\n\n" + Lang(L"app_version");
            MessageBox(hWnd, aboutText.c_str(),
                Lang(L"about_title").c_str(), MB_OK | MB_ICONINFORMATION);
        } else if (LOWORD(wParam) == MENU_EXIT) {
            DestroyWindow(hWnd);
        }
        break;

    case WM_MEASUREITEM: {
        LPMEASUREITEMSTRUCT mi = (LPMEASUREITEMSTRUCT)lParam;
        if (mi->CtlType == ODT_MENU) {
            const MenuItemData* item = (const MenuItemData*)mi->itemData;
            if (item) {
                HDC hdc = GetDC(hWnd);
                HFONT font = CreateMenuFont();
                HFONT old = (HFONT)SelectObject(hdc, font);
                SIZE sz = {};
                GetTextExtentPoint32W(hdc, item->text.c_str(),
                                      (int)item->text.size(), &sz);
                SelectObject(hdc, old);
                DeleteObject(font);
                ReleaseDC(hWnd, hdc);
                mi->itemWidth = sz.cx + 42;
            } else {
                mi->itemWidth = 140;
            }
            mi->itemHeight = 30;
            return TRUE;
        }
        break;
    }

    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT di = (LPDRAWITEMSTRUCT)lParam;
        if (di->CtlType == ODT_MENU) {
            DrawMenuItem(di);
            return TRUE;
        }
        break;
    }

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

// Инициализация приложения и его компонентов
static void InitApplication(HINSTANCE hInstance) {
    // Регистрация основного класса окна
    WNDCLASSEX wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = APP_CLASS_NAME;
    if (!RegisterClassEx(&wc)) {
        ShowFatalError(Lang(L"error_register_class").c_str());
    }

    // Регистрация класса OSD окна
    WNDCLASSEX wc_osd = {};
    wc_osd.cbSize = sizeof(wc_osd);
    wc_osd.lpfnWndProc = OSDWndProc;
    wc_osd.hInstance = hInstance;
    wc_osd.lpszClassName = OSD_CLASS_NAME;
    wc_osd.hbrBackground = NULL;
    wc_osd.hCursor = LoadCursor(NULL, IDC_ARROW);
    if (!RegisterClassEx(&wc_osd)) {
        ShowFatalError(Lang(L"error_register_osd").c_str());
    }

    // Регистрация класса окна-индикатора раскладки у курсора
    WNDCLASSEX wc_ind = {};
    wc_ind.cbSize = sizeof(wc_ind);
    wc_ind.lpfnWndProc = LayoutIndicatorWndProc;
    wc_ind.hInstance = hInstance;
    wc_ind.lpszClassName = LAYOUT_IND_CLASS_NAME;
    wc_ind.hbrBackground = NULL;
    wc_ind.hCursor = LoadCursor(NULL, IDC_ARROW);
    if (!RegisterClassEx(&wc_ind)) {
        ShowFatalError(Lang(L"error_register_indicator").c_str());
    }

    // Скрытое окно-приёмник сообщений, таймеров и команд меню.
    g_hWnd = CreateWindowEx(0, APP_CLASS_NAME, APP_CLASS_NAME, WS_OVERLAPPED,
        0, 0, 0, 0, NULL, NULL, hInstance, NULL);
    if (!g_hWnd) {
        ShowFatalError(Lang(L"error_create_window").c_str());
    }

    // Установка хука и таймера
    g_khook = SetWindowsHookEx(WH_KEYBOARD_LL, KbdHook, hInstance, 0);
    if (!g_khook) {
        ShowFatalError(Lang(L"error_keyboard_hook").c_str());
    }

}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;

    g_hEvent = CreateEvent(NULL, TRUE, FALSE, APP_GUID);
    if (!g_hEvent) {
        ShowFatalError(Lang(L"error_create_event").c_str());
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        // Первый экземпляр продолжает работать, второй просто завершается
        CloseHandle(g_hEvent);
        g_hEvent = NULL;
        return 0;
    }

    InitLanguageManager();   // язык по умолчанию - по системе
    LoadSettingsFromIni();   // затем перекрываем языком из INI

    InitApplication(hInstance);
    if (g_alwaysShowOsd) {
        ShowOsdWindow();
    }

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    CleanupResources();
    return 0;
}
