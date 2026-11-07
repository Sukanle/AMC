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

uint32_t hash_name(const char *name) {
    uint32_t hash = 2'166'136'261u;
    for (const unsigned char *p = reinterpret_cast<const unsigned char *>(name); *p != 0; ++p) {
        hash ^= *p;
        hash *= 16'777'619u;
    }
    return hash;
}

// C-side open-addressed index. The flexible slot array is allocated in the same
// Lua userdata, so the closure upvalue owns and releases it.
struct NativeIndex {
    size_t capacity;
    const Entry *slots[1];
};

int native_index(lua_State *state) {
    const char *key = lua_tostring(state, 2);
    if (key == nullptr) {
        lua_pushnil(state);
        return 1;
    }
    const auto *index = static_cast<const NativeIndex *>(lua_touserdata(state, lua_upvalueindex(1)));
    if (index == nullptr || index->capacity == 0) {
        lua_pushnil(state);
        return 1;
    }
    const size_t mask = index->capacity - 1;
    size_t slot = static_cast<size_t>(hash_name(key)) & mask;
    for (size_t probe = 0; probe < index->capacity; ++probe) {
        const Entry *entry = index->slots[slot];
        if (entry == nullptr) break;
        if (std::strcmp(entry->name, key) == 0) {
            lua_pushcfunction(state, entry->function);
            return 1;
        }
        slot = (slot + 1) & mask;
    }
    lua_pushnil(state);
    return 1;
}

constexpr uint64_t view_magic = 0x41554556494557ULL;   // "AUEVIEW"

// A non-owning view into a parent userdata. The parent is kept alive through
// uservalue 1, so nested record/array views cannot outlive their storage.
struct View {
    uint64_t magic;
    void *base;
};

struct ArrayView {
    uint64_t magic;
    void *base;
    uint32_t count;
    uint32_t stride;
    FieldKind element_kind;
    uint32_t element_size;
    const Type *element_type;
    const Contract *contract;
};

void *object_base(lua_State *state) {
    void *object = lua_touserdata(state, 1);
    if (object == nullptr) return nullptr;
    if (lua_rawlen(state, 1) == sizeof(View) && static_cast<const View *>(object)->magic == view_magic)
        return static_cast<const View *>(object)->base;
    return object;
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
        // Record/array/opaque fields are materialized by type_index rather than
        // read as scalars; keep them inaccessible if one reaches this path.
        case FieldKind::record:
        case FieldKind::array:
        case FieldKind::opaque:
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
        // Record/array/opaque fields are rejected by type_newindex and
        // array_newindex before reaching this scalar store path.
        case FieldKind::record:
        case FieldKind::array:
        case FieldKind::opaque:
        case FieldKind::unknown: luaL_error(state, "field '%s' is not writable", field.name); return;
    }
}

void push_record_view(lua_State *state, void *base, const Type &type, const Contract &contract) {
    auto *view = static_cast<View *>(lua_newuserdatauv(state, sizeof(View), 1));
    view->magic = view_magic;
    view->base = base;
    lua_pushvalue(state, 1);
    lua_setiuservalue(state, -2, 1);

    const std::string name = type_metatable_name(contract, type);
    luaL_getmetatable(state, name.c_str());
    lua_setmetatable(state, -2);
}

void push_array_view(lua_State *state, void *base, const Field &field, const Contract &contract) {
    auto *view = static_cast<ArrayView *>(lua_newuserdatauv(state, sizeof(ArrayView), 1));
    view->magic = view_magic;
    view->base = base;
    view->count = field.count;
    view->stride = field.stride;
    view->element_kind = field.element_kind;
    view->element_size = field.element_size;
    view->element_type = field.type_index < contract.type_count ? &contract.types[field.type_index] : nullptr;
    view->contract = &contract;
    lua_pushvalue(state, 1);
    lua_setiuservalue(state, -2, 1);

    luaL_getmetatable(state, "aue.array");
    lua_setmetatable(state, -2);
}

