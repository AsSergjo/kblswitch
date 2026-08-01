// tray_menu.cpp - иконка в системном трее и контекстное меню
// (Настройки / О программе / Выход) с иконками-глифами из Segoe Fluent Icons.

#include "kblswitch.h"

// Данные пунктов меню (используются только в этом модуле)
static MenuItemData g_menuItems[3];

// Добавление иконки в системный трей
void AddTrayIcon(HWND hWnd) {
    NOTIFYICONDATA nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hWnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = g_hIcon;
    std::wstring tip = Lang(L"app_name");
    wcsncpy_s(nid.szTip, _countof(nid.szTip), tip.c_str(), _TRUNCATE);
    Shell_NotifyIcon(NIM_ADD, &nid);
}

// Удаление иконки из системного трея
void RemoveTrayIcon() {
    NOTIFYICONDATA nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_hWnd;
    nid.uID = 1;
    Shell_NotifyIcon(NIM_DELETE, &nid);
}

// (Пере)создание контекстного меню (трей + OSD) с учётом текущего языка.
// Пункты - owner-draw: иконки-глифы из Segoe Fluent Icons + текст.
void BuildContextMenu() {
    if (g_hCtxMenu) { DestroyMenu(g_hCtxMenu); g_hCtxMenu = NULL; }
    g_hCtxMenu = CreatePopupMenu();

    g_menuItems[0] = { MENU_SETTINGS, 0xE713, Lang(L"menu_settings") }; // Настройки (шестерёнка)
    g_menuItems[1] = { MENU_ABOUT,    0xE946, Lang(L"menu_about") };    // О программе (инфо)
    g_menuItems[2] = { MENU_EXIT,     0xE7E8, Lang(L"menu_exit") };     // Выход (питание)

    if (g_hCtxMenu) {
        AppendMenu(g_hCtxMenu, MF_OWNERDRAW, g_menuItems[0].id, (LPCTSTR)&g_menuItems[0]);
        AppendMenu(g_hCtxMenu, MF_SEPARATOR, 0, NULL);
        AppendMenu(g_hCtxMenu, MF_OWNERDRAW, g_menuItems[1].id, (LPCTSTR)&g_menuItems[1]);
        AppendMenu(g_hCtxMenu, MF_SEPARATOR, 0, NULL);
        AppendMenu(g_hCtxMenu, MF_OWNERDRAW, g_menuItems[2].id, (LPCTSTR)&g_menuItems[2]);
    }
}

// Шрифт пунктов меню (крупнее стандартного)
HFONT CreateMenuFont() {
    return CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

// Отрисовка пункта контекстного меню: глиф Segoe Fluent Icons + текст
void DrawMenuItem(LPDRAWITEMSTRUCT di) {
    const MenuItemData* item = (const MenuItemData*)di->itemData;
    if (!item) return;
    HDC hdc = di->hDC;
    RECT rc = di->rcItem;
    bool selected = (di->itemState & ODS_SELECTED) != 0;

    // Фон пункта: закрашиваем всегда (иначе снятая подсветка остаётся синей)
    HBRUSH bg = CreateSolidBrush(selected ? GetSysColor(COLOR_HIGHLIGHT)
                                          : GetSysColor(COLOR_MENU));
    FillRect(hdc, &rc, bg);
    DeleteObject(bg);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, selected ? GetSysColor(COLOR_HIGHLIGHTTEXT)
                               : GetSysColor(COLOR_MENUTEXT));

    // Иконка-глиф из шрифта Segoe Fluent Icons
    if (item->glyph != 0) {
        HFONT iconFont = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, L"Segoe Fluent Icons");
        HFONT oldIcon = (HFONT)SelectObject(hdc, iconFont);
        RECT ir = { rc.left + 6, rc.top, rc.left + 30, rc.bottom };
        WCHAR glyph[2] = { item->glyph, 0 };
        DrawTextW(hdc, glyph, 1, &ir, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        SelectObject(hdc, oldIcon);
        DeleteObject(iconFont);
    }

    // Текст пункта (крупный шрифт)
    HFONT menuFont = CreateMenuFont();
    HFONT oldFont = (HFONT)SelectObject(hdc, menuFont);
    RECT tr = { rc.left + 34, rc.top, rc.right - 8, rc.bottom };
    DrawTextW(hdc, item->text.c_str(), -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdc, oldFont);
    DeleteObject(menuFont);
}

// Обновление подсказки у иконки трея (после смены языка)
void UpdateTrayTip() {
    if (!g_hWnd) return;
    NOTIFYICONDATA nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_hWnd;
    nid.uID = 1;
    nid.uFlags = NIF_TIP;
    std::wstring tip = Lang(L"app_name");
    wcsncpy_s(nid.szTip, _countof(nid.szTip), tip.c_str(), _TRUNCATE);
    Shell_NotifyIcon(NIM_MODIFY, &nid);
}

// Показать контекстное меню
void ShowTrayMenu(HWND hWnd) {
    if (!g_hCtxMenu) return;
    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(hWnd);
    TrackPopupMenu(g_hCtxMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hWnd, NULL);
    PostMessage(hWnd, WM_NULL, 0, 0);
}

// Показать контекстное меню (Настройки / О программе / Выход).
// Используется и главным окном, и окном-индикатором раскладки (OSD).
// Команды меню обрабатывает главное окно g_hWnd.
void ShowAppContextMenu(POINT pt) {
    if (!g_hCtxMenu) return;
    SetForegroundWindow(g_hWnd);
    TrackPopupMenu(g_hCtxMenu, TPM_RIGHTBUTTON | TPM_LEFTALIGN, pt.x, pt.y, 0, g_hWnd, NULL);
    PostMessage(g_hWnd, WM_NULL, 0, 0);
}
