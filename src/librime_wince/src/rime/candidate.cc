//
// rime/candidate.cc -- WinCE-port mirror of upstream candidate.cc.
//
// Changes vs. upstream:
//   * `auto shadow = ...` / `auto uniquified = ...` -> explicit
//     an<ShadowCandidate> / an<UniquifiedCandidate>.
//   * Range-for `for (const auto& item : uniquified->items())` -> classic
//     iterator loop over CandidateList::const_iterator.
//   * `vector<of<Candidate>>` -> `vector<of<Candidate> >` for return type.
//
#include <rime/candidate.h>

namespace rime {

static an<Candidate> UnpackShadowCandidate(const an<Candidate>& cand) {
  an<ShadowCandidate> shadow = As<ShadowCandidate>(cand);
  return shadow ? shadow->item() : cand;
}

an<Candidate> Candidate::GetGenuineCandidate(const an<Candidate>& cand) {
  an<UniquifiedCandidate> uniquified = As<UniquifiedCandidate>(cand);
  return UnpackShadowCandidate(uniquified ? uniquified->items().front() : cand);
}

vector<of<Candidate> > Candidate::GetGenuineCandidates(
    const an<Candidate>& cand) {
  vector<of<Candidate> > result;
  an<UniquifiedCandidate> uniquified = As<UniquifiedCandidate>(cand);
  if (uniquified) {
    const CandidateList& items = uniquified->items();
    for (CandidateList::const_iterator it = items.begin();
         it != items.end(); ++it) {
      result.push_back(UnpackShadowCandidate(*it));
    }
  } else {
    result.push_back(UnpackShadowCandidate(cand));
  }
  return result;
}

int Candidate::compare(const Candidate& other) {
  // the one nearer to the beginning of segment comes first
  int k = static_cast<int>(start_) - static_cast<int>(other.start_);
  if (k != 0)
    return k;
  // then the longer comes first
  k = static_cast<int>(end_) - static_cast<int>(other.end_);
  if (k != 0)
    return -k;
  // compare quality
  double qdiff = quality_ - other.quality_;
  if (qdiff != 0.)
    return (qdiff > 0.) ? -1 : 1;
  // draw
  return 0;
}

}  // namespace rime
