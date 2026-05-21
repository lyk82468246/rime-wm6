//
// Copyright RIME Developers
// Distributed under the BSD License
//
// 2026-05 WinCE port: C++03 backport.
//
// Differences from upstream src/librime/src/rime/algo/strings.h:
//   * `enum class SplitBehavior { ... }`  ->  namespace-scoped plain enum,
//     so callers can keep using `SplitBehavior::KeepToken`. The TYPE name
//     `SplitBehavior` is replaced with `SplitBehavior::Type` in signatures.
//   * Rvalue-reference forwarding `T&& delim`  ->  `const T& delim`.
//   * `std::initializer_list<C>` overload of join() removed -- C++03 has no
//     initializer lists; callers needing brace-init must build a vector first.
//   * `std::begin(container)` / `std::end(container)`  ->  member calls.
//
#ifndef RIME_STRINGS_H_
#define RIME_STRINGS_H_

#include <rime/common.h>

namespace rime {
namespace strings {

// C++03 has no `enum class`. The namespace trick keeps the spelling
// `SplitBehavior::KeepToken` at call sites; only the type name changes.
namespace SplitBehavior {
enum Type { KeepToken, SkipToken };
}

vector<string> split(const string& str,
                     const string& delim,
                     SplitBehavior::Type behavior);

vector<string> split(const string& str, const string& delim);

template <typename Iter, typename T>
string join(Iter start, Iter end, const T& delim) {
  string result;
  if (start != end) {
    result += (*start);
    ++start;
  }
  for (; start != end; ++start) {
    result += (delim);
    result += (*start);
  }
  return result;
}

template <typename C, typename T>
inline string join(const C& container, const T& delim) {
  return join(container.begin(), container.end(), delim);
}

}  // namespace strings
}  // namespace rime

#endif  // RIME_STRINGS_H_
