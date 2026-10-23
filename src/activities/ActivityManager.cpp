#include "ActivityManager.h"

#include <FontCacheManager.h>
#include <HalPowerManager.h>
#include <Arduino.h>
#include <esp_heap_caps.h>

#include <algorithm>
#include <cstring>

#include "OpdsServerStore.h"
#include "BleInput.h"
#include "util/BleMemoryPolicy.h"
#include "InterfaceFontSelection.h"
#include "SdCardFontSystem.h"
#include "components/UITheme.h"
#include "boot_sleep/BootActivity.h"
#include "boot_sleep/SleepActivity.h"
#include "browser/OpdsBookBrowserActivity.h"
#include "home/CrashActivity.h"
#include "home/FileBrowserActivity.h"
#include "home/FolioLibraryActivity.h"
#include "home/HomeActivity.h"
#include "home/RecentBooksActivity.h"
#include "home/ToDoListActivity.h"
#include "home/ReadingStatsActivity.h"
#include "network/CrossPointWebServerActivity.h"
#include "reader/ReaderActivity.h"
#include "reader/EpubReaderClippingListActivity.h"
#include "settings/OpdsServerListActivity.h"
#include "settings/BluetoothSettingsActivity.h"
#include "settings/SettingsActivity.h"
#include "util/EpubDiagnostics.h"
#include "util/FullScreenMessageActivity.h"
#include "util/CbzDiagnostics.h"

static portMUX_TYPE activityManagerSpinlock = portMUX_INITIALIZER_UNLOCKED;
#if NOOIR_EPUB_DIAGNOSTICS
static bool lifecycleHomeConstructionRecorded = false;
static bool lifecycleFirstRenderRecorded = false;
static bool lifecycleFirstRefreshRecorded = false;
static bool lifecycleFirstUsableRecorded = false;
#endif

static bool isSdIndependentUiActivityName(const std::string& name) {
  return name == "Boot" || name == "Sleep" || name == "Crash" || name == "OtaUpdate" ||
         name == "SdFirmwareUpdate" || name == "FontDownload";
}

#if FREEINK_CAP_BLE_HID_HOST
static constexpr unsigned long BLUETOOTH_NOTIFICATION_DURATION_MS = 2400;

static void logActivityHeapShape(const char* stage) {
  LOG_INF("HEAPSHAPE", "stage=%s free=%u largest=%u min=%u internal8=%u/%u dma8=%u/%u",
          stage ? stage : "unknown", static_cast<unsigned>(ESP.getFreeHeap()),
          static_cast<unsigned>(ESP.getMaxAllocHeap()), static_cast<unsigned>(ESP.getMinFreeHeap()),
          static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
          static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
          static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_DMA | MALLOC_CAP_8BIT)),
          static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_8BIT)));
}
#endif
static constexpr unsigned long SETTINGS_NOTIFICATION_DURATION_MS = 3000;

void ActivityManager::begin() {
#if defined(configNUM_CORES) && configNUM_CORES > 1
  constexpr BaseType_t renderTaskCore = 1;
#else
  constexpr BaseType_t renderTaskCore = 0;
#endif
  xTaskCreatePinnedToCore(&renderTaskTrampoline, "ActivityManagerRender",
                          8192,               // Stack size
                          this,               // Parameters
                          1,                  // Priority
                          &renderTaskHandle,  // Task handle
                          renderTaskCore  // Keep long renders/cover decodes off CPU 0's idle watchdog when available
  );
  assert(renderTaskHandle != nullptr && "Failed to create render task");
}

void ActivityManager::renderTaskTrampoline(void* param) {
  auto* self = static_cast<ActivityManager*>(param);
  self->renderTaskLoop();
}

