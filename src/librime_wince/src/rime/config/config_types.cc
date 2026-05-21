//
// rime/config/config_types.cc -- WinCE-port mirror of upstream config_types.cc.
//
// Changes vs. upstream:
//   * <boost/algorithm/string.hpp> dropped. boost::to_lower / starts_with
//     replaced by 4-line hand-rolled equivalents.
//   * C++11 conversions replaced:
//       std::stoi   -> std::strtol
//       std::stod   -> std::strtod
//       std::to_string(int)    -> sprintf into char[32]
//       std::to_string(double) -> sprintf into char[32], "%g"
//     No try/catch around the conversions: strtol/strtod indicate failure
//     by leaving endp == start, which we detect and propagate as a false
//     return -- matches the upstream caller-visible contract.
//   * `auto` decltypes -> explicit ConfigList/Map iterator types.
//   * `nullptr` -> default-constructed `an<T>()`. Returning nullptr in
//     upstream relies on the implicit conversion to shared_ptr<T>; our
//     `an<T>` wrapper doesn't have that conversion, so we return an empty
//     `an<T>()` instead. Same observable behavior (empty pointer).
//   * `override` removed (handled by the header).
//
#include <cstdio>     // sprintf
#include <cstdlib>    // strtol, strtod
#include <cstring>    // strncmp
#include <rime/config/config_data.h>
#include <rime/config/config_types.h>

