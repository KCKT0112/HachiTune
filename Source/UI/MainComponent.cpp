#include "MainComponent.h"
#include "../Audio/IO/MidiExporter.h"
#include "../Models/ProjectSerializer.h"
#include "../Utils/AppLogger.h"
#include "../Utils/Constants.h"
#include "../Utils/F0Smoother.h"
#include "../Utils/Localization.h"
#include "../Utils/MelSpectrogram.h"
#include "../Utils/PitchCurveProcessor.h"
#include "../Utils/PlatformPaths.h"
#include "../Utils/WindowSizing.h"
#include <atomic>
#include <climits>
#include <iostream>

MainComponent::MainComponent(bool enableAudioDevice)
    : enableAudioDeviceFlag(enableAudioDevice) {
  LOG("MainComponent: constructor start");
  setSize(WindowSizing::kDefaultWidth, WindowSizing::kDefaultHeight);
  setOpaque(true); // Required for native title bar

  LOG("MainComponent: creating core components...");
  // Initialize components
  project = std::make_unique<Project>();
  if (enableAudioDeviceFlag)
    audioEngine = std::make_unique<AudioEngine>();
  fcpePitchDetector = std::make_unique<FCPEPitchDetector>();
  rmvpePitchDetector = std::make_unique<RMVPEPitchDetector>();
  vocoder = std::make_unique<Vocoder>();
  undoManager = std::make_unique<PitchUndoManager>(100);

  // Initialize new modular components
  fileManager = std::make_unique<AudioFileManager>();
  audioAnalyzer = std::make_unique<AudioAnalyzer>();
  incrementalSynth = std::make_unique<IncrementalSynthesizer>();
  playbackController = std::make_unique<PlaybackController>();
  menuHandler = std::make_unique<MenuHandler>();
  settingsManager = std::make_unique<SettingsManager>();

  LOG("MainComponent: loading ONNX models...");
  // Try to load FCPE model
  auto modelsDir = PlatformPaths::getModelsDirectory();

  fcpeModelPath = modelsDir.getChildFile("fcpe.onnx");
  melFilterbankPath = modelsDir.getChildFile("mel_filterbank.bin");
  centTablePath = modelsDir.getChildFile("cent_table.bin");
  rmvpeModelPath = modelsDir.getChildFile("rmvpe.onnx");
  someModelPath = modelsDir.getChildFile("some.onnx");

  // Initialize legacy SOME detector
  someDetector = std::make_unique<SOMEDetector>();
  reloadInferenceModels(false);

  LOG("MainComponent: wiring up components...");
  // Wire up modular components (after all detectors are initialized)
  audioAnalyzer->setFCPEDetector(fcpePitchDetector.get());
  audioAnalyzer->setRMVPEDetector(rmvpePitchDetector.get());
  audioAnalyzer->setSOMEDetector(someDetector.get());

  // Apply pitch detector type from settings
  audioAnalyzer->setPitchDetectorType(settingsManager->getPitchDetectorType());

  incrementalSynth->setVocoder(vocoder.get());
  playbackController->setAudioEngine(audioEngine.get());
  menuHandler->setUndoManager(undoManager.get());
  menuHandler->setPluginMode(isPluginMode());
  settingsManager->setVocoder(vocoder.get());

  // Load vocoder settings
  settingsManager->applySettings();

  LOG("MainComponent: initializing audio device...");
  // Initialize audio (standalone app only)
  if (audioEngine)
    audioEngine->initializeAudio();
  LOG("MainComponent: audio initialized");

  LOG("MainComponent: setting up callbacks...");

  // Setup MenuHandler callbacks
  menuHandler->onOpenFile = [this]() { openFile(); };
  menuHandler->onSaveProject = [this]() { saveProject(); };
  menuHandler->onExportFile = [this]() { exportFile(); };
  menuHandler->onExportMidi = [this]() { exportMidiFile(); };
  menuHandler->onUndo = [this]() { undo(); };
  menuHandler->onRedo = [this]() { redo(); };
  menuHandler->onShowSettings = [this]() { showSettings(); };
  menuHandler->onQuit = [this]() {
    juce::JUCEApplication::getInstance()->systemRequestedQuit();
  };

  // View menu callbacks
  menuHandler->setShowDeltaPitch(settingsManager->getShowDeltaPitch());
  menuHandler->setShowBasePitch(settingsManager->getShowBasePitch());
  pianoRoll.setShowDeltaPitch(settingsManager->getShowDeltaPitch());
  pianoRoll.setShowBasePitch(settingsManager->getShowBasePitch());

  menuHandler->onShowDeltaPitchChanged = [this](bool show) {
    pianoRoll.setShowDeltaPitch(show);
    settingsManager->setShowDeltaPitch(show);
    settingsManager->saveConfig();
  };
  menuHandler->onShowBasePitchChanged = [this](bool show) {
    pianoRoll.setShowBasePitch(show);
    settingsManager->setShowBasePitch(show);
    settingsManager->saveConfig();
  };

  // Add child components - macOS uses native menu, others use in-app menu bar
#if JUCE_MAC
  if (!isPluginMode())
    juce::MenuBarModel::setMacMainMenu(menuHandler.get());
#else
  menuBar.setModel(menuHandler.get());
  menuBar.setLookAndFeel(&menuBarLookAndFeel);
  addAndMakeVisible(menuBar);
#endif
  addAndMakeVisible(toolbar);
  addAndMakeVisible(workspace);

  // Setup workspace with piano roll as main content
  workspace.setMainContent(&pianoRoll);

  // Add parameter panel to workspace (visible by default)
  workspace.addPanel("parameters", TR("panel.parameters"), &parameterPanel,
                     true);

  // Configure toolbar for plugin mode
  if (isPluginMode())
    toolbar.setPluginMode(true);

  // Set undo manager for piano roll
  pianoRoll.setUndoManager(undoManager.get());

  // Setup toolbar callbacks
  toolbar.onPlay = [this]() { play(); };
  toolbar.onPause = [this]() { pause(); };
  toolbar.onStop = [this]() { stop(); };
  toolbar.onGoToStart = [this]() { seek(0.0); };
  toolbar.onGoToEnd = [this]() {
    if (project)
      seek(project->getAudioData().getDuration());
  };
  toolbar.onZoomChanged = [this](float pps) { onZoomChanged(pps); };
  toolbar.onEditModeChanged = [this](EditMode mode) { setEditMode(mode); };
  toolbar.onLoopToggled = [this](bool enabled) {
    if (!project)
      return;
    project->setLoopEnabled(enabled);
    const auto &loopRange = project->getLoopRange();
    toolbar.setLoopEnabled(loopRange.enabled);
    if (audioEngine) {
      if (loopRange.enabled)
        audioEngine->setLoopRange(loopRange.startSeconds, loopRange.endSeconds);
      audioEngine->setLoopEnabled(loopRange.enabled);
    }
    pianoRoll.repaint();
  };
  toolbar.onToggleParameters = [this](bool visible) {
    workspace.showPanel("parameters", visible);
  };

  // Plugin mode callbacks
  toolbar.onReanalyze = [this]() {
    if (onReanalyzeRequested)
      onReanalyzeRequested();
  };
  // Removed onRender callback - Melodyne-style: edits automatically trigger
  // real-time processing

  // Setup piano roll callbacks
  pianoRoll.onSeek = [this](double time) { seek(time); };
  pianoRoll.onNoteSelected = [this](Note *note) { onNoteSelected(note); };
  pianoRoll.onPitchEdited = [this]() { onPitchEdited(); };
  pianoRoll.onPitchEditFinished = [this]() {
    resynthesizeIncremental();
    // Melodyne-style: trigger real-time processor update in plugin mode
    if (isPluginMode() && onPitchEditFinished)
      onPitchEditFinished();
  };
  pianoRoll.onZoomChanged = [this](float pps) { onZoomChanged(pps); };
  pianoRoll.onUndo = [this]() { undo(); };
  pianoRoll.onRedo = [this]() { redo(); };
  pianoRoll.onPlayPause = [this]() {
    if (isPlaying)
      pause();
    else
      play();
  };
  pianoRoll.onLoopRangeChanged = [this](const LoopRange &range) {
    toolbar.setLoopEnabled(range.enabled);
    if (audioEngine) {
      audioEngine->setLoopRange(range.startSeconds, range.endSeconds);
      audioEngine->setLoopEnabled(range.enabled);
    }
  };

  // Setup parameter panel callbacks
  parameterPanel.onParameterChanged = [this]() { onPitchEdited(); };
  parameterPanel.onParameterEditFinished = [this]() {
    resynthesizeIncremental();
    // Melodyne-style: trigger real-time processor update in plugin mode
    if (isPluginMode() && onPitchEditFinished)
      onPitchEditFinished();
  };
  parameterPanel.onGlobalPitchChanged = [this]() {
    pianoRoll.repaint(); // Update display
  };
  parameterPanel.onVolumeChanged = [this](float dB) {
    if (audioEngine)
      audioEngine->setVolumeDb(dB);
  };
  parameterPanel.setProject(project.get());

  // Sync toolbar toggle with panel visibility
  toolbar.setParametersVisible(workspace.isPanelVisible("parameters"));
  workspace.onPanelVisibilityChanged = [this](const juce::String& id, bool visible) {
    if (id == "parameters")
      toolbar.setParametersVisible(visible);
  };

  // Setup audio engine callbacks
  if (audioEngine) {
    juce::Component::SafePointer<MainComponent> safeThis(this);

    audioEngine->setPositionCallback([safeThis](double position) {
      if (safeThis == nullptr)
        return;
      // Throttle cursor updates - store position and let timer handle it
      safeThis->pendingCursorTime.store(position);
      safeThis->hasPendingCursorUpdate.store(true);
    });

    audioEngine->setFinishCallback([safeThis]() {
      if (safeThis == nullptr)
        return;
      safeThis->isPlaying = false;
      safeThis->toolbar.setPlaying(false);
    });
  }

  // Set initial project
  pianoRoll.setProject(project.get());

  // Add keyboard listener
  addKeyListener(this);
  setWantsKeyboardFocus(true);

  // Load config
  if (enableAudioDeviceFlag)
    settingsManager->loadConfig();

  LOG("MainComponent: setting up constraint layout...");
  // Setup Kiwi constraint layout
  setupConstraints();

  LOG("MainComponent: starting timer...");
  // Start timer for UI updates
  startTimerHz(30);
  LOG("MainComponent: constructor complete");
}

GPUProvider MainComponent::getProviderFromDevice(
    const juce::String &device) const {
  if (device == "CUDA")
    return GPUProvider::CUDA;
  if (device == "DirectML")
    return GPUProvider::DirectML;
  if (device == "CoreML")
    return GPUProvider::CoreML;
  if (device.isNotEmpty() && device != "CPU")
    LOG("Unsupported pitch detector device: " + device + ", using CPU");
  return GPUProvider::CPU;
}

