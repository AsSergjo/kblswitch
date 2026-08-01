// input.cpp - исправление раскладки последнего слова (RU<->EN),
// отслеживание ввода и низкоуровневый хук клавиатуры KBLSWITCH.

#include "kblswitch.h"

// === Буферы отслеживаемого слова (используются только в этом модуле) ===
static WCHAR g_currentWord[WORD_BUFFER_SIZE] = {0};
static int   g_currentWordLen = 0;
static WCHAR g_lastWord[WORD_BUFFER_SIZE] = {0};
static int   g_lastWordLen = 0;
static WCHAR g_trailingText[TRAILING_BUFFER_SIZE] = {0};
static int   g_trailingTextLen = 0;

// Состояние физической комбинации Ctrl+Shift. В современных TSF-приложениях
// (в частности, в Блокноте Windows 11) системный переключатель меняет язык
// ввода, но GetKeyboardLayout() у потока окна может продолжать возвращать
// прежний HKL. Поэтому чистую комбинацию Ctrl+Shift отслеживаем также в хуке.
static BOOL g_physicalCtrlDown = FALSE;
static BOOL g_physicalShiftDown = FALSE;
static BOOL g_physicalAltDown = FALSE;
static BOOL g_physicalWinDown = FALSE;
static BOOL g_ctrlShiftChordSeen = FALSE;
static BOOL g_ctrlShiftChordBlocked = FALSE;
static BOOL g_ctrlShiftChordHandled = FALSE;

// === Исправление последнего слова RU<->EN ===
static const WCHAR EN_LOWER_MAP[] = L"`qwertyuiop[]asdfghjkl;'zxcvbnm,./";
static const WCHAR EN_UPPER_MAP[] = L"~QWERTYUIOP{}ASDFGHJKL:\"ZXCVBNM<>?";
static const WCHAR RU_LOWER_MAP[] = L"ёйцукенгшщзхъфывапролджэячсмитьбю.";
static const WCHAR RU_UPPER_MAP[] = L"ЁЙЦУКЕНГШЩЗХЪФЫВАПРОЛДЖЭЯЧСМИТЬБЮ,";

static BOOL FindMappedChar(WCHAR ch, const WCHAR* from, const WCHAR* to, WCHAR* mapped) {
    for (int i = 0; from[i] != L'\0' && to[i] != L'\0'; ++i) {
        if (from[i] == ch) {
            *mapped = to[i];
            return TRUE;
        }
    }
    return FALSE;
}

static BOOL MapCharRuToEn(WCHAR ch, WCHAR* mapped) {
    return FindMappedChar(ch, RU_LOWER_MAP, EN_LOWER_MAP, mapped) ||
           FindMappedChar(ch, RU_UPPER_MAP, EN_UPPER_MAP, mapped);
}

static BOOL MapCharEnToRu(WCHAR ch, WCHAR* mapped) {
    return FindMappedChar(ch, EN_LOWER_MAP, RU_LOWER_MAP, mapped) ||
           FindMappedChar(ch, EN_UPPER_MAP, RU_UPPER_MAP, mapped);
}

static BOOL IsConvertibleChar(WCHAR ch) {
    WCHAR unused;
    return MapCharRuToEn(ch, &unused) || MapCharEnToRu(ch, &unused);
}

static BOOL ConvertWordLayout(const WCHAR* source, int sourceLen,
                              WCHAR* target, int targetSize, BOOL ruToEn) {
    if (!source || !target || sourceLen <= 0 || targetSize <= sourceLen) return FALSE;

    for (int i = 0; i < sourceLen; ++i) {
        WCHAR mapped = 0;
        BOOL ok = ruToEn ? MapCharRuToEn(source[i], &mapped)
                         : MapCharEnToRu(source[i], &mapped);
        if (!ok) return FALSE;
        target[i] = mapped;
    }
    target[sourceLen] = L'\0';
    return TRUE;
}

