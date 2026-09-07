#pragma once

#include "aurora/allocation.hpp"

#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>

namespace aurora {

// Native error messages can survive the client arena that was selected when
// the error occurred. Copy the message and construct the exception with host
// allocation selected; unwinding restores the caller's complete routing state.
// Accept a message rather than an exception object: copying an existing
// stdexcept object could retain message storage from the client's arena.
template <typename Exception>
[[noreturn]] void throw_host_exception(std::string_view message) {
  static_assert(std::is_same_v<Exception, std::logic_error> || std::is_same_v<Exception, std::domain_error> ||
                std::is_same_v<Exception, std::invalid_argument> || std::is_same_v<Exception, std::length_error> ||
                std::is_same_v<Exception, std::out_of_range> || std::is_same_v<Exception, std::runtime_error> ||
                std::is_same_v<Exception, std::range_error> || std::is_same_v<Exception, std::overflow_error> ||
                std::is_same_v<Exception, std::underflow_error>);
  const allocation::HostAllocationScope host;
  throw Exception(std::string(message));
}

} // namespace aurora
