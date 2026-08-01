// overlay.cpp - оверлеи KBLSWITCH: OSD-индикатор раскладки, окно-индикатор
// у текстового курсора, получение имени текущей раскладки и сохранение
// позиции OSD в реестре.

#include "kblswitch.h"

// === Состояние (используется только в этом модуле) ===
static BYTE    g_osdAlpha = 255;   // Текущая прозрачность OSD
static TCHAR   g_osdText[64] = {0}; // Текст для OSD
static TCHAR   g_indicatorText[16] = {0}; // Текст индикатора
static HWND    g_indLastFgWnd = NULL; // Последнее активное окно с индикатором

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

// Сохранение позиции OSD-окна в реестре
static void SaveOsdPosition(int x, int y) {
    HKEY hKey;
    if (RegCreateKeyEx(HKEY_CURRENT_USER, REG_KEY_PATH,
                       0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        DWORD vx = (DWORD)x, vy = (DWORD)y;
        RegSetValueEx(hKey, L"OsdX", 0, REG_DWORD, (const BYTE*)&vx, sizeof(vx));
        RegSetValueEx(hKey, L"OsdY", 0, REG_DWORD, (const BYTE*)&vy, sizeof(vy));
        RegCloseKey(hKey);
    }
}

// Восстановление позиции OSD-окна из реестра.
// Возвращает TRUE и заполняет *x/*y, если значения найдены
static BOOL LoadOsdPosition(int* x, int* y) {
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

            HBRUSH hBrush = CreateSolidBrush(g_osdColor);
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

        case WM_CONTEXTMENU: {
            // Правая кнопка мыши по окну-индикатору раскладки (OSD):
            // контекстное меню - Настройки / О программе / Выход
            POINT pt;
            pt.x = GET_X_LPARAM(lParam);
            pt.y = GET_Y_LPARAM(lParam);
            if (pt.x == -1 && pt.y == -1) {
                GetCursorPos(&pt);
            }
            ShowAppContextMenu(pt);
            return 0;
        }

        case WM_DESTROY: {

            break;
        }

        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
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
        MONITORINFO mi = {};
        mi.cbSize = sizeof(mi);
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

// Получение позиции текстового курсора (каретки) активного окна в экранных координатах.
// Возвращает TRUE, если позиция определена.
//
// Современные приложения (VS Code, браузеры на Chromium и др.) рисуют каретку сами
// и не используют системную каретку Windows, поэтому GetGUIThreadInfo возвращает
// hwndCaret = NULL. В этом случае используем позицию курсора мыши как приближение
// к месту ввода (пользователь обычно кликает туда, где хочет печатать).
static BOOL GetTextCaretScreenPos(POINT* pt) {
    if (!pt) return FALSE;

    HWND fgWnd = GetForegroundWindow();
    if (!fgWnd) return FALSE;

    DWORD threadId = GetWindowThreadProcessId(fgWnd, NULL);

    GUITHREADINFO gti;
    ZeroMemory(&gti, sizeof(gti));
    gti.cbSize = sizeof(gti);
    if (GetGUIThreadInfo(threadId, &gti) && gti.hwndCaret && IsWindowVisible(gti.hwndCaret)) {
        // По документации rcCaret задан в клиентских координатах окна hwndCaret,
        // поэтому переводим его верхнюю левую точку в экранные координаты.
        POINT ptClient;
        ptClient.x = gti.rcCaret.left;
        ptClient.y = gti.rcCaret.top;
        if (ClientToScreen(gti.hwndCaret, &ptClient)) {
            pt->x = ptClient.x;
            pt->y = ptClient.y;
            return TRUE;
        }
    }

    // Системная каретка недоступна (VS Code, Chromium) - используем мышь
    return GetCursorPos(pt) != FALSE;
}

// Обновление окна-индикатора: позиционирует его над текстовым курсором.
void UpdateLayoutIndicator() {
    if (!g_showCaretIndicator) return;

    POINT caret;
    if (!GetTextCaretScreenPos(&caret)) {
        HideLayoutIndicator();
        return;
    }

    // При смене активного окна - показываем индикатор заново
    HWND fgWnd = GetForegroundWindow();
    if (fgWnd != g_indLastFgWnd) {
        g_indLastFgWnd = fgWnd;
        g_indicatorTypedChars = 0;
        g_indicatorShowTick = GetTickCount();
    }

    // Клик мыши (была нажата с прошлого опроса) - пользователь переместил
    // курсор, показываем индикатор заново
    if (GetAsyncKeyState(VK_LBUTTON) & 1) {
        g_indicatorTypedChars = 0;
        g_indicatorShowTick = GetTickCount();
    }

    // После начала набора текста индикатор скрываем
    if (g_indicatorTypedChars >= INDICATOR_HIDE_AFTER_CHARS) {
        HideLayoutIndicator();
        return;
    }

    // Автоматическое закрытие по таймауту (indicator_timeout из INI)
    if (g_indicatorShowTick != 0 && g_indicatorTimeoutMs > 0 &&
        (DWORD)(GetTickCount() - g_indicatorShowTick) >= g_indicatorTimeoutMs) {
        HideLayoutIndicator();
        return;
    }

    const int size = LAYOUT_IND_SIZE;
    const int offset = 8; // Отступ над кареткой

    if (!g_hLayoutIndWnd) {
        g_hLayoutIndWnd = CreateWindowEx(
            WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
            LAYOUT_IND_CLASS_NAME, L"", WS_POPUP,
            0, 0, size, size,
            NULL, NULL, GetModuleHandle(NULL), NULL);
        if (!g_hLayoutIndWnd) return;
        SetLayeredWindowAttributes(g_hLayoutIndWnd, 0, g_osdConfigAlpha, LWA_ALPHA);
    }

    // Обновляем текст индикатора при смене раскладки
    TCHAR layout[16] = {0};
    GetLayoutName(layout, _countof(layout));

    if (_tcscmp(g_indicatorText, layout) != 0) {
        _tcscpy_s(g_indicatorText, _countof(g_indicatorText), layout);
        InvalidateRect(g_hLayoutIndWnd, NULL, TRUE);
    }

    // Позиция: над кареткой, по центру
    int x = caret.x - size / 2;
    int y = caret.y - size - offset;

    // Зажимаем в пределах виртуального рабочего стола (все мониторы)
    int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    if (x < vx) x = vx;
    if (x + size > vx + vw) x = vx + vw - size;

    if (y < vy) {
        // Сверху не хватает места - показываем снизу от каретки
        y = caret.y + offset;
    }
    if (y + size > vy + vh) y = vy + vh - size;

    // Фиксируем момент показа (если таймер ещё не был запущен)
    if (g_indicatorShowTick == 0) {
        g_indicatorShowTick = GetTickCount();
    }

    SetWindowPos(g_hLayoutIndWnd, HWND_TOPMOST, x, y, size, size,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    UpdateWindow(g_hLayoutIndWnd);
}

// Скрытие окна-индикатора, если оно показано
void HideLayoutIndicator() {
    if (g_hLayoutIndWnd && IsWindowVisible(g_hLayoutIndWnd)) {
        ShowWindow(g_hLayoutIndWnd, SW_HIDE);
    }
}

// Обработчик сообщений для окна-индикатора раскладки у курсора ввода
LRESULT CALLBACK LayoutIndicatorWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    // Высота шрифта для размера окна 32x32
    static const int IND_FONT_HEIGHT = -16;

    // Кэширование DWM API для скругления углов
    static HRESULT (WINAPI *pDwmSetWindowAttribute)(HWND, DWORD, LPCVOID, DWORD) = NULL;
    static HMODULE hDwmApi = NULL;
    static BOOL dwmInitialized = FALSE;

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
                const DWORD DWMWA_WINDOW_CORNER_PREFERENCE = 33;
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

            HBRUSH hBrush = CreateSolidBrush(g_osdColor);
            HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, hBrush);
            FillRect(hdc, &rect, hBrush);
            SelectObject(hdc, hOldBrush);
            DeleteObject(hBrush);

            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(255, 255, 255));

            HFONT hFont = CreateFont(IND_FONT_HEIGHT, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Arial");
            HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);

            DrawText(hdc, g_indicatorText, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            SelectObject(hdc, hOldFont);
            DeleteObject(hFont);

            EndPaint(hWnd, &ps);
            break;
        }

        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}
