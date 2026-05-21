//
// rime/filter.h -- WinCE-port mirror of upstream filter.h.
//
// Abstract Filter interface: takes an upstream Translation and returns a
// (possibly different) Translation. Subclasses live in gear/ (Uniquifier,
// Simplifier, etc.). Pure header; no .cc to port.
//
// Changes vs. upstream:
//   * `= default` -> empty body.
//
#ifndef RIME_FILTER_H_
#define RIME_FILTER_H_

#include <rime/candidate.h>
#include <rime/common.h>
#include <rime/component.h>
#include <rime/ticket.h>

namespace rime {

class Engine;
struct Segment;
class Translation;

class Filter : public Class<Filter, const Ticket&> {
 public:
  explicit Filter(const Ticket& ticket)
      : engine_(ticket.engine), name_space_(ticket.name_space) {}
  virtual ~Filter() {}

  virtual an<Translation> Apply(an<Translation> translation,
                                CandidateList* candidates) = 0;

  virtual bool AppliesToSegment(Segment* segment) { (void)segment; return true; }

  string name_space() const { return name_space_; }

 protected:
  Engine* engine_;
  string name_space_;
};

}  // namespace rime

#endif  // RIME_FILTER_H_
