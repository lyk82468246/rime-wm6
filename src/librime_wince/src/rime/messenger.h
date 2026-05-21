//
// rime/messenger.h -- WinCE-port mirror of upstream messenger.h.
//
// One-method mixin: a `signal<void(type, value)>` for components to push
// out-of-band status messages. Engine inherits Messenger to fan out
// "option"/"property"/"schema" notifications.
//
// Change vs. upstream: `using MessageSink = ...` template alias -> typedef.
//
#ifndef RIME_MESSENGER_H_
#define RIME_MESSENGER_H_

#include <rime/common.h>

namespace rime {

class Messenger {
 public:
  typedef signal<void(const string& message_type, const string& message_value)>
      MessageSink;

  MessageSink& message_sink() { return message_sink_; }

 protected:
  MessageSink message_sink_;
};

}  // namespace rime

#endif  // RIME_MESSENGER_H_
