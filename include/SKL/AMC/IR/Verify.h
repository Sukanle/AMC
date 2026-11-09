/*
 * Copyright 2026 Sukanle(https://github.com/Sukanle)
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef AMC_VERIFY_H
#define AMC_VERIFY_H

#include "SKL/AMC/Core/Core.h"

#include <string>
#include <vector>

namespace skl::amc {

// AI-P1: verify that a runtime implementation still matches its frozen ABI
// contract.
//
// `contract`    — the published/frozen `.abix` (what consumers compiled against)
// `implementation` — metadata regenerated from the current sources
//
// Two modules are consistent only when their type sets, layouts, field sets,
// function signatures and optional interaction contracts are identical.
// Additions are reported as drift too, because they widen the ABI surface and
// must be an explicit decision. Ownership and error-boundary differences are
// reported as contract drift without being folded into layout compatibility.
enum class ChangeKind {
    TYPE_ADDED,
    TYPE_REMOVED,
    LAYOUT_CHANGED,
    FIELD_ADDED,
    FIELD_REMOVED,
    FIELD_OFFSET_CHANGED,
    FIELD_TYPE_CHANGED,
    FUNCTION_ADDED,
    FUNCTION_REMOVED,
    FUNCTION_SIGNATURE_CHANGED,
    FUNCTION_PARAMETER_CHANGED,
    CALLING_CONVENTION_CHANGED,
    OWNERSHIP_CHANGED,
    ERROR_BOUNDARY_CHANGED,
    INTERFACE_CHANGED,
};

const char *change_kind_name(ChangeKind kind);

struct VerifyChange {
    ChangeKind kind = ChangeKind::TYPE_REMOVED;
    std::string type;     // owning type name (may be empty for free functions)
    std::string name;     // field / function name when applicable
    std::string detail;   // human-readable explanation
};

struct VerifyResult {
    std::string package;
    std::string contract_path;
    std::string implementation_path;
    bool consistent = true;
    std::vector<VerifyChange> changes;
};

bool verify_modules(
    const ABIModule &contract, const ABIModule &implementation, VerifyResult &result, std::string &error);

std::string verify_result_to_text(const VerifyResult &result);
std::string verify_result_to_json(const VerifyResult &result);
std::string verify_report_to_text(const std::vector<VerifyResult> &results);
std::string verify_report_to_json(const std::vector<VerifyResult> &results, bool consistent);

}   // namespace skl::amc

#endif
