
#include "kiotty_spec_adapter.h"

#include <memory>
#include <string_view>

namespace impl = kiotty::spec::implementation;
namespace entity = kiotty::spec::implementation::entity;
using namespace kiotty::spec::adapter;

namespace {

std::string_view describeStatus(KiottySpecStatus code);

}

extern "C" {

KiottySpecContextResult KiottySpec_createContext(const KiottySpecConfiguration* configuration)
{
    KiottySpecContextResult result {};
    if (configuration == nullptr) {
        result.code = KIOTTY_SPEC_STATUS_INVALID_ARGUMENT;
        return result;
    }
    const ConfigurationResult settings = parseConfiguration(*configuration);
    if (!settings) {
        result.code = KIOTTY_SPEC_STATUS_INVALID_ARGUMENT;
        return result;
    }
    result.value = std::make_unique<KiottySpecContext>(settings.value).release();
    result.code = KIOTTY_SPEC_STATUS_SUCCESS;
    return result;
}

KiottySpecStatusResult KiottySpec_open(KiottySpecContext* context, KiottySpecText path)
{
    if (context == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    const TextResult parsed = Boundary(context->context).parse(path, Field::PATH);
    if (!parsed) { return status(parsed.code); }
    return status(context->context.open(parsed.value));
}

void KiottySpec_releaseContext(KiottySpecContext* context)
{ const std::unique_ptr<KiottySpecContext> owner(context); }

KiottySpecErrorResult KiottySpec_readLastError(KiottySpecContext* context)
{
    KiottySpecErrorResult result {};
    if (context == nullptr) {
        result.code = KIOTTY_SPEC_STATUS_INVALID_ARGUMENT;
        return result;
    }
    const entity::FailureDetail& detail = context->context.failure().detail();
    copyText(result.value.text, detail.text);
    result.value.line = detail.line;
    result.value.status = detail.status;
    result.value.subject = detail.subject;
    result.code = KIOTTY_SPEC_STATUS_SUCCESS;
    return result;
}

KiottySpecStatusResult KiottySpec_initializeProject(KiottySpecContext* context,
                                                    KiottySpecText project_name)
{
    if (context == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    const TextResult parsed = Boundary(context->context).parse(project_name, Field::PROJECT_NAME);
    if (!parsed) { return status(parsed.code); }
    return status(context->context.initialize(parsed.value));
}

KiottySpecNameResult KiottySpec_formatStatus(KiottySpecStatus status_code)
{ return nameResult(describeStatus(status_code)); }

KiottySpecNameResult KiottySpec_formatKind(KiottySpecItemKind kind)
{ return nameResult(entity::kindName(static_cast<entity::ItemKind>(kind))); }

KiottySpecItemKind KiottySpec_parseKind(KiottySpecText name)
{
    const TextResult parsed = parseText(name, KIOTTY_SPEC_MAX_NAME_LENGTH);
    if (!parsed) { return static_cast<KiottySpecItemKind>(0); }
    return static_cast<KiottySpecItemKind>(entity::parseKind(impl::viewOf(parsed.value)));
}

KiottySpecNameResult KiottySpec_formatCheckpointState(KiottySpecCheckpointState state)
{
    return nameResult(entity::stateName(static_cast<entity::CheckpointState>(state)));
}

}

namespace {

std::string_view describeStatus(KiottySpecStatus code)
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
    return std::string_view();
}

}
