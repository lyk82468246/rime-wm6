//
// rime_api.cc -- C export surface for RimeCore.dll, MVP implementation.
//
// Architecture choice for this batch: rather than wire PinyinTranslator
// through the not-yet-ported gear modules (speller / abc_segmentor /
// ascii_composer / selector / express_editor), the API owns its own
// lightweight MiniSession that:
//   * accumulates a raw pinyin buffer in response to alphanumeric keys
//   * runs PinyinTranslator on every input change to refresh candidates
//   * paginates the result and tracks a highlighted index
//   * commits the highlighted (or raw) text on Space / Return / digit
//
// This bypasses Engine/Context entirely. When the gear modules eventually
// land, the natural follow-up is to register PinyinTranslator as a
// Component, register a real speller processor + abc_segmentor, drive
// everything through Session/Engine, and reduce rime_api.cc to a thin
// translation layer over Session. The MiniSession layer is intentionally
// self-contained so it can be deleted in one go when that happens.
//
// All output strings are UTF-8 and owned by the API on return (allocated
// via std::malloc, freed in RimeFreeCommit / RimeFreeContext). Callers
// must use those Free functions, not std::free, to keep the contract.
//
#include <rime_api.h>

#include <cstdlib>
#include <cstring>

#include <rime/candidate.h>
#include <rime/common.h>
#include <rime/segmentation.h>
#include <rime/service.h>
#include <rime/translation.h>
#include <rime/gear/pinyin_translator.h>
#include <mutex.h>

using namespace rime;

namespace {

// ----------------------------------------------------------------------
// X11 keysyms we care about. These match upstream rime's KeyEvent
// convention and are what WMRimeSIP will translate Windows VK_* into.
// ----------------------------------------------------------------------
const int kXK_BackSpace = 0xff08;
const int kXK_Tab       = 0xff09;
const int kXK_Return    = 0xff0d;
const int kXK_Escape    = 0xff1b;
const int kXK_space     = 0x0020;
const int kXK_Page_Up   = 0xff55;
const int kXK_Page_Down = 0xff56;

const size_t kDefaultPageSize = 5;

// ----------------------------------------------------------------------
// Per-session state. Owned by g_sessions; held alive while the session
// is registered. All access guarded by g_mutex (CRITICAL_SECTION shim).
// ----------------------------------------------------------------------
struct MiniSession {
  string input;                  // raw pinyin entered so far
  CandidateList candidates;      // all candidates for the current input
  size_t page_size;              // commits to this many per page
  size_t page_no;                // 0-based
  size_t highlighted_in_page;    // 0-based within current page
  string pending_commit;         // ready for RimeGetCommit
  MiniSession()
      : page_size(kDefaultPageSize),
        page_no(0),
        highlighted_in_page(0) {}
};

// ----------------------------------------------------------------------
// Process-wide state.
// ----------------------------------------------------------------------
struct ApiState {
  bool initialized;
  RimeNotificationHandler notification_handler;
  void* notification_ctx;
  string shared_data_dir;
  string user_data_dir;
  string app_name;
  an<PinyinTranslator> translator;  // shared across sessions
  map<RimeSessionId, MiniSession*> sessions;
  RimeSessionId next_id;
  wince::mutex mutex;

