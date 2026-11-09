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
#ifndef AMC_QUERY_COMPATIBILITY_H
#define AMC_QUERY_COMPATIBILITY_H

#include "SKL/AMC/IR/Compatibility.h"

#include <string>

namespace skl::amc::query {

// Query-layer compatibility facade. It deliberately depends on the IR policy,
// not on Pass, so `amc query --compatible` keeps the Query -> Pass boundary.
bool compare(
    const ABIModule &contract, const ABIModule &implementation, ir::CompatibilityResult &result, std::string &error);

}   // namespace skl::amc::query

#endif   // AMC_QUERY_COMPATIBILITY_H
