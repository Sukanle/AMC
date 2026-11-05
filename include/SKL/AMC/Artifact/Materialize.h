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
#ifndef AMC_MATERIALIZE_H
#define AMC_MATERIALIZE_H

#include "SKL/AMC/Core/Core.h"

#include "SKL/ABIX/Metadata/Descriptor.h"

#include <stdint.h>
#include <string>
#include <vector>

namespace amc {

// AI-PM: runtime projection (materialization).
//
// The Metadata Region is the compact, pointer-free ABI image. This turns it
// back into the pointer-rich `ModuleDescriptor` the runtime registry consumes,
// so generated code can register a region-loaded module without any
// compile-time descriptor arrays. The struct owns all backing storage and must
// not be copied or moved after build(): `descriptor` points into its vectors.
struct MaterializedModule {
    std::vector<skl::abix::metadata::FieldDescriptor> fields;
    std::vector<skl::abix::metadata::TypeDescriptor> types;
    std::vector<skl::abix::metadata::ParameterDescriptor> parameters;
    std::vector<skl::abix::metadata::FunctionDescriptor> functions;
    std::vector<skl::abix::metadata::SymbolDescriptor> symbols;
    std::vector<skl::abix::model::TypeDesc> canonical_types;
    std::vector<skl::abix::model::TypeLayout> canonical_layouts;
    std::string name;
    std::string version;
    skl::abix::metadata::ModuleDescriptor descriptor{};

    MaterializedModule() = default;
    MaterializedModule(const MaterializedModule &) = delete;
    MaterializedModule &operator=(const MaterializedModule &) = delete;

    bool build(const AbiModule &module, std::string &error);
    bool build_from_region(const uint8_t *data, size_t size, std::string &error);
    bool build_from_region(const std::vector<uint8_t> &region, std::string &error);
};

}   // namespace amc

#endif
