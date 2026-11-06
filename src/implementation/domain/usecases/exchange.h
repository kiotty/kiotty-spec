#pragma once

#include "implementation/domain/usecases/checker.h"
#include "implementation/context/context_builder.h"
#include "implementation/data/datasource/database.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace kiotty::spec::implementation {

struct ImportReport {
    size_t rows = 0;
    std::vector<std::string> conflicts;
};

class Exchange {
public:
    Exchange(Database &db, ItemStore &items, LinkStore &links, Checker &checker, ContextBuilder &context)
        : db_(db), items_(items), links_(links), checker_(checker), context_(context) {}

    std::string exportMarkdown(KiottySpecItemKind kind);
    std::string traceReport();
    void exportJsonl(const std::filesystem::path &dir);
    ImportReport importJsonl(const std::filesystem::path &dir, bool merge);
    std::vector<std::string> importMarkdown(KiottySpecItemKind kind, std::string_view markdown);
    std::string queryReadonly(std::string_view sql);

private:
    Database &db_;
    ItemStore &items_;
    LinkStore &links_;
    Checker &checker_;
    ContextBuilder &context_;
};

}
