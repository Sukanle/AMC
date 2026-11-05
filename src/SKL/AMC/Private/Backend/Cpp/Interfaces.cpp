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
#include "SKL/AMC/Private/Backend/Cpp/Emitters.h"

#include "SKL/AMC/Private/Backend/Cpp/CppNames.h"
#include "SKL/AMC/Core/Core.h"

#include <string>
#include <vector>

namespace amc::lang::backend::cpp {

void Emit::contract(const amc::InterfaceRecord &record) {
    const std::string name = cpp_name(record.name);
    // Methods addressed by slot, so a non-contiguous table is still emitted
    // in slot order.
    std::vector<const amc::InterfaceMethod *> by_slot;
    for (uint32_t i = 0; i < record.method_count; ++i) {
        const auto &method = _m.interface_methods[record.method_begin + i];
        if (method.slot >= by_slot.size()) by_slot.resize(method.slot + 1, nullptr);
        by_slot[method.slot] = &method;
    }
    _o.print(
        "\n"
        "// Interface contract `{0}` (version {1}).\n"
        "inline constexpr ::skl::abix::poly::InterfaceId k{2}Interface{{{3}, {4}}};\n"
        "inline constexpr uint32_t k{2}Version = {1};\n",
        record.name, record.version, name, hex_u64(record.id.lo), hex_u64(record.id.hi));

    for (const auto *method : by_slot) {
        if (method == nullptr) continue;
        std::string params;
        for (const auto &parameter : method->parameters) {
            if (!params.empty()) params += ", ";
            params += cpp_type_of(_m, parameter.type_id);
        }
        _o.print("using {}_{}_fn = {} (*)({});\n", name, cpp_name(method->name), cpp_type_of(_m, method->return_type),
            params);
    }

    // Traits: the contract facts a proxy needs. Signatures double as the slot
    // capacity, so a sparse table keeps a zero hash for every unbound slot.
    _o.print("struct {}Traits {{\n", name);
    if (by_slot.empty()) {
        _o.print(
            "    static constexpr ::skl::mics::util::array<::skl::abix::poly::InterfaceMethodHash, 0> signatures = "
            "{{}};\n");
    } else {
        _o.print("    static constexpr ::skl::mics::util::array signatures = {{\n");
        for (const auto *method : by_slot) {
            if (method == nullptr)
                _o.print("        ::skl::abix::poly::InterfaceMethodHash{{0ULL, 0ULL}},\n");
            else
                _o.print("        ::skl::abix::poly::InterfaceMethodHash{{{}, {}}},\n", hex_u64(method->signature.lo),
                    hex_u64(method->signature.hi));
        }
        _o.print("    }};\n");
    }
    _o.print(
        "    static constexpr uint32_t version = k{0}Version;\n"
        "    static constexpr ::skl::abix::poly::InterfaceId id = k{0}Interface;\n"
        "    struct alignas(8) Slots {{\n",
        name);
    for (size_t slot = 0; slot < by_slot.size(); ++slot) {
        const auto *method = by_slot[slot];
        if (method != nullptr)
            _o.print("        uint32_t {};\n", cpp_name(method->name));
        else
            _o.print("        uint32_t reserved_slot_{};\n", slot);
    }
    _o.print("    }};\n");
    _o.print(
        "    alignas(8) static constexpr Slots slots = "
        "::skl::mics::util::make_iota<Slots, signatures.size(), 0>();\n");
    _o.print("}};\n");

    if (!interface_has_handle_methods(_m, record)) {
        _o.print("// No proxy class: a method does not take the object handle as its first parameter.\n");
        return;
    }
    _o.print(
        "class {0} : public ::skl::abix::poly::UniqueInterfaceProxy<{0}Traits> {{\n"
        "    using Base = ::skl::abix::poly::UniqueInterfaceProxy<{0}Traits>;\n\n"
        "public:\n"
        "    using Base::Base;\n",
        name);
    for (const auto *method : by_slot) {
        if (method == nullptr) continue;
        const std::string mname = cpp_name(method->name);
        const std::string ret = cpp_type_of(_m, method->return_type);
        std::string decl, forward;
        for (size_t p = 1; p < method->parameters.size(); ++p) {
            const std::string arg = fmt::format("arg{}", p - 1);
            if (!decl.empty()) {
                decl += ", ";
                forward += ", ";
            }
            decl += cpp_type_of(_m, method->parameters[p].type_id) + " " + arg;
            forward += arg;
        }
        if (forward.empty())
            _o.print(
                "    {0} {1}({2}) const noexcept {{ return this->template call<{3}_{1}_fn>({3}Traits::slots.{1}); }}\n",
                ret, mname, decl, name);
        else
            _o.print(
                "    {0} {1}({2}) const noexcept {{ return this->template call<{3}_{1}_fn>({3}Traits::slots.{1}, {4}); "
                "}}\n",
                ret, mname, decl, name, forward);
    }
    _o.print("}};\n");
}


void Emit::interfaces() {
    // Interface contracts (`[[interface]]` / `[[implements]]`): a named, versioned
    // contract, not a private vtable. For each contract the projection emits its
    // identity, version, adapter typedefs and a `Traits` struct with a typed
    // zero-based slot table (`Traits::slots.<method>`) plus the expected
    // signatures. When every method takes the object handle first it also emits
    // a proxy class derived from `UniqueInterfaceProxy<Traits>` — so a consumer can
    // bind, verify and call through a header-declared interface without touching
    // the provider's layout.
    for (const auto &record : _m.interfaces)
        contract(record);
}

}   // namespace amc::lang::backend::cpp
