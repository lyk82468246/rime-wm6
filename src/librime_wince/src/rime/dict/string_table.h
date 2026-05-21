//
// rime/dict/string_table.h -- WinCE-port stub of upstream string_table.h.
//
// MVP: we don't vendor marisa-trie (yet). Same public API as upstream's
// marisa-backed StringTable, but the storage backend is a plain
// vector<string> keyed by insertion order. Lookup/HasKey use a
// side-map<string, StringId> for O(log n). CommonPrefixMatch / Predict
// do linear scans -- fine for the few-thousand-entry tables we'll ship
// in the MVP mini-dict; the marisa replacement will swap in transparently.
//
// On-disk format (NOT marisa-compatible -- our own simple TLV):
//   magic[4]   = "RST1"
//   count[4]   = uint32_t little-endian
//   repeat count times:
//     len[4]   = uint32_t
//     data[len] = utf-8 key bytes (no terminator)
//
// StringId = the insertion-order index assigned by StringTableBuilder.
// Dump preserves that order. Load reads in order, so deserialised id ==
// original id. This is the contract Table::OnLoad / OnBuildFinish rely on.
//
// Changes vs. upstream:
//   * `using StringId = uint32_t;` template alias -> typedef.
//   * `= default` ctors/dtors -> empty bodies.
//   * `marisa::Trie trie_;` -> `vector<string> keys_;` + `map<string,
//     StringId> index_;`.
//   * `marisa::Keyset keys_;` -> `vector<Pending> pending_;`.
//   * Default arg `StringId* reference = nullptr` -> `= NULL`.
//
#ifndef RIME_STRING_TABLE_H_
#define RIME_STRING_TABLE_H_

#include <stdint.h>
#include <rime_api.h>
#include <rime/common.h>

namespace rime {

typedef uint32_t StringId;

const StringId kInvalidStringId = (StringId)(-1);

class RIME_DLL StringTable {
 public:
  StringTable() {}
  virtual ~StringTable() {}
  // Load from a Dump()ed buffer (typically an mmap'd slice of a .table.bin).
  StringTable(const char* ptr, size_t size);

  bool HasKey(const string& key);
  StringId Lookup(const string& key);
  void CommonPrefixMatch(const string& query, vector<StringId>* result);
  void Predict(const string& query, vector<StringId>* result);
  string GetString(StringId string_id);

  size_t NumKeys() const;
  size_t BinarySize() const;

  // Read-only access to the internal sorted key list. Used by Prism's
  // flat-mode loader, which builds a Darts::DoubleArray over these keys
  // at Load time. The reference is stable for the StringTable's lifetime.
  const vector<string>& keys() const { return keys_; }

 protected:
  // Re-derive index_ from keys_ after Load or Build.
  void RebuildIndex();

  vector<string> keys_;             // id -> string
  map<string, StringId> index_;     // string -> id (for HasKey / Lookup)
};

class RIME_DLL StringTableBuilder : public StringTable {
 public:
  void Add(const string& key,
           double weight = 1.0,
           StringId* reference = NULL);
  void Clear();
  void Build();
  void Dump(char* ptr, size_t size);

 private:
  struct Pending {
    string key;
    double weight;
    StringId* ref;
    Pending() : weight(1.0), ref(NULL) {}
    Pending(const string& k, double w, StringId* r)
        : key(k), weight(w), ref(r) {}
  };
  vector<Pending> pending_;
};

}  // namespace rime

#endif  // RIME_STRING_TABLE_H_
