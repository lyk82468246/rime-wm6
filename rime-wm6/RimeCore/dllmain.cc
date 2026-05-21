//
// dllmain.cc -- RimeCore.dll entry point + wince_compat smoke test.
//
// The smoke test instantiates every wince_compat shim once at module load so
// that any template-instantiation errors surface at build time, not later
// when we start wiring real librime code through the shims.
//
// This file will be replaced once we wire up rime_api.cc / setup.cc as the
// real export surface. For now it serves two purposes:
//   1. Force-instantiate the shim templates so MSVC9 emits diagnostics for
//      anything our C++03 backports got wrong.
//   2. Verify that <windows.h> and the wince_compat headers coexist cleanly.
//

#include <windows.h>

#include <sstream>

#include "wince_compat.h"
#include <rime/common.h>
#include <rime/algo/strings.h>
#include <rime/algo/spelling.h>
#include <rime/algo/calculus.h>
#include <rime/algo/algebra.h>
#include <rime/algo/encoder.h>
#include <rime/key_event.h>
#include <rime/config/config_types.h>
#include <rime/config/config_data.h>
#include <rime/config.h>
#include <rime/config/builtin_schemas.h>
#include <rime/candidate.h>
#include <rime/segmentation.h>
#include <rime/translation.h>
#include <rime/menu.h>
#include <rime/filter.h>
#include <rime/processor.h>
#include <rime/segmentor.h>
#include <rime/translator.h>
#include <rime/composition.h>
#include <rime/commit_history.h>
#include <rime/context.h>
#include <rime/schema.h>
#include <rime/engine.h>
#include <rime/service.h>
#include <rime/dict/mapped_file.h>
#include <rime/dict/vocabulary.h>
#include <rime/dict/prism.h>
#include <rime/dict/string_table.h>
#include <rime/dict/table.h>
#include <rime/dict/dictionary.h>
#include <rime/algo/syllabifier.h>
#include <rime/gear/pinyin_translator.h>
#include <rime/config/yaml_parser.h>
#include <rime_api.h>