static void ResetWordBuffers() {
    g_currentWordLen = 0;
    g_currentWord[0] = L'\0';
    g_lastWordLen = 0;
    g_lastWord[0] = L'\0';
    g_trailingTextLen = 0;
    g_trailingText[0] = L'\0';
}

static BOOL IsCtrlKey(UINT vkCode) {
    return vkCode == VK_CONTROL || vkCode == VK_LCONTROL || vkCode == VK_RCONTROL;
}

static BOOL IsShiftKey(UINT vkCode) {
    return vkCode == VK_SHIFT || vkCode == VK_LSHIFT || vkCode == VK_RSHIFT;
}

static BOOL IsAltKey(UINT vkCode) {
    return vkCode == VK_MENU || vkCode == VK_LMENU || vkCode == VK_RMENU;
}

static BOOL IsWinKey(UINT vkCode) {
    return vkCode == VK_LWIN || vkCode == VK_RWIN;
}

static BOOL UsesCtrlShiftLayoutShortcut() {
    // Отслеживаем именно ту комбинацию, которую приложение настроено
    // эмулировать. Это не даёт обычным сочетаниям Ctrl+Shift+<клавиша>
    // ошибочно менять текст OSD.
    return g_modCtrl && g_modShift && !g_modAlt &&
           !IsCtrlKey(g_key) && !IsShiftKey(g_key);
}

static void ShowPhysicalCtrlShiftResult() {
    if (!g_currentLang[0]) {
        GetLayoutName(g_currentLang, _countof(g_currentLang));
    }

    // KBLSWITCH рассчитан на пару EN/RU. Для TSF нельзя надёжно запросить
    // новое состояние чужого процесса, поэтому после подтверждённой системной
    // комбинации запоминаем противоположный язык.
    if (_tcsicmp(g_currentLang, L"EN") == 0) {
        _tcscpy_s(g_currentLang, _countof(g_currentLang), L"RU");
    } else if (_tcsicmp(g_currentLang, L"RU") == 0) {
        _tcscpy_s(g_currentLang, _countof(g_currentLang), L"EN");
    } else {
        // Для неизвестной раскладки оставляем HKL источником истины: штатный
        // таймер покажет OSD, если приложение действительно обновит HKL.
        return;
    }

    ResetWordBuffers();
    if (g_showCaretIndicator) {
        g_indicatorTypedChars = 0;
        g_indicatorShowTick = GetTickCount();
    }
    SetTimer(g_hWnd, OSD_SHOW_TIMER_ID, 100, NULL);
}

