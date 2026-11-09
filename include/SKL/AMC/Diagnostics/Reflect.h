#pragma once
#include "SKL/AMC/Diagnostics/Diagnostics.h"

#include "SKL/AMC/Diagnostics/Error.h"
#include "SKL/AMC/Support/EnumReflect.h"

SKL_RFS_ENUM(skl::amc::ErrorCategory)
SKL_RFS_ENUM_VALUE(skl::amc::ErrorCategory::USAGE, "usage")
SKL_RFS_ENUM_VALUE(skl::amc::ErrorCategory::CONFIG, "config")
SKL_RFS_ENUM_VALUE(skl::amc::ErrorCategory::IO, "io")
SKL_RFS_ENUM_VALUE(skl::amc::ErrorCategory::PROVIDER, "provider")
SKL_RFS_ENUM_VALUE(skl::amc::ErrorCategory::FORMAT, "format")
SKL_RFS_ENUM_VALUE(skl::amc::ErrorCategory::VALIDATION, "validation")
SKL_RFS_ENUM_VALUE(skl::amc::ErrorCategory::COMPATIBILITY, "compatibility")
SKL_RFS_ENUM_VALUE(skl::amc::ErrorCategory::INTERNAL, "internal")
SKL_RFS_ENUM()

SKL_RFS_ENUM(skl::amc::diagnostics::Severity)
SKL_RFS_ENUM_VALUE(skl::amc::diagnostics::Severity::NOTE, "note")
SKL_RFS_ENUM_VALUE(skl::amc::diagnostics::Severity::WARNING, "warning")
SKL_RFS_ENUM_VALUE(skl::amc::diagnostics::Severity::ERROR, "error")
SKL_RFS_ENUM()

SKL_RFS_ENUM(skl::amc::ErrorFormat)
SKL_RFS_ENUM_VALUE(skl::amc::ErrorFormat::TEXT, "text")
SKL_RFS_ENUM_VALUE(skl::amc::ErrorFormat::JSON, "json")
SKL_RFS_ENUM()
