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
// Boundary-overhead benchmark: L0 (direct C binding) vs L1 (__index contract
// dispatch). It reports nanoseconds per full call:
//
//   l0_call       — resolve a raw field and pcall it
//   l1_call_warm  — resolve the memoized field and pcall it (steady state)
//   l1_call_cold  — drop the memoized field first, so every call takes the
//                   __index contract lookup (worst case)
//
// Timing is inherently noisy and this runs as a smoke test that must simply
// succeed; the numbers are printed for comparison, not asserted.
#include "Aue/Aue.h"

#include <chrono>
#include <stdint.h>
#include <fmt/format.h>

namespace {

using Clock = std::chrono::steady_clock;

enum class Mode {
    L0,
    L1,
    L2,
};

volatile int64_t g_sink = 0;

void push_counter(lua_State *state) { lua_getfield(state, LUA_REGISTRYINDEX, "aue_bench_counter"); }

double call_nanoseconds(lua_State *state, Mode mode, bool force_index, int iterations) {
    const auto start = Clock::now();
    for (int i = 0; i < iterations; ++i) {
        lua_getglobal(state, "counter");
        if (mode == Mode::L1 && force_index) {
            // Invalidate the memoized binding so __index runs again.
            lua_pushstring(state, "get");
            lua_pushnil(state);
            lua_rawset(state, -3);
        }
        lua_getfield(state, -1, "get");
        push_counter(state);
        if (lua_pcall(state, 1, 1, 0) == LUA_OK) {
            g_sink += static_cast<int64_t>(lua_tointeger(state, -1));
            lua_pop(state, 2);
        } else {
            lua_pop(state, 2);
        }
    }
    const auto end = Clock::now();
    return std::chrono::duration<double, std::nano>(end - start).count() / iterations;
}

double field_nanoseconds(lua_State *state, int iterations) {
    const auto start = Clock::now();
    for (int i = 0; i < iterations; ++i) {
        push_counter(state);
        lua_getfield(state, -1, "value");
        g_sink += static_cast<int64_t>(lua_tointeger(state, -1));
        lua_pop(state, 2);
    }
    const auto end = Clock::now();
    return std::chrono::duration<double, std::nano>(end - start).count() / iterations;
}

double field_set_nanoseconds(lua_State *state, int iterations) {
    const auto start = Clock::now();
    for (int i = 0; i < iterations; ++i) {
        push_counter(state);
        lua_pushinteger(state, i);
        lua_setfield(state, -2, "value");
        lua_pop(state, 1);
    }
    const auto end = Clock::now();
    return std::chrono::duration<double, std::nano>(end - start).count() / iterations;
}

double child_nanoseconds(lua_State *state, int iterations) {
    const auto start = Clock::now();
    for (int i = 0; i < iterations; ++i) {
        push_counter(state);
        lua_getfield(state, -1, "child");
        lua_getfield(state, -1, "x");
        g_sink += static_cast<int64_t>(lua_tointeger(state, -1));
        lua_pop(state, 3);
    }
    const auto end = Clock::now();
    return std::chrono::duration<double, std::nano>(end - start).count() / iterations;
}

double array_len_nanoseconds(lua_State *state, int iterations) {
    const auto start = Clock::now();
    for (int i = 0; i < iterations; ++i) {
        push_counter(state);
        lua_getfield(state, -1, "samples");
        lua_len(state, -1);
        g_sink += static_cast<int64_t>(lua_tointeger(state, -1));
        lua_pop(state, 3);
    }
    const auto end = Clock::now();
    return std::chrono::duration<double, std::nano>(end - start).count() / iterations;
}

double array_get_nanoseconds(lua_State *state, int iterations) {
    const auto start = Clock::now();
    for (int i = 0; i < iterations; ++i) {
        push_counter(state);
        lua_getfield(state, -1, "samples");
        lua_pushinteger(state, (i % 4) + 1);
        lua_gettable(state, -2);
        g_sink += static_cast<int64_t>(lua_tointeger(state, -1));
        lua_pop(state, 3);
    }
    const auto end = Clock::now();
    return std::chrono::duration<double, std::nano>(end - start).count() / iterations;
}

double gc_nanoseconds(lua_State *state, int iterations) {
    const auto start = Clock::now();
    for (int i = 0; i < iterations; ++i) {
        lua_getglobal(state, "counter");
        lua_getfield(state, -1, "new");
        lua_pushinteger(state, i);
        if (lua_pcall(state, 1, 1, 0) == LUA_OK)
            lua_pop(state, 2);
        else
            lua_pop(state, 2);
    }
    lua_gc(state, LUA_GCCOLLECT, 0);
    const auto end = Clock::now();
    return std::chrono::duration<double, std::nano>(end - start).count() / iterations;
}

bool prepare(Mode mode, lua_State **out) {
    lua_State *state = luaL_newstate();
    if (state == nullptr) return false;
    luaL_openlibs(state);
    skl::aue::register_counter_types(state);
    switch (mode) {
        case Mode::L0: skl::aue::register_direct(state, skl::aue::counter_contract()); break;
        case Mode::L1: skl::aue::register_meta(state, skl::aue::counter_contract()); break;
        case Mode::L2: skl::aue::register_native(state, skl::aue::counter_contract()); break;
    }
    lua_setglobal(state, "counter");

    // One shared Counter stored in the registry for the call path.
    lua_getglobal(state, "counter");
    lua_getfield(state, -1, "new");
    lua_pushinteger(state, 0);
    if (lua_pcall(state, 1, 1, 0) != LUA_OK) {
        lua_close(state);
        return false;
    }
    lua_setfield(state, LUA_REGISTRYINDEX, "aue_bench_counter");
    lua_pop(state, 1);
    *out = state;
    return true;
}

}   // namespace

