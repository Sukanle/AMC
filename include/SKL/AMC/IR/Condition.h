#pragma once

#include <stdint.h>

#include <optional>
#include <string_view>

namespace amc {

// Generic error condition for an error boundary. The numeric values mirror
// `skl::epcd::GenericCondition` and are frozen: they are part of the boundary
// protocol and must never be renumbered.
enum class GenericCondition : int32_t {
    unknown = -1,
    none = 0,
    address_family_not_supported = 1,
    address_in_use = 2,
    address_not_available = 3,
    already_connected = 4,
    argument_list_too_long = 5,
    argument_out_of_domain = 6,
    bad_address = 7,
    bad_file_descriptor = 8,
    bad_message = 9,
    broken_pipe = 10,
    connection_aborted = 11,
    connection_already_in_progress = 12,
    connection_refused = 13,
    connection_reset = 14,
    cross_device_link = 15,
    destination_address_required = 16,
    device_or_resource_busy = 17,
    directory_not_empty = 18,
    executable_format_error = 19,
    file_exists = 20,
    file_too_large = 21,
    filename_too_long = 22,
    function_not_supported = 23,
    host_unreachable = 24,
    identifier_removed = 25,
    illegal_byte_sequence = 26,
    inappropriate_io_control_operation = 27,
    interrupted = 28,
    invalid_argument = 29,
    invalid_seek = 30,
    io_error = 31,
    is_a_directory = 32,
    message_size = 33,
    network_down = 34,
    network_reset = 35,
    network_unreachable = 36,
    no_buffer_space = 37,
    no_child_process = 38,
    no_link = 39,
    no_lock_available = 40,
    no_message_available = 41,
    no_message = 42,
    no_protocol_option = 43,
    no_space_on_device = 44,
    no_stream_resources = 45,
    no_such_device_or_address = 46,
    no_such_device = 47,
    no_such_file_or_directory = 48,
    no_such_process = 49,
    not_a_directory = 50,
    not_a_socket = 51,
    not_a_stream = 52,
    not_connected = 53,
    not_enough_memory = 54,
    not_supported = 55,
    operation_canceled = 56,
    operation_in_progress = 57,
    operation_not_permitted = 58,
    operation_not_supported = 59,
    operation_would_block = 60,
    owner_dead = 61,
    permission_denied = 62,
    protocol_error = 63,
    protocol_not_supported = 64,
    read_only_file_system = 65,
    resource_deadlock_would_occur = 66,
    resource_unavailable_try_again = 67,
    result_out_of_range = 68,
    state_not_recoverable = 69,
    stream_timeout = 70,
    text_file_busy = 71,
    timed_out = 72,
    too_many_files_open_in_system = 73,
    too_many_files_open = 74,
    too_many_links = 75,
    too_many_symbolic_link_levels = 76,
    value_too_large = 77,
    wrong_protocol_type = 78,
};

// Parses an EPCD/`std::errc` spelling such as `invalid_argument`,
// `std::errc::invalid_argument` or `GenericCondition::invalid_argument`.
// Returns nullopt for an unknown spelling.
std::optional<GenericCondition> parse_generic_condition(std::string_view text) noexcept;

// Stable diagnostic spelling for a condition, or `"unknown"` for an
// unrecognized numeric value.
const char *generic_condition_name(GenericCondition condition) noexcept;

}   // namespace amc
