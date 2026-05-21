//
// rime/schema.cc -- WinCE-port mirror of upstream schema.cc.
//
// Changes vs. upstream:
//   * <boost/algorithm/string.hpp> dropped; we don't need starts_with
//     because we route schema_id directly through LoadBuiltinSchema
//     instead of forking on the ".default" prefix.
//   * `Config::Require("config")->Create(...)` REMOVED. The
//     Class<Config, const string&> registry path needs Config to inherit
//     from Class<>, which we trimmed for MVP. Instead, on construction
//     we ask config/builtin_schemas.h for a ready-made ConfigData and
//     wrap it in a new Config. Schemas not in the builtin table get an
//     empty Config (FetchUsefulConfigItems falls through to defaults).
//
#include <rime/schema.h>
#include <rime/config/builtin_schemas.h>
#include <rime/config/config_data.h>

namespace rime {

Schema::Schema()
    : schema_id_(".default"),
      page_size_(5),
      page_down_cycle_(false) {
  config_.reset(new Config);  // empty Config; default values fill in
  FetchUsefulConfigItems();
}

Schema::Schema(const string& schema_id)
    : schema_id_(schema_id),
      page_size_(5),
      page_down_cycle_(false) {
  if (HasBuiltinSchema(schema_id)) {
    an<ConfigData> data = LoadBuiltinSchema(schema_id);
    config_.reset(new Config(data));
  } else {
    config_.reset(new Config);  // empty; schema_name_ ends up "<id>?"
  }
  FetchUsefulConfigItems();
}

void Schema::FetchUsefulConfigItems() {
  if (!config_) {
    schema_name_ = schema_id_ + "?";
    return;
  }
  if (!config_->GetString("schema/name", &schema_name_)) {
    schema_name_ = schema_id_;
  }
  config_->GetInt("menu/page_size", &page_size_);
  if (page_size_ < 1) {
    page_size_ = 5;
  }
  config_->GetString("menu/alternative_select_keys", &select_keys_);
  config_->GetBool("menu/page_down_cycle", &page_down_cycle_);
}

}  // namespace rime
