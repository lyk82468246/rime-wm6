//
// Copyright RIME Developers
// Distributed under the BSD License
//
// 2026-05 WinCE port: C++03 backport.
//
// Differences from upstream src/librime/src/rime/algo/strings.cc:
//   * `SplitBehavior` enum parameter type spelled `SplitBehavior::Type` to
//     match the namespace-scoped enum in our strings.h.
//   * `emplace_back(...)`  ->  `push_back(...)`. emplace_back is C++11.
//   * Removed the trailing `;` after function closing braces (harmless but
//     pedantically illegal in C++03 at namespace scope).
//
#include <rime/algo/strings.h>

namespace rime {
namespace strings {

vector<string> split(const string& str,
                     const string& delim,
                     SplitBehavior::Type behavior) {
  vector<string> result;
  size_t lastPos, pos;
  if (behavior == SplitBehavior::SkipToken) {
    lastPos = str.find_first_not_of(delim, 0);
  } else {
    lastPos = 0;
  }
  pos = str.find_first_of(delim, lastPos);

  while (std::string::npos != pos || std::string::npos != lastPos) {
    result.push_back(str.substr(lastPos, pos - lastPos));
    if (behavior == SplitBehavior::SkipToken) {
      lastPos = str.find_first_not_of(delim, pos);
    } else {
      if (pos == std::string::npos) {
        break;
      }
      lastPos = pos + 1;
    }
    pos = str.find_first_of(delim, lastPos);
  }
  return result;
}

vector<string> split(const string& str, const string& delim) {
  return split(str, delim, SplitBehavior::KeepToken);
}

}  // namespace strings
}  // namespace rime
