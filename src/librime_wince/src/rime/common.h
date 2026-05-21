//
// Copyright RIME Developers
// Distributed under the BSD License
//
// 2011-03-14 GONG Chen <chen.sst@gmail.com>
// 2026-05    WinCE port: rewrite for C++03 / MSVC 9.0 (Visual Studio 2008).
//
// This is the WinCE-port replacement for upstream
// src/librime/src/rime/common.h. Same identifiers (`an<T>`, `the<T>`,
// `of<T>`, `weak<T>`, `New<T>(...)`, `As<X>(...)`, `Is<X>(...)`, `path`,
// `signal`, `connection`, `function`, `hash_map`, `hash_set`, ...), same
// semantics where C++03 permits, backed by wince_compat shims instead of
// <memory> / <functional> / <filesystem> / boost.
//
// Why not use the upstream header: it relies on C++11 template aliases,
// variadic templates, perfect forwarding, std::shared_ptr / unique_ptr,
// std::filesystem::path, and boost::signals2 / boost::unordered_*. None of
// those are available with MSVC 9.0.
//
#ifndef RIME_COMMON_H_
#define RIME_COMMON_H_

#include <rime/build_config.h>

// Standard library facilities that MSVC 9.0's Dinkumware STL does provide.
#include <list>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

// wince_compat replaces <memory>, <functional>, <filesystem> and
// boost::signals2 with hand-written C++03 shims. See src/wince_compat/.
#include "wince_compat.h"

// Logging. RIME_ENABLE_LOGGING is intentionally undefined in build_config.h,
// so we always pull in the no-op stub. Every LOG/CHECK macro collapses to a
// void expression.
#include <rime/no_logging.h>

// Convenience macros for calling pointer-to-member-function on `this`. Copied
// verbatim from upstream; used by gear/editor and gear/key_binding_processor.
#define RIME_THIS_CALL(f) (this->*(f))
#define RIME_THIS_CALL_AS(T, f) ((T*)this->*(f))

