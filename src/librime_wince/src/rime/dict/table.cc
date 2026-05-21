//
// rime/dict/table.cc -- WinCE-port mirror of upstream table.cc.
//
// Implementation of TableAccessor / TableQuery / Table. Build/Save paths
// compile but Create() is the read-only stub, so they fail cleanly when
// invoked. Read paths (Load, QueryWords, QueryPhrases, Query) are the
// MVP target.
//
// Changes vs. upstream:
//   * `auto it = ...` -> explicit STL iterator types.
//   * Range-for `for (const auto& v : vocabulary)` -> iterator loop.
//   * `q.push({start_pos, initial_state})` brace-init -> explicit pair.
//   * `std::priority_queue<pair<...>>` etc. -> `> >` close.
//   * Lambda `auto extra = extra_code();` etc. -> explicit type.
//   * `(*result)[end_pos].push_back(accessor)` -- no change, just iteration.
//   * Removed `if (false)` dead-code block from upstream (ambiguous-source
//     penalty was guarded out anyway).
//
#include <cfloat>
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <queue>
#include <rime/common.h>
#include <rime/algo/syllabifier.h>
#include <rime/dict/table.h>

namespace rime {

const char kTableFormatLatest[] = "Rime::Table/4.0";
const double kTableFormatLowestCompatible = 4.0;

const char kTableFormatPrefix[] = "Rime::Table/";
const size_t kTableFormatPrefixLen = sizeof(kTableFormatPrefix) - 1;

TableAccessor::TableAccessor(const Code& index_code,
                             const List<table::Entry>* list,
                             double credibility,
                             double quality_len)
    : index_code_(index_code),
      entries_(list->at.get()),
      long_entries_(NULL),
      size_(list->size),
      cursor_(0),
      credibility_(credibility),
      quality_len_(quality_len) {}

TableAccessor::TableAccessor(const Code& index_code,
                             const Array<table::Entry>* array,
                             double credibility,
                             double quality_len)
    : index_code_(index_code),
      entries_(array->at),
      long_entries_(NULL),
      size_(array->size),
      cursor_(0),
      credibility_(credibility),
      quality_len_(quality_len) {}

TableAccessor::TableAccessor(const Code& index_code,
                             const table::TailIndex* code_map,
                             double credibility,
                             double quality_len)
    : index_code_(index_code),
      entries_(NULL),
      long_entries_(code_map->at),
      size_(code_map->size),
      cursor_(0),
      credibility_(credibility),
      quality_len_(quality_len) {}

bool TableAccessor::exhausted() const {
  if (entries_ || long_entries_) {
    return !(size_ - cursor_);
  }
  return true;
}

size_t TableAccessor::remaining() const {
  if (entries_ || long_entries_) {
    return size_ - cursor_;
  }
  return 0;
}

const table::Entry* TableAccessor::entry() const {
  if (exhausted())
    return NULL;
  if (entries_)
    return &entries_[cursor_];
  else
    return &long_entries_[cursor_].entry;
}

const table::Code* TableAccessor::extra_code() const {
  if (!long_entries_ || cursor_ >= size_)
    return NULL;
  return &long_entries_[cursor_].extra_code;
}

Code TableAccessor::code() const {
  const table::Code* extra = extra_code();
  if (!extra) {
    return index_code();
  }
  Code code(index_code());
  for (const SyllableId* p = extra->begin(); p != extra->end(); ++p) {
    code.push_back(*p);
  }
  return code;
}

bool TableAccessor::Next() {
  if (exhausted())
    return false;
  ++cursor_;
  return !exhausted();
}

bool TableQuery::Advance(SyllableId syllable_id,
                         double credibility,
                         double quality_len,
                         size_t last_pos) {
  if (!Walk(syllable_id)) {
    return false;
  }
  ++level_;
  index_code_.push_back(syllable_id);
  credibility_.push_back(credibility_sum() + credibility);
  quality_len_.push_back(quality_len_sum() + quality_len);
  last_pos_.push_back(last_pos);
  return true;
}

bool TableQuery::Backdate() {
  if (level_ == 0)
    return false;
  --level_;
  if (index_code_.size() > level_) {
    index_code_.pop_back();
    credibility_.pop_back();
    quality_len_.pop_back();
    last_pos_.pop_back();
  }
  return true;
}

void TableQuery::Reset() {
  level_ = 0;
  index_code_.clear();
  credibility_.clear();
  quality_len_.clear();
  last_pos_.clear();
}

namespace {

inline bool node_less(const table::TrunkIndexNode& a,
                      const table::TrunkIndexNode& b) {
  return a.key < b.key;
}

table::TrunkIndexNode* find_node(table::TrunkIndexNode* first,
                                 table::TrunkIndexNode* last,
                                 const SyllableId& key) {
  table::TrunkIndexNode target;
  target.key = key;
  table::TrunkIndexNode* it =
      std::lower_bound(first, last, target, node_less);
  return (it == last || key < it->key) ? last : it;
}

inline Code add_syllable(Code code, SyllableId syllable_id) {
  code.push_back(syllable_id);
  return code;
}

}  // namespace

bool TableQuery::Walk(SyllableId syllable_id) {
  if (level_ == 0) {
    if (!lv1_index_ || syllable_id < 0 ||
        syllable_id >= static_cast<SyllableId>(lv1_index_->size))
      return false;
    table::HeadIndexNode* node = &lv1_index_->at[syllable_id];
    if (!node->next_level)
      return false;
    lv2_index_ = &node->next_level->trunk();
  } else if (level_ == 1) {
    if (!lv2_index_)
      return false;
    table::TrunkIndexNode* node =
        find_node(lv2_index_->begin(), lv2_index_->end(), syllable_id);
    if (node == lv2_index_->end())
      return false;
    if (!node->next_level)
      return false;
    lv3_index_ = &node->next_level->trunk();
  } else if (level_ == 2) {
    if (!lv3_index_)
      return false;
    table::TrunkIndexNode* node =
        find_node(lv3_index_->begin(), lv3_index_->end(), syllable_id);
    if (node == lv3_index_->end())
      return false;
    if (!node->next_level)
      return false;
    lv4_index_ = &node->next_level->tail();
  } else {
    return false;
  }
  return true;
}

TableAccessor TableQuery::Access(SyllableId syllable_id,
                                 double credibility,
                                 double quality_len) const {
  credibility += credibility_sum();
  quality_len += quality_len_sum();
  if (level_ == 0) {
    if (!lv1_index_ || syllable_id < 0 ||
        syllable_id >= static_cast<SyllableId>(lv1_index_->size))
      return TableAccessor();
    table::HeadIndexNode* node = &lv1_index_->at[syllable_id];
    return TableAccessor(add_syllable(index_code_, syllable_id),
                         &node->entries, credibility, quality_len);
  } else if (level_ == 1 || level_ == 2) {
    table::TrunkIndex* index = (level_ == 1) ? lv2_index_ : lv3_index_;
    if (!index)
      return TableAccessor();
    table::TrunkIndexNode* node =
        find_node(index->begin(), index->end(), syllable_id);
    if (node == index->end())
      return TableAccessor();
    return TableAccessor(add_syllable(index_code_, syllable_id),
                         &node->entries, credibility, quality_len);
  } else if (level_ == 3) {
    if (!lv4_index_)
      return TableAccessor();
    return TableAccessor(index_code_, lv4_index_, credibility, quality_len);
  }
  return TableAccessor();
}

string Table::GetString(const table::StringType& x) {
  return string_table_->GetString(x.str_id());
}

bool Table::AddString(const string& src,
                      table::StringType* dest,
                      double weight) {
  string_table_builder_->Add(src, weight, &dest->str_id());
  return true;
}

bool Table::OnBuildStart() {
  string_table_builder_.reset(new StringTableBuilder);
  return true;
}

bool Table::OnBuildFinish() {
  string_table_builder_->Build();
  // saving string table image
  size_t image_size = string_table_builder_->BinarySize();
  char* image = Allocate<char>(image_size);
  if (!image) {
    LOG(ERROR) << "Error creating string table image.";
    return false;
  }
  string_table_builder_->Dump(image, image_size);
  metadata_->string_table = image;
  metadata_->string_table_size = image_size;
  return true;
}

bool Table::OnLoad() {
  string_table_.reset(new StringTable(metadata_->string_table.get(),
                                      metadata_->string_table_size));
  return true;
}

Table::Table(const path& file_path)
    : MappedFile(file_path),
      metadata_(NULL),
      syllabary_(NULL),
      index_(NULL) {}

Table::~Table() {}

bool Table::Load() {
  LOG(INFO) << "loading table file: " << file_path().string();

  if (IsOpen())
    Close();

  if (!OpenReadOnly()) {
    LOG(ERROR) << "Error opening table file '"
               << file_path().string() << "'.";
    return false;
  }

  metadata_ = Find<table::Metadata>(0);
  if (!metadata_) {
    LOG(ERROR) << "metadata not found.";
    Close();
    return false;
  }
  if (strncmp(metadata_->format, kTableFormatPrefix, kTableFormatPrefixLen)) {
    LOG(ERROR) << "invalid metadata.";
    Close();
    return false;
  }
  double format_version = atof(&metadata_->format[kTableFormatPrefixLen]);
  if (format_version < kTableFormatLowestCompatible - DBL_EPSILON) {
    LOG(ERROR) << "table format version " << format_version
               << " is no longer supported. please upgrade to version "
               << kTableFormatLatest;
    return false;
  }

  syllabary_ = metadata_->syllabary.get();
  if (!syllabary_) {
    LOG(ERROR) << "syllabary not found.";
    Close();
    return false;
  }
  index_ = metadata_->index.get();
  if (!index_) {
    LOG(ERROR) << "table index not found.";
    Close();
    return false;
  }

  return OnLoad();
}

bool Table::Save() {
  LOG(INFO) << "saving table file: " << file_path().string();
  if (!index_) {
    LOG(ERROR) << "the table has not been constructed!";
    return false;
  }
  return ShrinkToFit();
}

uint32_t Table::dict_file_checksum() const {
  return metadata_ ? metadata_->dict_file_checksum : 0;
}

bool Table::Build(const Syllabary& syllabary,
                  const Vocabulary& vocabulary,
                  size_t num_entries,
                  uint32_t dict_file_checksum) {
  const size_t kReservedSize = 4096;
  size_t num_syllables = syllabary.size();
  size_t estimated_file_size =
      kReservedSize + 32 * num_syllables + 64 * num_entries;
  LOG(INFO) << "building table.";
  LOG(INFO) << "num syllables: " << num_syllables;
  LOG(INFO) << "num entries: " << num_entries;
  if (!Create(estimated_file_size)) {
    LOG(ERROR) << "Error creating table file '"
               << file_path().string() << "'.";
    return false;
  }

  metadata_ = Allocate<table::Metadata>();
  if (!metadata_) {
    LOG(ERROR) << "Error creating metadata.";
    return false;
  }
  metadata_->dict_file_checksum = dict_file_checksum;
  metadata_->num_syllables = num_syllables;
  metadata_->num_entries = num_entries;

  if (!OnBuildStart()) {
    return false;
  }

  syllabary_ = CreateArray<table::StringType>(num_syllables);
  if (!syllabary_) {
    LOG(ERROR) << "Error creating syllabary.";
    return false;
  } else {
    size_t i = 0;
    for (Syllabary::const_iterator sit = syllabary.begin();
         sit != syllabary.end(); ++sit, ++i) {
      AddString(*sit, &syllabary_->at[i], 0.0);
    }
  }
  metadata_->syllabary = syllabary_;

  index_ = BuildIndex(vocabulary, num_syllables);
  if (!index_) {
    LOG(ERROR) << "Error creating table index.";
    return false;
  }
  metadata_->index = index_;

  if (!OnBuildFinish()) {
    return false;
  }

  std::strncpy(metadata_->format, kTableFormatLatest,
               table::Metadata::kFormatMaxLength);
  return true;
}

table::Index* Table::BuildIndex(const Vocabulary& vocabulary,
                                size_t num_syllables) {
  return reinterpret_cast<table::Index*>(
      BuildHeadIndex(vocabulary, num_syllables));
}

table::HeadIndex* Table::BuildHeadIndex(const Vocabulary& vocabulary,
                                        size_t num_syllables) {
  table::HeadIndex* index = CreateArray<table::HeadIndexNode>(num_syllables);
  if (!index) {
    return NULL;
  }
  for (Vocabulary::const_iterator vit = vocabulary.begin();
       vit != vocabulary.end(); ++vit) {
    int syllable_id = vit->first;
    table::HeadIndexNode& node = index->at[syllable_id];
    const ShortDictEntryList& entries = vit->second.entries;
    if (!BuildEntryList(entries, &node.entries)) {
      return NULL;
    }
    if (vit->second.next_level) {
      Code code;
      code.push_back(syllable_id);
      table::TrunkIndex* next_level_index =
          BuildTrunkIndex(code, *vit->second.next_level);
      if (!next_level_index) {
        return NULL;
      }
      node.next_level = reinterpret_cast<table::PhraseIndex*>(next_level_index);
    }
  }
  return index;
}

table::TrunkIndex* Table::BuildTrunkIndex(const Code& prefix,
                                          const Vocabulary& vocabulary) {
  table::TrunkIndex* index =
      CreateArray<table::TrunkIndexNode>(vocabulary.size());
  if (!index) {
    return NULL;
  }
  size_t count = 0;
  for (Vocabulary::const_iterator vit = vocabulary.begin();
       vit != vocabulary.end(); ++vit) {
    int syllable_id = vit->first;
    table::TrunkIndexNode& node = index->at[count++];
    node.key = syllable_id;
    const ShortDictEntryList& entries = vit->second.entries;
    if (!BuildEntryList(entries, &node.entries)) {
      return NULL;
    }
    if (vit->second.next_level) {
      Code code(prefix);
      code.push_back(syllable_id);
      if (code.size() < Code::kIndexCodeMaxLength) {
        table::TrunkIndex* next_level_index =
            BuildTrunkIndex(code, *vit->second.next_level);
        if (!next_level_index) {
          return NULL;
        }
        node.next_level =
            reinterpret_cast<table::PhraseIndex*>(next_level_index);
      } else {
        table::TailIndex* tail_index =
            BuildTailIndex(code, *vit->second.next_level);
        if (!tail_index) {
          return NULL;
        }
        node.next_level = reinterpret_cast<table::PhraseIndex*>(tail_index);
      }
    }
  }
  return index;
}

table::TailIndex* Table::BuildTailIndex(const Code& prefix,
                                        const Vocabulary& vocabulary) {
  if (vocabulary.find(-1) == vocabulary.end()) {
    return NULL;
  }
  const VocabularyPage& page = vocabulary.find(-1)->second;
  table::TailIndex* index = CreateArray<table::LongEntry>(page.entries.size());
  if (!index) {
    return NULL;
  }
  size_t count = 0;
  for (ShortDictEntryList::const_iterator sit = page.entries.begin();
       sit != page.entries.end(); ++sit) {
    table::LongEntry& dest = index->at[count++];
    size_t extra_code_length = (*sit)->code.size() - Code::kIndexCodeMaxLength;
    dest.extra_code.size = extra_code_length;
    dest.extra_code.at = Allocate<SyllableId>(extra_code_length);
    if (!dest.extra_code.at) {
      LOG(ERROR) << "Error creating code sequence; file size: " << file_size();
      return NULL;
    }
    std::copy((*sit)->code.begin() + Code::kIndexCodeMaxLength,
              (*sit)->code.end(),
              dest.extra_code.begin());
    BuildEntry(**sit, &dest.entry);
  }
  return index;
}

Array<table::Entry>* Table::BuildEntryArray(const ShortDictEntryList& entries) {
  Array<table::Entry>* array = CreateArray<table::Entry>(entries.size());
  if (!array) {
    return NULL;
  }
  for (size_t i = 0; i < entries.size(); ++i) {
    if (!BuildEntry(*entries[i], &array->at[i])) {
      return NULL;
    }
  }
  return array;
}

bool Table::BuildEntryList(const ShortDictEntryList& src,
                           List<table::Entry>* dest) {
  if (!dest)
    return false;
  dest->size = src.size();
  dest->at = Allocate<table::Entry>(src.size());
  if (!dest->at) {
    LOG(ERROR) << "Error creating table entries; file size: " << file_size();
    return false;
  }
  size_t i = 0;
  for (ShortDictEntryList::const_iterator d = src.begin();
       d != src.end(); ++d, ++i) {
    if (!BuildEntry(**d, &dest->at[i]))
      return false;
  }
  return true;
}

bool Table::BuildEntry(const ShortDictEntry& dict_entry, table::Entry* entry) {
  if (!entry)
    return false;
  if (!AddString(dict_entry.text, &entry->text, dict_entry.weight)) {
    LOG(ERROR) << "Error creating table entry '" << dict_entry.text
               << "'; file size: " << file_size();
    return false;
  }
  entry->weight = static_cast<table::Weight>(dict_entry.weight);
  return true;
}

bool Table::GetSyllabary(Syllabary* result) {
  if (!result || !syllabary_)
    return false;
  for (size_t i = 0; i < syllabary_->size; ++i) {
    result->insert(GetSyllableById(static_cast<SyllableId>(i)));
  }
  return true;
}

string Table::GetSyllableById(SyllableId syllable_id) {
  if (!syllabary_ || syllable_id < 0 ||
      syllable_id >= static_cast<SyllableId>(syllabary_->size))
    return string();
  return GetString(syllabary_->at[syllable_id]);
}

TableAccessor Table::QueryWords(SyllableId syllable_id) {
  TableQuery query(index_);
  return query.Access(syllable_id);
}

TableAccessor Table::QueryPhrases(const Code& code) {
  if (code.empty())
    return TableAccessor();
  TableQuery query(index_);
  for (size_t i = 0; i < Code::kIndexCodeMaxLength; ++i) {
    if (code.size() == i + 1)
      return query.Access(code[i]);
    if (!query.Advance(code[i]))
      return TableAccessor();
  }
  return query.Access(-1);
}

// log(0.05) ~= -3.0
const double kPenaltyForAmbiguousSyllable = -2.995732274;

bool Table::Query(const SyllableGraph& syll_graph,
                  size_t start_pos,
                  TableQueryResult* result) {
  if (!result || !index_ || start_pos >= syll_graph.interpreted_length)
    return false;
  result->clear();
  std::queue<pair<size_t, TableQuery> > q;
  TableQuery initial_state(index_);
  q.push(std::make_pair(start_pos, initial_state));
  while (!q.empty()) {
    size_t current_pos = q.front().first;
    TableQuery query(q.front().second);
    q.pop();
    SpellingIndices::const_iterator idx_it =
        syll_graph.indices.find(current_pos);
    if (idx_it == syll_graph.indices.end()) {
      continue;
    }
    if (query.level() == Code::kIndexCodeMaxLength) {
      TableAccessor accessor(query.Access(-1));
      if (!accessor.exhausted()) {
        (*result)[current_pos].push_back(accessor);
      }
      continue;
    }
    const SpellingIndex& index = idx_it->second;
    for (SpellingIndex::const_iterator sit = index.begin();
         sit != index.end(); ++sit) {
      SyllableId syll_id = sit->first;
      const SpellingPropertiesList& props_list = sit->second;
      for (SpellingPropertiesList::const_iterator pit = props_list.begin();
           pit != props_list.end(); ++pit) {
        const EdgeProperties* props = *pit;
        size_t end_pos = props->end_pos;

        // Upstream had a guarded-off `if (false)` block applying an
        // ambiguous-syllable penalty; dropped here since it never ran.
        double next_credibility = props->credibility;

        bool is_normal_spelling = props->type == kNormalSpelling;
        double delta_quality_len =
            (is_normal_spelling ? 1.0 : 0.0) * (end_pos - current_pos);
        TableAccessor accessor =
            query.Access(syll_id, next_credibility, delta_quality_len);
        if (!accessor.exhausted()) {
          (*result)[end_pos].push_back(accessor);
        }
        if (end_pos < syll_graph.interpreted_length &&
            query.Advance(syll_id, next_credibility, delta_quality_len,
                          current_pos)) {
          q.push(std::make_pair(end_pos, query));
          query.Backdate();
        }
      }
    }
  }
  // Silence "unused" warning on the penalty constant when DLOG is a no-op.
  (void)kPenaltyForAmbiguousSyllable;
  return !result->empty();
}

string Table::GetEntryText(const table::Entry& entry) {
  return GetString(entry.text);
}

}  // namespace rime
