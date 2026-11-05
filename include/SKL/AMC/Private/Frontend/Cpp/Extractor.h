#pragma once

#include "SKL/AMC/Private/Frontend/Cpp/Native.h"
#include "SKL/AMC/Core/Core.h"
#include "SKL/AMC/Lang/Lang.h"
#include "SKL/AMC/Semantic/Identity.h"
#include "SKL/AMC/Semantic/Model.h"
#include "SKL/AMC/Semantic/Sync.h"

#include <clang/AST/ASTContext.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/RecordLayout.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/AST/TemplateBase.h>
#include <clang/AST/Type.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Lex/Lexer.h>

#include <algorithm>
#include <cctype>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace amc::lang::frontend::cpp {

class Extractor : public clang::RecursiveASTVisitor<Extractor> {
public:
    Extractor(clang::ASTContext &c, const std::set<std::string> &wanted, amc::AbiModule &m,
        const std::vector<amc::lang::AdaptDecl> &a, std::vector<NativeAdapt> &out, amc::semantic::SemanticModule &s);
    bool VisitRecordDecl(clang::RecordDecl *d);
    bool VisitEnumDecl(clang::EnumDecl *d);
    bool VisitTypedefNameDecl(clang::TypedefNameDecl *d);
    bool VisitClassTemplateDecl(clang::ClassTemplateDecl *d);
    bool VisitFunctionTemplateDecl(clang::FunctionTemplateDecl *d);
    bool VisitClassTemplateSpecializationDecl(clang::ClassTemplateSpecializationDecl *d);
    bool VisitFunctionDecl(clang::FunctionDecl *d);
    bool VisitDecl(clang::Decl *d);

private:
    clang::ASTContext &ctx;
    const std::set<std::string> &wanted;
    amc::AbiModule &module;
    const std::vector<amc::lang::AdaptDecl> &adapts;
    std::vector<NativeAdapt> &adaptations;
    amc::semantic::SemanticModule &semantic;
    std::unordered_map<std::string, amc::Hash128> ids;
    std::unordered_map<std::string, amc::semantic::SemanticTypeId> semantic_type_ids;
    std::unordered_map<const clang::TemplateDecl *, amc::semantic::TemplateDeclId> semantic_templates;
    std::set<amc::semantic::SemanticTypeId> semantic_type_records;
    std::set<amc::semantic::TemplateInstanceId> semantic_instances;
    // Dedup keyed by ABI descriptor, so spellings that share an ABI (e.g.
    // `long` / `long long`) collapse onto one type record with one TypeID.
    std::unordered_map<std::string, amc::Hash128> primitive_ids;
    std::string name(const clang::NamedDecl *d) const;
    static bool plain_identifier(const std::string &s);
    void record_source(amc::Hash128 type_id, clang::SourceLocation location);

    amc::semantic::SourceLocation semantic_source(clang::SourceLocation location) const;

    std::string source_text(clang::SourceRange range) const;

    amc::Hash128 source_hash(clang::SourceRange range, uint64_t domain) const;

    static amc::semantic::SemanticTypeKind semantic_type_kind(clang::QualType type);

    amc::semantic::SemanticTypeId semantic_type_id(clang::QualType type);

    static amc::semantic::InstantiationOrigin instantiation_origin(clang::TemplateSpecializationKind kind);

    static void append_argument_key(std::string &out, const amc::semantic::TemplateArgument &argument);

    amc::semantic::TemplateArgument convert_argument(const clang::TemplateArgument &argument);

    amc::semantic::TemplateDeclId ensure_template_decl(clang::TemplateDecl *decl);

    void record_template_instance(clang::TemplateDecl *decl, llvm::ArrayRef<clang::TemplateArgument> arguments,
        clang::TemplateSpecializationKind kind, clang::NamedDecl *instantiated);
    void add_namespaces(const clang::NamedDecl *d);
    bool selected(const clang::NamedDecl *d) const;
    bool has_selected_member(const clang::CXXRecordDecl *d) const;
    amc::Hash128 add_type(clang::QualType qt);
    static uint32_t access_flags(clang::AccessSpecifier access);
    static uint32_t calling_convention(clang::FunctionDecl *d);
    void add_record(clang::CXXRecordDecl *d, const std::string &forced_name = std::string());
    void add_enum(clang::EnumDecl *d);
    void add_alias(clang::TypedefNameDecl *d);
    // Record methods an exposed type inherits from a base class. The exposed
    // type's own declarations are recorded through `wanted` (`X::m`); a method
    // declared on a base `B` (`B::m`) is not, so walk the base chain and record
    // any method whose name is one of the type's requested methods. A name a
    // more-derived class already declares shadows the base one (the thunk calls
    // `self->name`, so name lookup picks the override anyway).
    void add_inherited_methods(clang::CXXRecordDecl *d);

    // Record a native (STL) type by its canonical spelling, so a parameter type
    // that must not cross the boundary raw still has a stable identity for the
    // diagnosis and the adaptation record.
    amc::Hash128 register_native_type(const std::string &record_name);

    // Detect whether a parameter type is a native type with a declared
    // `[[adapt]]`. The mapping is explicit (ABI-SPEC §18): an STL type is never
    // adapted unless a matching declaration exists. `element` is the sequence
    // value type for a vector/span; read-only borrowed views only, so a
    // non-const lvalue reference or a mutable `std::span<T>` is not adapted
    // (writing back through a borrowed view is not modelled).
    bool detect_native(
        clang::QualType qt, const amc::lang::AdaptDecl *&decl, NativeKind &kind, clang::QualType &element);

    // An element spelling matches by normalized spelling (sugared or canonical),
    // or by the ABI of a fixed-width integer alias (`int32_t` matches `int`).
    bool element_matches(const std::string &declared, clang::QualType element) const;

    void add_function(clang::FunctionDecl *d);
};

}   // namespace amc::lang::frontend::cpp
