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
#include "SKL/AMC/Frontend/Cpp/Cpp.h"

#include "SKL/AMC/Core/Core.h"
#include "SKL/AMC/Core/Generated.h"
#include "SKL/AMC/Lang/Lang.h"
#include "SKL/AMC/Private/Frontend/Cpp/Extractor.h"
#include "SKL/AMC/Private/Frontend/Cpp/Thunks.h"
#include "SKL/AMC/Semantic/Identity.h"
#include "SKL/AMC/Semantic/Model.h"
#include "SKL/AMC/Semantic/Serialize.h"
#include "SKL/AMC/Semantic/Sync.h"

// The Clang resource directory detected at build time (`clang -print-resource-dir`).
// It is injected by CMake for the `amc-cpp` target; empty if unavailable.
#ifndef ABIX_CLANG_RESOURCE_DIR
#  define ABIX_CLANG_RESOURCE_DIR ""
#endif

#include <clang/AST/ASTConsumer.h>
#include <clang/Tooling/CompilationDatabase.h>
#include <clang/Tooling/JSONCompilationDatabase.h>
#include <clang/Tooling/Tooling.h>

#include <llvm/ADT/SmallString.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/Path.h>

#include <fmt/format.h>

#include <cstdlib>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace amc::lang::cpp {
namespace {

namespace frontend = frontend::cpp;

class Consumer : public clang::ASTConsumer {
public:
    Consumer(const std::set<std::string> &w, amc::AbiModule &m, const std::vector<amc::lang::AdaptDecl> &a,
        std::vector<frontend::NativeAdapt> &out, amc::semantic::SemanticModule &s)
        : wanted(w)
        , module(m)
        , adapts(a)
        , adaptations(out)
        , semantic(s) {}
    void HandleTranslationUnit(clang::ASTContext &c) override {
        if (amc::lang::cancellation_requested()) return;
        frontend::Extractor x(c, wanted, module, adapts, adaptations, semantic);
        x.TraverseDecl(c.getTranslationUnitDecl());
    }

private:
    const std::set<std::string> &wanted;
    amc::AbiModule &module;
    const std::vector<amc::lang::AdaptDecl> &adapts;
    std::vector<frontend::NativeAdapt> &adaptations;
    amc::semantic::SemanticModule &semantic;
};
class Action : public clang::ASTFrontendAction {
public:
    Action(const std::set<std::string> &w, amc::AbiModule &m, const std::vector<amc::lang::AdaptDecl> &a,
        std::vector<frontend::NativeAdapt> &out, amc::semantic::SemanticModule &s)
        : wanted(w)
        , module(m)
        , adapts(a)
        , adaptations(out)
        , semantic(s) {}
    std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(clang::CompilerInstance &, clang::StringRef) override {
        return std::make_unique<Consumer>(wanted, module, adapts, adaptations, semantic);
    }

private:
    const std::set<std::string> &wanted;
    amc::AbiModule &module;
    const std::vector<amc::lang::AdaptDecl> &adapts;
    std::vector<frontend::NativeAdapt> &adaptations;
    amc::semantic::SemanticModule &semantic;
};
class Factory : public clang::tooling::FrontendActionFactory {
public:
    Factory(const std::set<std::string> &w, amc::AbiModule &m, const std::vector<amc::lang::AdaptDecl> &a,
        std::vector<frontend::NativeAdapt> &out, amc::semantic::SemanticModule &s)
        : wanted(w)
        , module(m)
        , adapts(a)
        , adaptations(out)
        , semantic(s) {}
    std::unique_ptr<clang::FrontendAction> create() override {
        return std::make_unique<Action>(wanted, module, adapts, adaptations, semantic);
    }

private:
    const std::set<std::string> &wanted;
    amc::AbiModule &module;
    const std::vector<amc::lang::AdaptDecl> &adapts;
    std::vector<frontend::NativeAdapt> &adaptations;
    amc::semantic::SemanticModule &semantic;
};

}   // namespace