void ActivityManager::renderTaskLoop() {
  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    // Move the notification into the in-flight state while holding the same
    // spinlock used by lifecycle admission. A new notification cannot be
    // lost between queued and in-flight, and readers of hasPendingRenderWork()
    // never observe an unowned render interval.
    taskENTER_CRITICAL(&activityManagerSpinlock);
    renderInFlight.store(true, std::memory_order_release);
    renderQueued.store(false, std::memory_order_release);
    taskEXIT_CRITICAL(&activityManagerSpinlock);

    // Acquire the lock before reading currentActivity to avoid a TOCTOU race
    // where the main task deletes the activity between the null-check and render().
    RenderLock lock;
    if (currentActivity) {
      HalPowerManager::Lock powerLock;  // Ensure we don't go into low-power mode while rendering
      // Each activity starts with UI text scaling enabled. The Folio shelf
      // activities explicitly disable it for their featured book and grid so
      // changing UI Scale cannot alter the bookshelf presentation.
      renderer.setUiScaleTextEnabled(true);
      currentActivity->render(std::move(lock));
#if NOOIR_SD_FONT_DIAGNOSTICS
      if (currentActivity->isReaderActivity()) {
        sdFontSystem.diagnosticCheckpoint("reader_first_render");
      } else if (currentActivity->isHomeActivity() || currentActivity->name == "RecentBooks") {
        sdFontSystem.diagnosticCheckpoint("home_rendered");
      } else if (currentActivity->name == "FolioLibrary") {
        sdFontSystem.diagnosticCheckpoint("library_rendered");
      }
#endif
#if NOOIR_EPUB_DIAGNOSTICS
      if (!lifecycleFirstRenderRecorded) {
        lifecycleFirstRenderRecorded = true;
        EpubDiagnostics::phaseRecord("lifecycle_first_render");
      }
      if (!lifecycleFirstRefreshRecorded) {
        lifecycleFirstRefreshRecorded = true;
        EpubDiagnostics::phaseRecord("lifecycle_first_refresh");
      }
      if (!lifecycleFirstUsableRecorded &&
          (currentActivity->isHomeActivity() || currentActivity->name == "RecentBooks" ||
           currentActivity->isReaderActivity())) {
        lifecycleFirstUsableRecorded = true;
        EpubDiagnostics::phaseRecord("lifecycle_first_usable", currentActivity->isReaderActivity() ? 2 : 1);
      }
#endif
#if FREEINK_CAP_BLE_HID_HOST
      char bluetoothNotification[sizeof(pendingBluetoothNotification)] = {};
      bool showBluetoothNotification = false;
      taskENTER_CRITICAL(&activityManagerSpinlock);
      if (hasBluetoothNotification && !isReaderActivity()) {
        strncpy(visibleBluetoothNotification, pendingBluetoothNotification,
                sizeof(visibleBluetoothNotification) - 1);
        visibleBluetoothNotification[sizeof(visibleBluetoothNotification) - 1] = '\0';
        pendingBluetoothNotification[0] = '\0';
        hasBluetoothNotification = false;
        bluetoothNotificationVisible = visibleBluetoothNotification[0] != '\0';
        bluetoothNotificationShownAt = millis();
      }
      if (bluetoothNotificationVisible && !isReaderActivity()) {
        if (millis() - bluetoothNotificationShownAt < BLUETOOTH_NOTIFICATION_DURATION_MS) {
          strncpy(bluetoothNotification, visibleBluetoothNotification, sizeof(bluetoothNotification) - 1);
          showBluetoothNotification = bluetoothNotification[0] != '\0';
        } else {
          bluetoothNotificationVisible = false;
          visibleBluetoothNotification[0] = '\0';
        }
      }
      taskEXIT_CRITICAL(&activityManagerSpinlock);
      if (showBluetoothNotification) GUI.drawPopup(renderer, bluetoothNotification, true);
#endif
      char settingsNotification[sizeof(pendingSettingsNotification)] = {};
      bool showSettingsNotification = false;
      taskENTER_CRITICAL(&activityManagerSpinlock);
      if (hasSettingsNotification) {
        strncpy(visibleSettingsNotification, pendingSettingsNotification, sizeof(visibleSettingsNotification) - 1);
        visibleSettingsNotification[sizeof(visibleSettingsNotification) - 1] = '\0';
        pendingSettingsNotification[0] = '\0';
        hasSettingsNotification = false;
        settingsNotificationVisible = visibleSettingsNotification[0] != '\0';
        settingsNotificationShownAt = millis();
      }
      if (settingsNotificationVisible) {
        if (millis() - settingsNotificationShownAt < SETTINGS_NOTIFICATION_DURATION_MS) {
          strncpy(settingsNotification, visibleSettingsNotification, sizeof(settingsNotification) - 1);
          showSettingsNotification = settingsNotification[0] != '\0';
        } else {
          settingsNotificationVisible = false;
          visibleSettingsNotification[0] = '\0';
        }
      }
      taskEXIT_CRITICAL(&activityManagerSpinlock);
      if (showSettingsNotification) GUI.drawPopup(renderer, settingsNotification, true);
    }
    // Notify any task blocked in requestUpdateAndWait() that the render is done.
    TaskHandle_t waiter = nullptr;
    taskENTER_CRITICAL(&activityManagerSpinlock);
    waiter = waitingTaskHandle;
    waitingTaskHandle = nullptr;
    renderInFlight.store(false, std::memory_order_release);
    taskEXIT_CRITICAL(&activityManagerSpinlock);
#if FREEINK_CAP_BLE_HID_HOST
    if (!bluetoothSettingsActivationFailed_.load(std::memory_order_acquire)) {
      bleinput::requestLifecycleReevaluation();
      // The failure can arrive between the first check and reevaluation. In
      // that case discard this stale signal; an explicit Retry clears the
      // latch before queuing its own lifecycle request.
      if (bluetoothSettingsActivationFailed_.load(std::memory_order_acquire)) {
        bleinput::clearReAdmissionPending();
        (void)bleinput::takeLifecycleReevaluation();
      }
    }
#else
    bleinput::requestLifecycleReevaluation();
#endif
    if (waiter) {
      xTaskNotify(waiter, 1, eIncrement);
    }
  }
}

