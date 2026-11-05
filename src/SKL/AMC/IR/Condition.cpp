// SPDX-License-Identifier: Apache-2.0
#include "SKL/AMC/IR/Condition.h"

#include <algorithm>
#include <array>

namespace amc {
namespace {

struct ConditionEntry {
    std::string_view name;
    GenericCondition condition;
};

constexpr std::array<ConditionEntry, 80> kConditions{
    {
     {"unknown", GenericCondition::unknown},
     {"none", GenericCondition::none},
     {"address_family_not_supported", GenericCondition::address_family_not_supported},
     {"address_in_use", GenericCondition::address_in_use},
     {"address_not_available", GenericCondition::address_not_available},
     {"already_connected", GenericCondition::already_connected},
     {"argument_list_too_long", GenericCondition::argument_list_too_long},
     {"argument_out_of_domain", GenericCondition::argument_out_of_domain},
     {"bad_address", GenericCondition::bad_address},
     {"bad_file_descriptor", GenericCondition::bad_file_descriptor},
     {"bad_message", GenericCondition::bad_message},
     {"broken_pipe", GenericCondition::broken_pipe},
     {"connection_aborted", GenericCondition::connection_aborted},
     {"connection_already_in_progress", GenericCondition::connection_already_in_progress},
     {"connection_refused", GenericCondition::connection_refused},
     {"connection_reset", GenericCondition::connection_reset},
     {"cross_device_link", GenericCondition::cross_device_link},
     {"destination_address_required", GenericCondition::destination_address_required},
     {"device_or_resource_busy", GenericCondition::device_or_resource_busy},
     {"directory_not_empty", GenericCondition::directory_not_empty},
     {"executable_format_error", GenericCondition::executable_format_error},
     {"file_exists", GenericCondition::file_exists},
     {"file_too_large", GenericCondition::file_too_large},
     {"filename_too_long", GenericCondition::filename_too_long},
     {"function_not_supported", GenericCondition::function_not_supported},
     {"host_unreachable", GenericCondition::host_unreachable},
     {"identifier_removed", GenericCondition::identifier_removed},
     {"illegal_byte_sequence", GenericCondition::illegal_byte_sequence},
     {"inappropriate_io_control_operation", GenericCondition::inappropriate_io_control_operation},
     {"interrupted", GenericCondition::interrupted},
     {"invalid_argument", GenericCondition::invalid_argument},
     {"invalid_seek", GenericCondition::invalid_seek},
     {"io_error", GenericCondition::io_error},
     {"is_a_directory", GenericCondition::is_a_directory},
     {"message_size", GenericCondition::message_size},
     {"network_down", GenericCondition::network_down},
     {"network_reset", GenericCondition::network_reset},
     {"network_unreachable", GenericCondition::network_unreachable},
     {"no_buffer_space", GenericCondition::no_buffer_space},
     {"no_child_process", GenericCondition::no_child_process},
     {"no_link", GenericCondition::no_link},
     {"no_lock_available", GenericCondition::no_lock_available},
     {"no_message_available", GenericCondition::no_message_available},
     {"no_message", GenericCondition::no_message},
     {"no_protocol_option", GenericCondition::no_protocol_option},
     {"no_space_on_device", GenericCondition::no_space_on_device},
     {"no_stream_resources", GenericCondition::no_stream_resources},
     {"no_such_device_or_address", GenericCondition::no_such_device_or_address},
     {"no_such_device", GenericCondition::no_such_device},
     {"no_such_file_or_directory", GenericCondition::no_such_file_or_directory},
     {"no_such_process", GenericCondition::no_such_process},
     {"not_a_directory", GenericCondition::not_a_directory},
     {"not_a_socket", GenericCondition::not_a_socket},
     {"not_a_stream", GenericCondition::not_a_stream},
     {"not_connected", GenericCondition::not_connected},
     {"not_enough_memory", GenericCondition::not_enough_memory},
     {"not_supported", GenericCondition::not_supported},
     {"operation_canceled", GenericCondition::operation_canceled},
     {"operation_in_progress", GenericCondition::operation_in_progress},
     {"operation_not_permitted", GenericCondition::operation_not_permitted},
     {"operation_not_supported", GenericCondition::operation_not_supported},
     {"operation_would_block", GenericCondition::operation_would_block},
     {"owner_dead", GenericCondition::owner_dead},
     {"permission_denied", GenericCondition::permission_denied},
     {"protocol_error", GenericCondition::protocol_error},
     {"protocol_not_supported", GenericCondition::protocol_not_supported},
     {"read_only_file_system", GenericCondition::read_only_file_system},
     {"resource_deadlock_would_occur", GenericCondition::resource_deadlock_would_occur},
     {"resource_unavailable_try_again", GenericCondition::resource_unavailable_try_again},
     {"result_out_of_range", GenericCondition::result_out_of_range},
     {"state_not_recoverable", GenericCondition::state_not_recoverable},
     {"stream_timeout", GenericCondition::stream_timeout},
     {"text_file_busy", GenericCondition::text_file_busy},
     {"timed_out", GenericCondition::timed_out},
     {"too_many_files_open_in_system", GenericCondition::too_many_files_open_in_system},
     {"too_many_files_open", GenericCondition::too_many_files_open},
     {"too_many_links", GenericCondition::too_many_links},
     {"too_many_symbolic_link_levels", GenericCondition::too_many_symbolic_link_levels},
     {"value_too_large", GenericCondition::value_too_large},
     {"wrong_protocol_type", GenericCondition::wrong_protocol_type},
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

}   // namespace amc
