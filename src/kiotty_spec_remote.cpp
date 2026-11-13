#include "kiotty_spec.h"
#include "kiotty_spec_adapter.h"

namespace impl = kiotty::spec::implementation;
namespace entity = kiotty::spec::implementation::entity;
using namespace kiotty::spec::adapter;

namespace {

using WorkspaceResult = impl::Result<impl::Workspace&, KiottySpecStatus>;
using AcceptResult = impl::Result<impl::AcceptOutcome, KiottySpecStatus>;

WorkspaceResult findRemote(KiottySpecContext* local, KiottySpecContext* remote);
KiottySpecRemote toRemote(const entity::RemoteRecord& record);
KiottySpecRuleDiff toRuleDiff(const impl::RuleDiffRecord& record);
entity::Step pickEntryStep(impl::OpenedContext& context);
KiottySpecAcceptResult acceptFailure(KiottySpecStatus code);

}

extern "C" {

KiottySpecRemoteResult KiottySpec_readRemote(KiottySpecContext* context)
{
    KiottySpecRemoteResult result {};
    const OpenedResult spec = findOpenedContext(context);
    result.code = spec.code;
    if (!spec) { return result; }
    const auto remote = spec.value->ruleSync().remote().read();
    result.code = remote.code;
    if (remote) { result.value = toRemote(remote.value); }
    return result;
}

KiottySpecStatusResult KiottySpec_writeRemote(KiottySpecContext* context,
                                              const KiottySpecRemote* remote)
{
    const OpenedResult spec = findOpenedContext(context);
    if (!spec) { return status(spec.code); }
    const RemoteRecordResult parsed = RemoteParser(*spec.value).parseRemote(remote);
    if (!parsed) { return status(parsed.code); }
    return status(spec.value->ruleSync().remote().write(parsed.value));
}

KiottySpecCursorResult KiottySpec_openRuleDiffCursor(KiottySpecContext* local,
                                                     KiottySpecContext* remote)
{
    const WorkspaceResult other = findRemote(local, remote);
    if (!other) { return cursorFailure(other.code); }
    impl::OpenedContext& spec = *findOpenedContext(local).value;
    const impl::CursorIdResult opened = spec.ruleSync().differ().open(*other.value);
    return Boundary(spec).wrap(opened);
}

KiottySpecStatusResult KiottySpec_fetchRuleDiffPage(KiottySpecContext* context,
                                                    KiottySpecCursor cursor,
                                                    KiottySpecRuleDiffPage* page)
{
    const OpenedResult spec = findOpenedContext(context);
    if (!spec) { return status(spec.code); }
    if (page == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    page->length = 0;
    impl::RuleDiffer& differ = spec.value->ruleSync().differ();
    while (page->length < KIOTTY_SPEC_PAGE_CAPACITY) {
        const auto row = differ.next(cursor);
        if (!row) { return status(row.code); }
        const bool is_end = row.value.id.empty();
        if (is_end) { break; }
        page->elements[page->length] = toRuleDiff(row.value);
        ++page->length;
    }
    return status(KIOTTY_SPEC_STATUS_SUCCESS);
}

KiottySpecCursorResult KiottySpec_openRuleBodyDiffCursor(KiottySpecContext* local,
                                                         KiottySpecContext* remote,
                                                         KiottySpecText id)
{
    const WorkspaceResult other = findRemote(local, remote);
    if (!other) { return cursorFailure(other.code); }
    impl::OpenedContext& spec = *findOpenedContext(local).value;
    const Boundary boundary(spec);
    const TextResult rule = boundary.parse(id, Field::ID);
    if (!rule) { return cursorFailure(rule.code); }
    if (rule.value.length() == 0) { return cursorFailure(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    const impl::CursorIdResult opened =
        spec.ruleSync().differ().openBody(*other.value, rule.value);
    return boundary.wrap(opened);
}

KiottySpecAcceptResult KiottySpec_acceptRules(KiottySpecContext* local, KiottySpecContext* remote,
                                              const KiottySpecAcceptInput* input,
                                              KiottySpecActor actor)
{
    const WorkspaceResult other = findRemote(local, remote);
    if (!other) { return acceptFailure(other.code); }
    impl::OpenedContext& spec = *findOpenedContext(local).value;
    const ActorResult parsed_actor = ValueParser(spec).parseActor(actor);
    if (!parsed_actor) { return acceptFailure(parsed_actor.code); }
    DraftResult draft = RemoteParser(spec).parseAccept(input);
    if (!draft) { return acceptFailure(draft.code); }
    draft.value.entry_step = pickEntryStep(spec);
    const AcceptResult accepted =
        spec.ruleSync().acceptor().accept(*other.value, draft.value, parsed_actor.value);
    if (!accepted) { return acceptFailure(accepted.code); }
    KiottySpecAcceptResult result {};
    result.checkpoint = accepted.value.checkpoint;
    copyTerminated(impl::Span<char>(result.change_request, sizeof(result.change_request)),
                   accepted.value.change_request.view());
    result.code = KIOTTY_SPEC_STATUS_SUCCESS;
    return result;
}

KiottySpecStatusResult KiottySpec_exportRules(KiottySpecContext* local, KiottySpecText path)
{
    const OpenedResult spec = findOpenedContext(local);
    if (!spec) { return status(spec.code); }
    const TextResult parsed = Boundary(*spec.value).parse(path, Field::PATH);
    if (!parsed) { return status(parsed.code); }
    return status(spec.value->ruleSync().exporter().exportTo(parsed.value));
}

}

namespace kiotty::spec::adapter {

RemoteRecordResult RemoteParser::parseRemote(const KiottySpecRemote* remote) const
{
    if (remote == nullptr) {
        return RemoteRecordResult::failure(raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT));
    }
    const impl::Span<const char> url = impl::boundedText(remote->url, sizeof(remote->url));
    const impl::Span<const char> base =
        impl::boundedText(remote->rule_base, sizeof(remote->rule_base));
    const bool is_url_ended = url.length() < sizeof(remote->url);
    const bool is_base_ended = base.length() < sizeof(remote->rule_base);
    if (!is_url_ended || !is_base_ended) {
        return RemoteRecordResult::failure(raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT));
    }
    entity::RemoteRecord record {};
    record.url.assign(url);
    record.rule_base.assign(base);
    const KiottySpecStatus checked = entity::checkRemote(record);
    if (checked != KIOTTY_SPEC_STATUS_SUCCESS) {
        return RemoteRecordResult::failure(raise(checked));
    }
    return RemoteRecordResult::success(record);
}

DraftResult RemoteParser::parseAccept(const KiottySpecAcceptInput* input) const
{
    if (input == nullptr) {
        return DraftResult::failure(raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT));
    }
    const Boundary boundary(_context);
    const TextResult description = boundary.parse(input->description, Field::DESCRIPTION);
    const TextResult title = boundary.parse(input->title, Field::TITLE);
    const TextResult summary = boundary.parse(input->summary, Field::SUMMARY);
    const TextResult body = boundary.parse(input->body, Field::BODY);
    const TextResult base = parseCommit(input->rule_base);
    const bool is_parsed = description && title && summary && body && base;
    if (!is_parsed) { return DraftResult::failure(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    if (title.value.length() == 0) {
        return DraftResult::failure(raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT));
    }
    impl::AcceptDraft draft {};
    draft.description = description.value;
    draft.title = title.value;
    draft.summary = summary.value;
    draft.body = body.value;
    draft.rule_base = base.value;
    return DraftResult::success(draft);
}

TextResult RemoteParser::parseCommit(KiottySpecText commit) const
{
    const TextResult parsed = parseText(commit, KIOTTY_SPEC_MAX_COMMIT_LENGTH);
    if (!parsed) { return TextResult::failure(raise(parsed.code)); }
    const bool is_commit = parsed.value.length() > 0 && entity::isValidCommit(parsed.value);
    if (!is_commit) { return TextResult::failure(raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT)); }
    return parsed;
}

KiottySpecStatus RemoteParser::raise(KiottySpecStatus code) const
{
    return _context.failure().raise(code, KIOTTY_SPEC_SUBJECT_REMOTE);
}

}

