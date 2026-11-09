#pragma once

#include "SKL/AMC/CLI/CLI.h"
#include "SKL/AMC/Support/EnumReflect.h"

// Command spellings are the single source of truth for both LLVM subcommand
// dispatch and `--help`. Keep `Command::NONE` unregistered so it is only used
// as the lookup fallback.
SKL_RFS_ENUM(skl::amc::cli::Command)
SKL_RFS_ENUM_VALUE(skl::amc::cli::Command::BUILD, "build")
SKL_RFS_ENUM_VALUE(skl::amc::cli::Command::CONTEXT, "context")
SKL_RFS_ENUM_VALUE(skl::amc::cli::Command::METADATA, "metadata")
SKL_RFS_ENUM_VALUE(skl::amc::cli::Command::ADAPTER, "adapter")
SKL_RFS_ENUM_VALUE(skl::amc::cli::Command::QUERY, "query")
SKL_RFS_ENUM_VALUE(skl::amc::cli::Command::INSPECT, "inspect")
SKL_RFS_ENUM_VALUE(skl::amc::cli::Command::VALIDATE, "validate")
SKL_RFS_ENUM_VALUE(skl::amc::cli::Command::PUBLISH, "publish")
SKL_RFS_ENUM_VALUE(skl::amc::cli::Command::FETCH, "fetch")
SKL_RFS_ENUM_VALUE(skl::amc::cli::Command::VERIFY, "verify")
SKL_RFS_ENUM_VALUE(skl::amc::cli::Command::TARGET, "target")
SKL_RFS_ENUM_VALUE(skl::amc::cli::Command::DIFF, "diff")
SKL_RFS_ENUM_VALUE(skl::amc::cli::Command::COMPATIBILITY, "compatibility")
SKL_RFS_ENUM_VALUE(skl::amc::cli::Command::CACHE, "cache")
SKL_RFS_ENUM_VALUE(skl::amc::cli::Command::GENERATE, "generate")
SKL_RFS_ENUM_VALUE(skl::amc::cli::Command::BACKEND, "backend")
SKL_RFS_ENUM_VALUE(skl::amc::cli::Command::FRONTEND, "frontend")
SKL_RFS_ENUM()
