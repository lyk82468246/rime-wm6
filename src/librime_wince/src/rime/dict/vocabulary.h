//
// rime/dict/vocabulary.h -- WinCE-port mirror of upstream vocabulary.h.
//
// Pure data types: Code (vector<SyllableId>), DictEntry / ShortDictEntry
// (the per-word records), Vocabulary (multi-level page tree keyed by
// SyllableId). No third-party deps; mostly a mechanical C++03 backport.
//
// Changes vs. upstream:
//   * `using Syllabary = set<string>;` template alias -> typedef.
//   * `using SyllableId = int32_t;` -> typedef.
//   * NSDMI on double/int fields (weight, quality_len, commit_count, ...)
//     -> default-ctor mem-init list.
//   * `Code() = default;` etc. -> empty body.
//   * `ShortDictEntry ToShort() const { return {text, code, weight}; }`
//     brace-initialiser -> explicit ShortDictEntry ctor.
//   * `vector<of<X>>` -> `vector<of<X> >` (MSVC9 `> >` rule).
//   * `using DictEntryFilter = function<bool(an<DictEntry>)>;` -> typedef.
//
#ifndef RIME_VOCABULARY_H_
#define RIME_VOCABULARY_H_

#include <stdint.h>
#include <rime_api.h>
#include <rime/common.h>

namespace rime {

typedef set<string> Syllabary;

typedef int32_t SyllableId;

class Code : public vector<SyllableId> {
 public:
  Code() {}
  Code(const Code::const_iterator& begin, const Code::const_iterator& end)
      : vector<SyllableId>(begin, end) {}

  static const size_t kIndexCodeMaxLength = 3;

  bool operator<(const Code& other) const;
  bool operator==(const Code& other) const;

  void CreateIndex(Code* index_code);

  string ToString() const;
};

struct ShortDictEntry {
  string text;
  Code code;
  double weight;

  ShortDictEntry() : weight(0.0) {}
  ShortDictEntry(const string& t, const Code& c, double w)
      : text(t), code(c), weight(w) {}

  bool operator<(const ShortDictEntry& other) const;
};

struct DictEntry {
  string text;
  string comment;
  string preedit;
  Code code;           // multi-syllable code from prism
  string custom_code;  // user defined code
  double weight;
  double quality_len;
  int commit_count;
  int remaining_code_length;
  int matching_code_size;

  DictEntry()
      : weight(0.0),
        quality_len(0.0),
        commit_count(0),
        remaining_code_length(0),
        matching_code_size(0) {}

  ShortDictEntry ToShort() const {
    return ShortDictEntry(text, code, weight);
  }
  bool IsExactMatch() const {
    return matching_code_size == 0 ||
           (size_t)matching_code_size == code.size();
  }
  bool IsPredictiveMatch() const {
    return matching_code_size != 0 &&
           (size_t)matching_code_size < code.size();
  }
  bool operator<(const DictEntry& other) const;
};

class ShortDictEntryList : public vector<of<ShortDictEntry> > {
 public:
  void Sort();
  void SortRange(size_t start, size_t count);
};

class DictEntryList : public vector<of<DictEntry> > {
 public:
  void Sort();
  void SortRange(size_t start, size_t count);
};

typedef function<bool(an<DictEntry>)> DictEntryFilter;

class RIME_DLL DictEntryFilterBinder {
 public:
  virtual ~DictEntryFilterBinder() {}
  virtual void AddFilter(DictEntryFilter filter);

 protected:
  DictEntryFilter filter_;
};

class Vocabulary;

struct VocabularyPage {
  ShortDictEntryList entries;
  an<Vocabulary> next_level;
};

class Vocabulary : public map<int, VocabularyPage> {
 public:
  ShortDictEntryList* LocateEntries(const Code& code);
  void SortHomophones();
};

// word -> { code, ... }
typedef hash_map<string, set<string> > ReverseLookupTable;

}  // namespace rime

#endif  // RIME_VOCABULARY_H_
