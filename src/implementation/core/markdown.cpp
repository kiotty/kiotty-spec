#include "implementation/core/markdown.h"

#include "implementation/core/text.h"

#include <md4c.h>

#include <algorithm>
#include <functional>

namespace kiotty::core::markdown {

namespace {

// md4c 콜백을 C++ 람다로 받기 위한 얇은 어댑터.
struct Walker {
    std::function<void(MD_BLOCKTYPE, void *)> enterBlock;
    std::function<void(MD_BLOCKTYPE, void *)> leaveBlock;
    std::function<void(const MD_CHAR *, MD_SIZE)> text;
};

void walk(std::string_view document, Walker &walker) {
    MD_PARSER parser{};
    parser.abi_version = 0;
    parser.flags = MD_FLAG_TABLES | MD_FLAG_STRIKETHROUGH | MD_FLAG_TASKLISTS;
    parser.enter_block = [](MD_BLOCKTYPE type, void *detail, void *user) -> int {
        auto *self = static_cast<Walker *>(user);
        if (self->enterBlock) {
            self->enterBlock(type, detail);
        }
        return 0;
    };
    parser.leave_block = [](MD_BLOCKTYPE type, void *detail, void *user) -> int {
        auto *self = static_cast<Walker *>(user);
        if (self->leaveBlock) {
            self->leaveBlock(type, detail);
        }
        return 0;
    };
    parser.enter_span = [](MD_SPANTYPE, void *, void *) -> int { return 0; };
    parser.leave_span = [](MD_SPANTYPE, void *, void *) -> int { return 0; };
    parser.text = [](MD_TEXTTYPE, const MD_CHAR *text, MD_SIZE size, void *user) -> int {
        auto *self = static_cast<Walker *>(user);
        if (self->text) {
            self->text(text, size);
        }
        return 0;
    };
    md_parse(document.data(), static_cast<MD_SIZE>(document.size()), &parser, &walker);
}

unsigned headingLevel(MD_BLOCKTYPE type, void *detail) {
    if (type != MD_BLOCK_H || detail == nullptr) {
        return 0;
    }
    return static_cast<MD_BLOCK_H_DETAIL *>(detail)->level;
}

} // namespace

std::vector<Section> splitSections(std::string_view document, unsigned level) {
    struct Mark {
        std::string heading;
        size_t lineStart = 0;   // 제목 줄의 시작 오프셋
        size_t bodyStart = 0;   // 제목 다음 줄의 시작 오프셋
    };
    std::vector<Mark> marks;
    bool inHeading = false;
    bool sawText = false;
    size_t lastTextEnd = 0;

    Walker walker;
    walker.enterBlock = [&](MD_BLOCKTYPE type, void *detail) {
        if (headingLevel(type, detail) == level) {
            inHeading = true;
            sawText = false;
            marks.push_back({});
        }
    };
    walker.leaveBlock = [&](MD_BLOCKTYPE type, void *detail) {
        if (headingLevel(type, detail) == level && inHeading) {
            inHeading = false;
            const size_t lineEnd = document.find('\n', lastTextEnd);
            marks.back().bodyStart = lineEnd == std::string_view::npos ? document.size() : lineEnd + 1;
            if (!sawText) {
                marks.pop_back();
            }
        }
    };
    walker.text = [&](const MD_CHAR *text, MD_SIZE size) {
        if (!inHeading) {
            return;
        }
        const auto offset = static_cast<size_t>(text - document.data());
        if (offset > document.size()) {
            return; // 원문 밖을 가리키는 텍스트(엔티티 치환 등)는 위치 계산에 쓰지 않는다.
        }
        if (!sawText) {
            const size_t newline = document.rfind('\n', offset);
            marks.back().lineStart = newline == std::string_view::npos ? 0 : newline + 1;
            sawText = true;
        }
        marks.back().heading.append(text, size);
        lastTextEnd = offset + size;
    };
    walk(document, walker);

    std::vector<Section> sections;
    for (size_t i = 0; i < marks.size(); ++i) {
        const size_t end = i + 1 < marks.size() ? marks[i + 1].lineStart : document.size();
        const size_t start = std::min(marks[i].bodyStart, end);
        sections.push_back({std::string(trim(marks[i].heading)),
                            std::string(trim(document.substr(start, end - start)))});
    }
    return sections;
}

size_t countListItemsUnderHeading(std::string_view document, const std::vector<std::string> &keywords) {
    size_t count = 0;
    unsigned activeLevel = 0;      // 일치하는 제목의 수준. 0 이면 비활성
    unsigned currentHeading = 0;   // 지금 읽고 있는 제목의 수준
    std::string headingText;

    Walker walker;
    walker.enterBlock = [&](MD_BLOCKTYPE type, void *detail) {
        const unsigned level = headingLevel(type, detail);
        if (level != 0) {
            if (activeLevel != 0 && level <= activeLevel) {
                activeLevel = 0;
            }
            currentHeading = level;
            headingText.clear();
            return;
        }
        if (type == MD_BLOCK_LI && activeLevel != 0) {
            ++count;
        }
    };
    walker.leaveBlock = [&](MD_BLOCKTYPE type, void *detail) {
        const unsigned level = headingLevel(type, detail);
        if (level == 0) {
            return;
        }
        for (const auto &keyword : keywords) {
            if (containsInsensitive(headingText, keyword)) {
                activeLevel = level;
                break;
            }
        }
        currentHeading = 0;
    };
    walker.text = [&](const MD_CHAR *text, MD_SIZE size) {
        if (currentHeading != 0) {
            headingText.append(text, size);
        }
    };
    walk(document, walker);
    return count;
}

std::vector<std::string> headings(std::string_view document) {
    std::vector<std::string> result;
    bool inHeading = false;
    Walker walker;
    walker.enterBlock = [&](MD_BLOCKTYPE type, void *detail) {
        if (headingLevel(type, detail) != 0) {
            inHeading = true;
            result.emplace_back();
        }
    };
    walker.leaveBlock = [&](MD_BLOCKTYPE type, void *detail) {
        if (headingLevel(type, detail) != 0) {
            inHeading = false;
        }
    };
    walker.text = [&](const MD_CHAR *text, MD_SIZE size) {
        if (inHeading) {
            result.back().append(text, size);
        }
    };
    walk(document, walker);
    return result;
}

} // namespace kiotty::core::markdown
