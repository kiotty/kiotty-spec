#include "implementation/domain/usecases/checker/kiotty_acceptance_criteria.h"

#include "implementation/core/kiotty_text_view.h"
#include "implementation/domain/usecases/kiotty_model_catalog.h"

#include <string_view>

namespace kiotty::spec::implementation {

namespace {

constexpr size_t MAX_HEADING_LEVEL = 6;
constexpr size_t ACCEPTANCE_WORD_LENGTH = 10;
constexpr size_t NUMBERED_MARKER_LENGTH = 2;
constexpr std::string_view HEADING_SPACE = " ";

size_t measureHeadingLevel(std::string_view line);
size_t updateActiveLevel(size_t active_level, size_t level, std::string_view line);
bool startsWithKeyword(std::string_view text);
bool isAcceptanceWord(std::string_view candidate);
bool isListLine(std::string_view line);

}

size_t countAcceptanceCriteria(Span<const char> body)
{
    const std::string_view text = viewOf(body);
    size_t count = 0;
    size_t active_level = 0;
    size_t start = 0;
    const size_t text_size = text.size();
    while (start <= text_size) {
        size_t end = text.find('\n', start);
        if (end == std::string_view::npos) { end = text_size; }
        std::string_view line = text.substr(start, end - start);
        const bool has_carriage_return = !line.empty() && line.back() == '\r';
        if (has_carriage_return) { line.remove_suffix(1); }
        const size_t level = measureHeadingLevel(line);
        const bool is_heading = level > 0;
        if (is_heading) { active_level = updateActiveLevel(active_level, level, line); }
        const bool is_criterion = !is_heading && active_level != 0 && isListLine(line);
        if (is_criterion) { ++count; }
        if (end == text_size) { break; }
        start = end + 1;
    }
    return count;
}

namespace {

size_t measureHeadingLevel(std::string_view line)
{
    const size_t line_size = line.size();
    size_t level = 0;
    while (level < line_size && line[level] == '#') { ++level; }
    const bool has_space = line.substr(level, HEADING_SPACE.size()) == HEADING_SPACE;
    const bool is_heading = level > 0 && level <= MAX_HEADING_LEVEL && has_space;
    return is_heading ? level : 0;
}

size_t updateActiveLevel(size_t active_level, size_t level, std::string_view line)
{
    const bool is_keyword = startsWithKeyword(line.substr(level));
    if (is_keyword) { return level; }
    const bool is_section_closed = active_level != 0 && level <= active_level;
    return is_section_closed ? 0 : active_level;
}

bool startsWithKeyword(std::string_view text)
{
    for (const std::string_view keyword : ACCEPTANCE_CRITERIA_KEYWORDS) {
        const size_t position = text.find(keyword);
        if (position != std::string_view::npos) { return true; }
    }
    const size_t text_size = text.size();
    for (size_t index = 0; index + ACCEPTANCE_WORD_LENGTH <= text_size; ++index) {
        const bool is_word = isAcceptanceWord(text.substr(index, ACCEPTANCE_WORD_LENGTH));
        if (is_word) { return true; }
    }
    return false;
}

bool isAcceptanceWord(std::string_view candidate)
{
    const char word[] = "acceptance";
    for (size_t offset = 0; offset < ACCEPTANCE_WORD_LENGTH; ++offset) {
        char letter = candidate[offset];
        if (letter >= 'A' && letter <= 'Z') { letter = static_cast<char>(letter - 'A' + 'a'); }
        if (letter != word[offset]) { return false; }
    }
    return true;
}

bool isListLine(std::string_view line)
{
    const size_t line_size = line.size();
    size_t index = 0;
    while (index < line_size && (line[index] == ' ' || line[index] == '\t')) { ++index; }
    if (index + 1 < line_size &&
        (line[index] == '-' || line[index] == '*' || line[index] == '+') &&
        line[index + 1] == ' ') {
        return true;
    }
    size_t digits = index;
    while (digits < line_size && line[digits] >= '0' && line[digits] <= '9') { ++digits; }
    const bool has_digits = digits > index;
    const std::string_view marker = line.substr(digits, NUMBERED_MARKER_LENGTH);
    const bool is_numbered_marker = marker == ". " || marker == ") ";
    return has_digits && is_numbered_marker;
}

}

}
