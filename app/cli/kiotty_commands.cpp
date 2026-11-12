#include "kiotty_commands.h"

namespace kiotty::cli {

namespace {

using Key = MessageKey;

const std::vector<CommandEntry> COMMAND_TABLE = {
    { "init", "", Key::INIT_SYNOPSIS, Key::INIT_SUMMARY, initializeProject },
    { "item", "add", Key::ITEM_ADD_SYNOPSIS, Key::ITEM_ADD_SUMMARY, addItem },
    { "item", "rename", Key::ITEM_RENAME_SYNOPSIS, Key::ITEM_RENAME_SUMMARY, renameItem },
    { "item", "set", Key::ITEM_SET_SYNOPSIS, Key::ITEM_SET_SUMMARY, setItem },
    { "item", "show", Key::ITEM_SHOW_SYNOPSIS, Key::ITEM_SHOW_SUMMARY, showItem },
    { "item", "list", Key::ITEM_LIST_SYNOPSIS, Key::ITEM_LIST_SUMMARY, listItems },
    { "item", "children", Key::ITEM_CHILDREN_SYNOPSIS, Key::ITEM_CHILDREN_SUMMARY,
      listChildren },
    { "item", "body", Key::ITEM_BODY_SYNOPSIS, Key::ITEM_BODY_SUMMARY, showBody },
    { "item", "remove", Key::ITEM_REMOVE_SYNOPSIS, Key::ITEM_REMOVE_SUMMARY, removeItem },
    { "tag", "show", Key::TAG_SHOW_SYNOPSIS, Key::TAG_SHOW_SUMMARY, showTags },
    { "tag", "set", Key::TAG_SET_SYNOPSIS, Key::TAG_SET_SUMMARY, setTag },
    { "relation", "add", Key::RELATION_ADD_SYNOPSIS, Key::RELATION_ADD_SUMMARY,
      addRelation },
    { "relation", "remove", Key::RELATION_REMOVE_SYNOPSIS, Key::RELATION_REMOVE_SUMMARY,
      removeRelation },
    { "relation", "list", Key::RELATION_LIST_SYNOPSIS, Key::RELATION_LIST_SUMMARY,
      listRelations },
    { "impact", "", Key::IMPACT_SYNOPSIS, Key::IMPACT_SUMMARY, showImpact },
    { "checkpoint", "start", Key::CHECKPOINT_START_SYNOPSIS, Key::CHECKPOINT_START_SUMMARY,
      startCheckpoint },
    { "checkpoint", "resume", Key::CHECKPOINT_RESUME_SYNOPSIS,
      Key::CHECKPOINT_RESUME_SUMMARY, resumeCheckpoint },
    { "checkpoint", "complete", Key::CHECKPOINT_COMPLETE_SYNOPSIS,
      Key::CHECKPOINT_COMPLETE_SUMMARY, completeCheckpoint },
    { "checkpoint", "show", Key::CHECKPOINT_SHOW_SYNOPSIS, Key::CHECKPOINT_SHOW_SUMMARY,
      showCheckpoint },
    { "checkpoint", "list", Key::CHECKPOINT_LIST_SYNOPSIS, Key::CHECKPOINT_LIST_SUMMARY,
      listCheckpoints },
    { "checkpoint", "prune", Key::CHECKPOINT_PRUNE_SYNOPSIS, Key::CHECKPOINT_PRUNE_SUMMARY,
      pruneCheckpoints },
    { "where", "", Key::WHERE_SYNOPSIS, Key::WHERE_SUMMARY, showPosition },
    { "history", "", Key::HISTORY_SYNOPSIS, Key::HISTORY_SUMMARY, showHistory },
    { "decision", "show", Key::DECISION_SHOW_SYNOPSIS, Key::DECISION_SHOW_SUMMARY,
      showDecision },
    { "decision", "set", Key::DECISION_SET_SYNOPSIS, Key::DECISION_SET_SUMMARY,
      setDecision },
    { "diff", "", Key::DIFF_SYNOPSIS, Key::DIFF_SUMMARY, runDiff },
    { "model", "", Key::MODEL_SYNOPSIS, Key::MODEL_SUMMARY, showModel },
};

}

const std::vector<CommandEntry>& commands() { return COMMAND_TABLE; }

}