void ActivityManager::loop() {
  bool settingsToastActive = false;
  unsigned long settingsToastShownAt = 0;
  taskENTER_CRITICAL(&activityManagerSpinlock);
  settingsToastActive = settingsNotificationVisible;
  settingsToastShownAt = settingsNotificationShownAt;
  taskEXIT_CRITICAL(&activityManagerSpinlock);
  if (settingsToastActive && millis() - settingsToastShownAt >= SETTINGS_NOTIFICATION_DURATION_MS) {
    bool dismissed = false;
    taskENTER_CRITICAL(&activityManagerSpinlock);
    if (settingsNotificationVisible && settingsNotificationShownAt == settingsToastShownAt) {
      settingsNotificationVisible = false;
      visibleSettingsNotification[0] = '\0';
      dismissed = true;
    }
    taskEXIT_CRITICAL(&activityManagerSpinlock);
    if (dismissed) requestUpdate();
  }
#if FREEINK_CAP_BLE_HID_HOST
  bool bluetoothToastActive = false;
  unsigned long bluetoothToastShownAt = 0;
  taskENTER_CRITICAL(&activityManagerSpinlock);
  bluetoothToastActive = bluetoothNotificationVisible;
  bluetoothToastShownAt = bluetoothNotificationShownAt;
  taskEXIT_CRITICAL(&activityManagerSpinlock);
  if (bluetoothToastActive) {
    const bool timedOut = millis() - bluetoothToastShownAt >= BLUETOOTH_NOTIFICATION_DURATION_MS;
    bool mappedButtonActive = mappedInput.wasAnyPressed() || mappedInput.wasAnyReleased();
    if (!mappedButtonActive) {
      for (uint8_t index = 0; index < MappedInputManager::kButtonCount; ++index) {
        if (mappedInput.isPressed(static_cast<MappedInputManager::Button>(index))) {
          mappedButtonActive = true;
          break;
        }
      }
    }
    if (timedOut || mappedButtonActive) {
      bool dismissed = false;
      taskENTER_CRITICAL(&activityManagerSpinlock);
      if (bluetoothNotificationVisible &&
          ((timedOut && bluetoothNotificationShownAt == bluetoothToastShownAt) || mappedButtonActive)) {
        bluetoothNotificationVisible = false;
        visibleBluetoothNotification[0] = '\0';
        dismissed = true;
      }
      taskEXIT_CRITICAL(&activityManagerSpinlock);
      // Queue the normal activity redraw so the popup disappears, then let the
      // same mapped input continue through the activity loop below.
      if (dismissed) requestUpdate();
    }
  }
#endif
  const bool sleepTransitionPending = pendingActivity && pendingActivity->name == "Sleep";
  if (suppressRestoredReaderInput && !sleepTransitionPending) {
    const auto button = [this](const MappedInputManager::Button input) {
      return mappedInput.isPressed(input) || mappedInput.wasReleased(input);
    };
    const bool closingInput = button(MappedInputManager::Button::Back) ||
                              button(MappedInputManager::Button::Confirm) ||
                              button(MappedInputManager::Button::Power) ||
                              button(MappedInputManager::Button::Left) ||
                              button(MappedInputManager::Button::Right) ||
                              button(MappedInputManager::Button::Up) ||
                              button(MappedInputManager::Button::Down);
    if (!closingInput) suppressRestoredReaderInput = false;
    return;
  }

#if FREEINK_CAP_BLE_HID_HOST
  uint8_t bleShortcut = 0xFF;
  if (mappedInput.takeBleShortcut(bleShortcut) && !sleepTransitionPending) {
    switch (bleShortcut) {
      case bleinput::mapping::kShortcutHome:
        goHome();
        break;
      case bleinput::mapping::kShortcutLibrary:
        if (prepareWebExclusiveExit()) goToFileBrowser();
        break;
      case bleinput::mapping::kShortcutRecent:
        goToRecentBooks();
        break;
      case bleinput::mapping::kShortcutClippings:
        goToClippings();
        break;
      default:
        break;
    }
    return;
  }
#endif

  if (currentActivity && !sleepTransitionPending) {
    if (!currentActivity->isHomeActivity() && mappedInput.wasHomeGesture()) {
      if (currentActivity->handleHomeGesture()) {
        return;
      }
      goHome();
      return;
    }

    // Note: do not hold a lock here, the loop() method must be responsible for acquire one if needed
    currentActivity->loop();
  }

  while (pendingAction != PendingAction::None) {
    if (pendingAction == PendingAction::Pop) {
      RenderLock lock;

      if (!currentActivity) {
        // Should never happen in practice
        LOG_ERR("ACT", "Pop set but currentActivity is null; ignoring pop request");
        pendingAction = PendingAction::None;
        continue;
      }

      const bool readingStatsActivity = currentActivity->name == "ReadingStats" || currentActivity->name == "ReadingCalendar";
      ActivityResult pendingResult = std::move(currentActivity->result);

      // Destroy the current activity
      exitActivity(lock);
      pendingAction = PendingAction::None;

      if (stackActivities.empty()) {
        LOG_DBG("ACT", "No more activities on stack, going home");
        lock.unlock();  // goHome may acquire its own lock
        goHome();
        continue;  // Will launch goHome immediately

      } else {
        currentActivity = std::move(stackActivities.back());
        stackActivities.pop_back();
        bleinput::clearNoDeviceTimeoutSuppression();
        LOG_DBG("ACT", "Popped from activity stack, new size = %zu", stackActivities.size());
        if (currentActivity->isReaderActivity()) suppressRestoredReaderInput = true;
        bool lockReleasedForHandler = false;
        // Handle result if necessary
        if (currentActivity->resultHandler) {
          LOG_DBG("ACT", "Handling result for popped activity");

          // Move it here to avoid the case where handler calling another startActivityForResult()
          auto handler = std::move(currentActivity->resultHandler);
          currentActivity->resultHandler = nullptr;
          lock.unlock();  // Handler may acquire its own lock
          lockReleasedForHandler = true;
          handler(pendingResult);
        }

        // Request an update to ensure the popped activity gets re-rendered
        if (pendingAction == PendingAction::None) {
          if (readingStatsActivity && currentActivity && currentActivity->name == "RecentBooks") {
            auto* recentBooks = static_cast<RecentBooksActivity*>(currentActivity.get());
            if (lockReleasedForHandler) {
              RenderLock restoreLock;
              recentBooks->restoreTransientStateAfterReadingStats();
            } else {
              recentBooks->restoreTransientStateAfterReadingStats();
            }
          }
          bleinput::requestLifecycleReevaluation();
          requestUpdate();
          if (readingStatsActivity && currentActivity && currentActivity->name == "RecentBooks") {
            LOG_INF("RSDIAG", "stage=parent_redraw_requested free=%u largest=%u",
                    static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()));
          }
        }

        // Handler may request another pending action, we will handle it in the next loop iteration
        continue;
      }

    } else if (pendingActivity || pendingSettingsConstruction) {
      const bool constructSettingsAfterExit = pendingSettingsConstruction;
#if FREEINK_CAP_BLE_HID_HOST
      if (constructSettingsAfterExit && BleHid.isRunning()) {
        bleinput::clearReaderBleReturnPending();
        bleinput::stop();
      }
#endif
      const bool webActivityOnStack = std::any_of(stackActivities.begin(), stackActivities.end(), [](const auto& activity) {
        return activity && activity->name == "CrossPointWebServer";
      });
      const bool leavingWebExclusive = pendingAction == PendingAction::Replace &&
                                       ((currentActivity && currentActivity->name == "CrossPointWebServer") ||
                                        webActivityOnStack);
      const bool recentBooksToReader = currentActivity && pendingActivity && currentActivity->name == "RecentBooks" &&
                                       pendingActivity->name == "Reader" && pendingAction == PendingAction::Replace;
      const bool recentBooksToReadingStats = currentActivity && pendingActivity &&
                                             currentActivity->name == "RecentBooks" &&
                                             (pendingActivity->name == "ReadingStats" ||
                                              pendingActivity->name == "ReadingCalendar") &&
                                             pendingAction == PendingAction::Push;
      const bool readerShellToConcrete = currentActivity && pendingActivity && currentActivity->name == "Reader" &&
                                         pendingActivity->name == "EpubReader" && pendingAction == PendingAction::Replace;
#if FREEINK_CAP_BLE_HID_HOST
      if (recentBooksToReader) logActivityHeapShape("reader_transition_before_recentbooks_exit");
      if (readerShellToConcrete) logActivityHeapShape("reader_transition_before_reader_shell_exit");
#endif
      // Current activity has requested a new activity to be launched
      RenderLock lock;
      const size_t webExitBeforeFree = leavingWebExclusive ? ESP.getFreeHeap() : 0;
      const size_t webExitBeforeLargest = leavingWebExclusive ? ESP.getMaxAllocHeap() : 0;

      if (recentBooksToReadingStats) {
        static_cast<RecentBooksActivity*>(currentActivity.get())->releaseTransientStateForReadingStats();
      }

      if (pendingAction == PendingAction::Replace) {
        // Destroy the current activity
        exitActivity(lock);
        // Clear the stack
        while (!stackActivities.empty()) {
          stackActivities.back()->onExit();
          stackActivities.pop_back();
        }
      } else if (pendingAction == PendingAction::Push) {
        // Move current activity to stack
        stackActivities.push_back(std::move(currentActivity));
        LOG_DBG("ACT", "Pushed to activity stack, new size = %zu", stackActivities.size());
      }
      if (constructSettingsAfterExit) {
        LOG_INF("SETDIAG", "stage=before_settings_construction free=%u largest=%u",
                ESP.getFreeHeap(), ESP.getMaxAllocHeap());
        pendingSettingsConstruction = false;
        pendingActivity = std::make_unique<SettingsActivity>(renderer, mappedInput);
        LOG_INF("SETDIAG", "stage=after_settings_construction free=%u largest=%u",
                ESP.getFreeHeap(), ESP.getMaxAllocHeap());
      }
      if (leavingWebExclusive) {
        LOG_INF("WEBMEM", "owner=web_activity_and_child_stack free=%u->%u largest=%u->%u",
                static_cast<unsigned>(webExitBeforeFree), static_cast<unsigned>(ESP.getFreeHeap()),
                static_cast<unsigned>(webExitBeforeLargest), static_cast<unsigned>(ESP.getMaxAllocHeap()));
        LOG_INF("WEBMEM", "stage=exclusive_exit_clean free=%u largest=%u", static_cast<unsigned>(ESP.getFreeHeap()),
                static_cast<unsigned>(ESP.getMaxAllocHeap()));
      }
#if FREEINK_CAP_BLE_HID_HOST
      if (recentBooksToReader) logActivityHeapShape("reader_transition_after_recentbooks_destroyed");
      if (readerShellToConcrete) logActivityHeapShape("reader_transition_after_reader_shell_destroyed");
#endif
      pendingAction = PendingAction::None;
      currentActivity = std::move(pendingActivity);
      bleinput::clearNoDeviceTimeoutSuppression();
      // These screens do not use the SD-backed UI font and may need the heap
      // immediately. Release its complete asset set while the transition lock
      // is still held, before the activity's onEnter() starts its own work.
      // Sleep and Web perform their own logged teardown boundaries.
      if (isSdIndependentUiActivityName(currentActivity->name) && currentActivity->name != "Sleep") {
        sdFontSystem.releaseInterfaceFontCaches();
      }

      lock.unlock();  // onEnter may acquire its own lock
#if FREEINK_CAP_BLE_HID_HOST
      if (recentBooksToReader) logActivityHeapShape("reader_transition_before_reader_activity_enter");
      if (readerShellToConcrete) logActivityHeapShape("reader_transition_before_epub_reader_enter");
#endif
      if (currentActivity->name == "Sleep") {
        LOG_INF("SLEEP", "sleep_quiesce_complete section_stopped free=%u largest=%u",
                static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()));
        EpubDiagnostics::phaseRecord("sleep_section_stopped");
      }
#if FREEINK_CAP_BLE_HID_HOST
      // Keep an established host connected across ordinary navigation. Yield
      // only for activities that declare a real memory-sensitive phase (for
      // example, opening a book or building an EPUB section).
      const bool bleRunning = BleHid.isRunning();
      if (bleinput::activityTransitionNeedsBleSuspension(bleRunning, currentActivity->bluetoothResourceSensitive())) {
        bleinput::recordDiagnosticEvent("resource_stop activity=%.20s", currentActivity->name.c_str());
        if (currentActivity->isReaderActivity()) {
          bleinput::stopForReaderResourcePressure();
        } else {
          bleinput::clearReaderBleReturnPending();
          bleinput::stop();
        }
      }
#endif
      currentActivity->onEnter();
#if FREEINK_CAP_BLE_HID_HOST
      if (leavingWebExclusive) {
        // CrossPointWebServerActivity has already torn down HTTP, WebSocket,
        // discovery, mDNS, and Wi-Fi. Re-enable normal BLE admission only
        // after the replacement UI has been constructed and entered.
        bleinput::setWebExclusive(false);
        bleinput::requestLifecycleReevaluation();
        LOG_INF("WEBMEM", "stage=exclusive_exit_ready free=%u largest=%u activity=%.20s",
                static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()),
                currentActivity ? currentActivity->name.c_str() : "none");
      }
