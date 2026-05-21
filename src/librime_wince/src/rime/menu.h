//
// rime/menu.h -- WinCE-port mirror of upstream menu.h.
//
// Translation -> CandidateList pagination layer. Owns a MergedTranslation
// and a chain of filters applied to it; Prepare() pulls candidates lazily,
// CreatePage() slices them.
//
// Changes vs. upstream:
//   * NSDMI `int page_size = 0;` etc. -> default-ctor mem-init list.
//
#ifndef RIME_MENU_H_
#define RIME_MENU_H_

#include <rime_api.h>
#include <rime/candidate.h>
#include <rime/common.h>

namespace rime {

struct Page {
  int page_size;
  int page_no;
  bool is_last_page;
  CandidateList candidates;

  Page() : page_size(0), page_no(0), is_last_page(false) {}
};

class Filter;
class MergedTranslation;
class Translation;

class Menu {
 public:
  RIME_DLL Menu();

  RIME_DLL void AddTranslation(an<Translation> translation);
  void AddFilter(Filter* filter);

  RIME_DLL size_t Prepare(size_t candidate_count);
  RIME_DLL Page* CreatePage(size_t page_size, size_t page_no);
  an<Candidate> GetCandidateAt(size_t index);

  // CAVEAT: returns the number of candidates currently obtained,
  // rather than the total number of available candidates.
  size_t candidate_count() const { return candidates_.size(); }

  bool empty() const;

 private:
  an<MergedTranslation> merged_;
  an<Translation> result_;
  CandidateList candidates_;
};

}  // namespace rime

#endif  // RIME_MENU_H_
