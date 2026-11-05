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
#ifndef AMC_COMPILATION_PROVIDER_H
#define AMC_COMPILATION_PROVIDER_H

#include <filesystem>
#include <string>
#include <vector>

namespace amc::compilation {

// One entry of `AMC/providers.toml`. `roles` contains `frontend` and/or
// `backend`; `protocol` is `jsonl-v1` for provider processes and `direct` for
// built-in backends such as the Aue/Lua contract writer.
struct ProviderManifestEntry {
    std::string language;
    std::string executable;
    std::vector<std::string> roles;
    std::string protocol;
    std::string contract;
    std::vector<std::string> features;
};

struct ProviderManifest {
    uint32_t version = 1;
    std::vector<ProviderManifestEntry> providers;
};

bool load_provider_manifest(
    const std::filesystem::path &executable_dir, ProviderManifest &manifest, std::string &error);
const ProviderManifestEntry *find_provider_manifest(const ProviderManifest &manifest, const std::string &language);
std::string describe_provider(const ProviderManifestEntry &provider);

// Resolve the `amc-<language>` provider executable that sits next to `amc`.
// `base_provider` is the path to the reference `amc-cpp` provider computed by
// the driver; the C++ provider is returned unchanged for the default language.
std::filesystem::path provider_executable(
    const std::filesystem::path &base_provider, const std::string &language);

// Run a provider capability over the JSON-lines IPC protocol.
bool dispatch_provider(const std::filesystem::path &provider, const char *capability,
    const std::filesystem::path &input, const std::filesystem::path &output, std::string &error);

// Dispatch a provider `backend` capability, defaulting the output to
// `amc_generated<extension>` when `destination` does not already carry it.
int generate(
    const std::filesystem::path &provider, const std::filesystem::path &input, const std::filesystem::path &destination,
    const std::string &extension);

}   // namespace amc::compilation

#endif   // AMC_COMPILATION_PROVIDER_H