#endif
#if FREEINK_CAP_BLE_HID_HOST
      if (recentBooksToReader) logActivityHeapShape("reader_transition_after_reader_activity_enter");
      if (readerShellToConcrete) logActivityHeapShape("reader_transition_after_epub_reader_enter");
#endif
      bleinput::requestLifecycleReevaluation();
#if NOOIR_EPUB_DIAGNOSTICS
      if (!lifecycleHomeConstructionRecorded &&
          (currentActivity->isHomeActivity() || currentActivity->name == "RecentBooks")) {
        lifecycleHomeConstructionRecorded = true;
        EpubDiagnostics::phaseRecord("boot_home_construct");
      }
#endif

      // onEnter may request another pending action, we will handle it in the next loop iteration
      continue;
    }
  }

  // Keep optional interface-font I/O out of the render task and out of the
  // input path above. One bounded .cpfont metadata file is processed per main
  // loop pass; built-in UI remains active until all three sizes finish.
  if (currentActivity) {
    const bool webExclusiveActivityActive = currentActivity->name == "CrossPointWebServer" ||
        std::any_of(stackActivities.begin(), stackActivities.end(), [](const auto& activity) {
          return activity && activity->name == "CrossPointWebServer";
        });
    const bool sdIndependentScreen = isSdIndependentUiActivityName(currentActivity->name);
    const bool readerActivityActive = isReaderActivity();
    bool bluetoothOwnsHeap = bleinput::reAdmissionPending();
#if FREEINK_CAP_BLE_HID_HOST
    bluetoothOwnsHeap = bluetoothOwnsHeap || BleHid.isRunning();
#endif
    // Heavy activities unload custom interface fonts. Do not reload while a
    // Reader is current or on the activity stack, Web owns the heap, an
    // independent recovery/sleep screen is active, or BLE admission is
    // pending/active. A light activity will lazily restore the saved choice.
    if (InterfaceFontSelection::mayLoadUiFontsNow(readerActivityActive, webExclusiveActivityActive,
                                                  sdIndependentScreen, bluetoothOwnsHeap)) {
      // UI font load steps register/replace renderer families. Serialize them
      // with render use just like the lifecycle teardown path.
      RenderLock uiFontLock;
      sdFontSystem.ensureUiLoaded(renderer);
      if (sdFontSystem.progressUiLoad(renderer)) requestUpdate();
    }
  }

  if (requestedUpdate.load(std::memory_order_acquire)) {
    // Consume the deferred request and publish queued state together under
    // the same lock so lifecycle admission cannot observe a false-idle gap.
    queueRenderNotification();
  }
}

