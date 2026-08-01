// setting.cpp - окно настроек KBLSWITCH в современном стиле Windows 11.
// Полностью рисуется через GDI+ (как окно настроек лаунчера):
// бесшовные «плитки», переключатели, слайдеры, выбор цвета, захват клавиш.
// Тема - по системе (тёмная/светлая). Компилируется как C++17.

#define _WIN32_WINNT 0x0A00
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef UNICODE
#define UNICODE
#endif

#include <windows.h>
#include <windowsx.h>
#include <tchar.h>
#include <stdlib.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <gdiplus.h>
#include "resource.h"
#include "settings.h"
#include "lng_manager.h"

using namespace Gdiplus;

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "dwmapi.lib")

#define SETTINGS_CLASS L"KBLSwitchSettingsClass"
#define SETTINGS_W 408
#define SETTINGS_H 768

// --- Области окна ---
enum class Hit {
    None, Key, ModCtrl, ModShift, ModAlt, FixRuEn, FixEnRu,
    AlwaysOsd, Color, Alpha, Caret, Timeout, LangRu, LangEn, Ok
};

static RECT RowRect(int index)
{
    int top = 58 + index * 64;   // шаг 64, высота строки 58 (зазор 6px)
    return { 22, top, SETTINGS_W - 22, top + 58 };
}
static RECT KeyBtnRect()     { RECT r = RowRect(0); return { 246, r.top + 13, 378, r.top + 45 }; }
static RECT ModChipRect(int i){ RECT r = RowRect(1); int x = 246 + i * 48; return { x, r.top + 15, x + 42, r.top + 43 }; }
static RECT FixRuEnBtnRect() { RECT r = RowRect(2); return { 246, r.top + 13, 378, r.top + 45 }; }
static RECT FixEnRuBtnRect() { RECT r = RowRect(3); return { 246, r.top + 13, 378, r.top + 45 }; }
static RECT ColorBtnRect()   { RECT r = RowRect(5); return { 246, r.top + 13, 378, r.top + 45 }; }
static RECT AlphaTrackRect()   { RECT r = RowRect(6); return { 38, r.top + 38, 370, r.top + 56 }; }
static RECT TimeoutTrackRect() { RECT r = RowRect(8); return { 38, r.top + 38, 370, r.top + 56 }; }
static RECT LangRuRect()    { RECT r = RowRect(9); return { 246, r.top + 15, 318, r.top + 43 }; }
static RECT LangEnRect()    { RECT r = RowRect(9); return { 326, r.top + 15, 378, r.top + 43 }; }
static RECT OkRect()        { return { 149, 724, 259, 760 }; }   // по центру, только OK

// --- Состояние ---
static HWND      s_hWnd = NULL;
static HINSTANCE s_hInst = NULL;
static Settings  s_settings;   // редактируемый черновик

static ULONG_PTR s_gdiToken = 0;
static BOOL      s_gdiStarted = FALSE;
static bool      s_dark = false;
static bool      s_capturing = false;
static Hit       s_captureTarget = Hit::None;
static Hit       sHover = Hit::None, sPress = Hit::None;
static bool      sTrackMouse = false;
static COLORREF  s_customColors[16] = {};

// --- Прототипы ---
static void  Paint(HDC dc);
static Hit   HitTest(POINT pt);
static void  ApplyChanges();
static LRESULT CALLBACK SettingsWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

// ===================================================================
// Тема (по системе)
// ===================================================================
static bool IsSystemDarkMode()
{
    DWORD v = 1, sz = sizeof(v);
    if (RegGetValueW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"AppsUseLightTheme", RRF_RT_REG_DWORD, NULL, &v, &sz) == ERROR_SUCCESS)
        return v == 0;
    return false;
}

struct Palette {
    Color bg, tile, tileHot, outline, outlineHot, title, body, accent,
          switchOff, trackBase, thumbLine, thumbLineHot, dim, border;
};

static Palette MakePalette(bool dark)
{
    if (dark) {
        return {
            Color(255, 28, 28, 30),   Color(255, 40, 40, 42),   Color(255, 47, 47, 51),
            Color(255, 70, 70, 76),   Color(255, 96, 150, 210), Color(255, 235, 235, 235),
            Color(255, 205, 205, 208),Color(255, 0, 153, 255),  Color(255, 92, 98, 104),
            Color(255, 78, 84, 90),   Color(255, 150, 155, 160),Color(255, 90, 150, 215),
            Color(255, 132, 132, 138),Color(255, 90, 94, 100)
        };
    }
    return {
        Color(255, 247, 249, 252), Color(255, 255, 255, 255), Color(255, 237, 246, 255),
        Color(255, 218, 224, 231), Color(255, 119, 181, 239), Color(255, 31, 41, 55),
        Color(255, 51, 63, 78),    Color(255, 21, 119, 213),  Color(255, 205, 214, 224),
        Color(255, 210, 218, 227), Color(255, 143, 158, 174), Color(255, 75, 145, 213),
        Color(255, 150, 150, 150), Color(255, 168, 180, 194)
    };
}

static Color ColorFromCOLORREF(COLORREF c)
{
    return Color(255, GetRValue(c), GetGValue(c), GetBValue(c));
}

