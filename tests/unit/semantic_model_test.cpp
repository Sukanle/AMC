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
#include "SKL/AMC/Semantic/Identity.h"
#include "SKL/AMC/Semantic/Model.h"

#include <fmt/base.h>

#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char *message) {
    if (!condition) {
        fmt::print(stderr, "  FAIL: {}\n", message);
        ++failures;
    }
}

skl::amc::semantic::SemanticTypeId type_id(uint64_t value) {
    return skl::amc::semantic::SemanticTypeId{
        skl::amc::Hash128{value, 0}
    };
}

skl::amc::semantic::TemplateDeclId template_id(uint64_t value) {
    return skl::amc::semantic::TemplateDeclId{
        skl::amc::Hash128{value, 0}
    };
}

skl::amc::semantic::TemplateInstId instance_id(uint64_t value) {
    return skl::amc::semantic::TemplateInstId{
        skl::amc::Hash128{value, 0}
    };
}

void test_type_identity() {
    using namespace skl::amc::semantic;
    const Qualifiers plain{};
    Qualifiers const_qualified;
    const_qualified.is_const = true;

    const auto base =
        semantic_type_identity(SemanticTypeKind::RECORD, "demo::Foo", plain, ValueCategory::VALUE, {}, {}, {}, 1);
    const auto same =
        semantic_type_identity(SemanticTypeKind::RECORD, "demo::Foo", plain, ValueCategory::VALUE, {}, {}, {}, 1);
    check(base == same, "type identity is deterministic");
    check(base.valid(), "type identity is non-zero");

    const auto const_type = semantic_type_identity(
        SemanticTypeKind::RECORD, "demo::Foo", const_qualified, ValueCategory::VALUE, {}, {}, {}, 1);
    check(base != const_type, "top-level const changes the semantic type identity");

    const auto reference =
        semantic_type_identity(SemanticTypeKind::LVREF, "demo::Foo&", plain, ValueCategory::LVALUE, {}, {}, {}, 1);
    check(base != reference, "reference kind changes the semantic type identity");

    const auto renamed =
        semantic_type_identity(SemanticTypeKind::RECORD, "demo::Bar", plain, ValueCategory::VALUE, {}, {}, {}, 1);
    check(base != renamed, "canonical name changes the semantic type identity");

    const auto rules_v2 =
        semantic_type_identity(SemanticTypeKind::RECORD, "demo::Foo", plain, ValueCategory::VALUE, {}, {}, {}, 2);
    check(base != rules_v2, "rules version changes the semantic type identity");

    const auto instance = semantic_type_identity(
        SemanticTypeKind::TEMINST, "demo::Box<int>", plain, ValueCategory::VALUE, {}, {}, instance_id(7), 1);
    const auto instance_other = semantic_type_identity(
        SemanticTypeKind::TEMINST, "demo::Box<int>", plain, ValueCategory::VALUE, {}, {}, instance_id(8), 1);
    check(instance != instance_other, "template instance changes the semantic type identity");
}

void test_function_identity() {
    using namespace skl::amc::semantic;
    const SemanticTypeId owner = type_id(1);
    const SemanticTypeId int_type = type_id(2);
    const SemanticTypeId float_type = type_id(3);
    const Qualifiers plain{};
    Qualifiers const_member;
    const_member.is_const = true;

    const auto set_int = semantic_function_identity(
        SemanticFuncKind::MEMBER, owner, "set", {int_type}, plain, ValueCategory::VALUE, {}, 1);
    const auto set_int_again = semantic_function_identity(
        SemanticFuncKind::MEMBER, owner, "set", {int_type}, plain, ValueCategory::VALUE, {}, 1);
    check(set_int == set_int_again, "function identity is deterministic");

    const auto set_float = semantic_function_identity(
        SemanticFuncKind::MEMBER, owner, "set", {float_type}, plain, ValueCategory::VALUE, {}, 1);
    check(set_int != set_float, "overload parameter types change the semantic function identity");

    const auto set_int_const = semantic_function_identity(
        SemanticFuncKind::MEMBER, owner, "set", {int_type}, const_member, ValueCategory::VALUE, {}, 1);
    check(set_int != set_int_const, "cv-qualifier changes the semantic function identity");

    const auto set_int_rvalue = semantic_function_identity(
        SemanticFuncKind::MEMBER, owner, "set", {int_type}, plain, ValueCategory::XVALUE, {}, 1);
    check(set_int != set_int_rvalue, "ref-qualifier changes the semantic function identity");

    const auto set_int_instance = semantic_function_identity(
        SemanticFuncKind::MEMBER, owner, "set", {int_type}, plain, ValueCategory::VALUE, instance_id(9), 1);
    check(set_int != set_int_instance, "template instance changes the semantic function identity");

    const auto other_owner = semantic_function_identity(
        SemanticFuncKind::MEMBER, type_id(4), "set", {int_type}, plain, ValueCategory::VALUE, {}, 1);
    check(set_int != other_owner, "owner changes the semantic function identity");

    const auto free_set =
        semantic_function_identity(SemanticFuncKind::FREE, {}, "set", {int_type}, plain, ValueCategory::VALUE, {}, 1);
    check(set_int != free_set, "function kind changes the semantic function identity");
}