namespace {

WorkspaceResult findRemote(KiottySpecContext* local, KiottySpecContext* remote)
{
    const OpenedResult local_spec = findOpenedContext(local);
    if (!local_spec) { return WorkspaceResult::failure(local_spec.code); }
    const bool is_same = local == remote;
    if (is_same) {
        return WorkspaceResult::failure(local_spec.value->failure().raise(
            KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, KIOTTY_SPEC_SUBJECT_REMOTE_RULES));
    }
    const OpenedResult remote_spec = findOpenedContext(remote);
    if (!remote_spec) {
        return WorkspaceResult::failure(local_spec.value->failure().raise(
            remote_spec.code, KIOTTY_SPEC_SUBJECT_REMOTE_RULES));
    }
    return WorkspaceResult::success(remote_spec.value->workspace());
}

KiottySpecRemote toRemote(const entity::RemoteRecord& record)
{
    KiottySpecRemote remote {};
    copyTerminated(impl::Span<char>(remote.url, sizeof(remote.url)), record.url.view());
    copyTerminated(impl::Span<char>(remote.rule_base, sizeof(remote.rule_base)),
                   record.rule_base.view());
    return remote;
}

KiottySpecRuleDiff toRuleDiff(const impl::RuleDiffRecord& record)
{
    KiottySpecRuleDiff diff {};
    copyTerminated(impl::Span<char>(diff.id, sizeof(diff.id)), record.id.view());
    copyTerminated(impl::Span<char>(diff.title, sizeof(diff.title)), record.title.view());
    diff.change = static_cast<KiottySpecChange>(record.change);
    diff.changed_fields = record.changed_fields;
    return diff;
}

entity::Step pickEntryStep(impl::OpenedContext& context)
{
    const auto position = context.readSide().finder().history().position();
    const uint32_t inspecting = static_cast<uint32_t>(entity::Step::CODE_INSPECTING);
    const uint32_t next = position ? position.value.next_step : inspecting;
    const bool is_earlier = entity::isValidStep(next) && next < inspecting;
    return is_earlier ? static_cast<entity::Step>(next) : entity::Step::CODE_INSPECTING;
}

KiottySpecAcceptResult acceptFailure(KiottySpecStatus code)
{
    KiottySpecAcceptResult result {};
    result.code = code;
    return result;
}

}