namespace rime {

// ---------------------------------------------------------------------------
// Standard library names imported into the rime namespace.
// ---------------------------------------------------------------------------
using std::list;
using std::make_pair;
using std::map;
using std::pair;
using std::set;
using std::string;
using std::vector;

// ---------------------------------------------------------------------------
// wince_compat replacements imported into the rime namespace.
// ---------------------------------------------------------------------------
using wince::function;
using wince::signal;
using wince::connection;

// ---------------------------------------------------------------------------
// hash_map / hash_set
//
// MSVC 9.0 has no std::unordered_map / unordered_set, and boost::unordered_*
// won't compile here. We substitute std::map / std::set (red-black tree). The
// performance hit on the IME's small per-session maps is negligible.
//
// These must be class templates rather than typedefs because upstream uses
// them as base classes (e.g. `class Corrections : public hash_map<...>` in
// dict/corrector.h).
// ---------------------------------------------------------------------------
template <class Key, class T>
class hash_map : public std::map<Key, T> {};

template <class T>
class hash_set : public std::set<T> {};

// ---------------------------------------------------------------------------
// Smart pointer aliases: an<T>, the<T>, of<T>, weak<T>.
//
// Upstream defines these with C++11 template aliases (`using an = ...`),
// which MSVC 9.0 lacks. We model them as class templates inheriting from the
// wince_compat shim. The trick that makes this transparent:
//
//   * Sibling aliases (an, the, of) all inherit from the same
//     wince::shared_ptr<T>, so passing one where another is expected
//     succeeds via standard derived-to-base conversion plus our templated
//     `shared_ptr<U>` constructor below.
//   * The templated `template<class U> X(const wince::shared_ptr<U>&)`
//     constructor handles upcasts (e.g. `an<Base> = an<Derived>`). C++03
//     [temp.deduct.call] 14.8.2.1p4 specifically allows U to be deduced from
//     a derived class of `wince::shared_ptr<U>`.
//   * No data members are added, so derived-to-base slicing is harmless.
//
// upstream `the<T> = std::unique_ptr<T>` is downgraded to shared ownership
// here. Manual audit confirms librime never relies on the unique-ness of a
// `the<T>` (no move-return into a container, no transfer-of-ownership API).
// ---------------------------------------------------------------------------
template <class T>
class an : public wince::shared_ptr<T> {
 public:
  an() {}
  an(const wince::shared_ptr<T>& o) : wince::shared_ptr<T>(o) {}
  template <class U>
  an(const wince::shared_ptr<U>& o) : wince::shared_ptr<T>(o) {}
};

template <class T>
class the : public wince::shared_ptr<T> {
 public:
  the() {}
  the(const wince::shared_ptr<T>& o) : wince::shared_ptr<T>(o) {}
  template <class U>
  the(const wince::shared_ptr<U>& o) : wince::shared_ptr<T>(o) {}
};

// Upstream defines `template <class T> using of = an<T>;` -- `of` and `an`
// are the same type. C++03 has no template aliases, so we model `of<T>`
// as a DERIVED CLASS of `an<T>`. This preserves the assumption that
// "an<T>&" binds to elements of "vector<of<T> >" via derived-to-base
// reference conversion. Methods like Dictionary::primary_table() rely on
// this -- returning `const an<Table>&` from a `vector<of<Table> >` slot
// would otherwise dangle.
template <class T>
class of : public an<T> {
 public:
  of() {}
  of(const wince::shared_ptr<T>& o) : an<T>(o) {}
  template <class U>
  of(const wince::shared_ptr<U>& o) : an<T>(o) {}
};

template <class T>
class weak : public wince::weak_ptr<T> {
 public:
  weak() {}
  weak(const wince::weak_ptr<T>& o) : wince::weak_ptr<T>(o) {}
  // Allow `weak<T> w = some_an_or_shared_ptr;`. The U deduction works through
  // the derived-class-deduces-base-template rule.
  template <class U>
  weak(const wince::shared_ptr<U>& s) : wince::weak_ptr<T>(s) {}
  template <class U>
  weak(const wince::weak_ptr<U>& o) : wince::weak_ptr<T>(o) {}
};

// ---------------------------------------------------------------------------
// Pointer cast helpers: As<X>(p) and Is<X>(p).
//
// We accept `const wince::shared_ptr<Y>&` (rather than `const an<Y>&`) so
// that the function works for any of the alias wrappers above via the
// derived-class deduction rule.
// ---------------------------------------------------------------------------
template <class X, class Y>
inline an<X> As(const wince::shared_ptr<Y>& ptr) {
  return an<X>(wince::dynamic_pointer_cast<X>(ptr));
}

template <class X, class Y>
inline bool Is(const wince::shared_ptr<Y>& ptr) {
  return wince::dynamic_pointer_cast<X>(ptr).get() != 0;
}

// ---------------------------------------------------------------------------
// New<T>(...): make_shared replacement, returning an<T>.
//
// Upstream uses variadic templates + std::forward. C++03 has neither, so we
// provide 0..5 const-reference overloads. Any rime code that requires
// non-const-lvalue construction can call `an<T>(new T(arg))` directly.
// ---------------------------------------------------------------------------
template <class T>
inline an<T> New() {
  return an<T>(wince::make_shared<T>());
}
template <class T, class A1>
inline an<T> New(const A1& a1) {
  return an<T>(wince::make_shared<T, A1>(a1));
}
template <class T, class A1, class A2>
inline an<T> New(const A1& a1, const A2& a2) {
  return an<T>(wince::make_shared<T, A1, A2>(a1, a2));
}
template <class T, class A1, class A2, class A3>
inline an<T> New(const A1& a1, const A2& a2, const A3& a3) {
  return an<T>(wince::make_shared<T, A1, A2, A3>(a1, a2, a3));
}
template <class T, class A1, class A2, class A3, class A4>
inline an<T> New(const A1& a1, const A2& a2, const A3& a3, const A4& a4) {
  return an<T>(wince::make_shared<T, A1, A2, A3, A4>(a1, a2, a3, a4));
}
template <class T, class A1, class A2, class A3, class A4, class A5>
inline an<T> New(const A1& a1, const A2& a2, const A3& a3,
                 const A4& a4, const A5& a5) {
  return an<T>(wince::make_shared<T, A1, A2, A3, A4, A5>(a1, a2, a3, a4, a5));
}

// ---------------------------------------------------------------------------
// path: typedef onto wince::path.
//
// Upstream wraps std::filesystem::path with UTF-8-aware constructors. Our
// wince::path already stores UTF-8 internally and exposes the same API
// surface that the kept librime call sites need (operator/=, filename(),
// extension(), parent_path(), wstring()), so a typedef suffices.
// ---------------------------------------------------------------------------
typedef wince::path path;

}  // namespace rime

#endif  // RIME_COMMON_H_
