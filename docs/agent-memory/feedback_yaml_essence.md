---
name: YAML config is rime's soul; allow short-term hardcoded fallback
description: User wants yaml-cpp restored in the long run, but accepts a temporary hardcoded ConfigData for the MVP
type: feedback
originSessionId: 3ffd7d86-f40d-4114-9d5d-4a911b8d6d89
---
YAML schema/config loading is considered the essence of rime by the user. The MVP cut of yaml-cpp is acceptable as a temporary fallback (hardcoded ConfigData baked into RimeCore.dll), but it MUST be revisited and replaced with a real YAML parser in a later phase -- not silently left as a permanent shortcut.

**Why:** User said on 2026-05-18: "你想要砍掉 yaml配置文件吗？我认为这是rime的精髓；不过考虑到难度，也许你可以先fallback到一个定死的". YAML defines user-editable schemas (luna_pinyin.schema.yaml etc.) and rime's __include/__patch composition; killing it permanently would gut the IME's user-facing flexibility.

**How to apply:**
- For now: port `config_types` (ConfigItem / ConfigValue / ConfigList / ConfigMap) as pure runtime data structures. Provide a hardcoded built-in default schema (luna_pinyin minimum) so the engine can boot. Skip yaml parsing and config_compiler entirely.
- Plan ahead: leave `Config::LoadFromFile()` as a stub that returns a hardcoded tree but keep the API shape compatible with the future real loader. No "this is permanent" shortcuts in API design.
- Long term (post-MVP): port a small C++03-compatible YAML subset reader (e.g. minimal yaml-cpp fork, or hand-write one -- yaml is simpler than regex). Restore `__include` / `__patch` composition. Revisit this memory then.
