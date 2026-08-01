// lng_manager.h - менеджер языков KBLSWITCH (русский / английский).
// Строки интерфейса хранятся в файлах language\rus.lng и language\eng.lng
// (UTF-8, формат "key=value"). Если файлы отсутствуют или повреждены -
// используется встроенный русский словарь (фолбэк).

#ifndef KBLSWITCH_LNG_MANAGER_H
#define KBLSWITCH_LNG_MANAGER_H

#include <string>

enum class AppLanguage : unsigned char {
    Russian = 1,
    English = 2
};

// Инициализация менеджера языков (по умолчанию - язык системы)
bool InitLanguageManager();

// Установка языка интерфейса. При отсутствии/повреждении .lng - фолбэк на русский.
bool SetAppLanguage(AppLanguage language);

AppLanguage GetAppLanguage();

// Возвращает строку по ключу (например, Lang(L"menu_exit")).
// Если ключ не найден - возвращает "[key]".
std::wstring Lang(const wchar_t* key);

#endif // KBLSWITCH_LNG_MANAGER_H
