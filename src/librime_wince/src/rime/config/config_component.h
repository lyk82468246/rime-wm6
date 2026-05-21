//
// rime/config/config_component.h -- WinCE-port mirror of upstream
// config_component.h, drastically trimmed.
//
// Upstream Config inherits from `Class<Config, const string&>` (the rime
// Component registry hook) and ships a full ConfigComponentBase /
// ConfigComponent<Loader, Provider> / ConfigLoader / ConfigBuilder /
// ConfigResourceProvider stack that wires named-file -> ConfigData
// loading through Service / ResourceResolver / ConfigCompiler plugin
// chains.
//
// None of that infrastructure is here yet (Service, ResourceResolver,
// component.h, config_compiler -- all unported for MVP). So this header
// keeps ONLY the Config class itself, with its full Get/Set/Is/Save/Load
// API surface. Config can be constructed directly from a hand-built
// ConfigData; engine code that previously asked the registry for a
// "schema:luna_pinyin" config will be served by a small factory
// (LoadHardcodedConfig) until yaml-cpp and the registry come back.
//
// Changes vs. upstream:
//   * Removed `Class<Config, const string&>` base. Config no longer
//     participates in the Component registry.
//   * Removed `<type_traits>` include (was only needed by the registry
//     template machinery that's gone).
//   * Removed `<iostream>` include in favor of `<iosfwd>` for the
//     stream-parameter signatures.
//   * Removed `<rime/component.h>` and `<rime/resource.h>` includes.
//   * Removed `ConfigResourceProvider`, `DeployedConfigResourceProvider`,
//     `UserConfigResourceProvider`, `ConfigComponentBase`,
//     `ConfigComponent<>`, `ConfigLoader`, `ConfigBuilder`.
//   * `using ConfigItemRef::operator=;` -> explicit forwarding template
//     (same fix used in ConfigListEntryRef / ConfigMapEntryRef).
//
#ifndef RIME_CONFIG_COMPONENT_H_
#define RIME_CONFIG_COMPONENT_H_

#include <iosfwd>
#include <rime/common.h>
#include <rime/config/config_types.h>

namespace rime {

class ConfigData;

class Config : public ConfigItemRef {
 public:
  // CAVEAT: Config instances created without argument own a private
  // ConfigData; they are NOT shared via any registry in the MVP build.
  RIME_DLL Config();
  RIME_DLL virtual ~Config();

  // Wrap an externally-managed ConfigData (e.g. one returned from a
  // future shared-cache loader).
  explicit Config(an<ConfigData> data);

  // returns whether actually saved to file.
  bool Save();
  bool LoadFromStream(std::istream& stream);
  bool SaveToStream(std::ostream& stream);
  RIME_DLL bool LoadFromFile(const path& file_path);
  RIME_DLL bool SaveToFile(const path& file_path);

  // access a tree node of a particular type with "path/to/node"
  RIME_DLL bool IsNull(const string& path);
  bool IsValue(const string& path);
  RIME_DLL bool IsList(const string& path);
  RIME_DLL bool IsMap(const string& path);
  RIME_DLL bool GetBool(const string& path, bool* value);
  RIME_DLL bool GetInt(const string& path, int* value);
  RIME_DLL bool GetDouble(const string& path, double* value);
  RIME_DLL bool GetString(const string& path, string* value);
  RIME_DLL size_t GetListSize(const string& path);

  an<ConfigItem> GetItem(const string& path);
  an<ConfigValue> GetValue(const string& path);
  RIME_DLL an<ConfigList> GetList(const string& path);
  RIME_DLL an<ConfigMap> GetMap(const string& path);

  // setters
  bool SetBool(const string& path, bool value);
  RIME_DLL bool SetInt(const string& path, int value);
  bool SetDouble(const string& path, double value);
  RIME_DLL bool SetString(const string& path, const char* value);
  bool SetString(const string& path, const string& value);
  // setter for adding or replacing items in the tree
  RIME_DLL bool SetItem(const string& path, an<ConfigItem> item);

  // Forwarding for the templated base operator= (see config_types.h note
  // on MSVC 9.0's bug with `using base::operator=`).
  template <class T>
  ConfigItemRef& operator=(const T& x) {
    return ConfigItemRef::operator=(x);
  }

 protected:
  an<ConfigItem> GetItem() const;
  void SetItem(an<ConfigItem> item);

  an<ConfigData> data_;
};

}  // namespace rime

#endif  // RIME_CONFIG_COMPONENT_H_
