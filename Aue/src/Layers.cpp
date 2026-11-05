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
#include "Aue/Aue.h"

#include <cstring>
#include <limits>
#include <string>

namespace aue {
namespace {

// `__index(table, key)` for the L1 Meta layer. Upvalue 1 is the pre-built
// name -> function index, so a cold lookup is a single rawget instead of a
// linear scan over the contract entries.
int meta_index(lua_State *state) {
    const char *key = lua_tostring(state, 2);
    if (key == nullptr) {
        lua_pushnil(state);
        return 1;
    }
    lua_pushvalue(state, 2);
    lua_rawget(state, lua_upvalueindex(1));
    if (!lua_isfunction(state, -1)) {
        lua_pop(state, 1);
        lua_pushnil(state);
        return 1;
    }
    // Memoize on the module table so subsequent access is a plain rawget.
    lua_pushvalue(state, 2);
    lua_pushvalue(state, -2);
    lua_rawset(state, 1);
    return 1;
}

void set_string_field(lua_State *state, const char *key, const char *value) {
    lua_pushstring(state, value);
    lua_setfield(state, -2, key);
}

std::string type_metatable_name(const Contract &contract, const Type &type) {
    std::string name = type.name;
    for (size_t pos = 0; (pos = name.find("::", pos)) != std::string::npos; pos += 1)
        name.replace(pos, 2, ".");
    return std::string(contract.module) + "." + name;
}

const Field *lookup_field(lua_State *state) {
    lua_pushvalue(state, 2);
    lua_rawget(state, lua_upvalueindex(1));
    if (!lua_islightuserdata(state, -1)) {
        lua_pop(state, 1);
        return nullptr;
    }
    const auto *field = static_cast<const Field *>(lua_touserdata(state, -1));
    lua_pop(state, 1);
    return field;
}

void push_native_value(lua_State *state, const void *address, const Field &field) {
    switch (field.kind) {
        case FieldKind::integer_signed: {
            int64_t value = 0;
            switch (field.size) {
                case 1: {
                    int8_t raw = 0;
                    std::memcpy(&raw, address, sizeof(raw));
                    value = raw;
                    break;
                }
                case 2: {
                    int16_t raw = 0;
                    std::memcpy(&raw, address, sizeof(raw));
                    value = raw;
                    break;
                }
                case 4: {
                    int32_t raw = 0;
                    std::memcpy(&raw, address, sizeof(raw));
                    value = raw;
                    break;
                }
                case 8: {
                    int64_t raw = 0;
                    std::memcpy(&raw, address, sizeof(raw));
                    value = raw;
                    break;
                }
                default: lua_pushnil(state); return;
            }
            lua_pushinteger(state, static_cast<lua_Integer>(value));
            return;
        }
        case FieldKind::integer_unsigned: {
            uint64_t value = 0;
            switch (field.size) {
                case 1: {
                    uint8_t raw = 0;
                    std::memcpy(&raw, address, sizeof(raw));
                    value = raw;
                    break;
                }
                case 2: {
                    uint16_t raw = 0;
                    std::memcpy(&raw, address, sizeof(raw));
                    value = raw;
                    break;
                }
                case 4: {
                    uint32_t raw = 0;
                    std::memcpy(&raw, address, sizeof(raw));
                    value = raw;
                    break;
                }
                case 8: {
                    uint64_t raw = 0;
                    std::memcpy(&raw, address, sizeof(raw));
                    value = raw;
                    break;
                }
                default: lua_pushnil(state); return;
            }
            if (value > static_cast<uint64_t>(std::numeric_limits<lua_Integer>::max()))
                lua_pushnumber(state, static_cast<lua_Number>(value));
            else
                lua_pushinteger(state, static_cast<lua_Integer>(value));
            return;
        }
        case FieldKind::floating: {
            if (field.size == sizeof(float)) {
                float raw = 0;
                std::memcpy(&raw, address, sizeof(raw));
                lua_pushnumber(state, static_cast<lua_Number>(raw));
            } else if (field.size == sizeof(double)) {
                double raw = 0;
                std::memcpy(&raw, address, sizeof(raw));
                lua_pushnumber(state, static_cast<lua_Number>(raw));
            } else {
                lua_pushnil(state);
            }
            return;
        }
        case FieldKind::boolean: {
            uint8_t raw = 0;
            std::memcpy(&raw, address, sizeof(raw));
            lua_pushboolean(state, raw != 0);
            return;
        }
        case FieldKind::pointer: {
            uintptr_t raw = 0;
            if (field.size > sizeof(raw)) {
                lua_pushnil(state);
                return;
            }
            std::memcpy(&raw, address, field.size);
            lua_pushinteger(state, static_cast<lua_Integer>(raw));
            return;
        }
        case FieldKind::unknown: lua_pushnil(state); return;
    }
    lua_pushnil(state);
}

void store_native_value(lua_State *state, void *address, const Field &field) {
    switch (field.kind) {
        case FieldKind::integer_signed:
        case FieldKind::integer_unsigned: {
            const lua_Integer value = luaL_checkinteger(state, 3);
            switch (field.size) {
                case 1: {
                    const int8_t raw = static_cast<int8_t>(value);
                    std::memcpy(address, &raw, sizeof(raw));
                    return;
                }
                case 2: {
                    const int16_t raw = static_cast<int16_t>(value);
                    std::memcpy(address, &raw, sizeof(raw));
                    return;
                }
                case 4: {
                    const int32_t raw = static_cast<int32_t>(value);
                    std::memcpy(address, &raw, sizeof(raw));
                    return;
                }
                case 8: {
                    const int64_t raw = static_cast<int64_t>(value);
                    std::memcpy(address, &raw, sizeof(raw));
                    return;
                }
                default: luaL_error(state, "field '%s' has unsupported width %u", field.name, field.size); return;
            }
        }
        case FieldKind::floating: {
            const lua_Number value = luaL_checknumber(state, 3);
            if (field.size == sizeof(float)) {
                const float raw = static_cast<float>(value);
                std::memcpy(address, &raw, sizeof(raw));
            } else if (field.size == sizeof(double)) {
                const double raw = static_cast<double>(value);
                std::memcpy(address, &raw, sizeof(raw));
            } else {
                luaL_error(state, "field '%s' has unsupported float width %u", field.name, field.size);
            }
            return;
        }
        case FieldKind::boolean: {
            const uint8_t raw = lua_toboolean(state, 3) != 0 ? 1 : 0;
            std::memcpy(address, &raw, sizeof(raw));
            return;
        }
        case FieldKind::pointer: {
            const lua_Integer value = luaL_checkinteger(state, 3);
            const uintptr_t raw = static_cast<uintptr_t>(value);
            if (field.size > sizeof(raw)) {
                luaL_error(state, "field '%s' has unsupported pointer width %u", field.name, field.size);
                return;
            }
            std::memcpy(address, &raw, field.size);
            return;
        }
        case FieldKind::unknown: luaL_error(state, "field '%s' is not writable", field.name); return;
    }
}

int type_index(lua_State *state) {
    const Field *field = lookup_field(state);
    if (field == nullptr) {
        lua_pushnil(state);
        return 1;
    }
    const void *base = lua_touserdata(state, 1);
    if (base == nullptr) {
        lua_pushnil(state);
        return 1;
    }
    push_native_value(state, static_cast<const uint8_t *>(base) + field->offset, *field);
    return 1;
}

int type_newindex(lua_State *state) {
    const Field *field = lookup_field(state);
    if (field == nullptr) {
        const char *key = lua_tostring(state, 2);
        return luaL_error(state, "unknown field '%s'", key == nullptr ? "?" : key);
    }
    void *base = lua_touserdata(state, 1);
    if (base == nullptr) return luaL_error(state, "field access on a non-userdata value");
    store_native_value(state, static_cast<uint8_t *>(base) + field->offset, *field);
    return 0;
}

int type_gc(lua_State *) { return 0; }

}   // namespace

void register_contract_types(lua_State *state, const Contract &contract) {
    if (contract.types == nullptr) return;
    for (size_t i = 0; i < contract.type_count; ++i) {
        const Type &type = contract.types[i];
        const std::string name = type_metatable_name(contract, type);
        luaL_newmetatable(state, name.c_str());

        // Pre-build the field index once; __index/__newindex then do a single
        // rawget and never scan the descriptor list.
        lua_newtable(state);
        for (size_t j = 0; j < type.field_count; ++j) {
            const Field &field = type.fields[j];
            lua_pushlightuserdata(state, const_cast<Field *>(&field));
            lua_setfield(state, -2, field.name);
        }

        lua_pushvalue(state, -1);
        lua_pushcclosure(state, type_index, 1);
        lua_setfield(state, -3, "__index");

        lua_pushvalue(state, -1);
        lua_pushcclosure(state, type_newindex, 1);
        lua_setfield(state, -3, "__newindex");

        lua_pushcfunction(state, type_gc);
        lua_setfield(state, -3, "__gc");

        lua_pop(state, 2);
    }
}

void register_direct(lua_State *state, const Contract &contract) {
    lua_newtable(state);
    for (size_t i = 0; i < contract.entry_count; ++i) {
        lua_pushcfunction(state, contract.entries[i].function);
        lua_setfield(state, -2, contract.entries[i].name);
    }
    set_string_field(state, "_module", contract.module);
    set_string_field(state, "_abi_hash", contract.abi_hash);
    set_string_field(state, "_layer", "l0");
}

void register_meta(lua_State *state, const Contract &contract) {
    lua_newtable(state);
    set_string_field(state, "_module", contract.module);
    set_string_field(state, "_abi_hash", contract.abi_hash);
    set_string_field(state, "_layer", "l1");

    // Aue-1: pre-parse the ABIX contract once at registration. L1 resolves
    // names through this table instead of scanning the contract on every
    // cold access, while the module table stays empty until first use.
    lua_newtable(state);
    for (size_t i = 0; i < contract.entry_count; ++i) {
        lua_pushcfunction(state, contract.entries[i].function);
        lua_setfield(state, -2, contract.entries[i].name);
    }

    lua_newtable(state);
    lua_pushvalue(state, -2);
    lua_pushcclosure(state, meta_index, 1);
    lua_setfield(state, -2, "__index");
    lua_setmetatable(state, -3);
    lua_pop(state, 1);
}

const char *level_name(Level level) noexcept {
    switch (level) {
        case Level::l0: return "l0";
        case Level::l1: return "l1";
        case Level::l2: return "l2";
    }
    return "unknown";
}

Level register_layer(lua_State *state, const Contract &contract, Level requested) {
    const Level selected = select_level(requested);
    if (selected == Level::l1) {
        register_meta(state, contract);
    } else {
        // L0 also covers a requested L2 that this build cannot provide.
        register_direct(state, contract);
    }
    return selected;
}

}   // namespace aue