void MainComponent::reloadInferenceModels(bool async) {
  if (!settingsManager)
    return;

  const auto device = settingsManager->getDevice();
  const auto provider = getProviderFromDevice(device);
  int deviceId = settingsManager->getGPUDeviceId();
  if (deviceId < 0)
    deviceId = 0;

  auto fcpePath = fcpeModelPath;
  auto melPath = melFilterbankPath;
  auto centPath = centTablePath;
  auto rmvpePath = rmvpeModelPath;
  auto somePath = someModelPath;

  auto reloadTask = [device, provider, deviceId, fcpePath, melPath, centPath,
                     rmvpePath, somePath](MainComponent *self) {
    if (!self)
      return;

    if (self->fcpePitchDetector) {
      if (fcpePath.existsAsFile()) {
        LOG("MainComponent: loading FCPE model (device " + device + ", id " +
            juce::String(deviceId) + ")...");
        if (self->fcpePitchDetector->loadModel(fcpePath, melPath, centPath,
                                               provider, deviceId)) {
          LOG("FCPE pitch detector loaded successfully");
        } else {
          LOG("Failed to load FCPE model");
        }
      } else {
        LOG("FCPE model not found at: " + fcpePath.getFullPathName());
      }
    }

    if (self->rmvpePitchDetector) {
      if (rmvpePath.existsAsFile()) {
        LOG("MainComponent: loading RMVPE model (device " + device + ", id " +
            juce::String(deviceId) + ")...");
        if (self->rmvpePitchDetector->loadModel(rmvpePath, provider,
                                                deviceId)) {
          LOG("RMVPE pitch detector loaded successfully");
        } else {
          LOG("Failed to load RMVPE model");
        }
      } else {
        LOG("RMVPE model not found at: " + rmvpePath.getFullPathName());
      }
    }

    if (self->someDetector) {
      if (somePath.existsAsFile()) {
        LOG("MainComponent: loading SOME model (device " + device + ", id " +
            juce::String(deviceId) + ")...");
        if (self->someDetector->loadModel(somePath, provider, deviceId)) {
          LOG("SOME detector loaded successfully");
        } else {
          LOG("Failed to load SOME model");
        }
      } else {
        LOG("SOME model not found at: " + somePath.getFullPathName());
      }
    }
  };

  if (!async) {
    reloadTask(this);
    return;
  }

  if (isReloadingModels.exchange(true))
    return;

  if (modelReloadThread.joinable())
    modelReloadThread.join();

  juce::Component::SafePointer<MainComponent> safeThis(this);
  modelReloadThread = std::thread([safeThis, reloadTask]() mutable {
    if (safeThis == nullptr)
      return;
    reloadTask(safeThis.getComponent());
    if (safeThis != nullptr)
      safeThis->isReloadingModels = false;
  });
}

bool MainComponent::isInferenceBusy() const {
  if (isLoadingAudio.load())
    return true;
  if (audioAnalyzer && audioAnalyzer->isAnalyzing())
    return true;
  if (incrementalSynth && incrementalSynth->isSynthesizing())
    return true;
  if (isRendering.load())
    return true;
  if (isReloadingModels.load())
    return true;
  return false;
}

MainComponent::~MainComponent() {
#if JUCE_MAC
  juce::MenuBarModel::setMacMainMenu(nullptr);
#else
  menuBar.setModel(nullptr);
  menuBar.setLookAndFeel(nullptr);
#endif
  removeKeyListener(this);
  stopTimer();

  if (modelReloadThread.joinable())
    modelReloadThread.join();

  cancelLoading = true;
  if (loaderThread.joinable())
    loaderThread.join();
  if (loaderJoinerThread.joinable())
    loaderJoinerThread.join();

  cancelRender = true;
  if (renderThread.joinable())
    renderThread.join();

  if (audioEngine) {
    audioEngine->clearCallbacks();
    audioEngine->shutdownAudio();
  }

  if (enableAudioDeviceFlag)
    settingsManager->saveConfig();
}

juce::Point<int> MainComponent::getSavedWindowSize() const {
  if (settingsManager)
    return {settingsManager->getWindowWidth(),
            settingsManager->getWindowHeight()};
  return {WindowSizing::kDefaultWidth, WindowSizing::kDefaultHeight};
}

void MainComponent::paint(juce::Graphics &g) {
  g.fillAll(juce::Colour(APP_COLOR_BACKGROUND));
}

void MainComponent::resized() {
  // Update container size constraints
  updateLayoutConstraints();

  // Apply layout to all components
#if !JUCE_MAC
  layout.applyComponentVars(&menuBar, menuBarVars);
#endif
  layout.applyComponentVars(&toolbar, toolbarVars);
  layout.applyComponentVars(&workspace, workspaceVars);

  if (settingsOverlay)
    layout.applyComponentVars(settingsOverlay.get(), settingsOverlayVars);

  if (enableAudioDeviceFlag && settingsManager)
    settingsManager->setWindowSize(getWidth(), getHeight());
}

void MainComponent::mouseDown(const juce::MouseEvent &e) {
  juce::ignoreUnused(e);
}

void MainComponent::mouseDrag(const juce::MouseEvent &e) {
  juce::ignoreUnused(e);
}

void MainComponent::mouseDoubleClick(const juce::MouseEvent &e) {
  juce::ignoreUnused(e);
}

void MainComponent::timerCallback() {
  // Handle throttled cursor updates (30Hz max)
  if (hasPendingCursorUpdate.load()) {
    double position = pendingCursorTime.load();
    hasPendingCursorUpdate.store(false);

    pianoRoll.setCursorTime(position);
    toolbar.setCurrentTime(position);

    // Follow playback: scroll to keep cursor visible
    if (isPlaying && toolbar.isFollowPlayback()) {
      float cursorX =
          static_cast<float>(position * pianoRoll.getPixelsPerSecond());
      float viewWidth = static_cast<float>(
          pianoRoll.getWidth() - 74); // minus piano keys and scrollbar
      float scrollX = static_cast<float>(pianoRoll.getScrollX());

      // If cursor is outside visible area, scroll to center it
      if (cursorX < scrollX || cursorX > scrollX + viewWidth) {
        double newScrollX =
            std::max(0.0, static_cast<double>(cursorX - viewWidth * 0.3f));
        pianoRoll.setScrollX(newScrollX);
      }
    }
  }

  if (isLoadingAudio.load()) {
    const auto progress = static_cast<float>(loadingProgress.load());
    toolbar.setProgress(progress);

    juce::String msg;
    {
      const juce::ScopedLock sl(loadingMessageLock);
      msg = loadingMessage;
    }

    if (msg.isNotEmpty() && msg != lastLoadingMessage) {
      toolbar.showProgress(msg);
      lastLoadingMessage = msg;
    }

    return;
  }

  if (lastLoadingMessage.isNotEmpty()) {
    toolbar.hideProgress();
    lastLoadingMessage.clear();
  }
}

bool MainComponent::keyPressed(const juce::KeyPress &key,
                               juce::Component * /*originatingComponent*/) {
  // Ctrl+S: Save project
  if (key == juce::KeyPress('s', juce::ModifierKeys::ctrlModifier, 0) ||
      key == juce::KeyPress('S', juce::ModifierKeys::ctrlModifier, 0)) {
    saveProject();
    return true;
  }

  // Ctrl+Z or Cmd+Z: Undo
  if (key == juce::KeyPress('z', juce::ModifierKeys::ctrlModifier, 0) ||
      key == juce::KeyPress('z', juce::ModifierKeys::commandModifier, 0)) {
    undo();
    return true;
  }

  // Ctrl+A or Cmd+A: Select all notes
  if (key == juce::KeyPress('a', juce::ModifierKeys::ctrlModifier, 0) ||
      key == juce::KeyPress('a', juce::ModifierKeys::commandModifier, 0)) {
    if (project) {
      project->selectAllNotes();
      pianoRoll.repaint();
    }
    return true;
  }

  // Ctrl+Y or Ctrl+Shift+Z or Cmd+Shift+Z: Redo
  if (key == juce::KeyPress('y', juce::ModifierKeys::ctrlModifier, 0) ||
      key == juce::KeyPress('z',
                            juce::ModifierKeys::ctrlModifier |
                                juce::ModifierKeys::shiftModifier,
                            0) ||
      key == juce::KeyPress('z',
                            juce::ModifierKeys::commandModifier |
                                juce::ModifierKeys::shiftModifier,
                            0)) {
    redo();
    return true;
  }

  // D: Toggle draw mode
  if (key == juce::KeyPress('d') || key == juce::KeyPress('D')) {
    if (pianoRoll.getEditMode() == EditMode::Draw)
      setEditMode(EditMode::Select);
    else
      setEditMode(EditMode::Draw);
    return true;
  }

  // Space bar: toggle play/pause
  if (key == juce::KeyPress::spaceKey) {
    if (isPlaying)
      pause();
    else
      play();
    return true;
  }

  // Escape: stop (or exit draw mode)
  if (key == juce::KeyPress::escapeKey) {
    if (pianoRoll.getEditMode() == EditMode::Draw) {
      setEditMode(EditMode::Select);
    } else {
      stop();
    }
    return true;
  }
  // Home: go to start
  if (key == juce::KeyPress::homeKey) {
    seek(0.0);
    return true;
  }

  if (key == juce::KeyPress::endKey) {
    if (project) {
      seek(project->getAudioData().getDuration());
    }
    return true;
  }

  return false;
}

void MainComponent::saveProject() {
  if (!juce::MessageManager::getInstance()->isThisTheMessageThread()) {
    juce::Component::SafePointer<MainComponent> safeThis(this);
    juce::MessageManager::callAsync([safeThis]() {
      if (safeThis != nullptr)
        safeThis->saveProject();
    });
    return;
  }

  if (!project)
    return;

  auto target = project->getProjectFilePath();
  if (target == juce::File{}) {
    // Prevent re-triggering while dialog is open
    if (fileChooser != nullptr)
      return;

    // Default next to audio if possible
    auto audio = project->getFilePath();
    if (audio.existsAsFile())
      target = audio.withFileExtension("htpx");
    else
      target =
          juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
              .getChildFile("Untitled.htpx");

#if JUCE_WINDOWS && JUCE_MODAL_LOOPS_PERMITTED
    juce::FileChooser chooser(TR("dialog.save_project"), target, "*.htpx",
                              true, false, this);
    if (!chooser.browseForFileToSave(true))
      return;

    auto file = chooser.getResult();
    if (file == juce::File{})
      return;

    if (file.getFileExtension().isEmpty())
      file = file.withFileExtension("htpx");

    toolbar.showProgress(TR("progress.saving"));
    toolbar.setProgress(-1.0f);

    const bool ok = ProjectSerializer::saveToFile(*project, file);
    if (ok)
      project->setProjectFilePath(file);

    toolbar.hideProgress();
    return;
#else
    fileChooser = std::make_unique<juce::FileChooser>(
        TR("dialog.save_project"), target, "*.htpx");

    auto chooserFlags = juce::FileBrowserComponent::saveMode |
                        juce::FileBrowserComponent::canSelectFiles |
                        juce::FileBrowserComponent::warnAboutOverwriting;

    juce::Component::SafePointer<MainComponent> safeThis(this);
    fileChooser->launchAsync(chooserFlags, [safeThis](
                                               const juce::FileChooser &fc) {
      if (safeThis == nullptr)
        return;

      auto file = fc.getResult();
      safeThis->fileChooser
          .reset(); // Allow next dialog to open (must be after getResult)

      if (file == juce::File{})
        return;

      if (file.getFileExtension().isEmpty())
        file = file.withFileExtension("htpx");

      safeThis->toolbar.showProgress(TR("progress.saving"));
      safeThis->toolbar.setProgress(-1.0f);

      const bool ok = ProjectSerializer::saveToFile(*safeThis->project, file);
      if (ok)
        safeThis->project->setProjectFilePath(file);

      safeThis->toolbar.hideProgress();
    });

    return;
#endif
  }

  toolbar.showProgress(TR("progress.saving"));
  toolbar.setProgress(-1.0f);

  const bool ok = ProjectSerializer::saveToFile(*project, target);
  juce::ignoreUnused(ok);

  toolbar.hideProgress();
}

