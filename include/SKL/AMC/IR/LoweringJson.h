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
#ifndef AMC_IR_LOWERING_JSON_H
#define AMC_IR_LOWERING_JSON_H

#include "SKL/AMC/Core/Core.h"
#include "SKL/AMC/IR/Lowering.h"

#include <string>

namespace amc::ir {

// Serializes the lowered contract next to an artifact (`<artifact>.lowering`).
// Like the semantic sidecar it is an auxiliary file: the ABI artifact itself is
// unchanged, so a consumer that does not understand lowering keeps working.
bool write_lowering_json(const LoweringResult &result, const std::string &path, std::string &error);
bool read_lowering_json(const std::string &path, LoweringResult &result, std::string &error);

// Loads `<artifact>.lowering` when it exists. Returns true when the sidecar is
// absent (the caller then behaves as before M4), false only for a malformed or
// unreadable sidecar.
bool load_lowering_sidecar(const std::string &artifact_path, LoweringResult &result, std::string &error);

// Applies the lowered ownership/type/function contract to the ABI view a
// backend projects. This is intentionally the only place a backend turns
// lowering records into ABI ownership data: it never re-derives ownership from
// a function name. A lowered record that references an ABI identity absent from
// the artifact is a hard error: it means the sidecar and artifact came from
// different builds.
bool apply_lowering_contracts(const LoweringResult &result, AbiModule &module, std::string &error);

}   // namespace amc::ir

#endif   // AMC_IR_LOWERING_JSON_H