void test_template_decl_identity() {
    using namespace skl::amc::semantic;
    TemplateParam type_parameter;
    type_parameter.kind = TemplateParamKind::TYPE;
    type_parameter.index = 0;
    type_parameter.name = "T";

    const auto base = template_decl_identity(TemplateTypeKind::CLASS, "demo::Box", {type_parameter}, {}, 1);
    const auto same = template_decl_identity(TemplateTypeKind::CLASS, "demo::Box", {type_parameter}, {}, 1);
    check(base == same, "template declaration identity is deterministic");

    TemplateParam renamed = type_parameter;
    renamed.name = "U";
    const auto renamed_decl = template_decl_identity(TemplateTypeKind::CLASS, "demo::Box", {renamed}, {}, 1);
    check(base != renamed_decl, "template parameter name changes the declaration identity");

    TemplateParam non_type = type_parameter;
    non_type.kind = TemplateParamKind::NON;
    non_type.name = "N";
    const auto non_type_decl = template_decl_identity(TemplateTypeKind::CLASS, "demo::Box", {non_type}, {}, 1);
    check(base != non_type_decl, "template parameter kind changes the declaration identity");

    const auto constrained = template_decl_identity(TemplateTypeKind::CLASS, "demo::Box", {type_parameter},
        skl::amc::hash_text("requires std::is_arithmetic_v<T>", 0x52455153ULL), 1);
    check(base != constrained, "requires clause changes the template declaration identity");

    const auto function_template = template_decl_identity(TemplateTypeKind::FUNC, "demo::Box", {type_parameter}, {}, 1);
    check(base != function_template, "template kind changes the declaration identity");
}

void test_template_instance_identity() {
    using namespace skl::amc::semantic;
    const TemplateDeclId decl = template_id(10);

    TemplateArgKey int_arg;
    int_arg.kind = TemplateArgKind::TYPE;
    int_arg.type = type_id(2);

    TemplateArgKey float_arg;
    float_arg.kind = TemplateArgKind::TYPE;
    float_arg.type = type_id(3);

    const auto int_instance = template_instance_identity(decl, {int_arg}, false, 1);
    const auto int_instance_again = template_instance_identity(decl, {int_arg}, false, 1);
    check(int_instance == int_instance_again, "template instance identity is deterministic");

    const auto float_instance = template_instance_identity(decl, {float_arg}, false, 1);
    check(int_instance != float_instance, "template argument type changes the instance identity");

    const auto explicit_instance = template_instance_identity(decl, {int_arg}, true, 1);
    check(int_instance != explicit_instance, "explicit specialization changes the instance identity");

    const auto other_decl = template_instance_identity(template_id(11), {int_arg}, false, 1);
    check(int_instance != other_decl, "template declaration changes the instance identity");

    TemplateArgKey integral_arg;
    integral_arg.kind = TemplateArgKind::INTEGRAL;
    integral_arg.value = 2;
    const auto integral_instance = template_instance_identity(decl, {integral_arg}, false, 1);
    check(int_instance != integral_instance, "type and integral arguments are distinct");

    TemplateArgKey pack_arg;
    pack_arg.kind = TemplateArgKind::PACK;
    pack_arg.canonical = skl::amc::hash_text("int,float", 0x5041434bULL);
    const auto pack_instance = template_instance_identity(decl, {pack_arg}, false, 1);
    const auto pack_instance_other = template_instance_identity(decl, {pack_arg}, false, 2);
    check(pack_instance != pack_instance_other, "rules version changes the instance identity");
}

void test_model_defaults() {
    using namespace skl::amc::semantic;
    SemanticOwner ownership;
    check(ownership.ownership == OwnershipKind::UNSPECIFIED, "ownership defaults to unspecified");
    check(ownership.lifetime == LifetimeKind::UNKNOWN, "lifetime defaults to unknown");
    check(!ownership.release.valid(), "release defaults to an invalid id");

    SemanticType type;
    check(!type.id.valid(), "semantic type id defaults to invalid");
    check(type.kind == SemanticTypeKind::UNKNOWN, "semantic type kind defaults to unknown");

    SemanticFunc function;
    check(function.kind == SemanticFuncKind::FREE, "function kind defaults to free function");
    check(!function.template_instance.valid(), "function template instance defaults to invalid");
}

}   // namespace

int main() {
    test_type_identity();
    test_function_identity();
    test_template_decl_identity();
    test_template_instance_identity();
    test_model_defaults();

    if (failures != 0) {
        fmt::print(stderr, "amc-semantic-test: {} failure(s)\n", failures);
        return 1;
    }
    fmt::print("amc-semantic-test: all checks passed\n");
    return 0;
}
