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
#define AMC_GENERATED_DECLARE_NATIVE_TYPE_TRAITS
#include <SKL/AMC/Core/Core.h>
#include "amc_core.hpp"

int main() {
    skl::abix::runtime::Registry<128> registry;
    if (registry.register_module(amc_generated::amc_module) != skl::abix::runtime::RegisterStatus::ok) return 1;
    return registry.type_of<amc::AbiModule>()
                && registry.type_of<amc::MapOperation>()
                && registry.type_of<amc::CompatibilityRecord>()
             ? 0
             : 2;
}
