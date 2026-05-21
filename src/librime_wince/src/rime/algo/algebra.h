//
// rime/algo/algebra.h -- WinCE-port mirror of upstream algebra.h.
//
// Script and Projection: schema-driven spelling pipeline. Script holds the
// derived spellings (e.g. all the readings the speller will accept for a
// given pinyin syllable), Projection is a stack of Calculation objects --
// our calculus DSL -- applied in order.
//
// Changes vs. upstream:
//   * `map<string, vector<Spelling>>` -> `map<string, vector<Spelling> >`
//     (MSVC 9.0's C++03 parser sees the trailing `>>` as operator>>).
//   * `vector<of<Calculation>>` -> `vector<of<Calculation> >` for the same
//     reason.
//
#ifndef RIME_ALGEBRA_H_
#define RIME_ALGEBRA_H_

#include <rime/common.h>
#include <rime/config.h>
#include "spelling.h"

namespace rime {

class Calculation;
class Schema;  // forward-declared; algebra never instantiates one

class Script : public map<string, vector<Spelling> > {
 public:
  RIME_DLL bool AddSyllable(const string& syllable);
  void Merge(const string& s,
             const SpellingProperties& sp,
             const vector<Spelling>& v);
  void Dump(const path& file_path) const;
};

class Projection {
 public:
  RIME_DLL bool Load(an<ConfigList> settings);
  // "spelling" -> "gnilleps"
  RIME_DLL bool Apply(string* value);
  // {z, y, x} -> {a, b, c, d}
  RIME_DLL bool Apply(Script* value);

 protected:
  vector<of<Calculation> > calculation_;
};

}  // namespace rime

#endif  // RIME_ALGEBRA_H_