void MainComponent::openFile() {
  if (!juce::MessageManager::getInstance()->isThisTheMessageThread()) {
    juce::Component::SafePointer<MainComponent> safeThis(this);
    juce::MessageManager::callAsync([safeThis]() {
      if (safeThis != nullptr)
        safeThis->openFile();
    });
    return;
  }

  // Prevent re-triggering while dialog is open
  if (fileChooser != nullptr)
    return;

  fileChooser = std::make_unique<juce::FileChooser>(
      TR("dialog.select_audio"), juce::File{}, "*.wav;*.mp3;*.flac;*.aiff");

  auto chooserFlags = juce::FileBrowserComponent::openMode |
                      juce::FileBrowserComponent::canSelectFiles;

  juce::Component::SafePointer<MainComponent> safeThis(this);
  fileChooser->launchAsync(
      chooserFlags, [safeThis](const juce::FileChooser &fc) {
        if (safeThis == nullptr)
          return;

        auto file = fc.getResult();
        safeThis->fileChooser.reset(); // Allow next dialog to open
        if (file.existsAsFile()) {
          safeThis->loadAudioFile(file);
        }
      });
}

void MainComponent::loadAudioFile(const juce::File &file) {
  if (isLoadingAudio.load())
    return;

  cancelLoading = false;
  isLoadingAudio = true;
  loadingProgress = 0.0;
  {
    const juce::ScopedLock sl(loadingMessageLock);
    loadingMessage = TR("progress.loading_audio");
  }
  toolbar.showProgress(TR("progress.loading_audio"));
  toolbar.setProgress(0.0f);

  if (loaderThread.joinable())
    loaderThread.join();

  juce::Component::SafePointer<MainComponent> safeThis(this);
  loaderThread = std::thread([safeThis, file]() {
    if (safeThis == nullptr)
      return;

    auto updateProgress = [safeThis](double p, const juce::String &msg) {
      if (safeThis == nullptr)
        return;
      safeThis->loadingProgress = juce::jlimit(0.0, 1.0, p);
      const juce::ScopedLock sl(safeThis->loadingMessageLock);
      safeThis->loadingMessage = msg;
    };

    updateProgress(0.05, TR("progress.loading_audio"));

    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader(
        formatManager.createReaderFor(file));
    if (reader == nullptr || safeThis->cancelLoading.load()) {
      juce::MessageManager::callAsync([safeThis]() {
        if (safeThis != nullptr)
          safeThis->isLoadingAudio = false;
      });
      return;
    }

    // Read audio data
    const int numSamples = static_cast<int>(reader->lengthInSamples);
    const int srcSampleRate = static_cast<int>(reader->sampleRate);

    juce::AudioBuffer<float> buffer(1, numSamples);

    updateProgress(0.10, "Reading audio...");
    if (reader->numChannels == 1) {
      reader->read(&buffer, 0, numSamples, 0, true, false);
    } else {
      juce::AudioBuffer<float> stereoBuffer(2, numSamples);
      reader->read(&stereoBuffer, 0, numSamples, 0, true, true);

      const float *left = stereoBuffer.getReadPointer(0);
      const float *right = stereoBuffer.getReadPointer(1);
      float *mono = buffer.getWritePointer(0);

      for (int i = 0; i < numSamples; ++i)
        mono[i] = (left[i] + right[i]) * 0.5f;
    }

    if (safeThis->cancelLoading.load()) {
      juce::MessageManager::callAsync([safeThis]() {
        if (safeThis != nullptr)
          safeThis->isLoadingAudio = false;
      });
      return;
    }

    // Resample if needed
    if (srcSampleRate != SAMPLE_RATE) {
      updateProgress(0.18, "Resampling...");
      const double ratio = static_cast<double>(srcSampleRate) / SAMPLE_RATE;
      const int newNumSamples = static_cast<int>(numSamples / ratio);

      juce::AudioBuffer<float> resampledBuffer(1, newNumSamples);
      const float *src = buffer.getReadPointer(0);
      float *dst = resampledBuffer.getWritePointer(0);

      for (int i = 0; i < newNumSamples; ++i) {
        const double srcPos = i * ratio;
        const int srcIndex = static_cast<int>(srcPos);
        const double frac = srcPos - srcIndex;

        if (srcIndex + 1 < numSamples)
          dst[i] = static_cast<float>(src[srcIndex] * (1.0 - frac) +
                                      src[srcIndex + 1] * frac);
        else
          dst[i] = src[srcIndex];
      }

      buffer = std::move(resampledBuffer);
    }

    updateProgress(0.22, "Preparing project...");
    auto newProject = std::make_shared<Project>();
    newProject->setFilePath(file);
    auto &audioData = newProject->getAudioData();
    audioData.waveform = std::move(buffer);
    audioData.sampleRate = SAMPLE_RATE;

    if (safeThis->cancelLoading.load()) {
      juce::MessageManager::callAsync([safeThis]() {
        if (safeThis != nullptr)
          safeThis->isLoadingAudio = false;
      });
      return;
    }

    updateProgress(0.25, TR("progress.analyzing_audio"));
    safeThis->analyzeAudio(*newProject, updateProgress);

    if (safeThis->cancelLoading.load()) {
      juce::MessageManager::callAsync([safeThis]() {
        if (safeThis != nullptr)
          safeThis->isLoadingAudio = false;
      });
      return;
    }

    updateProgress(0.95, "Finalizing...");

    juce::MessageManager::callAsync([safeThis, newProject]() mutable {
      if (safeThis == nullptr)
        return;

      // Clear undo history before replacing project to avoid dangling pointers
      if (safeThis->undoManager)
        safeThis->undoManager->clear();

      safeThis->project = std::make_unique<Project>(std::move(*newProject));

      // Update UI
      safeThis->pianoRoll.setProject(safeThis->project.get());
      safeThis->parameterPanel.setProject(safeThis->project.get());
      safeThis->toolbar.setTotalTime(
          safeThis->project->getAudioData().getDuration());
      safeThis->toolbar.setLoopEnabled(
          safeThis->project->getLoopRange().enabled);

      // Get audio data reference (used in multiple places below)
      auto &audioData = safeThis->project->getAudioData();

      // Set audio to engine (standalone mode only)
      // CRITICAL: In plugin mode, audioEngine is nullptr and should NEVER be
      // accessed
      if (safeThis) {
        // CRITICAL FIRST CHECK: Never call loadWaveform in plugin mode
        if (safeThis->isPluginMode()) {
          // This is expected in plugin mode - audioEngine doesn't exist
          // No need to log or call loadWaveform
        } else if (safeThis->audioEngine) {
          // Double-check audioEngine is still valid before calling
          AudioEngine *engine = safeThis->audioEngine.get();
          if (engine) {
            DBG("MainComponent::loadAudioFile - calling loadWaveform, "
                "engine="
                << juce::String::toHexString(
                       reinterpret_cast<uintptr_t>(engine)));
            try {
              engine->loadWaveform(audioData.waveform, audioData.sampleRate);
              const auto &loopRange = safeThis->project->getLoopRange();
              if (loopRange.enabled)
                engine->setLoopRange(loopRange.startSeconds,
                                     loopRange.endSeconds);
              engine->setLoopEnabled(loopRange.enabled);
            } catch (...) {
              DBG("MainComponent::loadAudioFile - EXCEPTION in loadWaveform!");
            }
          } else {
            DBG("MainComponent::loadAudioFile - engine pointer is null!");
          }
        } else {
          DBG("MainComponent::loadAudioFile - audioEngine is null in "
              "standalone mode!");
        }
      }

      // Save original waveform for incremental synthesis
      safeThis->originalWaveform.makeCopyOf(audioData.waveform);
      safeThis->hasOriginalWaveform = true;

      // Center view on detected pitch range
      const auto &f0 = audioData.f0;
      if (!f0.empty()) {
        float minF0 = 10000.0f, maxF0 = 0.0f;
        for (float freq : f0) {
          if (freq > 50.0f) // Valid pitch
          {
            minF0 = std::min(minF0, freq);
            maxF0 = std::max(maxF0, freq);
          }
        }
        if (maxF0 > minF0) {
          float minMidi = freqToMidi(minF0) - 2.0f; // Add margin
          float maxMidi = freqToMidi(maxF0) + 2.0f;
          safeThis->pianoRoll.centerOnPitchRange(minMidi, maxMidi);
        }
      }

      // Ensure vocoder is loaded (shared logic - analyzeAudio should have
      // loaded it, but double-check)
      if (!safeThis->vocoder->isLoaded()) {
        DBG("MainComponent::loadAudioFile - vocoder not loaded, loading now");
        auto modelPath = PlatformPaths::getModelsDirectory().getChildFile(
            "pc_nsf_hifigan.onnx");
        if (modelPath.existsAsFile()) {
          if (safeThis->vocoder->loadModel(modelPath)) {
            DBG("MainComponent::loadAudioFile - vocoder model loaded "
                "successfully");
          } else {
            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::WarningIcon, "Inference failed",
                "Failed to load vocoder model at:\n" +
                    modelPath.getFullPathName() +
                    "\n\nPlease check your model installation and try again.");
            return;
          }
        } else {
          juce::AlertWindow::showMessageBoxAsync(
              juce::AlertWindow::WarningIcon, "Missing model file",
              "pc_nsf_hifigan.onnx was not found at:\n" +
                  modelPath.getFullPathName() +
                  "\n\nPlease install the required model files and try again.");
          return;
        }
      }

      safeThis->repaint();
      safeThis->isLoadingAudio = false;

      // Notify plugin mode that project data is ready
      if (safeThis->isPluginMode() && safeThis->onProjectDataChanged)
        safeThis->onProjectDataChanged();
    });
  });
}