  ApiState()
      : initialized(false),
        notification_handler(NULL),
        notification_ctx(NULL),
        next_id(1) {}
};

ApiState& state() {
  // Function-local static -- avoids static-init ordering issues with
  // other global registrars (registry / module initializers).
  static ApiState s;
  return s;
}

// ----------------------------------------------------------------------
// Helpers
// ----------------------------------------------------------------------
char* dup_cstr(const string& s) {
  char* p = static_cast<char*>(std::malloc(s.size() + 1));
  if (!p) return NULL;
  std::memcpy(p, s.c_str(), s.size());
  p[s.size()] = '\0';
  return p;
}

void notify(RimeSessionId id, const char* type, const char* value) {
  RimeNotificationHandler h = state().notification_handler;
  void* ctx = state().notification_ctx;
  if (h) h(ctx, id, type, value);
}

// Refresh the candidate list for a session's current input. Resets the
// page + highlight to 0.
void refresh_candidates(MiniSession* s) {
  s->candidates.clear();
  s->page_no = 0;
  s->highlighted_in_page = 0;
  if (s->input.empty() || !state().translator || !state().translator->loaded())
    return;
  Segment seg(0, s->input.size());
  an<Translation> t = state().translator->Query(s->input, seg);
  if (!t) return;
  // Drain the translation. We cap at a generous limit so a runaway
  // translator can't OOM the device. 200 candidates is ~40 pages at the
  // default page size; the user won't scroll further in practice.
  const size_t kMaxCandidates = 200;
  while (!t->exhausted() && s->candidates.size() < kMaxCandidates) {
    an<Candidate> c = t->Peek();
    if (c) s->candidates.push_back(c);
    if (!t->Next()) break;
  }
}

size_t page_start(const MiniSession* s) {
  return s->page_no * s->page_size;
}

size_t page_end(const MiniSession* s) {
  size_t end = page_start(s) + s->page_size;
  if (end > s->candidates.size()) end = s->candidates.size();
  return end;
}

bool is_last_page(const MiniSession* s) {
  return page_end(s) >= s->candidates.size();
}

// Commit the candidate at (page_no, in_page_index). On success, sets
// pending_commit, clears input + candidates. Returns true if anything
// was committed.
bool commit_candidate(MiniSession* s, size_t in_page_index) {
  size_t abs = page_start(s) + in_page_index;
  if (abs >= s->candidates.size()) return false;
  s->pending_commit = s->candidates[abs]->text();
  s->input.clear();
  s->candidates.clear();
  s->page_no = 0;
  s->highlighted_in_page = 0;
  return true;
}

// Commit the raw input as-is (used on Return when input is non-empty).
bool commit_raw(MiniSession* s) {
  if (s->input.empty()) return false;
  s->pending_commit = s->input;
  s->input.clear();
  s->candidates.clear();
  s->page_no = 0;
  s->highlighted_in_page = 0;
  return true;
}

MiniSession* find_session_locked(RimeSessionId id) {
  map<RimeSessionId, MiniSession*>::iterator it = state().sessions.find(id);
  if (it == state().sessions.end()) return NULL;
  return it->second;
}

}  // namespace

// ======================================================================
// Lifecycle
// ======================================================================

extern "C" RIME_DLL void RimeSetup(RimeTraits* traits) {
  ApiState& s = state();
  wince::lock_guard g(s.mutex);
  if (traits) {
    if (traits->shared_data_dir) s.shared_data_dir = traits->shared_data_dir;
    if (traits->user_data_dir)   s.user_data_dir   = traits->user_data_dir;
    if (traits->app_name)        s.app_name        = traits->app_name;
  }
}

extern "C" RIME_DLL void RimeInitialize(RimeTraits* traits) {
  RimeSetup(traits);
  ApiState& s = state();
  wince::lock_guard g(s.mutex);
  if (s.initialized) return;

  Service::instance().StartService();

  // Load the default dictionary. MVP: hard-code luna_pinyin under
  // shared_data_dir. Future: take a list from RimeTraits.modules or a
  // schema's translator/dictionary key.
  string prism_path = s.shared_data_dir;
  string table_path = s.shared_data_dir;
  if (!prism_path.empty() && prism_path[prism_path.size() - 1] != '\\' &&
      prism_path[prism_path.size() - 1] != '/') {
    prism_path += '\\';
    table_path += '\\';
  }
  prism_path += "luna_pinyin.prism.bin";
  table_path += "luna_pinyin.table.bin";

  an<PinyinTranslator> tr = New<PinyinTranslator>();
  if (tr->LoadDictionary("luna_pinyin", prism_path, table_path)) {
    s.translator = tr;
    notify(0, "deploy", "success");
  } else {
    // Keep s.translator empty; sessions will still construct but their
    // candidate menus will be empty. The frontend can still show a raw
    // preedit and degrade gracefully.
    LOG(ERROR) << "RimeInitialize: failed to load luna_pinyin from "
               << s.shared_data_dir;
    notify(0, "deploy", "failure");
  }
  s.initialized = true;
}

extern "C" RIME_DLL void RimeFinalize(void) {
  ApiState& s = state();
  wince::lock_guard g(s.mutex);
  if (!s.initialized) return;
  // Drop all sessions.
  for (map<RimeSessionId, MiniSession*>::iterator it = s.sessions.begin();
       it != s.sessions.end(); ++it) {
    delete it->second;
  }
  s.sessions.clear();
  s.translator.reset();
  Service::instance().StopService();
  s.initialized = false;
}

extern "C" RIME_DLL void RimeSetNotificationHandler(
    RimeNotificationHandler handler, void* context_object) {
  ApiState& s = state();
  wince::lock_guard g(s.mutex);
  s.notification_handler = handler;
  s.notification_ctx = context_object;
}

// ======================================================================
// Session
// ======================================================================

extern "C" RIME_DLL RimeSessionId RimeCreateSession(void) {
  ApiState& s = state();
  wince::lock_guard g(s.mutex);
  if (!s.initialized) return 0;
  RimeSessionId id = s.next_id++;
  s.sessions[id] = new MiniSession();
  return id;
}

