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
#ifndef AMC_LUA_H
#define AMC_LUA_H

#include "SKL/AMC/Core/Core.h"

#include <string>

namespace amc {

// AI-P3: generates the Aue (Lua) boundary-layer contract from ABIX metadata.
//
// The emitted header defines an `aue::Contract` whose entries map each ABIX
// function to a native Lua C function symbol (`int (*)(lua_State*)`). L0 and
// L1 both consume this one contract, so both ends agree on the ABI hash and
// on the exact function set — the plan's "双端对齐 hash".
//
// Symbol naming: the ABIX function name sanitized to a C identifier
// (`Foo::bar` -> `Foo_bar`). The native module must export that symbol.
std::string generate_lua_contract(const AbiModule &module, std::string &error);

// The C identifier AMC uses for an ABIX function's native entry point.
std::string lua_function_symbol(std::string_view name);

// The Lua-visible key for a function: `<package>_` is stripped when present,
// so `counter_add` in package `counter` is exposed as `add`.
std::string lua_entry_name(std::string_view package, std::string_view name);

// Emits a Lua script that asserts the generated contract exposes every ABIX
// function and that the runtime contract's abi_hash matches the metadata.
// Run under both L0 and L1 it turns the generated contract into an executable
// conformance case (metadata -> generated Lua test -> L0/L1).
std::string generate_lua_conformance(const AbiModule &module, std::string &error);

}   // namespace amc

#endif
