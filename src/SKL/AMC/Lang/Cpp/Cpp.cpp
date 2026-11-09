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
#include "SKL/AMC/Backend/Cpp/Cpp.h"
#include "SKL/AMC/Frontend/Cpp/Cpp.h"
#include "SKL/AMC/Lang/Lang.h"

#include <fmt/format.h>

int main(int argc, char **argv) {
#ifdef __APPLE__
    if (!skl::amc::lang::cpp::kClangResourceDir[0]) {
        fmt::print(stderr,
            "warning: ABIX_CLANG_RESOURCE_DIR not set at build time; "
            "built-in headers (stdarg.h, etc.) may not be found.\n");
    }
#endif

    static const skl::amc::lang::Provider provider{
        "cpp",
        {
          {skl::amc::support::Capability::FRONTEND, skl::amc::lang::cpp::run_frontend},
          {skl::amc::support::Capability::BACKEND, skl::amc::lang::cpp::run_backend},
          },
        "0.4.0",
        {"templates", "interfaces"},
    };
    return skl::amc::lang::run_provider(argc, argv, provider);
}
