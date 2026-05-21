//
// wince_compat/shared_ptr.h -- C++03 replacement for std::shared_ptr / weak_ptr
// / make_shared / pointer_cast for the librime WinCE port.
//
// Design summary:
//   - Non-intrusive: T does not need to derive from anything. A separate
//     control block holds the strong + weak ref counts and a virtual
//     destructor that disposes the managed object.
//   - Refcounts are atomic (InterlockedIncrement / -Decrement). Even though
//     RIME_NO_THREADING is defined for the engine, the IME shell may call in
//     from a different thread (e.g. the WH_KEYBOARD_LL hook thread).
//   - make_shared() allocates control + object separately (two `new`s). We
//     sacrifice the single-allocation optimisation of real std::make_shared
//     for ~80% fewer lines of code, and accept that allocator hooks aren't
//     needed.
//
// Lifetime model (standard shared_ptr semantics):
//   - One implicit "weak" count is held collectively by the strong count.
//     When strong drops to 0, the managed object is destroyed but the control
//     block stays alive for outstanding weak_ptrs.
//   - When the last weak count drops to 0, the control block is delete'd.
//
#ifndef WINCE_COMPAT_SHARED_PTR_H_
#define WINCE_COMPAT_SHARED_PTR_H_

#include <windows.h>
#include <cstddef>  // for NULL / size_t

namespace wince {

template <class T> class shared_ptr;
template <class T> class weak_ptr;

namespace detail {

class ref_count_base {
 public:
  ref_count_base() : strong_(1), weak_(1) {}
  virtual ~ref_count_base() {}

  void add_ref() {
    InterlockedIncrement(&strong_);
  }
  // Returns true iff this call dropped the strong count to 0
  // (and therefore disposed the object).
  bool release() {
    if (InterlockedDecrement(&strong_) == 0) {
      dispose();
      release_weak();  // drop the implicit weak ref held by the strong group
      return true;
    }
    return false;
  }

  void add_weak() {
    InterlockedIncrement(&weak_);
  }
  void release_weak() {
    if (InterlockedDecrement(&weak_) == 0) {
      delete this;
    }
  }

  // Atomically: if strong_ != 0, ++strong_ and return true. Used by
  // weak_ptr::lock() to upgrade to shared_ptr without racing against the
  // last strong release.
  bool try_add_ref() {
    LONG c = strong_;
    while (c != 0) {
      LONG observed = InterlockedCompareExchange(&strong_, c + 1, c);
      if (observed == c) return true;
      c = observed;
    }
    return false;
  }

  long use_count() const { return strong_; }

 protected:
  virtual void dispose() = 0;

 private:
  ref_count_base(const ref_count_base&);
  ref_count_base& operator=(const ref_count_base&);

  // WinCE's InterlockedXxx prototypes take LPLONG (i.e. plain LONG*), not
  // the desktop-Win32 `LONG volatile*` form. We therefore drop the volatile
  // qualifier here. The InterlockedXxx functions themselves act as memory
  // barriers, so we don't lose ordering guarantees. The only plain read of
  // strong_ (in use_count() and at the start of try_add_ref) is safe: 32-bit
  // aligned LONG reads are atomic on ARM, and try_add_ref's CAS will retry
  // if the value mutated.
  LONG strong_;
  LONG weak_;
};

// Control block for the "shared_ptr<T>(new T(...))" path: holds a raw pointer
// to a separately-heap-allocated T and deletes it on dispose.
template <class T>
class ref_count_owned : public ref_count_base {
 public:
  explicit ref_count_owned(T* p) : ptr_(p) {}
 protected:
  virtual void dispose() { delete ptr_; }
 private:
  T* ptr_;
};

}  // namespace detail

// ---------------------------------------------------------------------------
// shared_ptr
// ---------------------------------------------------------------------------
template <class T>
class shared_ptr {
  template <class U> friend class shared_ptr;
  template <class U> friend class weak_ptr;

 public:
  typedef T element_type;

  shared_ptr() : ptr_(NULL), ctl_(NULL) {}

