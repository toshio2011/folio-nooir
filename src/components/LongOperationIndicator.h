#pragma once

#include <cstdint>

#include "GfxRenderer.h"

// A deliberately small, non-animated progress hint for synchronous reader work.
// The indicator is only presented by the owning activity at a safe point where
// the current e-ink page can remain visible; it never owns a timer or a redraw
// loop of its own.
class LongOperationIndicator final {
 public:
  class Scope {
   public:
    Scope() = default;
    Scope(LongOperationIndicator* owner, const char* operation) : owner(owner) {
      if (owner) owner->begin(operation);
    }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
    Scope(Scope&& other) noexcept : owner(other.owner) { other.owner = nullptr; }
    Scope& operator=(Scope&& other) noexcept {
      if (this == &other) return *this;
      finishIfNeeded("scope_move");
      owner = other.owner;
      other.owner = nullptr;
      return *this;
    }
    ~Scope() { finishIfNeeded("scope_exit"); }

    void complete() {
      if (!owner) return;
      owner->complete();
      owner = nullptr;
    }

    void cancel(const char* reason) {
      if (!owner) return;
      owner->cancel(reason);
      owner = nullptr;
    }

   private:
    void finishIfNeeded(const char* reason) {
      if (owner && owner->isActive()) owner->cancel(reason);
      owner = nullptr;
    }

    LongOperationIndicator* owner = nullptr;
  };

  explicit LongOperationIndicator(GfxRenderer& renderer) : renderer(renderer) {}

  Scope scoped(const char* operation) { return Scope(this, operation); }

  void begin(const char* operation);
  void stage(const char* stageName);

  // Show the existing page with a single static loading line before a
  // synchronous reader operation starts. The caller then schedules the real
  // render on the next event-loop turn. This never allocates a second page
  // buffer or starts an animation; allowRefresh lets a device profile opt out
  // if its display cannot safely accept the extra FAST_REFRESH.
  bool showBeforeWork(const char* operation, bool allowRefresh = true);

  // Call immediately before an existing reader refresh. At most one thin line
  // is painted per operation, and only after the threshold has elapsed. This
  // never schedules an additional e-ink refresh of its own.
  bool drawIfDue();

  void complete();
  void cancel(const char* reason);

  bool isActive() const { return active; }
  bool wasPresented() const { return presented; }

 private:
  static constexpr uint32_t SHOW_DELAY_MS = 650;
  static constexpr int INDICATOR_INSET = 8;
  static constexpr int INDICATOR_HEIGHT = 4;

  GfxRenderer& renderer;
  bool active = false;
  bool presented = false;
  bool loadingUiShown = false;
  const char* operation = nullptr;
  const char* currentStage = nullptr;
  uint32_t startedAt = 0;

  static const char* safeName(const char* value) { return value ? value : "unknown"; }
};
