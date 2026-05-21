//
// rime_api.h -- C export surface for RimeCore.dll.
//
// This is the MVP API the WMRimeSIP shell (and any future host) calls
// into. It is a deliberate subset of upstream's rime/api/rime_api.h:
// what's exposed is mostly source-compatible (struct layouts and
// function signatures match), but several feature areas are stubbed or
// absent because the underlying subsystems aren't ported yet.
//
// What's IN:
//   * Setup / Finalize lifecycle
//   * Session create / destroy / lookup
//   * Process key, commit composition, clear composition
//   * Get commit text + free
//   * Get context (preedit + candidate menu) + free
//   * Select a candidate by 0-based index on the current page
//   * Notification handler (option/schema/deploy events)
//
// What's OUT for now (calls return False / no-op when supplied):
//   * Deployer (start_maintenance, deploy, sync_user_data)
//   * Schema list / current schema switching
//   * Config get/set/iterators
//   * Candidate iteration beyond the current page
//   * Status struct (is_ascii_mode etc.) -- frontend can track this itself
//
// Memory model: every Rime* output struct that holds heap-allocated
// strings/arrays must be freed with the matching RimeFree*. Inputs
// passed into Rime* are not retained beyond the call.
//
// The macros RIME_DLL / RIME_API are unchanged from the prior stub.
//
#ifndef RIME_API_H_
#define RIME_API_H_

#include <stddef.h>
#include <stdint.h>

#if defined(RIMECORE_EXPORTS)
#define RIME_DLL __declspec(dllexport)
#elif defined(RIMECORE_IMPORTS)
#define RIME_DLL __declspec(dllimport)
#else
#define RIME_DLL
#endif

#define RIME_API extern "C" RIME_DLL

#ifdef __cplusplus
extern "C" {
#endif

#ifndef Bool
#define Bool int
#endif
#ifndef False
#define False 0
#endif
#ifndef True
#define True 1
#endif

typedef uintptr_t RimeSessionId;

// ---------- Traits ---------------------------------------------------
typedef struct rime_traits_t {
  int data_size;
  const char* shared_data_dir;  // where luna_pinyin.{prism,table}.bin live
  const char* user_data_dir;    // currently unused (no user_dict yet)
  const char* app_name;         // for logging; may be NULL
} RimeTraits;

// ---------- Commit ---------------------------------------------------
typedef struct rime_commit_t {
  int data_size;
  char* text;  // UTF-8; freed by RimeFreeCommit
} RimeCommit;

// ---------- Composition + Menu + Context -----------------------------
typedef struct rime_candidate_t {
  char* text;     // UTF-8; freed by RimeFreeContext
  char* comment;  // UTF-8 or NULL
  void* reserved;
} RimeCandidate;

typedef struct rime_menu_t {
  int page_size;
  int page_no;
  Bool is_last_page;
  int highlighted_candidate_index;
  int num_candidates;
  RimeCandidate* candidates;  // array of length num_candidates
  char* select_keys;          // NULL in MVP
} RimeMenu;

typedef struct rime_composition_t {
  int length;
  int cursor_pos;
  int sel_start;
  int sel_end;
  char* preedit;
} RimeComposition;

typedef struct rime_context_t {
  int data_size;
  RimeComposition composition;
  RimeMenu menu;
  char* commit_text_preview;  // NULL in MVP
  char** select_labels;       // NULL in MVP
} RimeContext;

typedef void (*RimeNotificationHandler)(void* context_object,
                                        RimeSessionId session_id,
                                        const char* message_type,
                                        const char* message_value);

// ---------- Lifecycle -----------------------------------------------
RIME_API void RimeSetup(RimeTraits* traits);
RIME_API void RimeInitialize(RimeTraits* traits);
RIME_API void RimeFinalize(void);
RIME_API void RimeSetNotificationHandler(RimeNotificationHandler handler,
                                         void* context_object);

// ---------- Session --------------------------------------------------
RIME_API RimeSessionId RimeCreateSession(void);
RIME_API Bool RimeFindSession(RimeSessionId session_id);
RIME_API Bool RimeDestroySession(RimeSessionId session_id);
RIME_API void RimeCleanupAllSessions(void);

// ---------- Input ----------------------------------------------------
// keycode/mask follow upstream's XK_* / kCtrlMask conventions. See
// rime/key_table.h for the bindings we ship.
RIME_API Bool RimeProcessKey(RimeSessionId session_id, int keycode, int mask);
RIME_API Bool RimeCommitComposition(RimeSessionId session_id);
RIME_API void RimeClearComposition(RimeSessionId session_id);

// ---------- Output ---------------------------------------------------
// On success the struct is filled in and the caller must free with
// RimeFreeCommit / RimeFreeContext. On no-output (no pending commit
// text, etc.) returns False and leaves the struct untouched.
RIME_API Bool RimeGetCommit(RimeSessionId session_id, RimeCommit* commit);
RIME_API Bool RimeFreeCommit(RimeCommit* commit);
RIME_API Bool RimeGetContext(RimeSessionId session_id, RimeContext* context);
RIME_API Bool RimeFreeContext(RimeContext* context);

// ---------- Candidate selection -------------------------------------
// Select the (index)th candidate on the current page (0-based). Returns
// True if there was such a candidate; the candidate text is committed
// into the session's commit buffer (retrieve with RimeGetCommit).
RIME_API Bool RimeSelectCandidateOnCurrentPage(RimeSessionId session_id,
                                               size_t index);

// ---------- Page navigation -----------------------------------------
// Move the candidate page forward (1) or backward (-1) by one. Returns
// True if the page changed.
RIME_API Bool RimeChangePage(RimeSessionId session_id, Bool backward);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // RIME_API_H_
