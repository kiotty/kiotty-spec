#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#ifdef _WIN32
#include <windows.h>

inline std::string utf8FromWide(std::wstring_view wide);

inline std::filesystem::path pathFromUtf8(std::string_view text)
{
    const int length =
        MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring wide(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(),
                        length);
    return std::filesystem::path(wide);
}

inline std::string pathToUtf8(const std::filesystem::path &path)
{
    return utf8FromWide(path.generic_wstring());
}

inline std::string utf8FromWide(std::wstring_view wide)
{
    const int length = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                                           nullptr, 0, nullptr, nullptr);
    std::string utf8(static_cast<size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), utf8.data(), length,
                        nullptr, nullptr);
    return utf8;
}
#else
inline std::filesystem::path pathFromUtf8(std::string_view text)
{
    return std::filesystem::path(std::string(text));
}

inline std::string pathToUtf8(const std::filesystem::path &path)
{
    return path.generic_string();
}
#endif
