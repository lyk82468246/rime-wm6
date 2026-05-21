---
name: YAML parser scope
description: What the hand-written YAML parser supports and what's intentionally left out
type: project
originSessionId: 3ffd7d86-f40d-4114-9d5d-4a911b8d6d89
---
`src/librime_wince/src/rime/config/yaml_parser.{h,cc}` is a ~600-line hand-written YAML parser, NOT a yaml-cpp port. It targets the subset rime's *.yaml files actually use.

**Supported:**
- Block mappings (indent-driven), block sequences (`- item`)
- Sequence-of-maps shorthand (`- key: val` + subsequent same-indent pairs)
- Flow mappings (`{ a: b }`) and flow sequences (`[ a, b, c ]`)
- Plain / single-quoted (`'`) / double-quoted (`"`) scalars
- Int / float / bool / null promotion (true/false/yes/no/null/~ etc.)
- Block literal scalars (`|`, `|-`, `|+`) with common-indent stripping
- Comments (`#`) outside quotes/flow brackets
- Optional `---` / `...` front-matter delimiters

**NOT supported (rime files don't use these):**
- Anchors `&foo` / aliases `*foo` / merge keys `<<: *foo`
- Folded scalars `>` (only `|` literal supported)
- Tag handles `!!int` etc. (types inferred from shape)
- Multi-document streams beyond optional front matter
- Tabs in indentation (treated as parse error)

**How to apply:** When loading a rime .yaml fails, first check if it uses any UNSUPPORTED feature above. If it does, either rewrite the .yaml to use plain block style, or extend yaml_parser.cc. Don't reach for yaml-cpp -- the whole point was to avoid that dependency on WinCE.

`ConfigData::LoadFromStream` and `LoadFromFile` are now wired up; both delegate to `yaml::Parse`. `SaveTo*` are still stubbed because rime rarely needs to emit YAML at runtime (only the deployer does, and the deployer is cut for MVP).
