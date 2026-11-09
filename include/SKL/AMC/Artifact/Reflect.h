#pragma once

#include "SKL/AMC/Artifact/Metadata.h"
#include "SKL/AMC/Support/EnumReflect.h"

SKL_RFS_ENUM(skl::amc::TargetVerificationStatus)
SKL_RFS_ENUM_VALUE(skl::amc::TargetVerificationStatus::COMPATIBLE, "compatible")
SKL_RFS_ENUM_VALUE(skl::amc::TargetVerificationStatus::INCOMPATIBLE, "incompatible")
SKL_RFS_ENUM_VALUE(skl::amc::TargetVerificationStatus::UNVERIFIABLE, "unverifiable")
SKL_RFS_ENUM()
