#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace kiotty::spec::implementation {

struct Section {
    std::string heading;
    std::string body;
};

std::vector<Section> splitSections(std::string_view document, unsigned level);

size_t countListItemsUnderHeading(std::string_view document, const std::vector<std::string> &keywords);

std::vector<std::string> headings(std::string_view document);

}
