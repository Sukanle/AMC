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
#include "SKL/AMC/Compilation/Provider.h"

#include "SKL/AMC/Core/Json.h"
#include "SKL/AMC/Util/Error.h"

#include <stdint.h>
#include <stdlib.h>

#include <chrono>
#include <exception>
#include <string>
#include <system_error>
#include <vector>

#include <fmt/format.h>

#include <toml++/toml.h>

#if defined(_WIN32)
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#else
#  include <csignal>
#  include <poll.h>
#  include <sys/wait.h>
#  include <unistd.h>
#endif

namespace amc::compilation {
namespace fs = std::filesystem;
using amc::util::fail;

namespace {

fs::path manifest_path(const fs::path &executable_dir) {
    if (const char *override_path = std::getenv("AMC_PROVIDERS_FILE");
        override_path != nullptr && *override_path != '\0')
        return fs::path(override_path);
    const fs::path beside_executable = executable_dir / "amc-providers.toml";
    std::error_code ec;
    if (fs::is_regular_file(beside_executable, ec)) return beside_executable;
#ifdef AMC_DEFAULT_PROVIDERS_FILE
    const fs::path source_tree = AMC_DEFAULT_PROVIDERS_FILE;
    if (fs::is_regular_file(source_tree, ec)) return source_tree;
#endif
    return beside_executable;
}

bool read_string_array(const toml::table &table, const char *key, std::vector<std::string> &out, std::string &error) {
    out.clear();
    const auto *entries = table[key].as_array();
    if (entries == nullptr) return true;
    for (const auto &entry : *entries) {
        const auto value = entry.value<std::string>();
        if (!value || value->empty()) {
            error = fmt::format("`{}` must contain non-empty strings", key);
            return false;
        }
        out.push_back(*value);
    }
    return true;
}

}   // namespace

bool load_provider_manifest(const fs::path &executable_dir, ProviderManifest &manifest, std::string &error) {
    manifest = {};
    const fs::path path = manifest_path(executable_dir);
    std::error_code ec;
    if (!fs::is_regular_file(path, ec)) {
        error = "provider manifest not found: " + path.string();
        return false;
    }

    try {
        const toml::table table = toml::parse_file(path.string());
        const auto version = table["version"].value<int64_t>();
        if (!version || *version != 1) {
            error = fmt::format("provider manifest {}: unsupported or missing version (expected 1)", path.string());
            return false;
        }
        manifest.version = static_cast<uint32_t>(*version);

        const auto *entries = table["provider"].as_array();
        if (entries == nullptr || entries->empty()) {
            error = fmt::format("provider manifest {}: at least one [[provider]] is required", path.string());
            return false;
        }

        for (const auto &entry : *entries) {
            const auto *spec = entry.as_table();
            if (spec == nullptr) {
                error = fmt::format("provider manifest {}: every [[provider]] must be a table", path.string());
                return false;
            }

            ProviderManifestEntry provider;
            const auto language = (*spec)["language"].value<std::string>();
            if (!language || language->empty()) {
                error = fmt::format("provider manifest {}: [[provider]] requires `language`", path.string());
                return false;
            }
            provider.language = *language;

            if (const auto executable = (*spec)["executable"].value<std::string>()) provider.executable = *executable;
            if (const auto protocol = (*spec)["protocol"].value<std::string>()) provider.protocol = *protocol;
            if (const auto contract = (*spec)["contract"].value<std::string>()) provider.contract = *contract;

            if (!read_string_array(*spec, "roles", provider.roles, error)
                || !read_string_array(*spec, "features", provider.features, error)) {
                error = fmt::format("provider manifest {}: {}", path.string(), error);
                return false;
            }
            if (provider.roles.empty()) {
                error = fmt::format(
                    "provider manifest {}: provider `{}` requires at least one role", path.string(), provider.language);
                return false;
            }
            for (size_t i = 0; i < provider.roles.size(); ++i) {
                const auto &role = provider.roles[i];
                if (role != "frontend" && role != "backend") {
                    error = fmt::format("provider manifest {}: provider `{}` has unknown role `{}`", path.string(),
                        provider.language, role);
                    return false;
                }
                for (size_t j = 0; j < i; ++j) {
                    if (provider.roles[j] == role) {
                        error = fmt::format("provider manifest {}: provider `{}` repeats role `{}`", path.string(),
                            provider.language, role);
                        return false;
                    }
                }
            }
            if (provider.protocol != "jsonl-v1" && provider.protocol != "direct") {
                error = fmt::format("provider manifest {}: provider `{}` has unknown protocol `{}`", path.string(),
                    provider.language, provider.protocol);
                return false;
            }
            if (provider.protocol.empty()) {
                error = fmt::format(
                    "provider manifest {}: provider `{}` requires `protocol`", path.string(), provider.language);
                return false;
            }
            if (provider.protocol == "jsonl-v1" && provider.executable.empty()) {
                error = fmt::format("provider manifest {}: provider `{}` speaks jsonl-v1 but has no `executable`",
                    path.string(), provider.language);
                return false;
            }
            if (find_provider_manifest(manifest, provider.language) != nullptr) {
                error = fmt::format("provider manifest {}: duplicate language `{}`", path.string(), provider.language);
                return false;
            }
            manifest.providers.push_back(std::move(provider));
        }
    } catch (const toml::parse_error &e) {
        error = fmt::format("provider manifest {}: {}", path.string(), e.description());
        return false;
    }

    return true;
}

const ProviderManifestEntry *find_provider_manifest(const ProviderManifest &manifest, const std::string &language) {
    for (const auto &provider : manifest.providers)
        if (provider.language == language) return &provider;
    return nullptr;
}

std::string describe_provider(const ProviderManifestEntry &provider) {
    std::string line = provider.language;
    for (const auto &role : provider.roles)
        line += " " + role;
    line += " protocol=" + provider.protocol;
    if (!provider.contract.empty()) line += " contract=" + provider.contract;
    return line;
}

fs::path provider_executable(const fs::path &base_provider, const std::string &language) {
    if (language.empty() || language == "cpp") return base_provider;
    ProviderManifest manifest;
    std::string manifest_error;
    if (load_provider_manifest(base_provider.parent_path(), manifest, manifest_error)) {
        if (const auto *entry = find_provider_manifest(manifest, language);
            entry != nullptr && !entry->executable.empty())
            return base_provider.parent_path() / entry->executable;
    }
    return base_provider.parent_path() / ("amc-" + language);
}

namespace {

// Minimal cross-platform child process running `provider --ipc`: a request pipe
// (parent -> child stdin) and a response pipe (child stdout -> parent).
struct ProviderProcess {
#if defined(_WIN32)
    HANDLE process = nullptr;
    HANDLE request_write = nullptr;
    HANDLE response_read = nullptr;
#else
    pid_t pid = -1;
    int request_write = -1;
    int response_read = -1;
#endif
    std::string read_buffer;
};

#if defined(_WIN32)
bool provider_spawn(const fs::path &provider, ProviderProcess &proc, std::string &error) {
    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;

    HANDLE request_read = nullptr;
    HANDLE response_write = nullptr;
    if (!CreatePipe(&request_read, &proc.request_write, &security, 0)
        || !CreatePipe(&proc.response_read, &response_write, &security, 0)) {
        error = "cannot create provider pipes";
        return false;
    }
    // Hand only the child's pipe ends to the child.
    SetHandleInformation(proc.request_write, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(proc.response_read, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = request_read;
    startup.hStdOutput = response_write;
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);

    std::string command = "\"" + provider.string() + "\" --ipc";
    PROCESS_INFORMATION info{};
    const BOOL created =
        CreateProcessA(nullptr, command.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &startup, &info);
    CloseHandle(request_read);
    CloseHandle(response_write);
    if (!created) {
        error = "cannot create provider process";
        return false;
    }
    CloseHandle(info.hThread);
    proc.process = info.hProcess;
    return true;
}

bool provider_write(const ProviderProcess &proc, const std::string &line) {
    DWORD written = 0;
    return WriteFile(proc.request_write, line.data(), static_cast<DWORD>(line.size()), &written, nullptr) != 0
        && written == line.size();
}

int provider_wait(ProviderProcess &proc) {
    if (proc.request_write != nullptr) CloseHandle(proc.request_write);
    if (proc.response_read != nullptr) CloseHandle(proc.response_read);
    DWORD code = 1;
    WaitForSingleObject(proc.process, INFINITE);
    GetExitCodeProcess(proc.process, &code);
    CloseHandle(proc.process);
    return static_cast<int>(code);
}
#else
bool provider_spawn(const fs::path &provider, ProviderProcess &proc, std::string &error) {
    int request_pipe[2] = {};
    int response_pipe[2] = {};
    if (pipe(request_pipe) != 0 || pipe(response_pipe) != 0) {
        error = "cannot create provider pipes";
        return false;
    }
    const pid_t child = fork();
    if (child < 0) {
        error = "cannot fork provider process";
        return false;
    }
    if (child == 0) {
        dup2(request_pipe[0], STDIN_FILENO);
        dup2(response_pipe[1], STDOUT_FILENO);
        close(request_pipe[0]);
        close(request_pipe[1]);
        close(response_pipe[0]);
        close(response_pipe[1]);
        execl(provider.c_str(), provider.c_str(), "--ipc", static_cast<char *>(nullptr));
        _exit(127);
    }
    close(request_pipe[0]);
    close(response_pipe[1]);
    proc.pid = child;
    proc.request_write = request_pipe[1];
    proc.response_read = response_pipe[0];
    return true;
}

bool provider_write(const ProviderProcess &proc, const std::string &line) {
    return write(proc.request_write, line.data(), line.size()) == static_cast<ssize_t>(line.size());
}

int provider_wait(ProviderProcess &proc) {
    if (proc.request_write >= 0) close(proc.request_write);
    if (proc.response_read >= 0) close(proc.response_read);
    int status = 1;
    waitpid(proc.pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}
#endif

int provider_timeout_ms() {
    const char *value = std::getenv("AMC_PROVIDER_TIMEOUT_MS");
    if (value == nullptr || *value == '\0') return 0;
    char *end = nullptr;
    const long parsed = std::strtol(value, &end, 10);
    if (end == value || parsed <= 0) return 0;
    return parsed > 24L * 60L * 60L * 1'000L ? 24 * 60 * 60 * 1'000 : static_cast<int>(parsed);
}

void provider_terminate(ProviderProcess &proc) {
#if defined(_WIN32)
    if (proc.process != nullptr) TerminateProcess(proc.process, 124);
#else
    if (proc.pid > 0) kill(proc.pid, SIGKILL);
#endif
}

bool provider_send(const ProviderProcess &proc, const amc::Json &message) {
    return provider_write(proc, message.dump() + "\n");
}

bool provider_read_some(
    ProviderProcess &proc, char *buffer, size_t size, int timeout_ms, long &count, std::string &error) {
#if defined(_WIN32)
    const auto deadline = timeout_ms > 0 ? std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms)
                                         : std::chrono::steady_clock::time_point::max();
    for (;;) {
        DWORD available = 0;
        if (PeekNamedPipe(proc.response_read, nullptr, 0, nullptr, &available, nullptr) == 0) {
            error = "provider pipe closed";
            return false;
        }
        if (available > 0) {
            DWORD read_count = 0;
            if (ReadFile(proc.response_read, buffer, static_cast<DWORD>(size), &read_count, nullptr) == 0) {
                error = "provider read failed";
                return false;
            }
            count = static_cast<long>(read_count);
            return true;
        }
        if (WaitForSingleObject(proc.process, 0) == WAIT_OBJECT_0) {
            count = 0;
            return true;
        }
        if (timeout_ms > 0 && std::chrono::steady_clock::now() >= deadline) {
            error = fmt::format("provider timeout after {} ms", timeout_ms);
            return false;
        }
        Sleep(5);
    }
#else
    struct pollfd descriptor{proc.response_read, POLLIN, 0};
    const int ready = poll(&descriptor, 1, timeout_ms > 0 ? timeout_ms : -1);
    if (ready == 0) {
        error = fmt::format("provider timeout after {} ms", timeout_ms);
        return false;
    }
    if (ready < 0) {
        error = "provider read failed";
        return false;
    }
    count = static_cast<long>(read(proc.response_read, buffer, size));
    return true;
#endif
}

bool provider_read_line(ProviderProcess &proc, std::string &line, std::string &error, int timeout_ms) {
    const auto deadline = timeout_ms > 0 ? std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms)
                                         : std::chrono::steady_clock::time_point::max();
    for (;;) {
        const auto newline = proc.read_buffer.find('\n');
        if (newline != std::string::npos) {
            line.assign(proc.read_buffer, 0, newline);
            proc.read_buffer.erase(0, newline + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            return true;
        }
        int remaining = 0;
        if (timeout_ms > 0) {
            const auto left =
                std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
            remaining = left.count() > 0 ? static_cast<int>(left.count()) : 0;
            if (remaining == 0) {
                error = fmt::format("provider timeout after {} ms", timeout_ms);
                return false;
            }
        }
        char buffer[4'096] = {};
        long count = 0;
        if (!provider_read_some(proc, buffer, sizeof(buffer), remaining, count, error)) return false;
        if (count <= 0) {
            error = "provider closed the response stream";
            return false;
        }
        proc.read_buffer.append(buffer, static_cast<size_t>(count));
    }
}

std::string provider_error_text(const amc::Json &message) {
    std::string text = "provider reported an error";
    const auto *error = message.find("error");
    if (error == nullptr) return text;
    const auto *code = error->find("code");
    const auto *detail = error->find("message");
    text.clear();
    if (code != nullptr) text += code->as_string();
    if (detail != nullptr) {
        if (!text.empty()) text += ": ";
        text += detail->as_string();
    }
    if (const auto *diagnostics = message.find("diagnostics"); diagnostics != nullptr && diagnostics->is_array()) {
        for (const auto &entry : diagnostics->items()) {
            const auto *diagnostic = entry.find("message");
            if (diagnostic == nullptr || diagnostic->as_string().empty()) continue;
            text += ": " + diagnostic->as_string();
            break;
        }
    }
    return text.empty() ? "provider reported an error" : text;
}

bool provider_wait_response(ProviderProcess &proc, long long id, const std::string &expected, amc::Json &response,
    std::string &error, int timeout_ms) {
    for (;;) {
        std::string line;
        if (!provider_read_line(proc, line, error, timeout_ms)) {
            if (error.rfind("provider timeout", 0) == 0) {
                amc::Json cancel = amc::Json::object();
                cancel.set("schema", "abix.plugin/1");
                cancel.set("id", amc::Json(id));
                cancel.set("type", "CANCEL");
                cancel.set("target", amc::Json(id));
                (void)provider_send(proc, cancel);
                provider_terminate(proc);
            }
            return false;
        }
        if (line.empty()) continue;

        std::string parse_error;
        if (!amc::parse_json(line, response, parse_error)) {
            error = "invalid provider response: " + parse_error;
            return false;
        }
        if (const auto *schema = response.find("schema"); schema != nullptr && schema->as_string() != "abix.plugin/1") {
            error = "provider response has unsupported schema: " + schema->as_string();
            return false;
        }
        const auto *id_node = response.find("id");
        const long long response_id = id_node != nullptr ? static_cast<long long>(id_node->as_number(-1)) : -1;
        if (response_id != id) {
            error = fmt::format("provider response id mismatch (expected {}, got {})", id, response_id);
            return false;
        }
        const auto *type_node = response.find("type");
        const std::string type = type_node != nullptr ? type_node->as_string() : std::string{};
        if (type == "EVENT") continue;
        if (type == "ERROR") {
            error = provider_error_text(response);
            return false;
        }
        if (type != expected) {
            error = fmt::format("provider response type mismatch (expected {}, got {})", expected, type);
            return false;
        }
        return true;
    }
}

bool provider_protocol_compatible(const amc::Json &message) {
    const auto *protocol = message.find("protocol");
    if (protocol == nullptr) return false;
    if (protocol->is_object()) {
        const auto *major = protocol->find("major");
        return major != nullptr && static_cast<int>(major->as_number(0)) == 1;
    }
    return static_cast<int>(protocol->as_number(0)) == 1;
}

}   // namespace

bool dispatch_provider(const fs::path &provider, const char *capability, const fs::path &input, const fs::path &output,
    std::string &error) {
    ProviderProcess child;
    if (!provider_spawn(provider, child, error)) return false;
    const int timeout_ms = provider_timeout_ms();

    const auto fail_and_wait = [&](std::string message) {
        error = std::move(message);
        provider_wait(child);
        return false;
    };

    amc::Json hello = amc::Json::object();
    hello.set("schema", "abix.plugin/1");
    hello.set("id", amc::Json(1));
    hello.set("type", "HELLO");
    amc::Json protocol = amc::Json::object();
    protocol.set("major", amc::Json(1));
    protocol.set("minor", amc::Json(0));
    hello.set("protocol", std::move(protocol));
    if (!provider_send(child, hello)) return fail_and_wait("cannot send provider HELLO");

    amc::Json ready;
    if (!provider_wait_response(child, 1, "READY", ready, error, timeout_ms)) return fail_and_wait(error);
    if (!provider_protocol_compatible(ready)) return fail_and_wait("provider READY reported an incompatible protocol");

    amc::Json query = amc::Json::object();
    query.set("schema", "abix.plugin/1");
    query.set("id", amc::Json(2));
    query.set("type", "QUERY_CAPABILITIES");
    if (!provider_send(child, query)) return fail_and_wait("cannot send QUERY_CAPABILITIES");

    amc::Json capabilities;
    if (!provider_wait_response(child, 2, "CAPABILITIES", capabilities, error, timeout_ms)) return fail_and_wait(error);

    bool advertised = false;
    if (const auto *entries = capabilities.find("capabilities"); entries != nullptr && entries->is_array()) {
        for (const auto &entry : entries->items()) {
            if (entry.as_string() == capability) {
                advertised = true;
                break;
            }
        }
    }
    if (!advertised) return fail_and_wait(fmt::format("provider does not advertise capability `{}`", capability));

    const bool project = std::string(capability) == "backend";
    amc::Json work = amc::Json::object();
    work.set("schema", "abix.plugin/1");
    work.set("id", amc::Json(3));
    work.set("type", project ? "PROJECT" : "ANALYZE");
    if (!project) work.set("capability", capability);
    work.set("input", input.string());
    work.set("output", output.string());
    if (!provider_send(child, work)) return fail_and_wait(project ? "cannot send PROJECT" : "cannot send ANALYZE");

    amc::Json result;
    if (!provider_wait_response(child, 3, "RESULT", result, error, timeout_ms)) return fail_and_wait(error);
    const auto *status = result.find("status");
    if (status == nullptr || status->as_string() != "ok")
        return fail_and_wait("provider RESULT did not report status=ok");

    amc::Json done = amc::Json::object();
    done.set("schema", "abix.plugin/1");
    done.set("id", amc::Json(4));
    done.set("type", "DONE");
    if (!provider_send(child, done)) return fail_and_wait("cannot send DONE");

    amc::Json done_response;
    if (!provider_wait_response(child, 4, "DONE", done_response, error, timeout_ms)) return fail_and_wait(error);

    const int exit_code = provider_wait(child);
    if (exit_code != 0) {
        error = fmt::format("provider exited with status {}", exit_code);
        return false;
    }
    return true;
}

int generate(
    const fs::path &provider, const fs::path &input, const fs::path &destination, const std::string &extension) {
    fs::path output = destination;
    if (destination.extension() != extension) output /= ("amc_generated" + extension);
    std::error_code ec;
    if (!output.parent_path().empty()) {
        fs::create_directories(output.parent_path(), ec);
        if (ec)
            return fail(
                amc::ErrorCategory::io, "generate", "cannot create output directory", output.string(), ec.message());
    }
    std::string error;
    if (!dispatch_provider(provider, "backend", input, output, error))
        return fail(amc::ErrorCategory::provider, "backend", error, input.string());
    return 0;
}

}   // namespace amc::compilation