static void RoundPath(GraphicsPath& path, REAL x, REAL y, REAL w, REAL h, REAL r)
{
    path.Reset();
    path.AddArc(x,             y,             r * 2, r * 2, 180.0f, 90.0f);
    path.AddArc(x + w - r * 2, y,             r * 2, r * 2, 270.0f, 90.0f);
    path.AddArc(x + w - r * 2, y + h - r * 2, r * 2, r * 2,   0.0f, 90.0f);
    path.AddArc(x,             y + h - r * 2, r * 2, r * 2,  90.0f, 90.0f);
    path.CloseFigure();
}

// ===================================================================
// Работа с INI-файлом
// ===================================================================
void SettingsGetIniPath(TCHAR* buffer, int bufferSize)
{
    GetModuleFileNameW(NULL, buffer, bufferSize);
    TCHAR* slash = _tcsrchr(buffer, L'\\');
    if (slash) *(slash + 1) = L'\0';
    _tcscat_s(buffer, bufferSize, SETTINGS_INI_NAME);
}

void SettingsLoad(Settings* s)
{
    TCHAR iniPath[MAX_PATH];
    SettingsGetIniPath(iniPath, MAX_PATH);

    s->key = (UINT)GetPrivateProfileIntW(L"Settings", L"Key", VK_RMENU, iniPath);
    s->fixRuToEnKey = (UINT)GetPrivateProfileIntW(L"Settings", L"FixRuToEnKey", VK_F8, iniPath);
    s->fixEnToRuKey = (UINT)GetPrivateProfileIntW(L"Settings", L"FixEnToRuKey", VK_F9, iniPath);

    TCHAR modifiers[64];
    GetPrivateProfileStringW(L"Settings", L"Modifiers", L"Ctrl+Shift",
                             modifiers, _countof(modifiers), iniPath);
    s->modCtrl  = (_tcsstr(modifiers, L"Ctrl")  != NULL);
    s->modShift = (_tcsstr(modifiers, L"Shift") != NULL);
    s->modAlt   = (_tcsstr(modifiers, L"Alt")   != NULL);

    s->alwaysShowOsd      = GetPrivateProfileIntW(L"Settings", L"always_show_osd", 0, iniPath) != 0;
    s->osdAlpha           = (BYTE)GetPrivateProfileIntW(L"Settings", L"osd_alpha", 220, iniPath);
    s->showCaretIndicator = GetPrivateProfileIntW(L"Settings", L"caret_indicator", 1, iniPath) != 0;

    int timeoutSec = GetPrivateProfileIntW(L"Settings", L"indicator_timeout", 3, iniPath);
    s->indicatorTimeoutMs = timeoutSec > 0 ? (DWORD)timeoutSec * 1000 : 0;

    TCHAR colorStr[16];
    s->osdColor = RGB(127, 127, 127);
    if (GetPrivateProfileStringW(L"Settings", L"Color", L"7F7F7F",
                                 colorStr, _countof(colorStr), iniPath) > 0) {
        if (_tcslen(colorStr) == 6) {
            long colorVal = _tcstol(colorStr, NULL, 16);
            s->osdColor = RGB((int)((colorVal >> 16) & 0xFF),
                              (int)((colorVal >> 8) & 0xFF),
                              (int)(colorVal & 0xFF));
        }
    }

    // Язык интерфейса (по умолчанию - текущий язык менеджера, т.е. по системе)
    int lang = GetPrivateProfileIntW(L"Settings", L"Language",
                                     (int)GetAppLanguage(), iniPath);
    s->language = (lang == (int)AppLanguage::English) ? (int)AppLanguage::English
                                                      : (int)AppLanguage::Russian;
}

void SettingsSave(const Settings* s)
{
    TCHAR iniPath[MAX_PATH];
    SettingsGetIniPath(iniPath, MAX_PATH);

    WCHAR buf[32];

    _sntprintf_s(buf, _countof(buf), _TRUNCATE, L"%u", s->key);
    WritePrivateProfileStringW(L"Settings", L"Key", buf, iniPath);

    _sntprintf_s(buf, _countof(buf), _TRUNCATE, L"%u", s->fixRuToEnKey);
    WritePrivateProfileStringW(L"Settings", L"FixRuToEnKey", buf, iniPath);

    _sntprintf_s(buf, _countof(buf), _TRUNCATE, L"%u", s->fixEnToRuKey);
    WritePrivateProfileStringW(L"Settings", L"FixEnToRuKey", buf, iniPath);

    // Модификаторы: "Ctrl+Shift" / "Alt" и т.п.
    WCHAR mods[64] = { 0 };
    if (s->modCtrl)  _tcscat_s(mods, _countof(mods), L"Ctrl+");
    if (s->modShift) _tcscat_s(mods, _countof(mods), L"Shift+");
    if (s->modAlt)   _tcscat_s(mods, _countof(mods), L"Alt+");
    size_t len = _tcslen(mods);
    if (len > 0) mods[len - 1] = L'\0';
    WritePrivateProfileStringW(L"Settings", L"Modifiers", mods, iniPath);

    WritePrivateProfileStringW(L"Settings", L"always_show_osd",
                               s->alwaysShowOsd ? L"1" : L"0", iniPath);

    _sntprintf_s(buf, _countof(buf), _TRUNCATE, L"%u", s->osdAlpha);
    WritePrivateProfileStringW(L"Settings", L"osd_alpha", buf, iniPath);

    WritePrivateProfileStringW(L"Settings", L"caret_indicator",
                               s->showCaretIndicator ? L"1" : L"0", iniPath);

    _sntprintf_s(buf, _countof(buf), _TRUNCATE, L"%u", (UINT)(s->indicatorTimeoutMs / 1000));
    WritePrivateProfileStringW(L"Settings", L"indicator_timeout", buf, iniPath);

    WCHAR col[8];
    _sntprintf_s(col, _countof(col), _TRUNCATE, L"%02X%02X%02X",
                 GetRValue(s->osdColor), GetGValue(s->osdColor), GetBValue(s->osdColor));
    WritePrivateProfileStringW(L"Settings", L"Color", col, iniPath);

    _sntprintf_s(buf, _countof(buf), _TRUNCATE, L"%d", s->language);
    WritePrivateProfileStringW(L"Settings", L"Language", buf, iniPath);
}

