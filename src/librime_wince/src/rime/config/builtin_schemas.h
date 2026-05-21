//
// rime/config/builtin_schemas.h -- MVP fallback for the missing yaml-cpp.
//
// Until the YAML parser is restored (see feedback_yaml_essence.md), this
// file provides hand-built ConfigData trees for the schemas the WinCE
// build needs to boot. The trees mirror what yaml-cpp would have produced
// from src/librime/data/minimal/<name>.schema.yaml, restricted to the
// fields the ported engine actually reads.
//
// Currently supported:
//   * "luna_pinyin" -- the default pinyin schema, minimal subset:
//     schema/schema_id, schema/name, schema/version,
//     speller/alphabet, speller/delimiter, speller/algebra,
//     translator/dictionary.
//
// Callers:
//   * For now, the engine factory in setup.cc (when ported) will dispatch
//     to LoadBuiltinSchema instead of Config::LoadFromFile when the file
//     name matches a known builtin.
//   * Tests can call LoadBuiltinSchema directly to obtain a ConfigData
//     for the algebra/encoder/translator pipelines without touching disk.
//
// When yaml-cpp comes back this whole file is expected to delete cleanly,
// replaced by real .yaml files on disk. No long-term API commitments here.
//
#ifndef RIME_BUILTIN_SCHEMAS_H_
#define RIME_BUILTIN_SCHEMAS_H_

#include <rime/common.h>

namespace rime {

class ConfigData;

// Returns an<ConfigData> whose `root` is a ConfigMap populated for the
// requested schema_id, or a default-constructed (empty) an<ConfigData>
// if the id is not in the builtin table. modified_ is left false.
an<ConfigData> LoadBuiltinSchema(const string& schema_id);

// Returns true iff LoadBuiltinSchema would return a non-empty tree for
// schema_id. Cheap; useful for callers that want to choose between the
// builtin path and the (currently stubbed) file loader.
bool HasBuiltinSchema(const string& schema_id);

}  // namespace rime

#endif  // RIME_BUILTIN_SCHEMAS_H_
