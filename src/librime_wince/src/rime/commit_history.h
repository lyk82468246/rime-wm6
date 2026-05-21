//
// rime/commit_history.h -- WinCE-port mirror of upstream commit_history.h.
//
// Byte-equivalent to upstream; the file is already C++03-clean (no
// `= default`, no NSDMI, no range-for, no auto). Mirrored here only
// because we have a parallel tree.
//
#ifndef RIME_COMMIT_HISTORY_H_
#define RIME_COMMIT_HISTORY_H_

#include <rime/common.h>

namespace rime {

struct CommitRecord {
  string type;
  string text;
  CommitRecord(const string& a_type, const string& a_text)
      : type(a_type), text(a_text) {}
  CommitRecord(int keycode) : type("thru"), text(1, keycode) {}
};

class KeyEvent;
class Composition;

class CommitHistory : public list<CommitRecord> {
 public:
  static const size_t kMaxRecords = 20;
  void Push(const CommitRecord& record);
  void Push(const KeyEvent& key_event);
  void Push(const Composition& composition, const string& input);
  string repr() const;
  string latest_text() const { return empty() ? string() : back().text; }
};

}  // namespace rime

#endif  // RIME_COMMIT_HISTORY_H_