static void TrackPhysicalCtrlShift(WPARAM message, UINT vkCode) {
    if (!UsesCtrlShiftLayoutShortcut()) {
        g_physicalCtrlDown = FALSE;
        g_physicalShiftDown = FALSE;
        g_physicalAltDown = FALSE;
        g_physicalWinDown = FALSE;
        g_ctrlShiftChordSeen = FALSE;
        g_ctrlShiftChordBlocked = FALSE;
        g_ctrlShiftChordHandled = FALSE;
        return;
    }

    const BOOL keyDown = (message == WM_KEYDOWN || message == WM_SYSKEYDOWN);
    const BOOL keyUp = (message == WM_KEYUP || message == WM_SYSKEYUP);
    if (!keyDown && !keyUp) return;

    const BOOL isCtrl = IsCtrlKey(vkCode);
    const BOOL isShift = IsShiftKey(vkCode);
    const BOOL isAlt = IsAltKey(vkCode);
    const BOOL isWin = IsWinKey(vkCode);

    if (keyDown) {
        if (isCtrl) {
            g_physicalCtrlDown = TRUE;
        } else if (isShift) {
            g_physicalShiftDown = TRUE;
        } else if (isAlt) {
            g_physicalAltDown = TRUE;
        } else if (isWin) {
            g_physicalWinDown = TRUE;
        } else if (g_physicalCtrlDown || g_physicalShiftDown) {
            // Alt/Win и любая обычная клавиша превращают жест в прикладное
            // сочетание (например, Ctrl+Shift+S), OSD для него не показываем.
            g_ctrlShiftChordBlocked = TRUE;
        }

        if (g_physicalCtrlDown && g_physicalShiftDown &&
            !g_physicalAltDown && !g_physicalWinDown && !g_ctrlShiftChordBlocked) {
            g_ctrlShiftChordSeen = TRUE;
        }
        return;
    }

    // Windows завершает жест переключения при отпускании одного из
    // модификаторов. К этому моменту можно безопасно показать новый язык.
    if ((isCtrl || isShift) && g_ctrlShiftChordSeen &&
        !g_physicalAltDown && !g_physicalWinDown &&
        !g_ctrlShiftChordBlocked && !g_ctrlShiftChordHandled) {
        g_ctrlShiftChordHandled = TRUE;
        ShowPhysicalCtrlShiftResult();
    }

    if (isCtrl) g_physicalCtrlDown = FALSE;
    if (isShift) g_physicalShiftDown = FALSE;
    if (isAlt) g_physicalAltDown = FALSE;
    if (isWin) g_physicalWinDown = FALSE;

    if (!g_physicalCtrlDown && !g_physicalShiftDown) {
        g_ctrlShiftChordSeen = FALSE;
        g_ctrlShiftChordBlocked = FALSE;
        g_ctrlShiftChordHandled = FALSE;
    }
}

