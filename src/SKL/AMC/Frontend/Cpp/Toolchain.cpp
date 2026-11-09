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

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#if defined(__APPLE__)
#  include <llvm/ADT/SmallString.h>
#  include <llvm/Support/Path.h>
#  include <unistd.h>
#endif

#ifndef ABIX_CLANG_RESOURCE_DIR
#  define ABIX_CLANG_RESOURCE_DIR ""
#endif

namespace skl::amc::lang::cpp {

const char * const kClangResourceDir = ABIX_CLANG_RESOURCE_DIR;

#if defined(__APPLE__)
// Derive the Homebrew LLVM installation prefix from the known resource-dir.
//   kClangResourceDir  →  /opt/homebrew/opt/llvm/lib/clang/22
//   brew_prefix        →  /opt/homebrew/opt/llvm
//
// Returns the empty string when the prefix cannot be derived.
// On non-macOS platforms this always returns empty.
static std::string brew_llvm_prefix() {
    if (!kClangResourceDir[0]) return {};
    llvm::SmallString<128> pfx(kClangResourceDir);
    llvm::sys::path::remove_filename(pfx);
    llvm::sys::path::remove_filename(pfx);
    llvm::sys::path::remove_filename(pfx);
    llvm::SmallString<128> sanity(pfx);
    llvm::sys::path::append(sanity, "include", "c++", "v1");
    return access(sanity.c_str(), F_OK) == 0 ? pfx.c_str() : std::string{};
}

// Detect the macOS SDK path and append flags that mirror what the Clang
// driver would normally set when invoked from the command line.
//
// When loaded as a library (no config file is read), Clang does not
// automatically know its resource directory or the active SDK sysroot.
// This function fills those gaps.
//
// Include order (required by libc++ <stddef.h>):
//   1. libc++ headers  (Brew LLVM: -cxx-isystem via -nostdinc++)
//   2. Clang built-ins (via -resource-dir + -isystem)
//   3. System headers  (via -isysroot)
//
// The most important design rule is that **all three sets of headers must
// come from the same LLVM distribution**.  On macOS the default Xcode SDK
// ships its own libc++ at <sdk>/usr/include/c++/v1, but that version may
// be incompatible with the Homebrew LLVM's Clang built-in headers (e.g.
// std::string internal layout differs, leading to duplicate member errors).
//
// We therefore:
//   a. Derive the Homebrew LLVM prefix from the resource-dir known at
//      build time (kClangResourceDir);
//   b. Use -nostdinc++ to suppress the default C++ standard library search
//      (which would otherwise pick up the SDK's libc++);
//   c. Use -cxx-isystem to re-add only the Homebrew LLVM's own libc++.
//
// All three values are derived automatically: the resource directory is
// detected at build time via `clang -print-resource-dir` and injected through
// the ABIX_CLANG_RESOURCE_DIR compile definition; the SDK path is looked up
// at runtime from the SDKROOT environment variable or via xcrun.
void append_macos_sysroot(std::vector<std::string> &flags) {
    // ----- 1. Detect SDK path -------------------------------------------
    auto detect_sdk = []() -> std::string {
        if (const char *sdk = std::getenv("SDKROOT")) return sdk;
        FILE *fp = popen("xcrun --sdk macosx --show-sdk-path 2>/dev/null", "r");
        if (!fp) return {};
        char buf[4'096] = {0};
        std::string result;
        if (std::fgets(buf, sizeof(buf), fp)) result = buf;
        pclose(fp);
        while (!result.empty() && std::isspace(static_cast<unsigned char>(result.back())))
            result.pop_back();
        return result;
    };

    // ----- 2. Check for existing configuration ----------------------------
    // If the user (or the .abic.toml) already provides explicit sysroot or
    // a valid -isystem, don't override their choices.
    for (size_t i = 0; i < flags.size(); ++i) {
        const auto &f = flags[i];
        if (f == "-isysroot" || f.find("--sysroot") == 0) return;
        if (f == "-isystem" && i + 1 < flags.size()) {
            if (access(flags[i + 1].c_str(), F_OK) == 0) return;
        }
    }

    // ----- 3. Derive Homebrew LLVM prefix --------------------------------
    const std::string brew_prefix = brew_llvm_prefix();
    const bool have_brew_libcxx = !brew_prefix.empty();

    // ----- 4. macOS SDK setup --------------------------------------------
    //
    // Include order (all from the same LLVM distribution):
    //   1. libc++ headers   (Homebrew LLVM via -isystem)
    //   2. Clang built-ins  (<resource>/include via -isystem)
    //   3. System headers   (-isysroot)
    //
    // IMPORTANT: We use `-isystem` (NOT `-cxx-isystem`) for brew libc++.
    // In Clang's header search, `-isystem` paths are searched *before*
    // `-cxx-isystem` paths.  If we used `-cxx-isystem`, the resource-dir's
    // `-isystem` path would win over libc++, and `<stddef.h>` would fail
    // because it finds Clang's `<stddef.h>` instead of libc++'s wrapper.
    // ---------------------------------------------------------------------
    {
        std::string sdk = detect_sdk();
        if (!sdk.empty()) {
            // (a) libc++ – from Brew LLVM (compatible with our resource-dir)
            if (have_brew_libcxx) {
                llvm::SmallString<128> brew_libcxx(brew_prefix);
                llvm::sys::path::append(brew_libcxx, "include", "c++", "v1");
                flags.push_back("-nostdinc++");
                flags.push_back("-isystem");
                flags.push_back(brew_libcxx.c_str());
            }

            // (b) Clang built-in headers (stdarg.h, stddef.h, etc.)
            if (kClangResourceDir[0]) {
                flags.push_back("-resource-dir");
                flags.push_back(kClangResourceDir);
                llvm::SmallString<128> res_inc(kClangResourceDir);
                llvm::sys::path::append(res_inc, "include");
                if (access(res_inc.c_str(), F_OK) == 0) {
                    flags.push_back("-isystem");
                    flags.push_back(res_inc.c_str());
                }
            }

            // (c) SDK sysroot – system C headers, frameworks, etc.
            flags.push_back("-isysroot");
            flags.push_back(std::move(sdk));
        }
    }

    // ----- 5. Fallback (no SDK) ------------------------------------------
    // If no SDK is available, at least set the resource directory and
    // libc++ from the Brew LLVM prefix, so basic analysis still works.
    if (!flags.empty() && std::find(flags.begin(), flags.end(), "-resource-dir") == flags.end()) {
        // (a) libc++ from Brew LLVM (must use -isystem, not -cxx-isystem,
        //     because -cxx-isystem has lower priority than -isystem and
        //     would lose to the resource-dir's built-in headers below)
        if (have_brew_libcxx) {
            llvm::SmallString<128> brew_libcxx(brew_prefix);
            llvm::sys::path::append(brew_libcxx, "include", "c++", "v1");
            flags.push_back("-nostdinc++");
            flags.push_back("-isystem");
            flags.push_back(brew_libcxx.c_str());
        }

        // (b) Clang built-in headers
        if (kClangResourceDir[0]) {
            flags.push_back("-resource-dir");
            flags.push_back(kClangResourceDir);
            llvm::SmallString<128> res_inc(kClangResourceDir);
            llvm::sys::path::append(res_inc, "include");
            if (access(res_inc.c_str(), F_OK) == 0) {
                flags.push_back("-isystem");
                flags.push_back(res_inc.c_str());
            }
        }
    }
}
#endif

#if !defined(__APPLE__)
// Point the clang we are linked against at the resource directory detected at
// build time (`clang -print-resource-dir`, injected as ABIX_CLANG_RESOURCE_DIR).
// A `.abic.toml` used to spell this out by hand -- `-isystem
// /usr/lib/clang/22/include` -- which pins a toolchain version into a config
// file: it goes stale on the next upgrade, and on a case-insensitive filesystem
// it silently resolves a directory the source tree does not have. A config can
// still make its own choice by naming `-resource-dir` itself.
void append_clang_resource_dir(std::vector<std::string> &flags) {
    if (!kClangResourceDir[0]) return;
    for (const auto &f : flags) {
        if (f == "-resource-dir") return;
    }
    flags.push_back("-resource-dir");
    flags.push_back(kClangResourceDir);
}
#endif

}   // namespace skl::amc::lang::cpp
