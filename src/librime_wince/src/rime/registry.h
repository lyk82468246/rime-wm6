//
// rime/registry.h -- WinCE-port mirror of upstream registry.h.
//
// Single shared map<string, ComponentBase*> used by Class<>::Require to
// look up factories. Process-wide singleton via Registry::instance().
//
// Changes vs. upstream:
//   * `using ComponentMap = ...` -> typedef.
//   * `Registry() = default;` -> empty body.
//
#ifndef RIME_REGISTRY_H_
#define RIME_REGISTRY_H_

#include <rime_api.h>
#include <rime/common.h>

namespace rime {

class ComponentBase;

class Registry {
 public:
  typedef map<string, ComponentBase*> ComponentMap;

  RIME_DLL ComponentBase* Find(const string& name);
  RIME_DLL void Register(const string& name, ComponentBase* component);
  RIME_DLL void Unregister(const string& name);
  void Clear();

  RIME_DLL static Registry& instance();

 private:
  Registry() {}

  ComponentMap map_;
};

}  // namespace rime

#endif  // RIME_REGISTRY_H_
