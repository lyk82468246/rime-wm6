//
// rime/ticket.cc -- WinCE-port partial mirror of upstream ticket.cc.
//
// Only the Schema*-based constructor is implemented here. The Engine*
// constructor calls e->schema(), which forces a hard dep on rime/engine.h
// -- not yet ported. The engine-aware ctor is provided as a no-op stub
// that sets schema = NULL until engine.h lands; existing callers that
// need a populated `schema` field should use the Schema* overload or
// fill the struct directly.
//
// When engine.cc is mirrored, replace the body of the second ctor with
// the upstream implementation (one line: `schema = e ? e->schema() : NULL`).
//
#include <rime/ticket.h>

namespace rime {

Ticket::Ticket(Schema* s, const string& ns)
    : engine(NULL), schema(s), name_space(ns) {}

Ticket::Ticket(Engine* e, const string& ns, const string& prescription)
    : engine(e), schema(NULL), name_space(ns), klass(prescription) {
  // NOTE: schema would normally come from e->schema(); deferred until
  // engine.h is ported.
  size_t separator = klass.find('@');
  if (separator != string::npos) {
    name_space = klass.substr(separator + 1);
    klass.resize(separator);
  }
}

}  // namespace rime