namespace rime {

// ---------------------------------------------------------------------------
// Local helpers: tiny replacements for the boost / C++11 string utilities.
// ---------------------------------------------------------------------------
namespace {

void ascii_to_lower(string* s) {
  for (size_t i = 0; i < s->size(); ++i) {
    char c = (*s)[i];
    if (c >= 'A' && c <= 'Z')
      (*s)[i] = c - 'A' + 'a';
  }
}

bool starts_with(const string& s, const char* prefix) {
  size_t n = 0;
  while (prefix[n]) ++n;
  return s.size() >= n && std::strncmp(s.c_str(), prefix, n) == 0;
}

string int_to_string(int v) {
  char buf[32];
  std::sprintf(buf, "%d", v);
  return string(buf);
}

string double_to_string(double v) {
  char buf[32];
  std::sprintf(buf, "%g", v);
  return string(buf);
}

}  // namespace

// ConfigValue members

ConfigValue::ConfigValue(bool value) : ConfigItem(kScalar) {
  SetBool(value);
}

ConfigValue::ConfigValue(int value) : ConfigItem(kScalar) {
  SetInt(value);
}

ConfigValue::ConfigValue(double value) : ConfigItem(kScalar) {
  SetDouble(value);
}

ConfigValue::ConfigValue(const char* value)
    : ConfigItem(kScalar), value_(value) {}

ConfigValue::ConfigValue(const string& value)
    : ConfigItem(kScalar), value_(value) {}

bool ConfigValue::GetBool(bool* value) const {
  if (!value || value_.empty())
    return false;
  string bstr = value_;
  ascii_to_lower(&bstr);
  if (bstr == "true") {
    *value = true;
    return true;
  } else if (bstr == "false") {
    *value = false;
    return true;
  } else {
    return false;
  }
}

bool ConfigValue::GetInt(int* value) const {
  if (!value || value_.empty())
    return false;
  const char* start = value_.c_str();
  char* endp = NULL;
  // Hex prefix is accepted with base 16; otherwise base 10. strtol's
  // base=0 trick would handle both, but it also takes octal for a leading
  // "0", which upstream doesn't intend. Branch explicitly.
  long parsed;
  if (starts_with(value_, "0x")) {
    parsed = std::strtol(start, &endp, 16);
  } else {
    parsed = std::strtol(start, &endp, 10);
  }
  // Whole string must be consumed; rejecting "12abc" matches stoi behavior.
  if (endp == start || *endp != '\0')
    return false;
  *value = static_cast<int>(parsed);
  return true;
}

bool ConfigValue::GetDouble(double* value) const {
  if (!value || value_.empty())
    return false;
  const char* start = value_.c_str();
  char* endp = NULL;
  double parsed = std::strtod(start, &endp);
  if (endp == start || *endp != '\0')
    return false;
  *value = parsed;
  return true;
}

bool ConfigValue::GetString(string* value) const {
  if (!value)
    return false;
  *value = value_;
  return true;
}

bool ConfigValue::SetBool(bool value) {
  value_ = value ? "true" : "false";
  return true;
}

bool ConfigValue::SetInt(int value) {
  value_ = int_to_string(value);
  return true;
}

bool ConfigValue::SetDouble(double value) {
  value_ = double_to_string(value);
  return true;
}

bool ConfigValue::SetString(const char* value) {
  value_ = value;
  return true;
}

bool ConfigValue::SetString(const string& value) {
  value_ = value;
  return true;
}

// ConfigList members

an<ConfigItem> ConfigList::GetAt(size_t i) const {
  if (i >= seq_.size())
    return an<ConfigItem>();
  return seq_[i];
}

an<ConfigValue> ConfigList::GetValueAt(size_t i) const {
  return As<ConfigValue>(GetAt(i));
}

bool ConfigList::SetAt(size_t i, an<ConfigItem> element) {
  if (i >= seq_.size())
    seq_.resize(i + 1);
  seq_[i] = element;
  return true;
}

bool ConfigList::Insert(size_t i, an<ConfigItem> element) {
  if (i > seq_.size()) {
    seq_.resize(i);
  }
  seq_.insert(seq_.begin() + i, element);
  return true;
}

bool ConfigList::Append(an<ConfigItem> element) {
  seq_.push_back(element);
  return true;
}

bool ConfigList::Resize(size_t size) {
  seq_.resize(size);
  return true;
}

bool ConfigList::Clear() {
  seq_.clear();
  return true;
}

size_t ConfigList::size() const {
  return seq_.size();
}

ConfigList::Iterator ConfigList::begin() {
  return seq_.begin();
}

ConfigList::Iterator ConfigList::end() {
  return seq_.end();
}

// ConfigMap members

bool ConfigMap::HasKey(const string& key) const {
  return !!Get(key);
}

an<ConfigItem> ConfigMap::Get(const string& key) const {
  ConfigMap::Map::const_iterator it = map_.find(key);
  if (it == map_.end())
    return an<ConfigItem>();
  return it->second;
}

an<ConfigValue> ConfigMap::GetValue(const string& key) const {
  return As<ConfigValue>(Get(key));
}

bool ConfigMap::Set(const string& key, an<ConfigItem> element) {
  map_[key] = element;
  return true;
}

bool ConfigMap::Clear() {
  map_.clear();
  return true;
}

ConfigMap::Iterator ConfigMap::begin() {
  return map_.begin();
}

ConfigMap::Iterator ConfigMap::end() {
  return map_.end();
}

// ConfigItemRef members

bool ConfigItemRef::IsNull() const {
  an<ConfigItem> item = GetItem();
  return !item || item->type() == ConfigItem::kNull;
}

bool ConfigItemRef::IsValue() const {
  an<ConfigItem> item = GetItem();
  return item && item->type() == ConfigItem::kScalar;
}

bool ConfigItemRef::IsList() const {
  an<ConfigItem> item = GetItem();
  return item && item->type() == ConfigItem::kList;
}

bool ConfigItemRef::IsMap() const {
  an<ConfigItem> item = GetItem();
  return item && item->type() == ConfigItem::kMap;
}

bool ConfigItemRef::ToBool() const {
  bool value = false;
  an<ConfigValue> item = As<ConfigValue>(GetItem());
  if (item) {
    item->GetBool(&value);
  }
  return value;
}

int ConfigItemRef::ToInt() const {
  int value = 0;
  an<ConfigValue> item = As<ConfigValue>(GetItem());
  if (item) {
    item->GetInt(&value);
  }
  return value;
}

double ConfigItemRef::ToDouble() const {
  double value = 0.0;
  an<ConfigValue> item = As<ConfigValue>(GetItem());
  if (item) {
    item->GetDouble(&value);
  }
  return value;
}

string ConfigItemRef::ToString() const {
  string value;
  an<ConfigValue> item = As<ConfigValue>(GetItem());
  if (item) {
    item->GetString(&value);
  }
  return value;
}

an<ConfigList> ConfigItemRef::AsList() {
  an<ConfigList> list = As<ConfigList>(GetItem());
  if (!list) {
    list = New<ConfigList>();
    SetItem(list);
  }
  return list;
}

an<ConfigMap> ConfigItemRef::AsMap() {
  an<ConfigMap> map = As<ConfigMap>(GetItem());
  if (!map) {
    map = New<ConfigMap>();
    SetItem(map);
  }
  return map;
}

void ConfigItemRef::Clear() {
  SetItem(an<ConfigItem>());
}

bool ConfigItemRef::Append(an<ConfigItem> item) {
  if (AsList()->Append(item)) {
    set_modified();
    return true;
  }
  return false;
}

size_t ConfigItemRef::size() const {
  an<ConfigList> list = As<ConfigList>(GetItem());
  return list ? list->size() : 0;
}

bool ConfigItemRef::HasKey(const string& key) const {
  an<ConfigMap> map = As<ConfigMap>(GetItem());
  return map ? map->HasKey(key) : false;
}

bool ConfigItemRef::modified() const {
  return data_ && data_->modified();
}

void ConfigItemRef::set_modified() {
  if (data_)
    data_->set_modified();
}

}  // namespace rime
