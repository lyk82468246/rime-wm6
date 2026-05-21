//
// rime/config/yaml_parser.h -- minimal YAML parser for the WinCE port.
//
// Parses the subset of YAML that rime's *.yaml files actually use:
//   * Block-style mappings ("key: value", indentation-driven nesting)
//   * Block-style sequences ("- item")
//   * Flow mappings ("{ a: b, c: d }")
//   * Flow sequences ("[ a, b, c ]")
//   * Scalars: plain, single-quoted ('...'), double-quoted ("...")
//   * Integers / floats / booleans / null promoted to ConfigValue
//   * Comments ("# ...") -- stripped outside quotes
//   * Block literal scalars ("|") -- preserves newlines, indent-stripped
//   * Document front-matter delimiters "---" / "..."
//
// Not supported (rime's files don't use these):
//   * Anchors (&foo) / aliases (*foo) / merge keys (<<: *foo)
//   * Folded scalars (">") -- only "|" is supported
//   * Tag handles (!!int etc.) -- types inferred from scalar shape
//   * Multi-document streams beyond an optional front matter at the top
//   * Tabs for indentation (YAML disallows them; we treat as error)
//
// On parse error, Parse returns a null an<ConfigItem> and (if `error`
// is non-NULL) writes a one-line "line N: <reason>" diagnostic into it.
// We don't throw; the wince_compat exception story isn't worth the
// complexity for a config loader.
//
#ifndef RIME_YAML_PARSER_H_
#define RIME_YAML_PARSER_H_

#include <rime/common.h>

namespace rime {

class ConfigItem;

namespace yaml {

// Parse a YAML document; returns null an<> on syntax error.
// `error` (optional) receives a human-readable diagnostic on failure.
an<ConfigItem> Parse(const string& text, string* error = NULL);

}  // namespace yaml
}  // namespace rime

#endif  // RIME_YAML_PARSER_H_
