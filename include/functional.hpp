#pragma once

#include <algorithm>
#include <functional>

// Code built against the Metrowerks Standard Library uses a small legacy
// extension surface that is not named the same way by modern host standard
// libraries. Keep those source-level names available to unmodified clients.
#if !defined(__MWERKS__)
namespace std {

template <typename MemberPointer>
auto mem_func(MemberPointer member) {
  return std::mem_fun(member);
}

template <typename InputIt, typename UnaryPredicate>
InputIt rfind_if(InputIt first, InputIt last, UnaryPredicate predicate) {
  for (; first != last && !predicate(*first); --first) {}

  return first;
}

} // namespace std
#endif
