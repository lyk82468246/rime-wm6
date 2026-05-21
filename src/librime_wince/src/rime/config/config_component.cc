//
// rime/config/config_component.cc -- WinCE-port mirror of the Config class
// portion of upstream config_component.cc.
//
// Implements Config::* only. Everything past line 148 in upstream
// (ConfigResourceProvider, DeployedConfigResourceProvider,
// UserConfigResourceProvider, ConfigComponentBase, ConfigLoader,
// ConfigBuilder, MultiplePlugins) is removed -- see the header comment in
// config_component.h for the rationale.
//
// Changes vs. upstream:
//   * `auto` -> explicit `an<ConfigItem>` / `an<ConfigValue>`.
//   * `nullptr` (constructor arg + LoadFromFile compiler param) -> NULL.
//
#include <rime/config/config_component.h>
#include <rime/config/config_data.h>
#include <rime/config/config_types.h>

namespace rime {

Config::Config() : ConfigItemRef(NULL), data_(New<ConfigData>()) {
  ConfigItemRef::data_ = data_.get();
}

Config::~Config() {}

Config::Config(an<ConfigData> data)
    : ConfigItemRef(data.get()), data_(data) {}

bool Config::Save() {
  return data_->Save();
}

bool Config::LoadFromStream(std::istream& stream) {
  return data_->LoadFromStream(stream);
}

bool Config::SaveToStream(std::ostream& stream) {
  return data_->SaveToStream(stream);
}

bool Config::LoadFromFile(const path& file_path) {
  return data_->LoadFromFile(file_path, NULL);
}

bool Config::SaveToFile(const path& file_path) {
  return data_->SaveToFile(file_path);
}

bool Config::IsNull(const string& path) {
  an<ConfigItem> p = data_->Traverse(path);
  return !p || p->type() == ConfigItem::kNull;
}

bool Config::IsValue(const string& path) {
  an<ConfigItem> p = data_->Traverse(path);
  return !p || p->type() == ConfigItem::kScalar;
}

bool Config::IsList(const string& path) {
  an<ConfigItem> p = data_->Traverse(path);
  return !p || p->type() == ConfigItem::kList;
}

bool Config::IsMap(const string& path) {
  an<ConfigItem> p = data_->Traverse(path);
  return !p || p->type() == ConfigItem::kMap;
}

bool Config::GetBool(const string& path, bool* value) {
  an<ConfigValue> p = As<ConfigValue>(data_->Traverse(path));
  return p && p->GetBool(value);
}

bool Config::GetInt(const string& path, int* value) {
  an<ConfigValue> p = As<ConfigValue>(data_->Traverse(path));
  return p && p->GetInt(value);
}

bool Config::GetDouble(const string& path, double* value) {
  an<ConfigValue> p = As<ConfigValue>(data_->Traverse(path));
  return p && p->GetDouble(value);
}

bool Config::GetString(const string& path, string* value) {
  an<ConfigValue> p = As<ConfigValue>(data_->Traverse(path));
  return p && p->GetString(value);
}

size_t Config::GetListSize(const string& path) {
  an<ConfigList> list = GetList(path);
  return list ? list->size() : 0;
}

an<ConfigItem> Config::GetItem(const string& path) {
  return data_->Traverse(path);
}

an<ConfigValue> Config::GetValue(const string& path) {
  return As<ConfigValue>(data_->Traverse(path));
}

an<ConfigList> Config::GetList(const string& path) {
  return As<ConfigList>(data_->Traverse(path));
}

an<ConfigMap> Config::GetMap(const string& path) {
  return As<ConfigMap>(data_->Traverse(path));
}

bool Config::SetBool(const string& path, bool value) {
  return SetItem(path, New<ConfigValue>(value));
}

bool Config::SetInt(const string& path, int value) {
  return SetItem(path, New<ConfigValue>(value));
}

bool Config::SetDouble(const string& path, double value) {
  return SetItem(path, New<ConfigValue>(value));
}

bool Config::SetString(const string& path, const char* value) {
  return SetItem(path, New<ConfigValue>(value));
}

bool Config::SetString(const string& path, const string& value) {
  return SetItem(path, New<ConfigValue>(value));
}

bool Config::SetItem(const string& path, an<ConfigItem> item) {
  return data_->TraverseWrite(path, item);
}

an<ConfigItem> Config::GetItem() const {
  return data_->root;
}

void Config::SetItem(an<ConfigItem> item) {
  data_->root = item;
  set_modified();
}

}  // namespace rime