void MainComponent::analyzeAudio() {
  if (!project)
    return;

  // Run analysis in background thread to avoid blocking UI
  // All model inference (FCPE, RMVPE, SOME, vocoder) happens in background
  juce::Component::SafePointer<MainComponent> safeThis(this);

  if (loaderThread.joinable())
    loaderThread.join();

  loaderThread = std::thread([safeThis]() {
    if (safeThis == nullptr || !safeThis->project)
      return;

    // Create a copy for thread safety
    auto projectCopy = std::make_shared<Project>(*safeThis->project);

    // Perform analysis in background thread (all model inference here)
    safeThis->analyzeAudio(*projectCopy, [](double, const juce::String &) {});

    // Update main project on UI thread
    juce::MessageManager::callAsync([safeThis, projectCopy]() {
      if (safeThis == nullptr)
        return;

      // Copy analyzed data back
      safeThis->project->getAudioData().melSpectrogram =
          projectCopy->getAudioData().melSpectrogram;
      safeThis->project->getAudioData().f0 = projectCopy->getAudioData().f0;
      safeThis->project->getAudioData().voicedMask =
          projectCopy->getAudioData().voicedMask;
      safeThis->project->getAudioData().basePitch =
          projectCopy->getAudioData().basePitch;
      safeThis->project->getAudioData().deltaPitch =
          projectCopy->getAudioData().deltaPitch;

      // Update UI
      safeThis->pianoRoll.setProject(safeThis->project.get());
      safeThis->pianoRoll.repaint();

      // Trigger callbacks if needed
      if (safeThis->onProjectDataChanged)
        safeThis->onProjectDataChanged();
    });
  });
}

void MainComponent::analyzeAudio(
    Project &targetProject,
    const std::function<void(double, const juce::String &)> &onProgress,
    std::function<void()> onComplete) {
  // NOTE: This function performs all model inference operations.
  // It should ONLY be called from background threads to avoid blocking UI.
  // All model inference (FCPE, RMVPE, SOME, vocoder, mel spectrogram) happens
  // here.

  auto &audioData = targetProject.getAudioData();
  if (audioData.waveform.getNumSamples() == 0)
    return;

  auto showMissingModelAndAbort = [](const juce::String &modelName,
                                     const juce::File &path) {
    juce::MessageManager::callAsync([modelName, path]() {
      juce::AlertWindow::showMessageBoxAsync(
          juce::AlertWindow::WarningIcon, "Missing model file",
          modelName + " was not found at:\n" + path.getFullPathName() +
              "\n\nPlease install the required model files and try again.");
    });
  };

  // Extract F0
  const float *samples = audioData.waveform.getReadPointer(0);
  int numSamples = audioData.waveform.getNumSamples();

  onProgress(0.35, "Computing mel spectrogram...");
  // Compute mel spectrogram first (to know target frame count)
  // This is computationally intensive and runs in background thread
  MelSpectrogram melComputer(audioData.sampleRate, N_FFT, HOP_SIZE, NUM_MELS,
                             FMIN, FMAX);
  audioData.melSpectrogram = melComputer.compute(samples, numSamples);

  int targetFrames = static_cast<int>(audioData.melSpectrogram.size());

  onProgress(0.55, "Extracting pitch (F0)...");

  // Get pitch detector type from settings
  PitchDetectorType detectorType = settingsManager->getPitchDetectorType();

  if (detectorType == PitchDetectorType::RMVPE) {
    const auto rmvpeModelPath =
        PlatformPaths::getModelsDirectory().getChildFile("rmvpe.onnx");
    if (!rmvpeModelPath.existsAsFile() || !rmvpePitchDetector ||
        !rmvpePitchDetector->isLoaded()) {
      showMissingModelAndAbort("rmvpe.onnx", rmvpeModelPath);
      return;
    }
  } else if (detectorType == PitchDetectorType::FCPE) {
    const auto modelsDir = PlatformPaths::getModelsDirectory();
    const auto fcpeModelPath = modelsDir.getChildFile("fcpe.onnx");
    const auto melFilterbankPath = modelsDir.getChildFile("mel_filterbank.bin");
    const auto centTablePath = modelsDir.getChildFile("cent_table.bin");

    if (!fcpeModelPath.existsAsFile() || !fcpePitchDetector ||
        !fcpePitchDetector->isLoaded()) {
      showMissingModelAndAbort("fcpe.onnx", fcpeModelPath);
      return;
    }
    if (!melFilterbankPath.existsAsFile()) {
      showMissingModelAndAbort("mel_filterbank.bin", melFilterbankPath);
      return;
    }
    if (!centTablePath.existsAsFile()) {
      showMissingModelAndAbort("cent_table.bin", centTablePath);
      return;
    }
  }

  LOG("========== PITCH DETECTOR SELECTION ==========");
  LOG("Selected detector: " +
      juce::String(pitchDetectorTypeToString(detectorType)));
  LOG("RMVPE loaded: " +
      juce::String(
          rmvpePitchDetector && rmvpePitchDetector->isLoaded() ? "YES" : "NO"));
  LOG("FCPE loaded: " +
      juce::String(fcpePitchDetector && fcpePitchDetector->isLoaded() ? "YES"
                                                                      : "NO"));

  // Extract F0 based on selected detector type
  std::vector<float> extractedF0;
  if (detectorType == PitchDetectorType::RMVPE) {
    extractedF0 = rmvpePitchDetector->extractF0(samples, numSamples,
                                                audioData.sampleRate);
  } else if (detectorType == PitchDetectorType::FCPE) {
    extractedF0 =
        fcpePitchDetector->extractF0(samples, numSamples, audioData.sampleRate);
  }

  if (extractedF0.empty() || targetFrames <= 0) {
    juce::MessageManager::callAsync([]() {
      juce::AlertWindow::showMessageBoxAsync(
          juce::AlertWindow::WarningIcon, "Inference failed",
          "Failed to extract pitch (F0). Please check your model installation "
          "and settings.");
    });
    return;
  }

  {
    // Resample neural F0 (100 fps @ 16kHz) to vocoder frame rate (86.1 fps
    // @ 44.1kHz)
    audioData.f0.resize(targetFrames);

    // Time per frame for each system
    const double neuralFrameTime = 160.0 / 16000.0; // 0.01 seconds
    const double vocoderFrameTime =
        static_cast<double>(HOP_SIZE) /
        static_cast<double>(std::max(1, audioData.sampleRate));

    for (int i = 0; i < targetFrames; ++i) {
      double vocoderTime = i * vocoderFrameTime;
      double neuralFramePos = vocoderTime / neuralFrameTime;
      int srcIdx = static_cast<int>(neuralFramePos);
      double frac = neuralFramePos - srcIdx;

      if (srcIdx + 1 < static_cast<int>(extractedF0.size())) {
        float f0_a = extractedF0[srcIdx];
        float f0_b = extractedF0[srcIdx + 1];

        if (f0_a > 0.0f && f0_b > 0.0f) {
          // Log-domain interpolation for musical accuracy
          float logF0_a = std::log(f0_a);
          float logF0_b = std::log(f0_b);
          float logF0_interp = logF0_a * (1.0 - frac) + logF0_b * frac;
          audioData.f0[i] = std::exp(logF0_interp);
        } else if (f0_a > 0.0f) {
          audioData.f0[i] = f0_a;
        } else if (f0_b > 0.0f) {
          audioData.f0[i] = f0_b;
        } else {
          audioData.f0[i] = 0.0f;
        }
      } else if (srcIdx < static_cast<int>(extractedF0.size())) {
        audioData.f0[i] = extractedF0[srcIdx];
      } else {
        audioData.f0[i] = extractedF0.back() > 0.0f ? extractedF0.back() : 0.0f;
      }
    }

    // Create voiced mask
    audioData.voicedMask.resize(audioData.f0.size());
    for (size_t i = 0; i < audioData.f0.size(); ++i) {
      audioData.voicedMask[i] = audioData.f0[i] > 0;
    }

    // Apply F0 smoothing
    onProgress(0.65, "Smoothing pitch curve...");
    audioData.f0 = F0Smoother::smoothF0(audioData.f0, audioData.voicedMask);
    audioData.f0 = PitchCurveProcessor::interpolateWithUvMask(
        audioData.f0, audioData.voicedMask);
  }

  onProgress(0.75, TR("progress.loading_vocoder"));
  // Load vocoder model (model loading happens in background thread)
  auto modelPath =
      PlatformPaths::getModelsDirectory().getChildFile("pc_nsf_hifigan.onnx");

  if (!modelPath.existsAsFile() && !vocoder->isLoaded()) {
    showMissingModelAndAbort("pc_nsf_hifigan.onnx", modelPath);
    return;
  }

  if (modelPath.existsAsFile() && !vocoder->isLoaded()) {
    if (vocoder->loadModel(modelPath)) {
      DBG("Vocoder model loaded successfully: " + modelPath.getFullPathName());
    } else {
      juce::MessageManager::callAsync([modelPath]() {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon, "Inference failed",
            "Failed to load vocoder model at:\n" + modelPath.getFullPathName() +
                "\n\nPlease check your model installation and try again.");
      });
      return;
    }
  }

  onProgress(0.90, "Segmenting notes...");
  // Segment into notes (SOME model inference runs in background thread)
  segmentIntoNotes(targetProject);

  // Build dense base/delta curves from the detected pitch
  PitchCurveProcessor::rebuildCurvesFromSource(targetProject, audioData.f0);

  // Call completion callback if provided
  if (onComplete)
    onComplete();
}

