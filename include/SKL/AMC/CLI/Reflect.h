#pragma once

#include "SKL/AMC/CLI/CLI.h"
#include "SKL/AMC/Support/EnumReflect.h"

// Command spellings are the single source of truth for both LLVM subcommand
// dispatch and `--help`. Keep `Command::NONE` unregistered so it is only used
// as the lookup fallback.
SKL_RFS_ENUM(skl::amc::support::Command)
SKL_RFS_ENUM_VALUE(skl::amc::support::Command::BUILD, "build")
SKL_RFS_ENUM_VALUE(skl::amc::support::Command::CONTEXT, "context")
SKL_RFS_ENUM_VALUE(skl::amc::support::Command::METADATA, "metadata")
SKL_RFS_ENUM_VALUE(skl::amc::support::Command::ADAPTER, "adapter")
SKL_RFS_ENUM_VALUE(skl::amc::support::Command::QUERY, "query")
SKL_RFS_ENUM_VALUE(skl::amc::support::Command::INSPECT, "inspect")
SKL_RFS_ENUM_VALUE(skl::amc::support::Command::VALIDATE, "validate")
SKL_RFS_ENUM_VALUE(skl::amc::support::Command::PUBLISH, "publish")
SKL_RFS_ENUM_VALUE(skl::amc::support::Command::FETCH, "fetch")
SKL_RFS_ENUM_VALUE(skl::amc::support::Command::VERIFY, "verify")
SKL_RFS_ENUM_VALUE(skl::amc::support::Command::TARGET, "target")
SKL_RFS_ENUM_VALUE(skl::amc::support::Command::DIFF, "diff")
SKL_RFS_ENUM_VALUE(skl::amc::support::Command::COMPATIBILITY, "compatibility")
SKL_RFS_ENUM_VALUE(skl::amc::support::Command::CACHE, "cache")
SKL_RFS_ENUM_VALUE(skl::amc::support::Command::GENERATE, "generate")
SKL_RFS_ENUM_VALUE(skl::amc::support::Command::BACKEND, "backend")
SKL_RFS_ENUM_VALUE(skl::amc::support::Command::FRONTEND, "frontend")
SKL_RFS_ENUM()