static void AppendCurrentWordChar(WCHAR ch) {
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

static void TrackSeparatorChar(WCHAR ch) {
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

static void TrackBackspace() {
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

static BOOL HasEditingModifier() {
    return (GetAsyncKeyState(VK_CONTROL) & 0x8000) ||
           (GetAsyncKeyState(VK_MENU) & 0x8000) ||
           (GetAsyncKeyState(VK_LWIN) & 0x8000) ||
           (GetAsyncKeyState(VK_RWIN) & 0x8000);
}

static HKL GetForegroundKeyboardLayout() {
    HWND fgWnd = GetForegroundWindow();
    if (!fgWnd) return GetKeyboardLayout(0);
    DWORD threadId = GetWindowThreadProcessId(fgWnd, NULL);
    return GetKeyboardLayout(threadId);
}

static BOOL TryGetTypedChar(KBDLLHOOKSTRUCT* ks, WCHAR* ch) {
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

static void TrackTypedKey(KBDLLHOOKSTRUCT* ks) {
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

static BOOL SendBackspaces(int count) {
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

static BOOL SendUnicodeText(const WCHAR* text, int len) {
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

// Полное переключение раскладки на целевой язык (targetRu):
// эмулируем настроенную системную комбинацию (Ctrl+Shift по умолчанию) через
// SendInput с паузами - именно она РЕАЛЬНО переключает язык ввода, в т.ч. в
// TSF/UWP-приложениях (Store Notepad). WM_INPUTLANGCHANGEREQUEST НЕ используем:
// он меняет только HKL (отображение), но не фактический язык ввода, и может
// сбивать уже выполненное переключение.
// Целевой язык запоминаем для OSD/индикатора (обход stale-HKL в TSF).
static void DoLayoutSwitch(BOOL targetRu) {
    INPUT down[3] = {};
    int n = 0;
    if (g_modCtrl)  { down[n].type = INPUT_KEYBOARD; down[n].ki.wVk = VK_CONTROL; ++n; }
    if (g_modShift) { down[n].type = INPUT_KEYBOARD; down[n].ki.wVk = VK_SHIFT;   ++n; }
    if (g_modAlt)   { down[n].type = INPUT_KEYBOARD; down[n].ki.wVk = VK_MENU;    ++n; }

    if (n > 0) {
        // Все модификаторы вниз, пауза, затем вверх в обратном порядке
        SendInput(n, down, sizeof(INPUT));
        Sleep(40);
        for (int i = n - 1; i >= 0; --i) {
            INPUT up = down[i];
            up.ki.dwFlags = KEYEVENTF_KEYUP;
            SendInput(1, &up, sizeof(INPUT));
            Sleep(20);
        }
    }

    // Целевой язык для OSD/индикатора (отслеживаемый, обход stale-HKL)
    _tcscpy_s(g_currentLang, _countof(g_currentLang), targetRu ? L"RU" : L"EN");
}

static BOOL CorrectLastWord(BOOL ruToEn) {
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

    // Переключаем ввод на язык результата: эмуляция системной комбинации +
    // синхронизация HKL (целевой язык запоминается в DoLayoutSwitch)
    DoLayoutSwitch(ruToEn ? FALSE : TRUE); // RU->EN: цель EN; EN->RU: цель RU

    // Показываем индикатор актуальной раскладки у каретки (или у мыши)
    if (g_showCaretIndicator) {
        g_indicatorTypedChars = 0;
        g_indicatorShowTick = GetTickCount();
    }

    SetTimer(g_hWnd, OSD_SHOW_TIMER_ID, 100, NULL);

    return TRUE;
}

// Низкоуровневый хук клавиатуры
LRESULT CALLBACK KbdHook(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        KBDLLHOOKSTRUCT* ks = (KBDLLHOOKSTRUCT*)lParam;
        if (!(ks->flags & LLKHF_INJECTED)) {
            TrackPhysicalCtrlShift(wParam, ks->vkCode);

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
                // Режим захвата клавиши в окне настроек: поглощаем клавишу
                // и передаём её в окно настроек
                if (SettingsIsCapturing()) {
                    SettingsOnCapturedKey(ks->vkCode);
                    return 1;
                }

                // Пока окно настроек открыто и активно - не вмешиваемся в ввод
                if (SettingsDialogIsActive()) {
                    return CallNextHookEx(g_khook, nCode, wParam, lParam);
                }

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

                    // Определяем текущую раскладку (по отслеживаемому языку -
                    // HKL может быть устаревшим в TSF-приложениях) и
                    // переключаемся на противоположную.
                    if (!g_currentLang[0]) {
                        GetLayoutName(g_currentLang, _countof(g_currentLang));
                    }

                    // EN -> RU, RU/неизвестно -> EN
                    DoLayoutSwitch(_tcsicmp(g_currentLang, L"EN") == 0);

                    // Показываем индикатор актуальной раскладки у каретки
                    // (или у указателя мыши), если индикатор включён
                    if (g_showCaretIndicator) {
                        g_indicatorTypedChars = 0;
                        g_indicatorShowTick = GetTickCount();
                    }

                    SetTimer(g_hWnd, OSD_SHOW_TIMER_ID, 100, NULL);

                    return 1;
                }

                // Считаем введённые символы, чтобы скрыть индикатор раскладки
                // после начала набора текста
                WCHAR typedChar = 0;
                if (TryGetTypedChar(ks, &typedChar) && typedChar >= 0x20 && typedChar != 0x7F) {
                    if (g_showCaretIndicator && g_indicatorTypedChars < INDICATOR_HIDE_AFTER_CHARS) {
                        g_indicatorTypedChars++;
                        if (g_indicatorTypedChars >= INDICATOR_HIDE_AFTER_CHARS) {
                            HideLayoutIndicator();
                        }
                    }
                }

                // Клавиши навигации перемещают каретку в новое место -
                // показываем индикатор заново
                switch (ks->vkCode) {
                    case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN:
                    case VK_HOME: case VK_END: case VK_PRIOR: case VK_NEXT:
                    case VK_TAB:
                        g_indicatorTypedChars = 0;
                        g_indicatorShowTick = GetTickCount();
                        break;
                    default:
                        break;
                }

                TrackTypedKey(ks);
            }
        }
    }
    return CallNextHookEx(g_khook, nCode, wParam, lParam);
}
