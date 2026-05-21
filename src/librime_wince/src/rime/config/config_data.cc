//
// rime/config/config_data.cc -- WinCE-port mirror of upstream config_data.cc.
//
// Implements the in-memory subset:
//   * SplitPath / JoinPath / IsListItemReference / FormatListIndex
//   * ResolveListIndex
//   * Traverse (read path)
//   * TraverseWrite (write path via CoW chain)
//   * ConfigDataRootRef (the writable root anchor)
//   * destructor (honors auto_save_)
// YAML-dependent operations (Load*/Save*) are stubs that return false until
// we restore yaml-cpp.
//
// Changes vs. upstream:
//   * Dropped <yaml-cpp/yaml.h>, <filesystem>, <fstream>, ConfigCompiler
//     interaction, ConvertFromYaml, EmitScalar, EmitYaml. LoadFromFile and
//     friends return false and log via LOG(ERROR) (compiled out).
//   * boost::is_any_of / split / trim_left_copy_if / join replaced with
//     hand-rolled loops on '/'.
//   * `auto` -> explicit types.
//   * `nullptr` -> default `an<T>()`.
//   * `override` removed.
//   * C++11 range / iterator-pair `for (auto it = keys.begin(), end =
//     keys.end(); ...)` -> classic two-variable for loop.
//
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <rime/config/config_cow_ref.h>
#include <rime/config/config_data.h>
#include <rime/config/config_types.h>
#include <rime/config/yaml_parser.h>

namespace rime {

ConfigData::~ConfigData() {
  if (auto_save_)
    Save();
}

bool ConfigData::Save() {
  return modified_ && !file_path_.empty() && SaveToFile(file_path_);
}

// -- YAML loader: thin wrapper over yaml_parser.cc -------------------

bool ConfigData::LoadFromStream(std::istream& stream) {
  std::ostringstream buf;
  buf << stream.rdbuf();
  string err;
  an<ConfigItem> parsed = yaml::Parse(buf.str(), &err);
  if (!parsed) {
    LOG(ERROR) << "YAML parse error: " << err;
    return false;
  }
  root = parsed;
  modified_ = false;
  return true;
}

bool ConfigData::SaveToStream(std::ostream& stream) {
  (void)stream;
  LOG(ERROR) << "SaveToStream: YAML emitter not implemented in MVP build.";
  return false;
}

bool ConfigData::LoadFromFile(const path& file_path, ConfigCompiler* compiler) {
  (void)compiler;
  file_path_ = file_path;
  modified_ = false;
  root.reset();
  std::ifstream in(file_path.c_str(), std::ios::binary);
  if (!in.is_open()) {
    LOG(ERROR) << "LoadFromFile: cannot open " << file_path.string();
    return false;
  }
  return LoadFromStream(in);
}

bool ConfigData::SaveToFile(const path& file_path) {
  file_path_ = file_path;
  modified_ = false;
  LOG(ERROR) << "SaveToFile: YAML emitter not implemented in MVP build.";
  return false;
}

// -- List-reference helpers (e.g. "@before 0", "@last", "@2") ------------

bool ConfigData::IsListItemReference(const string& key) {
  return key.length() > 1 &&
         key[0] == '@' &&
         std::isalnum(static_cast<unsigned char>(key[1]));
}

string ConfigData::FormatListIndex(size_t index) {
  std::ostringstream formatted;
  formatted << "@" << index;
  return formatted.str();
}

static const string kAfter("after");
static const string kBefore("before");
static const string kLast("last");
static const string kNext("next");

size_t ConfigData::ResolveListIndex(an<ConfigItem> item,
                                    const string& key,
                                    bool read_only) {
  if (!IsListItemReference(key)) {
    return 0;
  }
  an<ConfigList> list = As<ConfigList>(item);
  if (!list) {
    return 0;
  }
  size_t cursor = 1;
  unsigned int index = 0;
  bool will_insert = false;
  if (key.compare(cursor, kNext.length(), kNext) == 0) {
    cursor += kNext.length();
    index = static_cast<unsigned int>(list->size());
  } else if (key.compare(cursor, kBefore.length(), kBefore) == 0) {
    cursor += kBefore.length();
    will_insert = true;
  } else if (key.compare(cursor, kAfter.length(), kAfter) == 0) {
    cursor += kAfter.length();
    index += 1;  // after i == before i+1
    will_insert = true;
  }
  if (cursor < key.length() && key[cursor] == ' ') {
    ++cursor;
  }
  if (key.compare(cursor, kLast.length(), kLast) == 0) {
    cursor += kLast.length();
    index += static_cast<unsigned int>(list->size());
    if (index != 0) {  // when list is empty, (before|after) last == 0
      --index;
    }
  } else {
    index += static_cast<unsigned int>(
        std::strtoul(key.c_str() + cursor, NULL, 10));
  }
  if (will_insert && !read_only) {
    list->Insert(index, an<ConfigItem>());
  }
  return index;
}

// -- Writable anchor at the root of a ConfigData tree --------------------

namespace {

class ConfigDataRootRef : public ConfigItemRef {
 public:
  ConfigDataRootRef(ConfigData* data) : ConfigItemRef(NULL), root_data_(data) {}
  an<ConfigItem> GetItem() const { return root_data_->root; }
  void SetItem(an<ConfigItem> item) { root_data_->root = item; }

