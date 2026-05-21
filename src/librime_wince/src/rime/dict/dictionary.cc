//
// rime/dict/dictionary.cc -- WinCE-port mirror of upstream dictionary.cc.
//
// DictEntryIterator + Dictionary core. DictionaryComponent is a stub
// pending ResourceResolver port (see header).
//
// Changes vs. upstream:
//   * `std::filesystem::exists` -> `wince::exists` (path.h shim).
//   * `<rime/resource.h>` / `Service::CreateDeployedResourceResolver` ->
//     unused; DictionaryComponent::Create returns NULL.
//   * Lambdas `[blacklist](an<DictEntry>) { ... }` -> named functor
//     `BlacklistFilter`.
//   * `auto` -> explicit types.
//   * Range-for -> classic iterator loops.
//   * `result->AddChunk({table.get(), a, remaining_code})` brace-init ->
//     dictionary::Chunk temporary then AddChunk(temp).
//   * `std::move(...)` on ctor args -> by-value or by-const-ref pass.
//   * `CodeMatch kFailed{false, 0, 0};` -> aggregate init via `=`.
//   * Move ctors removed from header; iterator copies via implicit copy.
//
#include <algorithm>
#include <rime/algo/syllabifier.h>
#include <rime/common.h>
#include <rime/dict/dictionary.h>
#include <rime/schema.h>
#include <rime/ticket.h>

namespace rime {

namespace dictionary {

struct Chunk {
  Table* table;
  Code code;
  const table::Entry* entries;
  size_t size;
  size_t cursor;
  string remaining_code;
  size_t matching_code_size;
  double credibility;
  double quality_len;

  Chunk()
      : table(NULL), entries(NULL), size(0), cursor(0),
        matching_code_size(0), credibility(0.0), quality_len(0.0) {}

  Chunk(Table* t,
        const Code& c,
        const table::Entry* e,
        size_t m,
        double cr,
        double q)
      : table(t),
        code(c),
        entries(e),
        size(1),
        cursor(0),
        matching_code_size(m),
        credibility(cr),
        quality_len(q) {}

  Chunk(Table* t, const TableAccessor& a, double cr, double q)
      : table(t),
        code(a.index_code()),
        entries(a.entry()),
        size(a.remaining()),
        cursor(0),
        matching_code_size(a.index_code().size()),
        credibility(cr),
        quality_len(q) {}

  Chunk(Table* t,
        const TableAccessor& a,
        const string& r,
        double cr,
        double q)
      : table(t),
        code(a.index_code()),
        entries(a.entry()),
        size(a.remaining()),
        cursor(0),
        remaining_code(r),
        matching_code_size(a.index_code().size()),
        credibility(cr),
        quality_len(q) {}

