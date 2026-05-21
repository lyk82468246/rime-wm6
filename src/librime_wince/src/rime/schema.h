//
// rime/schema.h -- WinCE-port mirror of upstream schema.h.
//
// Schema bundles a schema_id + a loaded Config + the "fast path" config
// items the engine reads on every keypress (page_size, select_keys,
// page_down_cycle, etc.).
//
// Changes vs. upstream:
//   * NSDMI `int page_size_ = 5;` etc. -> default-ctor mem-init list.
//   * `SchemaComponent : public Config::Component` REMOVED. Upstream uses
//     it to route "luna_pinyin" -> the "luna_pinyin.schema" Config via
//     the registry; our MVP build resolves builtin schema names directly
//     in Schema's constructor (see schema.cc and config/builtin_schemas.h).
//     The registry-based path comes back when yaml-cpp lands.
//
#ifndef RIME_SCHEMA_H_
#define RIME_SCHEMA_H_

#include <rime/common.h>
#include <rime/config.h>  // for convenience

namespace rime {

class Schema {
 public:
  Schema();
  explicit Schema(const string& schema_id);
  Schema(const string& schema_id, Config* config)
      : schema_id_(schema_id),
        page_size_(5),
        page_down_cycle_(false) {
    // the<Config> has no raw-pointer ctor (base is explicit); reset in body.
    config_.reset(config);
  }

  const string& schema_id() const { return schema_id_; }
  const string& schema_name() const { return schema_name_; }

  Config* config() const { return config_.get(); }
  void set_config(Config* config) { config_.reset(config); }

  int page_size() const { return page_size_; }
  bool page_down_cycle() const { return page_down_cycle_; }
  const string& select_keys() const { return select_keys_; }
  void set_select_keys(const string& keys) { select_keys_ = keys; }

 private:
  void FetchUsefulConfigItems();

  string schema_id_;
  string schema_name_;
  the<Config> config_;
  // frequently used config items
  int page_size_;
  bool page_down_cycle_;
  string select_keys_;
};

}  // namespace rime

#endif  // RIME_SCHEMA_H_
