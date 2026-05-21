//
// rime/commit_history.cc -- WinCE-port mirror of upstream commit_history.cc.
//
// Changes vs. upstream:
//   * `auto cand = seg.GetSelectedCandidate()` -> explicit an<Candidate>.
//   * Range-for over composition/list -> classic iterator loops.
//   * Brace-init `Push({type, text})` (C++11) -> `Push(CommitRecord(type, text))`.
//
#include <rime/candidate.h>
#include <rime/commit_history.h>
#include <rime/composition.h>
#include <rime/key_event.h>

namespace rime {

void CommitHistory::Push(const CommitRecord& record) {
  push_back(record);
  if (!empty() && size() > kMaxRecords)
    pop_front();
}

void CommitHistory::Push(const KeyEvent& key_event) {
  if (key_event.modifier() == 0) {
    if (key_event.keycode() == XK_BackSpace ||
        key_event.keycode() == XK_Return) {
      clear();
    } else if (key_event.keycode() >= 0x20 && key_event.keycode() <= 0x7e) {
      // printable ascii character
      Push(CommitRecord(key_event.keycode()));
    }
  }
}

void CommitHistory::Push(const Composition& composition, const string& input) {
  CommitRecord* last = NULL;
  size_t end = 0;
  for (Composition::const_iterator it = composition.begin();
       it != composition.end(); ++it) {
    const Segment& seg = *it;
    an<Candidate> cand = seg.GetSelectedCandidate();
    if (cand) {
      if (last && last->type == cand->type()) {
        // join adjacent text of same type
        last->text += cand->text();
      } else {
        // new record
        Push(CommitRecord(cand->type(), cand->text()));
        last = &back();
      }
      if (seg.status >= Segment::kConfirmed) {
        // terminate a record by confirmation
        last = NULL;
      }
      end = cand->end();
    } else {
      // no translation for the segment
      Push(CommitRecord("raw", input.substr(seg.start, seg.end - seg.start)));
      end = seg.end;
    }
  }
  if (input.length() > end) {
    Push(CommitRecord("raw", input.substr(end)));
  }
}

string CommitHistory::repr() const {
  string result;
  for (CommitHistory::const_iterator it = begin(); it != end(); ++it) {
    result += "[" + it->type + "]" + it->text;
  }
  return result;
}

}  // namespace rime
