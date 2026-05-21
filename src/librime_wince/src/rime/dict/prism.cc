//
// rime/dict/prism.cc -- WinCE-port mirror of upstream prism.cc.
//
// SpellingAccessor + Prism (Load, Build, HasKey, GetValue,
// CommonPrefixSearch, ExpandSearch, QuerySpelling).
//
// On-device we only exercise the read path. Build/Save compile but fail at
// MappedFile::Create (read-only stub). Restoring the writer path is part
// of the "user_dict comes back" follow-on.
//
// Changes vs. upstream:
//   * `auto it = ...` -> explicit STL iterator types.
//   * Range-for over `*script` etc. -> classic iterator loops.
//   * `q.push({key, node_pos})` brace-init -> aggregate init via temp
//     `node_t tmp = {key, node_pos};`.
//   * `result->push_back(Match{ret, key_pos});` brace-init -> aggregate
//     init via temp `Match m = {ret, key_pos};`.
//   * `nullptr` for `Match` defaults -> NULL.
//   * Lambdas: none in this file.
//
#include <cfloat>
#include <cstring>
#include <cstdlib>
#include <queue>
#include <rime/algo/algebra.h>
#include <rime/dict/prism.h>

namespace rime {

namespace {

struct node_t {
  string key;
  size_t node_pos;
};

// type high bit 30 carries is_correction (avoids sign bit).
const int32_t kTypeIsCorrectionMask = 1 << 30;
const int32_t kSpellingTypeMask = ~kTypeIsCorrectionMask;

}  // namespace

const char kPrismFormat[] = "Rime::Prism/4.0";
const double kPrismFormatVersion = 4.0;

const char kPrismFormatPrefix[] = "Rime::Prism/";
const size_t kPrismFormatPrefixLen = sizeof(kPrismFormatPrefix) - 1;

// Flat mode (MVP): on-disk file is just a StringTable + checksum; we
// build the Darts trie in memory at Load time. See FlatMetadata in
// prism.h for the byte layout.
const char kPrismFlatFormat[] = "Rime::PrismFlat/1.0";
const char kPrismFlatFormatPrefix[] = "Rime::PrismFlat/";
const size_t kPrismFlatFormatPrefixLen = sizeof(kPrismFlatFormatPrefix) - 1;

const char kDefaultAlphabet[] = "abcdefghijklmnopqrstuvwxyz";

SpellingAccessor::SpellingAccessor(prism::SpellingMap* spelling_map,
                                   SyllableId spelling_id)
    : spelling_id_(spelling_id), iter_(NULL), end_(NULL) {
  if (spelling_map &&
      spelling_id < static_cast<SyllableId>(spelling_map->size)) {
    iter_ = spelling_map->at[spelling_id].begin();
    end_ = spelling_map->at[spelling_id].end();
  }
}

bool SpellingAccessor::Next() {
  if (exhausted())
    return false;
  if (!iter_ || ++iter_ >= end_)
    spelling_id_ = -1;
  return exhausted();
}

bool SpellingAccessor::exhausted() const {
  return spelling_id_ == -1;
}

SyllableId SpellingAccessor::syllable_id() const {
  if (iter_ && iter_ < end_)
    return iter_->syllable_id;
  else
    return spelling_id_;
}

SpellingProperties SpellingAccessor::properties() const {
  SpellingProperties props;
  if (iter_ && iter_ < end_) {
    int32_t packed_type = iter_->type;
    props.type = static_cast<SpellingType>(packed_type & kSpellingTypeMask);
    props.is_correction = (packed_type & kTypeIsCorrectionMask) != 0;
    props.credibility = iter_->credibility;
    if (!iter_->tips.empty())
      props.tips = iter_->tips.c_str();
  }
  return props;
}

Prism::Prism(const path& file_path)
    : MappedFile(file_path),
      metadata_(NULL),
      spelling_map_(NULL),
      format_(0.0) {
  // the<T> wraps wince::shared_ptr<T> whose raw-pointer ctor is explicit,
  // so we cannot initialise trie_ in the mem-init list from `new T`.
  // Reset in the body, mirroring `the<T>::reset(T*)`.
  trie_.reset(new Darts::DoubleArray);
}

bool Prism::Load() {
  LOG(INFO) << "loading prism file: " << file_path().string();

  if (IsOpen())
    Close();

  if (!OpenReadOnly()) {
    LOG(ERROR) << "error opening prism file '" << file_path().string() << "'.";
    return false;
  }

  // Dispatch on format prefix. Flat mode coexists with the upstream-style
  // darts-on-disk format -- the first 16 bytes of either header are a
  // null-terminated format string.
  char* head = Find<char>(0);
  if (head &&
      strncmp(head, kPrismFlatFormatPrefix,
              kPrismFlatFormatPrefixLen) == 0) {
    // Flat-mode path.
    prism::FlatMetadata* flat = Find<prism::FlatMetadata>(0);
    if (!flat) {
      LOG(ERROR) << "flat metadata not found.";
      Close();
      return false;
    }
    format_ = atof(&flat->format[kPrismFlatFormatPrefixLen]);
    if (flat->string_table_size == 0) {
      LOG(ERROR) << "flat prism has empty string table.";
      Close();
      return false;
    }
    char* st_ptr = Find<char>(flat->string_table_offset);
    if (!st_ptr) {
      LOG(ERROR) << "string table offset out of range.";
      Close();
      return false;
    }
    flat_string_table_.reset(
        new StringTable(st_ptr, flat->string_table_size));
    if (flat_string_table_->NumKeys() != flat->num_syllables) {
      LOG(ERROR) << "flat prism: syllable count mismatch (header="
                 << flat->num_syllables << ", string_table="
                 << flat_string_table_->NumKeys() << ").";
    }
    // Build the Darts double-array trie in-memory. StringTable's keys
    // are stored in insertion order; the Python builder writes them in
    // strict ascending order so Darts::build can consume directly.
    const vector<string>& keys = flat_string_table_->keys();
    if (keys.empty()) {
      // Empty prism: leave trie_ with no array set. HasKey etc. will
      // return false / 0; not a crash.
      LOG(INFO) << "loaded empty flat prism.";
      return true;
    }
    vector<const char*> key_ptrs(keys.size());
    vector<size_t> key_lens(keys.size());
    for (size_t i = 0; i < keys.size(); ++i) {
      key_ptrs[i] = keys[i].c_str();
      key_lens[i] = keys[i].size();
    }
    if (0 != trie_->build(keys.size(), &key_ptrs[0], &key_lens[0])) {
      LOG(ERROR) << "Error building darts trie from flat prism.";
      Close();
      return false;
    }
    // Flat mode has no spelling map: SpellingAccessor degrades to
    // returning the queried syllable_id with default SpellingProperties,
    // which is exactly the "this syllable maps to itself, normal type"
    // behaviour we want.
    spelling_map_ = NULL;
    metadata_ = NULL;
    LOG(INFO) << "loaded flat prism with " << keys.size() << " syllables.";
    return true;
  }

  // Upstream-style darts-on-disk path.
  metadata_ = Find<prism::Metadata>(0);
  if (!metadata_) {
    LOG(ERROR) << "metadata not found.";
    Close();
    return false;
  }
  if (strncmp(metadata_->format, kPrismFormatPrefix, kPrismFormatPrefixLen)) {
    LOG(ERROR) << "invalid metadata.";
    Close();
    return false;
  }
  format_ = atof(&metadata_->format[kPrismFormatPrefixLen]);

  if (format_ < kPrismFormatVersion - DBL_EPSILON) {
    LOG(INFO) << "prism format " << format_ << " is too old.";
    Close();
    return false;
  }

  char* array = metadata_->double_array.get();
  if (!array) {
    LOG(ERROR) << "double array image not found.";
    Close();
    return false;
  }
  size_t array_size = metadata_->double_array_size;
  LOG(INFO) << "found double array image of size " << array_size << ".";
  trie_->set_array(array, array_size);

  spelling_map_ = NULL;
  if (format_ > 1.0 - DBL_EPSILON) {
    spelling_map_ = metadata_->spelling_map.get();
  }
  return true;
}

bool Prism::Save() {
  LOG(INFO) << "saving prism file: " << file_path().string();
  if (!trie_->total_size()) {
    LOG(ERROR) << "the trie has not been constructed!";
    return false;
  }
  return ShrinkToFit();
}

bool Prism::Build(const Syllabary& syllabary,
                  const Script* script,
                  uint32_t dict_file_checksum,
                  uint32_t schema_file_checksum) {
  // building double-array trie
  size_t num_syllables = syllabary.size();
  size_t num_spellings = script ? script->size() : syllabary.size();
  vector<const char*> keys(num_spellings);
  size_t key_id = 0;
  size_t map_size = 0;
  if (script) {
    for (Script::const_iterator it = script->begin();
         it != script->end(); ++it, ++key_id) {
      keys[key_id] = it->first.c_str();
      map_size += it->second.size();
    }
  } else {
    for (Syllabary::const_iterator it = syllabary.begin();
         it != syllabary.end(); ++it, ++key_id) {
      keys[key_id] = it->c_str();
    }
  }
  if (0 != trie_->build(num_spellings, &keys[0])) {
    LOG(ERROR) << "Error building double-array trie.";
    return false;
  }
  // creating prism file
  size_t array_size = trie_->size();
  size_t image_size = trie_->total_size();
  const size_t kDescriptorExtraSize = 12;
  size_t estimated_map_size =
      num_spellings * 12 +
      map_size * (4 + sizeof(prism::SpellingDescriptor) + kDescriptorExtraSize);
  const size_t kReservedSize = 1024;
  if (!Create(image_size + estimated_map_size + kReservedSize)) {
    LOG(ERROR) << "Error creating prism file '"
               << file_path().string() << "'.";
    return false;
  }
  // creating metadata
  prism::Metadata* metadata = Allocate<prism::Metadata>();
  if (!metadata) {
    LOG(ERROR) << "Error creating metadata in file '"
               << file_path().string() << "'.";
    return false;
  }
  metadata->dict_file_checksum = dict_file_checksum;
  metadata->schema_file_checksum = schema_file_checksum;
  metadata->num_syllables = num_syllables;
  metadata->num_spellings = num_spellings;
  metadata_ = metadata;
  // alphabet
  {
    set<char> alphabet;
    for (size_t i = 0; i < num_spellings; ++i)
      for (const char* p = keys[i]; *p; ++p)
        alphabet.insert(*p);
    char* p = metadata->alphabet;
    set<char>::const_iterator c = alphabet.begin();
    for (; c != alphabet.end(); ++p, ++c)
      *p = *c;
    *p = '\0';
  }
  // saving double-array image
  char* array = Allocate<char>(image_size);
  if (!array) {
    LOG(ERROR) << "Error creating double-array image.";
    return false;
  }
  std::memcpy(array, trie_->array(), image_size);
  metadata->double_array = array;
  metadata->double_array_size = array_size;
  // building spelling map
  if (script) {
    map<string, SyllableId> syllable_to_id;
    SyllableId syll_id = 0;
    for (Syllabary::const_iterator it = syllabary.begin();
         it != syllabary.end(); ++it) {
      syllable_to_id[*it] = syll_id++;
    }
    prism::SpellingMap* spelling_map =
        CreateArray<prism::SpellingMapItem>(num_spellings);
    if (!spelling_map) {
      LOG(ERROR) << "Error creating spelling map.";
      return false;
    }
    Script::const_iterator i = script->begin();
    prism::SpellingMapItem* item = spelling_map->begin();
    for (; i != script->end(); ++i, ++item) {
      size_t list_size = i->second.size();
      item->size = list_size;
      item->at = Allocate<prism::SpellingDescriptor>(list_size);
      if (!item->at) {
        LOG(ERROR) << "Error creating spelling descriptors.";
        return false;
      }
      vector<Spelling>::const_iterator j = i->second.begin();
      prism::SpellingDescriptor* desc = item->begin();
      for (; j != i->second.end(); ++j, ++desc) {
        desc->syllable_id = syllable_to_id[j->str];
        int32_t packed_type = static_cast<int32_t>(j->properties.type);
        if (j->properties.is_correction) {
          packed_type |= kTypeIsCorrectionMask;
        }
        desc->type = packed_type;
        desc->credibility =
            static_cast<prism::Credibility>(j->properties.credibility);
        if (!j->properties.tips.empty() &&
            !CopyString(j->properties.tips, &desc->tips)) {
          LOG(ERROR) << "Error creating spelling properties.";
          return false;
        }
      }
    }
    metadata->spelling_map = spelling_map;
    spelling_map_ = spelling_map;
  }
  // at last, complete the metadata
  std::strncpy(metadata->format, kPrismFormat,
               prism::Metadata::kFormatMaxLength);
  return true;
}

bool Prism::HasKey(const string& key) {
  int value = trie_->exactMatchSearch<int>(key.c_str());
  return value != -1;
}

bool Prism::GetValue(const string& key, int* value) const {
  int result = trie_->exactMatchSearch<int>(key.c_str());
  if (result == -1) {
    return false;
  }
  *value = result;
  return true;
}

void Prism::CommonPrefixSearch(const string& key, vector<Match>* result) {
  if (!result || key.empty())
    return;
  size_t len = key.length();
  result->resize(len);
  size_t num_results =
      trie_->commonPrefixSearch(key.c_str(), &result->front(), len, len);
  result->resize(num_results);
}

void Prism::ExpandSearch(const string& key,
                         vector<Match>* result,
                         size_t limit) {
  if (!result)
    return;
  result->clear();
  size_t count = 0;
  size_t node_pos = 0;
  size_t key_pos = 0;
  int ret = trie_->traverse(key.c_str(), node_pos, key_pos);
  // key is not a valid path
  if (ret == -2)
    return;
  if (ret != -1) {
    Match m;
    m.value = ret;
    m.length = key_pos;
    result->push_back(m);
    if (limit && ++count >= limit)
      return;
  }
  std::queue<node_t> q;
  {
    node_t seed;
    seed.key = key;
    seed.node_pos = node_pos;
    q.push(seed);
  }
  while (!q.empty()) {
    node_t node = q.front();
    q.pop();
    const char* c =
        (format_ > 1.0 - DBL_EPSILON) ? metadata_->alphabet : kDefaultAlphabet;
    for (; *c; ++c) {
      string k = node.key + *c;
      size_t k_pos = node.key.length();
      size_t n_pos = node.node_pos;
      ret = trie_->traverse(k.c_str(), n_pos, k_pos);
      if (ret <= -2) {
        // ignore
      } else if (ret == -1) {
        node_t next;
        next.key = k;
        next.node_pos = n_pos;
        q.push(next);
      } else {
        node_t next;
        next.key = k;
        next.node_pos = n_pos;
        q.push(next);
        Match m;
        m.value = ret;
        m.length = k_pos;
        result->push_back(m);
        if (limit && ++count >= limit)
          return;
      }
    }
  }
}

SpellingAccessor Prism::QuerySpelling(SyllableId spelling_id) {
  return SpellingAccessor(spelling_map_, spelling_id);
}

size_t Prism::array_size() const {
  return trie_->size();
}

uint32_t Prism::dict_file_checksum() const {
  return metadata_ ? metadata_->dict_file_checksum : 0;
}

uint32_t Prism::schema_file_checksum() const {
  return metadata_ ? metadata_->schema_file_checksum : 0;
}

}  // namespace rime
