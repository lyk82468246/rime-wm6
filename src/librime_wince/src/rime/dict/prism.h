//
// rime/dict/prism.h -- WinCE-port mirror of upstream prism.h.
//
// Wraps a Darts double-array trie over a memory-mapped .bin file. The
// .bin format is binary-identical to upstream's; we read prebuilt files
// generated on desktop. Build/Save paths compile but the underlying
// MappedFile::Create/Allocate are MVP-stubbed read-only, so any Build
// attempt fails early.
//
// Changes vs. upstream:
//   * `using Credibility = float;` template alias -> typedef.
//   * `using SpellingMapItem = ...` / `using SpellingMap = ...` -> typedef.
//   * `using Match = Darts::DoubleArray::result_pair_type;` -> typedef.
//   * NSDMI `prism::Metadata* metadata_ = nullptr;` and `double format_ = 0.0;`
//     -> default-ctor mem-init list.
//   * Default arg `const Script* script = nullptr` -> `= NULL`.
//
#ifndef RIME_PRISM_H_
#define RIME_PRISM_H_

#include <darts.h>
#include <rime/common.h>
#include <rime/algo/spelling.h>
#include <rime/dict/mapped_file.h>
#include <rime/dict/string_table.h>
#include <rime/dict/vocabulary.h>

namespace rime {

namespace prism {

typedef float Credibility;

struct SpellingDescriptor {
  SyllableId syllable_id;
  // bit 30: is_correction
  int32_t type;
  Credibility credibility;
  String tips;
};

typedef List<SpellingDescriptor> SpellingMapItem;
typedef Array<SpellingMapItem> SpellingMap;

struct Metadata {
  static const int kFormatMaxLength = 32;
  char format[kFormatMaxLength];
  uint32_t dict_file_checksum;
  uint32_t schema_file_checksum;
  uint32_t num_syllables;
  uint32_t num_spellings;
  uint32_t double_array_size;
  OffsetPtr<char> double_array;
  // v1.0
  OffsetPtr<SpellingMap> spelling_map;
  char alphabet[256];
};

// Flat mode -- our MVP-only format. The on-disk image is just a
// StringTable blob (sorted unique syllables) plus the dict checksum;
// Prism::Load builds the Darts::DoubleArray in-memory at load time.
//
// Format string: "Rime::PrismFlat/1.0". Coexists with upstream's
// "Rime::Prism/4.0" -- Prism::Load branches on the prefix.
//
struct FlatMetadata {
  static const int kFormatMaxLength = 32;
  char format[kFormatMaxLength];           // "Rime::PrismFlat/1.0\0..."
  uint32_t dict_file_checksum;
  uint32_t num_syllables;
  uint32_t string_table_offset;            // absolute byte offset
  uint32_t string_table_size;
};

}  // namespace prism

class SpellingAccessor {
 public:
  SpellingAccessor(prism::SpellingMap* spelling_map, SyllableId spelling_id);
  bool Next();
  bool exhausted() const;
  SyllableId syllable_id() const;
  SpellingProperties properties() const;

 protected:
  SyllableId spelling_id_;
  prism::SpellingDescriptor* iter_;
  prism::SpellingDescriptor* end_;
};

class Script;

class Prism : public MappedFile {
 public:
  typedef Darts::DoubleArray::result_pair_type Match;

  RIME_DLL explicit Prism(const path& file_path);

  RIME_DLL bool Load();
  RIME_DLL bool Save();
  RIME_DLL bool Build(const Syllabary& syllabary,
                      const Script* script = NULL,
                      uint32_t dict_file_checksum = 0,
                      uint32_t schema_file_checksum = 0);

  RIME_DLL bool HasKey(const string& key);
  RIME_DLL bool GetValue(const string& key, int* value) const;
  RIME_DLL void CommonPrefixSearch(const string& key, vector<Match>* result);
  RIME_DLL void ExpandSearch(const string& key,
                             vector<Match>* result,
                             size_t limit);
  SpellingAccessor QuerySpelling(SyllableId spelling_id);

  RIME_DLL size_t array_size() const;

  uint32_t dict_file_checksum() const;
  uint32_t schema_file_checksum() const;
  Darts::DoubleArray& trie() const { return *trie_; }

 protected:
  the<Darts::DoubleArray> trie_;
  prism::Metadata* metadata_;
  prism::SpellingMap* spelling_map_;
  double format_;
  // Flat mode only: holds the StringTable that backs the in-memory trie.
  // NULL when loading upstream-style .prism.bin (darts blob path).
  the<StringTable> flat_string_table_;
};

}  // namespace rime

#endif  // RIME_PRISM_H_
