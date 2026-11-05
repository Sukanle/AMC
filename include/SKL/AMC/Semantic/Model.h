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
#ifndef AMC_SEMANTIC_MODEL_H
#define AMC_SEMANTIC_MODEL_H

#include "SKL/AMC/Core/Core.h"

#include <stdint.h>

#include <string>
#include <string_view>
#include <vector>

// Semantic model for C++ source entities. This layer is deliberately separate
// from the ABI model: it keeps source-level type/overload/template/lifetime
// facts, while lowering derives the ABI entities consumed by the backends.
//
// The model is populated from Clang; AMC never re-implements template argument
// deduction, overload resolution, SFINAE, constraints or constexpr evaluation.
namespace amc::semantic {

// Strongly typed identities. The tag prevents mixing a semantic type id with a
// function or template id while keeping the underlying 128-bit key shared with
// the artifact layer.
template<typename Tag>
struct Id {
    Hash128 value{};

    [[nodiscard]] bool valid() const noexcept { return value.lo != 0 || value.hi != 0; }

    friend bool operator==(const Id &a, const Id &b) noexcept { return a.value == b.value; }
    friend bool operator!=(const Id &a, const Id &b) noexcept { return !(a == b); }
    friend bool operator<(const Id &a, const Id &b) noexcept {
        return a.value.lo == b.value.lo ? a.value.hi < b.value.hi : a.value.lo < b.value.lo;
    }
};

struct SemanticTypeTag;
struct SemanticFunctionTag;
struct TemplateDeclTag;
struct TemplateInstanceTag;

using SemanticTypeId = Id<SemanticTypeTag>;
using SemanticFunctionId = Id<SemanticFunctionTag>;
using TemplateDeclId = Id<TemplateDeclTag>;
using TemplateInstanceId = Id<TemplateInstanceTag>;

struct SourceLocation {
    std::string file;
    uint32_t line = 0;
    uint32_t column = 0;
    uint32_t offset = 0;
};

enum class DeclarationKind : uint32_t {
    namespace_,
    record,
    enumeration,
    field,
    function,
    method,
    constructor,
    destructor,
    template_,
    concept_,
    typedef_,
    variable
};

enum class SemanticTypeKind : uint32_t {
    builtin,
    pointer,
    lvalue_reference,
    rvalue_reference,
    array,
    record,
    enumeration,
    function,
    member_pointer,
    template_parameter,
    template_instance,
    dependent,
    unknown
};

struct Qualifiers {
    bool is_const = false;
    bool is_volatile = false;
    bool is_restrict = false;

