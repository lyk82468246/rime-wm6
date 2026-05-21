//
// rime/ticket.h -- WinCE-port mirror of upstream ticket.h.
//
// Carries the four pieces of context every component factory needs at
// construction: which engine instance, which active schema, the optional
// configuration namespace, and the "klass" prescription string ("translator"
// vs. "translator@alphabet" etc.).
//
// Changes vs. upstream:
//   * `= default` -> empty body.
//   * NSDMI `Engine* engine = nullptr;` etc. -> default-ctor mem-init list.
//   * The Engine* construction overload is DECLARED here but DEFINED in
//     ticket.cc only after engine.h becomes available. Until then,
//     callers that need ticket.engine pre-populated must use the
//     Schema*/string overload or construct via aggregate init.
//
#ifndef RIME_TICKET_H_
#define RIME_TICKET_H_

#include <rime_api.h>
#include <rime/common.h>

namespace rime {

class Engine;
class Schema;

struct Ticket {
  Engine* engine;
  Schema* schema;
  string name_space;
  string klass;

  Ticket() : engine(NULL), schema(NULL) {}
  Ticket(Schema* s, const string& ns);
  // prescription: in the form of "klass" or "klass@alias"
  // where alias, if given, will override default name space
  RIME_DLL Ticket(Engine* e,
                  const string& ns = "",
                  const string& prescription = "");
};

}  // namespace rime

#endif  // RIME_TICKET_H_
