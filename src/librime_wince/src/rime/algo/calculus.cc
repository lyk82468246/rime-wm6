//
// rime/algo/calculus.cc -- WinCE-port mirror of upstream calculus.cc.
//
// Changes vs. upstream:
//   * <boost/algorithm/string.hpp> replaced with a hand-rolled single-char
//     split. boost::is_from_range(c,c) is just "split on this one char",
//     so 6 lines of substring loop suffice.
//   * <utf8.h> still included; resolves to wince_compat/utf8.h on our
//     include path (wince_compat comes before librime/src), not to upstream
//     utfcpp.
//   * `the<X> x(new X); ... x->...; return x.release();` rewritten as
//     `std::auto_ptr<X> x(new X); ... x->...; return x.release();`.
//     Reasons: our `the<>` is a shared_ptr wrapper (no `release()`); we want
//     to preserve the original code's exception safety (if `pattern_.assign`
//     throws regex_error mid-construction, the partial object is destroyed,
//     not leaked); ownership ends up as a raw `Calculation*` returned to
//     Calculus::Parse, who returns it to the caller.
//   * `boost::regex_*` -> `wince::regex_*`.
//   * `auto it = ...` -> explicit iterator type.
//   * Three CJK-tagged comments translated to ASCII (// 糾錯 etc.) to
//     comply with the ASCII-only source-file rule.
//
#include <memory>  // std::auto_ptr -- still legal (though deprecated) in C++03
#include <utf8.h>
#include <rime/algo/calculus.h>
#include <rime/common.h>