// ===================================================================
// Форматирование значений
// ===================================================================
static void KeyNameText(UINT vk, WCHAR* out, int size)
{
    if (vk == 0) {
        std::wstring s = Lang(L"value_disabled");
        wcsncpy_s(out, size, s.c_str(), _TRUNCATE);
        return;
    }
    UINT scan = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    LONG lp = (LONG)(scan << 16);
    if (vk == VK_RMENU || vk == VK_RCONTROL || vk == VK_RSHIFT) lp |= (1L << 24);
    WCHAR name[48] = { 0 };
    if (scan != 0 && GetKeyNameTextW(lp, name, _countof(name)) > 0) {
        _sntprintf_s(out, size, _TRUNCATE, L"%s [%u]", name, vk);
    } else {
        _sntprintf_s(out, size, _TRUNCATE, L"VK %u", vk);
    }
}

// ===================================================================
// Отрисовка (GDI+)
// ===================================================================
static void DrawTile(Graphics& g, const Palette& P, RECT rc, bool hot)
{
    GraphicsPath tile;
    RoundPath(tile, (REAL)rc.left, (REAL)rc.top,
              (REAL)(rc.right - rc.left), (REAL)(rc.bottom - rc.top), 10.0f);
    g.FillPath(&SolidBrush(hot ? P.tileHot : P.tile), &tile);
    g.DrawPath(&Pen(hot ? P.outlineHot : P.outline, 1.0f), &tile);
}

static void DrawRowHeader(Graphics& g, const Palette& P, Font& rowFont,
                          RECT row, const WCHAR* caption, bool hot)
{
    DrawTile(g, P, row, hot);
    StringFormat nf;
    nf.SetAlignment(StringAlignmentNear);
    nf.SetLineAlignment(StringAlignmentCenter);
    g.DrawString(caption, -1, &rowFont,
        RectF((REAL)row.left + 10, (REAL)row.top, 200.0f,
              (REAL)(row.bottom - row.top)),
        &nf, &SolidBrush(P.title));
}

static void DrawSwitch(Graphics& g, const Palette& P, RECT row, bool enabled)
{
    const REAL swW = 34.0f, swH = 18.0f;
    REAL sx = (REAL)row.right - swW - 10.0f;
    REAL sy = (REAL)row.top + ((REAL)(row.bottom - row.top) - swH) / 2.0f;
    GraphicsPath p;
    RoundPath(p, sx, sy, swW, swH, swH / 2.0f);
    g.FillPath(&SolidBrush(enabled ? P.accent : P.switchOff), &p);
    g.DrawPath(&Pen(P.outline, 1.0f), &p);
    SolidBrush thumb(Color(255, 255, 255, 255));
    g.FillEllipse(&thumb, enabled ? sx + swW - 15.0f : sx + 3.0f, sy + 3.0f, 12.0f, 12.0f);
}

static void DrawKeyButton(Graphics& g, const Palette& P, RECT rc, UINT vk,
                          bool capturing, bool hot)
{
    GraphicsPath p;
    RoundPath(p, (REAL)rc.left, (REAL)rc.top,
              (REAL)(rc.right - rc.left), (REAL)(rc.bottom - rc.top), 7.0f);
    bool active = capturing || hot;
    g.FillPath(&SolidBrush(active ? P.tileHot : P.tile), &p);
    g.DrawPath(&Pen(capturing ? P.accent : (hot ? P.outlineHot : P.outline), 1.0f), &p);

    WCHAR txt[80];
    if (capturing) {
        std::wstring s = Lang(L"press_key");
        wcsncpy_s(txt, _countof(txt), s.c_str(), _TRUNCATE);
    } else {
        KeyNameText(vk, txt, _countof(txt));
    }
    StringFormat cf;
    cf.SetAlignment(StringAlignmentCenter);
    cf.SetLineAlignment(StringAlignmentCenter);
    g.DrawString(txt, -1, &Font(L"Segoe UI Semibold", 13.0f, FontStyleRegular, UnitPixel),
        RectF((REAL)rc.left, (REAL)rc.top,
              (REAL)(rc.right - rc.left), (REAL)(rc.bottom - rc.top)),
        &cf, &SolidBrush(capturing ? P.accent : P.body));
}