int main() {
    constexpr int kIterations = 200'000;

    lua_State *l0 = nullptr, *l1 = nullptr;
    if (!prepare(Mode::L0, &l0) || !prepare(Mode::L1, &l1)) {
        fmt::print(stderr, "abix-bench: could not initialise the Lua state\n");
        return 1;
    }

    const double l0_call = call_nanoseconds(l0, Mode::L0, false, kIterations);
    const double l1_call_warm = call_nanoseconds(l1, Mode::L1, false, kIterations);
    const double l1_call_cold = call_nanoseconds(l1, Mode::L1, true, kIterations);
    const double field_get = field_nanoseconds(l1, kIterations);
    const double field_set = field_set_nanoseconds(l1, kIterations);
    const double child_access = child_nanoseconds(l1, kIterations / 4);
    const double array_len = array_len_nanoseconds(l1, kIterations / 4);
    const double array_get = array_get_nanoseconds(l1, kIterations / 4);
    const double gc_alloc = gc_nanoseconds(l1, kIterations / 10);

    lua_close(l0);
    lua_close(l1);

    fmt::print(
        "abix-bench: l0_call={:.1f}ns l1_call_warm={:.1f}ns l1_call_cold={:.1f}ns"
        " field_get={:.1f}ns field_set={:.1f}ns child_access={:.1f}ns"
        " array_len={:.1f}ns array_get={:.1f}ns gc_alloc={:.1f}ns",
        l0_call, l1_call_warm, l1_call_cold, field_get, field_set, child_access, array_len, array_get, gc_alloc);

#if defined(AUE_ENABLE_L2)
    lua_State *l2 = nullptr;
    if (!prepare(Mode::l2, &l2)) {
        fmt::print(stderr, "\nabix-bench: could not initialise the L2 Lua state\n");
        return 1;
    }
    const double l2_call = call_nanoseconds(l2, Mode::l2, false, kIterations);
    lua_close(l2);
    fmt::print(" l2_call={:.1f}ns", l2_call);
#endif

    fmt::print("\n");
    return 0;
}
