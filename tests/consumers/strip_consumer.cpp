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
// Strip-consistency consumer (AI-PM).
//
// Compiled against a generated `amc_generated.hpp`. It proves that the ABIX
// runtime contract is expressed purely through TypeID/LayoutHash metadata:
// registering the generated module and resolving a type by TypeID must keep
// working after `.abix.names` has been stripped, while name lookup — which is
// diagnostic only — is never available at runtime.
#include "amc_generated.hpp"

int main() {
    skl::abix::runtime::Registry<16> registry;
    if (registry.register_module(amc_generated::amc_module) != skl::abix::runtime::RegisterStatus::ok) return 1;
    if (registry.find_by_id(amc_generated::AmcTestFoo_ABIX::type_id) == nullptr) return 2;
    if (registry.find_by_name("AmcTestFoo") != nullptr) return 3;
    return 0;
}
