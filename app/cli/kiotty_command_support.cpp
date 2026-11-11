#include "kiotty_commands.h"

#include <iostream>

namespace kiotty::cli {

namespace {

constexpr MessageKey STEP_NAME_KEYS[KIOTTY_SPEC_STEP_COUNT] = {
    MessageKey::STEP_CHANGE_REQUEST,       MessageKey::STEP_REQUIREMENT,
    MessageKey::STEP_EXTERNAL_INTERFACE,   MessageKey::STEP_DESIGN,
    MessageKey::STEP_UNIT_TEST_PLAN,       MessageKey::STEP_DEVELOPMENT_PLAN,
    MessageKey::STEP_IMPLEMENTATION,       MessageKey::STEP_SYSTEM_TEST_PLAN,
    MessageKey::STEP_SYSTEM_TEST_RUN,      MessageKey::STEP_CODE_INSPECTION,
    MessageKey::STEP_END_TO_END_TEST_PLAN, MessageKey::STEP_END_TO_END_TEST_RUN
};

}

int check(Session& session, KiottySpecStatus status)
{
    return status == KIOTTY_SPEC_STATUS_SUCCESS ? 0 : fail(session, status);
}

const std::string* positional(Session& session, size_t index)
{
    const std::vector<std::string>& values = session.args.positional;
    const bool is_present = index < values.size();
    return is_present ? &values.at(index) : nullptr;
}

std::string stepName(uint32_t step)
{
    for (uint32_t index = 0; index < KIOTTY_SPEC_STEP_COUNT; ++index) {
        const bool is_step = index + 1 == step;
        if (is_step) {
            const std::string name(findMessage(STEP_NAME_KEYS[index]));
            return std::to_string(step) + " " + name;
        }
    }
    return "-";
}

BodyOption bodyOption(Session& session, const char* text_name, const char* file_name)
{
    const std::optional<std::string> text = session.args.option(text_name);
    if (text) { return BodyOption{text, false}; }
    const std::optional<std::string> file = session.args.option(file_name);
    if (file) {
        const auto content = readFileText(*file);
        const bool is_unreadable = !content.has_value();
        if (is_unreadable) {
            std::cerr << "error: " << formatMessage(MessageKey::FILE_UNREADABLE, {*file}) << "\n";
        }
        return BodyOption{content, is_unreadable};
    }
    return BodyOption{};
}

}