void ActivityManager::exitActivity(const RenderLock& lock) {
  // Note: lock must be held by the caller
  if (currentActivity) {
    const bool readerActivity = currentActivity->isReaderActivity();
    const bool readingStatsActivity = currentActivity->name == "ReadingStats" || currentActivity->name == "ReadingCalendar";
    const bool epubReaderActivity = currentActivity->name == "EpubReader";
    const bool wifiSelectionActivity = currentActivity->name == "WifiSelection";
    currentActivity->onExit();
    currentActivity.reset();
    if (readingStatsActivity) {
      LOG_INF("RSDIAG", "stage=child_destroyed free=%u largest=%u",
              static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()));
    }
#if FREEINK_CAP_BLE_HID_HOST
    if (epubReaderActivity) {
      LOG_INF("MEM", "reader_exit stage=activity_destroyed free=%u max=%u",
              static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()));
      logActivityHeapShape("after_activity_destroyed");
    }
    if (wifiSelectionActivity) {
      LOG_INF("WIFI", "selection_destroyed free=%u largest=%u",
              static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()));
    }
#endif
#if NOOIR_EPUB_DIAGNOSTICS
    if (readerActivity) EpubDiagnostics::phaseRecord("reader_exit_activity_destroyed");
#endif
    bleinput::requestLifecycleReevaluation();
  }
}

