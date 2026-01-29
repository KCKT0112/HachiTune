#pragma once

#include "../Audio/Analysis/AudioAnalyzer.h"
#include "../Audio/AudioEngine.h"
#include "../Audio/Engine/PlaybackController.h"
#include "../Audio/FCPEPitchDetector.h"
#include "../Audio/IO/AudioFileManager.h"
#include "../Audio/RMVPEPitchDetector.h"
#include "../Audio/SOMEDetector.h"
#include "../Audio/Synthesis/IncrementalSynthesizer.h"
#include "../Audio/Vocoder.h"
#include "../JuceHeader.h"
#include "../Models/Project.h"
#include "../Utils/KiwiLayoutManager.h"
#include "../Utils/UndoManager.h"
#include "CustomMenuBarLookAndFeel.h"
#include "CustomTitleBar.h"
#include "Main/MenuHandler.h"
#include "Main/SettingsManager.h"
#include "ParameterPanel.h"
#include "PianoRollComponent.h"
#include "SettingsComponent.h"
#include "ToolbarComponent.h"
#include "Workspace/WorkspaceComponent.h"

#include <atomic>
#include <cstdint>
#include <thread>

class MainComponent : public juce::Component,
                      public juce::Timer,
                      public juce::KeyListener,
                      public juce::FileDragAndDropTarget {
public:
  explicit MainComponent(bool enableAudioDevice = true);
  ~MainComponent() override;

  void paint(juce::Graphics &g) override;
  void resized() override;

  void timerCallback() override;

  // KeyListener
  bool keyPressed(const juce::KeyPress &key,
                  juce::Component *originatingComponent) override;

  // Mouse handling for window dragging on macOS
  void mouseDown(const juce::MouseEvent &e) override;
  void mouseDrag(const juce::MouseEvent &e) override;
  void mouseDoubleClick(const juce::MouseEvent &e) override;

  // FileDragAndDropTarget
  bool isInterestedInFileDrag(const juce::StringArray &files) override;
  void filesDropped(const juce::StringArray &files, int x, int y) override;

  // Plugin mode
  bool isPluginMode() const { return !enableAudioDeviceFlag; }
  Project *getProject() { return project.get(); }
  Vocoder *getVocoder() { return vocoder.get(); }
  ToolbarComponent &getToolbar() { return toolbar; }
  juce::Point<int> getSavedWindowSize() const;

  // Check if ARA mode is active (for UI display)
  bool isARAModeActive() const;

  // Plugin mode - host audio handling
  void setHostAudio(const juce::AudioBuffer<float> &buffer, double sampleRate);
  void renderProcessedAudio();

  // Plugin mode callbacks
  std::function<void()> onReanalyzeRequested;
  std::function<void()>
      onProjectDataChanged; // Called when project data is ready or changed
  std::function<void()>
      onPitchEditFinished; // Called when pitch editing is finished
                           // (Melodyne-style: triggers real-time update)

  // Plugin mode - request host transport control (optional; only works if host
  // supports it)
  std::function<void(bool shouldPlay)> onRequestHostPlayState;
  std::function<void()> onRequestHostStop;

  // Plugin mode - update playback position from host
  void updatePlaybackPosition(double timeSeconds);
  void notifyHostStopped(); // Called when host stops playback

private:
  void openFile();
  void exportFile();
  void exportMidiFile();
  void play();
  void pause();
  void stop();
  void seek(double time);
  void resynthesizeIncremental(); // Incremental synthesis on edit
  void showSettings();

  void onNoteSelected(Note *note);
  void onPitchEdited();
  void onZoomChanged(float pixelsPerSecond);
  void reinterpolateUV(int startFrame,
                       int endFrame); // Re-infer UV regions using FCPE

  // Layout setup
  void setupConstraints();
  void updateLayoutConstraints();

  void reloadInferenceModels(bool async = false);
  GPUProvider getProviderFromDevice(const juce::String &device) const;
  bool isInferenceBusy() const;

  void loadAudioFile(const juce::File &file);
  void analyzeAudio();
  void analyzeAudio(
      Project &targetProject,
      const std::function<void(double, const juce::String &)> &onProgress,
      std::function<void()> onComplete = nullptr);
  void segmentIntoNotes();
  void segmentIntoNotes(Project &targetProject);

  void saveProject();

  void undo();
  void redo();
  void setEditMode(EditMode mode);

  std::unique_ptr<Project> project;
  std::unique_ptr<AudioEngine> audioEngine;
  std::unique_ptr<FCPEPitchDetector>
      fcpePitchDetector; // FCPE neural network detector (legacy)
  std::unique_ptr<RMVPEPitchDetector>
      rmvpePitchDetector; // RMVPE neural network detector
  std::unique_ptr<SOMEDetector>
      someDetector; // SOME note segmentation detector (legacy)
  std::unique_ptr<Vocoder> vocoder;
  std::unique_ptr<PitchUndoManager> undoManager;

  // New modular components
  std::unique_ptr<AudioFileManager> fileManager;
  std::unique_ptr<AudioAnalyzer> audioAnalyzer;
  std::unique_ptr<IncrementalSynthesizer> incrementalSynth;
  std::unique_ptr<PlaybackController> playbackController;
  std::unique_ptr<MenuHandler> menuHandler;
  std::unique_ptr<SettingsManager> settingsManager;

  const bool enableAudioDeviceFlag;

  CustomMenuBarLookAndFeel menuBarLookAndFeel;
  juce::MenuBarComponent menuBar;
  ToolbarComponent toolbar;
  WorkspaceComponent workspace;
  PianoRollComponent pianoRoll;
  ParameterPanel parameterPanel;

  std::unique_ptr<SettingsOverlay> settingsOverlay;

  std::unique_ptr<juce::FileChooser> fileChooser;

  juce::File fcpeModelPath;
  juce::File melFilterbankPath;
  juce::File centTablePath;
  juce::File rmvpeModelPath;
  juce::File someModelPath;

  // Original waveform for incremental synthesis
  juce::AudioBuffer<float> originalWaveform;
  bool hasOriginalWaveform = false;

  bool isPlaying = false;

  // Sync flag to prevent infinite loops
  bool isSyncingZoom = false;

  // Async load state
  std::thread loaderThread;
  std::thread loaderJoinerThread;
  std::atomic<bool> isLoadingAudio{false};
  std::atomic<bool> cancelLoading{false};
  std::atomic<std::uint64_t> hostAnalysisJobId{0};
  std::atomic<double> loadingProgress{0.0};
  juce::CriticalSection loadingMessageLock;
  juce::String loadingMessage;
  juce::String lastLoadingMessage;

  // Async render state (plugin mode)
  std::thread renderThread;
  std::atomic<bool> cancelRender{false};
  std::atomic<bool> isRendering{false};
  std::atomic<bool> isReloadingModels{false};
  std::thread modelReloadThread;

  // Incremental synthesis coalescing
  std::atomic<bool> pendingIncrementalResynth{false};

  // Cursor update throttling
  std::atomic<double> pendingCursorTime{0.0};
  std::atomic<bool> hasPendingCursorUpdate{false};
  juce::int64 lastCursorUpdateTime = 0;

#if JUCE_MAC
  juce::ComponentDragger dragger;
#endif

  // Kiwi constraint layout
  KiwiLayoutManager layout;
  KiwiLayoutManager::ComponentVars containerVars;
  KiwiLayoutManager::ComponentVars menuBarVars;
  KiwiLayoutManager::ComponentVars toolbarVars;
  KiwiLayoutManager::ComponentVars workspaceVars;
  KiwiLayoutManager::ComponentVars settingsOverlayVars;

  // Dynamic constraints that change on resize
  kiwi::Constraint containerWidthConstraint;
  kiwi::Constraint containerHeightConstraint;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
