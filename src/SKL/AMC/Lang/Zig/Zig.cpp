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
#include "SKL/AMC/Backend/Zig/Zig.h"
#include "SKL/AMC/Lang/Lang.h"

int main(int argc, char **argv) {
    static const amc::lang::Provider provider{
        "zig",
        {
          {"backend", amc::lang::zig::backend},
          },
        "0.2.0",
        {"target"},
    };
    return amc::lang::run_provider(argc, argv, provider);
}
