// settings_apply.cpp - загрузка настроек из INI и применение их
// к работающему приложению (вызывается из главного модуля и окна настроек).

#include "kblswitch.h"

// Текущий язык интерфейса (используется только в этом модуле)
static AppLanguage g_language = AppLanguage::Russian;

// Загрузка настроек из INI-файла
void LoadSettingsFromIni() {
    Settings s;
    SettingsLoad(&s);

    g_key = s.key;
    g_modCtrl = s.modCtrl;
    g_modShift = s.modShift;
    g_modAlt = s.modAlt;
    g_fixRuToEnKey = s.fixRuToEnKey;
    g_fixEnToRuKey = s.fixEnToRuKey;
    g_alwaysShowOsd = s.alwaysShowOsd;
    g_osdConfigAlpha = s.osdAlpha;
    g_showCaretIndicator = s.showCaretIndicator;
    g_indicatorTimeoutMs = s.indicatorTimeoutMs;
    g_osdColor = s.osdColor;

    // Язык интерфейса (из INI; если ключа нет - остаётся язык по системе)
    g_language = (AppLanguage)s.language;
    SetAppLanguage(g_language);
}

// Применение настроек из окна настроек к работающему приложению
void SettingsApplyToApp(const Settings* s) {
    g_key = s->key;
    g_modCtrl = s->modCtrl;
    g_modShift = s->modShift;
    g_modAlt = s->modAlt;
    g_fixRuToEnKey = s->fixRuToEnKey;
    g_fixEnToRuKey = s->fixEnToRuKey;
    g_alwaysShowOsd = s->alwaysShowOsd;
    g_osdConfigAlpha = s->osdAlpha;
    g_showCaretIndicator = s->showCaretIndicator;
    g_indicatorTimeoutMs = s->indicatorTimeoutMs;
    g_osdColor = s->osdColor;

    // Смена языка интерфейса: перестраиваем меню и подсказку трея
    if (g_language != (AppLanguage)s->language) {
        g_language = (AppLanguage)s->language;
        SetAppLanguage(g_language);
        BuildContextMenu();
        UpdateTrayTip();
        InvalidateRect(g_hWnd, NULL, TRUE);
    }

    // Применяем видимые изменения
    if (g_alwaysShowOsd) {
        ShowOsdWindow(GetModuleHandle(NULL));
    } else if (g_hOsdWnd) {
        ShowWindow(g_hOsdWnd, SW_HIDE);
    }

    // Немедленно обновляем прозрачность и цвет видимых окон
    if (g_hOsdWnd && IsWindowVisible(g_hOsdWnd)) {
        SetLayeredWindowAttributes(g_hOsdWnd, 0, g_osdConfigAlpha, LWA_ALPHA);
        InvalidateRect(g_hOsdWnd, NULL, TRUE);
    }
    if (g_hLayoutIndWnd && IsWindowVisible(g_hLayoutIndWnd)) {
        SetLayeredWindowAttributes(g_hLayoutIndWnd, 0, g_osdConfigAlpha, LWA_ALPHA);
        InvalidateRect(g_hLayoutIndWnd, NULL, TRUE);
    }

    if (g_showCaretIndicator) {
        UpdateLayoutIndicator();
    } else {
        HideLayoutIndicator();
    }
}
