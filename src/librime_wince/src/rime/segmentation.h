//
// rime/segmentation.h -- WinCE-port mirror of upstream segmentation.h.
//
// Segment + Segmentation: the byte-range partitioning of the input buffer
// (one Segment per "atomic" piece that translators will compete to fill
// with candidates). Segmentation inherits std::vector<Segment>; segments
// are appended in left-to-right rounds.
//
// Changes vs. upstream:
//   * NSDMI on Segment members -> default-ctor mem-initialiser list.
//   * `Segment() = default;` -> explicit body that initialises POD fields.
//   * `std::any_of(... lambda)` in HasAnyTagIn -> classic for loop (no
//     C++11 lambdas under MSVC 9.0).
//
#ifndef RIME_SEGMENTATION_H_
#define RIME_SEGMENTATION_H_

#include <iosfwd>
#include <rime_api.h>
#include <rime/common.h>

namespace rime {

class Candidate;
class Menu;

struct Segment {
  enum Status {
    kVoid,
    kGuess,
    kSelected,
    kConfirmed,
  };
  Status status;
  size_t start;
  size_t end;
  size_t length;
  set<string> tags;
  an<Menu> menu;
  size_t selected_index;
  string prompt;

  Segment() : status(kVoid), start(0), end(0), length(0), selected_index(0) {}

  Segment(int start_pos, int end_pos)
      : status(kVoid),
        start(start_pos),
        end(end_pos),
        length(end_pos - start_pos),
        selected_index(0) {}

  void Clear() {
    status = kVoid;
    tags.clear();
    menu.reset();
    selected_index = 0;
    prompt.clear();
  }

  void Close();
  bool Reopen(size_t caret_pos);

  bool HasTag(const string& tag) const { return tags.find(tag) != tags.end(); }
  bool HasAnyTagIn(const vector<string>& other_tags) const {
    for (vector<string>::const_iterator it = other_tags.begin();
         it != other_tags.end(); ++it) {
      if (HasTag(*it)) return true;
    }
    return false;
  }

  an<Candidate> GetCandidateAt(size_t index) const;
  an<Candidate> GetSelectedCandidate() const;
};

class RIME_DLL Segmentation : public vector<Segment> {
 public:
  Segmentation();
  virtual ~Segmentation() {}
  void Reset(const string& input);
  void Reset(size_t num_segments);
  bool AddSegment(Segment segment);

  bool Forward();
  bool Trim();
  bool HasFinishedSegmentation() const;
  size_t GetCurrentStartPosition() const;
  size_t GetCurrentEndPosition() const;
  size_t GetCurrentSegmentLength() const;
  size_t GetConfirmedPosition() const;

  const string& input() const { return input_; }

 protected:
  string input_;
};

std::ostream& operator<<(std::ostream& out, const Segmentation& segmentation);

}  // namespace rime

#endif  // RIME_SEGMENTATION_H_