static void DrawColorButton(Graphics& g, const Palette& P, RECT rc, COLORREF color, bool hot)
{
    GraphicsPath p;
    RoundPath(p, (REAL)rc.left, (REAL)rc.top,
              (REAL)(rc.right - rc.left), (REAL)(rc.bottom - rc.top), 7.0f);
    g.FillPath(&SolidBrush(hot ? P.tileHot : P.tile), &p);
    g.DrawPath(&Pen(hot ? P.outlineHot : P.outline, 1.0f), &p);

    GraphicsPath sw;
    RoundPath(sw, (REAL)rc.left + 10.0f, (REAL)rc.top + 6.0f, 20.0f, 20.0f, 5.0f);
    g.FillPath(&SolidBrush(ColorFromCOLORREF(color)), &sw);
    g.DrawPath(&Pen(Color(120, 0, 0, 0), 1.0f), &sw);

    StringFormat cf;
    cf.SetAlignment(StringAlignmentCenter);
    cf.SetLineAlignment(StringAlignmentCenter);
    g.DrawString(Lang(L"choose").c_str(), -1, &Font(L"Segoe UI Semibold", 13.0f, FontStyleRegular, UnitPixel),
        RectF((REAL)rc.left + 36.0f, (REAL)rc.top,
              (REAL)(rc.right - rc.left) - 40.0f, (REAL)(rc.bottom - rc.top)),
        &cf, &SolidBrush(P.body));
}

static void DrawChip(Graphics& g, const Palette& P, RECT rc, const WCHAR* text,
                     bool selected, bool hot)
{
    GraphicsPath p;
    RoundPath(p, (REAL)rc.left, (REAL)rc.top,
              (REAL)(rc.right - rc.left), (REAL)(rc.bottom - rc.top), 7.0f);
    SolidBrush fill(selected ? P.accent : (hot ? P.tileHot : P.tile));
    Pen border(selected ? P.accent : (hot ? P.outlineHot : P.outline), 1.0f);
    SolidBrush tcol(selected ? Color(255, 255, 255, 255) : P.body);
    g.FillPath(&fill, &p);
    g.DrawPath(&border, &p);
    StringFormat cf;
    cf.SetAlignment(StringAlignmentCenter);
    cf.SetLineAlignment(StringAlignmentCenter);
    g.DrawString(text, -1, &Font(L"Segoe UI Semibold", 13.0f, FontStyleRegular, UnitPixel),
        RectF((REAL)rc.left, (REAL)rc.top,
              (REAL)(rc.right - rc.left), (REAL)(rc.bottom - rc.top)),
        &cf, &tcol);
}

static void DrawSlider(Graphics& g, const Palette& P, RECT track, float fraction, bool hot)
{
    REAL left = (REAL)track.left + 7.0f;
    REAL right = (REAL)track.right - 7.0f;
    REAL y = ((REAL)track.top + track.bottom) * 0.5f;
    REAL thumbX = left + (right - left) * fraction;

    Pen base(P.trackBase, 5.0f);
    base.SetStartCap(LineCapRound);
    base.SetEndCap(LineCapRound);
    g.DrawLine(&base, left, y, right, y);

    Pen value(P.accent, 5.0f);
    value.SetStartCap(LineCapRound);
    value.SetEndCap(LineCapRound);
    g.DrawLine(&value, left, y, thumbX, y);

    SolidBrush thumb(Color(255, 255, 255, 255));
    Pen thumbLine(hot ? P.thumbLineHot : P.thumbLine, 1.5f);
    g.FillEllipse(&thumb, thumbX - 7.0f, y - 7.0f, 14.0f, 14.0f);
    g.DrawEllipse(&thumbLine, thumbX - 7.0f, y - 7.0f, 14.0f, 14.0f);
}

static void DrawButton(Graphics& g, const Palette& P, RECT rc, const WCHAR* text, bool hot)
{
    GraphicsPath p;
    RoundPath(p, (REAL)rc.left, (REAL)rc.top,
              (REAL)(rc.right - rc.left), (REAL)(rc.bottom - rc.top), 8.0f);
    g.FillPath(&SolidBrush(hot ? P.tileHot : P.tile), &p);
    g.DrawPath(&Pen(hot ? P.outlineHot : P.outline, 1.0f), &p);
    StringFormat cf;
    cf.SetAlignment(StringAlignmentCenter);
    cf.SetLineAlignment(StringAlignmentCenter);
    g.DrawString(text, -1, &Font(L"Segoe UI Semibold", 14.0f, FontStyleRegular, UnitPixel),
        RectF((REAL)rc.left, (REAL)rc.top,
              (REAL)(rc.right - rc.left), (REAL)(rc.bottom - rc.top)),
        &cf, &SolidBrush(P.title));
}

