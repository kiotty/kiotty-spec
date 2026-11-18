#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace kiotty::viewer {

enum class JsonScope { OBJECT, ARRAY };

class JsonWriter {
public:
    explicit JsonWriter(std::string& out) : _out(out) {}

    void open(JsonScope scope);
    void close(JsonScope scope);
    void writeKey(std::string_view name);
    void writeString(std::string_view text);
    void writeNumber(uint64_t value);
    void writeLiteral(std::string_view raw);

private:
    void separate();
    void quote(std::string_view text);
    void escapeControl(unsigned char byte);

    std::string& _out;
    std::vector<bool> _is_first {};
    bool _is_value_pending = false;
};

inline void JsonWriter::open(JsonScope scope)
{
    separate();
    const bool is_object = scope == JsonScope::OBJECT;
    _out += is_object ? '{' : '[';
    _is_first.push_back(true);
}

inline void JsonWriter::close(JsonScope scope)
{
    const bool is_object = scope == JsonScope::OBJECT;
    _out += is_object ? '}' : ']';
    _is_first.pop_back();
}

inline void JsonWriter::writeKey(std::string_view name)
{
    separate();
    quote(name);
    _out += ':';
    _is_value_pending = true;
}

inline void JsonWriter::writeString(std::string_view text)
{
    separate();
    quote(text);
}

inline void JsonWriter::writeNumber(uint64_t value)
{
    separate();
    _out += std::to_string(value);
}

inline void JsonWriter::writeLiteral(std::string_view raw)
{
    separate();
    _out += raw;
}

inline void JsonWriter::separate()
{
    const bool is_pending = _is_value_pending;
    if (is_pending) {
        _is_value_pending = false;
        return;
    }
    const bool is_top = _is_first.empty();
    if (is_top) { return; }
    const bool is_first = _is_first.back();
    if (is_first) {
        _is_first.back() = false;
        return;
    }
    _out += ',';
}

inline void JsonWriter::quote(std::string_view text)
{
    constexpr unsigned char FIRST_PRINTABLE = 0x20;
    _out += '"';
    for (const char letter : text) {
        const unsigned char byte = static_cast<unsigned char>(letter);
        if (letter == '"') { _out += "\\\""; }
        else if (letter == '\\') { _out += "\\\\"; }
        else if (letter == '\n') { _out += "\\n"; }
        else if (letter == '\r') { _out += "\\r"; }
        else if (letter == '\t') { _out += "\\t"; }
        else if (byte < FIRST_PRINTABLE) { escapeControl(byte); }
        else { _out += letter; }
    }
    _out += '"';
}

inline void JsonWriter::escapeControl(unsigned char byte)
{
    constexpr std::string_view HEX = "0123456789abcdef";
    constexpr unsigned int HIGH_SHIFT = 4;
    constexpr unsigned int LOW_MASK = 0x0F;
    _out += "\\u00";
    _out += HEX.at(static_cast<size_t>(byte >> HIGH_SHIFT));
    _out += HEX.at(static_cast<size_t>(byte & LOW_MASK));
}

}
