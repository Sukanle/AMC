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
#ifndef AMC_SEMANTIC_SYNC_H
#define AMC_SEMANTIC_SYNC_H

#include "SKL/AMC/Core/Core.h"
#include "SKL/AMC/Semantic/Model.h"

namespace skl::amc::semantic {

// Complete the semantic model from the ABI view.
//
// A frontend records source-level facts where it can (templates, constraints,
// overloads), but every ABI type and function still needs a semantic
// counterpart so lowering has a direct link instead of re-deriving one from a
// name. This pass creates the missing SemanticType/SemanticRecord/
// SemanticFunction records and fills `SemanticType::abi_type` /
// `SemanticFunction::abi_signature`. Existing records are never replaced, so
// frontend source facts keep precedence over the synthesized fallback.
void synchronize_from_abi(const ABIModule &module, SemanticModule &semantic);

}   // namespace skl::amc::semantic

#endif   // AMC_SEMANTIC_SYNC_H