static void Paint(HDC dc)
{
    Palette P = MakePalette(s_dark);
    Graphics g(dc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
    g.Clear(P.bg);

    GraphicsPath outer;
    RoundPath(outer, 1.0f, 1.0f, SETTINGS_W - 2.0f, SETTINGS_H - 2.0f, 8.0f);
    g.FillPath(&SolidBrush(P.bg), &outer);
    g.DrawPath(&Pen(P.border, 1.0f), &outer);

    Font titleFont(L"Segoe UI Semibold", 16.0f, FontStyleRegular, UnitPixel);
    Font rowFont(L"Segoe UI Semibold", 14.0f, FontStyleRegular, UnitPixel);
    Font dimFont(L"Segoe UI", 11.0f, FontStyleRegular, UnitPixel);

    StringFormat cf;
    cf.SetAlignment(StringAlignmentCenter);
    cf.SetLineAlignment(StringAlignmentCenter);

    // Заголовок
    g.DrawString(Lang(L"settings_title").c_str(), -1, &titleFont,
        RectF(20.0f, 16.0f, SETTINGS_W - 40.0f, 26.0f), &cf, &SolidBrush(P.title));

    // --- Строка 0: клавиша переключения ---
    {
        RECT row = RowRect(0);
        DrawRowHeader(g, P, rowFont, row, Lang(L"settings_switch_key").c_str(),
                      sHover == Hit::Key);
        DrawKeyButton(g, P, KeyBtnRect(), s_settings.key,
                      s_capturing && s_captureTarget == Hit::Key, sHover == Hit::Key);
    }

    // --- Строка 1: эмулируемая комбинация ---
    {
        RECT row = RowRect(1);
        DrawRowHeader(g, P, rowFont, row, Lang(L"settings_modifiers").c_str(),
                      sHover == Hit::ModCtrl || sHover == Hit::ModShift || sHover == Hit::ModAlt);
        DrawChip(g, P, ModChipRect(0), L"Ctrl",  s_settings.modCtrl,  sHover == Hit::ModCtrl);
        DrawChip(g, P, ModChipRect(1), L"Shift", s_settings.modShift, sHover == Hit::ModShift);
        DrawChip(g, P, ModChipRect(2), L"Alt",   s_settings.modAlt,   sHover == Hit::ModAlt);
    }

    // --- Строка 2: исправление RU->EN ---
    {
        RECT row = RowRect(2);
        DrawRowHeader(g, P, rowFont, row, Lang(L"settings_fix_ruen").c_str(),
                      sHover == Hit::FixRuEn);
        DrawKeyButton(g, P, FixRuEnBtnRect(), s_settings.fixRuToEnKey,
                      s_capturing && s_captureTarget == Hit::FixRuEn, sHover == Hit::FixRuEn);
    }

    // --- Строка 3: исправление EN->RU ---
    {
        RECT row = RowRect(3);
        DrawRowHeader(g, P, rowFont, row, Lang(L"settings_fix_enru").c_str(),
                      sHover == Hit::FixEnRu);
        DrawKeyButton(g, P, FixEnRuBtnRect(), s_settings.fixEnToRuKey,
                      s_capturing && s_captureTarget == Hit::FixEnRu, sHover == Hit::FixEnRu);
    }

    // --- Строка 4: всегда показывать OSD ---
    {
        RECT row = RowRect(4);
        DrawRowHeader(g, P, rowFont, row, Lang(L"settings_always_osd").c_str(),
                      sHover == Hit::AlwaysOsd);
        DrawSwitch(g, P, row, s_settings.alwaysShowOsd);
    }

    // --- Строка 5: цвет фона OSD ---
    {
        RECT row = RowRect(5);
        DrawRowHeader(g, P, rowFont, row, Lang(L"settings_osd_color").c_str(),
                      sHover == Hit::Color);
        DrawColorButton(g, P, ColorBtnRect(), s_settings.osdColor, sHover == Hit::Color);
    }

    // --- Строка 6: прозрачность OSD (слайдер) ---
    {
        RECT row = RowRect(6);
        DrawTile(g, P, row, sHover == Hit::Alpha || sPress == Hit::Alpha);
        WCHAR val[32];
        _sntprintf_s(val, _countof(val), _TRUNCATE, L"%u", s_settings.osdAlpha);
        StringFormat nf;
        nf.SetAlignment(StringAlignmentNear);
        nf.SetLineAlignment(StringAlignmentNear);
        g.DrawString(Lang(L"settings_osd_alpha").c_str(), -1, &rowFont,
            RectF((REAL)row.left + 10, (REAL)row.top + 4, 190.0f, 18.0f), &nf, &SolidBrush(P.title));
        StringFormat rf;
        rf.SetAlignment(StringAlignmentFar);
        rf.SetLineAlignment(StringAlignmentCenter);
        g.DrawString(val, -1, &rowFont,
            RectF((REAL)row.right - 60, (REAL)row.top + 4, 50.0f, 18.0f), &rf, &SolidBrush(P.title));
        DrawSlider(g, P, AlphaTrackRect(), s_settings.osdAlpha / 255.0f,
                   sHover == Hit::Alpha || sPress == Hit::Alpha);
    }

    // --- Строка 7: индикатор у курсора ---
    {
        RECT row = RowRect(7);
        DrawRowHeader(g, P, rowFont, row, Lang(L"settings_caret_indicator").c_str(),
                      sHover == Hit::Caret);
        DrawSwitch(g, P, row, s_settings.showCaretIndicator);
    }

    // --- Строка 8: таймаут индикатора (слайдер) ---
    {
        RECT row = RowRect(8);
        DrawTile(g, P, row, sHover == Hit::Timeout || sPress == Hit::Timeout);
        WCHAR val[32];
        UINT curSec = (UINT)(s_settings.indicatorTimeoutMs / 1000);
        _sntprintf_s(val, _countof(val), _TRUNCATE, L"%u %s",
                     curSec, Lang(L"value_sec_short").c_str());
        StringFormat nf;
        nf.SetAlignment(StringAlignmentNear);
        nf.SetLineAlignment(StringAlignmentNear);
        g.DrawString(Lang(L"settings_indicator_timeout").c_str(), -1, &rowFont,
            RectF((REAL)row.left + 10, (REAL)row.top + 4, 190.0f, 18.0f), &nf, &SolidBrush(P.title));
        StringFormat rf;
        rf.SetAlignment(StringAlignmentFar);
        rf.SetLineAlignment(StringAlignmentCenter);
        g.DrawString(val, -1, &rowFont,
            RectF((REAL)row.right - 60, (REAL)row.top + 4, 50.0f, 18.0f), &rf, &SolidBrush(P.title));
        float tfrac = (float)curSec / 10.0f;
        if (tfrac < 0.0f) tfrac = 0.0f;
        if (tfrac > 1.0f) tfrac = 1.0f;
        DrawSlider(g, P, TimeoutTrackRect(), tfrac,
                   sHover == Hit::Timeout || sPress == Hit::Timeout);
    }

    // --- Строка 9: язык интерфейса ---
    {
        RECT row = RowRect(9);
        std::wstring ruName = Lang(L"language_russian");
        std::wstring enName = Lang(L"language_english");
        DrawRowHeader(g, P, rowFont, row,
                      Lang(L"settings_language").c_str(),
                      sHover == Hit::LangRu || sHover == Hit::LangEn);
        DrawChip(g, P, LangRuRect(), ruName.c_str(),
                 s_settings.language == (int)AppLanguage::Russian, sHover == Hit::LangRu);
        DrawChip(g, P, LangEnRect(), enName.c_str(),
                 s_settings.language == (int)AppLanguage::English, sHover == Hit::LangEn);
    }

    // Подсказка и кнопка
    g.DrawString(Lang(L"right_click_hint").c_str(), -1, &dimFont,
        RectF(20.0f, 700.0f, SETTINGS_W - 40.0f, 18.0f), &cf, &SolidBrush(P.dim));
    DrawButton(g, P, OkRect(), Lang(L"ok").c_str(), sHover == Hit::Ok);
}

// ===================================================================
// Захват клавиши (вызывается из хука клавиатуры в kblswitch.cpp)
// ===================================================================
BOOL SettingsIsCapturing()
{
    return s_capturing;
}

void SettingsOnCapturedKey(UINT vk)
{
    if (!s_capturing) return;
    if (vk == VK_ESCAPE) {
        s_capturing = false;
        s_captureTarget = Hit::None;
        InvalidateRect(s_hWnd, NULL, FALSE);
        return;
    }
    Hit target = s_captureTarget;
    UINT value = (vk == VK_DELETE && target != Hit::Key) ? 0 : vk;
    if (target == Hit::Key)          s_settings.key = value;
    else if (target == Hit::FixRuEn) s_settings.fixRuToEnKey = value;
    else if (target == Hit::FixEnRu) s_settings.fixEnToRuKey = value;
    s_capturing = false;
    s_captureTarget = Hit::None;
    ApplyChanges();
}

// ===================================================================
// Проверка попадания
// ===================================================================
static Hit HitTest(POINT pt)
{
    for (int i = 0; i < 3; ++i)
        if (PtInRect(&ModChipRect(i), pt)) return (Hit)((int)Hit::ModCtrl + i);
    if (PtInRect(&KeyBtnRect(), pt))     return Hit::Key;
    if (PtInRect(&FixRuEnBtnRect(), pt)) return Hit::FixRuEn;
    if (PtInRect(&FixEnRuBtnRect(), pt)) return Hit::FixEnRu;
    if (PtInRect(&ColorBtnRect(), pt))   return Hit::Color;
    if (PtInRect(&LangRuRect(), pt))     return Hit::LangRu;
    if (PtInRect(&LangEnRect(), pt))     return Hit::LangEn;
    if (PtInRect(&OkRect(), pt))         return Hit::Ok;
    if (PtInRect(&RowRect(4), pt))       return Hit::AlwaysOsd;
    if (PtInRect(&RowRect(6), pt))       return Hit::Alpha;
    if (PtInRect(&RowRect(7), pt))       return Hit::Caret;
    if (PtInRect(&RowRect(8), pt))       return Hit::Timeout;
    return Hit::None;
}

static bool UpdateAlphaFromX(int x)
{
    RECT tr = AlphaTrackRect();
    int left = tr.left + 7, right = tr.right - 7;
    if (x < left) x = left;
    if (x > right) x = right;
    BYTE v = (BYTE)((x - left) * 255 / (right - left));
    if (v == s_settings.osdAlpha) return false;
    s_settings.osdAlpha = v;
    return true;
}

static bool UpdateTimeoutFromX(int x)
{
    RECT tr = TimeoutTrackRect();
    int left = tr.left + 7, right = tr.right - 7;
    if (x < left) x = left;
    if (x > right) x = right;
    UINT v = (UINT)((x - left) * 10 / (right - left));
    UINT cur = (UINT)(s_settings.indicatorTimeoutMs / 1000);
    if (v == cur) return false;
    s_settings.indicatorTimeoutMs = v * 1000;
    return true;
}

static void StartCapture(Hit target)
{
    s_capturing = true;
    s_captureTarget = target;
    InvalidateRect(s_hWnd, NULL, FALSE);
}

static void PickOsdColor()
{
    CHOOSECOLORW cc = { sizeof(cc) };
    cc.hwndOwner = s_hWnd;
    cc.lpCustColors = s_customColors;
    cc.rgbResult = s_settings.osdColor;
    cc.Flags = CC_FULLOPEN | CC_RGBINIT;
    if (ChooseColorW(&cc)) {
        s_settings.osdColor = cc.rgbResult;
        InvalidateRect(s_hWnd, NULL, FALSE);
    }
    if (IsWindow(s_hWnd)) {
        SetForegroundWindow(s_hWnd);
        SetFocus(s_hWnd);
    }
}

// Применение изменений немедленно: сохранить в INI и применить к приложению
static void ApplyChanges()
{
    SettingsSave(&s_settings);
    SettingsApplyToApp(&s_settings);
    InvalidateRect(s_hWnd, NULL, FALSE);
}

static void SaveAndClose(HWND hw)
{
    ApplyChanges();   // все изменения уже применены, но перестрахуемся
    DestroyWindow(hw);
}

// ===================================================================
// Обработчик сообщений
// ===================================================================
static LRESULT CALLBACK SettingsWndProc(HWND hw, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {

    case WM_CREATE: {
        // Скруглённые углы через DWM (SetWindowRgn НЕ используем - он отключает тень)
        DWORD corner = 2;   // DWM_WINDOW_CORNER_PREFERENCE_ROUND
        DwmSetWindowAttribute(hw, 33, &corner, sizeof(corner));
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_NCHITTEST: {
        LRESULT result = DefWindowProcW(hw, msg, wp, lp);
        if (result != HTCLIENT) return result;
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        ScreenToClient(hw, &pt);
        // Пустые места (заголовок, поля) - можно перетаскивать окно
        return HitTest(pt) == Hit::None ? HTCAPTION : HTCLIENT;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hw, &ps);
        RECT rect;
        GetClientRect(hw, &rect);
        HDC mem = CreateCompatibleDC(dc);
        HBITMAP bmp = mem ? CreateCompatibleBitmap(dc, rect.right, rect.bottom) : NULL;
        if (bmp) {
            HGDIOBJ old = SelectObject(mem, bmp);
            Paint(mem);
            BitBlt(dc, 0, 0, rect.right, rect.bottom, mem, 0, 0, SRCCOPY);
            SelectObject(mem, old);
            DeleteObject(bmp);
        } else {
            Paint(dc);
        }
        if (mem) DeleteDC(mem);
        EndPaint(hw, &ps);
        return 0;
    }

    case WM_MOUSEMOVE: {
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        if (GetCapture() == hw && sPress == Hit::Alpha) {
            if (UpdateAlphaFromX(pt.x)) ApplyChanges();
            sHover = Hit::Alpha;
            return 0;
        }
        if (GetCapture() == hw && sPress == Hit::Timeout) {
            if (UpdateTimeoutFromX(pt.x)) ApplyChanges();
            sHover = Hit::Timeout;
            return 0;
        }
        Hit hit = HitTest(pt);
        if (hit != sHover) {
            sHover = hit;
            InvalidateRect(hw, NULL, FALSE);
        }
        if (!sTrackMouse) {
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hw, 0 };
            TrackMouseEvent(&tme);
            sTrackMouse = true;
        }
        return 0;
    }

    case WM_MOUSELEAVE:
        sTrackMouse = false;
        if (sHover != Hit::None) {
            sHover = Hit::None;
            InvalidateRect(hw, NULL, FALSE);
        }
        return 0;

    case WM_SETCURSOR: {
        POINT pt;
        GetCursorPos(&pt);
        ScreenToClient(hw, &pt);
        if (HitTest(pt) != Hit::None) {
            SetCursor(LoadCursor(NULL, IDC_HAND));
            return TRUE;
        }
        break;
    }

    case WM_LBUTTONDOWN: {
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        sPress = HitTest(pt);
        if (sPress != Hit::None) {
            SetCapture(hw);
            if (sPress == Hit::Alpha && UpdateAlphaFromX(pt.x))
                ApplyChanges();
            else if (sPress == Hit::Timeout && UpdateTimeoutFromX(pt.x))
                ApplyChanges();
        }
        return 0;
    }

    case WM_LBUTTONUP: {
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        Hit pressed = sPress;
        sPress = Hit::None;
        if (GetCapture() == hw) ReleaseCapture();
        if (pressed != HitTest(pt)) return 0;

        switch (pressed) {
        case Hit::Key:
        case Hit::FixRuEn:
        case Hit::FixEnRu:
            StartCapture(pressed);
            break;
        case Hit::ModCtrl:  s_settings.modCtrl  = !s_settings.modCtrl;  break;
        case Hit::ModShift: s_settings.modShift = !s_settings.modShift; break;
        case Hit::ModAlt:   s_settings.modAlt   = !s_settings.modAlt;   break;
        case Hit::AlwaysOsd: s_settings.alwaysShowOsd = !s_settings.alwaysShowOsd; break;
        case Hit::Caret:     s_settings.showCaretIndicator = !s_settings.showCaretIndicator; break;
        case Hit::Color:     PickOsdColor(); break;
        case Hit::LangRu:
            if (s_settings.language != (int)AppLanguage::Russian) {
                s_settings.language = (int)AppLanguage::Russian;
                SetAppLanguage(AppLanguage::Russian);
            }
            break;
        case Hit::LangEn:
            if (s_settings.language != (int)AppLanguage::English) {
                s_settings.language = (int)AppLanguage::English;
                SetAppLanguage(AppLanguage::English);
            }
            break;
        case Hit::Ok:        SaveAndClose(hw); return 0;
        default: break;
        }
        ApplyChanges();
        return 0;
    }

    case WM_RBUTTONUP: {
        // Правый клик по кнопке клавиши исправления - отключить
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        Hit hit = HitTest(pt);
        if (hit == Hit::FixRuEn || hit == Hit::FixEnRu) {
            if (s_capturing) {
                s_capturing = false;
                s_captureTarget = Hit::None;
            }
            if (hit == Hit::FixRuEn) s_settings.fixRuToEnKey = 0;
            else                     s_settings.fixEnToRuKey = 0;
            ApplyChanges();
        }
        return 0;
    }

    case WM_CAPTURECHANGED:
        sPress = Hit::None;
        return 0;

    case WM_KEYDOWN:
        if (s_capturing) return 0;  // клавиши перехватывает хук
        if (wp == VK_ESCAPE) {
            DestroyWindow(hw);
        } else if (wp == VK_RETURN) {
            SaveAndClose(hw);
        } else if (wp == VK_LEFT || wp == VK_RIGHT) {
            int delta = (wp == VK_RIGHT) ? 1 : -1;
            if (sHover == Hit::Alpha) {
                int v = s_settings.osdAlpha + delta;
                if (v < 0) v = 0;
                if (v > 255) v = 255;
                if (v != s_settings.osdAlpha) {
                    s_settings.osdAlpha = (BYTE)v;
                    ApplyChanges();
                }
            } else if (sHover == Hit::Timeout) {
                int v = (int)(s_settings.indicatorTimeoutMs / 1000) + delta;
                if (v < 0) v = 0;
                if (v > 10) v = 10;
                if (v * 1000 != (int)s_settings.indicatorTimeoutMs) {
                    s_settings.indicatorTimeoutMs = (DWORD)v * 1000;
                    ApplyChanges();
                }
            }
        } else if (wp == VK_SPACE) {
            if (sHover == Hit::AlwaysOsd)
                s_settings.alwaysShowOsd = !s_settings.alwaysShowOsd;
            else if (sHover == Hit::Caret)
                s_settings.showCaretIndicator = !s_settings.showCaretIndicator;
            else
                return 0;
            ApplyChanges();
        }
        return 0;

    case WM_CLOSE:
        DestroyWindow(hw);
        return 0;

    case WM_NCDESTROY:
        sHover = Hit::None;
        sPress = Hit::None;
        sTrackMouse = false;
        s_capturing = false;
        s_captureTarget = Hit::None;
        s_hWnd = NULL;
        return 0;

    default:
        return DefWindowProcW(hw, msg, wp, lp);
    }
    return 0;
}

