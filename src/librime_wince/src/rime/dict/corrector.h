//
// rime/dict/corrector.h -- WinCE-port MVP STUB of upstream corrector.h.
//
// The corrector lets users mistype a syllable and still get matches
// (e.g. typing "zhongguu" -> match "zhongguo"). It is NOT part of the
// MVP; this stub provides just enough surface for syllabifier.cc to
// reference Corrector* + Corrections without bringing the full
// implementation along.
//
// On-device every Syllabifier is constructed with corrector_ == NULL,
// so the `if (corrector_) { ToleranceSearch(...) }` branches never run.
// When we restore the real corrector, replace this stub with a
// port of upstream's corrector.h/.cc (Levenshtein-style fuzzy DFA
// over the prism trie).
//
#ifndef RIME_CORRECTOR_H_
#define RIME_CORRECTOR_H_

#include <rime/common.h>
#include <rime/dict/vocabulary.h>
#include <rime/dict/prism.h>

namespace rime {

namespace corrector {

// (SyllableId -> best match info). Real Corrector populates this from
// the prism with tolerance edits. Stub never produces entries.
typedef map<SyllableId, Prism::Match> Corrections;

}  // namespace corrector

class Corrector {
 public:
  virtual ~Corrector() {}
  // Real implementation walks the prism and fills `corrections` with
  // SyllableIds reachable within `tolerance` edits of `current_input`.
  // Stub: no-op; caller sees an empty map.
  virtual void ToleranceSearch(Prism& /*prism*/,
                               const string& /*current_input*/,
                               corrector::Corrections* corrections,
                               size_t /*tolerance*/) {
    if (corrections) corrections->clear();
  }
};

}  // namespace rime

#endif  // RIME_CORRECTOR_H_
