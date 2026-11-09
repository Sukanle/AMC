#pragma once

#include <stdint.h>

#include <optional>
#include <string_view>

namespace skl::amc {

// Generic error condition for an error boundary. The numeric values mirror
// `skl::epcd::GenericCondition` and are frozen: they are part of the boundary
// protocol and must never be renumbered.
enum class GenericCondition : int32_t {
    UNKNOWN = -1,
    NONE = 0,
    ADDRESS_FAMILY_NOT_SUPPORTED = 1,
    ADDRESS_IN_USE = 2,
    ADDRESS_NOT_AVAILABLE = 3,
    ALREADY_CONNECTED = 4,
    ARGUMENT_LIST_TOO_LONG = 5,
    ARGUMENT_OUT_OF_DOMAIN = 6,
    BAD_ADDRESS = 7,
    BAD_FILE_DESCRIPTOR = 8,
    BAD_MESSAGE = 9,
    BROKEN_PIPE = 10,
    CONNECTION_ABORTED = 11,
    CONNECTION_ALREADY_IN_PROGRESS = 12,
    CONNECTION_REFUSED = 13,
    CONNECTION_RESET = 14,
    CROSS_DEVICE_LINK = 15,
    DESTINATION_ADDRESS_REQUIRED = 16,
    DEVICE_OR_RESOURCE_BUSY = 17,
    DIRECTORY_NOT_EMPTY = 18,
    EXECUTABLE_FORMAT_ERROR = 19,
    FILE_EXISTS = 20,
    FILE_TOO_LARGE = 21,
    FILENAME_TOO_LONG = 22,
    FUNCTION_NOT_SUPPORTED = 23,
    HOST_UNREACHABLE = 24,
    IDENTIFIER_REMOVED = 25,
    ILLEGAL_BYTE_SEQUENCE = 26,
    INAPPROPRIATE_IO_CONTROL_OPERATION = 27,
    INTERRUPTED = 28,
    INVALID_ARGUMENT = 29,
    INVALID_SEEK = 30,
    IO_ERROR = 31,
    IS_A_DIRECTORY = 32,
    MESSAGE_SIZE = 33,
    NETWORK_DOWN = 34,
    NETWORK_RESET = 35,
    NETWORK_UNREACHABLE = 36,
    NO_BUFFER_SPACE = 37,
    NO_CHILD_PROCESS = 38,
    NO_LINK = 39,
    NO_LOCK_AVAILABLE = 40,
    NO_MESSAGE_AVAILABLE = 41,
    NO_MESSAGE = 42,
    NO_PROTOCOL_OPTION = 43,
    NO_SPACE_ON_DEVICE = 44,
    NO_STREAM_RESOURCES = 45,
    NO_SUCH_DEVICE_OR_ADDRESS = 46,
    NO_SUCH_DEVICE = 47,
    NO_SUCH_FILE_OR_DIRECTORY = 48,
    NO_SUCH_PROCESS = 49,
    NOT_A_DIRECTORY = 50,
    NOT_A_SOCKET = 51,
    NOT_A_STREAM = 52,
    NOT_CONNECTED = 53,
    NOT_ENOUGH_MEMORY = 54,
    NOT_SUPPORTED = 55,
    OPERATION_CANCELED = 56,
    OPERATION_IN_PROGRESS = 57,
    OPERATION_NOT_PERMITTED = 58,
    OPERATION_NOT_SUPPORTED = 59,
    OPERATION_WOULD_BLOCK = 60,
    OWNER_DEAD = 61,
    PERMISSION_DENIED = 62,
    PROTOCOL_ERROR = 63,
    PROTOCOL_NOT_SUPPORTED = 64,
    READ_ONLY_FILE_SYSTEM = 65,
    RESOURCE_DEADLOCK_WOULD_OCCUR = 66,
    RESOURCE_UNAVAILABLE_TRY_AGAIN = 67,
    RESULT_OUT_OF_RANGE = 68,
    STATE_NOT_RECOVERABLE = 69,
    STREAM_TIMEOUT = 70,
    TEXT_FILE_BUSY = 71,
    TIMED_OUT = 72,
    TOO_MANY_FILES_OPEN_IN_SYSTEM = 73,
    TOO_MANY_FILES_OPEN = 74,
    TOO_MANY_LINKS = 75,
    TOO_MANY_SYMBOLIC_LINK_LEVELS = 76,
    VALUE_TOO_LARGE = 77,
    WRONG_PROTOCOL_TYPE = 78,
};

// Parses an EPCD/`std::errc` spelling such as `invalid_argument`,
// `std::errc::invalid_argument` or `GenericCondition::INVALID_ARGUMENT`.
// Returns nullopt for an unknown spelling.
std::optional<GenericCondition> parse_generic_condition(std::string_view text) noexcept;

// Stable diagnostic spelling for a condition, or `"unknown"` for an
// unrecognized numeric value.
const char *generic_condition_name(GenericCondition condition) noexcept;

}   // namespace skl::amc
