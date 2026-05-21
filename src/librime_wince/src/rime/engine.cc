//
// rime/engine.cc -- WinCE-port mirror of upstream engine.cc, simplified.
//
// SCOPE: this is the simplified ConcreteEngine. The IME-input main loop
// (ProcessKey -> Compose -> CalculateSegmentation -> TranslateSegments) is
// preserved verbatim; the schema-switching / formatter / switch-reset
// scaffolding is stubbed out.
//
// What's REMOVED vs. upstream:
//   * Switcher integration  -- `switcher_` is gone. Schemas are swapped
//     via Engine::ApplySchema() directly; users that need a runtime
//     switcher panel will get it back when gear/switcher_processor lands.
//   * Formatter integration -- `formatters_` and `post_processors_` are
//     gone. shape_formatter / shape_processor were the only producers
//     and live in gear/, not ported yet.
//   * Switches::FindOption() in InitializeOptions -- requires switches.cc
//     which we deferred (C++11-heavy). Options start with their
//     defaults; reset_value handling will return when switches lands.
//
// C++03 backports vs. upstream:
//   * Lambdas connected to Context notifiers replaced with named functor
//     types (OnCommitFn, OnSelectFn, ...). Same observable wiring.
//   * `auto` everywhere -> explicit types.
//   * Range-for `for (auto& p : processors_)` -> classic iterator loops.
//   * `CreateComponentsFromList` template body adjusted: brace-init
//     `Ticket{e, ns, klass}` -> `Ticket(e, ns, klass)`.
//   * `vector<an<T>>` -> `vector<of<T> >` (matches header) plus the `> >`
//     fix for MSVC 9.0.
//
#include <cctype>
#include <rime/common.h>
#include <rime/composition.h>
#include <rime/context.h>
#include <rime/engine.h>
#include <rime/filter.h>
#include <rime/key_event.h>
#include <rime/menu.h>
#include <rime/processor.h>
#include <rime/schema.h>
#include <rime/segmentation.h>
#include <rime/segmentor.h>
#include <rime/ticket.h>
#include <rime/translation.h>
#include <rime/translator.h>

namespace rime {

class ConcreteEngine : public Engine {
 public:
  ConcreteEngine();
  virtual ~ConcreteEngine();
  virtual bool ProcessKey(const KeyEvent& key_event);
  virtual void ApplySchema(Schema* schema);
  virtual void CommitText(string text);
  virtual void Compose(Context* ctx);

  // Public so functor wrappers below can dispatch.
  void OnCommit(Context* ctx);
  void OnSelect(Context* ctx);
  void OnContextUpdate(Context* ctx);
  void OnOptionUpdate(Context* ctx, const string& option);
  void OnPropertyUpdate(Context* ctx, const string& property);

 protected:
  void InitializeComponents();
  void CalculateSegmentation(Segmentation* segments);
  void TranslateSegments(Segmentation* segments);

