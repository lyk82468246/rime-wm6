//
// rime/gear/pinyin_translator.h -- minimal MVP translator for the WinCE
// port. Wraps Dictionary + Syllabifier so the engine can turn pinyin
// input into Chinese candidates.
//
// This is a deliberate subset of upstream's script_translator. Cut for
// MVP scope:
//   * Memory / UserDictionary / Poet / grammar
//   * TranslatorOptions (preedit formatting, comments, hints)
//   * Corrector (typo tolerance)
//   * filtering / blacklist
//   * sentence composition across multiple segments
//
// What we DO produce per Query:
//   * For each matched code length L starting at segment.start, emit
//     candidates whose text comes from DictEntry::text and whose
//     [start, end) covers [seg.start, seg.start + L).
//   * Sorted by weight desc (DictEntryIterator does partial sort).
//
// The fuller script_translator gets ported (or this file is grown)
// once UserDictionary + Poet land.
//
#ifndef RIME_PINYIN_TRANSLATOR_H_
#define RIME_PINYIN_TRANSLATOR_H_

#include <rime/common.h>
#include <rime/translation.h>
#include <rime/translator.h>

namespace rime {

class Dictionary;
class Prism;
class Syllabifier;
struct Segment;

class PinyinTranslator : public Translator {
 public:
  explicit PinyinTranslator(const Ticket& ticket);
  virtual ~PinyinTranslator();

  virtual an<Translation> Query(const string& input,
                                const Segment& segment);

  // MVP convenience: load a dictionary by name with explicit .bin paths,
  // bypassing the still-stubbed DictionaryComponent / ResourceResolver.
  // Returns true on success.
  bool LoadDictionary(const string& name,
                      const string& prism_path,
                      const string& table_path);

  // For tests / direct construction (no Engine / Ticket needed). Caller
  // must still call LoadDictionary before Query.
  PinyinTranslator();

  bool loaded() const;
  const string& dict_name() const { return dict_name_; }

 private:
  an<Dictionary> dict_;
  string dict_name_;
};

}  // namespace rime

#endif  // RIME_PINYIN_TRANSLATOR_H_