void ActivityManager::replaceActivity(std::unique_ptr<Activity>&& newActivity) {
  pendingSettingsConstruction = false;
  // Note: no lock here, this is usually called by loop() and we may run into deadlock
  if (currentActivity) {
    const bool recentBooksToReader = currentActivity->name == "RecentBooks" && newActivity && newActivity->name == "Reader";
#if FREEINK_CAP_BLE_HID_HOST
    if (recentBooksToReader) logActivityHeapShape("reader_transition_requested_reader_shell_pending");
#endif
    // Defer launch if we're currently in an activity, to avoid deleting the current activity
    // leading to the "delete this" problem
    pendingActivity = std::move(newActivity);
    pendingAction = PendingAction::Replace;
  } else {
    // No current activity, safe to launch immediately
    currentActivity = std::move(newActivity);
    bleinput::clearNoDeviceTimeoutSuppression();
#if FREEINK_CAP_BLE_HID_HOST
    if (bleinput::activityTransitionNeedsBleSuspension(BleHid.isRunning(),
                                                       currentActivity->bluetoothResourceSensitive())) {
      bleinput::recordDiagnosticEvent("resource_stop activity=%.20s", currentActivity->name.c_str());
      if (currentActivity->isReaderActivity()) {
        bleinput::stopForReaderResourcePressure();
      } else {
        bleinput::clearReaderBleReturnPending();
        bleinput::stop();
      }
    }
#endif
    currentActivity->onEnter();
    bleinput::requestLifecycleReevaluation();
#if NOOIR_EPUB_DIAGNOSTICS
    if (!lifecycleHomeConstructionRecorded &&
        (currentActivity->isHomeActivity() || currentActivity->name == "RecentBooks")) {
      lifecycleHomeConstructionRecorded = true;
      EpubDiagnostics::phaseRecord("boot_home_construct");
    }
#endif
  }
}

void ActivityManager::goToFileTransfer() {
  bleinput::setWebExclusive(true);
  replaceActivity(std::make_unique<CrossPointWebServerActivity>(renderer, mappedInput));
}

void ActivityManager::goToSettings() {
  if (currentActivity) {
    pendingActivity.reset();
    pendingSettingsConstruction = true;
    pendingAction = PendingAction::Replace;
    return;
  }
#if FREEINK_CAP_BLE_HID_HOST
  if (BleHid.isRunning()) bleinput::stop();
#endif
  replaceActivity(std::make_unique<SettingsActivity>(renderer, mappedInput));
}

void ActivityManager::goToFileBrowser(std::string path) {
  if (GUI.usesGraphicalLibrary()) {
    replaceActivity(std::make_unique<FolioLibraryActivity>(renderer, mappedInput, path.empty() ? "/" : std::move(path)));
    return;
  }
  replaceActivity(std::make_unique<FileBrowserActivity>(renderer, mappedInput, std::move(path)));
}

void ActivityManager::goToFolioShelf(const uint8_t tab) {
  if (!prepareWebExclusiveExit()) return;
  replaceActivity(std::make_unique<RecentBooksActivity>(renderer, mappedInput, tab));
}

void ActivityManager::goToRecentBooks() {
  if (!prepareWebExclusiveExit()) return;
  replaceActivity(std::make_unique<RecentBooksActivity>(renderer, mappedInput));
}

void ActivityManager::goToClippings() {
  if (!prepareWebExclusiveExit()) return;
  replaceActivity(std::make_unique<EpubReaderClippingListActivity>(renderer, mappedInput, "", "All books", true));
}

void ActivityManager::goToReadingStats() {
  replaceActivity(std::make_unique<ReadingStatsActivity>(renderer, mappedInput));
}

void ActivityManager::goToToDoList() {
  replaceActivity(std::make_unique<ToDoListActivity>(renderer, mappedInput));
}

void ActivityManager::goToBrowser() {
  const auto& servers = OPDS_STORE.getServers();
  // Skip the server picker when there's only one server configured
  if (servers.size() == 1) {
    replaceActivity(std::make_unique<OpdsBookBrowserActivity>(renderer, mappedInput, servers[0]));
  } else {
    replaceActivity(std::make_unique<OpdsServerListActivity>(renderer, mappedInput, true));
  }
}

void ActivityManager::goToReader(std::string path) {
  logCbzPath("activity-manager-go-to-reader", path);
  replaceActivity(std::make_unique<ReaderActivity>(renderer, mappedInput, std::move(path)));
}

void ActivityManager::goToReaderAtBookmark(std::string path, ProgressChangeResult bookmark) {
  logCbzPath("activity-manager-go-to-reader-bookmark", path);
  replaceActivity(std::make_unique<ReaderActivity>(renderer, mappedInput, std::move(path), std::move(bookmark)));
}

void ActivityManager::goToSleep(bool fromTimeout) {
  replaceActivity(std::make_unique<SleepActivity>(renderer, mappedInput, fromTimeout));
  loop();  // Important: sleep screen must be rendered immediately, the caller will go to sleep right after this returns
}

void ActivityManager::goToBoot() { replaceActivity(std::make_unique<BootActivity>(renderer, mappedInput)); }

void ActivityManager::goToFullScreenMessage(std::string message, EpdFontFamily::Style style) {
  replaceActivity(std::make_unique<FullScreenMessageActivity>(renderer, mappedInput, std::move(message), style));
}

