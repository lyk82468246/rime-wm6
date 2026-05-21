//
// wince_compat/signal.h -- minimal boost::signals2 replacement.
//
// Supports the subset rime uses (audited 2026-05-17):
//   - signal<void()> / signal<void(A1)> / signal<void(A1, A2)>
//   - connect(callable) returns a `connection`
//   - connection::disconnect() detaches the slot
//   - operator() invokes every connected, non-disconnected slot in order
//
// Slot lifetime is owned by the signal via shared_ptr; connection holds a
// weak_ptr, so:
//   - Dropping a connection without disconnecting is safe (slot remains).
//   - Destroying the signal before the connection is safe (weak goes empty).
//   - Calling disconnect after the signal is destroyed is a no-op.
//
// Dead slots are erased lazily inside operator() rather than immediately on
// disconnect, because users sometimes disconnect from inside a slot callback.
//
#ifndef WINCE_COMPAT_SIGNAL_H_
#define WINCE_COMPAT_SIGNAL_H_

#include <list>

#include "function.h"
#include "shared_ptr.h"

namespace wince {

// Type-erased base so a `connection` doesn't have to be templated on the
// signal signature. Each signal's slot type derives from this.
struct slot_base {
  bool live;
  slot_base() : live(true) {}
  virtual ~slot_base() {}
};

class connection {
 public:
  connection() {}
  explicit connection(const shared_ptr<slot_base>& s) : slot_(s) {}

  void disconnect() {
    shared_ptr<slot_base> s = slot_.lock();
    if (s) s->live = false;
  }

  bool connected() const {
    shared_ptr<slot_base> s = slot_.lock();
    return s && s->live;
  }

 private:
  weak_ptr<slot_base> slot_;
};

template <class Signature> class signal;

// ---------------------------------------------------------------------------
// signal<void()>
// ---------------------------------------------------------------------------
template <>
class signal<void()> {
  struct slot : slot_base {
    function<void()> f;
    slot(const function<void()>& f_) : f(f_) {}
  };

 public:
  template <class F>
  connection connect(const F& f) {
    // Direct ctor (not make_shared) to avoid template-parameter name shadowing
    // when the slot's signature template parameters reuse names like A1.
    shared_ptr<slot> s(new slot(function<void()>(f)));
    slots_.push_back(s);
    return connection(static_pointer_cast<slot_base>(s));
  }

  void operator()() {
    typedef std::list<shared_ptr<slot> >::iterator iter;
    iter it = slots_.begin();
    while (it != slots_.end()) {
      if (!(*it)->live) {
        it = slots_.erase(it);
      } else {
        (*it)->f();
        ++it;
      }
    }
  }

 private:
  std::list<shared_ptr<slot> > slots_;
};

// ---------------------------------------------------------------------------
// signal<void(A1)>
// ---------------------------------------------------------------------------
template <class A1>
class signal<void(A1)> {
  struct slot : slot_base {
    function<void(A1)> f;
    slot(const function<void(A1)>& f_) : f(f_) {}
  };

 public:
  template <class F>
  connection connect(const F& f) {
    shared_ptr<slot> s(new slot(function<void(A1)>(f)));
    slots_.push_back(s);
    return connection(static_pointer_cast<slot_base>(s));
  }

  void operator()(A1 a1) {
    typedef typename std::list<shared_ptr<slot> >::iterator iter;
    iter it = slots_.begin();
    while (it != slots_.end()) {
      if (!(*it)->live) {
        it = slots_.erase(it);
      } else {
        (*it)->f(a1);
        ++it;
      }
    }
  }

 private:
  std::list<shared_ptr<slot> > slots_;
};

// ---------------------------------------------------------------------------
// signal<void(A1, A2)>
// ---------------------------------------------------------------------------
template <class A1, class A2>
class signal<void(A1, A2)> {
  struct slot : slot_base {
    function<void(A1, A2)> f;
    slot(const function<void(A1, A2)>& f_) : f(f_) {}
  };

 public:
  template <class F>
  connection connect(const F& f) {
    shared_ptr<slot> s(new slot(function<void(A1, A2)>(f)));
    slots_.push_back(s);
    return connection(static_pointer_cast<slot_base>(s));
  }

  void operator()(A1 a1, A2 a2) {
    typedef typename std::list<shared_ptr<slot> >::iterator iter;
    iter it = slots_.begin();
    while (it != slots_.end()) {
      if (!(*it)->live) {
        it = slots_.erase(it);
      } else {
        (*it)->f(a1, a2);
        ++it;
      }
    }
  }

 private:
  std::list<shared_ptr<slot> > slots_;
};

}  // namespace wince

#endif  // WINCE_COMPAT_SIGNAL_H_
