// lng_manager.cpp - менеджер языков KBLSWITCH.
// Встроенный русский словарь используется как фолбэк: загрузка каждого языка
// начинается с него, затем поверх накладываются строки из language\rus.lng
// или language\eng.lng. Если файла нет или в нём нет ни одной строки
// (пустой/повреждённый) - остаётся русский (или происходит фолбэк выше).

#include "lng_manager.h"

#include <windows.h>
#include <fstream>
#include <iterator>
#include <map>

namespace {

std::map<std::wstring, std::wstring> BuiltInRussianStrings()
{
    return {
        {L"app_name", L"Переключатель раскладки"},
        {L"menu_settings", L"Настройки..."},
        {L"menu_about", L"О программе"},
        {L"menu_exit", L"Выход"},
        {L"about_title", L"О программе kblswitch"},
        {L"app_version", L"Версия 2.0.0.0"},
        {L"about_text",
            L"KBLSWITCH - переключатель раскладки клавиатуры с визуальным уведомлением.\n\n"
            L"Возможности:\n"
            L"• Переключение раскладки одной клавишей (например, «Правый Alt») с эмуляцией "
            L"системной комбинации (например, Ctrl+Shift);\n"
            L"• Исправление раскладки последнего слова: F8 - русская→английская, F9 - английская→русская;\n"
            L"• OSD-индикатор текущего языка по центру экрана (цвет, прозрачность, режим "
            L"«всегда на экране» с перетаскиванием);\n"
            L"• Индикатор раскладки у курсора ввода (или у указателя мыши);\n"
            L"• Современное окно настроек с мгновенным применением изменений;\n"
            L"• Два языка интерфейса: русский и английский.\n\n"
            L"Программа работает в системном трее; контекстное меню - Настройки • О программе • Выход."},
        {L"main_subtitle", L"Переключение раскладки клавиатуры одной клавишей"},
        {L"main_hint",
            L"Правая кнопка мыши - контекстное меню:\n"
            L"Настройки  \u2022  О программе  \u2022  Выход"},
        {L"error_title", L"Ошибка kblswitch"},
        {L"error_register_class", L"Не удалось зарегистрировать класс главного окна."},
        {L"error_register_osd", L"Не удалось зарегистрировать класс OSD-окна."},
        {L"error_register_indicator",
            L"Не удалось зарегистрировать класс окна-индикатора раскладки."},
        {L"error_create_window", L"Не удалось создать главное окно."},
        {L"error_keyboard_hook", L"Не удалось установить низкоуровневый хук клавиатуры."},
        {L"error_create_event", L"Не удалось создать объект синхронизации."},
        {L"settings_title", L"Настройки KBLSWITCH"},
        {L"settings_switch_key", L"Клавиша переключения"},
        {L"settings_modifiers", L"Эмулируемая комбинация"},
        {L"settings_fix_ruen", L"Русская \u2192 Английская"},
        {L"settings_fix_enru", L"Английская \u2192 Русская"},
        {L"settings_always_osd", L"Всегда показывать OSD"},
        {L"settings_osd_color", L"Цвет фона OSD"},
        {L"settings_osd_alpha", L"Прозрачность OSD"},
        {L"settings_caret_indicator", L"Индикатор у курсора ввода"},
        {L"settings_indicator_timeout", L"Таймаут индикатора"},
        {L"settings_language", L"Язык"},
        {L"current_prefix", L"текущее:"},
        {L"value_on", L"вкл"},
        {L"value_off", L"выкл"},
        {L"value_disabled", L"- (отключено)"},
        {L"value_sec", L"сек"},
        {L"value_sec_short", L"с"},
        {L"value_no_close", L"0 (не закрывать)"},
        {L"press_key", L"Нажмите клавишу..."},
        {L"choose", L"Выбрать"},
        {L"ok", L"OK"},
        {L"cancel", L"Отмена"},
        {L"right_click_hint", L"Правый клик по кнопке клавиши - отключить"},
        {L"language_russian", L"Русский"},
        {L"language_english", L"English"}
    };
}

std::map<std::wstring, std::wstring> sStrings = BuiltInRussianStrings();
AppLanguage sLanguage = AppLanguage::Russian;

std::wstring ExeDir()
{
    WCHAR path[MAX_PATH];
    GetModuleFileNameW(NULL, path, MAX_PATH);
    std::wstring full(path);
    size_t slash = full.find_last_of(L"\\/");
    return slash == std::wstring::npos ? full : full.substr(0, slash + 1);
}

std::wstring Utf8ToWide(const std::string& text)
{
    if (text.empty()) return {};
    int count = MultiByteToWideChar(CP_UTF8, 0, text.data(),
                                    static_cast<int>(text.size()), NULL, 0);
    if (count <= 0) return {};
    std::wstring result(count, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                        &result[0], count);
    return result;
}

void Trim(std::wstring& text)
{
    size_t first = text.find_first_not_of(L" \t\r");
    if (first == std::wstring::npos) { text.clear(); return; }
    size_t last = text.find_last_not_of(L" \t\r");
    text = text.substr(first, last - first + 1);
}

std::wstring Unescape(const std::wstring& text)
{
    std::wstring result;
    result.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] != L'\\' || i + 1 >= text.size()) {
            result += text[i];
            continue;
        }
        wchar_t next = text[++i];
        if (next == L'n') result += L'\n';
        else if (next == L'r') result += L'\r';
        else if (next == L't') result += L'\t';
        else result += next;
    }
    return result;
}

