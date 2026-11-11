#include "kiotty_commands.h"

#include <iostream>

namespace kiotty::cli {

namespace {

const char* const STEP_NAMES[] = { "",
                                   "변경 요청 접수",
                                   "요구사항",
                                   "외부 인터페이스",
                                   "설계",
                                   "유닛 테스트 계획",
                                   "개발 계획",
                                   "구현",
                                   "시스템 테스트 계획",
                                   "시스템 테스트 실행",
                                   "코드 검사",
                                   "E2E 테스트 계획",
                                   "E2E 테스트 실행" };
}

int check(Session& session, KiottySpecStatus status)
{
    return status == KIOTTY_SPEC_STATUS_SUCCESS ? 0 : fail(session, status);
}

const std::string* positional(Session& session, size_t index)
{
    return index < session.args.positional.size() ? &session.args.positional[index] : nullptr;
}

std::string stepName(uint32_t step)
{
    if (step == 0 || step > KIOTTY_SPEC_STEP_COUNT) { return "-"; }
    return std::to_string(step) + " " + STEP_NAMES[step];
}

BodyOption bodyOption(Session& session, const char* text_name, const char* file_name)
{
    const std::optional<std::string> text = session.args.option(text_name);
    if (text) { return BodyOption{text, false}; }
    const std::optional<std::string> file = session.args.option(file_name);
    if (file) {
        const auto content = readFileText(*file);
        if (!content) { std::cerr << "error: 파일을 읽을 수 없음: " << *file << "\n"; }
        return BodyOption{content, !content};
    }
    return BodyOption{};
}

}