void MainComponent::exportFile() {
  if (!juce::MessageManager::getInstance()->isThisTheMessageThread()) {
    juce::Component::SafePointer<MainComponent> safeThis(this);
    juce::MessageManager::callAsync([safeThis]() {
      if (safeThis != nullptr)
        safeThis->exportFile();
    });
    return;
  }

  if (!project)
    return;

  // Prevent re-triggering while dialog is open
  if (fileChooser != nullptr)
    return;

#if JUCE_WINDOWS && JUCE_MODAL_LOOPS_PERMITTED
  juce::FileChooser chooser(TR("dialog.save_audio"), juce::File{}, "*.wav",
                            true, false, this);
  if (!chooser.browseForFileToSave(true))
    return;

  auto file = chooser.getResult();
  if (file == juce::File{})
    return;

  if (file.getFileExtension().isEmpty())
    file = file.withFileExtension("wav");

  auto &audioData = project->getAudioData();

  // Show progress
  toolbar.showProgress(TR("progress.exporting_audio"));
  toolbar.setProgress(0.0f);

  // Delete existing file if it exists (to ensure clean replacement)
  if (file.existsAsFile()) {
    if (!file.deleteFile()) {
      toolbar.hideProgress();
      StyledMessageBox::show(
          this, TR("dialog.export_failed"),
          TR("dialog.failed_delete") + "\n" + file.getFullPathName(),
          StyledMessageBox::WarningIcon);
      return;
    }
  }

  toolbar.setProgress(0.3f);

  // Create output stream
  std::unique_ptr<juce::FileOutputStream> outputStream =
      std::make_unique<juce::FileOutputStream>(file);

  if (!outputStream->openedOk()) {
    toolbar.hideProgress();
    StyledMessageBox::show(
        this, TR("dialog.export_failed"),
        TR("dialog.failed_open") + "\n" + file.getFullPathName(),
        StyledMessageBox::WarningIcon);
    return;
  }

  toolbar.setProgress(0.5f);

  // Create writer
  juce::WavAudioFormat wavFormat;
  std::unique_ptr<juce::AudioFormatWriter> writer(wavFormat.createWriterFor(
      outputStream.release(), // Writer takes ownership of stream
      SAMPLE_RATE,
      1,  // mono
      16, // 16-bit
      {}, 0));

  if (writer == nullptr) {
    toolbar.hideProgress();
    StyledMessageBox::show(
        this, TR("dialog.export_failed"),
        TR("dialog.failed_create_writer") + "\n" + file.getFullPathName(),
        StyledMessageBox::WarningIcon);
    return;
  }

  toolbar.setProgress(0.7f);

  // Write audio data
  bool writeSuccess = writer->writeFromAudioSampleBuffer(
      audioData.waveform, 0, audioData.waveform.getNumSamples());

  toolbar.setProgress(0.9f);

  // Explicitly flush and close writer (destructor will also do this, but
  // explicit is better)
  writer->flush();
  writer.reset(); // Explicitly release writer and underlying stream

  toolbar.setProgress(1.0f);

  if (writeSuccess) {
    toolbar.hideProgress();
    StyledMessageBox::show(
        this, TR("dialog.export_complete"),
        TR("dialog.audio_exported") + "\n" + file.getFullPathName(),
        StyledMessageBox::InfoIcon);
  } else {
    toolbar.hideProgress();
    StyledMessageBox::show(
        this, TR("dialog.export_failed"),
        TR("dialog.failed_write") + "\n" + file.getFullPathName(),
        StyledMessageBox::WarningIcon);
  }
#else
  fileChooser = std::make_unique<juce::FileChooser>(TR("dialog.save_audio"),
                                                    juce::File{}, "*.wav");

  auto chooserFlags = juce::FileBrowserComponent::saveMode |
                      juce::FileBrowserComponent::canSelectFiles |
                      juce::FileBrowserComponent::warnAboutOverwriting;

  juce::Component::SafePointer<MainComponent> safeThis(this);
  fileChooser->launchAsync(chooserFlags, [safeThis](
                                             const juce::FileChooser &fc) {
    if (safeThis == nullptr)
      return;

    // Get result and reset fileChooser to allow next dialog
    auto file = fc.getResult();
    safeThis->fileChooser.reset();

    if (file == juce::File{})
      return;

    if (file.getFileExtension().isEmpty())
      file = file.withFileExtension("wav");

    auto &audioData = safeThis->project->getAudioData();

    // Show progress
    safeThis->toolbar.showProgress(TR("progress.exporting_audio"));
    safeThis->toolbar.setProgress(0.0f);

    // Delete existing file if it exists (to ensure clean replacement)
    if (file.existsAsFile()) {
      if (!file.deleteFile()) {
        safeThis->toolbar.hideProgress();
        StyledMessageBox::show(
            safeThis.getComponent(), TR("dialog.export_failed"),
            TR("dialog.failed_delete") + "\n" + file.getFullPathName(),
            StyledMessageBox::WarningIcon);
        return;
      }
    }

    safeThis->toolbar.setProgress(0.3f);

    // Create output stream
    std::unique_ptr<juce::FileOutputStream> outputStream =
        std::make_unique<juce::FileOutputStream>(file);

    if (!outputStream->openedOk()) {
      safeThis->toolbar.hideProgress();
      StyledMessageBox::show(
          safeThis.getComponent(), TR("dialog.export_failed"),
          TR("dialog.failed_open") + "\n" + file.getFullPathName(),
          StyledMessageBox::WarningIcon);
      return;
    }

    safeThis->toolbar.setProgress(0.5f);

    // Create writer
    juce::WavAudioFormat wavFormat;
    std::unique_ptr<juce::AudioFormatWriter> writer(wavFormat.createWriterFor(
        outputStream.release(), // Writer takes ownership of stream
        SAMPLE_RATE,
        1,  // mono
        16, // 16-bit
        {}, 0));

    if (writer == nullptr) {
      safeThis->toolbar.hideProgress();
      StyledMessageBox::show(
          safeThis.getComponent(), TR("dialog.export_failed"),
          TR("dialog.failed_create_writer") + "\n" + file.getFullPathName(),
          StyledMessageBox::WarningIcon);
      return;
    }

    safeThis->toolbar.setProgress(0.7f);

    // Write audio data
    bool writeSuccess = writer->writeFromAudioSampleBuffer(
        audioData.waveform, 0, audioData.waveform.getNumSamples());

    safeThis->toolbar.setProgress(0.9f);

    // Explicitly flush and close writer (destructor will also do this, but
    // explicit is better)
    writer->flush();
    writer.reset(); // Explicitly release writer and underlying stream

    safeThis->toolbar.setProgress(1.0f);

    if (writeSuccess) {
      safeThis->toolbar.hideProgress();
      StyledMessageBox::show(
          safeThis.getComponent(), TR("dialog.export_complete"),
          TR("dialog.audio_exported") + "\n" + file.getFullPathName(),
          StyledMessageBox::InfoIcon);
    } else {
      safeThis->toolbar.hideProgress();
      StyledMessageBox::show(
          safeThis.getComponent(), TR("dialog.export_failed"),
          TR("dialog.failed_write") + "\n" + file.getFullPathName(),
          StyledMessageBox::WarningIcon);
    }
  });
#endif
}

void MainComponent::exportMidiFile() {
  if (!juce::MessageManager::getInstance()->isThisTheMessageThread()) {
    juce::Component::SafePointer<MainComponent> safeThis(this);
    juce::MessageManager::callAsync([safeThis]() {
      if (safeThis != nullptr)
        safeThis->exportMidiFile();
    });
    return;
  }

  if (!project)
    return;

  // Prevent re-triggering while dialog is open
  if (fileChooser != nullptr)
    return;

  const auto &notes = project->getNotes();
  if (notes.empty()) {
    StyledMessageBox::show(this, TR("dialog.export_failed"),
                           TR("dialog.no_notes_to_export"),
                           StyledMessageBox::WarningIcon);
    return;
  }

  // Suggest filename based on project or audio file
  juce::File defaultFile;
  if (project->getFilePath().existsAsFile()) {
    defaultFile = project->getFilePath().withFileExtension("mid");
  } else if (project->getProjectFilePath().existsAsFile()) {
    defaultFile = project->getProjectFilePath().withFileExtension("mid");
  }

#if JUCE_WINDOWS && JUCE_MODAL_LOOPS_PERMITTED
  juce::FileChooser chooser(TR("dialog.export_midi"), defaultFile,
                            "*.mid;*.midi", true, false, this);
  if (!chooser.browseForFileToSave(true))
    return;

  auto file = chooser.getResult();
  if (file == juce::File{})
    return;

  // Ensure .mid extension
  if (file.getFileExtension().isEmpty())
    file = file.withFileExtension("mid");

  // Show progress
  toolbar.showProgress(TR("progress.exporting_midi"));
  toolbar.setProgress(0.3f);

  // Configure export options
  MidiExporter::ExportOptions options;
  options.tempo = 120.0f; // Default BPM
  options.ticksPerQuarterNote = 480;
  options.velocity = 100;
  options.quantizePitch = true; // Round to nearest semitone

  toolbar.setProgress(0.6f);

  // Export using adjusted pitch data (includes user edits)
  bool success = MidiExporter::exportToFile(project->getNotes(), file, options);

  toolbar.setProgress(1.0f);
  toolbar.hideProgress();

  if (success) {
    StyledMessageBox::show(
        this, TR("dialog.export_complete"),
        TR("dialog.midi_exported") + "\n" + file.getFullPathName(),
        StyledMessageBox::InfoIcon);
  } else {
    StyledMessageBox::show(
        this, TR("dialog.export_failed"),
        TR("dialog.failed_write_midi") + "\n" + file.getFullPathName(),
        StyledMessageBox::WarningIcon);
  }
#else
  fileChooser = std::make_unique<juce::FileChooser>(
      TR("dialog.export_midi"), defaultFile, "*.mid;*.midi");

  auto chooserFlags = juce::FileBrowserComponent::saveMode |
                      juce::FileBrowserComponent::canSelectFiles |
                      juce::FileBrowserComponent::warnAboutOverwriting;

  juce::Component::SafePointer<MainComponent> safeThis(this);
  fileChooser->launchAsync(
      chooserFlags, [safeThis](const juce::FileChooser &fc) {
        if (safeThis == nullptr)
          return;

        auto file = fc.getResult();
        safeThis->fileChooser
            .reset(); // Allow next dialog to open (must be after getResult)

        if (file == juce::File{})
          return;

        // Ensure .mid extension
        if (file.getFileExtension().isEmpty())
          file = file.withFileExtension("mid");

        // Show progress
        safeThis->toolbar.showProgress(TR("progress.exporting_midi"));
        safeThis->toolbar.setProgress(0.3f);

        // Configure export options
        MidiExporter::ExportOptions options;
        options.tempo = 120.0f; // Default BPM
        options.ticksPerQuarterNote = 480;
        options.velocity = 100;
        options.quantizePitch = true; // Round to nearest semitone

        safeThis->toolbar.setProgress(0.6f);

        // Export using adjusted pitch data (includes user edits)
        bool success = MidiExporter::exportToFile(safeThis->project->getNotes(),
                                                  file, options);

        safeThis->toolbar.setProgress(1.0f);
        safeThis->toolbar.hideProgress();

        if (success) {
          StyledMessageBox::show(
              safeThis.getComponent(), TR("dialog.export_complete"),
              TR("dialog.midi_exported") + "\n" + file.getFullPathName(),
              StyledMessageBox::InfoIcon);
        } else {
          StyledMessageBox::show(
              safeThis.getComponent(), TR("dialog.export_failed"),
              TR("dialog.failed_write_midi") + "\n" + file.getFullPathName(),
              StyledMessageBox::WarningIcon);
        }
      });
#endif
}

void MainComponent::play() {
  if (!project)
    return;

  // In plugin mode, playback is controlled by the host
  // We only update UI state, but don't actually start playback
  if (isPluginMode()) {
    if (onRequestHostPlayState)
      onRequestHostPlayState(true);
    // UI will be kept in sync by host callbacks; still update immediately for
    // responsiveness
    isPlaying = true;
    toolbar.setPlaying(true);
    return;
  }

  // Standalone mode: use AudioEngine for playback
  if (!audioEngine)
    return;

  const auto &loopRange = project->getLoopRange();
  if (loopRange.isValid()) {
    double position = audioEngine->getPosition();
    if (position < loopRange.startSeconds || position >= loopRange.endSeconds) {
      audioEngine->seek(loopRange.startSeconds);
      pianoRoll.setCursorTime(loopRange.startSeconds);
      toolbar.setCurrentTime(loopRange.startSeconds);
    }
  }

  isPlaying = true;
  toolbar.setPlaying(true);
  audioEngine->play();
}

