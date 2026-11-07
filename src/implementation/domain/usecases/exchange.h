#pragma once

#include "implementation/core/failure.h"
#include "implementation/data/datasource/storage.h"
#include "implementation/data/repository/item_store.h"
#include "implementation/data/repository/link_graph.h"
#include "implementation/domain/usecases/checker.h"

#include <string>
#include <string_view>
#include <vector>

namespace kiotty::spec::implementation {

struct ImportReport {
    size_t rows = 0;
    std::vector<std::string> conflicts;
};

using ImportReportResult = Result<ImportReport, KiottySpecStatus>;

class Exchange {
public:
    Exchange(Storage &storage, ItemStore &items, LinkGraph &graph, Checker &checker, Failure &failure)
        : _storage(storage), _items(items), _graph(graph), _checker(checker), _failure(failure) {}

    TextResult traceReport();
    KiottySpecStatus exportJsonl(Span<const char> directory);
    ImportReportResult importJsonl(Span<const char> directory, bool merge);
    TextListResult importMarkdown(KiottySpecItemKind kind, std::string_view markdown);
    TextResult queryReadonly(std::string_view sql);

private:
    KiottySpecStatus exportTable(Span<const char> directory, std::string_view table, std::string_view keyOrder);
    KiottySpecStatus importTable(std::string_view table, std::string_view content, bool merge, ImportReport &report);
    KiottySpecStatus importRow(const std::string &table, std::string_view line, size_t lineNo, bool merge, ImportReport &report);

    Storage &_storage;
    ItemStore &_items;
    LinkGraph &_graph;
    Checker &_checker;
    Failure &_failure;
};

}
