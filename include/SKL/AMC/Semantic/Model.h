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
namespace skl::amc::semantic {

enum class DeclarationKind : uint32_t {
    NAMESPACE,
    RECORD,
    ENUM,
    FIELD,
    FUNC,
    METHOD,
    CREATE,
    DESTORY,
    TEMPLATE,
    CONCEPT,
    TYPEDEF,
    VARIABLE,
};

enum class SemanticTypeKind : uint32_t {
    BUILTIN,
    POINTER,
    LVREF,   // lvalue reference,
    RVREF,   // rvalue reference,
    ARRAY,
    RECORD,
    ENUM,
    FUNC,
    MEMPTR,     // member pointer
    TEMPARAM,   // template parameter
    TEMINST,    // template instance
    DEPEND,
    UNKNOWN,
};

// Ownership is an interaction-level property, not a type property. STL mapping
// is one source of these facts; it never defines lifetime on its own.
enum class OwnershipKind : uint32_t {
    UNSPECIFIED,
    BORROWED,
    OWNED,
    SHARED,
    OPAQUE,
    TRANSFERRED,
};

enum class LifetimeKind : uint32_t {
    UNKNOWN,
    STATIC,
    CALLER,
    OWNER,
    SCOPE,
};

enum class InteractionKind : uint32_t {
    PARAM,
    RETURN,
};

enum class SemanticFuncKind : uint32_t {
    FREE,
    MEMBER,
    CREATE,
    DESTORY,
    OPETATOR,
    CONVERSION,
};

enum class TemplateTypeKind : uint32_t {
    FUNC,
    CLASS,
    ALIAS,
    VARIABLE
};

enum class TemplateParamKind : uint32_t {
    TYPE,
    NON,
    TEMPLATE,   // template template
};

enum class TemplateArgKind : uint32_t {
    TYPE,
    INTEGRAL,
    TEMPLATE,
    PACK,
    OTHER,
};

enum class InstOrigin : uint32_t {
    IMPLICIT_COMPILER,   // Clang already instantiated
    EXPLICIT_SOURCE,     // explicit instantiation in source
    AMC_REQUESTED,       // AMC generated TU
    CONFIG_REQUESTED,    // [[instantiate]]
};

enum class ValueCategory : uint32_t {
    VALUE,
    LVALUE,
    XVALUE,
};

enum class Mutability : uint32_t {
    IMMUTABLE,
    MUTABLE,
};


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
using SemanticFuncId = Id<SemanticFunctionTag>;
using TemplateDeclId = Id<TemplateDeclTag>;
using TemplateInstId = Id<TemplateInstanceTag>;

struct SourceLocation {
    std::string file;
    uint32_t line = 0;
    uint32_t column = 0;
    uint32_t offset = 0;
};

struct Qualifiers {
    bool is_const = false;
    bool is_volatile = false;
    bool is_restrict = false;

