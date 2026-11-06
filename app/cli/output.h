#pragma once

#include "kiotty_spec.h"

#include <nlohmann/json.hpp>

#include <stdexcept>
#include <string>

namespace kiotty::cli {

using nlohmann::json;

class CallError : public std::runtime_error {
public:
    CallError(KiottySpecStatus status, const std::string &message) : std::runtime_error(message), status(status) {}
    KiottySpecStatus status;
};

void check(KiottySpecStatus status);

std::string enumName(const char *type, int value);
int enumValue(const char *type, const std::string &name);

json toJson(const KiottySpecItem &item);
json toJson(const KiottySpecLink &link);
json toJson(const KiottySpecFile &file);
json toJson(const KiottySpecCheckpoint &checkpoint);
json toJson(const KiottySpecChangeRequest &change);
json toJson(const KiottySpecResult &result);
json toJson(const KiottySpecMilestoneEntry &entry);
json toJson(const KiottySpecStepRun &run);
json toJson(const KiottySpecPosition &position);

enum class CursorKind { Item, Link, File, Checkpoint, ChangeRequest, Result, MilestoneEntry, StepRun, Id };
json drain(KiottySpecCursor *cursor, CursorKind kind);

std::string take(char *text);

std::string toTsv(const json &value);
void printTsv(const json &value);
void printOrderedTsv(const nlohmann::ordered_json &value);
void printText(const std::string &text);

}
