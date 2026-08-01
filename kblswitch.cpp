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

// === Локальные переменные (используются только в этом модуле) ===
static HANDLE   g_hEvent = NULL;          // Событие единственного экземпляра
static UINT     WM_TASKBARCREATED = 0;
static UINT_PTR g_hExitCheckTimer = 0;    // ID таймера проверки завершения
static TCHAR    g_prog_dir[MAX_PATH];
static DWORD    g_prog_dir_len;

// === Вспомогательные функции оформления ===

// Предварительное объявление (используется в ShowFatalError)
static void CleanupResources();

// Тёмная тема приложения (Windows 10/11: "Приложения - по умолчанию тёмные")
static BOOL IsSystemDarkTheme() {
    DWORD v = 1, sz = sizeof(v);
    if (RegGetValueW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
            L"AppsUseLightTheme", RRF_RT_REG_DWORD, NULL, &v, &sz) == ERROR_SUCCESS) {
        return v == 0;
    }
    return FALSE;
}

// Скруглённые углы и тёмный заголовок в стиле Windows 11 (через DWM)
static void ApplyWindowStyle(HWND hWnd) {
    HMODULE hDwm = LoadLibraryW(L"dwmapi.dll");
    if (!hDwm) return;

    typedef HRESULT(WINAPI* DwmSetWindowAttribute_t)(HWND, DWORD, LPCVOID, DWORD);
    DwmSetWindowAttribute_t pDwm =
        (DwmSetWindowAttribute_t)GetProcAddress(hDwm, "DwmSetWindowAttribute");
    if (pDwm) {
        BOOL dark = IsSystemDarkTheme() ? TRUE : FALSE;
        pDwm(hWnd, 20, &dark, sizeof(dark));       // DWMWA_USE_IMMERSIVE_DARK_MODE
        DWORD corner = 2;                          // DWM_WINDOW_CORNER_PREFERENCE_ROUND
        pDwm(hWnd, 33, &corner, sizeof(corner));   // DWMWA_WINDOW_CORNER_PREFERENCE
    }
    FreeLibrary(hDwm);
}

// === Функции ===

// Вывод критической ошибки и завершение работы
static void ShowFatalError(const TCHAR* message, BOOL showSystemError) {
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

    MessageBox(NULL, finalMessage, Lang(L"error_title").c_str(), MB_OK | MB_ICONERROR);
    CleanupResources();
    ExitProcess(1);
}