// Возвращает количество разобранных строк (0 - файла нет или он пуст/повреждён)
size_t LoadFile(const std::wstring& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) return 0;

    std::string raw((std::istreambuf_iterator<char>(input)),
                    std::istreambuf_iterator<char>());
    if (raw.size() >= 3 && static_cast<unsigned char>(raw[0]) == 0xEF
        && static_cast<unsigned char>(raw[1]) == 0xBB
        && static_cast<unsigned char>(raw[2]) == 0xBF)
        raw.erase(0, 3);

    std::wstring content = Utf8ToWide(raw);
    size_t parsed = 0;
    size_t start = 0;
    while (start <= content.size()) {
        size_t end = content.find(L'\n', start);
        if (end == std::wstring::npos) end = content.size();
        std::wstring line = content.substr(start, end - start);
        Trim(line);
        if (!line.empty() && line[0] != L'#' && line[0] != L';') {
            size_t equal = line.find(L'=');
            if (equal != std::wstring::npos) {
                std::wstring key = line.substr(0, equal);
                std::wstring value = line.substr(equal + 1);
                Trim(key);
                Trim(value);
                if (!key.empty()) { sStrings[key] = Unescape(value); ++parsed; }
            }
        }
        if (end == content.size()) break;
        start = end + 1;
    }
    return parsed;
}

// Загрузка языка: начинаем со встроенного русского словаря, затем
// накладываем файл .lng. Если файла нет или в нём нет ни одной строки
// (пустой/повреждённый) - загрузка считается неудачной, остаётся русский.
bool LoadLanguage(AppLanguage language)
{
    sStrings = BuiltInRussianStrings();
    std::wstring file = (language == AppLanguage::Russian) ? L"rus.lng" : L"eng.lng";
    size_t parsed = LoadFile(ExeDir() + L"language\\" + file);
    if (parsed == 0) {
        if (language == AppLanguage::Russian) {
            sLanguage = AppLanguage::Russian;
            return true;
        }
        return false;
    }
    sLanguage = language;
    return true;
}

} // namespace

bool InitLanguageManager()
{
    bool russian = PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_RUSSIAN;
    return SetAppLanguage(russian ? AppLanguage::Russian : AppLanguage::English);
}

bool SetAppLanguage(AppLanguage language)
{
    std::map<std::wstring, std::wstring> previousStrings = sStrings;
    AppLanguage previousLanguage = sLanguage;
    if (LoadLanguage(language)) return true;
    // Фолбэк: запрошенный язык не загрузился - переключаемся на русский
    if (language != AppLanguage::Russian
        && LoadLanguage(AppLanguage::Russian)) return true;
    sStrings = std::move(previousStrings);
    sLanguage = previousLanguage;
    return false;
}

AppLanguage GetAppLanguage()
{
    return sLanguage;
}

std::wstring Lang(const wchar_t* key)
{
    auto found = sStrings.find(key ? key : L"");
    if (found != sStrings.end()) return found->second;
    return std::wstring(L"[") + (key ? key : L"") + L"]";
}
