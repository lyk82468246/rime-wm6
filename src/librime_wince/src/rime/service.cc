//
// rime/service.cc -- WinCE-port mirror of upstream service.cc, simplified.
//
// What's REMOVED vs. upstream:
//   * Deployer (and the deployer-message-sink wiring in Service ctor).
//   * ResourceResolver factory methods (4) and the <rime/resource.h>
//     include.
//   * Multi-catch try block in CreateSession reduced to std::exception only.
//
// Changes vs. upstream:
//   * Lambdas attached to Engine signals -> named functor wrappers
//     (CommitFwd, MessageFwd) holding a Session* and a SessionId.
//   * `auto` -> explicit.
//   * `std::lock_guard<std::mutex>` -> `wince::lock_guard`.
//   * `nullptr` for NotificationHandler -> default-constructed.
//
#include <rime/context.h>
#include <rime/engine.h>
#include <rime/schema.h>
#include <rime/service.h>

namespace rime {

namespace {

// Functor wrappers for Engine signal callbacks (replaces upstream lambdas).
struct CommitFwd {
  Session* s;
  CommitFwd(Session* sess) : s(sess) {}
  void operator()(const string& text) const { s->OnCommit(text); }
};

struct MessageFwd {
  Session* s;
  MessageFwd(Session* sess) : s(sess) {}
  void operator()(const string& type, const string& value) const {
    s->OnMessage(type, value);
  }
};

}  // namespace

Session::Session() : last_active_time_(0), my_id_(0) {
  engine_.reset(Engine::Create());
  my_id_ = reinterpret_cast<SessionId>(this);
  engine_->sink().connect(CommitFwd(this));
  engine_->message_sink().connect(MessageFwd(this));
}

bool Session::ProcessKey(const KeyEvent& key_event) {
  return engine_->ProcessKey(key_event);
}

void Session::Activate() {
  last_active_time_ = time(NULL);
}

void Session::ResetCommitText() {
  commit_text_.clear();
}

bool Session::CommitComposition() {
  if (!engine_)
    return false;
  engine_->context()->Commit();
  return !commit_text_.empty();
}

void Session::ClearComposition() {
  if (!engine_)
    return;
  engine_->context()->AbortComposition();
}

void Session::ApplySchema(Schema* schema) {
  engine_->ApplySchema(schema);
}

void Session::OnCommit(const string& commit_text) {
  commit_text_ += commit_text;
}

void Session::OnMessage(const string& type, const string& value) {
  Service::instance().Notify(my_id_, type, value);
}

Context* Session::context() const {
  return engine_ ? engine_->active_engine()->context() : NULL;
}

Schema* Session::schema() const {
  return engine_ ? engine_->active_engine()->schema() : NULL;
}

Service::Service() : started_(false) {}

Service::~Service() {
  StopService();
}

void Service::StartService() {
  started_ = true;
}

void Service::StopService() {
  started_ = false;
  CleanupAllSessions();
}

SessionId Service::CreateSession() {
  SessionId id = kInvalidSessionId;
  if (disabled())
    return id;
  // Trimmed multi-catch: only handle std::exception. Other throw types
  // are untracked here; if a future port path needs the int/char*/string
  // catches back, restore them in this block.
  try {
    an<Session> session = New<Session>();
    session->Activate();
    id = reinterpret_cast<uintptr_t>(session.get());
    sessions_[id] = session;
  } catch (const std::exception& ex) {
    LOG(ERROR) << "Error creating session: " << ex.what();
  } catch (...) {
    LOG(ERROR) << "Error creating session.";
  }
  return id;
}

an<Session> Service::GetSession(SessionId session_id) {
  if (disabled())
    return an<Session>();
  SessionMap::iterator it = sessions_.find(session_id);
  if (it != sessions_.end()) {
    an<Session>& session = it->second;
    session->Activate();
    return session;
  }
  return an<Session>();
}

bool Service::DestroySession(SessionId session_id) {
  SessionMap::iterator it = sessions_.find(session_id);
  if (it == sessions_.end())
    return false;
  sessions_.erase(it);
  return true;
}

void Service::CleanupStaleSessions() {
  time_t now = time(NULL);
  int count = 0;
  for (SessionMap::iterator it = sessions_.begin(); it != sessions_.end();) {
    if (it->second &&
        it->second->last_active_time() < now - Session::kLifeSpan) {
      sessions_.erase(it++);
      ++count;
    } else {
      ++it;
    }
  }
  if (count > 0) {
    LOG(INFO) << "Recycled " << count << " stale sessions.";
  }
}

void Service::CleanupAllSessions() {
  sessions_.clear();
}

void Service::SetNotificationHandler(const NotificationHandler& handler) {
  notification_handler_ = handler;
}

void Service::ClearNotificationHandler() {
  notification_handler_ = NotificationHandler();
}

void Service::Notify(SessionId session_id,
                     const string& message_type,
                     const string& message_value) {
  if (notification_handler_) {
    wince::lock_guard lock(mutex_);
    notification_handler_(session_id, message_type.c_str(),
                          message_value.c_str());
  }
}

Service& Service::instance() {
  static the<Service> s_instance;
  if (!s_instance) {
    s_instance.reset(new Service);
  }
  return *s_instance;
}

}  // namespace rime