 private:
  ConfigData* root_data_;  // shadows ConfigItemRef::data_, intentionally
};

}  // namespace

static an<ConfigItemRef> TypeCheckedCopyOnWrite(an<ConfigItemRef> parent,
                                                 const string& key) {
  // empty key is the __append: __merge: /+: /=: marker -- edit current node.
  if (key.empty()) {
    return parent;
  }
  bool is_list = ConfigData::IsListItemReference(key);
  ConfigItem::ValueType expected =
      is_list ? ConfigItem::kList : ConfigItem::kMap;
  an<ConfigItem> existing = *parent;
  if (existing && existing->type() != expected) {
    LOG(ERROR) << "copy on write failed; incompatible node type: " << key;
    return an<ConfigItemRef>();
  }
  return Cow(parent, key);
}

static an<ConfigItemRef> TraverseCopyOnWrite(an<ConfigItemRef> head,
                                              const string& node_path) {
  if (node_path.empty() || node_path == "/") {
    return head;
  }
  vector<string> keys = ConfigData::SplitPath(node_path);
  size_t n = keys.size();
  for (size_t i = 0; i < n; ++i) {
    const string& key = keys[i];
    an<ConfigItemRef> child = TypeCheckedCopyOnWrite(head, key);
    if (child) {
      head = child;
    } else {
      LOG(ERROR) << "while writing to " << node_path;
      return an<ConfigItemRef>();
    }
  }
  return head;
}

bool ConfigData::TraverseWrite(const string& node_path, an<ConfigItem> item) {
  an<ConfigItemRef> root_ref = New<ConfigDataRootRef>(this);
  an<ConfigItemRef> target = TraverseCopyOnWrite(root_ref, node_path);
  if (target) {
    *target = item;
    set_modified();
    return true;
  }
  return false;
}

// -- Path split / join ---------------------------------------------------

vector<string> ConfigData::SplitPath(const string& node_path) {
  // boost::trim_left_copy_if(/) + boost::split(/) -- expressed inline.
  vector<string> keys;
  size_t start = 0;
  while (start < node_path.size() && node_path[start] == '/') {
    ++start;
  }
  size_t pos = start;
  for (;;) {
    size_t next = node_path.find('/', pos);
    if (next == string::npos) {
      keys.push_back(node_path.substr(pos));
      break;
    }
    keys.push_back(node_path.substr(pos, next - pos));
    pos = next + 1;
  }
  return keys;
}

string ConfigData::JoinPath(const vector<string>& keys) {
  string out;
  for (size_t i = 0; i < keys.size(); ++i) {
    if (i != 0) out += '/';
    out += keys[i];
  }
  return out;
}

an<ConfigItem> ConfigData::Traverse(const string& node_path) {
  if (node_path.empty() || node_path == "/") {
    return root;
  }
  vector<string> keys = SplitPath(node_path);
  an<ConfigItem> p = root;
  for (size_t i = 0; i < keys.size(); ++i) {
    const string& key = keys[i];
    ConfigItem::ValueType node_type = ConfigItem::kMap;
    size_t list_index = 0;
    if (IsListItemReference(key)) {
      node_type = ConfigItem::kList;
      list_index = ResolveListIndex(p, key, true);
    }
    if (!p || p->type() != node_type) {
      return an<ConfigItem>();
    }
    if (node_type == ConfigItem::kList) {
      p = As<ConfigList>(p)->GetAt(list_index);
    } else {
      p = As<ConfigMap>(p)->Get(key);
    }
  }
  return p;
}

}  // namespace rime