void ActivityManager::goHome(HomeMenuItem initialMenuItem) {
  // Web/Wi-Fi owns the heap until its complete teardown. Do this before the
  // replacement RecentBooks/Home activity is constructed; its persistent
  // library reconstruction then runs only after the exclusive session ends.
  if (!prepareWebExclusiveExit()) return;
  if (GUI.usesBookshelfHome()) {
    replaceActivity(std::make_unique<RecentBooksActivity>(renderer, mappedInput));
    return;
  }
  if (initialMenuItem == HomeMenuItem::NONE && currentActivity) {
    const auto& activityName = currentActivity->name;
    if (activityName == "FileBrowser") {
      initialMenuItem = HomeMenuItem::FILE_BROWSER;
    } else if (activityName == "RecentBooks") {
      initialMenuItem = HomeMenuItem::RECENTS;
    } else if (activityName == "OpdsBookBrowser") {
      initialMenuItem = HomeMenuItem::OPDS_BROWSER;
    } else if (activityName == "CrossPointWebServer") {
      initialMenuItem = HomeMenuItem::FILE_TRANSFER;
    } else if (activityName == "Settings") {
      initialMenuItem = HomeMenuItem::SETTINGS_MENU;
    }
  }
  replaceActivity(std::make_unique<HomeActivity>(renderer, mappedInput, initialMenuItem));
}

bool ActivityManager::prepareWebExclusiveExit() {
  CrossPointWebServerActivity* webActivity = nullptr;
  if (currentActivity && currentActivity->name == "CrossPointWebServer") {
    webActivity = static_cast<CrossPointWebServerActivity*>(currentActivity.get());
  }
  if (!webActivity) {
    for (auto it = stackActivities.rbegin(); it != stackActivities.rend(); ++it) {
      if (*it && (*it)->name == "CrossPointWebServer") {
        webActivity = static_cast<CrossPointWebServerActivity*>(it->get());
        break;
      }
    }
  }
  if (!webActivity) return true;

  const bool released = webActivity->prepareForExit();
  if (!released) {
    LOG_ERR("WEBMEM", "stage=exclusive_exit_blocked free=%u largest=%u",
            static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()));
  }
  return released;
}
void ActivityManager::goToCrashReport() { replaceActivity(std::make_unique<CrashActivity>(renderer, mappedInput)); }

void ActivityManager::pushActivity(std::unique_ptr<Activity>&& activity) {
  pendingSettingsConstruction = false;
  if (pendingActivity) {
    // Should never happen in practice
    LOG_ERR("ACT", "pendingActivity while pushActivity is not expected");
    pendingActivity.reset();
  }
  pendingActivity = std::move(activity);
  pendingAction = PendingAction::Push;
}

void ActivityManager::popActivity() {
  pendingSettingsConstruction = false;
  if (pendingActivity) {
    // Should never happen in practice
    LOG_ERR("ACT", "pendingActivity while popActivity is not expected");
    pendingActivity.reset();
  }
  pendingAction = PendingAction::Pop;
}

bool ActivityManager::preventAutoSleep() const { return currentActivity && currentActivity->preventAutoSleep(); }

void ActivityManager::requestSleep() { sleepRequested = true; }

bool ActivityManager::consumeSleepRequest() {
  const bool requested = sleepRequested;
  sleepRequested = false;
  return requested;
}

bool ActivityManager::isReaderActivity() const {
  return std::any_of(stackActivities.begin(), stackActivities.end(),
                     [](const auto& activity) { return activity->isReaderActivity(); }) ||
         (currentActivity && currentActivity->isReaderActivity());
}

bool ActivityManager::isCurrentReaderActivity() const {
  return currentActivity && currentActivity->isReaderActivity();
}

const char* ActivityManager::currentActivityName() const {
  return currentActivity ? currentActivity->name.c_str() : "none";
}

bool ActivityManager::bluetoothShouldBeActive() const {
  return currentActivity && currentActivity->acceptsBluetoothInput();
}

bool ActivityManager::bluetoothResourceSensitive() const {
  if (currentActivity && currentActivity->bluetoothResourceSensitive()) return true;
  // BluetoothSettings is an explicit, lightweight BLE control surface. Its
  // parent Settings activity remains on the stack while the child is shown,
  // but that parent must not keep a user-requested BLE start blocked until the
  // user navigates Home.
  if (currentActivity && currentActivity->name == "BluetoothSettings") {
    return std::any_of(stackActivities.begin(), stackActivities.end(), [](const auto& activity) {
      // Settings is the lightweight parent that remains underneath this
      // child. A Reader (or another genuinely sensitive parent) still wins.
      return activity->name != "Settings" && activity->bluetoothResourceSensitive();
    });
  }
  return std::any_of(stackActivities.begin(), stackActivities.end(),
                     [](const auto& activity) { return activity->bluetoothResourceSensitive(); });
}

bool ActivityManager::bluetoothRenderSafe() const {
  return currentActivity && currentActivity->bluetoothRenderSafe();
}

void ActivityManager::notifyBluetoothAdmission() {
  if (currentActivity && currentActivity->name == "Settings") {
    static_cast<SettingsActivity*>(currentActivity.get())->markBluetoothAdmitted();
  }
}

void ActivityManager::notifyBluetoothActivationResult(const bool started) {
  if (currentActivity && currentActivity->name == "BluetoothSettings") {
    static_cast<BluetoothSettingsActivity*>(currentActivity.get())->onBluetoothActivationResult(started);
  }
}

