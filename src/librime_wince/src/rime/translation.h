//
// rime/translation.h -- WinCE-port mirror of upstream translation.h.
//
// Translation is the per-segment iterator-like that translators produce.
// Next() advances, Peek() returns the current candidate. The various
// subclasses compose: UnionTranslation glues two; MergedTranslation
// chooses among many via Compare(); CacheTranslation buffers Peek
// results to avoid re-running the underlying iterator.
//
// Changes vs. upstream:
//   * `= default` -> empty body.
//   * NSDMI -> default-ctor mem-initialiser lists.
//   * `list<of<Translation>>` / `vector<of<Translation>>` -> `> >`.
//   * `template <class T, class... Args> Cached(Args&&...)` -> 0..5 const&
//     overloads, matching how we already backported New<T>(...) in
//     common.h. Gear/ translators use Cached<T>(a, b, c) etc.; >5 args
//     would need extending.
//
#ifndef RIME_TRANSLATION_H_
#define RIME_TRANSLATION_H_

#include <rime_api.h>
#include <rime/candidate.h>
#include <rime/common.h>

namespace rime {

class RIME_DLL Translation {
 public:
  Translation() : exhausted_(false) {}
  virtual ~Translation() {}

  // A translation may contain multiple results, looks
  // something like a generator of candidates.
  virtual bool Next() = 0;

  virtual an<Candidate> Peek() = 0;

  // should it provide the next candidate (negative value, zero) or
  // should it give up the chance for other translations (positive)?
  virtual int Compare(an<Translation> other, const CandidateList& candidates);

  bool exhausted() const { return exhausted_; }

 protected:
  void set_exhausted(bool exhausted) { exhausted_ = exhausted; }

 private:
  bool exhausted_;
};

class UniqueTranslation : public Translation {
 public:
  UniqueTranslation(an<Candidate> candidate) : candidate_(candidate) {
    set_exhausted(!candidate);
  }

  bool Next();
  an<Candidate> Peek();

 protected:
  an<Candidate> candidate_;
};

class RIME_DLL FifoTranslation : public Translation {
 public:
  FifoTranslation();

  bool Next();
  an<Candidate> Peek();

  void Append(an<Candidate> candy);

  size_t size() const { return candies_.size() - cursor_; }

 protected:
  CandidateList candies_;
  size_t cursor_;
};

class UnionTranslation : public Translation {
 public:
  UnionTranslation();

  bool Next();
  an<Candidate> Peek();

  UnionTranslation& operator+=(an<Translation> t);

 protected:
  list<of<Translation> > translations_;
};

an<UnionTranslation> operator+(an<Translation> x, an<Translation> y);

class MergedTranslation : public Translation {
 public:
  explicit MergedTranslation(const CandidateList& previous_candidates);

  bool Next();
  an<Candidate> Peek();

  MergedTranslation& operator+=(an<Translation> t);

  size_t size() const { return translations_.size(); }

 protected:
  void Elect();

  const CandidateList& previous_candidates_;
  vector<of<Translation> > translations_;
  size_t elected_;
};

class CacheTranslation : public Translation {
 public:
  CacheTranslation(an<Translation> translation);

  virtual bool Next();
  virtual an<Candidate> Peek();

 protected:
  an<Translation> translation_;
  an<Candidate> cache_;
};

// C++03 backport of upstream's variadic Cached<T>(args...) factory.
// Gear translators call Cached<TableTranslation>(...) to wrap a new
// translation in a CacheTranslation in one expression. We provide 0..5
// const-reference overloads; extend if a future translator needs more.
template <class T>
inline an<Translation> Cached() {
  return New<CacheTranslation>(New<T>());
}
template <class T, class A1>
inline an<Translation> Cached(const A1& a1) {
  return New<CacheTranslation>(New<T, A1>(a1));
}
template <class T, class A1, class A2>
inline an<Translation> Cached(const A1& a1, const A2& a2) {
  return New<CacheTranslation>(New<T, A1, A2>(a1, a2));
}
template <class T, class A1, class A2, class A3>
inline an<Translation> Cached(const A1& a1, const A2& a2, const A3& a3) {
  return New<CacheTranslation>(New<T, A1, A2, A3>(a1, a2, a3));
}
template <class T, class A1, class A2, class A3, class A4>
inline an<Translation> Cached(const A1& a1, const A2& a2,
                              const A3& a3, const A4& a4) {
  return New<CacheTranslation>(New<T, A1, A2, A3, A4>(a1, a2, a3, a4));
}
template <class T, class A1, class A2, class A3, class A4, class A5>
inline an<Translation> Cached(const A1& a1, const A2& a2,
                              const A3& a3, const A4& a4, const A5& a5) {
  return New<CacheTranslation>(
      New<T, A1, A2, A3, A4, A5>(a1, a2, a3, a4, a5));
}

class DistinctTranslation : public CacheTranslation {
 public:
  DistinctTranslation(an<Translation> translation);
  virtual bool Next();

 protected:
  bool AlreadyHas(const string& text) const;

  set<string> candidate_set_;
};

class PrefetchTranslation : public Translation {
 public:
  PrefetchTranslation(an<Translation> translation);

  virtual bool Next();
  virtual an<Candidate> Peek();

 protected:
  virtual bool Replenish() { return false; }

  an<Translation> translation_;
  CandidateQueue cache_;
};

}  // namespace rime

#endif  // RIME_TRANSLATION_H_