// Очистка всех выделенных ресурсов
static void CleanupResources() {
    if (g_hExitCheckTimer) { KillTimer(NULL, g_hExitCheckTimer); g_hExitCheckTimer = 0; }
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

// Функция обработки сообщений главного окна
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
        g_hIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_APPICON));
        ApplyWindowStyle(hWnd);

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
            SettingsShowDialog(hWnd, GetModuleHandle(NULL));
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
            DrawMenuItem(hWnd, di);
            return TRUE;
        }
        break;
    }

    case WM_CONTEXTMENU: {
        // Контекстное меню главного окна (правая кнопка мыши)
        POINT pt;
        pt.x = GET_X_LPARAM(lParam);
        pt.y = GET_Y_LPARAM(lParam);
        if (pt.x == -1 && pt.y == -1) {
            GetCursorPos(&pt);
        }
        ShowAppContextMenu(pt);
        break;
    }

    case WM_GETMINMAXINFO: {
        MINMAXINFO* mmi = (MINMAXINFO*)lParam;
        mmi->ptMinTrackSize.x = 380;
        mmi->ptMinTrackSize.y = 200;
        break;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT rc;
        GetClientRect(hWnd, &rc);

        BOOL dark = IsSystemDarkTheme();
        COLORREF bg    = dark ? RGB(32, 32, 32) : RGB(243, 243, 243);
        COLORREF title = dark ? RGB(235, 235, 235) : RGB(28, 28, 28);
        COLORREF sub   = dark ? RGB(150, 150, 150) : RGB(96, 96, 96);
        COLORREF hint  = dark ? RGB(200, 200, 200) : RGB(60, 60, 60);
        COLORREF accent = dark ? RGB(0, 153, 255) : RGB(0, 95, 184);

        HBRUSH bgBrush = CreateSolidBrush(bg);
        FillRect(hdc, &rc, bgBrush);
        DeleteObject(bgBrush);

        HICON icon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_APPICON));
        DrawIconEx(hdc, 20, 20, icon, 40, 40, 0, NULL, DI_NORMAL);

        SetBkMode(hdc, TRANSPARENT);
        HFONT titleFont = CreateFont(-22, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Variable");
        HFONT subFont = CreateFont(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

        RECT tr = { 76, 16, rc.right - 20, 50 };
        SetTextColor(hdc, title);
        HFONT oldFont = (HFONT)SelectObject(hdc, titleFont);
        DrawText(hdc, Lang(L"app_name").c_str(), -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        RECT sr = { 76, 52, rc.right - 20, 74 };
        SetTextColor(hdc, sub);
        SelectObject(hdc, subFont);
        DrawText(hdc, Lang(L"main_subtitle").c_str(), -1, &sr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        // Акцентная линия
        HPEN pen = CreatePen(PS_SOLID, 2, accent);
        HGDIOBJ oldPen = SelectObject(hdc, pen);
        MoveToEx(hdc, 20, 96, NULL);
        LineTo(hdc, rc.right - 20, 96);
        SelectObject(hdc, oldPen);
        DeleteObject(pen);

        // Подсказка
        RECT hr = { 20, 116, rc.right - 20, 168 };
        SetTextColor(hdc, hint);
        DrawText(hdc, Lang(L"main_hint").c_str(), -1, &hr, DT_LEFT | DT_TOP);

        SelectObject(hdc, oldFont);
        DeleteObject(titleFont);
        DeleteObject(subFont);
        EndPaint(hWnd, &ps);
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
BOOL InitApplication(HINSTANCE hInstance) {
    // Регистрация основного класса окна
    WNDCLASSEX wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = APP_CLASS_NAME;
    if (!RegisterClassEx(&wc)) {
        ShowFatalError(Lang(L"error_register_class").c_str(), TRUE);
        return FALSE;
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
        ShowFatalError(Lang(L"error_register_osd").c_str(), TRUE);
        return FALSE;
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
        ShowFatalError(Lang(L"error_register_indicator").c_str(), TRUE);
        return FALSE;
    }

    // Создание главного окна (видимое; правая кнопка мыши - контекстное меню)
    g_hWnd = CreateWindowEx(0, APP_CLASS_NAME, L"Keyboard Layout Switcher", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 440, 260, NULL, NULL, hInstance, NULL);
    if (!g_hWnd) {
        ShowFatalError(Lang(L"error_create_window").c_str(), TRUE);
        return FALSE;
    }

    // Установка хука и таймера
    g_khook = SetWindowsHookEx(WH_KEYBOARD_LL, KbdHook, hInstance, 0);
    if (!g_khook) {
        ShowFatalError(Lang(L"error_keyboard_hook").c_str(), TRUE);
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
        ShowFatalError(Lang(L"error_create_event").c_str(), TRUE);
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        // Первый экземпляр продолжает работать, второй просто завершается
        CloseHandle(g_hEvent);
        g_hEvent = NULL;
        ExitProcess(0);
    }

    HINSTANCE hInstance = GetModuleHandle(NULL);
    InitLanguageManager();   // язык по умолчанию - по системе
    LoadSettingsFromIni();   // затем перекрываем языком из INI

    if (InitApplication(hInstance)) {
        if (g_alwaysShowOsd) {
            ShowOsdWindow(hInstance);
        }
        // Приложение живёт в трее; главное окно скрыто.
        // Контекстное меню (Настройки / О программе / Выход) доступно
        // правым кликом по OSD-индикатору раскладки или по иконке трея.
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
