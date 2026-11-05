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
#ifndef AMC_SEMANTIC_SERIALIZE_H
#define AMC_SEMANTIC_SERIALIZE_H

#include "SKL/AMC/Semantic/Model.h"

#include <string>

namespace amc::semantic {

// Writes the semantic sidecar (`<artifact>.semantic`) as JSON. The sidecar is
// not part of the stable ABI artifact: it carries template declarations,
// instances and source-level facts for tools, diagnostics and lowering.
bool write_semantic_json(const SemanticModule &module, const std::string &path, std::string &error);

// Reads a semantic sidecar written by `write_semantic_json`. Unknown fields are
// ignored; a missing required field is reported instead of silently producing a
// partially linked model.
bool read_semantic_json(const std::string &path, SemanticModule &module, std::string &error);

}   // namespace amc::semantic

#endif   // AMC_SEMANTIC_SERIALIZE_H
