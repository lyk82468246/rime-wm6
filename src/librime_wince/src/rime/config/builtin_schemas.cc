//
// rime/config/builtin_schemas.cc -- hand-built ConfigData fallback.
//
// Each schema's tree is constructed via tiny inline helpers (V/L/M) that
// stay close to the YAML it shadows -- if you have data/minimal/
// luna_pinyin.schema.yaml open in a side-by-side, you can read this file
// line-for-line against it. ASCII-only rule applies: the upstream `name`
// field contains CJK ("Luna Pinyin"); we use the romanised form.
//
// To extend with more builtin schemas: add another Build*() function and
// register it in the switch inside LoadBuiltinSchema().
//
#include <rime/config/builtin_schemas.h>
#include <rime/config/config_data.h>
#include <rime/config/config_types.h>

namespace rime {

// ---------- tiny builders to keep tree construction readable ----------
namespace {

// Scalar
an<ConfigItem> V(const char* s) {
  return New<ConfigValue>(string(s));
}

// List of scalars: L("a", "b", "c", (const char*)0)  -- NULL-terminated.
an<ConfigList> L(const char* a, const char* b = NULL, const char* c = NULL,
                 const char* d = NULL, const char* e = NULL,
                 const char* f = NULL, const char* g = NULL,
                 const char* h = NULL, const char* i = NULL,
                 const char* j = NULL, const char* k = NULL,
                 const char* l = NULL, const char* m = NULL,
                 const char* n = NULL) {
  an<ConfigList> out = New<ConfigList>();
  const char* args[] = {a, b, c, d, e, f, g, h, i, j, k, l, m, n};
  for (size_t idx = 0; idx < sizeof(args)/sizeof(args[0]); ++idx) {
    if (!args[idx]) break;
    out->Append(V(args[idx]));
  }
  return out;
}

// Add a key/value pair to a map; returns the map for chaining.
an<ConfigMap> Put(an<ConfigMap> m, const char* key, an<ConfigItem> value) {
  m->Set(string(key), value);
  return m;
}

}  // namespace

// ---------------------------------------------------------------------------
// luna_pinyin (minimal): mirrors data/minimal/luna_pinyin.schema.yaml down
// to the fields algo/algebra, algo/encoder, and any future translator
// invocation actually need. Switches, segmentor list, and the alphabet/
// cangjie/pinyin/reverse_lookup tag variants are NOT included -- those
// require gear/ + dict/ which haven't been ported yet.
// ---------------------------------------------------------------------------
static an<ConfigData> BuildLunaPinyin() {
  an<ConfigMap> root = New<ConfigMap>();

  // schema:
  //   schema_id: luna_pinyin
  //   name: Luna Pinyin            (upstream is "朙月拼音"; ASCII'd here)
  //   version: "0.15.test"
  an<ConfigMap> schema = New<ConfigMap>();
  Put(schema, "schema_id", V("luna_pinyin"));
  Put(schema, "name",      V("Luna Pinyin"));
  Put(schema, "version",   V("0.15.test"));
  Put(root, "schema", schema);

  // speller:
  //   alphabet: zyxwvutsrqponmlkjihgfedcba
  //   delimiter: " '"
  //   algebra: [...]
  an<ConfigMap> speller = New<ConfigMap>();
  Put(speller, "alphabet",  V("zyxwvutsrqponmlkjihgfedcba"));
  Put(speller, "delimiter", V(" '"));
  Put(speller, "algebra",
      L("erase/^xx$/",
        "abbrev/^([a-z]).+$/$1/",
        "abbrev/^([zcs]h).+$/$1/",
        "derive/^([nl])ve$/$1ue/correction",
        "derive/^([jqxy])u/$1v/correction",
        "derive/un$/uen/correction",
        "derive/ui$/uei/correction",
        "derive/iu$/iou/correction",
        "derive/([aeiou])ng$/$1gn/correction",
        "derive/([dtngkhrzcs])o(u|ng)$/$1o/correction",
        "derive/ong$/on/correction",
        "derive/ao$/oa/correction",
        "derive/([iu])a(o|ng?)$/a$1$2/correction"));
  Put(root, "speller", speller);

  // translator:
  //   dictionary: luna_pinyin
  //   preedit_format:
  //     - xform/([nljqxy])v/$1u_umlaut/  (upstream uses U+00FC; ASCII'd)
  // The preedit_format substitution character is replaced with the
  // 7-bit-safe placeholder "u_umlaut" to keep this file ASCII-only;
  // the WMRimeSIP shell layer is where any wchar_t conversion would
  // happen anyway.
  an<ConfigMap> translator = New<ConfigMap>();
  Put(translator, "dictionary", V("luna_pinyin"));
  Put(translator, "preedit_format",
      L("xform/([nljqxy])v/$1u_umlaut/"));
  Put(root, "translator", translator);

  an<ConfigData> data = New<ConfigData>();
  data->root = root;
  return data;
}

// ---------------------------------------------------------------------------

bool HasBuiltinSchema(const string& schema_id) {
  return schema_id == "luna_pinyin";
}

an<ConfigData> LoadBuiltinSchema(const string& schema_id) {
  if (schema_id == "luna_pinyin") {
    return BuildLunaPinyin();
  }
  return an<ConfigData>();
}

}  // namespace rime