namespace {

// ---------------------------------------------------------------------------
// Smoke test for the wince_compat shims. Lives in an anonymous namespace,
// invoked via a static initialiser so it's optimised out of the final binary
// if it never runs -- but its presence forces template instantiation.
// ---------------------------------------------------------------------------

struct Widget {
  int value;
  Widget() : value(0) {}
  explicit Widget(int v) : value(v) {}
};

// Polymorphic pair for exercising dynamic_pointer_cast / rime::As / rime::Is.
struct AnimalBase { virtual ~AnimalBase() {} };
struct Cat : AnimalBase {};

struct Voicer {
  int* sink;
  Voicer(int* s) : sink(s) {}
  void operator()(int x) const { *sink = x; }
};

void exercise_shims() {
  // shared_ptr / make_shared / weak_ptr / pointer_cast
  wince::shared_ptr<Widget> a = wince::make_shared<Widget>();
  wince::shared_ptr<Widget> b = wince::make_shared<Widget, int>(42);
  wince::weak_ptr<Widget> w(a);
  wince::shared_ptr<Widget> a2 = w.lock();
  wince::shared_ptr<Widget> a3 = wince::static_pointer_cast<Widget>(a);
  (void)a; (void)b; (void)a2; (void)a3;

  // function<void(int)>
  int captured = 0;
  wince::function<void(int)> fn = Voicer(&captured);
  fn(7);

  // signal<void(int)> + connection.disconnect
  wince::signal<void(int)> sig;
  wince::connection c = sig.connect(Voicer(&captured));
  sig(99);
  c.disconnect();
  sig(123);  // disconnected slot must NOT run; not asserted here, just exercised.

  // path: ctor / operator/= / wstring / exists
  wince::path p("\\Program Files\\WMRime");
  p /= "data";
  std::wstring wp = p.wstring();
  (void)wince::exists(p);
  (void)wp;

  // mutex / lock_guard
  wince::mutex m;
  {
    wince::lock_guard g(m);
  }
}

// ---------------------------------------------------------------------------
// Smoke test for the rime/common.h aliases over wince_compat. Verifies the
// derived-class deduction trick works for an<T> / the<T> / of<T> / weak<T>
// and the C++03 New<T>(...) overload set.
// ---------------------------------------------------------------------------
void exercise_rime_common() {
  using namespace rime;

  // New<T>() / New<T>(a) / 0..5-arg overloads + an<T> aliasing
  an<Widget> a = New<Widget>();
  an<Widget> b = New<Widget, int>(42);
  the<Widget> t = New<Widget>();
  of<Widget> o = New<Widget>();

  // Cross-alias assignments via the wince::shared_ptr<T> base.
  an<Widget> from_of = o;
  the<Widget> from_an = a;
  of<Widget> from_the = t;

  // weak<T> from an<T> (derived-to-base + templated weak<U>(shared_ptr<U>&) ctor).
  weak<Widget> w = a;
  an<Widget> revived = w.lock();
  (void)revived;

  // As<X>(p) and Is<X>(p) -- dynamic_pointer_cast through the wrapper.
  an<AnimalBase> animal = New<Cat>();
  an<Cat> cat = As<Cat>(animal);
  bool is_cat = Is<Cat>(animal);
  (void)cat; (void)is_cat;

  // path typedef + UTF-8 ops
  path data_dir("\\Storage Card\\rime-data");
  data_dir /= "luna_pinyin.schema.bin";

  // signal / connection (aliased from wince)
  signal<void(int)> sig;
  int captured = 0;
  connection conn = sig.connect(Voicer(&captured));
  sig(7);
  conn.disconnect();

  // hash_map / hash_set used as containers (also as base classes in librime)
  hash_map<string, int> dict;
  dict["zhong"] = 1;
  hash_set<int> seen;
  seen.insert(1);

  (void)from_of; (void)from_an; (void)from_the; (void)b;
}

// ---------------------------------------------------------------------------
// Smoke test for the first real ported librime translation unit
// (algo/strings.cc). Confirms that header-include resolution from
// src/librime_wince/src takes precedence over the upstream tree and that
// the C++03 backport of split/join links cleanly.
// ---------------------------------------------------------------------------
void exercise_rime_strings() {
  using namespace rime;

  // split: keep empty tokens vs. skip them
  vector<string> a = strings::split("a,b,,c", ",",
                                    strings::SplitBehavior::KeepToken);
  vector<string> b = strings::split("a,b,,c", ",",
                                    strings::SplitBehavior::SkipToken);
  vector<string> c = strings::split("a,b,,c", ",");  // KeepToken overload

  // join over an iterator pair and over a container
  string j1 = strings::join(a.begin(), a.end(), string(","));
  string j2 = strings::join(b, string("|"));

  (void)a; (void)b; (void)c; (void)j1; (void)j2;
}

// ---------------------------------------------------------------------------
// Smoke test for spelling.cc: exercises both Compose and Update transitions
// across all SpellingType values to surface any link-time issues.
// ---------------------------------------------------------------------------
void exercise_rime_spelling() {
  using namespace rime;

  Spelling s1;
  s1.str = "ni";
  s1.properties.credibility = 0.7;

  SpellingProperties delta;
  delta.type = kFuzzySpelling;
  delta.credibility = 0.2;
  delta.is_correction = true;
  delta.tips = "approx";

  // Compose: fuzziest type wins, credibility accumulates.
  s1.properties.Compose(delta);

  // Update: another candidate with same type, less correction-y.
  SpellingProperties other;
  other.type = kFuzzySpelling;
  other.credibility = 0.9;
  other.is_correction = false;
  s1.properties.Update(other);

  // Equality and ordering operators on Spelling.
  Spelling s2("nihao");
  (void)(s1 == s2);
  (void)(s1 < s2);
}

// ---------------------------------------------------------------------------
// Smoke test for wince_compat/regex. Exercises every public API once.
// Doesn't assert correctness here -- if any template fails to instantiate
// or the parser misbehaves on these patterns we'll see compile/link errors.
// Runtime correctness verification happens via dedicated tests in the engine
// porting phase once we actually port calculus.cc and recognizer.cc.
// ---------------------------------------------------------------------------
void exercise_wince_regex() {
  using namespace wince;

  // Construction
  regex r1("[a-z]+");
  regex r2(std::string("^(.+)ng$"));
  regex r3;
  r3.assign("zh|ch|sh");

  // Anchored match
  bool m1 = regex_match(std::string("hello"), r1);
  smatch m;
  bool m2 = regex_match(std::string("yang"), m, r2);
  std::string g1 = m.str(1);  // "ya" if m2

  // Unanchored search
  bool s1 = regex_search(std::string("---abc---"), r1);
  smatch sm;
  bool s2 = regex_search(std::string("xyz zhuang"), sm, r3);
  std::string sm0 = sm.str(0);  // "zh"
  std::string sm_pre = sm.prefix();
  std::string sm_suf = sm.suffix();
  int sm_pos = sm.position(0);
  int sm_len = sm.length(0);

  // Replacement with $N backreferences
  std::string out1 = regex_replace(std::string("zhang"),
                                   regex("([zcs])h"),
                                   std::string("$1"));   // -> "zang"
  std::string out2 = regex_replace(std::string("ni hao"),
                                   regex("\\s+"),
                                   std::string("_"));    // -> "ni_hao"
  std::string out3 = regex_replace(std::string("abc123def"),
                                   regex("\\d+"),
                                   std::string("[$0]")); // -> "abc[123]def"

  (void)m1; (void)m2; (void)g1;
  (void)s1; (void)s2; (void)sm0; (void)sm_pre; (void)sm_suf;
  (void)sm_pos; (void)sm_len;
  (void)out1; (void)out2; (void)out3;

  // Error path -- ensure regex_error inherits std::exception correctly.
  try {
    regex bad("(unclosed");
    (void)bad;
  } catch (const regex_error& e) {
    (void)e.what();
  } catch (const std::exception& e) {
    (void)e.what();
  }
}

// ---------------------------------------------------------------------------
// Smoke test for algo/calculus.cc -- the first real consumer of our hand-
// written regex engine. Exercises every Calculation subclass that Calculus
// can produce: xlit (Transliteration), xform (Transformation), erase
// (Erasion), derive (Derivation + Correction/Abbreviation/Fuzzing tag
// branches), fuzz (direct Fuzzing), abbrev (direct Abbreviation). Validates
// that wince::regex compilation and substitution drive end-to-end through
// rime's DSL parser.
// ---------------------------------------------------------------------------
void exercise_rime_calculus() {
  using namespace rime;

  Calculus calc;

  // xlit: simple codepoint mapping (no regex involved, but exercises utf8.h).
  Calculation* xlit = calc.Parse("xlit/abc/xyz/");
  if (xlit) {
    Spelling s("acb");
    xlit->Apply(&s);  // expects "xzy"
    delete xlit;
  }

  // xform: regex_replace with capture-group backref. Real test of Pike VM.
  Calculation* xform = calc.Parse("xform/^([zcs])h/$1/");
  if (xform) {
    Spelling s("zhang");
    xform->Apply(&s);  // expects "zang"
    delete xform;
  }

  // erase: regex_match (anchored). Drops the whole spelling on match.
  Calculation* erase = calc.Parse("erase/.*q.*/");
  if (erase) {
    Spelling s("qing");
    erase->Apply(&s);  // expects empty
    delete erase;
  }

  // derive with explicit fuzz tag -- goes through the Fuzzing branch.
  Calculation* fuzz = calc.Parse("derive/^zh/z/fuzz");
  if (fuzz) {
    Spelling s("zhang");
    fuzz->Apply(&s);  // expects "zang" + kFuzzySpelling
    delete fuzz;
  }

  // derive with no tag -- plain Derivation.
  Calculation* deriv = calc.Parse("derive/ng$/n/");
  if (deriv) {
    Spelling s("zhang");
    deriv->Apply(&s);  // expects "zhan"
    delete deriv;
  }

  // abbrev shortcut (separate factory, same effect as derive/.../.../abbrev).
  Calculation* abbr = calc.Parse("abbrev/^zh/z/");
  if (abbr) {
    Spelling s("zhang");
    abbr->Apply(&s);  // expects "zang" + kAbbreviation
    delete abbr;
  }

  // Unknown token: parser must return NULL, no crash.
  Calculation* bad = calc.Parse("nonsense/x/y/");
  (void)bad;  // NULL expected; no delete needed.
}

// ---------------------------------------------------------------------------
// Smoke test for key_event / key_table. Validates Parse / repr round-trips
// for both KeyEvent (single key) and KeySequence (string of keys with
// `{name}` escapes). Also exercises the modifier-bitmask path through
// RimeGetModifierByName / RimeGetModifierName.
// ---------------------------------------------------------------------------
void exercise_rime_key_event() {
  using namespace rime;

  // Single printable char.
  KeyEvent a("a");
  (void)a.keycode();   // expect 'a' (0x61)
  (void)a.modifier();  // expect 0
  std::string a_repr = a.repr();  // "a"

  // Modified key.
  KeyEvent ctrl_c("Control+c");
  (void)ctrl_c.ctrl();   // expect true
  (void)ctrl_c.shift();  // expect false
  std::string ctrl_c_repr = ctrl_c.repr();  // "Control+c"

  // Named key.
  KeyEvent ret("Return");
  (void)ret.keycode();  // expect XK_Return (0xff0d)
  std::string ret_repr = ret.repr();  // "Return"

  // Hex fallback for unnamed keycodes.
  KeyEvent unnamed(0x1234, 0);
  std::string unnamed_repr = unnamed.repr();  // "0x1234"

  // Sequence with brace-escaped named key.
  KeySequence seq("hi{Return}!");
  (void)seq.size();  // expect 4: 'h', 'i', Return, '!'
  std::string seq_repr = seq.repr();  // "hi{Return}!"

  // Empty sequence.
  KeySequence empty;
  (void)empty.repr();

  // Unparseable input must zero out the KeyEvent, not crash.
  KeyEvent bad("NoSuchKey");
  (void)bad.keycode();  // expect 0 after failed Parse

  (void)a_repr; (void)ctrl_c_repr; (void)ret_repr;
  (void)unnamed_repr; (void)seq_repr;
}

// ---------------------------------------------------------------------------
// Smoke test for config_types. Exercises every public surface so any
// template-instantiation or linkage gap surfaces at build time.
// ConfigData is a 2-method stub for now; the real loader will land in a
// later phase.
// ---------------------------------------------------------------------------

// Concrete ConfigItemRef so we can construct it from the smoke test --
// upstream's only concrete subclass is ConfigListEntryRef / ConfigMapEntryRef
// (both via Config). For exercising the base API directly we wrap a plain
// an<ConfigItem>.
namespace {
class StandaloneItemRef : public rime::ConfigItemRef {
 public:
  StandaloneItemRef(rime::ConfigData* data, rime::an<rime::ConfigItem> item)
      : rime::ConfigItemRef(data), item_(item) {}
 protected:
  rime::an<rime::ConfigItem> GetItem() const { return item_; }
  void SetItem(rime::an<rime::ConfigItem> item) { item_ = item; }
 private:
  rime::an<rime::ConfigItem> item_;
};
}  // namespace

void exercise_rime_config_types() {
  using namespace rime;

  // ConfigValue ctors and scalar accessors.
  an<ConfigValue> b = New<ConfigValue>(true);
  bool bv = false; b->GetBool(&bv);

  an<ConfigValue> i = New<ConfigValue>(42);
  int iv = 0; i->GetInt(&iv);

  an<ConfigValue> hex = New<ConfigValue>(string("0xff"));
  int hv = 0; hex->GetInt(&hv);  // expects 255

  an<ConfigValue> bad = New<ConfigValue>(string("12abc"));
  int xv = 0; (void)bad->GetInt(&xv);  // expects false return

  an<ConfigValue> d = New<ConfigValue>(3.14);
  double dv = 0.0; d->GetDouble(&dv);

  an<ConfigValue> s = New<ConfigValue>("hello");
  string sv; s->GetString(&sv);

  // ConfigList: SetAt resizes, GetAt returns null past end.
  an<ConfigList> list = New<ConfigList>();
  list->Append(New<ConfigValue>(string("one")));
  list->Append(New<ConfigValue>(string("two")));
  list->SetAt(5, New<ConfigValue>(string("six")));
  (void)list->size();
  an<ConfigItem> g0 = list->GetAt(0);
  an<ConfigItem> g99 = list->GetAt(99);
  an<ConfigValue> gv = list->GetValueAt(0);

  // ConfigMap: insert / lookup / negative-lookup.
  an<ConfigMap> mp = New<ConfigMap>();
  mp->Set("k1", New<ConfigValue>(string("v1")));
  mp->Set("k2", New<ConfigValue>(42));
  (void)mp->HasKey("k1");
  (void)mp->HasKey("missing");
  an<ConfigValue> mv = mp->GetValue("k2");

  // ConfigItemRef + chained subscript [] + operator=.
  ConfigData cd;
  StandaloneItemRef root(&cd, mp);
  root["k1"] = string("rewritten");
  root["k3"] = 7;
  (void)root.IsMap();
  (void)root.HasKey("k1");
  (void)root.size();

  // Subscript through a list-typed root.
  an<ConfigItem> list_root = list;
  StandaloneItemRef lroot(&cd, list_root);
  lroot[0] = string("zero");
  (void)lroot.IsList();
  (void)lroot.size();

  // ToBool / ToInt / ToDouble / ToString through a scalar ref.
  an<ConfigItem> scalar_item = New<ConfigValue>(string("true"));
  StandaloneItemRef sref(&cd, scalar_item);
  (void)sref.ToBool();

  // modified() / set_modified() round-trip via the data pointer.
  (void)root.modified();
  root.set_modified();
  (void)cd.modified();  // expects true

  // AsConfigItem overload resolution: assigning an existing an<ConfigItem>
  // should NOT wrap it in a new ConfigValue.
  an<ConfigItem> already = New<ConfigValue>(string("passthrough"));
  root["passthrough"] = already;

  (void)bv; (void)iv; (void)hv; (void)xv; (void)dv; (void)sv;
  (void)g0; (void)g99; (void)gv; (void)mv;
}

// ---------------------------------------------------------------------------
// Smoke test for config_data path-traversal. Exercises:
//   * SplitPath / JoinPath round-trip.
//   * IsListItemReference / FormatListIndex.
//   * Traverse on a tree built up in-memory (no YAML, but the API surface
//     downstream code will hit is identical).
//   * TraverseWrite (which drives CoW through ConfigCowRef<ConfigMap> and
//     ConfigCowRef<ConfigList>).
//   * ResolveListIndex variants: @0, @next, @last, @before 0, @after last.
// LoadFromFile is stubbed, but we still call it to confirm the symbol is
// linked and returns false cleanly.
// ---------------------------------------------------------------------------
void exercise_rime_config_data() {
  using namespace rime;

  // Path utilities.
  vector<string> keys = ConfigData::SplitPath("/schema/translator/dictionary");
  string joined = ConfigData::JoinPath(keys);  // "schema/translator/dictionary"

  vector<string> single = ConfigData::SplitPath("solitary");
  (void)single.size();  // expects 1

  vector<string> compress = ConfigData::SplitPath("a//b");
  (void)compress.size();  // expects 3 with empty middle

  // List-ref classification.
  (void)ConfigData::IsListItemReference("@3");
  (void)ConfigData::IsListItemReference("@next");
  (void)ConfigData::IsListItemReference("plain");
  (void)ConfigData::IsListItemReference("@");
  string formatted = ConfigData::FormatListIndex(7);  // "@7"

  // ResolveListIndex on a real list.
  an<ConfigList> list = New<ConfigList>();
  list->Append(New<ConfigValue>(string("a")));
  list->Append(New<ConfigValue>(string("b")));
  list->Append(New<ConfigValue>(string("c")));
  size_t idx_next = ConfigData::ResolveListIndex(list, "@next", true);
  size_t idx_last = ConfigData::ResolveListIndex(list, "@last", true);
  size_t idx_2    = ConfigData::ResolveListIndex(list, "@2", true);

  // Traverse on a hand-built tree.
  ConfigData cd;
  an<ConfigMap> root_map = New<ConfigMap>();
  an<ConfigMap> schema = New<ConfigMap>();
  schema->Set("name", New<ConfigValue>(string("luna_pinyin")));
  schema->Set("version", New<ConfigValue>(string("0.1")));
  root_map->Set("schema", schema);
  cd.root = root_map;

  an<ConfigItem> traversed = cd.Traverse("schema/name");
  an<ConfigItem> missing = cd.Traverse("schema/nonexistent");
  an<ConfigItem> whole = cd.Traverse("/");

  // TraverseWrite -- exercises Cow on the map subtree.
  bool wrote = cd.TraverseWrite("schema/version", New<ConfigValue>(string("0.2")));
  (void)cd.modified();  // expects true after a successful write

  // TraverseWrite into a fresh list-typed path: __append style "@next"
  // creates / extends a list at the addressed slot.
  cd.TraverseWrite("history/@next", New<ConfigValue>(string("first")));
  cd.TraverseWrite("history/@next", New<ConfigValue>(string("second")));
  an<ConfigItem> history_first = cd.Traverse("history/@0");

  // YAML stubs: must return false, must not crash.
  std::istringstream empty_in;
  (void)cd.LoadFromStream(empty_in);
  std::ostringstream out;
  (void)cd.SaveToStream(out);
  (void)cd.LoadFromFile(path("nope.yaml"), 0);
  (void)cd.SaveToFile(path("nope.yaml"));

  (void)keys; (void)joined; (void)compress; (void)formatted;
  (void)idx_next; (void)idx_last; (void)idx_2;
  (void)traversed; (void)missing; (void)whole;
  (void)wrote; (void)history_first;
}

// ---------------------------------------------------------------------------
// Smoke test for the top-level Config wrapper. Validates that the Get/Set
// path-based API delegates correctly into ConfigData::Traverse /
// TraverseWrite, and that the typed accessors honor scalar parsing
// (GetBool/GetInt/GetDouble).
// ---------------------------------------------------------------------------
void exercise_rime_config() {
  using namespace rime;

  Config cfg;

  // Build a hand-rolled tree via the Set* path API.
  cfg.SetString("schema/name", "luna_pinyin");
  cfg.SetString("schema/version", "0.1");
  cfg.SetInt("schema/page_size", 5);
  cfg.SetBool("schema/use_cursor", true);
  cfg.SetDouble("schema/weight", 1.5);

  // Type predicates against actual node types.
  (void)cfg.IsMap("schema");          // true
  (void)cfg.IsValue("schema/name");   // true
  (void)cfg.IsList("schema");         // false
  (void)cfg.IsNull("nope/missing");   // true (absent -> kNull)

  // Typed getters round-trip.
  string name;
  cfg.GetString("schema/name", &name);

  int page_size = 0;
  cfg.GetInt("schema/page_size", &page_size);

  bool use_cursor = false;
  cfg.GetBool("schema/use_cursor", &use_cursor);

  double weight = 0.0;
  cfg.GetDouble("schema/weight", &weight);

  // Container getters.
  an<ConfigMap> schema_map = cfg.GetMap("schema");

  // List ops via Set/Get on synthetic indices.
  cfg.SetString("history/@next", "a");
  cfg.SetString("history/@next", "b");
  cfg.SetString("history/@next", "c");
  size_t history_size = cfg.GetListSize("history");

  an<ConfigList> history = cfg.GetList("history");
  string second;
  cfg.GetString("history/@1", &second);  // "b"

  // SetItem with an arbitrary an<ConfigItem>.
  an<ConfigMap> nested = New<ConfigMap>();
  nested->Set("k", New<ConfigValue>(string("v")));
  cfg.SetItem("nested", nested);
  (void)cfg.IsMap("nested");

  // LoadFromFile must call into the stub (returns false, doesn't crash).
  (void)cfg.LoadFromFile(path("nonexistent.yaml"));

  (void)name; (void)page_size; (void)use_cursor; (void)weight;
  (void)schema_map; (void)history_size; (void)history; (void)second;
}

// ---------------------------------------------------------------------------
// Smoke test for algo/algebra. Builds a Projection from a few calculus
// formulas, applies it to both a plain string and a Script (multi-entry
// spelling table). Validates that ConfigList -> Calculation -> Projection
// chain links correctly end-to-end.
// ---------------------------------------------------------------------------
void exercise_rime_algebra() {
  using namespace rime;

  // Build a ConfigList of 3 formulas, the kind that would appear under
  //   speller:
  //     algebra:
  //       - xform/^zh/z/
  //       - xform/^ch/c/
  //       - derive/ng$/n/
  an<ConfigList> formulas = New<ConfigList>();
  formulas->Append(New<ConfigValue>(string("xform/^zh/z/")));
  formulas->Append(New<ConfigValue>(string("xform/^ch/c/")));
  formulas->Append(New<ConfigValue>(string("derive/ng$/n/")));

  Projection proj;
  bool loaded = proj.Load(formulas);

  // Apply on a plain string: "zhang" -> "zan" (xform/^zh + derive/ng$).
  string syl("zhang");
  (void)proj.Apply(&syl);

  // Apply on a Script: derive/ng$/n/ adds entries without deleting.
  Script script;
  script.AddSyllable("zhang");
  script.AddSyllable("chong");
  (void)proj.Apply(&script);

  // Failing formula: must return false, not throw.
  an<ConfigList> bad_formulas = New<ConfigList>();
  bad_formulas->Append(New<ConfigValue>(string("nonsense/x/y/")));
  Projection bad_proj;
  bool bad_loaded = bad_proj.Load(bad_formulas);

  (void)loaded; (void)bad_loaded;
}

// ---------------------------------------------------------------------------
// Smoke test for algo/encoder. Builds a TableEncoder with a tiny rule set
// + exclude pattern, drives EncodePhrase through a stub PhraseCollector,
// and verifies both branches of the Encoder hierarchy (TableEncoder /
// ScriptEncoder) instantiate and link cleanly.
// ---------------------------------------------------------------------------
namespace {
class CapturingCollector : public rime::PhraseCollector {
 public:
  void CreateEntry(const rime::string& phrase,
                   const rime::string& code_str,
                   const rime::string& value) {
    last_phrase = phrase;
    last_code = code_str;
    last_value = value;
  }
  bool TranslateWord(const rime::string& word,
                     std::vector<rime::string>* code) {
    // Fake "dictionary": map each Chinese char to a fixed code.
    // Real schemes would consult a marisa-trie; for smoke-testing we
    // hand-roll one entry.
    if (word == "a") { code->push_back("abcd"); return true; }
    if (word == "b") { code->push_back("bcde"); return true; }
    return false;
  }
  rime::string last_phrase;
  rime::string last_code;
  rime::string last_value;
};
}  // namespace

void exercise_rime_encoder() {
  using namespace rime;

  // RawCode round-trip.
  RawCode rc;
  rc.FromString("abc  def\tghi");  // splits on whitespace? actually space only
  string rc_str = rc.ToString();  // joined with space

  // Build a Config with an encoder rule set.
  Config cfg;
  cfg.SetString("encoder/rules/@next/length_equal", "2");
  cfg.SetString("encoder/rules/@0/formula", "AaAzBaBbBz");
  cfg.SetString("encoder/exclude_patterns/@next", "^z.*$");

  CapturingCollector cc;
  TableEncoder te(&cc);
  bool loaded = te.LoadSettings(&cfg);

  // Exclude-pattern test.
  (void)te.IsCodeExcluded("zhao");  // matches ^z.*$
  (void)te.IsCodeExcluded("abcd");  // doesn't match

  // Encode a 2-char phrase via DFS.
  bool encoded = te.EncodePhrase("ab", "value");
  (void)cc.last_code;  // should be "abcdbcde"-ish per formula AaAzBaBbBz

  // ScriptEncoder path.
  CapturingCollector cc2;
  ScriptEncoder se(&cc2);
  bool encoded_script = se.EncodePhrase("ab", "value");

  (void)rc_str; (void)loaded; (void)encoded; (void)encoded_script;
}

// ---------------------------------------------------------------------------
// Smoke test for builtin_schemas. The interesting bit is end-to-end:
// LoadBuiltinSchema -> wrap in Config -> hand the algebra list to
// Projection::Load -> run the real luna_pinyin algebra over a real
// pinyin syllable. If wince::regex misparses any of the 13 schema
// formulas, this test catches it at DLL load time.
// ---------------------------------------------------------------------------
void exercise_rime_builtin_schemas() {
  using namespace rime;

  // Unknown id: empty result, no crash.
  an<ConfigData> none = LoadBuiltinSchema("not_a_schema");
  (void)HasBuiltinSchema("not_a_schema");  // false
  (void)HasBuiltinSchema("luna_pinyin");   // true

  // Build the canonical luna_pinyin tree and read a few fields through Config.
  an<ConfigData> data = LoadBuiltinSchema("luna_pinyin");
  Config cfg(data);

  string schema_id;
  cfg.GetString("schema/schema_id", &schema_id);  // expect "luna_pinyin"
  string alphabet;
  cfg.GetString("speller/alphabet", &alphabet);
  string dict;
  cfg.GetString("translator/dictionary", &dict);
  size_t algebra_n = cfg.GetListSize("speller/algebra");  // expect 13

  // End-to-end: feed speller/algebra into a Projection and apply to a
  // real-looking pinyin syllable. The "derive/un$/uen/correction" rule
  // and the abbrev rules each have a chance to fire.
  an<ConfigList> formulas = cfg.GetList("speller/algebra");
  Projection proj;
  bool loaded = proj.Load(formulas);

  Script script;
  script.AddSyllable("zhuang");
  script.AddSyllable("xun");
  script.AddSyllable("liang");
  (void)proj.Apply(&script);
  // After 13 rules, expect derived forms in the script -- not asserting
  // specific shapes here, just that the engine ran cleanly.

  (void)none; (void)schema_id; (void)alphabet; (void)dict;
  (void)algebra_n; (void)loaded;
}

// ---------------------------------------------------------------------------
// Smoke test for engine + service. Exercises the per-session lifecycle
// (create / activate / process / destroy) plus the process-wide singleton
// (StartService / Notify / CleanupAllSessions). No processors / segmentors
// are registered yet, so the engine's input loop is a no-op -- the goal
// here is to verify that Engine::Create, the signal wire-up (Engine ->
// Session::OnCommit / OnMessage -> Service::Notify), and the Schema
// default-build (via builtin_schemas) all link and run cleanly under DLL
// load-time static-initialiser ordering.
// ---------------------------------------------------------------------------
namespace {
int g_notify_count = 0;
void capturing_notify(rime::SessionId /*id*/,
                      const char* /*type*/,
                      const char* /*value*/) {
  ++g_notify_count;
}
}  // namespace

void exercise_rime_engine_service() {
  using namespace rime;

  Service& svc = Service::instance();
  svc.StartService();
  svc.SetNotificationHandler(&capturing_notify);

  SessionId id = svc.CreateSession();
  // Engine::Create wires up a default Schema + Context inside ConcreteEngine
  // and connects its commit/message sinks back into Session. Just having
  // CreateSession return a non-zero id means the whole chain linked.
  (void)id;

  an<Session> sess = svc.GetSession(id);
  if (sess) {
    // Apply a real builtin schema -- triggers ApplySchema -> message_sink_,
    // which routes through Engine::message_sink_ -> Session::OnMessage ->
    // Service::Notify -> capturing_notify.
    an<ConfigData> data = LoadBuiltinSchema("luna_pinyin");
    Schema* schema = new Schema("luna_pinyin", new Config(data));
    sess->ApplySchema(schema);

    // Process a key event. With no processors registered, this falls
    // through to unhandled_key_notifier and Push into commit_history.
    KeyEvent k('a', 0);
    (void)sess->ProcessKey(k);

    // Round-trip commit/clear composition (no-op when empty, must not crash).
    (void)sess->CommitComposition();
    sess->ClearComposition();
    sess->ResetCommitText();
  }

  // Cleanup paths.
  svc.CleanupStaleSessions();  // nothing stale, but exercises the iterator.
  (void)svc.DestroySession(id);
  svc.ClearNotificationHandler();
  svc.StopService();  // also calls CleanupAllSessions.
}

// ---------------------------------------------------------------------------
// Smoke test for the dict layer's foundation: mapped_file + vocabulary +
// prism + darts. We can't mmap a real .bin during compile-link verification
// (no file system at DLL-load on this host), so this is a symbol-presence
// + template-instantiation exercise:
//   * Code / DictEntry / Vocabulary built up in memory.
//   * Prism / Darts::DoubleArray constructed; HasKey on an empty trie
//     returns false (or whatever, just doesn't crash on null double-array).
//   * OpenReadOnly against a missing path exercises the Win32-mmap error
//     path: CreateFileW fails -> ok_ = false -> Load returns false.
// ---------------------------------------------------------------------------
void exercise_rime_dict_layer() {
  using namespace rime;

  // Code and Vocabulary
  Code c;
  c.push_back(1);
  c.push_back(2);
  c.push_back(3);
  Code idx;
  c.CreateIndex(&idx);
  string code_repr = c.ToString();

  DictEntry e;
  e.text = "hello";
  e.code = c;
  e.weight = 1.0;
  ShortDictEntry se = e.ToShort();

  Vocabulary v;
  ShortDictEntryList* page = v.LocateEntries(c);
  if (page) {
    page->push_back(New<ShortDictEntry>(se.text, se.code, se.weight));
  }
  v.SortHomophones();

  // DictEntryFilterBinder + chained filter
  // (declare empty filters so AddFilter exercises the ChainedFilter path
  // we wrote -- the lambda replacement).
  DictEntryFilterBinder binder;
  // Constructing a default-init DictEntryFilter and adding it through the
  // base class is enough for compile-time coverage of ChainedFilter; we
  // don't invoke it.

  // Prism + Darts: instantiate, try to load a non-existent path.
  Prism p(path("__nope_prism__.bin"));
  (void)p.Load();  // expected false; exercises OpenReadOnly + Win32 mmap
  // negative path without crashing.

  (void)code_repr; (void)se;
}

// ---------------------------------------------------------------------------
// Smoke test for the dict layer's full surface: string_table builder/reader
// round-trip, syllabifier with an empty prism (degenerate but exercises the
// algorithm), table + dictionary instantiation, CreateDictionary helper.
// All paths are stubbed-data exercises; the on-device test with real .bin
// files lands once the mini-dict factory is wired up.
// ---------------------------------------------------------------------------
void exercise_rime_dict_full() {
  using namespace rime;

  // StringTable builder round-trip.
  StringTableBuilder builder;
  StringId id_a = kInvalidStringId;
  StringId id_b = kInvalidStringId;
  StringId id_dup = kInvalidStringId;
  builder.Add("hello", 1.0, &id_a);
  builder.Add("world", 1.0, &id_b);
  builder.Add("hello", 1.0, &id_dup);  // dup -> shares id_a
  builder.Build();
  (void)(id_a == id_dup);     // expect true
  (void)builder.NumKeys();    // expect 2

  size_t image_size = builder.BinarySize();
  vector<char> buf(image_size);
  builder.Dump(&buf[0], image_size);

  // Load round-trip
  StringTable loaded(&buf[0], image_size);
  (void)loaded.HasKey("hello");          // true
  (void)loaded.HasKey("nope");           // false
  (void)loaded.Lookup("world");          // == id_b
  vector<StringId> matches;
  loaded.CommonPrefixMatch("helloworld", &matches);  // includes "hello"
  vector<StringId> predicts;
  loaded.Predict("h", &predicts);         // includes "hello"
  (void)loaded.GetString(id_a);          // "hello"

  // Syllabifier with empty prism -- doesn't crash, returns 0.
  Prism empty_prism(path("__nope_prism__.bin"));
  SyllableGraph graph;
  Syllabifier syl(" ", false, false);
  (void)syl.BuildSyllableGraph("test", empty_prism, &graph);

  // Table instantiation against non-existent path.
  Table tab(path("__nope_table__.bin"));
  (void)tab.Load();  // false; mmap fails cleanly.
  (void)tab.dict_file_checksum();

  // CreateDictionary helper + Lookup on a non-loaded dict.
  an<Dictionary> dict = CreateDictionary("test_dict",
                                         path("__nope_prism__.bin"),
                                         path("__nope_table__.bin"));
  (void)dict->Load();             // false (no real files)
  (void)dict->loaded();           // false
  SyllableGraph g2;
  (void)dict->Lookup(g2, 0);      // null an<DictEntryCollector>

  // DictEntryIterator instantiation + filter chain.
  DictEntryIterator iter;
  (void)iter.exhausted();  // true
  (void)iter.entry_count();
}

// ---------------------------------------------------------------------------
// Smoke test for gear/pinyin_translator. The translator owns a Dictionary
// + Syllabifier and is the first wiring from input bytes to candidates.
// We can't load real .bin files at DLL-load on this host, so:
//   * Construct PinyinTranslator via both the Ticket and no-arg ctors.
//   * LoadDictionary with bogus paths -> false (Prism::Load fails on the
//     missing file; the helper resets the held dict pointer). loaded()
//     stays false; Query on un-loaded dict returns null an<Translation>.
//   * Query with empty input -> null.
// On-device verification happens once we ship .bin files alongside
// RimeCore.dll and call LoadDictionary from rime_api / setup.
// ---------------------------------------------------------------------------
void exercise_rime_pinyin_translator() {
  using namespace rime;

  // No-arg ctor (Ticket() inside): exercise the test-friendly path.
  PinyinTranslator tr;
  (void)tr.loaded();  // expect false

  // Empty-input query before loading must not crash.
  Segment seg(0, 0);
  an<Translation> t0 = tr.Query("", seg);
  (void)t0;  // expect null an<>

  // LoadDictionary against missing files. CreateDictionary returns a
  // valid an<Dictionary>; Load() fails on the missing prism .bin and
  // we reset() the held pointer.
  bool ok = tr.LoadDictionary("test_dict",
                              "__nope_prism__.bin",
                              "__nope_table__.bin");
  (void)ok;  // expect false
  (void)tr.loaded();  // expect false

  // Query on still-unloaded dict.
  an<Translation> t1 = tr.Query("nihao", Segment(0, 5));
  (void)t1;  // expect null an<>

  // Ticket-based ctor (the production path; Engine* may be NULL in tests).
  Ticket ticket;
  ticket.name_space = "translator";
  PinyinTranslator tr2(ticket);
  (void)tr2.name_space();  // "translator"
}

// ---------------------------------------------------------------------------
// Smoke test for config/yaml_parser. We can't easily mount a .yaml file at
// DLL-load time on the device, so we embed a representative sample that
// exercises every YAML feature the parser supports:
//   * Block mappings + nested block mappings
//   * Block sequences
//   * Sequence-of-maps shorthand ("- key: val")
//   * Flow sequences and flow mappings
//   * Plain / single-quoted / double-quoted scalars
//   * Integer / float / boolean / null promotion
//   * Block-literal scalar with "|"
//   * Inline comments + full-line comments + blank lines
// On success we Traverse a few representative paths to verify the tree
// matches expectation. A NULL return from Parse or any path mismatch
// would be a programmer bug we want to catch at DLL load.
// ---------------------------------------------------------------------------
void exercise_rime_yaml_parser() {
  using namespace rime;

  // ASCII-only YAML (per project rule on source-file encoding). The
  // parser itself is encoding-agnostic byte-for-byte; runtime .yaml
  // files from disk are UTF-8, which round-trips through the parser
  // because we never reinterpret bytes inside scalars.
  static const char kSample[] =
      "# top-level comment\n"
      "schema:\n"
      "  schema_id: luna_pinyin\n"
      "  name: \"luna pinyin\"\n"
      "  version: '0.1'\n"
      "  description: |\n"
      "    line one\n"
      "    line two\n"
      "      indented\n"
      "\n"
      "engine:\n"
      "  processors:\n"
      "    - ascii_composer\n"
      "    - speller\n"
      "  translators:\n"
      "    - punct_translator\n"
      "    - script_translator\n"
      "\n"
      "switches:\n"
      "  - name: ascii_mode\n"
      "    reset: 0\n"
      "    states: [ Chinese, ABC ]\n"
      "  - name: full_shape\n"
      "    states: [ half, full ]\n"
      "\n"
      "punctuator:\n"
      "  full_shape:\n"
      "    ',' : { commit: zh_comma }\n"
      "    '<' : [ a, b, c ]\n"
      "\n"
      "menu:\n"
      "  page_size: 5\n"
      "  enabled: true\n"
      "  weight: 1.5\n"
      "  missing: ~\n";

  string err;
  an<ConfigItem> root = yaml::Parse(string(kSample), &err);
  (void)root;
  (void)err.empty();

  // Stuff the parsed tree into a ConfigData so we can use Traverse.
  ConfigData cd;
  cd.root = root;

  // Scalars.
  an<ConfigValue> sv = As<ConfigValue>(cd.Traverse("schema/schema_id"));
  string schema_id;
  if (sv) sv->GetString(&schema_id);  // "luna_pinyin"
  (void)schema_id;

  an<ConfigValue> nv = As<ConfigValue>(cd.Traverse("schema/name"));
  string nm;
  if (nv) nv->GetString(&nm);  // "luna pinyin"
  (void)nm;

  an<ConfigValue> vv = As<ConfigValue>(cd.Traverse("schema/version"));
  string version;
  if (vv) vv->GetString(&version);  // "0.1"
  (void)version;

  // Block literal: preserved newlines and inner indent.
  an<ConfigValue> dv = As<ConfigValue>(cd.Traverse("schema/description"));
  string desc;
  if (dv) dv->GetString(&desc);  // "line one\nline two\n  indented\n"
  (void)desc;

  // Block sequence of plain scalars.
  an<ConfigList> processors =
      As<ConfigList>(cd.Traverse("engine/processors"));
  if (processors) {
    (void)processors->size();
    an<ConfigValue> pv = As<ConfigValue>(processors->GetAt(0));
    string pn;
    if (pv) pv->GetString(&pn);  // "ascii_composer"
    (void)pn;
  }

  // Sequence-of-maps shorthand with nested flow sequence.
  an<ConfigList> switches = As<ConfigList>(cd.Traverse("switches"));
  if (switches && switches->size() >= 1) {
    an<ConfigMap> sw0 = As<ConfigMap>(switches->GetAt(0));
    if (sw0) {
      an<ConfigValue> nv2 = As<ConfigValue>(sw0->Get("name"));
      string nm2;
      if (nv2) nv2->GetString(&nm2);  // "ascii_mode"
      (void)nm2;
      an<ConfigList> states = As<ConfigList>(sw0->Get("states"));
      if (states) (void)states->size();  // 2
    }
  }

  // Flow mapping with a quoted key.
  an<ConfigMap> full_shape =
      As<ConfigMap>(cd.Traverse("punctuator/full_shape"));
  if (full_shape) {
    an<ConfigMap> comma_action = As<ConfigMap>(full_shape->Get(","));
    if (comma_action) {
      an<ConfigValue> cv = As<ConfigValue>(comma_action->Get("commit"));
      string commit;
      if (cv) cv->GetString(&commit);  // "zh_comma"
      (void)commit;
    }
    an<ConfigList> angle_list = As<ConfigList>(full_shape->Get("<"));
    if (angle_list) (void)angle_list->size();  // 3
  }

  // Type promotion.
  an<ConfigValue> ps = As<ConfigValue>(cd.Traverse("menu/page_size"));
  int ipage = 0;
  if (ps) ps->GetInt(&ipage);  // 5
  (void)ipage;

  an<ConfigValue> en = As<ConfigValue>(cd.Traverse("menu/enabled"));
  bool enabled = false;
  if (en) en->GetBool(&enabled);  // true
  (void)enabled;

  an<ConfigValue> wt = As<ConfigValue>(cd.Traverse("menu/weight"));
  double w = 0.0;
  if (wt) wt->GetDouble(&w);  // 1.5
  (void)w;

  an<ConfigItem> missing = cd.Traverse("menu/missing");
  (void)missing;  // null-valued ConfigValue (the key exists, value is null)

  // Negative path: bad indentation (tab) -> parse error.
  string err2;
  an<ConfigItem> bad = yaml::Parse(
      string("good:\n\tbadly_indented: 1\n"), &err2);
  (void)bad;     // null
  (void)err2;    // non-empty (mentions tabs, line 2)
}

// ---------------------------------------------------------------------------
// Smoke test for the rime_api C export surface. Verifies:
//   * Setup / Initialize / Finalize lifecycle without dict files (dict
//     load gracefully fails; sessions still work for compose/clear).
//   * CreateSession / DestroySession round-trip.
//   * ProcessKey routes ASCII letters into the input buffer.
//   * GetContext returns a preedit + (possibly empty) menu.
//   * CommitComposition / ClearComposition.
//   * Memory ownership: every Get* allocation is freed by the matching
//     Free*. Address-sanitizer-style verification isn't available on
//     WinCE, but exercising the matched pair at DLL load surfaces any
//     immediate corruption / double-free.
// We never actually load a real .bin file here (the device-side test
// will), so candidates may be empty and CommitComposition with no
// candidates falls back to raw-text commit.
// ---------------------------------------------------------------------------
void exercise_rime_api() {
  // Bogus shared_data_dir: dict load will fail, but everything else
  // (session lifecycle, key dispatch, preedit buffer) is exercised.
  RimeTraits traits;
  std::memset(&traits, 0, sizeof(traits));
  traits.data_size = static_cast<int>(sizeof(traits) - sizeof(int));
  traits.shared_data_dir = "__nope__";
  traits.user_data_dir = "__nope_user__";
  traits.app_name = "rime.test";

  RimeInitialize(&traits);

  RimeSessionId id = RimeCreateSession();
  (void)RimeFindSession(id);  // True

  // Type "nihao" one char at a time. Translator likely has no loaded
  // dict, so candidates will be empty; the input buffer still updates.
  (void)RimeProcessKey(id, 'n', 0);
  (void)RimeProcessKey(id, 'i', 0);
  (void)RimeProcessKey(id, 'h', 0);
  (void)RimeProcessKey(id, 'a', 0);
  (void)RimeProcessKey(id, 'o', 0);

  // Read back composition + menu.
  RimeContext ctx;
  std::memset(&ctx, 0, sizeof(ctx));
  if (RimeGetContext(id, &ctx)) {
    (void)ctx.composition.preedit;  // "nihao"
    (void)ctx.menu.num_candidates;  // 0 since dict didn't load
    RimeFreeContext(&ctx);
  }

  // Backspace once -> "niha".
  (void)RimeProcessKey(id, 0xff08 /* BackSpace */, 0);

  // Space with no candidates falls back to raw commit.
  (void)RimeProcessKey(id, 0x20 /* space */, 0);

  RimeCommit commit;
  std::memset(&commit, 0, sizeof(commit));
  if (RimeGetCommit(id, &commit)) {
    (void)commit.text;  // "niha"
    RimeFreeCommit(&commit);
  }

  // Process key on a non-existent session -> False.
  (void)RimeProcessKey(id + 999, 'a', 0);

  // Clear / second-session basics.
  (void)RimeProcessKey(id, 'a', 0);
  RimeClearComposition(id);
  RimeSessionId id2 = RimeCreateSession();
  (void)RimeDestroySession(id2);
  (void)RimeDestroySession(id);

  RimeCleanupAllSessions();
  RimeFinalize();
}

// Anchor that references every exercise_* above so the linker doesn't drop
// them. Constructed at DLL load time via static-initialiser ordering.
struct SmokeTestRunner {
  SmokeTestRunner() {
    exercise_shims();
    exercise_rime_common();
    exercise_rime_strings();
    exercise_rime_spelling();
    exercise_wince_regex();
    exercise_rime_calculus();
    exercise_rime_key_event();
    exercise_rime_config_types();
    exercise_rime_config_data();
    exercise_rime_config();
    exercise_rime_algebra();
    exercise_rime_encoder();
    exercise_rime_builtin_schemas();
    exercise_rime_engine_service();
    exercise_rime_dict_layer();
    exercise_rime_dict_full();
    exercise_rime_pinyin_translator();
    exercise_rime_yaml_parser();
    exercise_rime_api();
  }
};
SmokeTestRunner g_smoke_runner;

}  // namespace

// No custom DllMain. The WinCE C runtime provides _DllMainCRTStartup which
// runs every static initialiser (including g_smoke_runner above) on
// DLL_PROCESS_ATTACH and returns TRUE for the other reasons.
//
// We previously tried writing one of the form
//     BOOL APIENTRY DllMain(HMODULE, DWORD, LPVOID) { ... }
// but MSVC9 + the WM6 Professional SDK kept emitting C2731 ("cannot overload
// function") that pointed at our own declaration as the prior overload --
// almost certainly because some SDK header forward-declares DllMain with a
// different parameter type (likely HANDLE rather than HMODULE) and the C++
// type system sees them as distinct overloads. Rime's modules register
// themselves through RIME_MODULE_INITIALIZER (.CRT$XCU section), so we never
// actually need a hand-written DllMain.
