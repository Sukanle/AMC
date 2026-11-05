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
#pragma once

#include "SKL/AMC/Core/Core.h"
#include "SKL/AMC/IR/Lowering.h"

#include <string>

namespace amc::lang::rust {

// ABIX IR -> Rust projection (`repr(C)` types + metadata region).
int run_backend(const char *input, const char *output);

// Map a packed ABIX primitive descriptor (see `amc::primitive_abi`) to the
// Rust type that preserves the C ABI on the target. Exposed for tests.
std::string primitive_rust_type(uint32_t primitive_abi);

// Render the Rust projection for a module. Exposed for tests.
std::string generate_projection(const amc::AbiModule &module, const amc::ir::LoweringResult *lowering = nullptr);

}   // namespace amc::lang::rust