void ActivityManager::setBluetoothSettingsActivationFailed(const bool failed) {
#if FREEINK_CAP_BLE_HID_HOST
  bluetoothSettingsActivationFailed_.store(failed, std::memory_order_release);
#else
  (void)failed;
#endif
}

bool ActivityManager::bluetoothSettingsActivationFailed() const {
#if FREEINK_CAP_BLE_HID_HOST
  return bluetoothSettingsActivationFailed_.load(std::memory_order_acquire);
#else
  return false;
#endif
}

bool ActivityManager::hasPendingRenderWork() const {
  taskENTER_CRITICAL(&activityManagerSpinlock);
  const bool pending = requestedUpdate.load(std::memory_order_acquire) ||
                       renderQueued.load(std::memory_order_acquire) ||
                       renderInFlight.load(std::memory_order_acquire);
  taskEXIT_CRITICAL(&activityManagerSpinlock);
  return pending;
}

void ActivityManager::postBluetoothNotification(const char* message) {
#if FREEINK_CAP_BLE_HID_HOST
  if (!message || !message[0]) return;
  taskENTER_CRITICAL(&activityManagerSpinlock);
  strncpy(pendingBluetoothNotification, message, sizeof(pendingBluetoothNotification) - 1);
  pendingBluetoothNotification[sizeof(pendingBluetoothNotification) - 1] = '\0';
  hasBluetoothNotification = true;
  taskEXIT_CRITICAL(&activityManagerSpinlock);
  if (!isReaderActivity()) requestUpdate();
#else
  (void)message;
#endif
}

void ActivityManager::postSettingsRecoveryNotification(const char* message) {
  if (!message || !message[0]) return;
  taskENTER_CRITICAL(&activityManagerSpinlock);
  strncpy(pendingSettingsNotification, message, sizeof(pendingSettingsNotification) - 1);
  pendingSettingsNotification[sizeof(pendingSettingsNotification) - 1] = '\0';
  hasSettingsNotification = true;
  taskEXIT_CRITICAL(&activityManagerSpinlock);
  requestUpdate();
}

bool ActivityManager::skipLoopDelay() const { return currentActivity && currentActivity->skipLoopDelay(); }

ScreenshotInfo ActivityManager::getScreenshotInfo() const {
  if (currentActivity) {
    return currentActivity->getScreenshotInfo();
  }
  return {};
}

void ActivityManager::requestUpdate(bool immediate) {
  if (immediate) {
    queueRenderNotification();
  } else {
    // Deferring the update until current loop is finished
    // This is to avoid multiple updates being requested in the same loop
    requestedUpdate = true;
  }
}

void ActivityManager::queueRenderNotification() {
  taskENTER_CRITICAL(&activityManagerSpinlock);
  if (renderTaskHandle) {
    requestedUpdate.exchange(false, std::memory_order_acq_rel);
    // Publish queued state before handing the notification to the render task.
    renderQueued.store(true, std::memory_order_release);
    xTaskNotify(renderTaskHandle, 1, eIncrement);
  }
  taskEXIT_CRITICAL(&activityManagerSpinlock);
}
void ActivityManager::requestUpdateAndWait() {
  if (!renderTaskHandle) {
    return;
  }

  // Atomic section to perform checks
  taskENTER_CRITICAL(&activityManagerSpinlock);
  auto currTaskHandler = xTaskGetCurrentTaskHandle();
  auto mutexHolder = xSemaphoreGetMutexHolder(renderingMutex);
  bool isRenderTask = (currTaskHandler == renderTaskHandle);
  bool alreadyWaiting = (waitingTaskHandle != nullptr);
  bool holdingRenderLock = (mutexHolder == currTaskHandler);
  if (!alreadyWaiting && !isRenderTask && !holdingRenderLock) {
    waitingTaskHandle = currTaskHandler;
  }
  taskEXIT_CRITICAL(&activityManagerSpinlock);

  // Render task cannot call requestUpdateAndWait() or it will cause a deadlock
  assert(!isRenderTask && "Render task cannot call requestUpdateAndWait()");

  // There should never be the case where 2 tasks are waiting for a render at the same time
  assert(!alreadyWaiting && "Already waiting for a render to complete");

  // Cannot call while holding RenderLock or it will cause a deadlock
  assert(!holdingRenderLock && "Cannot call requestUpdateAndWait() while holding RenderLock");

  queueRenderNotification();
  ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
}

// RenderLock

RenderLock::RenderLock() {
  xSemaphoreTake(activityManager.renderingMutex, portMAX_DELAY);
  isLocked = true;
}

RenderLock::RenderLock([[maybe_unused]] Activity&) {
  xSemaphoreTake(activityManager.renderingMutex, portMAX_DELAY);
  isLocked = true;
}

RenderLock::~RenderLock() {
  if (isLocked) {
    xSemaphoreGive(activityManager.renderingMutex);
    isLocked = false;
  }
}

void RenderLock::unlock() {
  if (isLocked) {
    xSemaphoreGive(activityManager.renderingMutex);
    isLocked = false;
  }
}

/**
 *
 * Checks if renderingMutex is busy.
 *
 * @return true if renderingMutex is busy, otherwise false.
 *
 */
bool RenderLock::peek() { return xQueuePeek(activityManager.renderingMutex, NULL, 0) != pdTRUE; };