void MainComponent::pause() {
  // In plugin mode, playback is controlled by the host
  if (isPluginMode()) {
    if (onRequestHostPlayState)
      onRequestHostPlayState(false);
    isPlaying = false;
    toolbar.setPlaying(false);
    return;
  }

  // Standalone mode: use AudioEngine for playback
  if (!audioEngine)
    return;
  isPlaying = false;
  toolbar.setPlaying(false);
  audioEngine->pause();
}

void MainComponent::stop() {
  // In plugin mode, playback is controlled by the host
  if (isPluginMode()) {
    if (onRequestHostStop)
      onRequestHostStop();
    isPlaying = false;
    toolbar.setPlaying(false);
    return;
  }

  // Standalone mode: use AudioEngine for playback
  if (!audioEngine)
    return;
  isPlaying = false;
  toolbar.setPlaying(false);
  audioEngine->stop();
  // Keep cursor at current position - user can press Home to go to start
}

void MainComponent::seek(double time) {
  // In plugin mode, seeking is controlled by the host
  // We only update UI cursor position, but don't actually seek in audio
  if (isPluginMode()) {
    // In plugin mode, we can't seek directly
    // The host controls playback position, we only update UI cursor
    pianoRoll.setCursorTime(time);
    toolbar.setCurrentTime(time);
    return;
  }

  // Standalone mode: use AudioEngine for seeking
  if (!audioEngine)
    return;
  audioEngine->seek(time);
  pianoRoll.setCursorTime(time);
  toolbar.setCurrentTime(time);

  // Scroll view to make cursor visible
  float cursorX = static_cast<float>(time * pianoRoll.getPixelsPerSecond());
  float viewWidth = static_cast<float>(pianoRoll.getWidth() -
                                       74); // minus piano keys and scrollbar
  float scrollX = static_cast<float>(pianoRoll.getScrollX());

  // If cursor is outside visible area, scroll to show it
  if (cursorX < scrollX || cursorX > scrollX + viewWidth) {
    // Center cursor in view, or scroll to start if time is 0
    double newScrollX;
    if (time < 0.001) {
      newScrollX = 0.0; // Go to start
    } else {
      newScrollX =
          std::max(0.0, static_cast<double>(cursorX - viewWidth * 0.3));
    }
    pianoRoll.setScrollX(newScrollX);
  }
}

void MainComponent::resynthesizeIncremental() {
  DBG("resynthesizeIncremental() called");

  if (!project) {
    DBG("  Skipped: no project");
    return;
  }

  // If a synthesis is already running, cancel it and coalesce to a single
  // rerun.
  if (incrementalSynth && incrementalSynth->isSynthesizing()) {
    DBG("  Coalescing: synthesis in progress, cancelling and scheduling rerun");
    pendingIncrementalResynth.store(true);
    incrementalSynth->cancel();
    return;
  }

  auto &audioData = project->getAudioData();
  if (audioData.melSpectrogram.empty() || audioData.f0.empty()) {
    DBG("  Skipped: mel or f0 empty");
    return;
  }
  if (!vocoder->isLoaded()) {
    DBG("  Skipped: vocoder not loaded");
    return;
  }

  // Check if there are dirty notes or F0 edits
  if (!project->hasDirtyNotes() && !project->hasF0DirtyRange()) {
    DBG("  Skipped: no dirty notes or F0 edits");
    return;
  }

  auto [dirtyStart, dirtyEnd] = project->getDirtyFrameRange();
  if (dirtyStart < 0 || dirtyEnd < 0) {
    DBG("  Skipped: invalid dirty range: " + juce::String(dirtyStart) + " to " +
        juce::String(dirtyEnd));
    return;
  }

  DBG("  Proceeding with synthesis: dirty frames " + juce::String(dirtyStart) +
      " to " + juce::String(dirtyEnd));

  // Setup incrementalSynth
  incrementalSynth->setProject(project.get());
  incrementalSynth->setVocoder(vocoder.get());

  // We're starting a new synthesis run; clear any previously pending rerun.
  pendingIncrementalResynth.store(false);

  // Show progress during synthesis
  toolbar.showProgress(TR("progress.synthesizing"));
  toolbar.setProgress(-1.0f); // Indeterminate progress

  // Disable toolbar during synthesis
  toolbar.setEnabled(false);

  // Use SafePointer to prevent accessing destroyed component
  juce::Component::SafePointer<MainComponent> safeThis(this);

  // Capture audioEngine pointer for async callback (only if not in plugin mode)
  AudioEngine *audioEnginePtr = nullptr;
  if (!isPluginMode() && audioEngine) {
    audioEnginePtr = audioEngine.get();
  }

  // Run synthesis (expands to silence boundaries, no padding, no crossfade)
  incrementalSynth->synthesizeRegion(
      // Progress callback
      [safeThis](const juce::String &message) {
        if (safeThis == nullptr)
          return;
        safeThis->toolbar.showProgress(message);
      },
      // Complete callback
      [safeThis, audioEnginePtr](bool success) {
        if (safeThis == nullptr)
          return;

        safeThis->toolbar.setEnabled(true);
        safeThis->toolbar.hideProgress();

        if (!success) {
          DBG("resynthesizeIncremental: Synthesis failed or was cancelled");

          if (safeThis->pendingIncrementalResynth.exchange(false)) {
            juce::MessageManager::callAsync([safeThis]() {
              if (safeThis != nullptr)
                safeThis->resynthesizeIncremental();
            });
          }
          return;
        }

        // Reload waveform in audio engine (standalone mode only)
        if (audioEnginePtr && !safeThis->isPluginMode()) {
          if (safeThis->audioEngine &&
              safeThis->audioEngine.get() == audioEnginePtr) {
            auto &audioData = safeThis->project->getAudioData();
            try {
              audioEnginePtr->loadWaveform(audioData.waveform,
                                           audioData.sampleRate, true);
            } catch (...) {
              DBG("resynthesizeIncremental: EXCEPTION in loadWaveform!");
            }
          }
        }

        // Repaint piano roll to show updated waveform
        safeThis->pianoRoll.repaint();

        // Notify plugin mode that project data changed
        if (safeThis->isPluginMode() && safeThis->onProjectDataChanged)
          safeThis->onProjectDataChanged();

        // If edits happened during synthesis, run one more time for the latest
        // state.
        if (safeThis->pendingIncrementalResynth.exchange(false)) {
          juce::MessageManager::callAsync([safeThis]() {
            if (safeThis != nullptr)
              safeThis->resynthesizeIncremental();
          });
        }
      });
}

void MainComponent::onNoteSelected(Note *note) {
  parameterPanel.setSelectedNote(note);
}

void MainComponent::onPitchEdited() {
  pianoRoll.repaint();
  parameterPanel.updateFromNote();
}

void MainComponent::onZoomChanged(float pixelsPerSecond) {
  if (isSyncingZoom)
    return;

  isSyncingZoom = true;

  // Update all components with zoom centered on cursor
  pianoRoll.setPixelsPerSecond(pixelsPerSecond, true);
  toolbar.setZoom(pixelsPerSecond);

  isSyncingZoom = false;
}

void MainComponent::undo() {
  // Cancel any in-progress drawing first
  pianoRoll.cancelDrawing();

  if (undoManager && undoManager->canUndo()) {
    undoManager->undo();
    pianoRoll.invalidateBasePitchCache(); // Refresh cache after note split etc.
    pianoRoll.repaint();

    if (project) {
      // Don't mark all notes as dirty - let undo action callbacks handle
      // the specific dirty range. This avoids synthesizing the entire project.
      // The undo action's callback will set the correct F0 dirty range.
      resynthesizeIncremental();
    }
  }
}

void MainComponent::redo() {
  if (undoManager && undoManager->canRedo()) {
    undoManager->redo();
    pianoRoll.invalidateBasePitchCache(); // Refresh cache after note split etc.
    pianoRoll.repaint();

    if (project) {
      // Don't mark all notes as dirty - let redo action callbacks handle
      // the specific dirty range. This avoids synthesizing the entire project.
      // The redo action's callback will set the correct F0 dirty range.
      resynthesizeIncremental();
    }
  }
}

void MainComponent::setEditMode(EditMode mode) {
  pianoRoll.setEditMode(mode);
  toolbar.setEditMode(mode);
}

void MainComponent::segmentIntoNotes() {
  if (!project)
    return;

  // Run segmentation in background thread to avoid blocking UI
  // SOME model inference happens in background
  juce::Component::SafePointer<MainComponent> safeThis(this);

  if (loaderThread.joinable())
    loaderThread.join();

  loaderThread = std::thread([safeThis]() {
    if (safeThis == nullptr || !safeThis->project)
      return;

    // Create a copy for thread safety
    auto projectCopy = std::make_shared<Project>(*safeThis->project);

    // Perform segmentation in background thread (SOME model inference here)
    safeThis->segmentIntoNotes(*projectCopy);

    // Update main project on UI thread
    juce::MessageManager::callAsync([safeThis, projectCopy]() {
      if (safeThis == nullptr)
        return;

      // Copy notes back
      safeThis->project->getNotes() = projectCopy->getNotes();

      // Update UI
      safeThis->pianoRoll.invalidateBasePitchCache();
      safeThis->pianoRoll.repaint();
    });
  });
}

