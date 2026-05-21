//
// rime/service.h -- WinCE-port mirror of upstream service.h, simplified.
//
// Session = per-input-method-session state (one Engine instance, last-active
// time for stale-session reaping, commit text accumulator).
// Service = process-wide singleton owning the SessionMap and a notification
// callback that frontends (WMRimeSIP shell, future apps) subscribe to.
//
// What's REMOVED vs. upstream:
//   * Deployer integration (deployer_, deployer()). The MVP has no
//     deployer phase -- schemas come from builtin_schemas instead of
//     compiled .bin files.
//   * ResourceResolver factory methods (4 of them) -- depend on Deployer
//     paths. Will return when the file-system-backed schema loader does.
//
// Changes vs. upstream:
//   * `using SessionId = uintptr_t;` etc. -> typedef.
//   * `<mutex>` (C++11 std::mutex) -> wince/mutex.h (CRITICAL_SECTION shim).
//   * NSDMI `time_t last_active_time_ = 0;` etc. -> default-ctor mem-init list.
//
#ifndef RIME_SERVICE_H_
#define RIME_SERVICE_H_

#include <stdint.h>
#include <time.h>
#include <rime/common.h>
#include <mutex.h>  // wince::mutex / wince::lock_guard

namespace rime {

typedef uintptr_t SessionId;

static const SessionId kInvalidSessionId = 0;

typedef function<void(SessionId session_id,
                      const char* message_type,
                      const char* message_value)>
    NotificationHandler;

class Context;
class Engine;
class KeyEvent;
class Schema;

class Session {
 public:
  static const int kLifeSpan = 5 * 60;  // seconds

  Session();
  bool ProcessKey(const KeyEvent& key_event);
  void Activate();
  void ResetCommitText();
  bool CommitComposition();
  void ClearComposition();
  void ApplySchema(Schema* schema);

  Context* context() const;
  Schema* schema() const;
  time_t last_active_time() const { return last_active_time_; }
  const string& commit_text() const { return commit_text_; }

  void OnCommit(const string& commit_text);  // public; signal target
  void OnMessage(const string& type, const string& value);

 private:
  the<Engine> engine_;
  time_t last_active_time_;
  string commit_text_;
  SessionId my_id_;
};

class RIME_DLL Service {
 public:
  ~Service();

  void StartService();
  void StopService();

  SessionId CreateSession();
  an<Session> GetSession(SessionId session_id);
  bool DestroySession(SessionId session_id);
  void CleanupStaleSessions();
  void CleanupAllSessions();

  void SetNotificationHandler(const NotificationHandler& handler);
  void ClearNotificationHandler();
  void Notify(SessionId session_id,
              const string& message_type,
              const string& message_value);

  bool disabled() { return !started_; }

  static Service& instance();

 private:
  Service();

  typedef map<SessionId, an<Session> > SessionMap;
  SessionMap sessions_;
  NotificationHandler notification_handler_;
  wince::mutex mutex_;
  bool started_;
};

}  // namespace rime

#endif  // RIME_SERVICE_H_
