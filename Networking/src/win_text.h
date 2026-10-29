#pragma once
// INTERN, BARA WINDOWS — UTF-8 ↔ UTF-16 och systemets feltext. Inkluderas
// efter <windows.h> av sys_win.cpp, ping_win.cpp och http_winhttp.cpp.

#include <string>

namespace glaze::net::win {

inline std::wstring wide(const std::string& utf8)
{
    if (utf8.empty())
        return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
                                      static_cast<int>(utf8.size()), nullptr, 0);
    if (n <= 0)
        return {};
    std::wstring ut(static_cast<std::size_t>(n), L'\0');
    if (MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()),
                            ut.data(), n) != n)
        return {};
    return ut;
}

inline std::string utf8(const wchar_t* w, std::size_t len)
{
    if (len == 0)
        return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, static_cast<int>(len), nullptr, 0,
                                      nullptr, nullptr);
    if (n <= 0)
        return {};
    std::string ut(static_cast<std::size_t>(n), '\0');
    if (WideCharToMultiByte(CP_UTF8, 0, w, static_cast<int>(len), ut.data(), n, nullptr,
                            nullptr) != n)
        return {};
    return ut;
}

inline std::string utf8(const std::wstring& w) { return utf8(w.data(), w.size()); }

// FormatMessage-texten för ett felnummer, utan avslutande radbrytning.
// `modul` != nullptr = felet hör till den modulen (WinHTTP:s 12xxx-koder
// står i winhttp.dll, inte i systemets tabell). Texten är på OS:ets
// UI-språk — en svensk Windows svarar på svenska.
inline std::string systemText(DWORD kod, HMODULE modul = nullptr)
{
    wchar_t*    buf   = nullptr;
    const DWORD flagg = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_IGNORE_INSERTS |
                        (modul ? FORMAT_MESSAGE_FROM_HMODULE : FORMAT_MESSAGE_FROM_SYSTEM);
    const DWORD n = FormatMessageW(flagg, modul, kod, 0, reinterpret_cast<LPWSTR>(&buf), 0,
                                   nullptr);
    std::string t = (n && buf) ? utf8(buf, n) : std::string("okänt fel");
    if (buf)
        LocalFree(buf);
    while (!t.empty() && (t.back() == '\n' || t.back() == '\r' || t.back() == ' '))
        t.pop_back();
    return t;
}

} // namespace glaze::net::win
