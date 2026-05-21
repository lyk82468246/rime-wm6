//
// rime/algo/syllabifier.h -- WinCE-port mirror of upstream syllabifier.h.
//
// Builds a directed graph of syllable matches over an input string by
// repeatedly querying a Prism. Drives Dictionary::Lookup downstream.
//
// Changes vs. upstream:
//   * `using SyllableId = int32_t;` template alias -> typedef.
//   * `using SpellingMap = ...` etc. template aliases -> typedef.
//   * `EdgeProperties() = default;` -> empty body.
//   * NSDMI `size_t input_length = 0;` etc. -> default-ctor mem-init list
//     on SyllableGraph (added explicitly below).
//   * Default args `bool enable_completion = false` -> kept (legal C++03).
//   * `Corrector* corrector_ = nullptr;` -> default-ctor mem-init list.
//
#ifndef RIME_SYLLABIFIER_H_
#define RIME_SYLLABIFIER_H_

#include <stdint.h>
#include <rime_api.h>
#include <rime/common.h>
#include "spelling.h"

namespace rime {

class Prism;
class Corrector;

typedef int32_t SyllableId;

struct EdgeProperties : SpellingProperties {
  EdgeProperties() {}
  EdgeProperties(SpellingProperties sup) : SpellingProperties(sup) {}
  // Start positions where this edge is ambiguous (e.g. syllable boundary
  // is ambiguous when same code can be split multiple ways).
  set<size_t> ambiguous_source_positions;
};

typedef map<SyllableId, EdgeProperties> SpellingMap;
typedef map<size_t, SpellingType> VertexMap;
typedef map<size_t, SpellingMap> EndVertexMap;
typedef map<size_t, EndVertexMap> EdgeMap;

typedef vector<const EdgeProperties*> SpellingPropertiesList;
typedef map<SyllableId, SpellingPropertiesList> SpellingIndex;
typedef map<size_t, SpellingIndex> SpellingIndices;

struct SyllableGraph {
  size_t input_length;
  size_t interpreted_length;
  VertexMap vertices;
  EdgeMap edges;
  SpellingIndices indices;

  SyllableGraph() : input_length(0), interpreted_length(0) {}
};

class Syllabifier {
 public:
  Syllabifier() : enable_completion_(false), strict_spelling_(false),
                  corrector_(NULL) {}
  explicit Syllabifier(const string& delimiters,
                       bool enable_completion = false,
                       bool strict_spelling = false)
      : delimiters_(delimiters),
        enable_completion_(enable_completion),
        strict_spelling_(strict_spelling),
        corrector_(NULL) {}

  RIME_DLL int BuildSyllableGraph(const string& input,
                                  Prism& prism,
                                  SyllableGraph* graph);
  RIME_DLL void EnableCorrection(Corrector* corrector);

 protected:
  void CheckOverlappedSpellings(SyllableGraph* graph, size_t start, size_t end);
  void Transpose(SyllableGraph* graph);

  string delimiters_;
  bool enable_completion_;
  bool strict_spelling_;
  Corrector* corrector_;
};

}  // namespace rime

#endif  // RIME_SYLLABIFIER_H_
