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
#include "SKL/AMC/Pass/Lowering.h"
#include "SKL/AMC/Semantic/Identity.h"

#include <fmt/base.h>

#include <string>

namespace {

int failures = 0;

void check(bool condition, const char *message) {
    if (!condition) {
        fmt::print(stderr, "  FAIL: {}\n", message);
        ++failures;
    }
}

amc::Hash128 id(const char *text) { return amc::hash_text(text, 0x54595045ULL); }

amc::semantic::SemanticTypeId semantic_type(const char *name) {
    return amc::semantic::semantic_type_identity(amc::semantic::SemanticTypeKind::record, name,
        amc::semantic::Qualifiers{}, amc::semantic::ValueCategory::value, {}, {}, {},
        amc::semantic::semantic_rules_version);
}

void test_lowering_links_types_functions_and_templates() {
    amc::ir::Module module;

    amc::Type base;
    base.name = "demo::Base";
    base.kind = amc::TypeKind::record;
    base.id = id("demo::Base");
    base.size = 4;
    base.align = 4;
    base.layout_hash = amc::hash_text("base-layout", 0x4c41594fULL);

    amc::Type foo;
    foo.name = "demo::Foo";
    foo.kind = amc::TypeKind::record;
    foo.id = id("demo::Foo");
    foo.size = 8;
    foo.align = 8;
    foo.layout_hash = amc::hash_text("foo-layout", 0x4c41594fULL);

    amc::Type int_type;
    int_type.name = "int";
    int_type.kind = amc::TypeKind::primitive;
    int_type.id = id("int");
    int_type.size = 4;
    int_type.align = 4;

    amc::Type void_type;
    void_type.name = "void";
    void_type.kind = amc::TypeKind::primitive;
    void_type.id = id("void");

    amc::Function set;
    set.name = "demo::Foo::set";
    set.owner_type = foo.id;
    set.signature = amc::hash_text("demo::Foo::set(int)", 0x5349474eULL);
    set.return_type = void_type.id;
    set.parameters.push_back({"value", int_type.id, 0});

    module.abi.types = {base, foo, int_type, void_type};
    module.abi.functions = {set};

    const auto base_semantic = semantic_type("demo::Base");
    const auto foo_semantic = semantic_type("demo::Foo");
    const auto int_semantic = semantic_type("int");
    const auto void_semantic = semantic_type("void");

    amc::semantic::SemanticType base_type;
    base_type.id = base_semantic;
    base_type.kind = amc::semantic::SemanticTypeKind::record;
    base_type.qualified_name = "demo::Base";
    amc::semantic::SemanticType foo_type;
    foo_type.id = foo_semantic;
    foo_type.kind = amc::semantic::SemanticTypeKind::record;
    foo_type.qualified_name = "demo::Foo";
    amc::semantic::SemanticType int_sem_type;
    int_sem_type.id = int_semantic;
    int_sem_type.kind = amc::semantic::SemanticTypeKind::builtin;
    int_sem_type.qualified_name = "int";
    amc::semantic::SemanticType void_sem_type;
    void_sem_type.id = void_semantic;
    void_sem_type.kind = amc::semantic::SemanticTypeKind::builtin;
    void_sem_type.qualified_name = "void";
    module.semantic.types = {base_type, foo_type, int_sem_type, void_sem_type};

    amc::semantic::SemanticRecord record;
    record.type = foo_semantic;
    record.bases.push_back({base_semantic, 0, amc::Visibility::public_, false});
    amc::semantic::SemanticField field;
    field.name = "value";
    field.type = int_semantic;
    field.bit_offset = 32;
    record.fields.push_back(field);
    module.semantic.records.push_back(record);

    amc::semantic::SemanticParameter parameter;
    parameter.name = "value";
    parameter.type = int_semantic;
    amc::semantic::SemanticFunction function;
    function.id = amc::semantic::semantic_function_identity(amc::semantic::SemanticFunctionKind::member_function,
        foo_semantic, "set", {int_semantic}, {}, amc::semantic::ValueCategory::value, {},
        amc::semantic::semantic_rules_version);
    function.kind = amc::semantic::SemanticFunctionKind::member_function;
    function.qualified_name = "demo::Foo::set";
    function.unqualified_name = "set";
    function.owner = foo_semantic;
    function.return_type = void_semantic;
    function.parameters.push_back(parameter);
    module.semantic.functions.push_back(function);

    amc::semantic::SemanticOwnership ownership;
    ownership.function = function.id;
    ownership.interaction = amc::semantic::InteractionKind::return_value;
    ownership.ownership = amc::semantic::OwnershipKind::owned;
    ownership.lifetime = amc::semantic::LifetimeKind::caller;
    module.semantic.ownership.push_back(ownership);

    amc::semantic::TemplateInstance instance;
    instance.id = amc::semantic::TemplateInstanceId{amc::hash_text("demo::Box<int>", 0x54494e53ULL)};
    instance.result = foo.id;
    module.semantic.instances.push_back(instance);

    amc::ir::LoweringResult result;
    amc::diagnostics::DiagnosticList diagnostics;
    check(amc::pass::lower(module, result, diagnostics), "lowering succeeds with warnings");
    check(result.types.size() == 4, "all semantic types are lowered");
    check(result.types[1].abi == foo.id && result.types[1].abi_index == 1, "type id and ABI index are linked");
    check(result.types[1].size == 8 && result.types[1].align == 8, "layout facts are copied");
    check(result.types[1].bases.size() == 1 && result.types[1].bases[0].abi == base.id,
        "base-class lowering keeps the ABI type id");
    check(result.types[1].fields.size() == 1 && result.types[1].fields[0].type == int_type.id,
        "field lowering keeps the ABI type id");
    check(result.functions.size() == 1 && result.functions[0].abi == set.signature,
        "function lowering keeps the ABI signature");
    check(result.functions[0].parameters.size() == 1
              && result.functions[0].parameters[0].type == int_type.id,
        "function parameter lowering keeps the ABI type id");
    check(result.ownership.size() == 1 && result.ownership[0].abi == set.signature,
        "ownership is linked to the ABI function");
    check(result.templates.size() == 1 && result.templates[0].status == amc::ir::LoweringStatus::lowered
              && result.templates[0].abi_type == foo.id,
        "materialized template instances reference the ABI type");
    check(!result.has_rejections(), "known entities are not rejected");
}

void test_lowering_reports_missing_and_ambiguous_entities() {
    amc::ir::Module module;
    amc::Type foo;
    foo.name = "demo::Foo";
    foo.id = id("demo::Foo");
    module.abi.types.push_back(foo);
    amc::Function first_pick;
    first_pick.name = "demo::pick";
    first_pick.return_type = id("int");
    first_pick.parameters.push_back({"a", id("int"), 0});
    amc::Function second_pick = first_pick;
    second_pick.parameters[0].name = "b";
    module.abi.functions.push_back(first_pick);
    module.abi.functions.push_back(second_pick);

    const auto missing = semantic_type("demo::Missing");
    amc::semantic::SemanticType missing_type;
    missing_type.id = missing;
    missing_type.qualified_name = "demo::Missing";
    module.semantic.types.push_back(missing_type);

    amc::semantic::SemanticFunction pick;
    pick.id = amc::semantic::semantic_function_identity(amc::semantic::SemanticFunctionKind::free_function, {},
        "pick", {semantic_type("int")}, {}, amc::semantic::ValueCategory::value, {},
        amc::semantic::semantic_rules_version);
    pick.qualified_name = "demo::pick";
    pick.unqualified_name = "pick";
    amc::semantic::SemanticParameter pick_parameter;
    pick_parameter.name = "value";
    pick_parameter.type = semantic_type("int");
    pick.parameters.push_back(pick_parameter);
    module.semantic.functions.push_back(pick);

    amc::ir::LoweringResult result;
    amc::diagnostics::DiagnosticList diagnostics;
    check(amc::pass::lower(module, result, diagnostics), "lowering returns diagnostics as warnings by default");
    check(result.types.size() == 1 && result.types[0].reason == amc::ir::LoweringReason::missing_abi_type,
        "missing ABI types are reported");
    check(result.functions.size() == 1
              && result.functions[0].reason == amc::ir::LoweringReason::ambiguous_abi_function,
        "ambiguous ABI functions are rejected");
    check(result.has_rejections(), "rejections are visible in the result");

    amc::diagnostics::DiagnosticList strict_diagnostics;
    amc::pass::LoweringOptions strict;
    strict.strict = true;
    check(!amc::pass::lower(module, result, strict_diagnostics, strict), "strict lowering fails on rejections");
    check(amc::diagnostics::has_errors(strict_diagnostics), "strict lowering emits errors");
}

}   // namespace

int main() {
    test_lowering_links_types_functions_and_templates();
    test_lowering_reports_missing_and_ambiguous_entities();

    if (failures != 0) {
        fmt::print(stderr, "amc-lowering-test: {} failure(s)\n", failures);
        return 1;
    }
    fmt::print("amc-lowering-test: all checks passed\n");
    return 0;
}