  // Take ownership of a raw pointer. If you do this, NEVER also wrap the same
  // raw pointer in a different shared_ptr -- the control blocks won't agree
  // and you'll get a double-delete.
  template <class U>
  explicit shared_ptr(U* p) : ptr_(p), ctl_(NULL) {
    if (p) {
      // We allocate the control block separately. If `new` throws here, the
      // raw object would leak; on WinCE/MSVC9 with /EHsc this is acceptable
      // for the use cases in this port (no exception-safety contract).
      ctl_ = new detail::ref_count_owned<U>(p);
    }
  }

  shared_ptr(const shared_ptr& o) : ptr_(o.ptr_), ctl_(o.ctl_) {
    if (ctl_) ctl_->add_ref();
  }

  template <class U>
  shared_ptr(const shared_ptr<U>& o) : ptr_(o.ptr_), ctl_(o.ctl_) {
    if (ctl_) ctl_->add_ref();
  }

  // Aliasing constructor: produce a shared_ptr that shares ownership with
  // `o` but points to `aliased`. Used by static_pointer_cast etc.
  template <class U>
  shared_ptr(const shared_ptr<U>& o, T* aliased) : ptr_(aliased), ctl_(o.ctl_) {
    if (ctl_) ctl_->add_ref();
  }

  // Construct from a weak_ptr: only succeeds if the weak is still live.
  // (Defined out-of-line below, after weak_ptr is declared.)
  explicit shared_ptr(const weak_ptr<T>& w);

  ~shared_ptr() {
    if (ctl_) ctl_->release();
  }

  shared_ptr& operator=(const shared_ptr& o) {
    shared_ptr(o).swap(*this);
    return *this;
  }

  template <class U>
  shared_ptr& operator=(const shared_ptr<U>& o) {
    shared_ptr(o).swap(*this);
    return *this;
  }

  void reset() {
    shared_ptr().swap(*this);
  }
  template <class U>
  void reset(U* p) {
    shared_ptr(p).swap(*this);
  }

  void swap(shared_ptr& o) {
    T* tp = ptr_; ptr_ = o.ptr_; o.ptr_ = tp;
    detail::ref_count_base* tc = ctl_; ctl_ = o.ctl_; o.ctl_ = tc;
  }

  T* get() const { return ptr_; }
  T& operator*() const { return *ptr_; }
  T* operator->() const { return ptr_; }
  long use_count() const { return ctl_ ? ctl_->use_count() : 0; }
  bool unique() const { return use_count() == 1; }

  // Safe-bool idiom (C++03). Permits `if (sp) ...` without allowing
  // accidental conversions like `sp + 1`.
  typedef T* shared_ptr::*unspecified_bool_type;
  operator unspecified_bool_type() const {
    return ptr_ ? &shared_ptr::ptr_ : NULL;
  }

 private:
  // Used by make_shared and the cast helpers below.
  shared_ptr(T* p, detail::ref_count_base* ctl) : ptr_(p), ctl_(ctl) {}

  T* ptr_;
  detail::ref_count_base* ctl_;

  // Friends for the cast helpers (need to invoke the private ctor above).
  template <class X, class Y>
  friend shared_ptr<X> static_pointer_cast(const shared_ptr<Y>& o);
  template <class X, class Y>
  friend shared_ptr<X> dynamic_pointer_cast(const shared_ptr<Y>& o);
};

template <class T, class U>
inline bool operator==(const shared_ptr<T>& a, const shared_ptr<U>& b) {
  return a.get() == b.get();
}
template <class T, class U>
inline bool operator!=(const shared_ptr<T>& a, const shared_ptr<U>& b) {
  return a.get() != b.get();
}
template <class T>
inline bool operator==(const shared_ptr<T>& a, int null_literal) {
  // Allows `if (sp == 0)`. The int parameter is just the literal 0.
  (void)null_literal;
  return a.get() == NULL;
}
template <class T>
inline bool operator!=(const shared_ptr<T>& a, int null_literal) {
  (void)null_literal;
  return a.get() != NULL;
}

// ---------------------------------------------------------------------------
// weak_ptr
// ---------------------------------------------------------------------------
template <class T>
class weak_ptr {
  template <class U> friend class shared_ptr;
  template <class U> friend class weak_ptr;

 public:
  weak_ptr() : ptr_(NULL), ctl_(NULL) {}

  weak_ptr(const weak_ptr& o) : ptr_(o.ptr_), ctl_(o.ctl_) {
    if (ctl_) ctl_->add_weak();
  }