int array_index(lua_State *state) {
    auto *view = static_cast<ArrayView *>(lua_touserdata(state, 1));
    if (view == nullptr || view->magic != view_magic) {
        lua_pushnil(state);
        return 1;
    }
    const lua_Integer index = luaL_checkinteger(state, 2);
    if (index < 1 || index > static_cast<lua_Integer>(view->count)) {
        lua_pushnil(state);
        return 1;
    }
    auto *address = static_cast<uint8_t *>(view->base) + static_cast<size_t>(index - 1) * view->stride;
    if (view->element_kind == FieldKind::record && view->element_type != nullptr && view->contract != nullptr) {
        push_record_view(state, address, *view->element_type, *view->contract);
        return 1;
    }
    if (view->element_kind == FieldKind::unknown
        || view->element_kind == FieldKind::array
        || view->element_kind == FieldKind::opaque) {
        return luaL_error(state, "array element type is not described by the contract");
    }
    Field element{"[]", 0, view->element_size, view->element_kind};
    push_native_value(state, address, element);
    return 1;
}

int array_newindex(lua_State *state) {
    auto *view = static_cast<ArrayView *>(lua_touserdata(state, 1));
    if (view == nullptr || view->magic != view_magic) return luaL_error(state, "invalid array view");
    const lua_Integer index = luaL_checkinteger(state, 2);
    if (index < 1 || index > static_cast<lua_Integer>(view->count))
        return luaL_error(state, "array index %d is out of range", static_cast<int>(index));
    if (view->element_kind == FieldKind::record
        || view->element_kind == FieldKind::array
        || view->element_kind == FieldKind::opaque
        || view->element_kind == FieldKind::unknown) {
        return luaL_error(state, "array element type is not directly writable");
    }
    auto *address = static_cast<uint8_t *>(view->base) + static_cast<size_t>(index - 1) * view->stride;
    Field element{"[]", 0, view->element_size, view->element_kind};
    store_native_value(state, address, element);
    return 0;
}

int array_len(lua_State *state) {
    const auto *view = static_cast<const ArrayView *>(lua_touserdata(state, 1));
    lua_pushinteger(state, view != nullptr && view->magic == view_magic ? static_cast<lua_Integer>(view->count) : 0);
    return 1;
}

int array_gc(lua_State *) { return 0; }

int type_index(lua_State *state) {
    const Field *field = lookup_field(state);
    if (field == nullptr) {
        lua_pushnil(state);
        return 1;
    }
    void *base = object_base(state);
    if (base == nullptr) {
        lua_pushnil(state);
        return 1;
    }
    const auto *contract = static_cast<const Contract *>(lua_touserdata(state, lua_upvalueindex(2)));
    void *address = static_cast<uint8_t *>(base) + field->offset;

    switch (field->kind) {
        case FieldKind::record:
            if (contract != nullptr && field->type_index < contract->type_count) {
                push_record_view(state, address, contract->types[field->type_index], *contract);
            } else {
                lua_pushnil(state);
            }
            return 1;
        case FieldKind::array:
            if (contract != nullptr)
                push_array_view(state, address, *field, *contract);
            else
                lua_pushnil(state);
            return 1;
        case FieldKind::opaque:
        case FieldKind::unknown: lua_pushnil(state); return 1;
        default:                 push_native_value(state, address, *field); return 1;
    }
}

int type_newindex(lua_State *state) {
    const Field *field = lookup_field(state);
    if (field == nullptr) {
        const char *key = lua_tostring(state, 2);
        return luaL_error(state, "unknown field '%s'", key == nullptr ? "?" : key);
    }
    void *base = object_base(state);
    if (base == nullptr) return luaL_error(state, "field access on a non-userdata value");
    if (field->kind == FieldKind::record
        || field->kind == FieldKind::array
        || field->kind == FieldKind::opaque
        || field->kind == FieldKind::unknown) {
        return luaL_error(state, "field '%s' cannot be written as a whole", field->name);
    }
    store_native_value(state, static_cast<uint8_t *>(base) + field->offset, *field);
    return 0;
}

