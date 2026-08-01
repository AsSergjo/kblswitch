// overlay.cpp - оверлеи KBLSWITCH: OSD-индикатор раскладки, окно-индикатор
// у текстового курсора, получение имени текущей раскладки и сохранение
// позиции OSD в реестре.

#include "kblswitch.h"

// === Состояние (используется только в этом модуле) ===
static BYTE    g_osdAlpha = 255;   // Текущая прозрачность OSD
static TCHAR   g_osdText[64] = {0}; // Текст для OSD
static TCHAR   g_indicatorText[16] = {0}; // Текст индикатора
static HWND    g_indLastFgWnd = NULL; // Последнее активное окно с индикатором

// Закреплённая позиция индикатора для приложений без системной каретки
// (VS Code, Chromium): фиксируется на момент показа, чтобы индикатор
// не "ехал" за указателем мыши
static POINT   g_indAnchor = {0, 0};
static DWORD   g_indAnchorTick = 0;

static void ApplyRoundedCorners(HWND hWnd) {
    using DwmSetWindowAttributeFn = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
    static HMODULE dwmModule = LoadLibraryW(L"dwmapi.dll");
    static DwmSetWindowAttributeFn setWindowAttribute = dwmModule
        ? reinterpret_cast<DwmSetWindowAttributeFn>(
              GetProcAddress(dwmModule, "DwmSetWindowAttribute"))
        : nullptr;

    if (!setWindowAttribute) return;

    constexpr DWORD DWMWA_WINDOW_CORNER_PREFERENCE = 33;
    constexpr DWORD DWM_WINDOW_CORNER_PREFERENCE_ROUND = 2;
    DWORD cornerPreference = DWM_WINDOW_CORNER_PREFERENCE_ROUND;
    setWindowAttribute(hWnd, DWMWA_WINDOW_CORNER_PREFERENCE,
                       &cornerPreference, sizeof(cornerPreference));
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

// Язык для отображения в OSD/индикаторе - отслеживаемый приложением
// g_currentLang (обход stale-HKL в TSF-приложениях, где HKL не отражает
// реальный язык ввода). Если ещё не инициализирован - берём из HKL.
static void GetDisplayLayout(TCHAR* buf, int bufSize) {
    if (!g_currentLang[0]) {
        GetLayoutName(g_currentLang, _countof(g_currentLang));
    }
    _tcsncpy_s(buf, bufSize, g_currentLang, _TRUNCATE);
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

    // Состояние перетаскивания
    static BOOL  s_dragging    = FALSE;
    static int   s_dragOffsetX = 0;
    static int   s_dragOffsetY = 0;

    switch (message) {
        case WM_CREATE: {
            ApplyRoundedCorners(hWnd);
            break;
        }

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);

            RECT rect;
            GetClientRect(hWnd, &rect);

            HBRUSH hBrush = CreateSolidBrush(g_osdColor);
            FillRect(hdc, &rect, hBrush);
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
            // OSD можно перетаскивать мышью в любом режиме;
            // позиция запоминается в реестре
            {
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
                SetCursor(LoadCursor(NULL, IDC_SIZEALL));
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
            // Над OSD - курсор перемещения (окно можно перетаскивать)
            if (LOWORD(lParam) == HTCLIENT) {
                SetCursor(LoadCursor(NULL, IDC_SIZEALL));
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

        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

// Показать OSD-окно
void ShowOsdWindow() {
    int w = g_alwaysShowOsd ? 80 : 150;
    int h = g_alwaysShowOsd ? 60 : 100;

    if (!g_hOsdWnd) {
        g_hOsdWnd = CreateWindowEx(
            WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
            OSD_CLASS_NAME, L"", WS_POPUP,
            0, 0, w, h,
            NULL, NULL, GetModuleHandle(NULL), NULL);
        if (!g_hOsdWnd) return;
    }

    // Текст OSD: сразу после нашего переключения - целевой язык (обход
    // stale-HKL в TSF), иначе - по текущему HKL
    GetDisplayLayout(g_osdText, _countof(g_osdText));

    KillTimer(g_hOsdWnd, OSD_TIMER_ID);
    KillTimer(g_hOsdWnd, OSD_FADE_TIMER_ID);

    g_osdAlpha = g_osdConfigAlpha; // используем настройку из INI для обоих режимов
    SetLayeredWindowAttributes(g_hOsdWnd, 0, g_osdAlpha, LWA_ALPHA);

    // Позиция OSD: если есть сохранённая (перетаскивание) - используем её,
    // иначе показываем по центру активного монитора.
    int x = 0, y = 0;
    BOOL hasSaved = LoadOsdPosition(&x, &y);

    if (!hasSaved) {
        HMONITOR hMonitor = MonitorFromWindow(GetForegroundWindow(), MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = {};
        mi.cbSize = sizeof(mi);
        if (GetMonitorInfo(hMonitor, &mi)) {
            x = mi.rcMonitor.left + (mi.rcMonitor.right - mi.rcMonitor.left - w) / 2;
            y = mi.rcMonitor.top + (mi.rcMonitor.bottom - mi.rcMonitor.top - h) / 2;
        }
    }

    // Зажимаем позицию в пределах виртуального рабочего стола (все мониторы)
    int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (x < vx)          x = vx;
    if (y < vy)          y = vy;
    if (x + w > vx + vw) x = vx + vw - w;
    if (y + h > vy + vh) y = vy + vh - h;

    SetWindowPos(g_hOsdWnd, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE);

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
// Индикатор показывается при переключении языка в любом приложении.
// Современные приложения (VS Code, браузеры на Chromium и др.) рисуют каретку сами
// и не используют системную - GetGUIThreadInfo возвращает hwndCaret = NULL,
// поэтому для них используем позицию курсора мыши как приближение к месту ввода.
static BOOL GetTextCaretScreenPos(POINT* pt, BOOL* isSystemCaret) {
    if (!pt) return FALSE;
    if (isSystemCaret) *isSystemCaret = FALSE;

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
            if (isSystemCaret) *isSystemCaret = TRUE;
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
    BOOL isSystemCaret = FALSE;
    if (!GetTextCaretScreenPos(&caret, &isSystemCaret)) {
        HideLayoutIndicator();
        return;
    }

    // Триггеры показа действуют только в текстовых полях (есть системная
    // каретка): клик мышью по полю или переключение на такое окно показывают
    // индикатор У КАРЕТКИ. В окнах без системной каретки (VS Code, Chromium,
    // OSD, рабочий стол) клики и смена окна индикатор НЕ вызывают.
    if (isSystemCaret) {
        // Смена активного окна на текстовое
        HWND fgWnd = GetForegroundWindow();
        if (fgWnd != g_indLastFgWnd) {
            g_indLastFgWnd = fgWnd;
            g_indicatorTypedChars = 0;
            g_indicatorShowTick = GetTickCount();
        }
        // Клик мыши в текстовом поле (каретка переместилась)
        if (GetAsyncKeyState(VK_LBUTTON) & 1) {
            g_indicatorTypedChars = 0;
            g_indicatorShowTick = GetTickCount();
        }
    }

    // Индикатор показывается только по явному срабатыванию: переключение
    // языка, навигация по тексту, клик/переход в текстовое поле.
    if (g_indicatorShowTick == 0) {
        HideLayoutIndicator();
        return;
    }

    // После начала набора текста индикатор скрываем
    if (g_indicatorTypedChars >= INDICATOR_HIDE_AFTER_CHARS) {
        HideLayoutIndicator();
        return;
    }

    // Автоматическое закрытие по таймауту (indicator_timeout из INI)
    if (g_indicatorTimeoutMs > 0 &&
        (DWORD)(GetTickCount() - g_indicatorShowTick) >= g_indicatorTimeoutMs) {
        HideLayoutIndicator();
        return;
    }

    // Для приложений без системной каретки (VS Code, Chromium) фиксируем
    // позицию у мыши на момент показа, чтобы индикатор не "ехал" за мышью.
    // При каждом новом срабатывании (смена языка, навигация) позиция
    // обновляется заново.
    if (!isSystemCaret) {
        if (g_indAnchorTick != g_indicatorShowTick) {
            g_indAnchor = caret;
            g_indAnchorTick = g_indicatorShowTick;
        }
        caret = g_indAnchor;
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

    // Обновляем текст индикатора: сразу после переключения - целевой язык
    // (обход stale-HKL), иначе - по текущему HKL
    TCHAR layout[16] = {0};
    GetDisplayLayout(layout, _countof(layout));

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

    switch (message) {
        case WM_CREATE: {
            ApplyRoundedCorners(hWnd);
            break;
        }

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);

            RECT rect;
            GetClientRect(hWnd, &rect);

            HBRUSH hBrush = CreateSolidBrush(g_osdColor);
            FillRect(hdc, &rect, hBrush);
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
