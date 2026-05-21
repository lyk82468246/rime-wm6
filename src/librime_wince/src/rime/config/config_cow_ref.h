//
// rime/config/config_cow_ref.h -- WinCE-port mirror of upstream
// config_cow_ref.h.
//
// Copy-on-write ConfigItemRef chain used by ConfigData::TraverseWrite to
// edit the tree without mutating shared subtrees (rime's __patch / __merge
// semantics depend on this). The two specialisations (ConfigMap and
// ConfigList) provide map-key and "@index"-style addressing.
//
// Changes vs. upstream:
//   * `auto container = ...` -> explicit `an<T>`.
//   * NSDMI `bool copied_ = false;` -> default-ctor mem-initialiser list.
//   * `nullptr` (where returned as an<ConfigItem>) -> default-constructed
//     `an<ConfigItem>()`.
//   * `override` keyword removed.
//   * `New<ConfigCowRef<ConfigList>>` -- the C++11 right-angle-bracket
//     parsing isn't reliable on MSVC 9.0; spelled with a space:
//     `New<ConfigCowRef<ConfigList> >`.
//
#ifndef RIME_CONFIG_COW_REF_H_
#define RIME_CONFIG_COW_REF_H_

#include <rime/common.h>
#include <rime/config/config_data.h>
#include <rime/config/config_types.h>

namespace rime {

template <class T>
class ConfigCowRef : public ConfigItemRef {
 public:
  ConfigCowRef(an<ConfigItemRef> parent, string key)
      : ConfigItemRef(NULL), parent_(parent), key_(key), copied_(false) {}

  an<ConfigItem> GetItem() const {
    an<T> container = As<T>(**parent_);
    return container ? Read(container, key_) : an<ConfigItem>();
  }

  void SetItem(an<ConfigItem> item) {
    an<T> container = As<T>(**parent_);
    if (!copied_) {
      container = CopyOnWrite(container, key_);
      *parent_ = container;
      copied_ = true;
    }
    Write(container, key_, item);
  }

 protected:
  static an<T> CopyOnWrite(const an<T>& container, const string& key);
  static an<ConfigItem> Read(const an<T>& container, const string& key);
  static void Write(const an<T>& container,
                    const string& key,
                    an<ConfigItem> value);

  an<ConfigItemRef> parent_;
  string key_;
  bool copied_;
};

template <class T>
inline an<T> ConfigCowRef<T>::CopyOnWrite(const an<T>& container,
                                          const string& key) {
  if (!container) {
    return New<T>();
  }
  return New<T>(*container);
}

template <>
inline an<ConfigItem> ConfigCowRef<ConfigMap>::Read(const an<ConfigMap>& map,
                                                    const string& key) {
  return map->Get(key);
}

template <>
inline void ConfigCowRef<ConfigMap>::Write(const an<ConfigMap>& map,
                                           const string& key,
                                           an<ConfigItem> value) {
  map->Set(key, value);
}

template <>
inline an<ConfigItem> ConfigCowRef<ConfigList>::Read(const an<ConfigList>& list,
                                                     const string& key) {
  return list->GetAt(ConfigData::ResolveListIndex(list, key, true));
}

template <>
inline void ConfigCowRef<ConfigList>::Write(const an<ConfigList>& list,
                                            const string& key,
                                            an<ConfigItem> value) {
  list->SetAt(ConfigData::ResolveListIndex(list, key), value);
}

inline an<ConfigItemRef> Cow(an<ConfigItemRef> parent, string key) {
  if (ConfigData::IsListItemReference(key))
    return New<ConfigCowRef<ConfigList> >(parent, key);
  else
    return New<ConfigCowRef<ConfigMap> >(parent, key);
}

}  // namespace rime

#endif  // RIME_CONFIG_COW_REF_H_
