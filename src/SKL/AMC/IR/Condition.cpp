// SPDX-License-Identifier: Apache-2.0
#include "SKL/AMC/IR/Condition.h"

#include <algorithm>
#include <array>

namespace skl::amc {
namespace {

struct ConditionEntry {
    std::string_view name;
    GenericCondition condition;
};

constexpr std::array<ConditionEntry, 80> kConditions{
    {
     {"unknown", GenericCondition::UNKNOWN},
     {"none", GenericCondition::NONE},
     {"address_family_not_supported", GenericCondition::ADDRESS_FAMILY_NOT_SUPPORTED},
     {"address_in_use", GenericCondition::ADDRESS_IN_USE},
     {"address_not_available", GenericCondition::ADDRESS_NOT_AVAILABLE},
     {"already_connected", GenericCondition::ALREADY_CONNECTED},
     {"argument_list_too_long", GenericCondition::ARGUMENT_LIST_TOO_LONG},
     {"argument_out_of_domain", GenericCondition::ARGUMENT_OUT_OF_DOMAIN},
     {"bad_address", GenericCondition::BAD_ADDRESS},
     {"bad_file_descriptor", GenericCondition::BAD_FILE_DESCRIPTOR},
     {"bad_message", GenericCondition::BAD_MESSAGE},
     {"broken_pipe", GenericCondition::BROKEN_PIPE},
     {"connection_aborted", GenericCondition::CONNECTION_ABORTED},
     {"connection_already_in_progress", GenericCondition::CONNECTION_ALREADY_IN_PROGRESS},
     {"connection_refused", GenericCondition::CONNECTION_REFUSED},
     {"connection_reset", GenericCondition::CONNECTION_RESET},
     {"cross_device_link", GenericCondition::CROSS_DEVICE_LINK},
     {"destination_address_required", GenericCondition::DESTINATION_ADDRESS_REQUIRED},
     {"device_or_resource_busy", GenericCondition::DEVICE_OR_RESOURCE_BUSY},
     {"directory_not_empty", GenericCondition::DIRECTORY_NOT_EMPTY},
     {"executable_format_error", GenericCondition::EXECUTABLE_FORMAT_ERROR},
     {"file_exists", GenericCondition::FILE_EXISTS},
     {"file_too_large", GenericCondition::FILE_TOO_LARGE},
     {"filename_too_long", GenericCondition::FILENAME_TOO_LONG},
     {"function_not_supported", GenericCondition::FUNCTION_NOT_SUPPORTED},
     {"host_unreachable", GenericCondition::HOST_UNREACHABLE},
     {"identifier_removed", GenericCondition::IDENTIFIER_REMOVED},
     {"illegal_byte_sequence", GenericCondition::ILLEGAL_BYTE_SEQUENCE},
     {"inappropriate_io_control_operation", GenericCondition::INAPPROPRIATE_IO_CONTROL_OPERATION},
     {"interrupted", GenericCondition::INTERRUPTED},
     {"invalid_argument", GenericCondition::INVALID_ARGUMENT},
     {"invalid_seek", GenericCondition::INVALID_SEEK},
     {"io_error", GenericCondition::IO_ERROR},
     {"is_a_directory", GenericCondition::IS_A_DIRECTORY},
     {"message_size", GenericCondition::MESSAGE_SIZE},
     {"network_down", GenericCondition::NETWORK_DOWN},
     {"network_reset", GenericCondition::NETWORK_RESET},
     {"network_unreachable", GenericCondition::NETWORK_UNREACHABLE},
     {"no_buffer_space", GenericCondition::NO_BUFFER_SPACE},
     {"no_child_process", GenericCondition::NO_CHILD_PROCESS},
     {"no_link", GenericCondition::NO_LINK},
     {"no_lock_available", GenericCondition::NO_LOCK_AVAILABLE},
     {"no_message_available", GenericCondition::NO_MESSAGE_AVAILABLE},
     {"no_message", GenericCondition::NO_MESSAGE},
     {"no_protocol_option", GenericCondition::NO_PROTOCOL_OPTION},
     {"no_space_on_device", GenericCondition::NO_SPACE_ON_DEVICE},
     {"no_stream_resources", GenericCondition::NO_STREAM_RESOURCES},
     {"no_such_device_or_address", GenericCondition::NO_SUCH_DEVICE_OR_ADDRESS},
     {"no_such_device", GenericCondition::NO_SUCH_DEVICE},
     {"no_such_file_or_directory", GenericCondition::NO_SUCH_FILE_OR_DIRECTORY},
     {"no_such_process", GenericCondition::NO_SUCH_PROCESS},
     {"not_a_directory", GenericCondition::NOT_A_DIRECTORY},
     {"not_a_socket", GenericCondition::NOT_A_SOCKET},
     {"not_a_stream", GenericCondition::NOT_A_STREAM},
     {"not_connected", GenericCondition::NOT_CONNECTED},
     {"not_enough_memory", GenericCondition::NOT_ENOUGH_MEMORY},
     {"not_supported", GenericCondition::NOT_SUPPORTED},
     {"operation_canceled", GenericCondition::OPERATION_CANCELED},
     {"operation_in_progress", GenericCondition::OPERATION_IN_PROGRESS},
     {"operation_not_permitted", GenericCondition::OPERATION_NOT_PERMITTED},
     {"operation_not_supported", GenericCondition::OPERATION_NOT_SUPPORTED},
     {"operation_would_block", GenericCondition::OPERATION_WOULD_BLOCK},
     {"owner_dead", GenericCondition::OWNER_DEAD},
     {"permission_denied", GenericCondition::PERMISSION_DENIED},
     {"protocol_error", GenericCondition::PROTOCOL_ERROR},
     {"protocol_not_supported", GenericCondition::PROTOCOL_NOT_SUPPORTED},
     {"read_only_file_system", GenericCondition::READ_ONLY_FILE_SYSTEM},
     {"resource_deadlock_would_occur", GenericCondition::RESOURCE_DEADLOCK_WOULD_OCCUR},
     {"resource_unavailable_try_again", GenericCondition::RESOURCE_UNAVAILABLE_TRY_AGAIN},
     {"result_out_of_range", GenericCondition::RESULT_OUT_OF_RANGE},
     {"state_not_recoverable", GenericCondition::STATE_NOT_RECOVERABLE},
     {"stream_timeout", GenericCondition::STREAM_TIMEOUT},
     {"text_file_busy", GenericCondition::TEXT_FILE_BUSY},
     {"timed_out", GenericCondition::TIMED_OUT},
     {"too_many_files_open_in_system", GenericCondition::TOO_MANY_FILES_OPEN_IN_SYSTEM},
     {"too_many_files_open", GenericCondition::TOO_MANY_FILES_OPEN},
     {"too_many_links", GenericCondition::TOO_MANY_LINKS},
     {"too_many_symbolic_link_levels", GenericCondition::TOO_MANY_SYMBOLIC_LINK_LEVELS},
     {"value_too_large", GenericCondition::VALUE_TOO_LARGE},
     {"wrong_protocol_type", GenericCondition::WRONG_PROTOCOL_TYPE},
     }
};

std::string_view strip_condition_prefix(std::string_view text) noexcept {
    for (const std::string_view prefix : {"std::errc::", "errc::", "GenericCondition::", "condition::"}) {
        if (text.size() > prefix.size() && text.substr(0, prefix.size()) == prefix) return text.substr(prefix.size());
    }
    return text;
}

}   // namespace

std::optional<GenericCondition> parse_generic_condition(std::string_view text) noexcept {
    const std::string_view name = strip_condition_prefix(text);
    const auto found = std::find_if(
        kConditions.begin(), kConditions.end(), [name](const ConditionEntry &entry) { return entry.name == name; });
    if (found == kConditions.end()) return std::nullopt;
    return found->condition;
}

const char *generic_condition_name(GenericCondition condition) noexcept {
    const auto found = std::find_if(kConditions.begin(), kConditions.end(),
        [condition](const ConditionEntry &entry) { return entry.condition == condition; });
    return found == kConditions.end() ? "unknown" : found->name.data();
}

}   // namespace skl::amc