  bool is_exact_match() const { return matching_code_size == code.size(); }
  bool is_predictive_match() const { return matching_code_size < code.size(); }
};

struct QueryResult {
  vector<Chunk> chunks;
};

bool compare_chunk_by_head_element(const Chunk& a, const Chunk& b) {
  if (!a.entries || a.cursor >= a.size)
    return false;
  if (!b.entries || b.cursor >= b.size)
    return true;
  if (a.is_exact_match() != b.is_exact_match())
    return a.is_exact_match() > b.is_exact_match();
  if (a.remaining_code.length() != b.remaining_code.length())
    return a.remaining_code.length() < b.remaining_code.length();
  return a.credibility + a.entries[a.cursor].weight >
         b.credibility + b.entries[b.cursor].weight;
}

struct CodeMatch {
  bool success;
  size_t depth;
  size_t end_pos;
};

CodeMatch match_extra_code(const table::Code* extra_code,
                           size_t depth,
                           const SyllableGraph& syll_graph,
                           size_t current_pos,
                           bool predict_word) {
  CodeMatch kFailed = {false, 0, 0};
  if (!extra_code || depth >= extra_code->size) {
    CodeMatch ok = {true, depth, current_pos};
    return ok;
  }
  if (current_pos >= syll_graph.interpreted_length) {
    if (predict_word) {
      CodeMatch ok = {true, depth, syll_graph.interpreted_length};
      return ok;
    } else {
      return kFailed;
    }
  }
  SpellingIndices::const_iterator index =
      syll_graph.indices.find(current_pos);
  if (index == syll_graph.indices.end())
    return kFailed;
  SyllableId current_syll_id = extra_code->at[depth];
  SpellingIndex::const_iterator spellings = index->second.find(current_syll_id);
  if (spellings == index->second.end())
    return kFailed;
  CodeMatch best_match = kFailed;
  const SpellingPropertiesList& props_list = spellings->second;
  for (SpellingPropertiesList::const_iterator pit = props_list.begin();
       pit != props_list.end(); ++pit) {
    const SpellingProperties* props = *pit;
    CodeMatch match = match_extra_code(extra_code, depth + 1, syll_graph,
                                       props->end_pos, predict_word);
    if (!match.success)
      continue;
    if (match.end_pos > best_match.end_pos)
      best_match = match;
  }
  return best_match;
}

}  // namespace dictionary

// Named functor replacing upstream's `[blacklist](an<DictEntry> e) {...}`.
namespace {
struct BlacklistFilter {
  const hash_set<string>* blacklist;
  BlacklistFilter(const hash_set<string>* b) : blacklist(b) {}
  bool operator()(an<DictEntry> entry) const {
    return entry && !blacklist->count(entry->text);
  }
};
}  // namespace

DictEntryIterator::DictEntryIterator()
    : query_result_(New<dictionary::QueryResult>()),
      chunk_index_(0),
      entry_count_(0) {}

void DictEntryIterator::AddChunk(const dictionary::Chunk& chunk) {
  query_result_->chunks.push_back(chunk);
  entry_count_ += chunk.size;
}

void DictEntryIterator::Sort() {
  vector<dictionary::Chunk>& chunks = query_result_->chunks;
  std::partial_sort(chunks.begin() + chunk_index_,
                    chunks.begin() + chunk_index_ + 1, chunks.end(),
                    dictionary::compare_chunk_by_head_element);
}

void DictEntryIterator::AddFilter(DictEntryFilter filter) {
  DictEntryFilterBinder::AddFilter(filter);
  while (!exhausted() && !filter_(Peek())) {
    entry_.reset();
    FindNextEntry();
  }
}

an<DictEntry> DictEntryIterator::Peek() {
  if (!entry_ && !exhausted()) {
    const dictionary::Chunk& chunk = query_result_->chunks[chunk_index_];
    const table::Entry& e = chunk.entries[chunk.cursor];
    entry_ = New<DictEntry>();
    entry_->code = chunk.code;
    entry_->text = chunk.table->GetEntryText(e);
    const double kS = 18.420680743952367;  // log(1e8)
    entry_->weight = e.weight - kS + chunk.credibility;
    entry_->quality_len = chunk.quality_len;
    if (!chunk.remaining_code.empty()) {
      entry_->comment = "~" + chunk.remaining_code;
      entry_->remaining_code_length = chunk.remaining_code.length();
    }
    if (chunk.is_predictive_match()) {
      entry_->matching_code_size = chunk.matching_code_size;
    }
  }
  return entry_;
}

bool DictEntryIterator::FindNextEntry() {
  if (exhausted()) {
    return false;
  }
  dictionary::Chunk& chunk = query_result_->chunks[chunk_index_];
  if (++chunk.cursor >= chunk.size) {
    ++chunk_index_;
  }
  if (exhausted()) {
    return false;
  }
  Sort();
  return true;
}

bool DictEntryIterator::Next() {
  do {
    entry_.reset();
    if (!FindNextEntry()) {
      return false;
    }
  } while (filter_ && !filter_(Peek()));
  return true;
}

bool DictEntryIterator::Skip(size_t num_entries) {
  while (num_entries > 0) {
    if (exhausted())
      return false;
    dictionary::Chunk& chunk = query_result_->chunks[chunk_index_];
    if (chunk.cursor + num_entries < chunk.size) {
      chunk.cursor += num_entries;
      return true;
    }
    num_entries -= (chunk.size - chunk.cursor);
    ++chunk_index_;
  }
  return true;
}

bool DictEntryIterator::exhausted() const {
  return chunk_index_ >= query_result_->chunks.size();
}

// Dictionary

Dictionary::Dictionary(const string& name,
                       const vector<string>& packs,
                       const vector<of<Table> >& tables,
                       const an<Prism>& prism)
    : name_(name), packs_(packs), tables_(tables), prism_(prism) {}

Dictionary::~Dictionary() {
  // shared Table/Prism objects -- don't close.
}

namespace {

void lookup_table(Table* table,
                  DictEntryCollector* collector,
                  const SyllableGraph& syllable_graph,
                  size_t start_pos,
                  bool predict_word,
                  double initial_credibility) {
  TableQueryResult result;
  if (!table->Query(syllable_graph, start_pos, &result)) {
    return;
  }
  for (TableQueryResult::iterator vit = result.begin();
       vit != result.end(); ++vit) {
    size_t end_pos = vit->first;
    for (vector<TableAccessor>::iterator ait = vit->second.begin();
         ait != vit->second.end(); ++ait) {
      TableAccessor& a = *ait;
      double cr = initial_credibility + a.credibility();
      double q = a.quality_len();
      if (a.extra_code()) {
        do {
          dictionary::CodeMatch match = dictionary::match_extra_code(
              a.extra_code(), 0, syllable_graph, end_pos, predict_word);
          if (!match.success)
            continue;
          size_t matching_code_size = a.index_code().size() + match.depth;
          dictionary::Chunk chunk(table, a.code(), a.entry(),
                                  matching_code_size, cr, q);
          (*collector)[match.end_pos].AddChunk(chunk);
        } while (a.Next());
      } else {
        dictionary::Chunk chunk(table, a, cr, q);
        (*collector)[end_pos].AddChunk(chunk);
      }
    }
  }
}

}  // namespace

an<DictEntryCollector> Dictionary::Lookup(const SyllableGraph& syllable_graph,
                                          size_t start_pos,
                                          const hash_set<string>* blacklist,
                                          bool predict_word,
                                          double initial_credibility) {
  if (!loaded())
    return an<DictEntryCollector>();
  an<DictEntryCollector> collector = New<DictEntryCollector>();
  for (vector<of<Table> >::const_iterator tit = tables_.begin();
       tit != tables_.end(); ++tit) {
    if (!(*tit)->IsOpen())
      continue;
    lookup_table(tit->get(), collector.get(), syllable_graph, start_pos,
                 predict_word, initial_credibility);
  }
  if (collector->empty())
    return an<DictEntryCollector>();
  for (DictEntryCollector::iterator cit = collector->begin();
       cit != collector->end(); ++cit) {
    cit->second.Sort();
    if (blacklist && !blacklist->empty()) {
      cit->second.AddFilter(BlacklistFilter(blacklist));
    }
  }
  return collector;
}

size_t Dictionary::LookupWords(DictEntryIterator* result,
                               const string& str_code,
                               bool predictive,
                               size_t expand_search_limit,
                               const hash_set<string>* blacklist) {
  DLOG(INFO) << "lookup: " << str_code;
  if (!loaded())
    return 0;
  vector<Prism::Match> keys;
  if (predictive) {
    prism_->ExpandSearch(str_code, &keys, expand_search_limit);
  } else {
    Prism::Match match;
    match.value = 0;
    match.length = 0;
    if (prism_->GetValue(str_code, &match.value)) {
      keys.push_back(match);
    }
  }
  DLOG(INFO) << "found " << keys.size() << " matching keys thru the prism.";
  size_t code_length = str_code.length();
  for (vector<Prism::Match>::iterator mit = keys.begin();
       mit != keys.end(); ++mit) {
    Prism::Match& match = *mit;
    SpellingAccessor accessor(prism_->QuerySpelling(match.value));
    while (!accessor.exhausted()) {
      SyllableId syllable_id = accessor.syllable_id();
      SpellingType type = accessor.properties().type;
      accessor.Next();
      if (type > kNormalSpelling)
        continue;
      string remaining_code;
      if (match.length > code_length) {
        string syllable = primary_table()->GetSyllableById(syllable_id);
        if (syllable.length() > code_length)
          remaining_code = syllable.substr(code_length);
      }
      for (vector<of<Table> >::iterator tit = tables_.begin();
           tit != tables_.end(); ++tit) {
        if (!(*tit)->IsOpen())
          continue;
        TableAccessor a = (*tit)->QueryWords(syllable_id);
        if (!a.exhausted()) {
          dictionary::Chunk chunk(tit->get(), a, remaining_code, 0.0, 0.0);
          result->AddChunk(chunk);
        }
      }
    }
  }
  if (blacklist && !blacklist->empty()) {
    result->AddFilter(BlacklistFilter(blacklist));
  }
  return keys.size();
}

bool Dictionary::Decode(const Code& code, vector<string>* result) {
  if (!result || tables_.empty())
    return false;
  result->clear();
  for (Code::const_iterator cit = code.begin(); cit != code.end(); ++cit) {
    string s = primary_table()->GetSyllableById(*cit);
    if (s.empty())
      return false;
    result->push_back(s);
  }
  return true;
}

bool Dictionary::Exists() const {
  if (tables_.empty()) return false;
  return wince::exists(prism_->file_path()) &&
         wince::exists(tables_[0]->file_path());
}

bool Dictionary::Remove() {
  if (loaded())
    return false;
  prism_->Remove();
  for (vector<of<Table> >::iterator tit = tables_.begin();
       tit != tables_.end(); ++tit) {
    (*tit)->Remove();
  }
  return true;
}

bool Dictionary::Load() {
  LOG(INFO) << "loading dictionary '" << name_ << "'.";
  if (tables_.empty()) {
    LOG(ERROR) << "Cannot load dictionary '" << name_
               << "'; it contains no tables.";
    return false;
  }
  an<Table>& primary = tables_[0];
  if (!primary || (!primary->IsOpen() && !primary->Load())) {
    LOG(ERROR) << "Error loading table for dictionary '" << name_ << "'.";
    return false;
  }
  if (!prism_ || (!prism_->IsOpen() && !prism_->Load())) {
    LOG(ERROR) << "Error loading prism for dictionary '" << name_ << "'.";
    return false;
  }
  for (size_t i = 1; i < tables_.size(); ++i) {
    an<Table>& table = tables_[i];
    if (!table->IsOpen() && table->Exists() && table->Load()) {
      LOG(INFO) << "loaded pack: " << packs_[i - 1];
    }
  }
  return true;
}

bool Dictionary::loaded() const {
  return !tables_.empty() && tables_[0]->IsOpen() && prism_ && prism_->IsOpen();
}

// Factory shortcut: build a Dictionary directly from explicit file paths.
// Caller still owns Load() / Exists() decisions.
an<Dictionary> CreateDictionary(const string& name,
                                const path& prism_path,
                                const path& table_path) {
  an<Prism> prism = New<Prism>(prism_path);
  an<Table> table = New<Table>(table_path);
  vector<of<Table> > tables;
  // an<T> and of<T> are sibling wrappers over wince::shared_ptr<T>;
  // the templated `of(const wince::shared_ptr<U>&)` ctor takes the
  // base-class reference produced by upcasting `an<Table>`.
  of<Table> shared = table;
  tables.push_back(shared);
  vector<string> empty_packs;
  return New<Dictionary>(name, empty_packs, tables, prism);
}

// DictionaryComponent: stub. Real impl resolves prism/table paths via
// the ResourceResolver service (cut from MVP). Use CreateDictionary
// directly for now.
Dictionary* DictionaryComponent::Create(const Ticket& /*ticket*/) {
  LOG(WARNING) << "DictionaryComponent::Create is stubbed in MVP; "
               << "use CreateDictionary(name, prism_path, table_path).";
  return NULL;
}

}  // namespace rime
