//
// rime/algo/calculus.h -- WinCE-port mirror of upstream calculus.h.
//
// Calculus is rime's micro-DSL for spelling derivations in schema files:
//   xlit/abc/xyz/    transliterate codepoints a->x, b->y, c->z
//   xform/^([zcs])h/$1/   regex-based rewrite (zh -> z, ch -> c, sh -> s)
//   erase/.*ng$/     drop spellings ending in -ng
//   derive/.../.../  generate alternative spellings (plain / correction /
//                    abbrev / fuzz tag variants)
//
// Changes vs. upstream:
//   * `using Factory = ...` template alias -> typedef.
//   * `= default` -> empty body.
//   * `override` removed (C++11 keyword).
//   * `boost::regex` -> `wince::regex` (our hand-written Pike VM engine).
//
#ifndef RIME_CALCULUS_H_
#define RIME_CALCULUS_H_

#include <stdint.h>
#include <rime_api.h>
#include <rime/common.h>
#include "spelling.h"

namespace rime {

class Calculation {
 public:
  typedef Calculation* Factory(const vector<string>& args);

  Calculation() {}
  virtual ~Calculation() {}
  virtual bool Apply(Spelling* spelling) = 0;
  virtual bool addition() { return true; }
  virtual bool deletion() { return true; }
};

class Calculus {
 public:
  RIME_DLL Calculus();
  void Register(const string& token, Calculation::Factory* factory);
  RIME_DLL Calculation* Parse(const string& definition);

 private:
  map<string, Calculation::Factory*> factories_;
};

// xlit/zyx/abc/
class Transliteration : public Calculation {
 public:
  static Factory Parse;
  bool Apply(Spelling* spelling);

 protected:
  map<uint32_t, uint32_t> char_map_;
};

// xform/x/y/
class Transformation : public Calculation {
 public:
  static Factory Parse;
  bool Apply(Spelling* spelling);

 protected:
  wince::regex pattern_;
  string replacement_;
};

// erase/x/
class Erasion : public Calculation {
 public:
  static Factory Parse;
  bool Apply(Spelling* spelling);
  bool addition() { return false; }

 protected:
  wince::regex pattern_;
};

// derive/x/X/
class Derivation : public Transformation {
 public:
  static Factory Parse;
  bool deletion() { return false; }
};

// fuzz/zyx/zx/
class Fuzzing : public Derivation {
 public:
  static Factory Parse;
  bool Apply(Spelling* spelling);
};

// abbrev/zyx/z/
class Abbreviation : public Derivation {
 public:
  static Factory Parse;
  bool Apply(Spelling* spelling);
};

// derive/zyx/zxy/correction
class Correction : public Derivation {
 public:
  bool Apply(Spelling* spelling);
};

}  // namespace rime

#endif  // RIME_CALCULUS_H_