// ===================================================================
// Публичный интерфейс
// ===================================================================
BOOL SettingsDialogIsOpen()
{
    return s_hWnd != NULL && IsWindow(s_hWnd);
}

HWND SettingsGetHwnd()
{
    return s_hWnd;
}

void SettingsShowDialog(HWND owner, HINSTANCE hInstance)
{
    if (SettingsDialogIsOpen()) {
        SetForegroundWindow(s_hWnd);
        return;
    }

    if (!s_gdiStarted) {
        GdiplusStartupInput input;
        if (GdiplusStartup(&s_gdiToken, &input, NULL) == Ok)
            s_gdiStarted = TRUE;
    }

    s_dark = IsSystemDarkMode();
    s_hInst = hInstance;
    SettingsLoad(&s_settings);
    s_capturing = false;
    s_captureTarget = Hit::None;

    WNDCLASSW wc = {};
    wc.style = CS_DROPSHADOW;   // тень под окном
    wc.lpfnWndProc = SettingsWndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = SETTINGS_CLASS;
    RegisterClassW(&wc);

    // Центрируем на рабочей области основного монитора
    RECT work;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    int x = work.left + ((work.right - work.left) - SETTINGS_W) / 2;
    int y = work.top + ((work.bottom - work.top) - SETTINGS_H) / 2;

    s_hWnd = CreateWindowExW(WS_EX_TOOLWINDOW, SETTINGS_CLASS, L"",
        WS_POPUP, x, y, SETTINGS_W, SETTINGS_H,
        owner, NULL, hInstance, NULL);
    if (!s_hWnd) return;

    ShowWindow(s_hWnd, SW_SHOWNORMAL);
    UpdateWindow(s_hWnd);
    SetForegroundWindow(s_hWnd);
    SetFocus(s_hWnd);

    // Модальный цикл (низкоуровневый хук продолжает работать - он
    // обрабатывается в цикле сообщений потока)
    MSG msg;
    while (IsWindow(s_hWnd) && GetMessage(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    s_hWnd = NULL;

    if (s_gdiStarted) {
        GdiplusShutdown(s_gdiToken);
        s_gdiStarted = FALSE;
    }
}
