//
// Copyright RIME Developers
// Distributed under the BSD License
//
// build_config.h -- hand-crafted for the Windows Mobile 6.5 / WinCE 6.0 port.
// Replaces the upstream CMake-generated build_config.h.in.
//
// All feature toggles for the WinCE port are pinned here. Code paths in the
// engine select between upstream behaviour and WinCE behaviour based on the
// symbols defined below. To re-enable a trimmed feature, undef its
// RIME_NO_* / RIME_WINCE_* gate here and provide the dependency.
//
#ifndef RIME_BUILD_CONFIG_H_
#define RIME_BUILD_CONFIG_H_

// ---------------------------------------------------------------------------
// Sanity check: this header is *only* for the WinCE port. If you are building
// on a desktop OS you almost certainly want the CMake-generated version.
// ---------------------------------------------------------------------------
#if !defined(_WIN32_WCE) && !defined(UNDER_CE)
#error "rime/build_config.h in librime_wince is only valid when targeting \
Windows CE / Windows Mobile. For desktop builds, use the upstream librime tree \
with its CMake-generated build_config.h."
#endif

// ---------------------------------------------------------------------------
// Platform identification
// ---------------------------------------------------------------------------
// Identifies the WinCE port. New code may #ifdef on this to select WinCE-only
// paths (e.g. CRITICAL_SECTION instead of std::mutex).
#define RIME_PLATFORM_WINCE 1

// ---------------------------------------------------------------------------
// Language level: MSVC 9.0 (Visual Studio 2008), C++03.
// No <chrono>, <thread>, <mutex>, <atomic>, <filesystem>, <regex>, <random>.
// No auto, nullptr, lambda, range-for, template aliases, variadic templates,
// rvalue references, std::shared_ptr / unique_ptr, std::function.
// Headers in librime_wince select C++03 implementations via this symbol.
// ---------------------------------------------------------------------------
#define RIME_CXX03 1

// ---------------------------------------------------------------------------
// Logging
// ---------------------------------------------------------------------------
// Intentionally do NOT define RIME_ENABLE_LOGGING. The existing
// no_logging.h stub in upstream librime provides VoidLogger and turns every
// LOG/CHECK macro into a no-op. Saves the glog dependency entirely.
// #undef RIME_ENABLE_LOGGING

// ---------------------------------------------------------------------------
// Concurrency: deployment / maintenance runs synchronously on the calling
// thread. The device never runs the dictionary compiler, so there is no
// background work to push off the UI thread.
// ---------------------------------------------------------------------------
#define RIME_NO_THREADING 1

// ---------------------------------------------------------------------------
// Build timestamps: <chrono> is not available, and reproducible builds are
// preferable anyway.
// ---------------------------------------------------------------------------
#define RIME_NO_TIMESTAMP 1

// ---------------------------------------------------------------------------
// MVP feature trim gates. Each symbol below selects the WinCE-port code path
// that stubs out (or replaces with a binary-blob reader) an upstream feature
// whose dependency is too heavy or platform-incompatible for WinCE.
// ---------------------------------------------------------------------------

// OpenCC simplified/traditional conversion. Pulls a multi-MB dictionary and
// modern C++. Cut entirely; users get either S or T per schema.
#define RIME_NO_OPENCC 1

// LevelDB-backed user dictionary (learned phrases, user-defined entries).
// LevelDB relies on memory-mapped files and POSIX-style file locks, neither
// of which behave correctly on WinCE. First-pass port is read-only.
#define RIME_NO_USER_DB 1

// On-device dictionary compiler. Builds prism.bin / table.bin from .dict.yaml.
// Far too slow on ARMV4I and depends on yaml-cpp. We precompile on the PC.
#define RIME_NO_DICT_COMPILER 1

// yaml-cpp. Replaced by a PC-side tool that emits our own compact binary
// schema format; the device only deserializes that format.
#define RIME_NO_YAML 1

// ---------------------------------------------------------------------------
// Data directories. On Windows Mobile, applications conventionally live under
// "\Program Files\<AppName>\". These are compile-time defaults; runtime
// overrides come through the shared_data_dir / user_data_dir fields of
// RimeTraits (see rime_api.h).
//
// NB: double backslashes are C string escapes; the actual path stored at run
// time is single-backslash.
// ---------------------------------------------------------------------------
#define RIME_DATA_DIR    "\\Program Files\\WMRime\\data"
#define RIME_PLUGINS_DIR "\\Program Files\\WMRime\\plugins"

#endif  // RIME_BUILD_CONFIG_H_
