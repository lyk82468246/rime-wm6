//
// wince_compat/regex.h -- minimal C++03 regex engine, boost::regex API subset.
//
// What's here: a Thompson-NFA implementation (Pike VM) covering exactly the
// regex features that librime touches. No backtracking, so no catastrophic
// patterns; worst case is O(n * m).
//
// Supported pattern features (audit confirmed sufficient for rime schemas):
//   * Literal bytes and escapes:  a   \.   \\   \n   \t   \r   \(   \)
//                                 \d  \D   \w   \W   \s   \S
//   * Wildcards / anchors:        .   ^    $
//   * Character classes:          [abc]  [^abc]  [a-z]  [^a-zA-Z0-9_]
//   * Greedy quantifiers:         *   +    ?
//   * Capture groups:             (...)        capture index starts at 1
//   * Alternation:                a|b|c
//
// Not supported (would have to add if a future schema demands it):
//   * Backreferences in pattern: \1 \2 ...
//   * Counted quantifiers:       {n}  {n,m}
//   * Lazy quantifiers:          *?  +?  ??
//   * Non-capturing groups:      (?:...)
//   * Lookaround:                (?=...)  (?!...)  (?<=...)
//   * POSIX classes:             [[:alpha:]]
//   * Word boundaries:           \b  \B
//
// Replacement format string accepts $0 (whole match) and $1..$9 (capture
// groups). Use `$$` to insert a literal `$`. All other characters are
// copied verbatim.
//
// Operates on byte sequences (matches std::string semantics). UTF-8 input
// works at the byte level: a literal Chinese character in the pattern matches
// its UTF-8 byte sequence in the input. Character classes operate on single
// bytes, so don't put multi-byte UTF-8 characters inside `[...]`.
//
#ifndef WINCE_COMPAT_REGEX_H_
#define WINCE_COMPAT_REGEX_H_

#include <exception>
#include <string>
#include <vector>

#include "shared_ptr.h"

namespace wince {

class regex_error : public std::exception {
 public:
  explicit regex_error(const char* msg) : msg_(msg) {}
  explicit regex_error(const std::string& msg) : msg_(msg) {}
  virtual ~regex_error() throw() {}
  virtual const char* what() const throw() { return msg_.c_str(); }
 private:
  std::string msg_;
};

namespace re_detail {
struct prog;  // opaque to clients; full definition lives in regex.cc
}  // namespace re_detail

class regex {
 public:
  regex();
  explicit regex(const char* pattern);
  explicit regex(const std::string& pattern);

  void assign(const char* pattern);
  void assign(const std::string& pattern);

  // Access compiled program. Returns NULL on a default-constructed regex.
  const re_detail::prog* program() const { return prog_.get(); }

 private:
  shared_ptr<re_detail::prog> prog_;
};

class smatch {
 public:
  smatch() : matched_(false) {}

  bool ready() const { return matched_; }
  bool empty() const { return !matched_; }
  size_t size() const;  // number of capture groups + 1 (the whole match)

  // n == 0 -> whole match; n >= 1 -> capture group n.
  std::string str(size_t n = 0) const;
  std::string operator[](size_t n) const { return str(n); }

  // Boost-compatible affix accessors.
  std::string prefix() const;
  std::string suffix() const;

  // Position helpers (byte indices into the original subject).
  int position(size_t n = 0) const;
  int length(size_t n = 0) const;

  // For internal use by regex_match / regex_search to populate the match.
  void assign(const std::string& subject,
              size_t whole_start,
              size_t whole_end,
              const std::vector<int>& captures);
  void clear();

 private:
  std::string subject_;
  // captures_ is laid out as [g0_start, g0_end, g1_start, g1_end, ...].
  // -1 means "no value recorded". g0 == whole match, present iff matched_.
  std::vector<int> captures_;
  bool matched_;
};

// Anchored full-string match.
bool regex_match(const std::string& s, const regex& re);
bool regex_match(const std::string& s, smatch& m, const regex& re);

// Unanchored: find the leftmost substring that matches the pattern.
bool regex_search(const std::string& s, const regex& re);
bool regex_search(const std::string& s, smatch& m, const regex& re);

// Replace every non-overlapping match left-to-right; `fmt` may reference
// capture groups with $0..$9 (use $$ for a literal $).
std::string regex_replace(const std::string& s,
                          const regex& re,
                          const std::string& fmt);

}  // namespace wince

#endif  // WINCE_COMPAT_REGEX_H_