int run_frontend(const char *config_path, const char *output_path) {
    if (amc::lang::cancellation_requested()) return amc::lang::kCapabilityCancelled;
    amc::lang::Config c;
    std::string e;
    if (!amc::lang::config_load(config_path, "cpp", c, e)) {
        fmt::print(stderr, "{}\n", e);
        return EXIT_FAILURE;
    }
    llvm::SmallString<256> config_path_abs(config_path);
    if (const std::error_code ec = llvm::sys::fs::make_absolute(config_path_abs)) {
        fmt::print(stderr, "cannot resolve {}: {}\n", config_path, ec.message());
        return EXIT_FAILURE;
    }
    llvm::SmallString<256> base(config_path_abs);
    llvm::sys::path::remove_filename(base);
    if (!c.db.empty() && llvm::sys::path::is_relative(c.db)) {
        llvm::SmallString<256> db_path(base);
        llvm::sys::path::append(db_path, c.db);
        c.db = db_path.str().str();
    }
    // Clang needs absolute paths, but `c.files` is left exactly as declared:
    // `expose_thunks` emits it verbatim into the generated `#include`, so a
    // declared relative path stays relative instead of leaking a machine-
    // specific absolute prefix. Only the tool file list is resolved here.
    std::vector<std::string> tool_files;
    tool_files.reserve(c.files.size());
    for (const auto &f : c.files) {
        if (llvm::sys::path::is_relative(f)) {
            llvm::SmallString<256> path(base);
            llvm::sys::path::append(path, f);
            tool_files.push_back(path.str().str());
        } else {
            tool_files.push_back(f);
        }
    }
#if defined(__APPLE__)
    append_macos_sysroot(c.flags);
#else
    append_clang_resource_dir(c.flags);
#endif
    std::string db_error;
    std::unique_ptr<clang::tooling::CompilationDatabase> db;
    if (!c.db.empty())
        db = clang::tooling::JSONCompilationDatabase::loadFromFile(
            c.db, db_error, clang::tooling::JSONCommandLineSyntax::AutoDetect);
    else
        db = std::make_unique<clang::tooling::FixedCompilationDatabase>(base.str(), c.flags);
    if (!db) {
        fmt::print(stderr, "{}\n", db_error);
        return EXIT_FAILURE;
    }
    std::set<std::string> wanted(c.symbols.begin(), c.symbols.end());
    // An exposed type and its methods must be recorded even when they are not
    // explicit export symbols: the generator needs their layout and signatures.
    for (const auto &expose : c.exposed) {
        wanted.insert(expose.type);
        for (const auto &method : expose.methods)
            wanted.insert(expose.type + "::" + method);
    }
    amc::AbiModule module;
    module.package_name = "cpp";
    amc::semantic::SemanticModule semantic;
    semantic.package_name = "cpp";
    semantic.rules_version = amc::semantic::semantic_rules_version;

    // Generic instantiation protocol, stage A/B: AMC coordinates the request and
    // the C++ frontend (Clang/Sema) performs the instantiation and constraint
    // check. We force it with an anchor translation unit that requires each
    // requested instantiation to be complete, so a constraint failure surfaces
    // as a normal compiler diagnostic instead of a silent opaque record.
    std::string anchor_path;
    std::string anchor;
    if (!c.instantiations.empty()) {
        // The anchor lives in `base/.amc/`, so it needs the resolved paths.
        for (const auto &file : tool_files)
            anchor += "#include \"" + file + "\"\n";
        for (const auto &inst : c.instantiations) {
            std::string concrete = inst.template_name + "<";
            for (size_t i = 0; i < inst.args.size(); ++i) {
                if (i != 0) concrete += ", ";
                concrete += inst.args[i];
            }
            concrete += ">";
            if (!inst.as.empty()) anchor += "using " + inst.as + " = " + concrete + ";\n";
            anchor += "static_assert(sizeof(" + concrete + ") > 0, \"abix instantiate\");\n";
        }
        llvm::SmallString<256> anchor_file(base);
        llvm::sys::path::append(anchor_file, ".amc", "instantiate.hpp");
        anchor_path = anchor_file.str().str();
        llvm::SmallString<256> anchor_dir(base);
        llvm::sys::path::append(anchor_dir, ".amc");
        if (const std::error_code ec = llvm::sys::fs::create_directories(anchor_dir)) {
            fmt::print(stderr, "cannot create {}: {}\n", anchor_dir.str().str(), ec.message());
            return EXIT_FAILURE;
        }
        tool_files = {anchor_path};
    }

    std::vector<frontend::NativeAdapt> adaptations;
    Factory factory(wanted, module, c.adaptations, adaptations, semantic);
    clang::tooling::ClangTool tool(*db, tool_files);
    if (!anchor_path.empty()) tool.mapVirtualFile(anchor_path, anchor);
    // Report the include closure so AMC's instantiation cache can invalidate on
    // any dependency (including headers it does not otherwise know about). The
    // depfile is written next to the artifact as `<output>.d`.
    const std::string depfile = std::string(output_path) + ".d";
    const std::string resource_dir = ABIX_CLANG_RESOURCE_DIR;
    tool.appendArgumentsAdjuster(
        [depfile, resource_dir](const clang::tooling::CommandLineArguments &cmd, llvm::StringRef) {
            clang::tooling::CommandLineArguments out;
            // Always parse as C++. Without this a bare `.h` is treated as a C header
            // and rejects `-std=c++17`. `cmd[0]` is the compiler word, so the `-x`
            // selector must come after it (before the flags and the input).
            if (!cmd.empty()) {
                out.push_back(cmd[0]);
                out.push_back("-x");
                out.push_back("c++");
                out.insert(out.end(), cmd.begin() + 1, cmd.end());
            } else {
                out = cmd;
            }
            // Point the driver at its builtin headers (<stddef.h>, ...). ClangTool
            // does not locate the resource directory on its own, so a header that
            // includes <stddef.h> fails with "file not found".
            if (!resource_dir.empty()) out.push_back("-resource-dir=" + resource_dir);
            // Report the include closure so AMC's instantiation cache can invalidate
            // on any dependency (including headers it does not otherwise know about).
            out.push_back("-MD");
            out.push_back("-MF" + depfile);
            out.push_back("-MT");
            out.push_back(depfile);
            return out;
        });
    if (tool.run(&factory) != 0) {
        fmt::print(stderr, "clang analysis failed\n");
        return EXIT_FAILURE;
    }
    if (amc::lang::cancellation_requested()) return amc::lang::kCapabilityCancelled;
    frontend::ThunkEmitter emitter(module, c, adaptations, base, semantic);
    if (!emitter.run(e)) {
        fmt::print(stderr, "{}\n", e);
        return EXIT_FAILURE;
    }
    // Complete the semantic view after thunk registration: every ABI type and
    // function now has a semantic counterpart with a direct `abi_type` /
    // `abi_signature` link, so lowering does not have to re-identify entities
    // by spelling.
    amc::semantic::synchronize_from_abi(module, semantic);
    if (amc::lang::cancellation_requested()) return amc::lang::kCapabilityCancelled;
    if (!amc::write_abix(module, output_path, e)) {
        fmt::print(stderr, "{}\n", e);
        return EXIT_FAILURE;
    }
    if (!semantic.types.empty()
        || !semantic.functions.empty()
        || !semantic.records.empty()
        || !semantic.templates.empty()
        || !semantic.instances.empty()
        || !semantic.ownership.empty()) {
        const std::string semantic_path = std::string(output_path) + ".semantic";
        if (!amc::semantic::write_semantic_json(semantic, semantic_path, e)) {
            fmt::print(stderr, "{}\n", e);
            return EXIT_FAILURE;
        }
    }
    return EXIT_SUCCESS;
}
}   // namespace amc::lang::cpp
