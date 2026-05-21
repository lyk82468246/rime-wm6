//
// rime/dict/dictionary.h -- WinCE-port mirror of upstream dictionary.h.
//
// Two layers:
//   * DictEntryIterator -- streaming view over a query's Chunks.
//     Supports filter binding and partial-sort by chunk-head weight.
//   * Dictionary -- ties together a Prism (input -> syllable graph) and
//     one or more Tables (syllable code -> phrase entries).
//
// MVP scope adjustments:
//   * `DictionaryComponent` is STUBBED. The Component::Create flow uses
//     `Service::instance().CreateDeployedResourceResolver(...)` which we
//     cut from service.h. The full Component path comes back when
//     ResourceResolver is restored. For now, use the new helper
//     `CreateDictionary(name, prism_path, table_path)` to build a
//     Dictionary directly from explicit file paths.
//
// Changes vs. upstream:
//   * Move ctors / `= default` on DictEntryIterator dropped (C++03 has
//     no move semantics; copying via the implicit copy ctor is fine
//     since we never `std::move` an iterator across containers).
//   * NSDMI on DictEntryIterator fields -> default-ctor mem-init list.
//   * `vector<of<Table>>` -> `vector<of<Table> >`.
//   * `using DictEntryCollector = ...` -> typedef.
//   * Default arg `predict_word = false` etc. -> kept.
//   * Lambdas in Lookup/LookupWords -> moved to .cc as named functor.
//   * `std::move(packs)` ctor arg pass -> by-value + swap.
//
#ifndef RIME_DICTIONARY_H_
#define RIME_DICTIONARY_H_

#include <rime_api.h>
#include <rime/common.h>
#include <rime/component.h>
#include <rime/dict/prism.h>
#include <rime/dict/table.h>
#include <rime/dict/vocabulary.h>

namespace rime {

namespace dictionary {

struct Chunk;
struct QueryResult;

}  // namespace dictionary

class RIME_DLL DictEntryIterator : public DictEntryFilterBinder {
 public:
  DictEntryIterator();
  virtual ~DictEntryIterator() {}

  void AddChunk(const dictionary::Chunk& chunk);
  void Sort();
  virtual void AddFilter(DictEntryFilter filter);
  an<DictEntry> Peek();
  bool Next();
  bool Skip(size_t num_entries);
  bool exhausted() const;
  size_t entry_count() const { return entry_count_; }

 protected:
  bool FindNextEntry();

 private:
  an<dictionary::QueryResult> query_result_;
  size_t chunk_index_;
  an<DictEntry> entry_;
  size_t entry_count_;
};

typedef map<size_t, DictEntryIterator> DictEntryCollector;

class Config;
class Schema;
struct SyllableGraph;
struct Ticket;

class Dictionary : public Class<Dictionary, const Ticket&> {
 public:
  RIME_DLL Dictionary(const string& name,
                      const vector<string>& packs,
                      const vector<of<Table> >& tables,
                      const an<Prism>& prism);
  virtual ~Dictionary();

  bool Exists() const;
  RIME_DLL bool Remove();
  RIME_DLL bool Load();

  RIME_DLL an<DictEntryCollector> Lookup(
      const SyllableGraph& syllable_graph,
      size_t start_pos,
      const hash_set<string>* blacklist = NULL,
      bool predict_word = false,
      double initial_credibility = 0.0);
  RIME_DLL size_t LookupWords(DictEntryIterator* result,
                              const string& str_code,
                              bool predictive,
                              size_t limit = 0,
                              const hash_set<string>* blacklist = NULL);
  RIME_DLL bool Decode(const Code& code, vector<string>* result);

  const string& name() const { return name_; }
  RIME_DLL bool loaded() const;

  const vector<string>& packs() const { return packs_; }
  const vector<of<Table> >& tables() const { return tables_; }
  const an<Table>& primary_table() const { return tables_[0]; }
  const an<Prism>& prism() const { return prism_; }

 private:
  string name_;
  vector<string> packs_;
  vector<of<Table> > tables_;
  an<Prism> prism_;
};

// MVP convenience factory: bypasses Component/Ticket/ResourceResolver and
// builds a Dictionary from explicit on-disk file paths. The returned
// Dictionary still needs .Load() before use.
RIME_DLL an<Dictionary> CreateDictionary(const string& name,
                                         const path& prism_path,
                                         const path& table_path);

// Stub DictionaryComponent. Create() returns NULL pending the
// ResourceResolver port. Present so Class<Dictionary>::Require("dictionary")
// can still resolve, just yields a null factory.
class DictionaryComponent : public Dictionary::Component {
 public:
  DictionaryComponent() {}
  virtual ~DictionaryComponent() {}
  virtual Dictionary* Create(const Ticket& ticket);
};

}  // namespace rime

#endif  // RIME_DICTIONARY_H_
