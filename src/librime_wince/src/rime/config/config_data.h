//
// rime/config/config_data.h -- WinCE-port mirror of upstream config_data.h.
//
// Owns the runtime ConfigItem tree and exposes the path-based read/write
// API the rest of librime uses (Traverse / TraverseWrite, plus the
// SplitPath / JoinPath / ResolveListIndex helpers).
//
// What's NOT implemented in this phase:
//   * LoadFromStream / LoadFromFile -- return false (no YAML parser yet).
//   * SaveToStream / SaveToFile     -- return false (no YAML emitter yet).
//   * ConfigCompiler integration    -- the parameter type is forward-
//     declared but the loader will always pass NULL.
// These will land when we restore yaml-cpp (see feedback_yaml_essence.md).
//
// Changes vs. upstream:
//   * `= default` -> empty body.
//   * NSDMI `modified_ = false;` -> default-ctor mem-initialiser list.
//
#ifndef RIME_CONFIG_DATA_H_
#define RIME_CONFIG_DATA_H_

#include <iosfwd>  // istream / ostream forward decls for stub signatures
#include <rime/common.h>

namespace rime {

class ConfigCompiler;  // forward-declared; loader treats NULL as "no compiler"
class ConfigItem;

class ConfigData {
 public:
  ConfigData() : modified_(false), auto_save_(false) {}
  ~ConfigData();

  // Returns whether actually saved to file. Stubbed to false until the
  // YAML emitter is restored.
  bool Save();
  bool LoadFromStream(std::istream& stream);
  bool SaveToStream(std::ostream& stream);
  bool LoadFromFile(const path& file_path, ConfigCompiler* compiler);
  bool SaveToFile(const path& file_path);

  // In-memory path operations -- these ARE implemented.
  bool TraverseWrite(const string& node_path, an<ConfigItem> item);
  an<ConfigItem> Traverse(const string& node_path);

  static vector<string> SplitPath(const string& node_path);
  static string JoinPath(const vector<string>& keys);
  static bool IsListItemReference(const string& key);
  static string FormatListIndex(size_t index);
  static size_t ResolveListIndex(an<ConfigItem> list,
                                 const string& key,
                                 bool read_only = false);

  const path& file_path() const { return file_path_; }
  bool modified() const { return modified_; }
  void set_modified() { modified_ = true; }
  void set_auto_save(bool auto_save) { auto_save_ = auto_save; }

  an<ConfigItem> root;

 protected:
  path file_path_;
  bool modified_;
  bool auto_save_;
};

}  // namespace rime

#endif  // RIME_CONFIG_DATA_H_