  vector<of<Processor> > processors_;
  vector<of<Segmentor> > segmentors_;
  vector<of<Translator> > translators_;
  vector<of<Filter> > filters_;
};

// ---------------------------------------------------------------------------
// C++03 functor wrappers replacing upstream's lambdas. One per Context
// signal we wire. Keep the operator() const so they bind to
// wince::function<...>.
// ---------------------------------------------------------------------------
namespace {

struct OnCommitFn {
  ConcreteEngine* e;
  OnCommitFn(ConcreteEngine* eng) : e(eng) {}
  void operator()(Context* ctx) const { e->OnCommit(ctx); }
};

struct OnSelectFn {
  ConcreteEngine* e;
  OnSelectFn(ConcreteEngine* eng) : e(eng) {}
  void operator()(Context* ctx) const { e->OnSelect(ctx); }
};

struct OnUpdateFn {
  ConcreteEngine* e;
  OnUpdateFn(ConcreteEngine* eng) : e(eng) {}
  void operator()(Context* ctx) const { e->OnContextUpdate(ctx); }
};

struct OnOptionFn {
  ConcreteEngine* e;
  OnOptionFn(ConcreteEngine* eng) : e(eng) {}
  void operator()(Context* ctx, const string& option) const {
    e->OnOptionUpdate(ctx, option);
  }
};

struct OnPropertyFn {
  ConcreteEngine* e;
  OnPropertyFn(ConcreteEngine* eng) : e(eng) {}
  void operator()(Context* ctx, const string& property) const {
    e->OnPropertyUpdate(ctx, property);
  }
};

}  // namespace

// ---------------------------------------------------------------------------

Engine* Engine::Create() {
  return new ConcreteEngine;
}

Engine::Engine() : active_engine_(NULL) {
  schema_.reset(new Schema);
  context_.reset(new Context);
}

Engine::~Engine() {
  context_.reset();
  schema_.reset();
}

// Default base-class bodies. Moved here (out of engine.h) so that the
// inline reference to KeyEvent does not require its complete type in
// every TU that includes engine.h. Concrete engines override these.
bool Engine::ProcessKey(const KeyEvent& /*key_event*/) { return false; }
void Engine::ApplySchema(Schema* /*schema*/) {}
void Engine::CommitText(string text) { sink_(text); }
void Engine::Compose(Context* /*ctx*/) {}

ConcreteEngine::ConcreteEngine() {
  context_->commit_notifier().connect(OnCommitFn(this));
  context_->select_notifier().connect(OnSelectFn(this));
  context_->update_notifier().connect(OnUpdateFn(this));
  context_->option_update_notifier().connect(OnOptionFn(this));
  context_->property_update_notifier().connect(OnPropertyFn(this));

  InitializeComponents();
  // Switches-based option reset deferred; options start at defaults.
}

ConcreteEngine::~ConcreteEngine() {}

bool ConcreteEngine::ProcessKey(const KeyEvent& key_event) {
  ProcessResult ret = kNoop;
  for (vector<of<Processor> >::iterator it = processors_.begin();
       it != processors_.end(); ++it) {
    ret = (*it)->ProcessKeyEvent(key_event);
    if (ret == kRejected)
      break;
    if (ret == kAccepted)
      return true;
  }
  // record unhandled keys: spaces, numbers, bksp's, etc.
  context_->commit_history().Push(key_event);
  // Post-processor pipeline (shape_processor) deferred; gear/ not ported.
  // notify interested parties
  context_->unhandled_key_notifier()(context_.get(), key_event);
  return false;
}

void ConcreteEngine::OnContextUpdate(Context* ctx) {
  if (!ctx)
    return;
  Compose(ctx);
}

void ConcreteEngine::OnOptionUpdate(Context* ctx, const string& option) {
  if (!ctx)
    return;
  // apply new option to active segment
  if (ctx->IsComposing()) {
    ctx->RefreshNonConfirmedComposition();
  }
  // notification
  bool option_is_on = ctx->get_option(option);
  string msg(option_is_on ? option : "!" + option);
  message_sink_("option", msg);
}

void ConcreteEngine::OnPropertyUpdate(Context* ctx, const string& property) {
  if (!ctx)
    return;
  string value = ctx->get_property(property);
  string msg(property + "=" + value);
  message_sink_("property", msg);
}

void ConcreteEngine::Compose(Context* ctx) {
  if (!ctx)
    return;
  Composition& comp = ctx->composition();
  const string active_input = ctx->input().substr(0, ctx->caret_pos());
  comp.Reset(active_input);
  if (ctx->caret_pos() < ctx->input().length() &&
      ctx->caret_pos() == comp.GetConfirmedPosition()) {
    // translate one segment past caret pos.
    comp.Reset(ctx->input());
  }
  CalculateSegmentation(&comp);
  TranslateSegments(&comp);
}

void ConcreteEngine::CalculateSegmentation(Segmentation* segments) {
  while (!segments->HasFinishedSegmentation()) {
    size_t start_pos = segments->GetCurrentStartPosition();
    // recognize a segment by calling the segmentors in turn
    for (vector<of<Segmentor> >::iterator it = segmentors_.begin();
         it != segmentors_.end(); ++it) {
      if (!(*it)->Proceed(segments))
        break;
    }
    // no advancement
    if (start_pos == segments->GetCurrentEndPosition())
      break;
    // only one segment is allowed past caret pos: the one immediately
    // after the caret.
    if (start_pos >= context_->caret_pos())
      break;
    // move onto the next segment...
    if (!segments->HasFinishedSegmentation())
      segments->Forward();
  }
  // start an empty segment only at the end of a confirmed composition.
  if (!segments->empty() && !segments->back().HasTag("placeholder"))
    segments->Trim();
  if (!segments->empty() && segments->back().status >= Segment::kSelected)
    segments->Forward();
}

void ConcreteEngine::TranslateSegments(Segmentation* segments) {
  for (Segmentation::iterator sit = segments->begin();
       sit != segments->end(); ++sit) {
    Segment& segment = *sit;
    if (segment.status >= Segment::kGuess)
      continue;
    size_t len = segment.end - segment.start;
    string input = segments->input().substr(segment.start, len);
    an<Menu> menu = New<Menu>();
    for (vector<of<Translator> >::iterator tit = translators_.begin();
         tit != translators_.end(); ++tit) {
      an<Translation> translation = (*tit)->Query(input, segment);
      if (!translation)
        continue;
      if (translation->exhausted()) {
        continue;
      }
      menu->AddTranslation(translation);
    }
    for (vector<of<Filter> >::iterator fit = filters_.begin();
         fit != filters_.end(); ++fit) {
      if ((*fit)->AppliesToSegment(&segment)) {
        menu->AddFilter(fit->get());
      }
    }
    segment.status = Segment::kGuess;
    segment.menu = menu;
    segment.selected_index = 0;
  }
}

void ConcreteEngine::CommitText(string text) {
  context_->commit_history().Push(CommitRecord("raw", text));
  // FormatText() removed; no shape_formatter in this build.
  sink_(text);
}

void ConcreteEngine::OnCommit(Context* ctx) {
  context_->commit_history().Push(ctx->composition(), ctx->input());
  string text = ctx->GetCommitText();
  // FormatText() removed; no shape_formatter in this build.
  sink_(text);
}

void ConcreteEngine::OnSelect(Context* ctx) {
  Segment& seg(ctx->composition().back());
  seg.Close();
  if (seg.end == ctx->input().length()) {
    // composition has finished
    seg.status = Segment::kConfirmed;
    // strategy one: commit directly;
    // strategy two: continue composing with another empty segment.
    if (ctx->get_option("_auto_commit"))
      ctx->Commit();
    else
      ctx->composition().Forward();
  } else {
    bool reached_caret_pos = (seg.end >= ctx->caret_pos());
    ctx->composition().Forward();
    if (reached_caret_pos) {
      // finished converting current segment
      // move caret to the end of input
      ctx->set_caret_pos(ctx->input().length());
    } else {
      Compose(ctx);
    }
  }
}

void ConcreteEngine::ApplySchema(Schema* schema) {
  if (!schema)
    return;
  schema_.reset(schema);
  context_->Clear();
  context_->ClearTransientOptions();
  InitializeComponents();
  // Switches-based option reset deferred; options stay at defaults.
  message_sink_("schema", schema_->schema_id() + "/" + schema_->schema_name());
}

// Helper for InitializeComponents: pull a list of "klass" prescriptions
// from Config, look up each via T::Require, instantiate, push back.
// Upstream uses a `template <class T>` free function with `vector<an<T>>&`;
// we keep the same shape but use `vector<of<T> >` and explicit iterators.
template <class T>
inline void CreateComponentsFromList(Engine* engine,
                                     Config* config,
                                     const string& config_key,
                                     const string& component_type,
                                     vector<of<T> >& target_collection) {
  an<ConfigList> component_list = config->GetList(config_key);
  if (!component_list)
    return;
  size_t n = component_list->size();
  for (size_t i = 0; i < n; ++i) {
    an<ConfigValue> prescription = As<ConfigValue>(component_list->GetAt(i));
    if (!prescription)
      continue;
    Ticket ticket(engine, component_type, prescription->str());
    typename Class<T, const Ticket&>::Component* c = T::Require(ticket.klass);
    if (!c) {
      LOG(ERROR) << "error creating " << component_type << ": '"
                 << ticket.klass << "'";
      continue;
    }
    T* component = c->Create(ticket);
    if (!component) {
      LOG(ERROR) << "error creating " << component_type << " from ticket: '"
                 << ticket.klass << "'";
      continue;
    }
    of<T> instance;
    instance.reset(component);
    target_collection.push_back(instance);
  }
}

void ConcreteEngine::InitializeComponents() {
  processors_.clear();
  segmentors_.clear();
  translators_.clear();
  filters_.clear();

  Config* config = schema_->config();
  if (!config)
    return;

  CreateComponentsFromList<Processor>(this, config, "engine/processors",
                                      "processor", processors_);
  CreateComponentsFromList<Segmentor>(this, config, "engine/segmentors",
                                      "segmentor", segmentors_);
  CreateComponentsFromList<Translator>(this, config, "engine/translators",
                                       "translator", translators_);
  CreateComponentsFromList<Filter>(this, config, "engine/filters", "filter",
                                   filters_);
}

}  // namespace rime
