//
// rime/composition.h -- WinCE-port mirror of upstream composition.h.
//
// Composition extends Segmentation with preedit / commit / debug rendering
// of the current segment chain.
//
// Changes vs. upstream:
//   * Preedit NSDMI -> default-ctor mem-init list.
//   * `Composition() = default;` -> empty body.
//
#ifndef RIME_COMPOSITION_H_
#define RIME_COMPOSITION_H_

#include <rime/segmentation.h>

namespace rime {

struct Preedit {
  string text;
  size_t caret_pos;
  size_t sel_start;
  size_t sel_end;

  Preedit() : caret_pos(0), sel_start(0), sel_end(0) {}
};

class Composition : public Segmentation {
 public:
  Composition() {}

  bool HasFinishedComposition() const;
  Preedit GetPreedit(const string& full_input,
                     size_t caret_pos,
                     const string& caret) const;
  string GetPrompt() const;
  string GetCommitText() const;
  string GetScriptText(bool keep_selection = true) const;
  RIME_DLL string GetDebugText() const;
  // Returns text of the last segment before the given position.
  string GetTextBefore(size_t pos) const;
};

}  // namespace rime

#endif  // RIME_COMPOSITION_H_
