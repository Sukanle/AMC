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
// The demo native module for the Aue conformance prototype.
//
// It is an ordinary C++ module with a Lua-visible contract: both the L0
// Default layer and the L1 Meta layer end up calling exactly these functions,
// so a differential test isolates the dispatch layer from the module itself.
//
// When AMC is available the build generates the contract from
// `counter.abic.toml` and feeds it in (AUE_HAVE_GENERATED_CONTRACT); otherwise
// a hand-written contract with the same entries is used.
#include "Aue/Aue.h"

#ifdef AUE_HAVE_GENERATED_CONTRACT
#  include "counter_contract.hpp"
#endif

namespace {
// Keep the native layout in sync with `Counter.api.hpp`; the generated contract
// carries the field offsets, while this struct is the actual storage.
struct Counter {
    int32_t value;
};

int g_live_count = 0;

void counter_destroy(void *object) {
    if (object != nullptr && g_live_count > 0) --g_live_count;
}
}   // namespace

// External linkage so a generated contract can reference these symbols by name.
extern "C" int counter_new(lua_State *state) {
    const int32_t initial = static_cast<int32_t>(luaL_checkinteger(state, 1));
    auto *counter = static_cast<Counter *>(lua_newuserdatauv(state, sizeof(Counter), 0));
    counter->value = initial;
    ++g_live_count;
    luaL_setmetatable(state, "counter.Counter");
    return 1;
}

extern "C" int counter_add(lua_State *state) {
    auto *counter = static_cast<Counter *>(luaL_checkudata(state, 1, "counter.Counter"));
    counter->value += static_cast<int32_t>(luaL_checkinteger(state, 2));
    return 0;
}

extern "C" int counter_get(lua_State *state) {
    const auto *counter = static_cast<const Counter *>(luaL_checkudata(state, 1, "counter.Counter"));
    lua_pushinteger(state, counter->value);
    return 1;
}

extern "C" int counter_live_count(lua_State *state) {
    lua_pushinteger(state, g_live_count);
    return 1;
}

#ifndef AUE_HAVE_GENERATED_CONTRACT
namespace {
const aue::Entry kCounterEntries[] = {
    {       "new", 1,        counter_new},
    {       "add", 2,        counter_add},
    {       "get", 1,        counter_get},
    {"live_count", 0, counter_live_count},
};

const aue::Field kCounterFields[] = {
    {"value", 0, sizeof(int32_t), aue::FieldKind::integer_signed},
};

const aue::Type kCounterTypes[] = {
    {"Counter", sizeof(Counter), alignof(Counter), kCounterFields, 1},
};

const aue::Contract kCounterContract = {
    // Kept in sync with `Aue/Counter.abic.toml`; the generated conformance
    // script asserts the runtime contract and the metadata agree on this hash.
    "counter",
    "0Xcc79d555a7d016e2f2685c23bd849972",
    kCounterEntries,
    4,
    kCounterTypes,
    1,
};
}   // namespace
#endif

namespace aue {

void register_counter_types(lua_State *state) {
    register_contract_types(state, counter_contract());
    register_type_destroy(state, "counter.Counter", counter_destroy);
}

const Contract &counter_contract() {
#ifdef AUE_HAVE_GENERATED_CONTRACT
    return amc_generated::lua_contract;
#else
    return kCounterContract;
#endif
}

}   // namespace aue
