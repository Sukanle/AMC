#pragma once

#include "SKL/AMC/Compilation/Cache.h"
#include "SKL/AMC/Support/EnumReflect.h"

SKL_RFS_ENUM(skl::amc::CacheStatus)
SKL_RFS_ENUM_VALUE(skl::amc::CacheStatus::HIT, "hit")
SKL_RFS_ENUM_VALUE(skl::amc::CacheStatus::MISS, "miss")
SKL_RFS_ENUM_VALUE(skl::amc::CacheStatus::CORRUPTED, "corrupted")
SKL_RFS_ENUM_VALUE(skl::amc::CacheStatus::INVALID_MANIFEST, "invalid_manifest")
SKL_RFS_ENUM_VALUE(skl::amc::CacheStatus::SCHEMA_MISMATCH, "schema_mismatch")
SKL_RFS_ENUM()