    friend bool operator==(const Qualifiers &a, const Qualifiers &b) noexcept {
        return a.is_const == b.is_const && a.is_volatile == b.is_volatile && a.is_restrict == b.is_restrict;
    }
};

enum class ValueCategory : uint32_t {
    value,
    lvalue,
    xvalue
};

struct SemanticType {
    SemanticTypeId id{};
    SemanticTypeKind kind = SemanticTypeKind::unknown;
    std::string qualified_name;   // canonical, source-level
    Qualifiers qualifiers;
    SemanticTypeId canonical{};   // canonical semantic type id
    SemanticTypeId element{};     // pointee / element / underlying
    TemplateInstanceId template_instance{};
    SourceLocation source;
    uint32_t flags = 0;
};

enum class TemplateKind : uint32_t {
    function,
    class_,
    alias,
    variable
};

enum class TemplateParameterKind : uint32_t {
    type,
    non_type,
    template_template
};

struct TemplateParameter {
    TemplateParameterKind kind = TemplateParameterKind::type;
    uint32_t index = 0;
    std::string name;
    Hash128 default_argument{};      // zero when absent
    Hash128 constraint{};            // normalized constraint key, zero when absent
    std::string constraint_source;   // source text for diagnostics
};

struct TemplateDecl {
    TemplateDeclId id{};
    TemplateKind kind = TemplateKind::function;
    std::string qualified_name;
    std::vector<TemplateParameter> parameters;
    std::string requires_source;   // source text for diagnostics
    Hash128 requires_hash{};       // normalized requires clause
    SourceLocation source;
};

enum class InstantiationOrigin : uint32_t {
    implicit_compiler,   // Clang already instantiated
    explicit_source,     // explicit instantiation in source
    amc_requested,       // AMC generated TU
    config_requested     // [[instantiate]]
};

enum class TemplateArgumentKind : uint32_t {
    type,
    integral,
    template_,
    pack,
    other
};

struct TemplateArgument {
    TemplateArgumentKind kind = TemplateArgumentKind::type;
    SemanticTypeId type{};
    int64_t value = 0;
    TemplateDeclId template_decl{};
    Hash128 canonical{};   // canonical key for packs / other arguments
};

struct ConstraintResult {
    bool evaluated = false;
    bool satisfied = false;
    std::string reason;
};

struct TemplateInstance {
    TemplateInstanceId id{};
    TemplateDeclId declaration{};
    std::vector<TemplateArgument> arguments;
    InstantiationOrigin origin = InstantiationOrigin::implicit_compiler;
    ConstraintResult constraint;
    Hash128 result{};   // SemanticTypeId or SemanticFunctionId
    bool is_explicit_specialization = false;
    SourceLocation source;
};

enum class SemanticFunctionKind : uint32_t {
    free_function,
    member_function,
    constructor,
    destructor,
    operator_,
    conversion
};

struct SemanticParameter {
    std::string name;
    SemanticTypeId type{};
    ValueCategory category = ValueCategory::value;
    std::string default_value;
    bool is_variadic = false;
};

struct SemanticFunction {
    SemanticFunctionId id{};
    SemanticFunctionKind kind = SemanticFunctionKind::free_function;
    std::string qualified_name;
    std::string unqualified_name;
    SemanticTypeId owner{};   // zero for free functions
    SemanticTypeId return_type{};
    std::vector<SemanticParameter> parameters;
    Qualifiers qualifiers;
    ValueCategory ref_qualifier = ValueCategory::value;
    bool is_variadic = false;
    bool is_noexcept = false;
    bool is_static = false;
    bool is_virtual = false;
    bool is_constexpr = false;
    bool is_template_primary = false;
    TemplateInstanceId template_instance{};
    ConstraintResult constraint;
    SourceLocation source;
};

struct SemanticBase {
    SemanticTypeId type{};
    uint32_t bit_offset = 0;
    Visibility visibility = Visibility::none;
    bool is_virtual = false;
};

struct SemanticField {
    std::string name;
    SemanticTypeId type{};
    uint32_t bit_offset = 0;
    uint32_t bit_width = 0;   // 0 = not a bitfield
    Visibility visibility = Visibility::none;
    bool is_static = false;
    bool is_mutable = false;
};

struct SemanticRecord {
    SemanticTypeId type{};
    std::vector<SemanticBase> bases;
    std::vector<SemanticField> fields;
    bool is_polymorphic = false;
    bool is_abstract = false;
    bool is_template_primary = false;
    SourceLocation source;
};

// Ownership is an interaction-level property, not a type property. STL mapping
// is one source of these facts; it never defines lifetime on its own.
enum class OwnershipKind : uint32_t {
    unspecified,
    borrowed,
    owned,
    shared,
    opaque,
    transferred
};

enum class LifetimeKind : uint32_t {
    unknown,
    static_,
    caller,
    owner,
    scope
};

enum class Mutability : uint32_t {
    immutable,
    mutable_
};

enum class InteractionKind : uint32_t {
    parameter,
    return_value
};

struct SemanticOwnership {
    SemanticFunctionId function{};
    InteractionKind interaction = InteractionKind::parameter;
    uint32_t index = 0;   // parameter index; ignored for return values
    OwnershipKind ownership = OwnershipKind::unspecified;
    LifetimeKind lifetime = LifetimeKind::unknown;
    Mutability mutability = Mutability::immutable;
    SemanticFunctionId release{};   // semantic release entry, zero when absent
    std::string invalidation;       // source/contract note
};

struct SemanticModule {
    std::string package_name;
    std::string package_version;
    uint32_t rules_version = 1;
    std::vector<SemanticType> types;
    std::vector<SemanticRecord> records;
    std::vector<SemanticFunction> functions;
    std::vector<TemplateDecl> templates;
    std::vector<TemplateInstance> instances;
    std::vector<SemanticOwnership> ownership;
};

}   // namespace amc::semantic

#endif   // AMC_SEMANTIC_MODEL_H
