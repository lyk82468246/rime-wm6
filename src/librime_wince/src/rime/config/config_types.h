//
// rime/config/config_types.h -- WinCE-port mirror of upstream config_types.h.
//
// The runtime tree behind a YAML schema: ConfigItem (abstract) with three
// concrete kinds -- ConfigValue (scalar string), ConfigList (vector), and
// ConfigMap (string->item map). Plus ConfigItemRef* for mutable indexed
// access (`cfg["foo"][0] = 42` style code).
//
// Changes vs. upstream:
//   * Removed `<type_traits>` dep. Upstream uses std::is_convertible +
//     std::true_type/std::false_type to dispatch a templated operator=()
//     between "x is already an<ConfigItem>" and "x is a scalar". C++03 has
//     no <type_traits>; we replace SFINAE with plain function-overload
//     dispatch in AsConfigItem(), which is simpler anyway.
//   * `using Sequence = ...` / `using Iterator = ...` -> typedef.
//   * `= default` -> empty body.
//   * NSDMI `type_ = kNull;` -> default-ctor mem-initialiser list.
//   * `bool empty() const override` -> `bool empty() const` (no override
//     keyword in C++03; virtual dispatch still works via signature match).
//   * `using ConfigItemRef::operator=;` declarations in the derived
//     *EntryRef classes replaced with an explicit templated forwarding
//     operator=(). C++03 syntax permits `using` of a base operator=, but
//     MSVC 9.0 has bugs around it; the forwarding template is reliable.
//
#ifndef RIME_CONFIG_TYPES_H_
#define RIME_CONFIG_TYPES_H_

#include <rime_api.h>
#include <rime/common.h>

namespace rime {

// config item base class
class ConfigItem {
 public:
  enum ValueType { kNull, kScalar, kList, kMap };

  ConfigItem() : type_(kNull) {}
  virtual ~ConfigItem() {}

  ValueType type() const { return type_; }

  virtual bool empty() const { return type_ == kNull; }

 protected:
  ConfigItem(ValueType type) : type_(type) {}

  ValueType type_;
};

class ConfigValue : public ConfigItem {
 public:
  ConfigValue() : ConfigItem(kScalar) {}
  RIME_DLL ConfigValue(bool value);
  RIME_DLL ConfigValue(int value);
  RIME_DLL ConfigValue(double value);
  RIME_DLL ConfigValue(const char* value);
  RIME_DLL ConfigValue(const string& value);

  // scalar value accessors
  bool GetBool(bool* value) const;
  RIME_DLL bool GetInt(int* value) const;
  bool GetDouble(double* value) const;
  RIME_DLL bool GetString(string* value) const;
  bool SetBool(bool value);
  bool SetInt(int value);
  bool SetDouble(double value);
  bool SetString(const char* value);
  bool SetString(const string& value);

  const string& str() const { return value_; }

  bool empty() const { return value_.empty(); }

 protected:
  string value_;
};

class ConfigList : public ConfigItem {
 public:
  typedef vector<of<ConfigItem> > Sequence;
  typedef Sequence::iterator Iterator;

  ConfigList() : ConfigItem(kList) {}
  RIME_DLL an<ConfigItem> GetAt(size_t i) const;
  RIME_DLL an<ConfigValue> GetValueAt(size_t i) const;
  RIME_DLL bool SetAt(size_t i, an<ConfigItem> element);
  bool Insert(size_t i, an<ConfigItem> element);
  RIME_DLL bool Append(an<ConfigItem> element);
  bool Resize(size_t size);
  RIME_DLL bool Clear();
  RIME_DLL size_t size() const;

  Iterator begin();
  Iterator end();

  bool empty() const { return seq_.empty(); }

 protected:
  Sequence seq_;
};

// limitation: map keys have to be strings, preferably alphanumeric
class ConfigMap : public ConfigItem {
 public:
  typedef map<string, an<ConfigItem> > Map;
  typedef Map::iterator Iterator;

  ConfigMap() : ConfigItem(kMap) {}
  RIME_DLL bool HasKey(const string& key) const;
  RIME_DLL an<ConfigItem> Get(const string& key) const;
  RIME_DLL an<ConfigValue> GetValue(const string& key) const;
  RIME_DLL bool Set(const string& key, an<ConfigItem> element);
  bool Clear();

  Iterator begin();
  Iterator end();

  bool empty() const { return map_.empty(); }

