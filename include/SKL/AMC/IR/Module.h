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
#ifndef AMC_IR_MODULE_H
#define AMC_IR_MODULE_H

#include "SKL/AMC/Core/Core.h"
#include "SKL/AMC/IR/Lowering.h"
#include "SKL/AMC/Semantic/Model.h"

#include <stdint.h>

namespace amc::ir {

// A0 contract: the IR is the only data exchanged between a frontend, the pass
// pipeline and a backend. Until A2 merges the two views, it aggregates the
// existing ABI module and the semantic model so the boundary can be frozen
// without moving code.
inline constexpr uint32_t module_schema_version = 2;

struct Module {
    AbiModule abi;
    semantic::SemanticModule semantic;
    LoweringResult lowering;
    uint32_t schema_version = module_schema_version;
};

}   // namespace amc::ir

#endif   // AMC_IR_MODULE_H