    friend bool operator==(const Qualifiers &a, const Qualifiers &b) noexcept {
        return a.is_const == b.is_const && a.is_volatile == b.is_volatile && a.is_restrict == b.is_restrict;
    }
};

struct SemanticType {
    SemanticTypeId id{};
    SemanticTypeKind kind = SemanticTypeKind::UNKNOWN;
    std::string qualified;   // canonical, source-level
    Qualifiers qualifiers;
    SemanticTypeId canonical{};   // canonical semantic type id
    SemanticTypeId element{};     // pointee / element / underlying
    TemplateInstId template_inst{};
    Hash128 abi_type{};   // direct ABI TypeID link (written by the frontend)
    SourceLocation source;
    uint32_t flags = 0;
};

struct TemplateParam {
    TemplateParamKind kind = TemplateParamKind::TYPE;
    uint32_t index = 0;
    std::string name;
    Hash128 default_argument{};      // zero when absent
    Hash128 constraint{};            // normalized constraint key, zero when absent
    std::string constraint_source;   // source text for diagnostics
};

struct TemplateDecl {
    TemplateDeclId id{};
    TemplateTypeKind kind = TemplateTypeKind::FUNC;
    std::string qualified_name;
    std::vector<TemplateParam> parameters;
    std::string requires_source;   // source text for diagnostics
    Hash128 requires_hash{};       // normalized requires clause
    SourceLocation source;
};

struct TemplateArg {
    TemplateArgKind kind = TemplateArgKind::TYPE;
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

struct TemplateInst {
    TemplateInstId id{};
    TemplateDeclId declaration{};
    std::vector<TemplateArg> args;
    InstOrigin origin = InstOrigin::IMPLICIT_COMPILER;
    ConstraintResult constraint;
    Hash128 result{};   // SemanticTypeId or SemanticFunctionId
    bool is_explicit_specialization = false;
    SourceLocation source;
};

struct SemanticParam {
    std::string name;
    SemanticTypeId type{};
    ValueCategory category = ValueCategory::VALUE;
    std::string default_value;
    bool is_variadic = false;
};

struct SemanticFunc {
    SemanticFuncId id{};
    SemanticFuncKind kind = SemanticFuncKind::FREE;
    std::string qualified;
    std::string unqualified;
    SemanticTypeId owner{};   // zero for free functions
    SemanticTypeId return_type{};
    std::vector<SemanticParam> params;
    Qualifiers qualifiers;
    ValueCategory ref_qualifier = ValueCategory::VALUE;
    bool is_variadic = false;
    bool is_noexcept = false;
    bool is_static = false;
    bool is_virtual = false;
    bool is_constexpr = false;
    bool is_template_primary = false;
    TemplateInstId template_instance{};
    Hash128 abi_signature{};   // direct ABI signature link (written by the frontend)
    ConstraintResult constraint;
    SourceLocation source;
};

struct SemanticBase {
    SemanticTypeId type{};
    uint32_t bit_offset = 0;
    Visibility visibility = Visibility::NONE;
    bool is_virtual = false;
};

struct SemanticField {
    std::string name;
    SemanticTypeId type{};
    uint32_t bit_offset = 0;
    uint32_t bit_width = 0;   // 0 = not a bitfield
    Visibility visibility = Visibility::NONE;
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

struct SemanticOwner {
    SemanticFuncId function{};
    InteractionKind interaction = InteractionKind::PARAM;
    uint32_t index = 0;   // parameter index; ignored for return values
    OwnershipKind ownership = OwnershipKind::UNSPECIFIED;
    LifetimeKind lifetime = LifetimeKind::UNKNOWN;
    Mutability mutability = Mutability::IMMUTABLE;
    SemanticFuncId release{};   // semantic release entry, zero when absent
    std::string invalidation;   // source/contract note
};

// One method of an automatically exposed type. `abi_signature` is the direct
// link to the generated thunk; the backend never has to parse the thunk name to
// recover the method contract.
struct ExposedMethod {
    std::string name;
    std::string symbol;
    Hash128 abi_signature{};
};

// An `[[expose]]` capability contract: an opaque handle with a constructor /
// destructor policy and a list of callable thunks. It is recorded by the
// frontend and lowered into `ir::LoweredExposedType` for backend projections.
struct ExposedType {
    SemanticTypeId type{};
    Hash128 abi_type{};   // direct ABI TypeID link
    std::string qualified;
    std::string symbol;
    Hash128 create{};    // ABI create thunk signature, zero when absent
    Hash128 destroy{};   // ABI destroy thunk signature, zero when absent
    bool has_constructor = false;
    bool opaque = true;
    std::vector<ExposedMethod> methods;
};

// Rust-specific semantic facts. These are language extensions, not ABI facts:
// generics, trait declarations/impls, error enums, lifetimes, trait objects and
// async functions. The ABI mapping remains a separate lowering decision.
struct RustGenericParam {
    uint32_t index = 0;
    std::string name;
    std::string kind;   // type | const | lifetime
    std::string default_value;
};

struct RustGeneric {
    std::string id;
    std::string name;
    std::string path;
    std::string kind;   // type | function | trait | item
    std::vector<RustGenericParam> params;
    std::vector<std::string> where_clause;
    std::vector<std::string> instances;
};

struct RustTraitParameter {
    std::string name;
    std::string type;
};

struct RustTraitMethod {
    std::string name;
    std::string path;
    std::string abi;
    std::string return_type;
    bool has_receiver = false;
    bool required = false;
    std::vector<RustTraitParameter> params;
};

struct RustAssociatedType {
    std::string name;
    std::string kind;   // type | const
    std::vector<std::string> bounds;
};

struct RustTrait {
    std::string id;
    std::string name;
    std::string path;
    std::string kind;   // trait | impl
    std::string self_type;
    std::string trait_ref;
    bool negative = false;
    std::vector<RustGenericParam> params;
    std::vector<std::string> where_clause;
    std::vector<std::string> supertraits;
    std::vector<RustAssociatedType> associated_types;
    std::vector<RustTraitMethod> methods;
};

struct RustErrorVar {
    std::string name;
    std::string discriminant;
    std::string payload;
};

struct RustError {
    std::string id;
    std::string name;
    std::string path;
    std::string kind;   // enum | record | primitive
    std::vector<RustErrorVar> variants;
    std::vector<std::string> functions;
};

struct RustLifetime {
    std::string owner;
    std::string name;
    std::string kind;   // param | late_bound | outlives
    std::vector<std::string> bounds;
};

struct RustTraitObj {
    std::string owner;
    std::string context;   // parameter | return | field | alias
    std::string type;
    std::vector<std::string> traits;
};

struct RustAsyncFunc {
    std::string id;
    std::string path;
    std::string name;
    std::string return_type;
};

// Standard containers that need an explicit ABI contract. The language fact
// records where the container appears; it does not decide its layout.
struct RustContainer {
    std::string owner;
    std::string context;   // parameter | return | field | alias
    std::string type;
    std::string container;   // option | box | string | vec
    std::string element;
};

struct RustCargoDependency {
    std::string name;
    std::string requirement;
    std::string kind;
    std::string path;
    bool optional = false;
};

struct RustCargo {
    std::string package_id;
    std::string manifest_path;
    std::string workspace_root;
    std::string target_directory;
    std::vector<std::string> workspace_members;
    std::vector<std::string> default_members;
    std::vector<RustCargoDependency> dependencies;
};

struct RustSemantic {
    RustCargo cargo;
    std::vector<RustGeneric> generics;
    std::vector<RustTrait> traits;
    std::vector<RustError> errors;
    std::vector<RustLifetime> lifetimes;
    std::vector<RustTraitObj> trait_objects;
    std::vector<RustAsyncFunc> async_functions;
    std::vector<RustContainer> containers;
};

struct SemanticModule {
    std::string package_name;
    std::string package_version;
    uint32_t rules_version = 1;
    std::vector<SemanticType> types;
    std::vector<SemanticRecord> records;
    std::vector<SemanticFunc> functions;
    std::vector<TemplateDecl> templates;
    std::vector<TemplateInst> instances;
    std::vector<SemanticOwner> ownership;
    std::vector<ExposedType> exposed;
    RustSemantic rust;
};

}   // namespace skl::amc::semantic

#endif   // AMC_SEMANTIC_MODEL_H
