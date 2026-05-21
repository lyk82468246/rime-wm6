//
// rime/key_event.h -- WinCE-port mirror of upstream key_event.h.
//
// Changes vs. upstream:
//   * CJK comments translated to ASCII (file-encoding rule).
//   * `KeyEvent() = default;` -> empty body.
//   * NSDMI `int keycode_ = 0;` -> default-ctor mem-initialiser list.
//   * `KeySequence() = default;` -> empty body.
//
#ifndef RIME_KEY_EVENT_H_
#define RIME_KEY_EVENT_H_

#include <iostream>
#include <rime/common.h>
#include <rime/key_table.h>

namespace rime {

class KeyEvent {
 public:
  // C++03: no NSDMI -- members initialised in the mem-initialiser list.
  KeyEvent() : keycode_(0), modifier_(0) {}
  KeyEvent(int keycode, int modifier)
      : keycode_(keycode), modifier_(modifier) {}
  RIME_DLL KeyEvent(const string& repr);

  int keycode() const { return keycode_; }
  void keycode(int value) { keycode_ = value; }
  int modifier() const { return modifier_; }
  void modifier(int value) { modifier_ = value; }

  bool shift() const { return (modifier_ & kShiftMask) != 0; }
  bool ctrl() const { return (modifier_ & kControlMask) != 0; }
  bool alt() const { return (modifier_ & kAltMask) != 0; }
  bool caps() const { return (modifier_ & kLockMask) != 0; }
  bool super() const { return (modifier_ & kSuperMask) != 0; }
  bool release() const { return (modifier_ & kReleaseMask) != 0; }

  // Stringify a key as "modifier+name". Keys lacking a registered name fall
  // back to 4- or 6-digit hex, e.g. "0x12ab", "0xfffffe".
  RIME_DLL string repr() const;

  // Parse the textual representation produced by repr().
  RIME_DLL bool Parse(const string& repr);

  bool operator==(const KeyEvent& other) const {
    return keycode_ == other.keycode_ && modifier_ == other.modifier_;
  }

  bool operator<(const KeyEvent& other) const {
    if (keycode_ != other.keycode_)
      return keycode_ < other.keycode_;
    return modifier_ < other.modifier_;
  }

 private:
  int keycode_;
  int modifier_;
};

// A sequence of key events. Inherits std::vector<KeyEvent> for storage.
class KeySequence : public vector<KeyEvent> {
 public:
  KeySequence() {}
  RIME_DLL KeySequence(const string& repr);

  // Stringify the sequence. Non-printable keys (or modified keys) are
  // emitted as `{name}`; printable single-byte keys are emitted bare.
  RIME_DLL string repr() const;

  // Parse a sequence description produced by repr().
  RIME_DLL bool Parse(const string& repr);
};

inline std::ostream& operator<<(std::ostream& out, const KeyEvent& key_event) {
  out << key_event.repr();
  return out;
}

inline std::ostream& operator<<(std::ostream& out, const KeySequence& key_seq) {
  out << key_seq.repr();
  return out;
}

}  // namespace rime

#endif  // RIME_KEY_EVENT_H_
