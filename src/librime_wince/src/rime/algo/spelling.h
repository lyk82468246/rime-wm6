//
// Copyright RIME Developers
// Distributed under the BSD License
//
// 2012-01-17 GONG Chen <chen.sst@gmail.com>
// 2026-05    WinCE port: C++03 backport.
//
// Differences from upstream src/librime/src/rime/algo/spelling.h:
//   * Non-static data member initialisers (NSDMI) like `size_t end_pos = 0;`
//     moved into an explicit default constructor's mem-initialiser list.
//   * `Spelling() = default;` -> `Spelling() {}`.
//   * No other behavioural changes; the enum and struct layouts match.
//
#ifndef RIME_SPELLING_H_
#define RIME_SPELLING_H_

#include <rime/common.h>

namespace rime {

enum SpellingType {
  kNormalSpelling,
  kFuzzySpelling,
  kAbbreviation,
  kCompletion,
  kAmbiguousSpelling,
  kInvalidSpelling
};

struct SpellingProperties {
  SpellingType type;
  size_t end_pos;
  double credibility;
  string tips;
  bool is_correction;

  // C++03 default ctor replaces the upstream NSDMIs. `tips` (std::string)
  // self-initialises to empty.
  SpellingProperties()
      : type(kNormalSpelling),
        end_pos(0),
        credibility(0.0),
        is_correction(false) {}

  // Layer rule-generated properties on top of the current spelling.
  void Compose(const SpellingProperties& delta);
  // Merge another candidate spelling's properties into this one, preferring
  // the more confident / less fuzzy variant.
  void Update(const SpellingProperties& other);
};

struct Spelling {
  string str;
  SpellingProperties properties;

  Spelling() {}
  Spelling(const string& _str) : str(_str) {}

  bool operator==(const Spelling& other) { return str == other.str; }
  bool operator<(const Spelling& other) { return str < other.str; }
};

}  // namespace rime

#endif  // RIME_SPELLING_H_
