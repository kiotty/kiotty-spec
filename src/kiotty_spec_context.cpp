// C 인터페이스: 컨텍스트 · 이름 · jsonl 교환. 입력을 Span 으로, 내부 레코드를 C 구조체로 바꾸는 일만 한다.

#include "kiotty_spec_adapter.h"

#include <memory>
#include <string_view>

namespace impl = kiotty::spec::implementation;
namespace entity = kiotty::spec::implementation::entity;
using namespace kiotty::spec::adapter;

extern "C" {

KiottySpecContextResult KiottySpec_createContext(const KiottySpecConfiguration* configuration)
{
    if (configuration == nullptr) { return KiottySpecContextResult{KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, nullptr}; }
    impl::Configuration settings;
    if (configuration->max_items != 0) { settings.maxItems = configuration->max_items; }
    if (configuration->max_relations != 0) { settings.maxRelations = configuration->max_relations; }
    if (configuration->max_tags != 0) { settings.maxTags = configuration->max_tags; }
    if (configuration->max_checkpoints != 0) { settings.maxCheckpoints = configuration->max_checkpoints; }
    if (configuration->max_cursors != 0) { settings.maxCursors = configuration->max_cursors; }
    if (configuration->max_cursor_rows != 0) { settings.maxCursorRows = configuration->max_cursor_rows; }
    if (configuration->work_buffer_bytes != 0) { settings.workBufferBytes = configuration->work_buffer_bytes; }
    // 할당 실패는 process 종료다 (개발수칙 1.1). 소유권은 releaseContext 가 되찾는다.
    KiottySpecContext* context = std::make_unique<KiottySpecContext>(settings).release();
    return KiottySpecContextResult{KIOTTY_SPEC_STATUS_SUCCESS, context};
}

// 실패하면 readLastError 로 이유를 읽는다.
KiottySpecStatusResult KiottySpec_open(KiottySpecContext* context, KiottySpecText path)
{
    if (context == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    return status(context->context.open(spanOf(path)));
}

void KiottySpec_releaseContext(KiottySpecContext* context) { const std::unique_ptr<KiottySpecContext> owner(context); }

KiottySpecSizeResult KiottySpec_readLastError(KiottySpecContext* context, char* buffer, size_t capacity)
{
    if (context == nullptr) { return KiottySpecSizeResult{KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, 0}; }
    return copyOut(context->context.failure().message(), buffer, capacity);
}

const char* KiottySpec_statusName(KiottySpecStatus code)
{
    switch (code) {
    case KIOTTY_SPEC_STATUS_SUCCESS: return "SUCCESS";
    case KIOTTY_SPEC_STATUS_NOT_FOUND: return "NOT_FOUND";
    case KIOTTY_SPEC_STATUS_INVALID_ARGUMENT: return "INVALID_ARGUMENT";
    case KIOTTY_SPEC_STATUS_CONFIRMED: return "CONFIRMED";
    case KIOTTY_SPEC_STATUS_PERMISSION: return "PERMISSION";
    case KIOTTY_SPEC_STATUS_PRECONDITION: return "PRECONDITION";
    case KIOTTY_SPEC_STATUS_CONFLICT: return "CONFLICT";
    case KIOTTY_SPEC_STATUS_WRONG_CURSOR_TYPE: return "WRONG_CURSOR_TYPE";
    case KIOTTY_SPEC_STATUS_INPUT_OUTPUT: return "INPUT_OUTPUT";
    case KIOTTY_SPEC_STATUS_DATABASE: return "DATABASE";
    case KIOTTY_SPEC_STATUS_OUT_OF_RANGE: return "OUT_OF_RANGE";
    case KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED: return "CAPACITY_EXCEEDED";
    case KIOTTY_SPEC_STATUS_ARITHMETIC_OVERFLOW: return "ARITHMETIC_OVERFLOW";
    case KIOTTY_SPEC_STATUS_POOL_IS_FULL: return "POOL_IS_FULL";
    case KIOTTY_SPEC_STATUS_INVALID_HANDLE: return "INVALID_HANDLE";
    case KIOTTY_SPEC_STATUS_INVALID_UTF8: return "INVALID_UTF8";
    }
    return "UNKNOWN";
}

KiottySpecStatusResult KiottySpec_initProject(KiottySpecContext* context, KiottySpecText project_name, bool install_git_hooks)
{
    if (context == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    return status(context->context.init(spanOf(project_name), install_git_hooks));
}

const char* KiottySpec_kindName(KiottySpecItemKind kind)
{
    const std::string_view name = entity::kindName(static_cast<entity::ItemKind>(kind));
    return name.empty() ? "" : name.data();
}

KiottySpecItemKind KiottySpec_parseKind(KiottySpecText name)
{
    return static_cast<KiottySpecItemKind>(entity::parseKind(impl::viewOf(spanOf(name))));
}

const char* KiottySpec_checkpointStateName(KiottySpecCheckpointState state)
{
    const std::string_view name = entity::stateName(static_cast<entity::CheckpointState>(state));
    return name.empty() ? "" : name.data();
}

KiottySpecStatusResult KiottySpec_exportJsonl(KiottySpecContext* context, KiottySpecText directory)
{
    if (context == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    if (directory.length > 0) { return status(context->context.exchange().exportJsonl(spanOf(directory))); }
    const auto fallback = context->context.jsonlDirectory();
    if (!fallback) { return status(fallback.code); }
    return status(context->context.exchange().exportJsonl(fallback.value.view()));
}

KiottySpecSizeResult KiottySpec_importJsonl(KiottySpecContext* context, KiottySpecText directory, char* report, size_t capacity)
{
    if (context == nullptr) { return KiottySpecSizeResult{KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, 0}; }
    // 보고는 호출자 버퍼에 바로 쓴다. 버퍼가 없으면 버린다.
    char discard[1] {};
    const impl::Span<char> area = report != nullptr && capacity > 0 ? impl::Span<char>(report, capacity) : impl::Span<char>(discard, 1);
    impl::Result<size_t, KiottySpecStatus> imported = impl::Result<size_t, KiottySpecStatus>::failure(KIOTTY_SPEC_STATUS_NOT_FOUND);
    if (directory.length > 0) {
        imported = context->context.exchange().importJsonl(spanOf(directory), area);
    } else {
        const auto fallback = context->context.jsonlDirectory();
        if (!fallback) { return KiottySpecSizeResult{fallback.code, 0}; }
        imported = context->context.exchange().importJsonl(fallback.value.view(), area);
    }
    return KiottySpecSizeResult{imported.code, imported ? imported.value : 0};
}

}
