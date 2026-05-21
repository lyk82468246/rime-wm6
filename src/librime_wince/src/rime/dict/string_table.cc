//
// rime/dict/string_table.cc -- WinCE-port stub of upstream string_table.cc.
//
// Implements the vector<string>-backed StringTable described in
// string_table.h. The .bin format we Dump matches our own Load -- it
// will NOT load marisa-format .table.bin files. When marisa is vendored,
// swap StringTable's storage for marisa::Trie and the public API stays
// stable.
//
// Limitations:
//   * NumKeys after Load is read from the magic+count header.
//   * BinarySize on a not-yet-Dump()ed builder returns the size we WILL
//     write -- so Dump caller can size its buffer correctly.
//   * Duplicate keys in Add() are deduped at Build(): only the first
//     insertion's reference gets the assigned id; subsequent duplicates
//     receive the SAME id (matches marisa behaviour).
//   * On a malformed buffer (bad magic, truncated len), the loader
//     leaves keys_/index_ empty and NumKeys()==0; callers see an empty
//     table rather than crashing.
//
#include <cstring>
#include <rime/dict/string_table.h>

namespace rime {

namespace {

const char kStringTableMagic[4] = { 'R', 'S', 'T', '1' };

inline uint32_t read_u32_le(const char* p) {
  return  (uint32_t)(uint8_t)p[0]
       | ((uint32_t)(uint8_t)p[1] <<  8)
       | ((uint32_t)(uint8_t)p[2] << 16)
       | ((uint32_t)(uint8_t)p[3] << 24);
}

inline void write_u32_le(char* p, uint32_t v) {
  p[0] = (char)(v & 0xff);
  p[1] = (char)((v >>  8) & 0xff);
  p[2] = (char)((v >> 16) & 0xff);
  p[3] = (char)((v >> 24) & 0xff);
}

}  // namespace

StringTable::StringTable(const char* ptr, size_t size) {
  if (!ptr || size < 8)
    return;
  if (std::memcmp(ptr, kStringTableMagic, 4) != 0)
    return;
  uint32_t count = read_u32_le(ptr + 4);
  size_t pos = 8;
  keys_.reserve(count);
  for (uint32_t i = 0; i < count; ++i) {
    if (pos + 4 > size)
      break;
    uint32_t len = read_u32_le(ptr + pos);
    pos += 4;
    if (pos + len > size)
      break;
    keys_.push_back(string(ptr + pos, len));
    pos += len;
  }
  RebuildIndex();
}

void StringTable::RebuildIndex() {
  index_.clear();
  for (StringId i = 0; i < (StringId)keys_.size(); ++i) {
    // Only the first occurrence wins -- mirrors marisa Keyset dedup.
    index_.insert(std::make_pair(keys_[i], i));
  }
}

bool StringTable::HasKey(const string& key) {
  return index_.find(key) != index_.end();
}

StringId StringTable::Lookup(const string& key) {
  map<string, StringId>::const_iterator it = index_.find(key);
  if (it == index_.end())
    return kInvalidStringId;
  return it->second;
}

void StringTable::CommonPrefixMatch(const string& query,
                                    vector<StringId>* result) {
  if (!result) return;
  result->clear();
  // A key k is a prefix of query iff k.size() <= query.size() and
  // query.compare(0, k.size(), k) == 0.
  for (StringId i = 0; i < (StringId)keys_.size(); ++i) {
    const string& k = keys_[i];
    if (k.size() <= query.size() &&
        query.compare(0, k.size(), k) == 0) {
      result->push_back(i);
    }
  }
}

void StringTable::Predict(const string& query, vector<StringId>* result) {
  if (!result) return;
  result->clear();
  // A key k has query as prefix iff k.size() >= query.size() and
  // k.compare(0, query.size(), query) == 0.
  for (StringId i = 0; i < (StringId)keys_.size(); ++i) {
    const string& k = keys_[i];
    if (k.size() >= query.size() &&
        k.compare(0, query.size(), query) == 0) {
      result->push_back(i);
    }
  }
}

string StringTable::GetString(StringId string_id) {
  if (string_id >= (StringId)keys_.size())
    return string();
  return keys_[string_id];
}

size_t StringTable::NumKeys() const {
  return keys_.size();
}

size_t StringTable::BinarySize() const {
  size_t total = 8;  // magic + count
  for (size_t i = 0; i < keys_.size(); ++i) {
    total += 4 + keys_[i].size();
  }
  return total;
}

// --- Builder ---------------------------------------------------------------

void StringTableBuilder::Add(const string& key,
                             double weight,
                             StringId* reference) {
  pending_.push_back(Pending(key, weight, reference));
}

void StringTableBuilder::Clear() {
  keys_.clear();
  index_.clear();
  pending_.clear();
}

void StringTableBuilder::Build() {
  // Assign each unique key the id of its first insertion. Duplicate Add()s
  // share that id; their `reference` slots all get the same value.
  keys_.clear();
  index_.clear();
  for (size_t i = 0; i < pending_.size(); ++i) {
    const Pending& p = pending_[i];
    StringId id;
    map<string, StringId>::const_iterator it = index_.find(p.key);
    if (it != index_.end()) {
      id = it->second;
    } else {
      id = (StringId)keys_.size();
      keys_.push_back(p.key);
      index_.insert(std::make_pair(p.key, id));
    }
    if (p.ref) *p.ref = id;
  }
}

void StringTableBuilder::Dump(char* ptr, size_t size) {
  if (!ptr) return;
  if (size < BinarySize()) {
    LOG(ERROR) << "insufficient memory to dump string table.";
    return;
  }
  std::memcpy(ptr, kStringTableMagic, 4);
  write_u32_le(ptr + 4, (uint32_t)keys_.size());
  size_t pos = 8;
  for (size_t i = 0; i < keys_.size(); ++i) {
    const string& k = keys_[i];
    write_u32_le(ptr + pos, (uint32_t)k.size());
    pos += 4;
    std::memcpy(ptr + pos, k.data(), k.size());
    pos += k.size();
  }
}

}  // namespace rime
