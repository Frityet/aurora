#pragma once

#include <algorithm>
#include <functional>
#include <utility>

// Code built against the Metrowerks Standard Library uses a small legacy
// extension surface that is not named the same way by modern host standard
// libraries. Keep those source-level names available to unmodified clients.
#if !defined(__MWERKS__)
namespace aurora::compat {

template <typename MemberPointer>
struct LegacyMemberFunction {
  MemberPointer member;

  template <typename Object, typename... Arguments>
  constexpr decltype(auto) operator()(Object&& object, Arguments&&... arguments) const {
    return std::invoke(member, std::forward<Object>(object), std::forward<Arguments>(arguments)...);
  }
};

template <typename Function, typename Value>
struct LegacyBindSecond {
  Function function;
  // MSL bind2nd retains its argument by reference, including const-reference
  // member arguments. Copying the value would change the client's semantics.
  const Value& value;

  template <typename Argument>
  constexpr decltype(auto) operator()(Argument&& argument) const {
    return function(std::forward<Argument>(argument), value);
  }
};

} // namespace aurora::compat

namespace std {

template <typename MemberPointer>
constexpr auto mem_func(MemberPointer member) {
  return aurora::compat::LegacyMemberFunction<MemberPointer>{member};
}

// Specialize on our adapter so host libraries that still expose their obsolete
// bind2nd overload can coexist with the MSL reference-preserving contract.
template <typename MemberPointer, typename Value>
constexpr auto bind2nd(const aurora::compat::LegacyMemberFunction<MemberPointer>& function, const Value& value) {
  return aurora::compat::LegacyBindSecond<aurora::compat::LegacyMemberFunction<MemberPointer>, Value>{function, value};
}

template <typename InputIt, typename UnaryPredicate>
InputIt rfind_if(InputIt first, InputIt last, UnaryPredicate predicate) {
  for (; first != last && !predicate(*first); --first) {}

  return first;
}

} // namespace std
#endif
