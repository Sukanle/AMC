#include "SKL/AMC/Private/Frontend/Cpp/Extractor.h"

namespace amc::lang::frontend::cpp {

Extractor::Extractor(clang::ASTContext &c, const std::set<std::string> &wanted, amc::AbiModule &m,
    const std::vector<amc::lang::AdaptDecl> &a, std::vector<NativeAdapt> &out, amc::semantic::SemanticModule &s)
    : ctx(c)
    , wanted(wanted)
    , module(m)
    , adapts(a)
    , adaptations(out)
    , semantic(s) {}

bool Extractor::VisitRecordDecl(clang::RecordDecl *d) {
    if (amc::lang::cancellation_requested()) return false;
    auto *cxx = llvm::dyn_cast<clang::CXXRecordDecl>(d);
    if (!cxx || d->isImplicit() || !d->isThisDeclarationADefinition() || (!selected(d) && !has_selected_member(cxx)))
        return true;
    add_record(cxx);
    add_inherited_methods(cxx);
    return true;
}

bool Extractor::VisitEnumDecl(clang::EnumDecl *d) {
    if (amc::lang::cancellation_requested()) return false;
    if (d->isCompleteDefinition() && selected(d)) add_enum(d);
    return true;
}

bool Extractor::VisitTypedefNameDecl(clang::TypedefNameDecl *d) {
    if (amc::lang::cancellation_requested()) return false;
    if (selected(d)) add_alias(d);
    return true;
}

bool Extractor::VisitClassTemplateDecl(clang::ClassTemplateDecl *d) {
    if (amc::lang::cancellation_requested()) return false;
    if (d->isImplicit()) return true;
    if (!selected(d) && !has_selected_member(d->getTemplatedDecl())) return true;
    ensure_template_decl(d);
    for (clang::ClassTemplateSpecializationDecl *specialization : d->specializations()) {
        if (specialization->isCompleteDefinition())
            record_template_instance(d, specialization->getTemplateArgs().asArray(),
                specialization->getSpecializationKind(), specialization);
    }
    return true;
}

bool Extractor::VisitFunctionTemplateDecl(clang::FunctionTemplateDecl *d) {
    if (amc::lang::cancellation_requested()) return false;
    if (d->isImplicit()) return true;
    if (!selected(d)) return true;
    ensure_template_decl(d);
    for (clang::FunctionDecl *specialization : d->specializations()) {
        if (const clang::TemplateArgumentList *arguments = specialization->getTemplateSpecializationArgs())
            record_template_instance(
                d, arguments->asArray(), specialization->getTemplateSpecializationKind(), specialization);
    }
    return true;
}

bool Extractor::VisitClassTemplateSpecializationDecl(clang::ClassTemplateSpecializationDecl *d) {
    if (amc::lang::cancellation_requested()) return false;
    if (!d->isCompleteDefinition()) return true;
    clang::ClassTemplateDecl *declaration = d->getSpecializedTemplate();
    if (declaration == nullptr) return true;
    if (!selected(d) && !selected(declaration) && !has_selected_member(declaration->getTemplatedDecl())) return true;
    record_template_instance(declaration, d->getTemplateArgs().asArray(), d->getSpecializationKind(), d);
    return true;
}

bool Extractor::VisitFunctionDecl(clang::FunctionDecl *d) {
    if (amc::lang::cancellation_requested()) return false;
    const clang::FunctionTemplateSpecializationInfo *info = d->getTemplateSpecializationInfo();
    if (info != nullptr
        && info->TemplateArguments != nullptr
        && info->getTemplate() != nullptr
        && (selected(d) || selected(info->getTemplate()))) {
        record_template_instance(
            info->getTemplate(), info->TemplateArguments->asArray(), d->getTemplateSpecializationKind(), d);
    }
    return true;
}

bool Extractor::VisitDecl(clang::Decl *d) {
    if (amc::lang::cancellation_requested()) return false;
    if (auto *fn = llvm::dyn_cast<clang::FunctionDecl>(d))
        if (selected(fn)) add_function(fn);
    return true;
}

std::string Extractor::name(const clang::NamedDecl *d) const { return d->getQualifiedNameAsString(); }

bool Extractor::plain_identifier(const std::string &s) {
    if (s.empty() || !(std::isalpha(static_cast<unsigned char>(s[0])) || s[0] == '_')) return false;
    for (const char c : s)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_')) return false;
    return true;
}

void Extractor::record_source(amc::Hash128 type_id, clang::SourceLocation location) {
    if (location.isInvalid()) return;
    const auto presumed = ctx.getSourceManager().getPresumedLoc(location);
    if (presumed.isInvalid()) return;
    amc::SourceOrigin origin;
    origin.type_id = type_id;
    origin.file = presumed.getFilename() != nullptr ? presumed.getFilename() : "";
    origin.line = presumed.getLine();
    origin.column = presumed.getColumn();
    module.sources.push_back(std::move(origin));
}

amc::semantic::SourceLocation Extractor::semantic_source(clang::SourceLocation location) const {
    amc::semantic::SourceLocation out;
    if (location.isInvalid()) return out;
    const auto presumed = ctx.getSourceManager().getPresumedLoc(location);
    if (presumed.isInvalid()) return out;
    out.file = presumed.getFilename() != nullptr ? presumed.getFilename() : "";
    out.line = presumed.getLine();
    out.column = presumed.getColumn();
    return out;
}

std::string Extractor::source_text(clang::SourceRange range) const {
    if (range.isInvalid()) return {};
    return clang::Lexer::getSourceText(
        clang::CharSourceRange::getTokenRange(range), ctx.getSourceManager(), ctx.getLangOpts())
        .str();
}

amc::Hash128 Extractor::source_hash(clang::SourceRange range, uint64_t domain) const {
    const std::string text = source_text(range);
    return text.empty() ? amc::Hash128{} : amc::hash_text(text, domain);
}

amc::semantic::SemanticTypeKind Extractor::semantic_type_kind(clang::QualType type) {
    if (type->isLValueReferenceType()) return amc::semantic::SemanticTypeKind::lvalue_reference;
    if (type->isRValueReferenceType()) return amc::semantic::SemanticTypeKind::rvalue_reference;
    if (type->isPointerType()) return amc::semantic::SemanticTypeKind::pointer;
    if (type->isArrayType()) return amc::semantic::SemanticTypeKind::array;
    if (type->isFunctionType()) return amc::semantic::SemanticTypeKind::function;
    if (type->isMemberPointerType()) return amc::semantic::SemanticTypeKind::member_pointer;
    if (type->isRecordType()) return amc::semantic::SemanticTypeKind::record;
    if (type->isEnumeralType()) return amc::semantic::SemanticTypeKind::enumeration;
    if (type->isBuiltinType()) return amc::semantic::SemanticTypeKind::builtin;
    if (type->isDependentType()) return amc::semantic::SemanticTypeKind::dependent;
    return amc::semantic::SemanticTypeKind::unknown;
}

amc::semantic::SemanticTypeId Extractor::semantic_type_id(clang::QualType type) {
    const clang::QualType canonical = type.getCanonicalType();
    amc::semantic::Qualifiers qualifiers;
    qualifiers.is_const = type.isConstQualified();
    qualifiers.is_volatile = type.isVolatileQualified();
    qualifiers.is_restrict = type.isRestrictQualified();
    amc::semantic::ValueCategory category = amc::semantic::ValueCategory::value;
    if (type->isLValueReferenceType())
        category = amc::semantic::ValueCategory::lvalue;
    else if (type->isRValueReferenceType())
        category = amc::semantic::ValueCategory::xvalue;
    const std::string spelling = canonical.getAsString();
    const std::string key = spelling
                          + (qualifiers.is_const ? "|c" : "")
                          + (qualifiers.is_volatile ? "|v" : "")
                          + (qualifiers.is_restrict ? "|r" : "")
                          + "|"
                          + std::to_string(static_cast<uint32_t>(category));
    const auto found = semantic_type_ids.find(key);
    if (found != semantic_type_ids.end()) return found->second;

    amc::semantic::SemanticTypeId element;
    if (type->isPointerType() || type->isReferenceType())
        element = semantic_type_id(type->getPointeeType());
    else if (const auto *array = ctx.getAsArrayType(type))
        element = semantic_type_id(array->getElementType());

    const amc::semantic::SemanticTypeKind kind = semantic_type_kind(type);
    const auto id = amc::semantic::semantic_type_identity(
        kind, spelling, qualifiers, category, {}, element, {}, amc::semantic::semantic_rules_version);
    semantic_type_ids.emplace(key, id);
    if (semantic_type_records.insert(id).second) {
        amc::semantic::SemanticType record;
        record.id = id;
        record.kind = kind;
        record.qualified_name = spelling;
        record.qualifiers = qualifiers;
        record.element = element;
        semantic.types.push_back(std::move(record));
    }
    return id;
}

amc::semantic::InstantiationOrigin Extractor::instantiation_origin(clang::TemplateSpecializationKind kind) {
    switch (kind) {
        case clang::TSK_ExplicitSpecialization:
        case clang::TSK_ExplicitInstantiationDeclaration:
        case clang::TSK_ExplicitInstantiationDefinition:  return amc::semantic::InstantiationOrigin::explicit_source;
        case clang::TSK_ImplicitInstantiation:            return amc::semantic::InstantiationOrigin::implicit_compiler;
        default:                                          return amc::semantic::InstantiationOrigin::implicit_compiler;
    }
}

void Extractor::append_argument_key(std::string &out, const amc::semantic::TemplateArgument &argument) {
    out += std::to_string(static_cast<uint32_t>(argument.kind));
    out += ":";
    out += std::to_string(argument.type.value.lo);
    out += ":";
    out += std::to_string(argument.type.value.hi);
    out += ":";
    out += std::to_string(argument.value);
    out += ":";
    out += std::to_string(argument.template_decl.value.lo);
    out += ":";
    out += std::to_string(argument.template_decl.value.hi);
    out += ":";
    out += std::to_string(argument.canonical.lo);
    out += ":";
    out += std::to_string(argument.canonical.hi);
    out += ";";
}

amc::semantic::TemplateArgument Extractor::convert_argument(const clang::TemplateArgument &argument) {
    amc::semantic::TemplateArgument out;
    switch (argument.getKind()) {
        case clang::TemplateArgument::Type:
            out.kind = amc::semantic::TemplateArgumentKind::type;
            out.type = semantic_type_id(argument.getAsType());
            break;
        case clang::TemplateArgument::Integral:
            out.kind = amc::semantic::TemplateArgumentKind::integral;
            out.value = argument.getAsIntegral().getSExtValue();
            break;
        case clang::TemplateArgument::Template:
            out.kind = amc::semantic::TemplateArgumentKind::template_;
            if (auto *decl = argument.getAsTemplate().getAsTemplateDecl())
                out.template_decl = ensure_template_decl(decl);
            break;
        case clang::TemplateArgument::TemplateExpansion:
            out.kind = amc::semantic::TemplateArgumentKind::pack;
            if (auto *decl = argument.getAsTemplateOrTemplatePattern().getAsTemplateDecl())
                out.template_decl = ensure_template_decl(decl);
            break;
        case clang::TemplateArgument::Pack: {
            out.kind = amc::semantic::TemplateArgumentKind::pack;
            std::string key;
            for (const auto &child : argument.pack_elements()) {
                const auto converted = convert_argument(child);
                append_argument_key(key, converted);
            }
            out.canonical = amc::hash_text(key, 0x5041434bULL);
            break;
        }
        default: out.kind = amc::semantic::TemplateArgumentKind::other; break;
    }
    return out;
}

amc::semantic::TemplateDeclId Extractor::ensure_template_decl(clang::TemplateDecl *decl) {
    const auto found = semantic_templates.find(decl);
    if (found != semantic_templates.end()) return found->second;

    amc::semantic::TemplateDecl record;
    record.kind = llvm::isa<clang::ClassTemplateDecl>(decl) ? amc::semantic::TemplateKind::class_
                                                            : amc::semantic::TemplateKind::function;
    record.qualified_name = decl->getQualifiedNameAsString();
    if (const clang::TemplateParameterList *parameters = decl->getTemplateParameters()) {
        if (const clang::Expr *require = parameters->getRequiresClause())
            record.requires_source = source_text(require->getSourceRange());
        if (!record.requires_source.empty())
            record.requires_hash = amc::hash_text(record.requires_source, 0x52455153ULL);
        for (unsigned i = 0; i < parameters->size(); ++i) {
            const clang::NamedDecl *parameter = parameters->getParam(i);
            amc::semantic::TemplateParameter out;
            out.index = i;
            out.name = parameter->getNameAsString();
            if (auto *type = llvm::dyn_cast<clang::TemplateTypeParmDecl>(parameter)) {
                out.kind = amc::semantic::TemplateParameterKind::type;
                if (type->hasDefaultArgument())
                    out.default_argument = source_hash(type->getDefaultArgument().getSourceRange(), 0x44454641ULL);
                if (type->hasTypeConstraint()) {
                    if (const clang::TypeConstraint *constraint = type->getTypeConstraint()) {
                        if (const clang::Expr *expression = constraint->getImmediatelyDeclaredConstraint()) {
                            out.constraint = source_hash(expression->getSourceRange(), 0x434f4e53ULL);
                            out.constraint_source = source_text(expression->getSourceRange());
                        }
                    }
                }
            } else if (auto *non_type = llvm::dyn_cast<clang::NonTypeTemplateParmDecl>(parameter)) {
                out.kind = amc::semantic::TemplateParameterKind::non_type;
                if (non_type->hasDefaultArgument())
                    out.default_argument = source_hash(non_type->getDefaultArgument().getSourceRange(), 0x44454641ULL);
            } else if (auto *template_parameter = llvm::dyn_cast<clang::TemplateTemplateParmDecl>(parameter)) {
                out.kind = amc::semantic::TemplateParameterKind::template_template;
                if (template_parameter->hasDefaultArgument())
                    out.default_argument =
                        source_hash(template_parameter->getDefaultArgument().getSourceRange(), 0x44454641ULL);
            } else {
                continue;
            }
            record.parameters.push_back(std::move(out));
        }
    }
    record.id = amc::semantic::template_decl_identity(record.kind, record.qualified_name, record.parameters,
        record.requires_hash, amc::semantic::semantic_rules_version);
    record.source = semantic_source(decl->getLocation());
    semantic.templates.push_back(record);
    semantic_templates.emplace(decl, record.id);
    return record.id;
}

void Extractor::record_template_instance(clang::TemplateDecl *decl, llvm::ArrayRef<clang::TemplateArgument> arguments,
    clang::TemplateSpecializationKind kind, clang::NamedDecl *instantiated) {
    if (decl == nullptr) return;
    const amc::semantic::TemplateDeclId declaration = ensure_template_decl(decl);
    std::vector<amc::semantic::TemplateArgument> semantic_arguments;
    std::vector<amc::semantic::TemplateArgumentKey> keys;
    semantic_arguments.reserve(arguments.size());
    keys.reserve(arguments.size());
    for (const auto &argument : arguments) {
        auto converted = convert_argument(argument);
        amc::semantic::TemplateArgumentKey key;
        key.kind = converted.kind;
        key.type = converted.type;
        key.value = converted.value;
        key.template_decl = converted.template_decl;
        key.canonical = converted.canonical;
        keys.push_back(key);
        semantic_arguments.push_back(std::move(converted));
    }
    const bool explicit_specialization = kind == clang::TSK_ExplicitSpecialization;
    const auto instance = amc::semantic::template_instance_identity(
        declaration, keys, explicit_specialization, amc::semantic::semantic_rules_version);
    if (!semantic_instances.insert(instance).second) return;

    amc::semantic::TemplateInstance record;
    record.id = instance;
    record.declaration = declaration;
    record.arguments = std::move(semantic_arguments);
    record.origin = instantiation_origin(kind);
    record.is_explicit_specialization = explicit_specialization;
    record.constraint.evaluated = false;
    record.constraint.satisfied = false;
    record.constraint.reason = "recorded from Clang; constraint satisfaction is enforced by the compiler";
    record.source = semantic_source(instantiated != nullptr ? instantiated->getLocation() : decl->getLocation());

    if (instantiated != nullptr) {
        if (auto *specialization = llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(instantiated)) {
            record.result = amc::semantic::semantic_type_identity(amc::semantic::SemanticTypeKind::template_instance,
                specialization->getQualifiedNameAsString(), {}, amc::semantic::ValueCategory::value, {}, {}, instance,
                amc::semantic::semantic_rules_version)
                                .value;
        } else if (auto *function = llvm::dyn_cast<clang::FunctionDecl>(instantiated)) {
            amc::semantic::SemanticFunctionKind function_kind = amc::semantic::SemanticFunctionKind::free_function;
            amc::semantic::SemanticTypeId owner;
            if (auto *method = llvm::dyn_cast<clang::CXXMethodDecl>(function)) {
                if (llvm::isa<clang::CXXConstructorDecl>(method))
                    function_kind = amc::semantic::SemanticFunctionKind::constructor;
                else if (llvm::isa<clang::CXXDestructorDecl>(method))
                    function_kind = amc::semantic::SemanticFunctionKind::destructor;
                else if (llvm::isa<clang::CXXConversionDecl>(method))
                    function_kind = amc::semantic::SemanticFunctionKind::conversion;
                else
                    function_kind = amc::semantic::SemanticFunctionKind::member_function;
                owner = semantic_type_id(ctx.getCanonicalTagType(method->getParent()));
            }
            std::vector<amc::semantic::SemanticTypeId> parameters;
            parameters.reserve(function->getNumParams());
            for (const auto *parameter : function->parameters())
                parameters.push_back(semantic_type_id(parameter->getType()));
            record.result = amc::semantic::semantic_function_identity(function_kind, owner, function->getNameAsString(),
                parameters, {}, amc::semantic::ValueCategory::value, instance, amc::semantic::semantic_rules_version)
                                .value;
        }
    }
    semantic.instances.push_back(std::move(record));
}

void Extractor::add_namespaces(const clang::NamedDecl *d) {
    const auto *context = d->getDeclContext();
    std::vector<std::string> names;
    while (context && !llvm::isa<clang::TranslationUnitDecl>(context)) {
        if (const auto *named = llvm::dyn_cast<clang::NamespaceDecl>(context)) {
            if (!named->isAnonymousNamespace()) names.push_back(named->getQualifiedNameAsString());
        }
        context = context->getParent();
    }
    // for (auto it = names.rbegin(); it != names.rend(); ++it) {
    for (auto &e : names) {
        if (ids.count(e)) continue;
        amc::Type t;
        t.name = e;
        t.kind = amc::TypeKind::namespace_type;
        t.id = amc::hash_text(t.name, 0x54595045);
        t.size = t.align = 1;
        ids.emplace(t.name, t.id);
        module.types.push_back(std::move(t));
    }
}

bool Extractor::selected(const clang::NamedDecl *d) const {
    auto n = name(d);
    return wanted.count(n) || wanted.count(d->getNameAsString());
}

bool Extractor::has_selected_member(const clang::CXXRecordDecl *d) const {
    const auto prefix = name(d) + "::";
    for (const auto &symbol : wanted)
        if (symbol.compare(0, prefix.size(), prefix) == 0) return true;
    return false;
}

amc::Hash128 Extractor::add_type(clang::QualType qt) {
    // Keep the source spelling before canonicalizing: a typedef such as
    // `abix_span_i32` names a concrete instantiation, and that name should
    // be the exported ABI entity even when the alias is not an explicit
    // export symbol.
    const std::string original = qt.getAsString();
    // A typedef declared inside a class is a member typedef (for example
    // `std::allocator<T>::allocator_type`). Its unqualified spelling must
    // never become a global ABI type name, or generated code would emit
    // `TypeTraits<::allocator_type>`, which is not valid at namespace scope.
    // `getAs` looks through sugar (elaborated/qualified spellings), so this
    // finds the typedef even when it is written as a member type.
    bool member_typedef = false;
    if (const auto *typedef_type = qt->getAs<clang::TypedefType>())
        member_typedef = llvm::isa<clang::CXXRecordDecl>(typedef_type->getDecl()->getDeclContext());
    qt = qt.getCanonicalType();
    auto s = qt.getAsString();
    auto it = ids.find(s);
    if (it != ids.end()) return it->second;
    if (original != s) {
        auto alias = ids.find(original);
        if (alias != ids.end()) {
            ids.emplace(s, alias->second);
            return alias->second;
        }
    }
    if (qt->isDependentType()) {
        amc::Type t;
        t.name = s;
        t.id = amc::hash_text(s, 0x54595045);
        t.size = t.align = 1;
        ids.emplace(s, t.id);
        module.types.push_back(std::move(t));
        return ids.at(s);
    }
    // A concrete template specialization: when the instantiation is complete
    // (the frontend owns instantiation), record its real layout and fields.
    // Otherwise fall back to an opaque record so a reference never crashes.
    if (qt->getAs<clang::TemplateSpecializationType>()) {
        // When the specialization was reached through a typedef alias (for
        // example `abix_span_i32`), use the alias as the exported ABI name.
        const bool alias_named = !member_typedef && original != s && plain_identifier(original);
        if (auto *record = qt->getAsCXXRecordDecl(); record && record->isCompleteDefinition()) {
            add_record(record, alias_named ? original : std::string());
            const auto found = ids.find(alias_named ? original : name(record));
            if (found != ids.end()) {
                ids[s] = found->second;
                return found->second;
            }
        }
        const std::string type_name = alias_named ? original : s;
        amc::Type t;
        t.name = type_name;
        t.kind = amc::TypeKind::record;
        t.id = amc::hash_text(type_name, 0x54595045);
        t.size = 1;
        t.align = 1;
        ids.emplace(s, t.id);
        if (alias_named) ids.emplace(original, t.id);
        module.types.push_back(std::move(t));
        return ids.at(s);
    }
    if (const auto *record = qt->getAsCXXRecordDecl()) {
        const auto record_key = qt.getAsString();
        // A typedef alias of an instantiation (`abix_span_i32`) names the
        // concrete ABI entity; record it under the alias so it can be
        // lowered without an explicit export symbol. Restrict this to class
        // template specializations: a nested type or a plain typedef spelled
        // unqualified inside its scope (e.g. `reader` inside `Domain`, or
        // `LayoutHash` for `Hash128`) also differs from its canonical
        // spelling but is not an alias, and renaming the record after it
        // would produce a type name that is not valid in any namespace.
        const bool alias_named = !member_typedef
                              && original != s
                              && plain_identifier(original)
                              && llvm::isa<clang::ClassTemplateSpecializationDecl>(record);
        add_record(const_cast<clang::CXXRecordDecl *>(record), alias_named ? original : std::string());
        auto found = ids.find(alias_named ? original : record_key);
        if (found != ids.end()) {
            ids[s] = found->second;
            return found->second;
        }
        found = ids.find(name(record));
        if (found != ids.end()) {
            ids[s] = found->second;
            return found->second;
        }
        // Incomplete record (a forward declaration such as `struct Counter;`):
        // record it as opaque and register it so a self-referential reference
        // terminates instead of recursing forever. cv-qualifiers are dropped
        // so `DemoBuffer` and `const DemoBuffer` share one opaque type.
        const std::string opaque_name = name(record);
        auto existing = ids.find(opaque_name);
        if (existing != ids.end()) {
            ids.emplace(s, existing->second);
            return existing->second;
        }
        amc::Type t;
        t.name = opaque_name;
        t.kind = amc::TypeKind::record;
        t.id = amc::hash_text(opaque_name, 0x54595045);
        t.size = 1;
        t.align = 1;
        ids.emplace(s, t.id);
        ids.emplace(opaque_name, t.id);
        module.types.push_back(std::move(t));
        return ids.at(s);
    }
    if (const auto *enumeration = qt->getAs<clang::EnumType>()) {
        add_enum(enumeration->getDecl());
        auto found = ids.find(name(enumeration->getDecl()));
        if (found != ids.end()) return found->second;
        return amc::hash_text(s, 0x54595045);
    }
    amc::Type t;
    t.name = s;
    t.size = uint32_t(ctx.getTypeSize(qt) / 8);
    t.align = uint32_t(ctx.getTypeAlign(qt) / 8);
    t.kind = amc::TypeKind::primitive;
    // Primitives are identified by their ABI descriptor, not their spelling,
    // so that ABI-equal types (long/long long, ...) share one TypeID and the
    // abstract C kind (double, long double) is preserved for target-dependent
    // projection (e.g. AVR `double == f32`). Non-scalar types keep the
    // spelling-based identity.
    uint32_t abi = 0;
    const std::string abi_key = primitive_abi_key(ctx, qt, abi);
    t.primitive_abi = abi;
    if (!abi_key.empty()) {
        t.id = amc::hash_text(abi_key, 0x54595045);
        const auto seen = primitive_ids.find(abi_key);
        if (seen != primitive_ids.end()) {
            ids.emplace(s, seen->second);
            return seen->second;
        }
        primitive_ids.emplace(abi_key, t.id);
    } else {
        t.id = amc::hash_text(s, 0x54595045);
    }
    clang::QualType pointee_type;
    bool is_pointer = false;
    if (const auto *p = qt->getAs<clang::PointerType>()) {
        t.kind = amc::TypeKind::pointer;
        t.name = p->getPointeeType().getAsString() + "*";
        t.size = ctx.getTypeSize(qt) / 8;
        t.align = ctx.getTypeAlign(qt) / 8;
        is_pointer = true;
        pointee_type = p->getPointeeType();
    }
    if (const auto *a = llvm::dyn_cast<clang::ConstantArrayType>(qt.getTypePtr())) {
        t.kind = amc::TypeKind::array;
        t.array_count = uint32_t(a->getSize().getZExtValue());
    }
    ids[s] = t.id;
    module.types.push_back(t);
    // Record the pointee AFTER registering this pointer, so a self-referential
    // type (a node with a pointer to itself) terminates instead of recursing.
    if (is_pointer && !pointee_type->isFunctionType() && !pointee_type->isVoidType()) {
        const amc::Hash128 pointee_id = add_type(pointee_type);
        const uint32_t flags = pointee_type.isConstQualified() ? amc::pointee_const : 0u;
        if (!(pointee_id == amc::Hash128{})) module.pointees.push_back({t.id, pointee_id, flags});
    }
    return t.id;
}

uint32_t Extractor::access_flags(clang::AccessSpecifier access) {
    switch (access) {
        case clang::AS_public:    return amc::visibility_flags(amc::Visibility::public_);
        case clang::AS_protected: return amc::visibility_flags(amc::Visibility::protected_);
        case clang::AS_private:   return amc::visibility_flags(amc::Visibility::private_);
        case clang::AS_none:      return amc::visibility_flags(amc::Visibility::none);
    }
    return 0;
}

uint32_t Extractor::calling_convention(clang::FunctionDecl *d) {
    const auto *prototype = d->getType()->getAs<clang::FunctionProtoType>();
    if (!prototype) return 0;
    switch (prototype->getCallConv()) {
        case clang::CC_C:           return 1;
        case clang::CC_X86StdCall:  return 2;
        case clang::CC_X86FastCall: return 3;
        case clang::CC_X86ThisCall: return 4;
        default:                    return d->hasAttr<clang::AArch64SVEPcsAttr>() ? 5 : 0;
    }
}

void Extractor::add_record(clang::CXXRecordDecl *d, const std::string &forced_name) {
    const auto record_name = forced_name.empty() ? name(d) : forced_name;
    if (ids.count(record_name) != 0) return;
    if (d->isDependentContext() || ctx.getCanonicalTagType(d)->isDependentType()) return;
    if (!d->isCompleteDefinition()) return;
    add_namespaces(d);
    amc::Type t;
    t.name = record_name;
    t.kind = amc::TypeKind::record;
    if (d->getDescribedClassTemplate()) t.flags |= amc::type_template_primary;
    t.id = amc::hash_text(t.name, 0x54595045);
    auto &l = ctx.getASTRecordLayout(d);
    auto qt = ctx.getCanonicalTagType(d);
    t.size = uint32_t(ctx.getTypeSize(qt) / 8);
    t.align = uint32_t(ctx.getTypeAlign(qt) / 8);
    t.field_begin = uint32_t(module.fields.size());
    ids[record_name] = t.id;
    // Also resolve references spelled the Clang way (e.g. a specialization
    // named `abix_span<int>` exported under the alias `abix_span_i32`).
    ids[name(d)] = t.id;
    module.types.push_back(t);
    record_source(t.id, d->getLocation());
    const auto record_index = module.types.size() - 1;
    for (const auto &base : d->bases()) {
        amc::Field x;
        x.owner_type = t.id;
        x.name = base.getType().getAsString();
        x.type_id = add_type(base.getType());
        x.flags = amc::field_base | access_flags(base.getAccessSpecifier());
        if (const auto *base_record = base.getType()->getAsCXXRecordDecl()) {
            const clang::CharUnits offset =
                base.isVirtual() ? l.getVBaseClassOffset(base_record) : l.getBaseClassOffset(base_record);
            x.offset = static_cast<uint32_t>(offset.getQuantity());
        }
        module.fields.push_back(std::move(x));
    }
    for (auto *f : d->fields()) {
        // Anonymous implementation fields have no stable ABI name and
        // cannot participate in name-based compatibility or Map IR.
        if (f->getName().empty()) continue;
        const std::string fname = f->getNameAsString();
        // Skip if a field with the same name already exists in this type.
        // (libc++ std::string has both __long and __short inside an
        // anonymous union, each with fields named __data_, __size_, etc.)
        bool dup = false;
        for (uint32_t j = module.types[record_index].field_begin; j < module.fields.size(); ++j) {
            if (module.fields[j].name == fname) {
                dup = true;
                break;
            }
        }
        if (dup) continue;
        amc::Field x;
        x.owner_type = t.id;
        x.name = std::move(fname);
        x.type_id = add_type(f->getType());
        const uint64_t bit_offset = l.getFieldOffset(f->getFieldIndex());
        x.offset = uint32_t(bit_offset / 8);
        x.flags = access_flags(f->getAccess());
        if (f->isBitField()) {
            x.flags |= amc::field_bitfield;
            x.flags |= uint32_t(bit_offset % 8) << amc::field_bit_offset_shift;
            x.flags |= f->getBitWidthValue() << amc::field_bit_width_shift;
        }
        module.fields.push_back(x);
    }
    module.types[record_index].field_count = uint32_t(module.fields.size()) - module.types[record_index].field_begin;
    std::vector<amc::Field> layout_fields;
    for (uint32_t i = 0; i < module.types[record_index].field_count; ++i)
        layout_fields.push_back(module.fields[module.types[record_index].field_begin + i]);
    module.types[record_index].layout_hash =
        amc::canonical_layout_hash(module, module.types[record_index], layout_fields);
}

void Extractor::add_enum(clang::EnumDecl *d) {
    add_namespaces(d);
    if (ids.count(name(d)) != 0) return;
    amc::Type t;
    t.name = name(d);
    t.kind = amc::TypeKind::enumeration;
    t.id = amc::hash_text(t.name, 0x54595045);
    t.size = uint32_t(ctx.getTypeSize(d->getIntegerType()) / 8);
    t.align = uint32_t(ctx.getTypeAlign(d->getIntegerType()) / 8);
    ids[t.name] = t.id;
    module.types.push_back(t);
    record_source(t.id, d->getLocation());
}

void Extractor::add_alias(clang::TypedefNameDecl *d) {
    add_namespaces(d);
    const auto alias_name = name(d);
    if (ids.count(alias_name) != 0) return;
    const auto underlying_type = d->getUnderlyingType();
    // An alias of a template instantiation names a concrete ABI entity:
    // record the instantiated record's layout under the alias name, so the
    // exported name is stable and valid in every target language. This is the
    // `span<int> -> abix_span_i32` step; it does not by itself claim the type
    // satisfies any ABIX-STD contract.
    if (auto *record = underlying_type.getCanonicalType()->getAsCXXRecordDecl();
        record && record->isCompleteDefinition() && llvm::isa<clang::ClassTemplateSpecializationDecl>(record)) {
        add_record(record, alias_name);
        const auto found = ids.find(alias_name);
        if (found != ids.end()) {
            ids[underlying_type.getAsString()] = found->second;
            ids[underlying_type.getCanonicalType().getAsString()] = found->second;
            return;
        }
    }
    amc::Type t;
    t.name = alias_name;
    t.kind = amc::TypeKind::alias;
    t.id = amc::hash_text(t.name, 0x54595045);
    // Guard against incomplete underlying types (an uninstantiated template
    // must not be queried for its layout).
    if (underlying_type->isIncompleteType()) {
        t.size = t.align = 1;
    } else {
        t.size = uint32_t(ctx.getTypeSize(underlying_type) / 8);
        t.align = uint32_t(ctx.getTypeAlign(underlying_type) / 8);
    }
    ids[t.name] = t.id;
    const auto underlying = add_type(underlying_type);
    t.field_begin = uint32_t(module.fields.size());
    t.field_count = 1;
    module.types.push_back(t);
    record_source(t.id, d->getLocation());
    module.fields.push_back({t.id, "underlying", underlying, 0, 0});
}

void Extractor::add_inherited_methods(clang::CXXRecordDecl *d) {
    const std::string prefix = name(d) + "::";
    std::vector<std::string> methods;
    for (const auto &symbol : wanted)
        if (symbol.compare(0, prefix.size(), prefix) == 0) methods.push_back(symbol.substr(prefix.size()));
    if (methods.empty() || !d->isCompleteDefinition()) return;
    std::set<std::string> shadowed;
    for (const auto *method : d->methods())
        shadowed.insert(method->getNameAsString());
    std::vector<clang::CXXRecordDecl *> work;
    for (const auto &base : d->bases())
        if (auto *base_record = base.getType()->getAsCXXRecordDecl()) work.push_back(base_record);
    while (!work.empty()) {
        clang::CXXRecordDecl *base = work.back();
        work.pop_back();
        const std::string base_name = name(base);
        for (const auto *method : base->methods()) {
            const std::string method_name = method->getNameAsString();
            if (std::find(methods.begin(), methods.end(), method_name) == methods.end()) continue;
            if (shadowed.count(method_name) != 0) continue;
            const std::string qualified = base_name + "::" + method_name;
            bool recorded = false;
            for (const auto &function : module.functions)
                if (function.name == qualified) {
                    recorded = true;
                    break;
                }
            if (!recorded) add_function(const_cast<clang::CXXMethodDecl *>(method));
        }
        for (const auto *method : base->methods())
            shadowed.insert(method->getNameAsString());
        for (const auto &up : base->bases())
            if (auto *up_record = up.getType()->getAsCXXRecordDecl()) work.push_back(up_record);
    }
}

amc::Hash128 Extractor::register_native_type(const std::string &record_name) {
    auto found = ids.find(record_name);
    if (found != ids.end()) return found->second;
    amc::Type t;
    t.name = record_name;
    t.kind = amc::TypeKind::record;
    t.id = amc::hash_text(record_name, 0x54595045);
    t.size = t.align = 1;
    ids.emplace(record_name, t.id);
    module.types.push_back(std::move(t));
    return module.types.back().id;
}

bool Extractor::detect_native(
    clang::QualType qt, const amc::lang::AdaptDecl *&decl, NativeKind &kind, clang::QualType &element) {
    decl = nullptr;
    kind = NativeKind::none;
    const bool lvalue_ref = qt->isLValueReferenceType();
    if (lvalue_ref && !qt.getNonReferenceType().isConstQualified()) return false;
    const clang::QualType value = qt.getNonReferenceType().getUnqualifiedType();
    const auto *record = value->getAsCXXRecordDecl();
    if (record == nullptr) return false;
    const std::string qualified = record->getQualifiedNameAsString();
    NativeKind candidate = NativeKind::none;
    if (qualified == "std::basic_string")
        candidate = NativeKind::string;
    else if (qualified == "std::basic_string_view")
        candidate = NativeKind::string_view;
    else if (qualified == "std::vector")
        candidate = NativeKind::vector;
    else if (qualified == "std::span")
        candidate = NativeKind::span;
    else
        return false;
    const auto *spec = llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(record);
    if (spec == nullptr) return false;
    const auto &args = spec->getTemplateArgs();
    if (args.size() < 1 || args[0].getKind() != clang::TemplateArgument::Type) return false;
    if (candidate == NativeKind::string || candidate == NativeKind::string_view) {
        // The code unit must be C `char`; the boundary view is UTF-8 bytes.
        const clang::QualType unit = args[0].getAsType().getUnqualifiedType();
        if (!unit->isCharType() || ctx.getTypeSize(unit) != 8) return false;
        element = unit;
    } else {
        const clang::QualType value_type = args[0].getAsType();
        if (candidate == NativeKind::span && !value_type.isConstQualified()) return false;
        element = value_type.getUnqualifiedType();
    }
    for (const auto &adapt : adapts) {
        if (native_kind_of(adapt.native) != candidate) continue;
        if ((candidate == NativeKind::vector || candidate == NativeKind::span)
            && !element_matches(adapt.element, element))
            continue;
        decl = &adapt;
        kind = candidate;
        return true;
    }
    return false;
}

bool Extractor::element_matches(const std::string &declared, clang::QualType element) const {
    if (declared.empty()) return false;
    if (normalise_element(declared) == normalise_element(element.getAsString())
        || normalise_element(declared) == normalise_element(element.getCanonicalType().getAsString()))
        return true;
    uint32_t size = 0;
    bool is_signed = false;
    if (!fixed_width_int(declared, size, is_signed)) return false;
    const clang::QualType canonical = element.getCanonicalType();
    return canonical->isIntegerType()
        && ctx.getTypeSize(canonical) / 8 == size
        && canonical->isSignedIntegerType() == is_signed;
}

void Extractor::add_function(clang::FunctionDecl *d) {
    amc::Function f;
    if (const auto *method = llvm::dyn_cast<clang::CXXMethodDecl>(d)) {
        if (const auto *owner = method->getParent()) {
            f.owner_type = amc::hash_text(name(owner), 0x54595045);
            if (ids.count(name(owner)) == 0) add_record(const_cast<clang::CXXRecordDecl *>(owner));
        }
    }
    f.name = name(d);
    f.calling_convention = calling_convention(d);
    f.flags = access_flags(d->getAccess());
    f.return_type = add_type(d->getReturnType());
    for (auto *p : d->parameters()) {
        amc::Parameter q;
        q.name = p->getNameAsString();
        const amc::lang::AdaptDecl *decl = nullptr;
        NativeKind kind = NativeKind::none;
        clang::QualType element;
        if (detect_native(p->getType(), decl, kind, element)) {
            NativeAdapt adapt;
            adapt.kind = static_cast<uint32_t>(kind);
            adapt.element = element.getAsString();
            adapt.element_id = add_type(element);
            adapt.target = decl->target;
            adapt.native_type = register_native_type(native_type_name(kind, adapt.element));
            adaptations.push_back(std::move(adapt));
            q.type_id = adaptations.back().native_type;
        } else {
            q.type_id = add_type(p->getType());
        }
        f.parameters.push_back(q);
    }
    f.signature = amc::signature_hash(f);
    module.functions.push_back(std::move(f));
}

}   // namespace amc::lang::frontend::cpp
