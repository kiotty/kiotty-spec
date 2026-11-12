#pragma once

#include "kiotty_path_utf8.h"

#include <cstdlib>
#include <string>

#ifdef _WIN32
inline std::string readEnvironment(const char* name)
{
    const std::wstring wide_name = pathFromUtf8(name).wstring();
    const DWORD capacity = GetEnvironmentVariableW(wide_name.c_str(), nullptr, 0);
    if (capacity == 0) { return ""; }
    std::wstring value(capacity, L'\0');
    const DWORD length = GetEnvironmentVariableW(wide_name.c_str(), value.data(), capacity);
    const bool is_complete = length > 0 && length < capacity;
    if (!is_complete) { return ""; }
    value.resize(length);
    return utf8FromWide(value);
}
#else
inline std::string readEnvironment(const char* name)
{
    const char* value = std::getenv(name);
    return value != nullptr ? value : "";
}
#endif