  template <class U>
  weak_ptr(const shared_ptr<U>& s) : ptr_(s.ptr_), ctl_(s.ctl_) {
    if (ctl_) ctl_->add_weak();
  }

  template <class U>
  weak_ptr(const weak_ptr<U>& o) : ptr_(o.ptr_), ctl_(o.ctl_) {
    if (ctl_) ctl_->add_weak();
  }

  ~weak_ptr() {
    if (ctl_) ctl_->release_weak();
  }

  weak_ptr& operator=(const weak_ptr& o) {
    weak_ptr(o).swap(*this);
    return *this;
  }
  template <class U>
  weak_ptr& operator=(const shared_ptr<U>& s) {
    weak_ptr(s).swap(*this);
    return *this;
  }

  void reset() { weak_ptr().swap(*this); }
  void swap(weak_ptr& o) {
    T* tp = ptr_; ptr_ = o.ptr_; o.ptr_ = tp;
    detail::ref_count_base* tc = ctl_; ctl_ = o.ctl_; o.ctl_ = tc;
  }

  long use_count() const { return ctl_ ? ctl_->use_count() : 0; }
  bool expired() const { return use_count() == 0; }

  shared_ptr<T> lock() const {
    shared_ptr<T> out;
    if (ctl_ && ctl_->try_add_ref()) {
      out.ptr_ = ptr_;
      out.ctl_ = ctl_;
    }
    return out;
  }

 private:
  T* ptr_;
  detail::ref_count_base* ctl_;
};

template <class T>
shared_ptr<T>::shared_ptr(const weak_ptr<T>& w) : ptr_(NULL), ctl_(NULL) {
  if (w.ctl_ && w.ctl_->try_add_ref()) {
    ptr_ = w.ptr_;
    ctl_ = w.ctl_;
  }
}

// ---------------------------------------------------------------------------
// Pointer casts
// ---------------------------------------------------------------------------
template <class X, class Y>
inline shared_ptr<X> static_pointer_cast(const shared_ptr<Y>& o) {
  X* p = static_cast<X*>(o.get());
  shared_ptr<X> out;
  out.ptr_ = p;
  out.ctl_ = o.ctl_;
  if (out.ctl_) out.ctl_->add_ref();
  return out;
}

template <class X, class Y>
inline shared_ptr<X> dynamic_pointer_cast(const shared_ptr<Y>& o) {
  X* p = dynamic_cast<X*>(o.get());
  shared_ptr<X> out;
  if (p) {
    out.ptr_ = p;
    out.ctl_ = o.ctl_;
    if (out.ctl_) out.ctl_->add_ref();
  }
  return out;
}

// ---------------------------------------------------------------------------
// make_shared -- overloaded by arity (0 through 5 const& args).
// We deliberately do a separate `new` for the control block, trading the
// single-allocation optimisation for ~5x fewer lines of code.
// ---------------------------------------------------------------------------
template <class T>
inline shared_ptr<T> make_shared() {
  return shared_ptr<T>(new T());
}
template <class T, class A1>
inline shared_ptr<T> make_shared(const A1& a1) {
  return shared_ptr<T>(new T(a1));
}
template <class T, class A1, class A2>
inline shared_ptr<T> make_shared(const A1& a1, const A2& a2) {
  return shared_ptr<T>(new T(a1, a2));
}
template <class T, class A1, class A2, class A3>
inline shared_ptr<T> make_shared(const A1& a1, const A2& a2, const A3& a3) {
  return shared_ptr<T>(new T(a1, a2, a3));
}
template <class T, class A1, class A2, class A3, class A4>
inline shared_ptr<T> make_shared(const A1& a1, const A2& a2,
                                  const A3& a3, const A4& a4) {
  return shared_ptr<T>(new T(a1, a2, a3, a4));
}
template <class T, class A1, class A2, class A3, class A4, class A5>
inline shared_ptr<T> make_shared(const A1& a1, const A2& a2, const A3& a3,
                                  const A4& a4, const A5& a5) {
  return shared_ptr<T>(new T(a1, a2, a3, a4, a5));
}

}  // namespace wince

#endif  // WINCE_COMPAT_SHARED_PTR_H_