void MainComponent::segmentIntoNotes(Project &targetProject) {
  // NOTE: This function performs SOME model inference.
  // It should ONLY be called from background threads to avoid blocking UI.

  auto &audioData = targetProject.getAudioData();
  auto &notes = targetProject.getNotes();
  notes.clear();

  juce::Component::SafePointer<MainComponent> safeThis(this);

  if (audioData.f0.empty())
    return;

  const auto someModelPath =
      PlatformPaths::getModelsDirectory().getChildFile("some.onnx");
  if (!someDetector || !someDetector->isLoaded()) {
    juce::MessageManager::callAsync([someModelPath]() {
      juce::AlertWindow::showMessageBoxAsync(
          juce::AlertWindow::WarningIcon, "Missing model file",
          "some.onnx was not found at:\n" + someModelPath.getFullPathName() +
              "\n\nPlease install the required model files and try again.");
    });
    return;
  }

  // Try to use SOME model for segmentation if available
  // SOME model inference runs in background thread
  if (someDetector && someDetector->isLoaded() &&
      audioData.waveform.getNumSamples() > 0) {

    const float *samples = audioData.waveform.getReadPointer(0);
    int numSamples = audioData.waveform.getNumSamples();

    // audioData.f0 uses vocoder frame rate: 44100Hz / 512 hop = 86.13 fps
    // SOME uses 44100Hz / 512 hop = 86.13 fps (same!)
    // So SOME frames map directly to F0 frames
    const int f0Size = static_cast<int>(audioData.f0.size());

    // Use streaming detection to show notes as they're detected
    someDetector->detectNotesStreaming(
        samples, numSamples, SOMEDetector::SAMPLE_RATE,
        [&](const std::vector<SOMEDetector::NoteEvent> &chunkNotes) {
          for (const auto &someNote : chunkNotes) {
            if (someNote.isRest)
              continue;

            // SOME frames are already in the same frame rate as F0 (hop 512 at
            // 44100Hz)
            int f0Start = someNote.startFrame;
            int f0End = someNote.endFrame;

            f0Start = std::max(0, std::min(f0Start, f0Size - 1));
            f0End = std::max(f0Start + 1, std::min(f0End, f0Size));

            if (f0End - f0Start < 3)
              continue;

            // Use SOME's predicted MIDI value directly for note position
            // Delta pitch (from RMVPE/FCPE F0) will capture the pitch curve
            // details
            Note note(f0Start, f0End, someNote.midiNote);
            std::vector<float> f0Values(audioData.f0.begin() + f0Start,
                                        audioData.f0.begin() + f0End);
            note.setF0Values(std::move(f0Values));
            notes.push_back(note);
          }

          // Update UI on main thread
          juce::MessageManager::callAsync([safeThis]() {
            if (safeThis == nullptr)
              return;
            safeThis->pianoRoll
                .invalidateBasePitchCache(); // Regenerate smoothed base pitch
            safeThis->pianoRoll.repaint();
          });
        },
        nullptr // progress callback
    );

    // Wait a bit for streaming to complete (notes are added asynchronously)
    juce::Thread::sleep(100);

    DBG("SOME segmented into " << notes.size() << " notes");

    if (!audioData.f0.empty())
      PitchCurveProcessor::rebuildCurvesFromSource(targetProject, audioData.f0);

    return;
  }

  // Fallback: segment based on F0 pitch changes

  // Helper to finalize a note
  auto finalizeNote = [&](int start, int end) {
    if (end - start < 5)
      return; // Minimum 5 frames

    // Calculate average MIDI for this segment (only voiced frames)
    // Important: average the MIDI values, not the frequencies
    float midiSum = 0.0f;
    int midiCount = 0;
    for (int j = start; j < end; ++j) {
      if (j < static_cast<int>(audioData.voicedMask.size()) &&
          audioData.voicedMask[j] && audioData.f0[j] > 0) {
        midiSum += freqToMidi(audioData.f0[j]);
        midiCount++;
      }
    }
    if (midiCount == 0)
      return; // No voiced frames at all

    float midi = midiSum / midiCount;

    Note note(start, end, midi);
    std::vector<float> f0Values(audioData.f0.begin() + start,
                                audioData.f0.begin() + end);
    note.setF0Values(std::move(f0Values));
    notes.push_back(note);
  };

  // Segment F0 into notes, splitting on pitch changes > 0.5 semitones
  constexpr float pitchSplitThreshold = 0.5f; // semitones
  constexpr int minFramesForSplit =
      3; // require consecutive frames to confirm pitch change
  constexpr int maxUnvoicedGap =
      INT_MAX; // never break on unvoiced, only on pitch change

  bool inNote = false;
  int noteStart = 0;
  int currentMidiNote = 0; // quantized to nearest semitone
  int pitchChangeCount = 0;
  int pitchChangeStart = 0;
  int unvoicedCount = 0;

  for (size_t i = 0; i < audioData.f0.size(); ++i) {
    bool voiced = i < audioData.voicedMask.size() && audioData.voicedMask[i];

    if (voiced && !inNote) {
      // Start new note
      inNote = true;
      noteStart = static_cast<int>(i);
      currentMidiNote =
          static_cast<int>(std::round(freqToMidi(audioData.f0[i])));
      pitchChangeCount = 0;
      unvoicedCount = 0;
    } else if (voiced && inNote) {
      unvoicedCount = 0; // Reset unvoiced counter

      // Check for pitch change
      float currentMidi = freqToMidi(audioData.f0[i]);
      int quantizedMidi = static_cast<int>(std::round(currentMidi));

      if (quantizedMidi != currentMidiNote &&
          std::abs(currentMidi - currentMidiNote) > pitchSplitThreshold) {
        if (pitchChangeCount == 0)
          pitchChangeStart = static_cast<int>(i);
        pitchChangeCount++;

        // Confirm pitch change after consecutive frames
        if (pitchChangeCount >= minFramesForSplit) {
          // Finalize current note up to pitch change point
          finalizeNote(noteStart, pitchChangeStart);

          // Start new note from pitch change point
          noteStart = pitchChangeStart;
          currentMidiNote = quantizedMidi;
          pitchChangeCount = 0;
        }
      } else {
        pitchChangeCount = 0; // Reset if pitch returns
      }
    } else if (!voiced && inNote) {
      // Allow short unvoiced gaps within notes
      unvoicedCount++;
      if (unvoicedCount > maxUnvoicedGap) {
        // End note after long unvoiced gap
        finalizeNote(noteStart, static_cast<int>(i) - unvoicedCount);
        inNote = false;
        pitchChangeCount = 0;
        unvoicedCount = 0;
      }
    }
  }

  // Handle note at end
  if (inNote) {
    finalizeNote(noteStart, static_cast<int>(audioData.f0.size()));
  }

  // Update dense pitch curves after segmentation
  if (!audioData.f0.empty())
    PitchCurveProcessor::rebuildCurvesFromSource(targetProject, audioData.f0);
}

void MainComponent::showSettings() {
  if (!settingsOverlay) {
    // Pass AudioDeviceManager only in standalone mode
    juce::AudioDeviceManager *deviceMgr = nullptr;
    if (!isPluginMode() && audioEngine)
      deviceMgr = &audioEngine->getDeviceManager();

    settingsOverlay =
        std::make_unique<SettingsOverlay>(settingsManager.get(), deviceMgr);
    addAndMakeVisible(settingsOverlay.get());
    settingsOverlay->setVisible(false);
    settingsOverlay->onClose = [this]() {
      if (settingsOverlay)
        settingsOverlay->setVisible(false);
    };

    settingsOverlay->getSettingsComponent()->onSettingsChanged = [this]() {
      settingsManager->applySettings();
      reloadInferenceModels(true);
    };
    settingsOverlay->getSettingsComponent()->canChangeDevice = [this]() {
      return !isInferenceBusy();
    };
    settingsOverlay->getSettingsComponent()->onPitchDetectorChanged =
        [this](PitchDetectorType type) {
          audioAnalyzer->setPitchDetectorType(type);
        };
  }

  settingsOverlay->setBounds(getLocalBounds());
  settingsOverlay->setVisible(true);
  settingsOverlay->toFront(true);
  settingsOverlay->grabKeyboardFocus();
}

bool MainComponent::isInterestedInFileDrag(const juce::StringArray &files) {
  for (const auto &file : files) {
    if (file.endsWithIgnoreCase(".wav") || file.endsWithIgnoreCase(".mp3") ||
        file.endsWithIgnoreCase(".flac") || file.endsWithIgnoreCase(".aiff") ||
        file.endsWithIgnoreCase(".ogg") || file.endsWithIgnoreCase(".m4a"))
      return true;
  }
  return false;
}

void MainComponent::filesDropped(const juce::StringArray &files, int /*x*/,
                                 int /*y*/) {
  if (files.isEmpty())
    return;

  juce::File audioFile(files[0]);
  if (audioFile.existsAsFile())
    loadAudioFile(audioFile);
}

void MainComponent::setHostAudio(const juce::AudioBuffer<float> &buffer,
                                 double sampleRate) {
  if (!isPluginMode())
    return;

  DBG("MainComponent::setHostAudio called - starting async analysis");

  // Use SafePointer to prevent accessing destroyed component
  juce::Component::SafePointer<MainComponent> safeThis(this);

  // Create project if needed (on message thread)
  if (!project)
    project = std::make_unique<Project>();

  juce::AudioBuffer<float> resampledBuffer;
  const double inputSampleRate = sampleRate;
  if (inputSampleRate > 0.0 &&
      std::abs(inputSampleRate - static_cast<double>(SAMPLE_RATE)) > 1e-6) {
    const int inSamples = buffer.getNumSamples();
    const int outSamples = static_cast<int>(
        std::llround(static_cast<double>(inSamples) *
                     (static_cast<double>(SAMPLE_RATE) / inputSampleRate)));
    const int channels = buffer.getNumChannels();
    resampledBuffer.setSize(channels, std::max(0, outSamples), false, false,
                            true);
    resampledBuffer.clear();

    const double ratio = inputSampleRate / static_cast<double>(SAMPLE_RATE);
    for (int ch = 0; ch < channels; ++ch) {
      juce::LagrangeInterpolator interp;
      interp.reset();
      interp.process(ratio, buffer.getReadPointer(ch),
                     resampledBuffer.getWritePointer(ch),
                     resampledBuffer.getNumSamples());
    }
  }

  const juce::AudioBuffer<float> &stored =
      resampledBuffer.getNumSamples() > 0 ? resampledBuffer : buffer;
  const double storedSampleRate = resampledBuffer.getNumSamples() > 0
                                      ? static_cast<double>(SAMPLE_RATE)
                                      : inputSampleRate;

  // Store sample rate and waveform (on message thread)
  project->getAudioData().sampleRate = static_cast<int>(storedSampleRate);
  project->getAudioData().waveform = stored;

  // Store original waveform for synthesis
  originalWaveform = stored;
  hasOriginalWaveform = true;

  // Show analyzing progress
  toolbar.showProgress(TR("progress.analyzing"));

  // Run analysis in background thread to avoid blocking UI
  // Use the same analysis logic as loadAudioFile for code sharing
  cancelLoading.store(true);
  if (loaderThread.joinable()) {
    if (loaderJoinerThread.joinable())
      loaderJoinerThread.join();
    auto old = std::move(loaderThread);
    loaderJoinerThread = std::thread([t = std::move(old)]() mutable {
      if (t.joinable())
        t.join();
    });
  }
  cancelLoading.store(false);

  const auto jobId = hostAnalysisJobId.fetch_add(1) + 1;

  loaderThread = std::thread([safeThis, jobId]() {
    if (safeThis == nullptr)
      return;

    if (safeThis->cancelLoading.load() ||
        safeThis->hostAnalysisJobId.load() != jobId)
      return;

    // Create a copy of project data for analysis
    // This ensures thread safety
    auto projectCopy = std::make_shared<Project>();
    {
      // Copy project data (safe to read from background thread)
      projectCopy->getAudioData().waveform =
          safeThis->project->getAudioData().waveform;
      projectCopy->getAudioData().sampleRate =
          safeThis->project->getAudioData().sampleRate;
    }

    DBG("MainComponent::setHostAudio - analysis thread started");

    // Use shared analyzeAudio function (same as loadAudioFile)
    // This ensures code reuse and consistency
    auto updateProgress = [safeThis, jobId](double p, const juce::String &msg) {
      if (safeThis == nullptr)
        return;
      if (safeThis->cancelLoading.load() ||
          safeThis->hostAnalysisJobId.load() != jobId)
        return;

      // Progress updates can be shown in toolbar if needed
      juce::MessageManager::callAsync([safeThis, msg, p, jobId]() {
        if (safeThis != nullptr) {
          if (safeThis->cancelLoading.load() ||
              safeThis->hostAnalysisJobId.load() != jobId)
            return;
          // Could update progress bar here if needed
          DBG("MainComponent::setHostAudio - " << msg << " (" << (p * 100)
                                               << "%)");
        }
      });
    };

    // Perform analysis using shared function
    if (safeThis->cancelLoading.load() ||
        safeThis->hostAnalysisJobId.load() != jobId)
      return;

    safeThis->analyzeAudio(*projectCopy, updateProgress);

    if (safeThis->cancelLoading.load() ||
        safeThis->hostAnalysisJobId.load() != jobId)
      return;

    DBG("MainComponent::setHostAudio - analysis complete, updating UI");

    // Analysis complete - update main project on message thread
    // Use same UI update logic as loadAudioFile for consistency
    juce::MessageManager::callAsync([safeThis, projectCopy, jobId]() mutable {
      if (safeThis == nullptr)
        return;

      if (safeThis->hostAnalysisJobId.load() != jobId)
        return;

      // Clear undo history before replacing project to avoid dangling pointers
      if (safeThis->undoManager)
        safeThis->undoManager->clear();

      safeThis->project = std::make_unique<Project>(std::move(*projectCopy));

      // Update UI components (shared logic)
      safeThis->pianoRoll.setProject(safeThis->project.get());
      safeThis->parameterPanel.setProject(safeThis->project.get());
      safeThis->toolbar.setTotalTime(
          safeThis->project->getAudioData().getDuration());

      // Center view on detected pitch range (shared logic)
      const auto &f0 = safeThis->project->getAudioData().f0;
      if (!f0.empty()) {
        float minF0 = 10000.0f, maxF0 = 0.0f;
        for (float freq : f0) {
          if (freq > 50.0f) {
            minF0 = std::min(minF0, freq);
            maxF0 = std::max(maxF0, freq);
          }
        }
        if (maxF0 > minF0) {
          float minMidi = freqToMidi(minF0) - 2.0f;
          float maxMidi = freqToMidi(maxF0) + 2.0f;
          safeThis->pianoRoll.centerOnPitchRange(minMidi, maxMidi);
        }
      }

      safeThis->repaint();

      // Load vocoder if not already loaded (required for real-time processing)
      // This is done in analyzeAudio, but we ensure it's loaded here too
      if (!safeThis->vocoder->isLoaded()) {
        DBG("MainComponent::setHostAudio - loading vocoder model");
        auto modelPath = PlatformPaths::getModelsDirectory().getChildFile(
            "pc_nsf_hifigan.onnx");
        if (modelPath.existsAsFile()) {
          if (safeThis->vocoder->loadModel(modelPath)) {
            DBG("MainComponent::setHostAudio - vocoder model loaded "
                "successfully: "
                << modelPath.getFullPathName());
          } else {
            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::WarningIcon, "Inference failed",
                "Failed to load vocoder model at:\n" +
                    modelPath.getFullPathName() +
                    "\n\nPlease check your model installation and try again.");
            safeThis->toolbar.hideProgress();
            return;
          }
        } else {
          juce::AlertWindow::showMessageBoxAsync(
              juce::AlertWindow::WarningIcon, "Missing model file",
              "pc_nsf_hifigan.onnx was not found at:\n" +
                  modelPath.getFullPathName() +
                  "\n\nPlease install the required model files and try again.");
          safeThis->toolbar.hideProgress();
          return;
        }
      } else {
        DBG("MainComponent::setHostAudio - vocoder already loaded");
      }

      // Trigger real-time processor update (this will also set vocoder if
      // needed)
      if (safeThis->onProjectDataChanged)
        safeThis->onProjectDataChanged();

      // Hide progress bar
      safeThis->toolbar.hideProgress();

      DBG("MainComponent::setHostAudio - UI update complete");
    });
  });
}

