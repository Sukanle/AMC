#pragma once

#include <array>
#include <cstddef>
#include <filesystem>

namespace skl::amc::support {

// Top-level commands and aliases. The command-to-spelling table lives in
// `SKL/AMC/CLI/Reflect.h`; every command implementation registers its handler
// next to the implementation, so the dispatcher has no switch or name chain.
enum class Command : unsigned {
    NONE,
    BUILD,
    CONTEXT,
    METADATA,
    ADAPTER,
    QUERY,
    INSPECT,
    VALIDATE,
    PUBLISH,
    FETCH,
    VERIFY,
    TARGET,
    DIFF,
    COMPATIBILITY,
    CACHE,
    GENERATE,
    BACKEND,
    FRONTEND,
    COUNT,
};

using CommandHandler = int (*)(const std::filesystem::path &executable_dir);

inline std::array<CommandHandler, static_cast<size_t>(Command::COUNT)> &command_handlers() {
    static std::array<CommandHandler, static_cast<size_t>(Command::COUNT)> handlers{};
    return handlers;
}

inline bool register_command_handler(Command command, CommandHandler handler) {
    if (command == Command::NONE || command == Command::COUNT) return false;
    command_handlers()[static_cast<size_t>(command)] = handler;
    return true;
}

inline CommandHandler command_handler(Command command) {
    if (command == Command::NONE || command == Command::COUNT) return nullptr;
    return command_handlers()[static_cast<size_t>(command)];
}

}   // namespace skl::amc::support
