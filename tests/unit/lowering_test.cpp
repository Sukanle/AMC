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
#include "SKL/AMC/IR/LoweringJson.h"
#include "SKL/AMC/Pass/Lowering.h"
#include "SKL/AMC/Semantic/Identity.h"
#include "SKL/AMC/Semantic/Sync.h"

#include <fmt/base.h>

#include <cstdio>
#include <string>

namespace {

int failures = 0;

void check(bool condition, const char *message) {
    if (!condition) {
        fmt::print(stderr, "  FAIL: {}\n", message);
        ++failures;
    }
}

skl::amc::Hash128 id(const char *text) { return skl::amc::hash_text(text, 0x54595045ULL); }

skl::amc::semantic::SemanticTypeId semantic_type(const char *name) {
    return skl::amc::semantic::semantic_type_identity(skl::amc::semantic::SemanticTypeKind::RECORD, name,
        skl::amc::semantic::Qualifiers{}, skl::amc::semantic::ValueCategory::VALUE, {}, {}, {},
        skl::amc::semantic::semantic_rules_version);
}

void test_lowering_links_types_functions_and_templates() {
    skl::amc::ir::Module module;

    skl::amc::Type base;
    base.name = "demo::Base";
    base.kind = decltype(base.kind)::RECORD;
    base.id = id("demo::Base");
    base.size = 4;
    base.align = 4;
    base.layout_hash = skl::amc::hash_text("base-layout", 0x4c41594fULL);

    skl::amc::Type foo;
    foo.name = "demo::Foo";
    foo.kind = decltype(base.kind)::RECORD;
    foo.id = id("demo::Foo");
    foo.size = 8;
    foo.align = 8;
    foo.layout_hash = skl::amc::hash_text("foo-layout", 0x4c41594fULL);

    skl::amc::Type int_type;
    int_type.name = "int";
    int_type.kind = decltype(base.kind)::PRIMITIVE;
    int_type.id = id("int");
    int_type.size = 4;
    int_type.align = 4;

    skl::amc::Type void_type;
    void_type.name = "void";
    void_type.kind = decltype(base.kind)::PRIMITIVE;
    void_type.id = id("void");

    skl::amc::Function set;
    set.name = "demo::Foo::set";
    set.owner_type = foo.id;
    set.signature = skl::amc::hash_text("demo::Foo::set(int)", 0x5349474eULL);
    set.return_type = void_type.id;
    set.params.push_back({"value", int_type.id, 0});

    module.abi.types = {base, foo, int_type, void_type};
    module.abi.functions = {set};

    const auto base_semantic = semantic_type("demo::Base");
    const auto foo_semantic = semantic_type("demo::Foo");
    const auto int_semantic = semantic_type("int");
    const auto void_semantic = semantic_type("void");

    skl::amc::semantic::SemanticType base_type;
    base_type.id = base_semantic;
    base_type.kind = skl::amc::semantic::SemanticTypeKind::RECORD;
    base_type.qualified = "demo::Base";
    base_type.abi_type = base.id;
    skl::amc::semantic::SemanticType foo_type;
    foo_type.id = foo_semantic;
    foo_type.kind = skl::amc::semantic::SemanticTypeKind::RECORD;
    foo_type.qualified = "renamed::Foo";
    foo_type.abi_type = foo.id;
    skl::amc::semantic::SemanticType int_sem_type;
    int_sem_type.id = int_semantic;
    int_sem_type.kind = skl::amc::semantic::SemanticTypeKind::BUILTIN;
    int_sem_type.qualified = "int";
    int_sem_type.abi_type = int_type.id;
    skl::amc::semantic::SemanticType void_sem_type;
    void_sem_type.id = void_semantic;
    void_sem_type.kind = skl::amc::semantic::SemanticTypeKind::BUILTIN;
    void_sem_type.qualified = "void";
    void_sem_type.abi_type = void_type.id;
    module.semantic.types = {base_type, foo_type, int_sem_type, void_sem_type};

    skl::amc::semantic::SemanticRecord record;
    record.type = foo_semantic;
    record.bases.push_back({base_semantic, 0, skl::amc::Visibility::PUBLIC, false});
    skl::amc::semantic::SemanticField field;
    field.name = "value";
    field.type = int_semantic;
    field.bit_offset = 32;
    record.fields.push_back(field);
    module.semantic.records.push_back(record);

    skl::amc::semantic::SemanticParam parameter;
    parameter.name = "value";
    parameter.type = int_semantic;
    skl::amc::semantic::SemanticFunc function;
    function.id = skl::amc::semantic::semantic_function_identity(skl::amc::semantic::SemanticFuncKind::MEMBER,
        foo_semantic, "set", {int_semantic}, {}, skl::amc::semantic::ValueCategory::VALUE, {},
        skl::amc::semantic::semantic_rules_version);
    function.kind = skl::amc::semantic::SemanticFuncKind::MEMBER;
    function.qualified = "renamed::Foo::set";
    function.unqualified = "set";
    function.abi_signature = set.signature;
    function.owner = foo_semantic;
    function.return_type = void_semantic;
    function.params.push_back(parameter);
    module.semantic.functions.push_back(function);

    skl::amc::semantic::SemanticOwner ownership;
    ownership.function = function.id;
    ownership.interaction = skl::amc::semantic::InteractionKind::RETURN;
    ownership.ownership = skl::amc::semantic::OwnershipKind::OWNED;
    ownership.lifetime = skl::amc::semantic::LifetimeKind::CALLER;
    module.semantic.ownership.push_back(ownership);

    skl::amc::semantic::TemplateInst instance;
    instance.id = skl::amc::semantic::TemplateInstId{skl::amc::hash_text("demo::Box<int>", 0x54494e53ULL)};
    instance.result = foo.id;
    module.semantic.instances.push_back(instance);

    skl::amc::ir::LoweringResult result;
    skl::amc::diagnostics::DiagnosticList diagnostics;
    check(skl::amc::pass::lower(module, result, diagnostics), "lowering succeeds with warnings");
    check(result.types.size() == 4, "all semantic types are lowered");
    check(result.types[1].abi == foo.id && result.types[1].abi_index == 1,
        "type id and ABI index are linked through the direct abi_type field");
    check(result.types[1].size == 8 && result.types[1].align == 8, "layout facts are copied");
    check(result.types[1].bases.size() == 1 && result.types[1].bases[0].abi == base.id,
        "base-class lowering keeps the ABI type id");
    check(result.types[1].fields.size() == 1 && result.types[1].fields[0].type == int_type.id,
        "field lowering keeps the ABI type id");
    check(result.functions.size() == 1 && result.functions[0].abi == set.signature,
        "function lowering keeps the ABI signature through the direct link");
    check(result.functions[0].parameters.size() == 1 && result.functions[0].parameters[0].type == int_type.id,
        "function parameter lowering keeps the ABI type id");
    check(result.ownership.size() == 1 && result.ownership[0].abi == set.signature,
        "ownership is linked to the ABI function");
    check(result.templates.size() == 1
              && result.templates[0].status == skl::amc::ir::LoweringStatus::LOWERED
              && result.templates[0].abi_type == foo.id,
        "materialized template instances reference the ABI type");
    check(!result.has_rejections(), "known entities are not rejected");
}

void test_lowering_reports_missing_and_ambiguous_entities() {
    skl::amc::ir::Module module;
    skl::amc::Type foo;
    foo.name = "demo::Foo";
    foo.id = id("demo::Foo");
    module.abi.types.push_back(foo);
    skl::amc::Function first_pick;
    first_pick.name = "demo::pick";
    first_pick.return_type = id("int");
    first_pick.params.push_back({"a", id("int"), 0});
    skl::amc::Function second_pick = first_pick;
    second_pick.params[0].name = "b";
    module.abi.functions.push_back(first_pick);
    module.abi.functions.push_back(second_pick);

    const auto missing = semantic_type("demo::Missing");
    skl::amc::semantic::SemanticType missing_type;
    missing_type.id = missing;
    missing_type.qualified = "demo::Missing";
    module.semantic.types.push_back(missing_type);

    skl::amc::semantic::SemanticFunc pick;
    pick.id = skl::amc::semantic::semantic_function_identity(skl::amc::semantic::SemanticFuncKind::FREE, {}, "pick",
        {semantic_type("int")}, {}, skl::amc::semantic::ValueCategory::VALUE, {},
        skl::amc::semantic::semantic_rules_version);
    pick.qualified = "demo::pick";
    pick.unqualified = "pick";
    skl::amc::semantic::SemanticParam pick_parameter;
    pick_parameter.name = "value";
    pick_parameter.type = semantic_type("int");
    pick.params.push_back(pick_parameter);
    module.semantic.functions.push_back(pick);

    skl::amc::ir::LoweringResult result;
    skl::amc::diagnostics::DiagnosticList diagnostics;
    check(skl::amc::pass::lower(module, result, diagnostics), "lowering returns diagnostics as warnings by default");
    check(result.types.size() == 1 && result.types[0].reason == skl::amc::ir::LoweringReason::MISSING_ABI_TYPE,
        "missing ABI types are reported");
    check(result.functions.size() == 1
              && result.functions[0].reason == skl::amc::ir::LoweringReason::AMBIGUOUS_ABI_FUNCTION,
        "ambiguous ABI functions are rejected");
    check(result.has_rejections(), "rejections are visible in the result");

    skl::amc::diagnostics::DiagnosticList strict_diagnostics;
    skl::amc::pass::LoweringOptions strict;
    strict.strict = true;
    check(!skl::amc::pass::lower(module, result, strict_diagnostics, strict), "strict lowering fails on rejections");
    check(skl::amc::diagnostics::has_errors(strict_diagnostics), "strict lowering emits errors");
}

void test_sync_from_abi_creates_direct_links() {
    skl::amc::ABIModule module;
    skl::amc::Type widget;
    widget.name = "demo::Widget";
    widget.kind = skl::amc::TypeKind::RECORD;
    widget.id = id("demo::Widget");
    widget.size = 4;
    widget.align = 4;
    skl::amc::Type int_type;
    int_type.name = "int";
    int_type.kind = skl::amc::TypeKind::PRIMITIVE;
    int_type.id = id("int");
    int_type.size = 4;
    int_type.align = 4;
    skl::amc::Type void_type;
    void_type.name = "void";
    void_type.kind = skl::amc::TypeKind::PRIMITIVE;
    void_type.id = id("void");
    module.types = {widget, int_type, void_type};
    skl::amc::Field field;
    field.owner_type = widget.id;
    field.name = "value";
    field.type_id = int_type.id;
    module.fields.push_back(field);
    module.types[0].field_begin = 0;
    module.types[0].field_count = 1;
    skl::amc::Function fn;
    fn.name = "demo::Widget::get";
    fn.owner_type = widget.id;
    fn.return_type = int_type.id;
    fn.signature = skl::amc::hash_text("demo::Widget::get()", 0x5349474eULL);
    module.functions.push_back(fn);

    skl::amc::semantic::SemanticModule semantic;
    skl::amc::semantic::synchronize_from_abi(module, semantic);
    check(semantic.types.size() == 3, "every ABI type gets a semantic counterpart");
    check(semantic.records.size() == 1 && semantic.records[0].fields.size() == 1,
        "ABI record layout is mirrored into the semantic model");
    check(semantic.functions.size() == 1 && semantic.functions[0].abi_signature == fn.signature,
        "every ABI function gets a direct semantic signature link");
    bool linked = true;
    for (const auto &type : semantic.types)
        if (type.abi_type.lo == 0 && type.abi_type.hi == 0) linked = false;
    check(linked, "all synthesized semantic types carry abi_type");

    skl::amc::ir::Module ir;
    ir.abi = module;
    ir.semantic = semantic;
    skl::amc::ir::LoweringResult result;
    skl::amc::diagnostics::DiagnosticList diagnostics;
    check(skl::amc::pass::lower(ir, result, diagnostics), "synchronized semantic model lowers");
    check(result.types.size() == 3 && !result.has_rejections(), "synchronized types lower without rejections");
}

void test_lowering_exposed_contract() {
    skl::amc::ABIModule module;
    skl::amc::Type widget;
    widget.name = "demo::Widget";
    widget.kind = skl::amc::TypeKind::RECORD;
    widget.id = id("demo::Widget");
    widget.size = 4;
    widget.align = 4;
    skl::amc::Type widget_ptr;
    widget_ptr.name = "demo::Widget*";
    widget_ptr.kind = skl::amc::TypeKind::POINTER;
    widget_ptr.id = id("demo::Widget*");
    widget_ptr.size = 8;
    widget_ptr.align = 8;
    skl::amc::Type int_type;
    int_type.name = "int";
    int_type.kind = skl::amc::TypeKind::PRIMITIVE;
    int_type.id = id("int");
    int_type.size = 4;
    int_type.align = 4;
    skl::amc::Type void_type;
    void_type.name = "void";
    void_type.kind = skl::amc::TypeKind::PRIMITIVE;
    void_type.id = id("void");
    module.types = {widget, widget_ptr, int_type, void_type};

    skl::amc::Function create;
    create.name = "abix_Widget_create";
    create.return_type = widget_ptr.id;
    create.params.push_back({"value", int_type.id, 0});
    create.signature = skl::amc::signature_hash(create);
    skl::amc::Function destroy;
    destroy.name = "abix_Widget_destroy";
    destroy.return_type = void_type.id;
    destroy.params.push_back({"self", widget_ptr.id, 0});
    destroy.signature = skl::amc::signature_hash(destroy);
    skl::amc::Function get;
    get.name = "abix_Widget_get";
    get.return_type = int_type.id;
    get.params.push_back({"self", widget_ptr.id, 0});
    get.signature = skl::amc::signature_hash(get);
    module.functions = {create, destroy, get};

    skl::amc::semantic::ExposedType exposed;
    exposed.abi_type = widget.id;
    exposed.qualified = "demo::Widget";
    exposed.symbol = "Widget";
    exposed.create = create.signature;
    exposed.destroy = destroy.signature;
    exposed.has_constructor = true;
    skl::amc::semantic::ExposedMethod method;
    method.name = "get";
    method.symbol = "abix_Widget_get";
    method.abi_signature = get.signature;
    exposed.methods.push_back(method);

    skl::amc::ir::Module ir;
    ir.abi = module;
    ir.semantic.exposed.push_back(exposed);
    skl::amc::ir::LoweringResult result;
    skl::amc::diagnostics::DiagnosticList diagnostics;
    check(skl::amc::pass::lower(ir, result, diagnostics), "exposed lowering succeeds");
    check(result.exposed.size() == 1 && result.exposed[0].status == skl::amc::ir::LoweringStatus::LOWERED,
        "exposed type links to its ABI type and thunks");
    check(result.exposed[0].create == create.signature && result.exposed[0].destroy == destroy.signature,
        "exposed create/destroy signatures are preserved");
    check(result.exposed[0].methods.size() == 1 && result.exposed[0].methods[0].abi == get.signature,
        "exposed method links to its ABI signature");

    const std::string path = "amc-lowering-exposed-test.json";
    std::string error;
    check(skl::amc::ir::write_lowering_json(result, path, error), "exposed lowering JSON is written");
    skl::amc::ir::LoweringResult decoded;
    check(skl::amc::ir::read_lowering_json(path, decoded, error), "exposed lowering JSON is read back");
    check(decoded.exposed.size() == 1
              && decoded.exposed[0].methods.size() == 1
              && decoded.exposed[0].methods[0].abi == get.signature,
        "exposed lowering round-trip preserves the method contract");
    std::remove(path.c_str());
}

void test_lowering_json_roundtrip() {
    skl::amc::ir::Module module;
    skl::amc::Type int_type;
    int_type.name = "int";
    int_type.kind = skl::amc::TypeKind::PRIMITIVE;
    int_type.id = id("int");
    int_type.size = 4;
    int_type.align = 4;
    module.abi.types.push_back(int_type);
    skl::amc::semantic::SemanticType int_semantic;
    int_semantic.id = semantic_type("int");
    int_semantic.kind = skl::amc::semantic::SemanticTypeKind::BUILTIN;
    int_semantic.qualified = "int";
    int_semantic.abi_type = int_type.id;
    module.semantic.types.push_back(int_semantic);

    skl::amc::ir::LoweringResult result;
    skl::amc::diagnostics::DiagnosticList diagnostics;
    check(skl::amc::pass::lower(module, result, diagnostics), "lowering succeeds for the round-trip test");

    const std::string path = "amc-lowering-test.json";
    std::string error;
    check(skl::amc::ir::write_lowering_json(result, path, error), "lowering JSON is written");
    skl::amc::ir::LoweringResult decoded;
    check(skl::amc::ir::read_lowering_json(path, decoded, error), "lowering JSON is read back");
    check(decoded.types.size() == 1 && decoded.types[0].abi == int_type.id,
        "lowering round-trip preserves the ABI type id");
    std::remove(path.c_str());
}

}   // namespace

int main() {
    test_lowering_links_types_functions_and_templates();
    test_lowering_reports_missing_and_ambiguous_entities();
    test_sync_from_abi_creates_direct_links();
    test_lowering_exposed_contract();
    test_lowering_json_roundtrip();

    if (failures != 0) {
        fmt::print(stderr, "amc-lowering-test: {} failure(s)\n", failures);
        return 1;
    }
    fmt::print("amc-lowering-test: all checks passed\n");
    return 0;
}