extern "C" RIME_DLL Bool RimeFindSession(RimeSessionId session_id) {
  ApiState& s = state();
  wince::lock_guard g(s.mutex);
  return find_session_locked(session_id) ? True : False;
}

extern "C" RIME_DLL Bool RimeDestroySession(RimeSessionId session_id) {
  ApiState& s = state();
  wince::lock_guard g(s.mutex);
  map<RimeSessionId, MiniSession*>::iterator it = s.sessions.find(session_id);
  if (it == s.sessions.end()) return False;
  delete it->second;
  s.sessions.erase(it);
  return True;
}

extern "C" RIME_DLL void RimeCleanupAllSessions(void) {
  ApiState& s = state();
  wince::lock_guard g(s.mutex);
  for (map<RimeSessionId, MiniSession*>::iterator it = s.sessions.begin();
       it != s.sessions.end(); ++it) {
    delete it->second;
  }
  s.sessions.clear();
}

// ======================================================================
// Input
// ======================================================================

extern "C" RIME_DLL Bool RimeProcessKey(RimeSessionId session_id,
                                        int keycode, int mask) {
  (void)mask;  // MVP: ignore modifiers; Shift/Ctrl/Alt not used by speller
  ApiState& s = state();
  wince::lock_guard g(s.mutex);
  MiniSession* sess = find_session_locked(session_id);
  if (!sess) return False;

  // Lowercase ASCII letter: append to input.
  if (keycode >= 'a' && keycode <= 'z') {
    sess->input += static_cast<char>(keycode);
    refresh_candidates(sess);
    return True;
  }
  if (keycode >= 'A' && keycode <= 'Z') {
    sess->input += static_cast<char>(keycode - 'A' + 'a');
    refresh_candidates(sess);
    return True;
  }

  // Apostrophe / hyphen: input separator (luna_pinyin uses "' " as
  // delimiters; we already accept space too).
  if (keycode == '\'' || keycode == '-') {
    if (!sess->input.empty()) {
      sess->input += static_cast<char>(keycode);
      refresh_candidates(sess);
      return True;
    }
    return False;
  }

  // Backspace: pop last char.
  if (keycode == kXK_BackSpace) {
    if (sess->input.empty()) return False;
    sess->input.resize(sess->input.size() - 1);
    refresh_candidates(sess);
    return True;
  }

  // Escape: clear composition.
  if (keycode == kXK_Escape) {
    if (sess->input.empty()) return False;
    sess->input.clear();
    sess->candidates.clear();
    sess->page_no = 0;
    sess->highlighted_in_page = 0;
    return True;
  }

  // Space: commit the highlighted candidate, or raw if no candidates.
  if (keycode == kXK_space) {
    if (sess->input.empty()) return False;
    if (!sess->candidates.empty()) {
      commit_candidate(sess, sess->highlighted_in_page);
    } else {
      commit_raw(sess);
    }
    return True;
  }

  // Return: commit raw text (even if there are candidates).
  if (keycode == kXK_Return) {
    if (sess->input.empty()) return False;
    commit_raw(sess);
    return True;
  }

  // Digit 1-9: select that index on current page (1-based).
  if (keycode >= '1' && keycode <= '9') {
    if (sess->input.empty()) return False;
    size_t idx = static_cast<size_t>(keycode - '1');
    if (page_start(sess) + idx >= sess->candidates.size()) return True;
    commit_candidate(sess, idx);
    return True;
  }

  // Page Up / Page Down.
  if (keycode == kXK_Page_Up) {
    if (sess->page_no > 0) {
      --sess->page_no;
      sess->highlighted_in_page = 0;
    }
    return sess->input.empty() ? False : True;
  }
  if (keycode == kXK_Page_Down) {
    if (!is_last_page(sess)) {
      ++sess->page_no;
      sess->highlighted_in_page = 0;
    }
    return sess->input.empty() ? False : True;
  }

  return False;
}

extern "C" RIME_DLL Bool RimeCommitComposition(RimeSessionId session_id) {
  ApiState& s = state();
  wince::lock_guard g(s.mutex);
  MiniSession* sess = find_session_locked(session_id);
  if (!sess) return False;
  if (sess->input.empty() && sess->candidates.empty()) return False;
  if (!sess->candidates.empty()) {
    commit_candidate(sess, sess->highlighted_in_page);
  } else {
    commit_raw(sess);
  }
  return True;
}