namespace rime {

const double kAbbreviationPenalty = -0.6931471805599453;   // log(0.5)
const double kFuzzySpellingPenalty = -0.6931471805599453;  // log(0.5)
const double kCorrectionPenalty = -4.605170185988091;      // log(0.01)

Calculus::Calculus() {
  Register("xlit", &Transliteration::Parse);
  Register("xform", &Transformation::Parse);
  Register("erase", &Erasion::Parse);
  Register("derive", &Derivation::Parse);
  Register("fuzz", &Fuzzing::Parse);
  Register("abbrev", &Abbreviation::Parse);
}

void Calculus::Register(const string& token, Calculation::Factory* factory) {
  factories_[token] = factory;
}

Calculation* Calculus::Parse(const string& definition) {
  // The separator is the first non-lowercase-alpha character in the
  // definition. e.g. "xform/^([zcs])h/$1/" -> separator '/'.
  size_t sep = definition.find_first_not_of("zyxwvutsrqponmlkjihgfedcba");
  if (sep == string::npos)
    return NULL;
  // boost::split-equivalent on a single delimiter char. token_compress_off,
  // so empty tokens are kept (matters for trailing `/` -> empty final arg).
  char sep_char = definition[sep];
  vector<string> args;
  string::size_type start = 0;
  for (string::size_type i = 0; i < definition.size(); ++i) {
    if (definition[i] == sep_char) {
      args.push_back(definition.substr(start, i - start));
      start = i + 1;
    }
  }
  args.push_back(definition.substr(start));
  if (args.empty())
    return NULL;
  map<string, Calculation::Factory*>::iterator it = factories_.find(args[0]);
  if (it == factories_.end())
    return NULL;
  Calculation* result = (*it->second)(args);
  return result;
}

// Transliteration

Calculation* Transliteration::Parse(const vector<string>& args) {
  if (args.size() < 3)
    return NULL;
  const string& left(args[1]);
  const string& right(args[2]);
  const char* pl = left.c_str();
  const char* pr = right.c_str();
  uint32_t cl, cr;
  map<uint32_t, uint32_t> char_map;
  while ((cl = utf8::unchecked::next(pl)), (cr = utf8::unchecked::next(pr)),
         cl && cr) {
    char_map[cl] = cr;
  }
  if (cl == 0 && cr == 0) {
    std::auto_ptr<Transliteration> x(new Transliteration);
    x->char_map_.swap(char_map);
    return x.release();
  }
  return NULL;
}

bool Transliteration::Apply(Spelling* spelling) {
  if (!spelling || spelling->str.empty())
    return false;
  bool modified = false;
  const char* p = spelling->str.c_str();
  const int buffer_len = 256;
  char buffer[buffer_len] = "";
  char* q = buffer;
  uint32_t c;
  while ((c = utf8::unchecked::next(p))) {
    if (q - buffer > buffer_len - 7) {  // insufficient space
      modified = false;
      break;
    }
    if (char_map_.find(c) != char_map_.end()) {
      c = char_map_[c];
      modified = true;
    }
    q = utf8::unchecked::append(c, q);
  }
  if (modified) {
    *q = '\0';
    spelling->str.assign(buffer);
  }
  return modified;
}

// Transformation

Calculation* Transformation::Parse(const vector<string>& args) {
  if (args.size() < 3)
    return NULL;
  const string& left(args[1]);
  const string& right(args[2]);
  if (left.empty())
    return NULL;
  std::auto_ptr<Transformation> x(new Transformation);
  x->pattern_.assign(left);
  x->replacement_.assign(right);
  return x.release();
}

bool Transformation::Apply(Spelling* spelling) {
  if (!spelling || spelling->str.empty())
    return false;
  string result = wince::regex_replace(spelling->str, pattern_, replacement_);
  if (result == spelling->str)
    return false;
  spelling->str.swap(result);
  return true;
}

// Erasion

Calculation* Erasion::Parse(const vector<string>& args) {
  if (args.size() < 2)
    return NULL;
  const string& pattern(args[1]);
  if (pattern.empty())
    return NULL;
  std::auto_ptr<Erasion> x(new Erasion);
  x->pattern_.assign(pattern);
  return x.release();
}

bool Erasion::Apply(Spelling* spelling) {
  if (!spelling || spelling->str.empty())
    return false;
  if (!wince::regex_match(spelling->str, pattern_))
    return false;
  spelling->str.clear();
  return true;
}

// Derivation

Calculation* Derivation::Parse(const vector<string>& args) {
  if (args.size() < 3)
    return NULL;

  const string& left(args[1]);
  const string& right(args[2]);
  if (left.empty())
    return NULL;

  if (args.size() > 3) {
    const string& tag = args[3];
    // correction (was: jiu-cuo)
    if (tag == "correction") {
      std::auto_ptr<Correction> x(new Correction);
      x->pattern_.assign(left);
      x->replacement_.assign(right);
      return x.release();
    }
    // abbreviation (was: jian-pin)
    if (tag == "abbrev") {
      std::auto_ptr<Abbreviation> x(new Abbreviation);
      x->pattern_.assign(left);
      x->replacement_.assign(right);
      return x.release();
    }
    // fuzzy spelling (was: mo-hu-yin)
    if (tag == "fuzz") {
      std::auto_ptr<Fuzzing> x(new Fuzzing);
      x->pattern_.assign(left);
      x->replacement_.assign(right);
      return x.release();
    }
    // unrecognized tag falls through to plain derive.
  }

  std::auto_ptr<Derivation> x(new Derivation);
  x->pattern_.assign(left);
  x->replacement_.assign(right);
  return x.release();
}

// Fuzzing

Calculation* Fuzzing::Parse(const vector<string>& args) {
  if (args.size() < 3)
    return NULL;
  const string& left(args[1]);
  const string& right(args[2]);
  if (left.empty())
    return NULL;
  std::auto_ptr<Fuzzing> x(new Fuzzing);
  x->pattern_.assign(left);
  x->replacement_.assign(right);
  return x.release();
}

bool Fuzzing::Apply(Spelling* spelling) {
  bool result = Transformation::Apply(spelling);
  if (result) {
    spelling->properties.type = kFuzzySpelling;
    spelling->properties.credibility += kFuzzySpellingPenalty;
  }
  return result;
}

// Abbreviation

Calculation* Abbreviation::Parse(const vector<string>& args) {
  if (args.size() < 3)
    return NULL;
  const string& left(args[1]);
  const string& right(args[2]);
  if (left.empty())
    return NULL;
  std::auto_ptr<Abbreviation> x(new Abbreviation);
  x->pattern_.assign(left);
  x->replacement_.assign(right);
  return x.release();
}

bool Abbreviation::Apply(Spelling* spelling) {
  bool result = Transformation::Apply(spelling);
  if (result) {
    spelling->properties.type = kAbbreviation;
    spelling->properties.credibility += kAbbreviationPenalty;
  }
  return result;
}

// Correction

bool Correction::Apply(Spelling* spelling) {
  bool result = Transformation::Apply(spelling);
  if (result) {
    spelling->properties.is_correction = true;
    spelling->properties.credibility += kCorrectionPenalty;
  }
  return result;
}

}  // namespace rime
