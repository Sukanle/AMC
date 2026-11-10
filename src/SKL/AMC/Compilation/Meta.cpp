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
#include "SKL/AMC/Compilation/Meta.h"
#include "SKL/AMC/Private/Support/File.h"

#include "SKL/AMC/Artifact/Metadata.h"

#include <vector>


namespace skl::amc::compilation {

bool write_abix_meta(const ABIModule &module, const std::filesystem::path &artifact, std::string &error) {
    MetadataOptions options;
    std::vector<uint8_t> region;
    if (!build_metadata_region(module, options, region, error)) return false;
    MetadataHeader header;
    if (!read_metadata_header(region, header, error)) return false;
    const std::string path = artifact.string() + ".meta";
    if (auto err = amc::support::write_text_file(path, metadata_meta_document(module, header))) {
        error = "cannot write metadata output: " + path + ": " + amc::support::error_message(std::move(err));
        return false;
    }
    return true;
}

}   // namespace skl::amc::compilation
