#pragma once

#include "SKL/AMC/IR/Lowering.h"
#include "SKL/AMC/IR/Verify.h"
#include "SKL/AMC/Support/EnumReflect.h"

SKL_RFS_ENUM(skl::amc::ir::LoweringStatus)
SKL_RFS_ENUM_VALUE(skl::amc::ir::LoweringStatus::LOWERED, "lowered")
SKL_RFS_ENUM_VALUE(skl::amc::ir::LoweringStatus::OPAQUE, "opaque")
SKL_RFS_ENUM_VALUE(skl::amc::ir::LoweringStatus::REJECTED, "rejected")
SKL_RFS_ENUM()

SKL_RFS_ENUM(skl::amc::ir::LoweringReason)
SKL_RFS_ENUM_VALUE(skl::amc::ir::LoweringReason::OK, "ok")
SKL_RFS_ENUM_VALUE(skl::amc::ir::LoweringReason::MISSING_ABI_TYPE, "missing_abi_type")
SKL_RFS_ENUM_VALUE(skl::amc::ir::LoweringReason::MISSING_ABI_FUNCTION, "missing_abi_function")
SKL_RFS_ENUM_VALUE(skl::amc::ir::LoweringReason::AMBIGUOUS_ABI_FUNCTION, "ambiguous_abi_function")
SKL_RFS_ENUM_VALUE(skl::amc::ir::LoweringReason::MISSING_SEMANTIC_RECORD, "missing_semantic_record")
SKL_RFS_ENUM_VALUE(skl::amc::ir::LoweringReason::LAYOUT_UNVERIFIED, "layout_unverified")
SKL_RFS_ENUM_VALUE(skl::amc::ir::LoweringReason::OWNERSHIP_UNRESOLVED, "ownership_unresolved")
SKL_RFS_ENUM_VALUE(skl::amc::ir::LoweringReason::TEMPLATE_NOT_MATERIALIZED, "template_not_materialized")
SKL_RFS_ENUM()

SKL_RFS_ENUM(skl::amc::ChangeKind)
SKL_RFS_ENUM_VALUE(skl::amc::ChangeKind::TYPE_ADDED, "type_added")
SKL_RFS_ENUM_VALUE(skl::amc::ChangeKind::TYPE_REMOVED, "type_removed")
SKL_RFS_ENUM_VALUE(skl::amc::ChangeKind::LAYOUT_CHANGED, "layout_changed")
SKL_RFS_ENUM_VALUE(skl::amc::ChangeKind::FIELD_ADDED, "field_added")
SKL_RFS_ENUM_VALUE(skl::amc::ChangeKind::FIELD_REMOVED, "field_removed")
SKL_RFS_ENUM_VALUE(skl::amc::ChangeKind::FIELD_OFFSET_CHANGED, "field_offset_changed")
SKL_RFS_ENUM_VALUE(skl::amc::ChangeKind::FIELD_TYPE_CHANGED, "field_type_changed")
SKL_RFS_ENUM_VALUE(skl::amc::ChangeKind::FUNCTION_ADDED, "function_added")
SKL_RFS_ENUM_VALUE(skl::amc::ChangeKind::FUNCTION_REMOVED, "function_removed")
SKL_RFS_ENUM_VALUE(skl::amc::ChangeKind::FUNCTION_SIGNATURE_CHANGED, "function_signature_changed")
SKL_RFS_ENUM_VALUE(skl::amc::ChangeKind::FUNCTION_PARAMETER_CHANGED, "function_parameter_changed")
SKL_RFS_ENUM_VALUE(skl::amc::ChangeKind::CALLING_CONVENTION_CHANGED, "calling_convention_changed")
SKL_RFS_ENUM_VALUE(skl::amc::ChangeKind::OWNERSHIP_CHANGED, "ownership_changed")
SKL_RFS_ENUM_VALUE(skl::amc::ChangeKind::ERROR_BOUNDARY_CHANGED, "error_boundary_changed")
SKL_RFS_ENUM_VALUE(skl::amc::ChangeKind::INTERFACE_CHANGED, "interface_changed")
SKL_RFS_ENUM()