int type_gc(lua_State *) { return 0; }

int destroy_gc(lua_State *state) {
    auto *destroy = static_cast<Destroy *>(lua_touserdata(state, lua_upvalueindex(1)));
    if (destroy != nullptr && *destroy != nullptr) {
        void *object = lua_touserdata(state, 1);
        if (object != nullptr
            && !(lua_rawlen(state, 1) == sizeof(View) && static_cast<const View *>(object)->magic == view_magic)) {
            (*destroy)(object);
        }
    }
    return 0;
}

}   // namespace

void register_contract_types(lua_State *state, const Contract &contract) {
    if (contract.types == nullptr) return;

    if (luaL_newmetatable(state, "aue.array") != 0) {
        lua_pushcfunction(state, array_index);
        lua_setfield(state, -2, "__index");
        lua_pushcfunction(state, array_newindex);
        lua_setfield(state, -2, "__newindex");
        lua_pushcfunction(state, array_len);
        lua_setfield(state, -2, "__len");
        lua_pushcfunction(state, array_gc);
        lua_setfield(state, -2, "__gc");
    }
    lua_pop(state, 1);

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
        lua_pushlightuserdata(state, const_cast<Contract *>(&contract));
        lua_pushcclosure(state, type_index, 2);
        lua_setfield(state, -3, "__index");

        lua_pushvalue(state, -1);
        lua_pushlightuserdata(state, const_cast<Contract *>(&contract));
        lua_pushcclosure(state, type_newindex, 2);
        lua_setfield(state, -3, "__newindex");

        lua_pushcfunction(state, type_gc);
        lua_setfield(state, -3, "__gc");

        lua_pop(state, 2);
    }
}

void register_type_destroy(lua_State *state, const char *type_name, Destroy destroy) {
    if (type_name == nullptr || destroy == nullptr) return;
    luaL_getmetatable(state, type_name);
    if (!lua_istable(state, -1)) {
        lua_pop(state, 1);
        return;
    }
    auto *box = static_cast<Destroy *>(lua_newuserdatauv(state, sizeof(Destroy), 0));
    *box = destroy;
    lua_pushcclosure(state, destroy_gc, 1);
    lua_setfield(state, -2, "__gc");
    lua_pop(state, 1);
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

void register_native(lua_State *state, const Contract &contract) {
    size_t capacity = 8;
    const size_t wanted = contract.entry_count > 0 ? contract.entry_count * 2 : 1;
    while (capacity < wanted)
        capacity <<= 1;

    auto *index =
        static_cast<NativeIndex *>(lua_newuserdatauv(state, sizeof(NativeIndex) + capacity * sizeof(const Entry *), 0));
    index->capacity = capacity;
    std::memset(index->slots, 0, capacity * sizeof(const Entry *));

    const size_t mask = capacity - 1;
    for (size_t i = 0; i < contract.entry_count; ++i) {
        const Entry &entry = contract.entries[i];
        size_t slot = static_cast<size_t>(hash_name(entry.name)) & mask;
        while (index->slots[slot] != nullptr)
            slot = (slot + 1) & mask;
        index->slots[slot] = &entry;
    }

    lua_newtable(state);
    set_string_field(state, "_module", contract.module);
    set_string_field(state, "_abi_hash", contract.abi_hash);
    set_string_field(state, "_layer", "l2");

    lua_newtable(state);
    lua_pushvalue(state, -3);
    lua_pushcclosure(state, native_index, 1);
    lua_setfield(state, -2, "__index");
    lua_setmetatable(state, -2);
    lua_remove(state, -2);   // keep the module table as the return value
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
    switch (selected) {
        case Level::l2: register_native(state, contract); break;
        case Level::l1: register_meta(state, contract); break;
        case Level::l0: register_direct(state, contract); break;
    }
    return selected;
}

}   // namespace aue
