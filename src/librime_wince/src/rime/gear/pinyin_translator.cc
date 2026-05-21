//
// rime/gear/pinyin_translator.cc -- minimal MVP translator implementation.
//
// Flow:
//   1. Build SyllableGraph from input via Syllabifier + Prism alphabet.
//   2. Dictionary::Lookup -> DictEntryCollector keyed by end_pos.
//   3. For each (end_pos, iter), iterate the entries and append a
//      SimpleCandidate (type "phrase", quality = weight, comment = code).
//      [start, end) maps to [segment.start, segment.start + end_pos).
//   4. Return as a FifoTranslation (already weight-sorted within each
//      chunk by Dictionary; not globally re-sorted across lengths since
//      most IMEs prefer longest-match-first anyway).
//
// What this skips vs. upstream script_translator: predict_word lookups,
// blacklists, sentence composition via Poet, preedit formatting,
// UserDictionary merge. All deferred until those subsystems land.
//
#include <rime/gear/pinyin_translator.h>

#include <rime/candidate.h>
#include <rime/segmentation.h>
#include <rime/ticket.h>
#include <rime/algo/syllabifier.h>
#include <rime/dict/dictionary.h>
#include <rime/dict/prism.h>
#include <rime/dict/vocabulary.h>

namespace rime {

namespace {

// Default delimiters for syllabification. Upstream's luna_pinyin schema
// uses "' " (apostrophe + space) but at this layer we accept space too.
const char kDefaultDelimiters[] = " '";

const char kCandidateType[] = "phrase";

}  // namespace

PinyinTranslator::PinyinTranslator(const Ticket& ticket)
    : Translator(ticket) {}

PinyinTranslator::PinyinTranslator()
    : Translator(Ticket()) {}

PinyinTranslator::~PinyinTranslator() {}

bool PinyinTranslator::loaded() const {
  return dict_ && dict_->loaded();
}

bool PinyinTranslator::LoadDictionary(const string& name,
                                      const string& prism_path,
                                      const string& table_path) {
  dict_ = CreateDictionary(name, prism_path, table_path);
  if (!dict_) {
    return false;
  }
  if (!dict_->Load()) {
    dict_.reset();
    return false;
  }
  dict_name_ = name;
  return true;
}

an<Translation> PinyinTranslator::Query(const string& input,
                                        const Segment& segment) {
  if (!loaded() || input.empty()) {
    return an<Translation>();
  }

  // Build the syllable graph by walking the input through the prism.
  Syllabifier syllabifier(kDefaultDelimiters);
  SyllableGraph graph;
  Prism* prism = dict_->prism().get();
  if (!prism) {
    return an<Translation>();
  }
  int consumed = syllabifier.BuildSyllableGraph(input, *prism, &graph);
  if (consumed <= 0 || graph.interpreted_length == 0) {
    return an<Translation>();
  }

  an<DictEntryCollector> collector = dict_->Lookup(graph, 0);
  if (!collector || collector->empty()) {
    return an<Translation>();
  }

  an<FifoTranslation> result = New<FifoTranslation>();
  size_t seg_start = segment.start;

  // DictEntryCollector keys are end positions in `input`. We want to
  // emit longest matches first (typical IME ordering), so walk in
  // reverse-key order.
  for (DictEntryCollector::reverse_iterator rit = collector->rbegin();
       rit != collector->rend(); ++rit) {
    size_t end_pos = rit->first;
    DictEntryIterator& iter = rit->second;
    while (!iter.exhausted()) {
      an<DictEntry> entry = iter.Peek();
      if (entry) {
        an<SimpleCandidate> cand = New<SimpleCandidate>(
            string(kCandidateType),
            seg_start,
            seg_start + end_pos,
            entry->text);
        cand->set_comment(entry->comment);
        cand->set_preedit(entry->preedit);
        cand->set_quality(entry->weight);
        result->Append(cand);
      }
      if (!iter.Next())
        break;
    }
  }

  return result;
}

}  // namespace rime