 protected:
  Map map_;
};

// AsConfigItem -- coerce assignable RHS into an<ConfigItem>.
//
// Upstream uses SFINAE on std::is_convertible<T, an<ConfigItem>>. We dispatch
// with plain overloads, which match C++03 idioms and avoid <type_traits>:
//   * an<ConfigItem> / an<ConfigValue> / an<ConfigList> / an<ConfigMap> ->
//     bind to the typed overloads, return as-is.
//   * everything else (bool, int, double, const char*, string) -> falls
//     into the template, which wraps it in a New<ConfigValue>.
inline an<ConfigItem> AsConfigItem(const an<ConfigItem>& x) { return x; }
inline an<ConfigItem> AsConfigItem(const an<ConfigValue>& x) { return x; }
inline an<ConfigItem> AsConfigItem(const an<ConfigList>& x) { return x; }
inline an<ConfigItem> AsConfigItem(const an<ConfigMap>& x) { return x; }
template <class T>
inline an<ConfigItem> AsConfigItem(const T& x) {
  return New<ConfigValue>(x);
}

class ConfigData;
class ConfigListEntryRef;
class ConfigMapEntryRef;

class ConfigItemRef {
 public:
  ConfigItemRef(ConfigData* data) : data_(data) {}
  virtual ~ConfigItemRef() {}
  operator an<ConfigItem>() const { return GetItem(); }
  an<ConfigItem> operator*() const { return GetItem(); }
  template <class T>
  ConfigItemRef& operator=(const T& x) {
    SetItem(AsConfigItem(x));
    return *this;
  }
  ConfigListEntryRef operator[](size_t index);
  ConfigMapEntryRef operator[](const string& key);

  RIME_DLL bool IsNull() const;
  bool IsValue() const;
  RIME_DLL bool IsList() const;
  bool IsMap() const;

  RIME_DLL bool ToBool() const;
  RIME_DLL int ToInt() const;
  double ToDouble() const;
  RIME_DLL string ToString() const;

  RIME_DLL an<ConfigList> AsList();
  RIME_DLL an<ConfigMap> AsMap();
  RIME_DLL void Clear();

  // list
  RIME_DLL bool Append(an<ConfigItem> item);
  RIME_DLL size_t size() const;
  // map
  RIME_DLL bool HasKey(const string& key) const;

  RIME_DLL bool modified() const;
  RIME_DLL void set_modified();

 protected:
  virtual an<ConfigItem> GetItem() const = 0;
  virtual void SetItem(an<ConfigItem> item) = 0;

  ConfigData* data_;
};

class ConfigListEntryRef : public ConfigItemRef {
 public:
  ConfigListEntryRef(ConfigData* data, an<ConfigList> list, size_t index)
      : ConfigItemRef(data), list_(list), index_(index) {}
  // C++03 forwarding for the templated base operator=. MSVC 9.0 handles
  // `using ConfigItemRef::operator=;` poorly, so we explicitly forward
  // here. Returning the base type is fine -- callers (`cfg[i] = v;`)
  // discard the result.
  template <class T>
  ConfigItemRef& operator=(const T& x) {
    return ConfigItemRef::operator=(x);
  }

 protected:
  an<ConfigItem> GetItem() const { return list_->GetAt(index_); }
  void SetItem(an<ConfigItem> item) {
    list_->SetAt(index_, item);
    set_modified();
  }

 private:
  an<ConfigList> list_;
  size_t index_;
};

class ConfigMapEntryRef : public ConfigItemRef {
 public:
  ConfigMapEntryRef(ConfigData* data, an<ConfigMap> map, const string& key)
      : ConfigItemRef(data), map_(map), key_(key) {}
  template <class T>
  ConfigItemRef& operator=(const T& x) {
    return ConfigItemRef::operator=(x);
  }

 protected:
  an<ConfigItem> GetItem() const { return map_->Get(key_); }
  void SetItem(an<ConfigItem> item) {
    map_->Set(key_, item);
    set_modified();
  }

 private:
  an<ConfigMap> map_;
  string key_;
};

inline ConfigListEntryRef ConfigItemRef::operator[](size_t index) {
  return ConfigListEntryRef(data_, AsList(), index);
}

inline ConfigMapEntryRef ConfigItemRef::operator[](const string& key) {
  return ConfigMapEntryRef(data_, AsMap(), key);
}

}  // namespace rime

#endif  // RIME_CONFIG_TYPES_H_
