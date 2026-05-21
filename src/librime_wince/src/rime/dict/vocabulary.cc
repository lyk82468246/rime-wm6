//
// rime/dict/vocabulary.cc -- WinCE-port mirror of upstream vocabulary.cc.
//
// Implementation of Code / ShortDictEntry / DictEntry comparators,
// Vocabulary::LocateEntries / SortHomophones, and the chained
// DictEntryFilter combinator.
//
// Changes vs. upstream:
//   * Range-for `for (SyllableId s : *this)` -> iterator loop.
//   * `std::begin(c)` / `std::end(c)` -> `c.begin()` / `c.end()`
//     (MSVC 9.0's Dinkumware STL ships with C++03 only -- no free-function
//     begin/end on standard containers).
//   * `auto i(std::begin(c)+start)` -> explicit iterator type.
//   * Lambda inside AddFilter -> named functor `ChainedFilter` capturing
//     the previous and current filters by value.
//   * `std::move(filter_)` -> swap; no move semantics under C++03.
//
#include <algorithm>
#include <sstream>
#include <rime/dict/vocabulary.h>

namespace rime {

bool Code::operator<(const Code& other) const {
  if (size() != other.size())
    return size() < other.size();
  for (size_t i = 0; i < size(); ++i) {
    if (at(i) != other.at(i))
      return at(i) < other.at(i);
  }
  return false;
}

bool Code::operator==(const Code& other) const {
  if (size() != other.size())
    return false;
  for (size_t i = 0; i < size(); ++i) {
    if (at(i) != other.at(i))
      return false;
  }
  return true;
}

void Code::CreateIndex(Code* index_code) {
  if (!index_code)
    return;
  size_t index_code_size = Code::kIndexCodeMaxLength;
  if (size() <= index_code_size) {
    index_code_size = size();
  }
  index_code->resize(index_code_size);
  std::copy(begin(), begin() + index_code_size, index_code->begin());
}

string Code::ToString() const {
  std::stringstream stream;
  bool first = true;
  for (const_iterator it = begin(); it != end(); ++it) {
    if (first) {
      first = false;
    } else {
      stream << ",";
    }
    stream << *it;
  }
  return stream.str();
}

bool ShortDictEntry::operator<(const ShortDictEntry& other) const {
  // Sort different entries sharing the same code by weight desc.
  if (weight != other.weight)
    return weight > other.weight;
  return false;  // upstream: "reduce carbon emission" -- text comparison cut.
}

bool DictEntry::operator<(const DictEntry& other) const {
  if (weight != other.weight)
    return weight > other.weight;
  return false;
}

// Dereferencing comparator: sorts container<X*> / container<an<X> > by *p.
template <class T>
inline bool dereference_less(const T& a, const T& b) {
  return *a < *b;
}

void ShortDictEntryList::Sort() {
  std::stable_sort(begin(), end(),
                   dereference_less<value_type>);
}

void ShortDictEntryList::SortRange(size_t start, size_t count) {
  if (start >= size())
    return;
  iterator i = begin() + start;
  iterator j = (start + count >= size()) ? end() : (i + count);
  std::stable_sort(i, j, dereference_less<value_type>);
}

void DictEntryList::Sort() {
  std::stable_sort(begin(), end(),
                   dereference_less<value_type>);
}

void DictEntryList::SortRange(size_t start, size_t count) {
  if (start >= size())
    return;
  iterator i = begin() + start;
  iterator j = (start + count >= size()) ? end() : (i + count);
  std::stable_sort(i, j, dereference_less<value_type>);
}

namespace {
// Replaces upstream's `[prev, curr](e) -> prev(e) && curr(e)` lambda.
struct ChainedFilter {
  DictEntryFilter prev;
  DictEntryFilter curr;
  ChainedFilter(const DictEntryFilter& p, const DictEntryFilter& c)
      : prev(p), curr(c) {}
  bool operator()(an<DictEntry> e) const {
    return prev(e) && curr(e);
  }
};
}  // namespace

void DictEntryFilterBinder::AddFilter(DictEntryFilter filter) {
  if (!filter_) {
    filter_.swap(filter);
  } else {
    DictEntryFilter previous_filter;
    previous_filter.swap(filter_);  // std::move replacement
    filter_ = ChainedFilter(previous_filter, filter);
  }
}

ShortDictEntryList* Vocabulary::LocateEntries(const Code& code) {
  Vocabulary* v = this;
  size_t n = code.size();
  for (size_t i = 0; i < n; ++i) {
    int key = -1;
    if (i < Code::kIndexCodeMaxLength)
      key = code[i];
    VocabularyPage& page = (*v)[key];
    if (i == n - 1 || i == Code::kIndexCodeMaxLength) {
      return &page.entries;
    } else {
      if (!page.next_level) {
        page.next_level = New<Vocabulary>();
      }
      v = page.next_level.get();
    }
  }
  return NULL;
}

void Vocabulary::SortHomophones() {
  for (iterator it = begin(); it != end(); ++it) {
    VocabularyPage& page = it->second;
    page.entries.Sort();
    if (page.next_level)
      page.next_level->SortHomophones();
  }
}

}  // namespace rime