extern "C" RIME_DLL void RimeClearComposition(RimeSessionId session_id) {
  ApiState& s = state();
  wince::lock_guard g(s.mutex);
  MiniSession* sess = find_session_locked(session_id);
  if (!sess) return;
  sess->input.clear();
  sess->candidates.clear();
  sess->page_no = 0;
  sess->highlighted_in_page = 0;
  sess->pending_commit.clear();
}

// ======================================================================
// Output
// ======================================================================

extern "C" RIME_DLL Bool RimeGetCommit(RimeSessionId session_id,
                                       RimeCommit* commit) {
  if (!commit) return False;
  ApiState& s = state();
  wince::lock_guard g(s.mutex);
  MiniSession* sess = find_session_locked(session_id);
  if (!sess || sess->pending_commit.empty()) return False;
  commit->data_size = static_cast<int>(sizeof(RimeCommit) - sizeof(int));
  commit->text = dup_cstr(sess->pending_commit);
  sess->pending_commit.clear();
  return commit->text ? True : False;
}

extern "C" RIME_DLL Bool RimeFreeCommit(RimeCommit* commit) {
  if (!commit) return False;
  if (commit->text) { std::free(commit->text); commit->text = NULL; }
  return True;
}

extern "C" RIME_DLL Bool RimeGetContext(RimeSessionId session_id,
                                        RimeContext* context) {
  if (!context) return False;
  ApiState& s = state();
  wince::lock_guard g(s.mutex);
  MiniSession* sess = find_session_locked(session_id);
  if (!sess) return False;

  std::memset(context, 0, sizeof(*context));
  context->data_size = static_cast<int>(sizeof(RimeContext) - sizeof(int));
  context->composition.preedit = dup_cstr(sess->input);
  context->composition.length =
      static_cast<int>(sess->input.size());
  context->composition.cursor_pos =
      static_cast<int>(sess->input.size());

  size_t start = page_start(sess);
  size_t end = page_end(sess);
  size_t count = end - start;
  context->menu.page_size = static_cast<int>(sess->page_size);
  context->menu.page_no = static_cast<int>(sess->page_no);
  context->menu.is_last_page = is_last_page(sess) ? True : False;
  context->menu.highlighted_candidate_index =
      static_cast<int>(sess->highlighted_in_page);
  context->menu.num_candidates = static_cast<int>(count);
  context->menu.candidates = NULL;
  if (count > 0) {
    context->menu.candidates =
        static_cast<RimeCandidate*>(std::malloc(sizeof(RimeCandidate) * count));
    if (!context->menu.candidates) {
      std::free(context->composition.preedit);
      context->composition.preedit = NULL;
      return False;
    }
    for (size_t i = 0; i < count; ++i) {
      const an<Candidate>& c = sess->candidates[start + i];
      context->menu.candidates[i].text = dup_cstr(c->text());
      string cm = c->comment();
      context->menu.candidates[i].comment = cm.empty() ? NULL : dup_cstr(cm);
      context->menu.candidates[i].reserved = NULL;
    }
  }
  return True;
}

extern "C" RIME_DLL Bool RimeFreeContext(RimeContext* context) {
  if (!context) return False;
  if (context->composition.preedit) {
    std::free(context->composition.preedit);
    context->composition.preedit = NULL;
  }
  if (context->menu.candidates) {
    for (int i = 0; i < context->menu.num_candidates; ++i) {
      if (context->menu.candidates[i].text)
        std::free(context->menu.candidates[i].text);
      if (context->menu.candidates[i].comment)
        std::free(context->menu.candidates[i].comment);
    }
    std::free(context->menu.candidates);
    context->menu.candidates = NULL;
  }
  return True;
}

// ======================================================================
// Selection / navigation
// ======================================================================

extern "C" RIME_DLL Bool RimeSelectCandidateOnCurrentPage(
    RimeSessionId session_id, size_t index) {
  ApiState& s = state();
  wince::lock_guard g(s.mutex);
  MiniSession* sess = find_session_locked(session_id);
  if (!sess) return False;
  if (page_start(sess) + index >= sess->candidates.size()) return False;
  commit_candidate(sess, index);
  return True;
}

extern "C" RIME_DLL Bool RimeChangePage(RimeSessionId session_id,
                                        Bool backward) {
  ApiState& s = state();
  wince::lock_guard g(s.mutex);
  MiniSession* sess = find_session_locked(session_id);
  if (!sess) return False;
  if (backward) {
    if (sess->page_no == 0) return False;
    --sess->page_no;
    sess->highlighted_in_page = 0;
    return True;
  }
  if (is_last_page(sess)) return False;
  ++sess->page_no;
  sess->highlighted_in_page = 0;
  return True;
}