void MainComponent::updatePlaybackPosition(double timeSeconds) {
  if (!isPluginMode())
    return;

  // Only follow host position if we have a valid project with audio
  if (!project || project->getAudioData().waveform.getNumSamples() == 0)
    return;

  // Clamp position to audio duration (don't go beyond the end)
  double duration = project->getAudioData().getDuration();
  double clampedTime = std::min(timeSeconds, static_cast<double>(duration));

  // Update cursor position using the same mechanism as AudioEngine
  pendingCursorTime.store(clampedTime);
  hasPendingCursorUpdate.store(true);

  // In plugin mode, the host controls playback state
  // Set isPlaying to true when we receive position updates
  // This enables "follow playback" feature
  isPlaying = true;
}

void MainComponent::notifyHostStopped() {
  if (!isPluginMode())
    return;

  // In plugin mode, the host controls playback state
  isPlaying = false;
}

bool MainComponent::isARAModeActive() const {
  // Check if we're in plugin mode and have project data from ARA
  // ARA mode is indicated by having project data but no manual capture
  // In ARA mode, audio comes from ARA document controller, not from
  // processBlock
  if (!isPluginMode())
    return false;

  // If we have project data and it wasn't captured manually, it's likely from
  // ARA This is a heuristic - in a real implementation, we'd track this
  // explicitly
  if (project && project->getAudioData().waveform.getNumSamples() > 0) {
    // Check if we're not currently capturing (which would indicate non-ARA
    // mode) Note: This requires access to PluginProcessor, which we don't have
    // here A better approach would be to set a flag when ARA audio is received
    return true; // Assume ARA if we have project data in plugin mode
  }

  return false;
}

void MainComponent::renderProcessedAudio() {
  if (!isPluginMode() || !hasOriginalWaveform)
    return;

  // Show progress
  toolbar.showProgress(TR("progress.rendering"));

  // Cancel any previous render and wait for it
  cancelRender = true;
  if (renderThread.joinable())
    renderThread.join();
  isRendering = false;
  cancelRender = false;

  // Snapshot required data to avoid accessing Project concurrently from the
  // background thread
  if (!project || !vocoder) {
    toolbar.hideProgress();
    return;
  }

  auto f0Snapshot = project->getAudioData().f0;
  auto voicedMaskSnapshot = project->getAudioData().voicedMask;
  auto melSpecSnapshot = project->getAudioData().melSpectrogram;
  const float globalOffset = project->getGlobalPitchOffset();
  const int numChannels = originalWaveform.getNumChannels();
  Vocoder *voc = vocoder.get();
  auto onChanged = onProjectDataChanged;

  juce::Component::SafePointer<MainComponent> safeThis(this);

  renderThread =
      std::thread([this, safeThis, f0Snapshot = std::move(f0Snapshot),
                   voicedMaskSnapshot = std::move(voicedMaskSnapshot),
                   melSpecSnapshot = std::move(melSpecSnapshot), globalOffset,
                   numChannels, voc, onChanged]() mutable {
        if (safeThis == nullptr)
          return;

        safeThis->isRendering = true;

        auto finishRendering = [safeThis]() {
          if (safeThis != nullptr)
            safeThis->isRendering = false;
        };

        if (cancelRender.load())
          return finishRendering();

        if (f0Snapshot.empty() || melSpecSnapshot.empty()) {
          juce::MessageManager::callAsync([safeThis]() {
            if (safeThis != nullptr)
              safeThis->toolbar.hideProgress();
          });
          return finishRendering();
        }

        if (voicedMaskSnapshot.size() < f0Snapshot.size())
          voicedMaskSnapshot.resize(f0Snapshot.size(), true);

        // Apply global pitch offset
        for (size_t i = 0; i < f0Snapshot.size(); ++i) {
          if (cancelRender.load())
            return finishRendering();
          if (voicedMaskSnapshot[i] && f0Snapshot[i] > 0)
            f0Snapshot[i] *= std::pow(2.0f, globalOffset / 12.0f);
        }

        if (cancelRender.load())
          return finishRendering();

        // Synthesize
        auto synthesized = voc ? voc->infer(melSpecSnapshot, f0Snapshot)
                               : std::vector<float>{};

        if (cancelRender.load())
          return finishRendering();

        // (Output buffer currently not propagated; we keep behavior: just
        // notify + hide progress)
        juce::MessageManager::callAsync(
            [safeThis, ok = !synthesized.empty(), onChanged]() {
              if (safeThis == nullptr)
                return;
              safeThis->toolbar.hideProgress();
              if (ok && onChanged)
                onChanged();
              safeThis->isRendering = false;
            });
        finishRendering();
      });
}

// Setup Kiwi constraint layout
void MainComponent::setupConstraints() {
  // Create variables for container (this component)
  containerVars = layout.createComponentVars("container");

  // Create variables for child components
  menuBarVars = layout.createComponentVars("menuBar");
  toolbarVars = layout.createComponentVars("toolbar");
  workspaceVars = layout.createComponentVars("workspace");
  settingsOverlayVars = layout.createComponentVars("settingsOverlay");

  // Container constraints (will be updated in resized())
  containerWidthConstraint = containerVars.width == 800.0;
  containerHeightConstraint = containerVars.height == 600.0;
  layout.addConstraint(containerVars.left == 0.0);
  layout.addConstraint(containerVars.top == 0.0);
  layout.addConstraint(containerWidthConstraint);
  layout.addConstraint(containerHeightConstraint);

#if !JUCE_MAC
  // Menu bar constraints (non-macOS only)
  layout.addConstraint(menuBarVars.left == containerVars.left);
  layout.addConstraint(menuBarVars.top == containerVars.top);
  layout.addConstraint(menuBarVars.width == containerVars.width);
  layout.addConstraint(menuBarVars.height == 24.0);  // Fixed height

  // Toolbar below menu bar
  layout.addConstraint(toolbarVars.left == containerVars.left);
  layout.addConstraint(toolbarVars.top == menuBarVars.bottom);
  layout.addConstraint(toolbarVars.width == containerVars.width);
  layout.addConstraint(toolbarVars.height == 52.0);  // Fixed height
#else
  // Toolbar at top (macOS uses native menu)
  layout.addConstraint(toolbarVars.left == containerVars.left);
  layout.addConstraint(toolbarVars.top == containerVars.top);
  layout.addConstraint(toolbarVars.width == containerVars.width);
  layout.addConstraint(toolbarVars.height == 52.0);  // Fixed height
#endif

  // Workspace fills remaining space
  layout.addConstraint(workspaceVars.left == containerVars.left);
  layout.addConstraint(workspaceVars.top == toolbarVars.bottom);
  layout.addConstraint(workspaceVars.right == containerVars.right);
  layout.addConstraint(workspaceVars.bottom == containerVars.bottom);

  // Settings overlay covers entire component
  layout.addConstraint(settingsOverlayVars.left == containerVars.left);
  layout.addConstraint(settingsOverlayVars.top == containerVars.top);
  layout.addConstraint(settingsOverlayVars.right == containerVars.right);
  layout.addConstraint(settingsOverlayVars.bottom == containerVars.bottom);
}

// Update layout constraints when component is resized
void MainComponent::updateLayoutConstraints() {
  // Remove old size constraints
  layout.removeConstraint(containerWidthConstraint);
  layout.removeConstraint(containerHeightConstraint);

  // Create new size constraints with current dimensions
  containerWidthConstraint = containerVars.width == static_cast<double>(getWidth());
  containerHeightConstraint = containerVars.height == static_cast<double>(getHeight());

  // Add new constraints
  layout.addConstraint(containerWidthConstraint);
  layout.addConstraint(containerHeightConstraint);
}
