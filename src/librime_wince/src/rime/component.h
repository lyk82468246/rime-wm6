//
// rime/component.h -- WinCE-port mirror of upstream component.h.
//
// The component framework is tiny: ComponentBase is just an abstract
// virtual destructor; Class<T, Arg> exposes a typed factory interface
// pulled out of the Registry; Component<T> is the default "new T(arg)"
// factory implementation.
//
// Changes vs. upstream:
//   * `using Initializer = Arg;` -> typedef.
//   * `= default` -> empty body.
//
// This is the file Config used to inherit `Class<Config, const string&>`
// from. We previously trimmed Config's base list as part of the MVP
// shortcut; reattaching it is a follow-up (config_component.h will get
// the base restored once we wire up the ConfigComponent/Loader path).
//
#ifndef RIME_COMPONENT_H_
#define RIME_COMPONENT_H_

#include <rime/registry.h>

namespace rime {

class ComponentBase {
 public:
  ComponentBase() {}
  virtual ~ComponentBase() {}
};

template <class T, class Arg>
struct Class {
  typedef Arg Initializer;

  class Component : virtual public ComponentBase {
   public:
    virtual T* Create(Initializer arg) = 0;
  };

  static Component* Require(const string& name) {
    return dynamic_cast<Component*>(Registry::instance().Find(name));
  }
};

template <class T>
struct Component : public T::Component {
 public:
  T* Create(typename T::Initializer arg) { return new T(arg); }
};

}  // namespace rime

#endif  // RIME_COMPONENT_H_
