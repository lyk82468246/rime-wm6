//
// rime/processor.h -- WinCE-port mirror of upstream processor.h.
//
// Abstract Processor interface: takes a KeyEvent and tells the engine
// whether the input was accepted, rejected, or skipped. Subclasses
// (ascii_composer, speller, selector, etc.) live in gear/.
//
// Change vs. upstream: `= default` -> empty body.
//
#ifndef RIME_PROCESSOR_H_
#define RIME_PROCESSOR_H_

#include <rime/component.h>
#include <rime/ticket.h>

namespace rime {

class Engine;
class KeyEvent;

enum ProcessResult {
  kRejected,  // do the OS default processing
  kAccepted,  // consume it
  kNoop,      // leave it to other processors
};

class Processor : public Class<Processor, const Ticket&> {
 public:
  explicit Processor(const Ticket& ticket)
      : engine_(ticket.engine), name_space_(ticket.name_space) {}
  virtual ~Processor() {}

  virtual ProcessResult ProcessKeyEvent(const KeyEvent& key_event) {
    (void)key_event;
    return kNoop;
  }

  string name_space() const { return name_space_; }

 protected:
  Engine* engine_;
  string name_space_;
};

}  // namespace rime

#endif  // RIME_PROCESSOR_H_
