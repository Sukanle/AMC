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
#include "SKL/AMC/Private/Frontend/Cpp/Thunks.h"

#include "SKL/AMC/Core/Core.h"
#include "SKL/AMC/Core/Generated.h"
#include "SKL/AMC/Semantic/Model.h"

#include <llvm/ADT/SmallString.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/Path.h>
#include <llvm/Support/raw_ostream.h>

#include <algorithm>
#include <cctype>
#include <fmt/format.h>
#include <string>
#include <utility>
#include <vector>

namespace amc::lang::frontend::cpp {

ThunkEmitter::ThunkEmitter(amc::AbiModule &module, const amc::lang::Config &config,
    std::vector<NativeAdapt> &adaptations, llvm::StringRef base, amc::semantic::SemanticModule &semantic)
    : module_(module)
    , config_(config)
    , adaptations_(adaptations)
    , base_(base)
    , semantic_(semantic) {}

// P0 automatic capability generation: emit C thunks that expose selected
// methods of a concrete type, then register them as ABI entities so any
// consumer backend can bind them. The producer compiles the generated source
// into its module; neither side writes an adapter by hand.
//
// Only public members are supported, and only representations the bridge can
// express: a parameter or return type that is not a known, layout-safe type is
// rejected rather than guessed at.
// Spelling for a generated `#include`: prefer the path relative to the config
// directory, then relative to any `-I` directory declared in the import flags,
// and only fall back to the declared string. This keeps generated thunks free
// of machine-specific absolute prefixes while still resolving at compile time.
static std::string include_spelling(
    const std::string &file, llvm::StringRef base, const std::vector<std::string> &flags) {
    llvm::SmallString<256> declared(file);
    if (llvm::sys::path::is_relative(declared)) return file;   // already relative to the config dir
    const auto under = [&](llvm::StringRef root) -> std::string {
        llvm::SmallString<256> root_abs(root);
        if (const std::error_code ec = llvm::sys::fs::make_absolute(root_abs)) return {};
        llvm::sys::path::remove_dots(root_abs, true);
        llvm::SmallString<256> declared_abs(declared);
        if (const std::error_code ec = llvm::sys::fs::make_absolute(declared_abs)) return {};
        llvm::sys::path::remove_dots(declared_abs, true);
        llvm::StringRef root_ref(root_abs);
        llvm::StringRef declared_ref(declared_abs);
        if (!declared_ref.starts_with(root_ref)) return {};
        llvm::StringRef rel = declared_ref.drop_front(root_ref.size());
        while (!rel.empty() && llvm::sys::path::is_separator(rel.front()))
            rel = rel.drop_front();
        if (rel.empty() || rel.starts_with("..")) return {};
        std::string result = rel.str();
        for (char &c : result)
            if (c == '\\') c = '/';
        return result;
    };
    if (const std::string rel = under(base); !rel.empty()) return rel;
    for (const std::string &flag : flags) {
        if (flag.rfind("-I", 0) != 0 || flag.size() <= 2) continue;
        if (const std::string rel = under(flag.substr(2)); !rel.empty()) return rel;
    }
    return file;
}

bool ThunkEmitter::run(std::string &error) {
    amc::AbiModule &module = module_;
    const amc::lang::Config &config = config_;
    std::vector<NativeAdapt> &adaptations = adaptations_;
    llvm::StringRef base = base_;
    amc::semantic::SemanticModule &semantic = semantic_;

    if (config.exposed.empty()) return true;
    if (config.thunks_output.empty()) {
        error = "[[expose]] requires a [thunks] output path";
        return false;
    }
    for (const auto &adapt : config.adaptations) {
        const NativeKind kind = native_kind_of(adapt.native);
        if (kind == NativeKind::none) {
            error = "[[adapt]] native kind is not supported: " + adapt.native;
            return false;
        }
        if (adapt.target.empty()) {
            error = "[[adapt]] requires a boundary target type";
            return false;
        }
        if ((kind == NativeKind::vector || kind == NativeKind::span) && adapt.element.empty()) {
            error = "[[adapt]] requires an element spelling for " + adapt.native;
            return false;
        }
    }
    uint32_t pointer_size = 8;
    for (const auto &type : module.types)
        if (type.kind == amc::TypeKind::pointer) {
            pointer_size = type.size;
            break;
        }
    const auto intern = [&](const std::string &name, amc::TypeKind kind, uint32_t size, uint32_t align,
                            uint32_t primitive) -> amc::Hash128 {
        for (const auto &type : module.types)
            if (type.name == name) return type.id;
        amc::Type type;
        type.name = name;
        type.kind = kind;
        type.size = size;
        type.align = align;
        type.primitive_abi = primitive;
        type.id = amc::hash_text(name, 0x54595045);
        module.types.push_back(std::move(type));
        return module.types.back().id;
    };
    const amc::Hash128 void_id =
        intern("void", amc::TypeKind::primitive, 0, 1, amc::primitive_abi(0, amc::PrimitiveAbiKind::void_));
    const auto type_name = [&](amc::Hash128 id) -> std::string {
        for (const auto &type : module.types)
            if (type.id == id) return type.name;
        return {};
    };
    // Synthesize the ABIX-STD boundary type an adapted parameter is marshalled
    // onto, when the module does not already declare it. The layout mirrors
    // `SKL/ABIX/STD/{String,Span}.h` (the generated thunk includes the real header,
    // so the compiled parameter type is the header's); a consumer projects the
    // boundary by name. An existing type of the same name is reused untouched.
    const auto ensure_pointer = [&](const std::string &name, amc::Hash128 pointee) -> amc::Hash128 {
        const amc::Hash128 id = intern(name, amc::TypeKind::pointer, pointer_size, pointer_size, 0);
        for (const auto &ref : module.pointees)
            if (ref.pointer == id) return id;
        module.pointees.push_back({id, pointee, amc::pointee_const});
        return id;
    };
    const amc::Hash128 len_id =
        intern("unsigned long", amc::TypeKind::primitive, 8, 8, amc::primitive_abi(8, amc::PrimitiveAbiKind::uint));
    const amc::Hash128 encoding_id =
        intern("unsigned int", amc::TypeKind::primitive, 4, 4, amc::primitive_abi(4, amc::PrimitiveAbiKind::uint));
    const auto ensure_boundary = [&](NativeAdapt &adapt) -> amc::Hash128 {
        for (const auto &type : module.types)
            if (type.name == adapt.target) return type.id;
        const bool string_view = adapt.kind == static_cast<uint32_t>(NativeKind::string)
                              || adapt.kind == static_cast<uint32_t>(NativeKind::string_view);
        const std::string element = adapt.element.empty() ? "char" : adapt.element;
        const amc::Hash128 data_id = ensure_pointer("const " + element + "*", adapt.element_id);
        amc::Type type;
        type.name = adapt.target;
        type.kind = amc::TypeKind::record;
        type.id = amc::hash_text(type.name, 0x54595045);
        type.field_begin = static_cast<uint32_t>(module.fields.size());
        module.fields.push_back({type.id, "data", data_id, 0, 0});
        module.fields.push_back({type.id, "len", len_id, pointer_size, 0});
        uint32_t end = pointer_size + 8;
        if (string_view) {
            module.fields.push_back({type.id, "encoding", encoding_id, pointer_size + 8, 0});
            // v0.4.0 makes the former implicit trailing padding explicit.
            module.fields.push_back({type.id, "reserved", encoding_id, pointer_size + 12, 0});
            end = pointer_size + 16;
        }
        type.field_count = static_cast<uint32_t>(module.fields.size()) - type.field_begin;
        type.align = pointer_size;
        type.size = (end + pointer_size - 1) / pointer_size * pointer_size;
        std::vector<amc::Field> layout_fields(
            module.fields.begin() + type.field_begin, module.fields.begin() + type.field_begin + type.field_count);
        type.layout_hash = amc::canonical_layout_hash(module, type, layout_fields);
        module.types.push_back(std::move(type));
        return module.types.back().id;
    };
    // The C++ expression that marshals one boundary value at the call site.
    const auto adapt_expression = [&](const NativeAdapt &adapt, const std::string &arg) -> std::string {
        switch (static_cast<NativeKind>(adapt.kind)) {
            case NativeKind::string:      return "std::string(" + arg + ".data, " + arg + ".len)";
            case NativeKind::string_view: return "std::string_view(" + arg + ".data, " + arg + ".len)";
            case NativeKind::vector:
                return "std::vector<" + adapt.element + ">(" + arg + ".data, " + arg + ".data + " + arg + ".len)";
            case NativeKind::span: return "std::span<const " + adapt.element + ">(" + arg + ".data, " + arg + ".len)";
            default:               return arg;
        }
    };
    // Adaptation lookups are by the recorded native parameter type.
    const auto find_adapt = [&](amc::Hash128 native) -> NativeAdapt * {
        for (auto &adapt : adaptations)
            if (adapt.native_type == native) return &adapt;
        return nullptr;
    };
    // A boundary type is representable without an explicit contract when it is a
    // scalar, a pointer, an enumeration, an array, an ABIX-STD boundary type, or
    // a plain value record. STL types and template specializations are not:
    // passing them raw across a C ABI is unsafe, so they require a contract (for
    // example a StringView mapping) instead of guessing.
    const auto representable = [&](amc::Hash128 id) -> bool {
        for (const auto &type : module.types) {
            if (!(type.id == id)) continue;
            const std::string &name = type.name;
            if (name.rfind("abix_", 0) == 0) return true;   // ABIX-STD boundary type
            // STL types, template spellings and references must not cross the
            // boundary raw (they need a contract such as a StringView mapping).
            if (name.find("std::") != std::string::npos) return false;
            if (name.find('<') != std::string::npos) return false;
            if (name.find('&') != std::string::npos) return false;
            return true;
        }
        return false;
    };
    // A recorded primitive may carry a C spelling (e.g. `_Bool`); the generated
    // C++ thunk needs the C++ spelling.
    const auto cpp_spelling = [](std::string name) {
        if (name == "_Bool") return std::string("bool");
        return name;
    };
    const auto symbol_of = [](const std::string &name) {
        std::string out;
        for (const char c : name)
            out += (std::isalnum(static_cast<unsigned char>(c)) != 0) ? c : '_';
        return out;
    };

    std::string source =
        AMC_GENERATED_FILE_WARNING("//")
        "// Generated by `amc` - C thunks exposing C++ types as ABIX capabilities.\n"
        "// Build artifact: compile it into the producing module.\n"
        "#include \"SKL/ABIX/Interface/Interface.h\"\n";
    // A declared native↔ABIX-STD adaptation needs the boundary header for the
    // spelling it puts in the thunk signature.
    bool wants_string = false, wants_span = false;
    for (const auto &adapt : config.adaptations) {
        const NativeKind kind = native_kind_of(adapt.native);
        if (kind == NativeKind::string || kind == NativeKind::string_view) wants_string = true;
        if (kind == NativeKind::vector || kind == NativeKind::span) wants_span = true;
    }
    if (wants_string) source += "#include \"SKL/ABIX/STD/String.h\"\n";
    if (wants_span) source += "#include \"SKL/ABIX/STD/Span.h\"\n";
    for (const auto &file : config.files)
        source += "#include \"" + include_spelling(file, base, config.flags) + "\"\n";
    source += "\n";
    // Interface manifest glue (discovered through `abix_module_query_v1`).
    std::string manifest;
    std::vector<std::string> interface_names;

    // Automatic ABIX-STD contract mapping: when an exposed method uses an
    // ABIX-STD boundary type, record its contract (SequenceView / StringView /
    // Result) so a consumer knows the semantics, not just the layout.
    std::vector<std::pair<amc::Hash128, uint32_t>> recorded_contracts;
    const auto record_contract = [&](amc::Hash128 id) -> amc::LoweringResult {
        const std::string name = type_name(id);
        amc::LoweringResult result;
        if (name.rfind("abix_span_", 0) == 0)
            result = amc::evaluate_sequence_view(module, id, amc::Hash128{}, "", false);
        else if (name.rfind("abix_str_", 0) == 0)
            result = amc::evaluate_string_view(module, id, amc::Hash128{}, "", false);
        else if (name.rfind("abix_result_", 0) == 0)
            result = amc::evaluate_result(module, id, amc::Hash128{}, "");
        else
            return {};
        for (const auto &seen : recorded_contracts)
            if (seen.first == id && seen.second == result.kind) return result;
        recorded_contracts.emplace_back(id, result.kind);
        module.lowerings.push_back(result);
        return result;
    };

    // Methods may be inherited: collect a type's base classes so an exposed
    // concrete type can list methods declared on a base (e.g. MemoryHistory's
    // insert/length/clear come from History).
    const auto collect_bases = [&](amc::Hash128 root) {
        std::vector<amc::Hash128> out;
        std::vector<amc::Hash128> work{root};
        while (!work.empty()) {
            const amc::Hash128 id = work.back();
            work.pop_back();
            for (const auto &type : module.types) {
                if (!(type.id == id)) continue;
                for (uint32_t i = 0; i < type.field_count; ++i) {
                    const auto &field = module.fields[type.field_begin + i];
                    if ((field.flags & amc::field_base) == 0) continue;
                    if (std::find(out.begin(), out.end(), field.type_id) != out.end()) continue;
                    out.push_back(field.type_id);
                    work.push_back(field.type_id);
                }
            }
        }
        return out;
    };

    for (const auto &expose : config.exposed) {
        amc::Type *record = nullptr;
        for (auto &type : module.types)
            if (type.name == expose.type || type.name == "::" + expose.type) {
                record = &type;
                break;
            }
        if (record == nullptr) {
            error = "[[expose]] references an unknown type: " + expose.type;
            return false;
        }
        const amc::Hash128 type_id = record->id;
        const std::string cpp_type = record->name;
        const std::string symbol_type = symbol_of(record->name);
        amc::semantic::ExposedType exposed_contract;
        exposed_contract.abi_type = type_id;
        exposed_contract.qualified_name = cpp_type;
        exposed_contract.symbol = symbol_type;
        exposed_contract.has_constructor = expose.has_constructor;
        exposed_contract.opaque = expose.opaque;
        if (expose.opaque) record->flags |= amc::type_opaque;
        const std::vector<amc::Hash128> bases = collect_bases(type_id);
        // A base subobject is part of the exposed opaque handle: project it as an
        // opaque blob too, never reconstructing its (possibly STL-heavy) layout.
        for (auto &type : module.types)
            if (std::find(bases.begin(), bases.end(), type.id) != bases.end()) type.flags |= amc::type_opaque;
        const auto is_method_of_type = [&](const amc::Function &function) {
            if (function.owner_type == type_id) return true;
            return std::find(bases.begin(), bases.end(), function.owner_type) != bases.end();
        };
        const amc::Hash128 self_id = intern(cpp_type + "*", amc::TypeKind::pointer, pointer_size, pointer_size, 0);

        const auto register_thunk = [&](const std::string &name, amc::Hash128 ret,
                                        const std::vector<std::pair<std::string, amc::Hash128>> &params) {
            amc::Function function;
            function.name = name;
            function.calling_convention = 1;   // cdecl
            function.return_type = ret;
            for (const auto &parameter : params) {
                amc::Parameter p;
                p.name = parameter.first;
                p.type_id = parameter.second;
                function.parameters.push_back(std::move(p));
            }
            function.signature = amc::signature_hash(function);
            module.functions.push_back(std::move(function));
            return module.functions.back().signature;
        };

        amc::Hash128 create_sig{}, destroy_sig{};
        if (expose.has_constructor) {
            std::string params, args;
            std::vector<std::pair<std::string, amc::Hash128>> signature;
            for (size_t i = 0; i < expose.constructor.size(); ++i) {
                const std::string &spelling = expose.constructor[i];
                const amc::Hash128 id = amc::find_type_id_by_spelling(module, spelling);
                if (id == amc::Hash128{}) {
                    error = "[[expose]] constructor parameter type is unknown: " + spelling;
                    return false;
                }
                if (!representable(id)) {
                    error = "[[expose]] constructor parameter type needs a contract: " + spelling;
                    return false;
                }
                if (i != 0) {
                    params += ", ";
                    args += ", ";
                }
                const std::string name = "arg" + std::to_string(i);
                params += spelling + " " + name;
                args += name;
                signature.emplace_back(name, id);
            }
            source += "extern \"C\" " + cpp_type + "* abix_" + symbol_type + "_create(" + params + ") {\n";
            source += "    return new " + cpp_type + "(" + args + ");\n}\n";
            create_sig = register_thunk("abix_" + symbol_type + "_create", self_id, signature);
        }
        if (expose.destructor) {
            source += "extern \"C\" void abix_" + symbol_type + "_destroy(" + cpp_type + "* self) {\n";
            source += "    delete self;\n}\n";
            destroy_sig = register_thunk("abix_" + symbol_type + "_destroy", void_id,
                {
                    {"self", self_id}
            });
        }
        exposed_contract.create = create_sig;
        exposed_contract.destroy = destroy_sig;
        if (!(create_sig == amc::Hash128{}) && !(destroy_sig == amc::Hash128{}))
            module.owned.push_back({type_id, create_sig, destroy_sig, 0});

        struct ExposedMethod {
            std::string symbol;
            amc::Hash128 signature;
        };
        std::vector<ExposedMethod> exposed_methods;
        for (const auto &method : expose.methods) {
            std::vector<amc::Function> matches;
            for (const auto &function : module.functions) {
                if (!is_method_of_type(function)) continue;
                if (function.name == method
                    || (function.name.size() > method.size()
                        && function.name.compare(function.name.size() - method.size(), method.size(), method) == 0
                        && function.name[function.name.size() - method.size() - 1] == ':')) {
                    matches.push_back(function);
                }
            }
            if (matches.empty()) {
                error = "[[expose]] references an unknown method: " + expose.type + "::" + method;
                return false;
            }
            const auto overload_suffix = [&](const amc::Function &function) {
                if (matches.size() <= 1) return std::string{};
                return fmt::format(
                    "_{:08x}{:04x}", function.signature.lo, static_cast<uint32_t>(function.signature.hi & 0xffff));
            };
            for (const auto &candidate : matches) {
                const amc::Function *found = &candidate;
                if (!representable(found->return_type)) {
                    error = "[[expose]] "
                          + expose.type
                          + "::"
                          + method
                          + " return type needs a contract: "
                          + type_name(found->return_type);
                    return false;
                }
                for (const auto &parameter : found->parameters) {
                    NativeAdapt *adapt = find_adapt(parameter.type_id);
                    if (adapt != nullptr) {
                        // A declared native↔ABIX-STD adaptation: the thunk takes the
                        // boundary type and marshals it. Record the boundary contract
                        // and the adaptation as a needs_adapter lowering, once.
                        if (adapt->boundary_type == amc::Hash128{}) {
                            adapt->boundary_type = ensure_boundary(*adapt);
                            record_contract(adapt->boundary_type);
                            amc::LoweringResult lowering;
                            lowering.type_id = adapt->native_type;
                            lowering.contract_type = adapt->boundary_type;
                            const bool string_view = adapt->kind == static_cast<uint32_t>(NativeKind::string)
                                                  || adapt->kind == static_cast<uint32_t>(NativeKind::string_view);
                            lowering.kind = static_cast<uint32_t>(
                                string_view ? amc::LoweringKind::string_view : amc::LoweringKind::sequence_view);
                            lowering.status = static_cast<uint32_t>(amc::LoweringStatus::needs_adapter);
                            lowering.reason = static_cast<uint32_t>(amc::LoweringReason::layout);
                            module.lowerings.push_back(lowering);
                        }
                        continue;
                    }
                    if (!representable(parameter.type_id)) {
                        error = "[[expose]] "
                              + expose.type
                              + "::"
                              + method
                              + " parameter type needs a contract: "
                              + type_name(parameter.type_id);
                        return false;
                    }
                }
                const bool returns_void = found->return_type == void_id;
                std::string return_spelling = returns_void ? "void" : cpp_spelling(type_name(found->return_type));
                std::string params = cpp_type + "* self";
                std::string args;
                std::vector<std::pair<std::string, amc::Hash128>> signature = {
                    {"self", self_id}
                };
                for (size_t i = 0; i < found->parameters.size(); ++i) {
                    const auto &parameter = found->parameters[i];
                    const std::string name = parameter.name.empty() ? "arg" + std::to_string(i) : parameter.name;
                    const NativeAdapt *adapt = find_adapt(parameter.type_id);
                    const amc::Hash128 param_type = adapt != nullptr ? adapt->boundary_type : parameter.type_id;
                    params += ", " + cpp_spelling(type_name(param_type)) + " " + name;
                    if (i != 0) args += ", ";
                    args += adapt != nullptr ? adapt_expression(*adapt, name) : name;
                    signature.emplace_back(name, param_type);
                }
                const std::string suffix = overload_suffix(*found);
                source += "extern \"C\" "
                        + return_spelling
                        + " abix_"
                        + symbol_type
                        + "_"
                        + method
                        + suffix
                        + "("
                        + params
                        + ") {\n";
                source +=
                    std::string("    ") + (returns_void ? "" : "return ") + "self->" + method + "(" + args + ");\n}\n";
                const std::string symbol = "abix_" + symbol_type + "_" + method + suffix;
                // Capture the method facts before registering the thunk: `register_thunk`
                // pushes onto `module.functions`, which may reallocate the vector and
                // invalidate `found`. Reading through `found` afterwards would be a
                // use-after-free, so copy what the contract recording needs first.
                const amc::Hash128 return_type_id = found->return_type;
                std::vector<amc::Hash128> parameter_type_ids;
                parameter_type_ids.reserve(found->parameters.size());
                for (const auto &parameter : found->parameters)
                    parameter_type_ids.push_back(parameter.type_id);
                // Record the ABIX-STD contracts of the boundary types in this
                // signature (span -> SequenceView, str -> StringView, result).
                const amc::LoweringResult return_contract = record_contract(return_type_id);
                for (const amc::Hash128 parameter_type_id : parameter_type_ids)
                    record_contract(parameter_type_id);
                const amc::Hash128 thunk_signature = register_thunk(symbol, return_type_id, signature);
                const uint32_t thunk_index = static_cast<uint32_t>(module.functions.size() - 1);
                exposed_methods.push_back({symbol, thunk_signature});
                amc::semantic::ExposedMethod exposed_method;
                exposed_method.name = method;
                exposed_method.symbol = symbol;
                exposed_method.abi_signature = thunk_signature;
                exposed_contract.methods.push_back(std::move(exposed_method));
                // A `abix_result_*` return binds to the error boundary: the status and
                // value are fields of the returned record, so a consumer can turn the
                // C aggregate into a typed Result without inspecting a bare integer.
                if (type_name(return_type_id).rfind("abix_result_", 0) == 0
                    && return_contract.status != static_cast<uint32_t>(amc::LoweringStatus::unsupported)) {
                    amc::ErrorContract contract;
                    contract.function_index = thunk_index;
                    contract.status_source = amc::StatusSource::return_field;
                    contract.status_parameter = return_contract.pointer_field;
                    contract.value_parameter = return_contract.length_field;
                    contract.success_value = 0;
                    contract.domain = amc::amc_error_domain;
                    module.errors.push_back(contract);
                }
            }
        }
        if (!(exposed_contract.create == amc::Hash128{})
            || !(exposed_contract.destroy == amc::Hash128{})
            || !exposed_contract.methods.empty())
            semantic.exposed.push_back(std::move(exposed_contract));
        source += "\n";

        // One interface per exposed type: each method binds its thunk by a
        // stable slot, so the runtime bus can dispatch without a vtable.
        if (!exposed_methods.empty()) {
            // Same identity formula as the [[interface]] builder and the ABIX
            // runtime helper: canonical name + version + ordered signatures.
            std::vector<amc::Hash128> ordered_signatures;
            ordered_signatures.reserve(exposed_methods.size());
            for (const auto &method : exposed_methods)
                ordered_signatures.push_back(method.signature);
            const amc::Hash128 interface_id = amc::interface_identity(cpp_type, 1, ordered_signatures);
            const auto hex = [](uint64_t v) { return std::to_string(v) + "ULL"; };
            const std::string methods_array = "abix_" + symbol_type + "_methods";
            manifest += "const skl::abix::poly::InterfaceMethod " + methods_array + "[] = {\n";
            for (size_t i = 0; i < exposed_methods.size(); ++i) {
                manifest += "    {{"
                          + hex(exposed_methods[i].signature.lo)
                          + ", "
                          + hex(exposed_methods[i].signature.hi)
                          + "}, skl::abix::poly::interface_entry(&"
                          + exposed_methods[i].symbol
                          + "), "
                          + std::to_string(i)
                          + ", 0},\n";
            }
            manifest += "};\n";
            const std::string interface_name = "abix_" + symbol_type + "_interface";
            manifest += "const skl::abix::poly::InterfaceManifestEntry "
                      + interface_name
                      + " = {{"
                      + hex(interface_id.lo)
                      + ", "
                      + hex(interface_id.hi)
                      + "}, 1, 0, "
                      + methods_array
                      + ", "
                      + std::to_string(exposed_methods.size())
                      + ", 0};\n";
            interface_names.push_back(interface_name);
        }
    }

    if (!interface_names.empty()) {
        manifest += "namespace {\nconst skl::abix::poly::InterfaceManifestEntry abix_module_interfaces[] = {\n";
        for (const auto &name : interface_names)
            manifest += "    " + name + ",\n";
        manifest += "};\nconst skl::abix::poly::ModuleManifest abix_module_manifest = {\n"
                    "    skl::abix::poly::abix_manifest_version, 1, 0, "
                  + std::to_string(interface_names.size())
                  + ", abix_module_interfaces, 0, 0, nullptr};\n}\n"
                    "SKL_ABIX_DEFINE_MODULE_QUERY(&abix_module_manifest)\n";
    }
    source += manifest;

    llvm::SmallString<256> path(base);
    llvm::sys::path::append(path, config.thunks_output);
    std::error_code ec;
    llvm::raw_fd_ostream out(path, ec, llvm::sys::fs::OF_Text);
    if (ec) {
        error = "cannot write thunk source: " + path.str().str() + ": " + ec.message();
        return false;
    }
    out << source;
    out.close();
    if (out.has_error()) {
        error = "cannot write thunk source: " + path.str().str() + ": " + out.error().message();
        return false;
    }
    return true;
}

}   // namespace amc::lang::frontend::cpp
