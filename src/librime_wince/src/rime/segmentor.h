//
// rime/segmentor.h -- WinCE-port mirror of upstream segmentor.h.
//
// Abstract Segmentor: pure virtual Proceed(Segmentation*). Subclasses
// (ascii_segmentor, matcher, abc_segmentor, ...) live in gear/.
//
// Change vs. upstream: `= default` -> empty body.
//
#ifndef RIME_SEGMENTOR_H_
#define RIME_SEGMENTOR_H_

#include <rime/component.h>
#include <rime/ticket.h>

namespace rime {

class Engine;
class Segmentation;

class Segmentor : public Class<Segmentor, const Ticket&> {
 public:
  explicit Segmentor(const Ticket& ticket)
      : engine_(ticket.engine), name_space_(ticket.name_space) {}
  virtual ~Segmentor() {}

  virtual bool Proceed(Segmentation* segmentation) = 0;

  string name_space() const { return name_space_; }

 protected:
  Engine* engine_;
  string name_space_;
};

}  // namespace rime

#endif  // RIME_SEGMENTOR_H_
