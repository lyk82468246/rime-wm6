//
// rime/engine.h -- WinCE-port mirror of upstream engine.h.
//
// Engine is the per-session IME state machine: owns a Schema + Context,
// drives the Process/Segment/Translate/Filter pipeline. Concrete subclass
// (ConcreteEngine) lives in engine.cc.
//
// Changes vs. upstream:
//   * `= default` -> empty body.
//   * NSDMI `Engine* active_engine_ = nullptr;` -> default-ctor mem-init list.
//   * `using CommitSink = ...` template alias -> typedef.
//   * `set_active_engine(Engine* engine = nullptr)` -> default arg `= NULL`.
//   * Inline default bodies for ProcessKey/ApplySchema/Compose moved to
//     engine.cc. MSVC 9.0 emits a bogus C2027 on `(void)key_event` when
//     KeyEvent is only forward-declared in the inline body's context.
//
#ifndef RIME_ENGINE_H_
#define RIME_ENGINE_H_

#include <rime_api.h>
#include <rime/common.h>
#include <rime/messenger.h>

namespace rime {

class KeyEvent;
class Schema;
class Context;

class Engine : public Messenger {
 public:
  typedef signal<void(const string& commit_text)> CommitSink;

  virtual ~Engine();
  virtual bool ProcessKey(const KeyEvent& key_event);
  virtual void ApplySchema(Schema* schema);
  virtual void CommitText(string text);
  virtual void Compose(Context* ctx);

  Schema* schema() const { return schema_.get(); }
  Context* context() const { return context_.get(); }
  CommitSink& sink() { return sink_; }

  Engine* active_engine() { return active_engine_ ? active_engine_ : this; }
  void set_active_engine(Engine* engine = NULL) { active_engine_ = engine; }

  RIME_DLL static Engine* Create();

 protected:
  Engine();

  the<Schema> schema_;
  the<Context> context_;
  CommitSink sink_;
  Engine* active_engine_;
};

}  // namespace rime

#endif  // RIME_ENGINE_H_
