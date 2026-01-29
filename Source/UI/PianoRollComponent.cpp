#include "PianoRollComponent.h"
#include "../Utils/BasePitchCurve.h"
#include "../Utils/CurveResampler.h"
#include "../Utils/Constants.h"
#include "../Utils/PitchCurveProcessor.h"
#include <algorithm>
#include <cmath>
#include <limits>

PianoRollComponent::PianoRollComponent() {
  // Initialize modular components
  coordMapper = std::make_unique<CoordinateMapper>();
  renderer = std::make_unique<PianoRollRenderer>();
  scrollZoomController = std::make_unique<ScrollZoomController>();
  pitchEditor = std::make_unique<PitchEditor>();
  boxSelector = std::make_unique<BoxSelector>();
  noteSplitter = std::make_unique<NoteSplitter>();

  // Wire up components
  renderer->setCoordinateMapper(coordMapper.get());
  scrollZoomController->setCoordinateMapper(coordMapper.get());
  pitchEditor->setCoordinateMapper(coordMapper.get());
  noteSplitter->setCoordinateMapper(coordMapper.get());

  // Setup scrollZoomController callbacks
  scrollZoomController->onRepaintNeeded = [this]() { repaint(); };
  scrollZoomController->onZoomChanged = [this](float pps) {
    if (onZoomChanged)
      onZoomChanged(pps);
  };
  scrollZoomController->onScrollChanged = [this](double x) {
    if (onScrollChanged)
      onScrollChanged(x);
  };

  // Setup pitchEditor callbacks
  pitchEditor->onNoteSelected = [this](Note *note) {
    if (onNoteSelected)
      onNoteSelected(note);
  };
  pitchEditor->onPitchEdited = [this]() {
    repaint();
    if (onPitchEdited)
      onPitchEdited();
  };
  pitchEditor->onPitchEditFinished = [this]() {
    if (onPitchEditFinished)
      onPitchEditFinished();
  };
  pitchEditor->onBasePitchCacheInvalidated = [this]() {
    invalidateBasePitchCache();
  };

  // Setup noteSplitter callbacks
  noteSplitter->onNoteSplit = [this]() {
    invalidateBasePitchCache();
    repaint();
  };

  addAndMakeVisible(horizontalScrollBar);
  addAndMakeVisible(verticalScrollBar);

  // Use scrollZoomController's scrollbars
  addAndMakeVisible(scrollZoomController->getHorizontalScrollBar());
  addAndMakeVisible(scrollZoomController->getVerticalScrollBar());

  horizontalScrollBar.addListener(this);
  verticalScrollBar.addListener(this);

  // Style scrollbars to match theme
  auto thumbColor = juce::Colour(APP_COLOR_PRIMARY).withAlpha(0.6f);
  auto trackColor = juce::Colour(0xFF252530);

  horizontalScrollBar.setColour(juce::ScrollBar::thumbColourId, thumbColor);
  horizontalScrollBar.setColour(juce::ScrollBar::trackColourId, trackColor);
  verticalScrollBar.setColour(juce::ScrollBar::thumbColourId, thumbColor);
  verticalScrollBar.setColour(juce::ScrollBar::trackColourId, trackColor);

  // Set initial scroll range
  verticalScrollBar.setRangeLimits(0, (MAX_MIDI_NOTE - MIN_MIDI_NOTE) *
                                          pixelsPerSemitone);
  verticalScrollBar.setCurrentRange(0, 500);

  // Default view centered on C3-C4 (MIDI 48-60)
  centerOnPitchRange(48.0f, 60.0f);

  // Enable keyboard focus for shortcuts
  setWantsKeyboardFocus(true);
  addKeyListener(this);

  // Setup Kiwi constraint layout
  setupConstraints();
}

PianoRollComponent::~PianoRollComponent() {
  removeKeyListener(this);
  horizontalScrollBar.removeListener(this);
  verticalScrollBar.removeListener(this);
}

void PianoRollComponent::paint(juce::Graphics &g) {
  // Apply rounded corner clipping
  const float cornerRadius = 8.0f;
  juce::Path clipPath;
  clipPath.addRoundedRectangle(getLocalBounds().toFloat(), cornerRadius);
  g.reduceClipRegion(clipPath);

  // Background
  g.fillAll(juce::Colour(APP_COLOR_BACKGROUND));

  constexpr int scrollBarSize = 8;

  // Create clipping region for main area (below timelines)
  auto mainArea = getLocalBounds()
                      .withTrimmedLeft(pianoKeysWidth)
                      .withTrimmedTop(headerHeight)
                      .withTrimmedBottom(scrollBarSize)
                      .withTrimmedRight(scrollBarSize);

  // Draw background waveform (only horizontal scroll, fills visible height)
  {
    juce::Graphics::ScopedSaveState saveState(g);
    g.reduceClipRegion(mainArea);
    drawBackgroundWaveform(g, mainArea);
  }

  // Draw scrolled content (grid, notes, pitch curves)
  {
    juce::Graphics::ScopedSaveState saveState(g);
    g.reduceClipRegion(mainArea);
    g.setOrigin(pianoKeysWidth - static_cast<int>(scrollX),
                headerHeight - static_cast<int>(scrollY));

    drawGrid(g);
    drawLoopOverlay(g);
    drawNotes(g);
    drawStretchGuides(g);
    drawPitchCurves(g);
    drawSelectionRect(g);
  }

  // Draw timeline (above grid, scrolls horizontally)
  drawTimeline(g);
  drawLoopTimeline(g);

  // Draw unified cursor line (spans from timeline through grid)
  {
    float x = static_cast<float>(pianoKeysWidth) + timeToX(cursorTime) -
              static_cast<float>(scrollX);
    float cursorTop = 0.0f;
    float cursorBottom =
        static_cast<float>(getHeight() - 8); // Exclude scrollbar

    // Only draw if cursor is in visible area
    if (x >= pianoKeysWidth && x < getWidth() - 8) {
      g.setColour(juce::Colours::white);
      g.fillRect(x - 0.5f, cursorTop, 1.0f, cursorBottom);

      // Draw triangle playhead indicator at top of timeline
      constexpr float triangleWidth = 10.0f;
      constexpr float triangleHeight = 8.0f;
      juce::Path triangle;
      triangle.addTriangle(x - triangleWidth * 0.5f, 0.0f, // Top-left
                           x + triangleWidth * 0.5f, 0.0f, // Top-right
                           x, triangleHeight // Bottom-center (pointing down)
      );
      g.fillPath(triangle);
    }
  }

  // Draw piano keys
  drawPianoKeys(g);
}

void PianoRollComponent::resized() {
  // Update container size constraints
  updateLayoutConstraints();

  // Apply layout to scrollbars
  layout.applyComponentVars(&horizontalScrollBar, horizontalScrollBarVars);
  layout.applyComponentVars(&verticalScrollBar, verticalScrollBarVars);

  updateScrollBars();
}

void PianoRollComponent::setupConstraints() {
  // Create variables for container (this component)
  containerVars = layout.createComponentVars("container");

  // Create variables for scrollbars
  horizontalScrollBarVars = layout.createComponentVars("horizontalScrollBar");
  verticalScrollBarVars = layout.createComponentVars("verticalScrollBar");

  // Container constraints (will be updated in resized())
  containerWidthConstraint = containerVars.width == 800.0;
  containerHeightConstraint = containerVars.height == 600.0;
  layout.addConstraint(containerVars.left == 0.0);
  layout.addConstraint(containerVars.top == 0.0);
  layout.addConstraint(containerWidthConstraint);
  layout.addConstraint(containerHeightConstraint);

  constexpr double scrollBarSize = 8.0;
  constexpr double pianoWidth = static_cast<double>(pianoKeysWidth);
  constexpr double headerH = static_cast<double>(headerHeight);

  // Horizontal scrollbar (bottom, after piano keys width)
  layout.addConstraint(horizontalScrollBarVars.left ==
                       containerVars.left + pianoWidth);
  layout.addConstraint(horizontalScrollBarVars.bottom == containerVars.bottom);
  layout.addConstraint(horizontalScrollBarVars.right ==
                       containerVars.right - scrollBarSize);
  layout.addConstraint(horizontalScrollBarVars.height == scrollBarSize);

  // Vertical scrollbar (right, after header height)
  layout.addConstraint(verticalScrollBarVars.right == containerVars.right);
  layout.addConstraint(verticalScrollBarVars.top == containerVars.top + headerH);
  layout.addConstraint(verticalScrollBarVars.bottom ==
                       containerVars.bottom - scrollBarSize);
  layout.addConstraint(verticalScrollBarVars.width == scrollBarSize);
}

void PianoRollComponent::updateLayoutConstraints() {
  // Remove old size constraints
  layout.removeConstraint(containerWidthConstraint);
  layout.removeConstraint(containerHeightConstraint);

  // Create new size constraints with current dimensions
  containerWidthConstraint =
      containerVars.width == static_cast<double>(getWidth());
  containerHeightConstraint =
      containerVars.height == static_cast<double>(getHeight());

  // Add new constraints
  layout.addConstraint(containerWidthConstraint);
  layout.addConstraint(containerHeightConstraint);
}

void PianoRollComponent::drawBackgroundWaveform(
    juce::Graphics &g, const juce::Rectangle<int> &visibleArea) {
  if (!project)
    return;

  const auto &audioData = project->getAudioData();
  if (audioData.waveform.getNumSamples() == 0)
    return;

  // Check if we can use cached waveform
  bool cacheValid = waveformCache.isValid() &&
                    std::abs(cachedScrollX - scrollX) < 1.0 &&
                    std::abs(cachedPixelsPerSecond - pixelsPerSecond) < 0.01f &&
                    cachedWidth == visibleArea.getWidth() &&
                    cachedHeight == visibleArea.getHeight();

  if (cacheValid) {
    g.drawImageAt(waveformCache, visibleArea.getX(), visibleArea.getY());
    return;
  }

  // Render waveform to cache
  waveformCache = juce::Image(juce::Image::ARGB, visibleArea.getWidth(),
                              visibleArea.getHeight(), true);
  juce::Graphics cacheGraphics(waveformCache);

  const float *samples = audioData.waveform.getReadPointer(0);
  int numSamples = audioData.waveform.getNumSamples();

  // Draw waveform filling the visible area height
  float visibleHeight = static_cast<float>(visibleArea.getHeight());
  float centerY = visibleHeight * 0.5f;
  float waveformHeight = visibleHeight * 0.8f;

  juce::Path waveformPath;
  int visibleWidth = visibleArea.getWidth();

  waveformPath.startNewSubPath(0.0f, centerY);

  // Draw only the visible portion
  for (int px = 0; px < visibleWidth; ++px) {
    double time = (scrollX + px) / pixelsPerSecond;
    int startSample = static_cast<int>(time * SAMPLE_RATE);
    int endSample =
        static_cast<int>((time + 1.0 / pixelsPerSecond) * SAMPLE_RATE);

    startSample = std::max(0, std::min(startSample, numSamples - 1));
    endSample = std::max(startSample + 1, std::min(endSample, numSamples));

    float maxVal = 0.0f;
    for (int i = startSample; i < endSample; ++i)
      maxVal = std::max(maxVal, std::abs(samples[i]));

    float y = centerY - maxVal * waveformHeight * 0.5f;
    waveformPath.lineTo(static_cast<float>(px), y);
  }

  // Bottom half (reverse)
  for (int px = visibleWidth - 1; px >= 0; --px) {
    double time = (scrollX + px) / pixelsPerSecond;
    int startSample = static_cast<int>(time * SAMPLE_RATE);
    int endSample =
        static_cast<int>((time + 1.0 / pixelsPerSecond) * SAMPLE_RATE);

    startSample = std::max(0, std::min(startSample, numSamples - 1));
    endSample = std::max(startSample + 1, std::min(endSample, numSamples));

    float maxVal = 0.0f;
    for (int i = startSample; i < endSample; ++i)
      maxVal = std::max(maxVal, std::abs(samples[i]));

    float y = centerY + maxVal * waveformHeight * 0.5f;
    waveformPath.lineTo(static_cast<float>(px), y);
  }

  waveformPath.closeSubPath();

  cacheGraphics.setColour(juce::Colour(APP_COLOR_WAVEFORM));
  cacheGraphics.fillPath(waveformPath);

  // Update cache metadata
  cachedScrollX = scrollX;
  cachedPixelsPerSecond = pixelsPerSecond;
  cachedWidth = visibleArea.getWidth();
  cachedHeight = visibleArea.getHeight();

  // Draw cached image
  g.drawImageAt(waveformCache, visibleArea.getX(), visibleArea.getY());
}

void PianoRollComponent::drawGrid(juce::Graphics &g) {
  float duration = project ? project->getAudioData().getDuration() : 60.0f;
  float width =
      std::max(duration * pixelsPerSecond, static_cast<float>(getWidth()));
  float height = (MAX_MIDI_NOTE - MIN_MIDI_NOTE) * pixelsPerSemitone;

  // Fill black key rows with semi-transparent darker background
  g.setColour(juce::Colour(0x18000000)); // Semi-transparent black overlay
  for (int midi = MIN_MIDI_NOTE; midi <= MAX_MIDI_NOTE; ++midi) {
    int noteInOctave = midi % 12;
    bool isBlack =
        (noteInOctave == 1 || noteInOctave == 3 || noteInOctave == 6 ||
         noteInOctave == 8 || noteInOctave == 10);
    if (isBlack) {
      float y = midiToY(static_cast<float>(midi));
      g.fillRect(0.0f, y, width, pixelsPerSemitone);
    }
  }

  // Horizontal lines (pitch)
  g.setColour(juce::Colour(APP_COLOR_GRID));

  for (int midi = MIN_MIDI_NOTE; midi <= MAX_MIDI_NOTE; ++midi) {
    float y = midiToY(static_cast<float>(midi));
    int noteInOctave = midi % 12;

    if (noteInOctave == 0) // C
    {
      g.setColour(juce::Colour(APP_COLOR_GRID_BAR));
      g.drawHorizontalLine(static_cast<int>(y), 0, width);
      g.setColour(juce::Colour(APP_COLOR_GRID));
    } else {
      g.drawHorizontalLine(static_cast<int>(y), 0, width);
    }
  }

  // Vertical lines (time)
  float secondsPerBeat = 60.0f / 120.0f; // Assuming 120 BPM
  float pixelsPerBeat = secondsPerBeat * pixelsPerSecond;

  for (float x = 0; x < width; x += pixelsPerBeat) {
    g.setColour(juce::Colour(APP_COLOR_GRID));
    g.drawVerticalLine(static_cast<int>(x), 0, height);
  }
}

void PianoRollComponent::drawLoopOverlay(juce::Graphics &g) {
  if (!project)
    return;

  double loopStartSeconds = 0.0;
  double loopEndSeconds = 0.0;
  bool loopEnabled = false;
  if (loopDragMode != LoopDragMode::None) {
    loopStartSeconds = loopDragStartSeconds;
    loopEndSeconds = loopDragEndSeconds;
    loopEnabled = true;
  } else {
    const auto &loopRange = project->getLoopRange();
    loopStartSeconds = loopRange.startSeconds;
    loopEndSeconds = loopRange.endSeconds;
    loopEnabled = loopRange.enabled;
  }

  if (loopStartSeconds > loopEndSeconds)
    std::swap(loopStartSeconds, loopEndSeconds);

  if (loopEndSeconds <= loopStartSeconds)
    return;

  const float startX = timeToX(loopStartSeconds);
  const float endX = timeToX(loopEndSeconds);

  const float height =
      (MAX_MIDI_NOTE - MIN_MIDI_NOTE) * pixelsPerSemitone;
  const auto baseColor = juce::Colour(APP_COLOR_PRIMARY);
  const auto fillColor =
      loopEnabled ? baseColor.withAlpha(0.08f) : baseColor.withAlpha(0.04f);

  g.setColour(fillColor);
  g.fillRect(startX, 0.0f, endX - startX, height);
}

void PianoRollComponent::drawTimeline(juce::Graphics &g) {
  constexpr int scrollBarSize = 8;
  auto timelineArea = juce::Rectangle<int>(
      pianoKeysWidth, 0, getWidth() - pianoKeysWidth - scrollBarSize,
      timelineHeight);

  // Background
  g.setColour(juce::Colour(0xFF1E1E28));
  g.fillRect(timelineArea);

  // Bottom border
  g.setColour(juce::Colour(APP_COLOR_GRID_BAR));
  g.drawHorizontalLine(timelineHeight - 1, static_cast<float>(pianoKeysWidth),
                       static_cast<float>(getWidth() - scrollBarSize));

  // Determine tick interval based on zoom level
  float secondsPerTick;
  if (pixelsPerSecond >= 200.0f)
    secondsPerTick = 0.5f;
  else if (pixelsPerSecond >= 100.0f)
    secondsPerTick = 1.0f;
  else if (pixelsPerSecond >= 50.0f)
    secondsPerTick = 2.0f;
  else if (pixelsPerSecond >= 25.0f)
    secondsPerTick = 5.0f;
  else
    secondsPerTick = 10.0f;

  float duration = project ? project->getAudioData().getDuration() : 60.0f;

  // Draw ticks and labels
  g.setFont(11.0f);

  for (float time = 0.0f; time <= duration + secondsPerTick;
       time += secondsPerTick) {
    float x =
        pianoKeysWidth + time * pixelsPerSecond - static_cast<float>(scrollX);

    if (x < pianoKeysWidth - 50 || x > getWidth())
      continue;

    // Tick mark
    bool isMajor = std::fmod(time, secondsPerTick * 2.0f) < 0.001f;
    int tickHeight = isMajor ? 8 : 4;

    g.setColour(juce::Colour(APP_COLOR_GRID_BAR));
    g.drawVerticalLine(static_cast<int>(x),
                       static_cast<float>(timelineHeight - tickHeight),
                       static_cast<float>(timelineHeight - 1));

    // Time label (only on major ticks)
    if (isMajor) {
      int minutes = static_cast<int>(time) / 60;
      int seconds = static_cast<int>(time) % 60;
      int tenths = static_cast<int>((time - std::floor(time)) * 10);

      juce::String label;
      if (minutes > 0)
        label = juce::String::formatted("%d:%02d", minutes, seconds);
      else if (secondsPerTick < 1.0f)
        label = juce::String::formatted("%d.%d", seconds, tenths);
      else
        label = juce::String::formatted("%ds", seconds);

      g.setColour(juce::Colour(0xFFAAAAAA));
      g.drawText(label, static_cast<int>(x) + 3, 2, 50, timelineHeight - 4,
                 juce::Justification::centredLeft, false);
    }
  }
}

void PianoRollComponent::drawLoopTimeline(juce::Graphics &g) {
  constexpr int scrollBarSize = 8;
  auto loopArea = juce::Rectangle<int>(
      pianoKeysWidth, timelineHeight,
      getWidth() - pianoKeysWidth - scrollBarSize, loopTimelineHeight);

  g.setColour(juce::Colour(0xFF171722));
  g.fillRect(loopArea);

  g.setColour(juce::Colour(APP_COLOR_GRID_BAR));
  g.drawHorizontalLine(headerHeight - 1,
                       static_cast<float>(pianoKeysWidth),
                       static_cast<float>(getWidth() - scrollBarSize));

  if (!project)
    return;

  double loopStartSeconds = 0.0;
  double loopEndSeconds = 0.0;
  bool loopEnabled = false;
  if (loopDragMode != LoopDragMode::None) {
    loopStartSeconds = loopDragStartSeconds;
    loopEndSeconds = loopDragEndSeconds;
    loopEnabled = true;
  } else {
    const auto &loopRange = project->getLoopRange();
    loopStartSeconds = loopRange.startSeconds;
    loopEndSeconds = loopRange.endSeconds;
    loopEnabled = loopRange.enabled;
  }

  if (loopStartSeconds > loopEndSeconds)
    std::swap(loopStartSeconds, loopEndSeconds);

  if (loopEndSeconds <= loopStartSeconds)
    return;

  const float startX =
      static_cast<float>(pianoKeysWidth) + timeToX(loopStartSeconds) -
      static_cast<float>(scrollX);
  const float endX =
      static_cast<float>(pianoKeysWidth) + timeToX(loopEndSeconds) -
      static_cast<float>(scrollX);

  auto range = juce::Rectangle<float>(
      startX, static_cast<float>(timelineHeight), endX - startX,
      static_cast<float>(loopTimelineHeight));

  const auto baseColor = juce::Colour(APP_COLOR_PRIMARY);
  const auto fillColor =
      loopEnabled ? baseColor.withAlpha(0.25f) : baseColor.withAlpha(0.12f);
  const auto edgeColor =
      loopEnabled ? baseColor : juce::Colour(0xFF666677);

  g.setColour(fillColor);
  g.fillRect(range);

  g.setColour(edgeColor);
  g.drawLine(startX, static_cast<float>(timelineHeight), startX,
             static_cast<float>(headerHeight - 1), 1.5f);
  g.drawLine(endX, static_cast<float>(timelineHeight), endX,
             static_cast<float>(headerHeight - 1), 1.5f);

  constexpr float flagWidth = 6.0f;
  constexpr float flagHeight = 6.0f;
  constexpr float flagTop = 0.0f;

  const float flagY = static_cast<float>(timelineHeight) + flagTop;

  juce::Path startFlag;
  startFlag.addTriangle(startX, flagY, startX, flagY + flagHeight,
                        startX - flagWidth, flagY + flagHeight);
  g.fillPath(startFlag);

  juce::Path endFlag;
  endFlag.addTriangle(endX, flagY, endX, flagY + flagHeight,
                      endX + flagWidth, flagY + flagHeight);
  g.fillPath(endFlag);
}

void PianoRollComponent::drawNotes(juce::Graphics &g) {
  if (!project)
    return;

  const auto &audioData = project->getAudioData();
  const float *globalSamples = audioData.waveform.getNumSamples() > 0
                                   ? audioData.waveform.getReadPointer(0)
                                   : nullptr;
  int globalTotalSamples = audioData.waveform.getNumSamples();

  // Calculate visible time range for culling
  double visibleStartTime = scrollX / pixelsPerSecond;
  double visibleEndTime = (scrollX + getWidth()) / pixelsPerSecond;

  for (auto &note : project->getNotes()) {
    // Skip rest notes (they have no pitch)
    if (note.isRest())
      continue;

    // Viewport culling: skip notes outside visible area
    double noteStartTime = framesToSeconds(note.getStartFrame());
    double noteEndTime = framesToSeconds(note.getEndFrame());
    if (noteEndTime < visibleStartTime || noteStartTime > visibleEndTime)
      continue;

    float x = static_cast<float>(noteStartTime * pixelsPerSecond);
    float w = framesToSeconds(note.getDurationFrames()) * pixelsPerSecond;
    float h = pixelsPerSemitone;

    // Position at grid cell center for MIDI note, then offset by pitch
    // adjustment
    float baseGridCenterY =
        midiToY(note.getMidiNote()) + pixelsPerSemitone * 0.5f;
    float pitchOffsetPixels = -note.getPitchOffset() * pixelsPerSemitone;
    float y = baseGridCenterY + pitchOffsetPixels - h * 0.5f;

    // Note color based on pitch
    juce::Colour noteColor = note.isSelected()
                                 ? juce::Colour(APP_COLOR_NOTE_SELECTED)
                                 : juce::Colour(APP_COLOR_NOTE_NORMAL);

    const float *samples = globalSamples;
    int totalSamples = globalTotalSamples;
    int startSample = 0;
    int endSample = 0;
    const auto &clipWaveform = note.getClipWaveform();
    if (!clipWaveform.empty()) {
      samples = clipWaveform.data();
      totalSamples = static_cast<int>(clipWaveform.size());
      startSample = 0;
      endSample = totalSamples;
    } else if (samples && totalSamples > 0) {
      startSample = static_cast<int>(framesToSeconds(note.getStartFrame()) *
                                     audioData.sampleRate);
      endSample = static_cast<int>(framesToSeconds(note.getEndFrame()) *
                                   audioData.sampleRate);
      startSample = std::max(0, std::min(startSample, totalSamples - 1));
      endSample = std::max(startSample + 1,
                           std::min(endSample, totalSamples));
    }

    if (samples && totalSamples > 0 && w > 2.0f &&
        endSample > startSample) {
      // Draw waveform slice inside note
      int numNoteSamples = endSample - startSample;
      int samplesPerPixel = std::max(1, static_cast<int>(numNoteSamples / w));

      float centerY = y + h * 0.5f;
      float waveHeight = h * 3.0f;

      // Build waveform data with increased resolution for smoother curves
      std::vector<float> waveValues;
      // Increase point density for smoother curves (up to 800 points)
      float step = std::max(0.5f, w / 1024.0f);

      for (float px = 0; px <= w; px += step) {
        int sampleIdx =
            startSample + static_cast<int>((px / w) * numNoteSamples);
        int sampleEnd = std::min(sampleIdx + samplesPerPixel, endSample);

        float maxVal = 0.0f;
        for (int i = sampleIdx; i < sampleEnd; ++i)
          maxVal = std::max(maxVal, std::abs(samples[i]));

        waveValues.push_back(maxVal);
      }

      // Apply smoothing filter to reduce aliasing artifacts
      if (waveValues.size() > 2) {
        std::vector<float> smoothed(waveValues.size());
        smoothed[0] = waveValues[0];
        for (size_t i = 1; i + 1 < waveValues.size(); ++i) {
          // Simple 3-point moving average for gentle smoothing
          smoothed[i] = (waveValues[i - 1] * 0.25f + waveValues[i] * 0.5f +
                         waveValues[i + 1] * 0.25f);
        }
        smoothed[waveValues.size() - 1] = waveValues[waveValues.size() - 1];
        waveValues = std::move(smoothed);
      }

      size_t numPoints = waveValues.size();
      if (numPoints < 2) {
        // Fallback for very short notes
        g.setColour(noteColor.withAlpha(0.85f));
        g.fillRoundedRectangle(x, y, std::max(w, 4.0f), h, 2.0f);
      } else {
        // Helper function for Catmull-Rom spline interpolation
        auto catmullRom = [](float t, float p0, float p1, float p2,
                             float p3) -> float {
          // Catmull-Rom spline: smooth interpolation between p1 and p2
          float t2 = t * t;
          float t3 = t2 * t;
          return 0.5f * ((2.0f * p1) + (-p0 + p2) * t +
                         (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
                         (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
        };

        // Draw filled waveform using smooth curves
        g.setColour(noteColor.withAlpha(0.85f));
        juce::Path waveformPath;

        // Build top curve with Catmull-Rom spline
        waveformPath.startNewSubPath(x, centerY -
                                            waveValues[0] * waveHeight * 0.5f);

        // Use cubic curves for smooth interpolation
        const int curveSegments = 4; // Interpolate 4 points between each pair
        for (size_t i = 0; i + 1 < numPoints; ++i) {
          float px1 =
              (static_cast<float>(i) / static_cast<float>(numPoints - 1)) * w;
          float px2 =
              (static_cast<float>(i + 1) / static_cast<float>(numPoints - 1)) *
              w;

          // Get control points for spline
          size_t idx0 = (i > 0) ? i - 1 : i;
          size_t idx1 = i;
          size_t idx2 = i + 1;
          size_t idx3 = (i + 2 < numPoints) ? i + 2 : i + 1;

          float val0 = waveValues[idx0];
          float val1 = waveValues[idx1];
          float val2 = waveValues[idx2];
          float val3 = waveValues[idx3];

          // Draw smooth curve segment
          for (int seg = 1; seg <= curveSegments; ++seg) {
            float t =
                static_cast<float>(seg) / static_cast<float>(curveSegments);
            float px = px1 + (px2 - px1) * t;
            float val = catmullRom(t, val0, val1, val2, val3);
            float yPos = centerY - val * waveHeight * 0.5f;
            waveformPath.lineTo(x + px, yPos);
          }
        }

        // Build bottom curve (mirror of top)
        waveformPath.lineTo(x + w, centerY + waveValues[numPoints - 1] *
                                                 waveHeight * 0.5f);

        for (int i = static_cast<int>(numPoints) - 2; i >= 0; --i) {
          float px1 =
              (static_cast<float>(i + 1) / static_cast<float>(numPoints - 1)) *
              w;
          float px2 =
              (static_cast<float>(i) / static_cast<float>(numPoints - 1)) * w;

          size_t idx0 = (i + 2 < numPoints) ? i + 2 : i + 1;
          size_t idx1 = i + 1;
          size_t idx2 = i;
          size_t idx3 = (i > 0) ? i - 1 : i;

          float val0 = waveValues[idx0];
          float val1 = waveValues[idx1];
          float val2 = waveValues[idx2];
          float val3 = waveValues[idx3];

          for (int seg = 1; seg <= curveSegments; ++seg) {
            float t =
                static_cast<float>(seg) / static_cast<float>(curveSegments);
            float px = px1 + (px2 - px1) * t;
            float val = catmullRom(t, val0, val1, val2, val3);
            float yPos = centerY + val * waveHeight * 0.5f;
            waveformPath.lineTo(x + px, yPos);
          }
        }

        waveformPath.closeSubPath();
        g.fillPath(waveformPath);

        // Draw smooth outline with anti-aliasing
        juce::Path outline;
        outline.startNewSubPath(x, centerY - waveValues[0] * waveHeight * 0.5f);

        // Top curve
        for (size_t i = 0; i + 1 < numPoints; ++i) {
          float px1 =
              (static_cast<float>(i) / static_cast<float>(numPoints - 1)) * w;
          float px2 =
              (static_cast<float>(i + 1) / static_cast<float>(numPoints - 1)) *
              w;

          size_t idx0 = (i > 0) ? i - 1 : i;
          size_t idx1 = i;
          size_t idx2 = i + 1;
          size_t idx3 = (i + 2 < numPoints) ? i + 2 : i + 1;

          float val0 = waveValues[idx0];
          float val1 = waveValues[idx1];
          float val2 = waveValues[idx2];
          float val3 = waveValues[idx3];

          for (int seg = 1; seg <= curveSegments; ++seg) {
            float t =
                static_cast<float>(seg) / static_cast<float>(curveSegments);
            float px = px1 + (px2 - px1) * t;
            float val = catmullRom(t, val0, val1, val2, val3);
            float yPos = centerY - val * waveHeight * 0.5f;
            outline.lineTo(x + px, yPos);
          }
        }

        // Bottom curve
        for (int i = static_cast<int>(numPoints) - 2; i >= 0; --i) {
          float px1 =
              (static_cast<float>(i + 1) / static_cast<float>(numPoints - 1)) *
              w;
          float px2 =
              (static_cast<float>(i) / static_cast<float>(numPoints - 1)) * w;

          size_t idx0 = (i + 2 < numPoints) ? i + 2 : i + 1;
          size_t idx1 = i + 1;
          size_t idx2 = i;
          size_t idx3 = (i > 0) ? i - 1 : i;

          float val0 = waveValues[idx0];
          float val1 = waveValues[idx1];
          float val2 = waveValues[idx2];
          float val3 = waveValues[idx3];

          for (int seg = 1; seg <= curveSegments; ++seg) {
            float t =
                static_cast<float>(seg) / static_cast<float>(curveSegments);
            float px = px1 + (px2 - px1) * t;
            float val = catmullRom(t, val0, val1, val2, val3);
            float yPos = centerY + val * waveHeight * 0.5f;
            outline.lineTo(x + px, yPos);
          }
        }

        outline.closeSubPath();
        g.setColour(noteColor.brighter(0.2f));
        // Use slightly thicker stroke with anti-aliasing for smoother
        // appearance
        g.strokePath(outline,
                     juce::PathStrokeType(1.2f, juce::PathStrokeType::curved,
                                          juce::PathStrokeType::rounded));
      }
    } else {
      // Fallback: simple rectangle for very short notes
      g.setColour(noteColor.withAlpha(0.85f));
      g.fillRoundedRectangle(x, y, std::max(w, 4.0f), h, 2.0f);
    }
  }

  // Draw split guide line when in split mode and hovering over a note
  if (editMode == EditMode::Split && splitGuideNote && splitGuideX >= 0) {
    float noteStartTime = framesToSeconds(splitGuideNote->getStartFrame());
    float noteEndTime = framesToSeconds(splitGuideNote->getEndFrame());
    float noteStartX = static_cast<float>(noteStartTime * pixelsPerSecond);
    float noteEndX = static_cast<float>(noteEndTime * pixelsPerSecond);

    // Only draw if guide is within note bounds (with margin)
    if (splitGuideX > noteStartX + 5 && splitGuideX < noteEndX - 5) {
      float noteY = midiToY(splitGuideNote->getAdjustedMidiNote());
      float noteH = pixelsPerSemitone;

      // Draw dashed vertical line
      g.setColour(juce::Colour(0xFFFF6B6B)); // Red-ish color for visibility
      float dashLength = 4.0f;
      for (float dy = 0; dy < noteH; dy += dashLength * 2) {
        float segmentLength = std::min(dashLength, noteH - dy);
        g.drawLine(splitGuideX, noteY + dy, splitGuideX,
                   noteY + dy + segmentLength, 2.0f);
      }
    }
  }
}

void PianoRollComponent::drawStretchGuides(juce::Graphics &g) {
  if (!project || editMode != EditMode::Stretch)
    return;

  auto boundaries = collectStretchBoundaries();
  if (boundaries.empty())
    return;

  const float height =
      (MAX_MIDI_NOTE - MIN_MIDI_NOTE) * pixelsPerSemitone;

  for (size_t i = 0; i < boundaries.size(); ++i) {
    int frame = boundaries[i].frame;
    const bool isActive =
        stretchDrag.active && boundaries[i].left == stretchDrag.boundary.left &&
        boundaries[i].right == stretchDrag.boundary.right;
    if (isActive)
      frame = stretchDrag.currentBoundary;

    float x = framesToSeconds(frame) * pixelsPerSecond;

    const bool isHovered = static_cast<int>(i) == hoveredStretchBoundaryIndex;
    float alpha = isHovered || isActive ? 0.8f : 0.35f;
    float thickness = isHovered || isActive ? 2.0f : 1.0f;

    g.setColour(juce::Colour(APP_COLOR_PRIMARY).withAlpha(alpha));
    g.drawLine(x, 0.0f, x, height, thickness);
  }
}

void PianoRollComponent::drawPitchCurves(juce::Graphics &g) {
  if (!project)
    return;

  const auto &audioData = project->getAudioData();
  if (audioData.f0.empty())
    return;

  // Get global pitch offset (applied to display only)
  float globalOffset = project->getGlobalPitchOffset();

  // Draw pitch curves per note with their pitch offsets applied (delta pitch)
  if (showDeltaPitch) {
    g.setColour(juce::Colour(APP_COLOR_PITCH_CURVE));

    const bool useLiveBasePreview =
        (isDragging || pitchEditor->isDraggingMultiNotes());
    const auto &draggedNotes = pitchEditor->getDraggedNotes();

    for (const auto &note : project->getNotes()) {
      if (note.isRest())
        continue;

      const bool isDraggedNote =
          (isDragging && draggedNote == &note) ||
          (pitchEditor->isDraggingMultiNotes() &&
           std::find(draggedNotes.begin(), draggedNotes.end(), &note) !=
               draggedNotes.end());
      const bool applyNoteOffset = !(useLiveBasePreview && isDraggedNote);

      juce::Path path;
      bool pathStarted = false;

      int startFrame = note.getStartFrame();
      int endFrame =
          std::min(note.getEndFrame(), static_cast<int>(audioData.f0.size()));

      for (int i = startFrame; i < endFrame; ++i) {
        // Base pitch: during drag, add pitchOffset to simulate the new base
        // pitch This gives real-time preview of how the curve will look after
        // drag completes
        float baseMidi =
            (i < static_cast<int>(audioData.basePitch.size()))
                ? audioData.basePitch[static_cast<size_t>(i)]
                : ((i < static_cast<int>(audioData.f0.size()) &&
                    audioData.f0[static_cast<size_t>(i)] > 0.0f)
                       ? freqToMidi(audioData.f0[static_cast<size_t>(i)])
                       : 0.0f);
        if (applyNoteOffset)
          baseMidi += note.getPitchOffset();
        float deltaMidi = (i < static_cast<int>(audioData.deltaPitch.size()))
                              ? audioData.deltaPitch[static_cast<size_t>(i)]
                              : 0.0f;
        // Final = base (with drag offset) + delta + global offset only
        float finalMidi = baseMidi + deltaMidi + globalOffset;

        if (finalMidi > 0.0f) {
          float x = framesToSeconds(i) * pixelsPerSecond;
          float y = midiToY(finalMidi) + pixelsPerSemitone * 0.5f;

          if (!pathStarted) {
            path.startNewSubPath(x, y);
            pathStarted = true;
          } else {
            path.lineTo(x, y);
          }
        }
      }

      if (pathStarted) {
        g.strokePath(path, juce::PathStrokeType(2.0f));
      }
    }
  }

  // Draw base pitch curve as dashed line
  // Use cached base pitch to avoid expensive recalculation on every repaint
  if (showBasePitch) {
    const bool useLiveBasePreview =
        (isDragging || pitchEditor->isDraggingMultiNotes());
    if (!useLiveBasePreview) {
      updateBasePitchCacheIfNeeded();
    }

    const auto &basePitchCurve =
        useLiveBasePreview ? audioData.basePitch : cachedBasePitch;
    if (!basePitchCurve.empty()) {
      // Calculate visible frame range
      double visibleStartTime = scrollX / pixelsPerSecond;
      double visibleEndTime = (scrollX + getWidth()) / pixelsPerSecond;
      int visStartFrame =
          std::max(0, static_cast<int>(visibleStartTime * audioData.sampleRate /
                                       HOP_SIZE));
      int visEndFrame = std::min(
          static_cast<int>(basePitchCurve.size()),
          static_cast<int>(visibleEndTime * audioData.sampleRate / HOP_SIZE) +
              1);

      // Draw base pitch curve with dashed line
      g.setColour(
          juce::Colour(0xFF00FF00).withAlpha(0.6f)); // Green with transparency
      juce::Path basePath;
      bool basePathStarted = false;

      for (int i = visStartFrame; i < visEndFrame; ++i) {
        if (i >= 0 && i < static_cast<int>(basePitchCurve.size())) {
          float baseMidi = basePitchCurve[static_cast<size_t>(i)];
          if (baseMidi > 0.0f) {
            float x = framesToSeconds(i) * pixelsPerSecond;
            float y = midiToY(baseMidi) +
                      pixelsPerSemitone * 0.5f; // Center in grid cell

            if (!basePathStarted) {
              basePath.startNewSubPath(x, y);
              basePathStarted = true;
            } else {
              basePath.lineTo(x, y);
            }
          } else if (basePathStarted) {
            // Break path at unvoiced regions - draw current segment before
            // breaking
            juce::Path dashedPath;
            juce::PathStrokeType stroke(1.5f);
            const float dashLengths[] = {4.0f, 4.0f}; // 4px dash, 4px gap
            stroke.createDashedStroke(dashedPath, basePath, dashLengths, 2);
            g.strokePath(dashedPath, juce::PathStrokeType(1.5f));
            basePath.clear();
            basePathStarted = false;
          }
        }
      }

      if (basePathStarted) {
        // Use dashed stroke for base pitch curve
        juce::Path dashedPath;
        juce::PathStrokeType stroke(1.5f);
        const float dashLengths[] = {4.0f, 4.0f}; // 4px dash, 4px gap
        stroke.createDashedStroke(dashedPath, basePath, dashLengths, 2);
        g.strokePath(dashedPath, juce::PathStrokeType(1.5f));
      }
    }
  }
}

void PianoRollComponent::drawCursor(juce::Graphics &g) {
  float x = timeToX(cursorTime);
  float height = (MAX_MIDI_NOTE - MIN_MIDI_NOTE) * pixelsPerSemitone;

  g.setColour(juce::Colours::white);
  g.fillRect(x - 0.5f, 0.0f, 1.0f, height);
}

void PianoRollComponent::drawPianoKeys(juce::Graphics &g) {
  constexpr int scrollBarSize = 8;
  auto keyArea = getLocalBounds()
                     .withWidth(pianoKeysWidth)
                     .withTrimmedTop(headerHeight)
                     .withTrimmedBottom(scrollBarSize);

  // Background
  g.setColour(juce::Colour(0xFF1A1A24));
  g.fillRect(keyArea);

  static const char *noteNames[] = {"C",  "C#", "D",  "D#", "E",  "F",
                                    "F#", "G",  "G#", "A",  "A#", "B"};

  // Draw each key
  // Use truncated scrollY to match grid origin (which uses
  // static_cast<int>(scrollY))
  int scrollYInt = static_cast<int>(scrollY);
  for (int midi = MIN_MIDI_NOTE; midi <= MAX_MIDI_NOTE; ++midi) {
    float y = midiToY(static_cast<float>(midi)) -
              static_cast<float>(scrollYInt) + headerHeight;
    int noteInOctave = midi % 12;

    // Check if it's a black key
    bool isBlack =
        (noteInOctave == 1 || noteInOctave == 3 || noteInOctave == 6 ||
         noteInOctave == 8 || noteInOctave == 10);

    if (isBlack)
      g.setColour(juce::Colour(0xFF2D2D37));
    else
      g.setColour(juce::Colour(0xFF3D3D47));

    g.fillRect(0.0f, y, static_cast<float>(pianoKeysWidth - 2),
               pixelsPerSemitone - 1);

    // Draw note name for all notes
    int octave = midi / 12 - 1;
    juce::String noteName =
        juce::String(noteNames[noteInOctave]) + juce::String(octave);

    // Use dimmer color for black keys
    g.setColour(isBlack ? juce::Colour(0xFFAAAAAA) : juce::Colours::white);
    g.setFont(12.0f);
    g.drawText(noteName, pianoKeysWidth - 36, static_cast<int>(y), 32,
               static_cast<int>(pixelsPerSemitone),
               juce::Justification::centred);
  }
}

float PianoRollComponent::midiToY(float midiNote) const {
  return (MAX_MIDI_NOTE - midiNote) * pixelsPerSemitone;
}

float PianoRollComponent::yToMidi(float y) const {
  return MAX_MIDI_NOTE - y / pixelsPerSemitone;
}

float PianoRollComponent::timeToX(double time) const {
  return static_cast<float>(time * pixelsPerSecond);
}

double PianoRollComponent::xToTime(float x) const {
  return x / pixelsPerSecond;
}

void PianoRollComponent::mouseDown(const juce::MouseEvent &e) {
  if (!project)
    return;

  // Grab keyboard focus so shortcuts work after mouse operations
  grabKeyboardFocus();

  float adjustedX = e.x - pianoKeysWidth + static_cast<float>(scrollX);
  float adjustedY = e.y - headerHeight + static_cast<float>(scrollY);

  // Handle timeline clicks - seek to position
  if (e.y < timelineHeight && e.x >= pianoKeysWidth) {
    double time = std::max(0.0, xToTime(adjustedX));

    // Use setCursorTime to properly handle dirty rect for old position
    setCursorTime(time);

    if (onSeek)
      onSeek(time);

    return;
  }

  // Handle loop timeline drag
  if (e.y >= timelineHeight && e.y < headerHeight && e.x >= pianoKeysWidth) {
    const auto &loopRange = project->getLoopRange();
    if (loopRange.endSeconds > loopRange.startSeconds) {
      const float startX =
          static_cast<float>(pianoKeysWidth) + timeToX(loopRange.startSeconds) -
          static_cast<float>(scrollX);
      const float endX =
          static_cast<float>(pianoKeysWidth) + timeToX(loopRange.endSeconds) -
          static_cast<float>(scrollX);

      if (std::abs(static_cast<float>(e.x) - startX) <= loopHandleHitPadding) {
        loopDragMode = LoopDragMode::ResizeStart;
        loopDragStartSeconds = loopRange.startSeconds;
        loopDragEndSeconds = loopRange.endSeconds;
        repaint();
        return;
      }
      if (std::abs(static_cast<float>(e.x) - endX) <= loopHandleHitPadding) {
        loopDragMode = LoopDragMode::ResizeEnd;
        loopDragStartSeconds = loopRange.startSeconds;
        loopDragEndSeconds = loopRange.endSeconds;
        repaint();
        return;
      }
      if (static_cast<float>(e.x) >= startX &&
          static_cast<float>(e.x) <= endX) {
        loopDragMode = LoopDragMode::Move;
        loopDragAnchorSeconds = xToTime(adjustedX);
        loopDragOriginalStart = loopRange.startSeconds;
        loopDragOriginalEnd = loopRange.endSeconds;
        loopDragStartSeconds = loopRange.startSeconds;
        loopDragEndSeconds = loopRange.endSeconds;
        repaint();
        return;
      }
    }

    loopDragMode = LoopDragMode::Create;
    loopDragStartX = static_cast<float>(e.x);
    loopDragStartSeconds = std::max(0.0, xToTime(adjustedX));
    loopDragEndSeconds = loopDragStartSeconds;
    repaint();
    return;
  }

  // Ignore clicks outside main area
  if (e.y < headerHeight || e.x < pianoKeysWidth)
    return;

  if (editMode == EditMode::Stretch) {
    int boundaryIndex =
        findStretchBoundaryIndex(adjustedX, stretchHandleHitPadding);
    if (boundaryIndex >= 0) {
      auto boundaries = collectStretchBoundaries();
      if (boundaryIndex < static_cast<int>(boundaries.size())) {
        startStretchDrag(boundaries[static_cast<size_t>(boundaryIndex)]);
        repaint();
        return;
      }
    }

    // In stretch mode, allow selecting notes but disable pitch dragging.
    Note *note = findNoteAt(adjustedX, adjustedY);
    if (note) {
      project->deselectAllNotes();
      note->setSelected(true);
      if (onNoteSelected)
        onNoteSelected(note);
      repaint();
      return;
    }

    // Box selection fallback
    project->deselectAllNotes();
    boxSelector->startSelection(adjustedX, adjustedY);
    repaint();
    return;
  }

  if (editMode == EditMode::Draw) {
    // Start drawing
    isDrawing = true;
    drawingEdits.clear();
    drawingEditIndexByFrame.clear();
    drawCurves.clear();
    activeDrawCurve = nullptr;
    lastDrawFrame = -1;
    lastDrawValueCents = 0;

    applyPitchDrawing(adjustedX, adjustedY);

    if (onPitchEdited)
      onPitchEdited();

    repaint();
    return;
  }

  if (editMode == EditMode::Split) {
    // Split mode - find and split note at click position
    Note *note = noteSplitter->findNoteAt(adjustedX, adjustedY);
    if (note) {
      noteSplitter->splitNoteAtX(note, adjustedX);
    }
    return;
  }

  // Check if clicking on a note
  Note *note = findNoteAt(adjustedX, adjustedY);

  if (note) {
    // Check if clicking on an already selected note (for multi-note drag)
    auto selectedNotes = project->getSelectedNotes();
    bool clickedOnSelected = note->isSelected() && selectedNotes.size() > 1;

    if (clickedOnSelected) {
      // Start multi-note drag
      pitchEditor->startMultiNoteDrag(selectedNotes, adjustedY);
    } else {
      // Single note selection and drag
      project->deselectAllNotes();
      note->setSelected(true);

      if (onNoteSelected)
        onNoteSelected(note);

      // Capture delta slice from global dense deltaPitch for this note
      auto &audioData = project->getAudioData();
      int startFrame = note->getStartFrame();
      int endFrame = note->getEndFrame();
      int numFrames = endFrame - startFrame;

      std::vector<float> delta(numFrames, 0.0f);
      for (int i = 0; i < numFrames; ++i) {
        int globalFrame = startFrame + i;
        if (globalFrame >= 0 &&
            globalFrame < static_cast<int>(audioData.deltaPitch.size()))
          delta[i] = audioData.deltaPitch[static_cast<size_t>(globalFrame)];
      }
      note->setDeltaPitch(std::move(delta));

      // Start single note dragging
      isDragging = true;
      draggedNote = note;
      dragStartY = adjustedY;
      originalPitchOffset = note->getPitchOffset();
      originalMidiNote = note->getMidiNote();

      // Save boundary F0 values and original F0 for undo
      int f0Size = static_cast<int>(audioData.f0.size());

      boundaryF0Start = (startFrame > 0 && startFrame - 1 < f0Size)
                            ? audioData.f0[startFrame - 1]
                            : 0.0f;
      boundaryF0End = (endFrame < f0Size) ? audioData.f0[endFrame] : 0.0f;

      // Save original F0 values for undo
      originalF0Values.clear();
      for (int i = startFrame; i < endFrame && i < f0Size; ++i)
        originalF0Values.push_back(audioData.f0[i]);

      prepareDragBasePreview();
    }

    repaint();
  } else {
    // Clicked on empty area - start box selection
    project->deselectAllNotes();
    boxSelector->startSelection(adjustedX, adjustedY);
    repaint();
  }
}

void PianoRollComponent::mouseDrag(const juce::MouseEvent &e) {
  // Throttle repaints during drag to ~60fps max
  juce::int64 now = juce::Time::getMillisecondCounter();
  bool shouldRepaint = (now - lastDragRepaintTime) >= minDragRepaintInterval;

  float adjustedX = e.x - pianoKeysWidth + static_cast<float>(scrollX);
  float adjustedY = e.y - headerHeight + static_cast<float>(scrollY);

  if (loopDragMode != LoopDragMode::None) {
    switch (loopDragMode) {
    case LoopDragMode::ResizeStart:
      loopDragStartSeconds = std::max(0.0, xToTime(adjustedX));
      break;
    case LoopDragMode::ResizeEnd:
      loopDragEndSeconds = std::max(0.0, xToTime(adjustedX));
      break;
    case LoopDragMode::Create:
      loopDragEndSeconds = std::max(0.0, xToTime(adjustedX));
      break;
    case LoopDragMode::Move: {
      double delta = xToTime(adjustedX) - loopDragAnchorSeconds;
      double newStart = loopDragOriginalStart + delta;
      double newEnd = loopDragOriginalEnd + delta;

      if (project) {
        const double duration = project->getAudioData().getDuration();
        if (duration > 0.0) {
          if (newStart < 0.0) {
            newEnd -= newStart;
            newStart = 0.0;
          }
          if (newEnd > duration) {
            double overflow = newEnd - duration;
            newStart -= overflow;
            newEnd = duration;
            if (newStart < 0.0)
              newStart = 0.0;
          }
        }
      }

      loopDragStartSeconds = newStart;
      loopDragEndSeconds = newEnd;
      break;
    }
    case LoopDragMode::None:
      break;
    }
    if (shouldRepaint) {
      repaint();
      lastDragRepaintTime = now;
    }
    return;
  }

  if (editMode == EditMode::Stretch && stretchDrag.active) {
    const double time = xToTime(adjustedX);
    const int targetFrame =
        static_cast<int>(secondsToFrames(static_cast<float>(time)));
    updateStretchDrag(targetFrame);

    if (shouldRepaint) {
      repaint();
      lastDragRepaintTime = now;
    }
    return;
  }

  if (editMode == EditMode::Draw && isDrawing) {
    applyPitchDrawing(adjustedX, adjustedY);

    if (onPitchEdited)
      onPitchEdited();

    if (shouldRepaint) {
      repaint();
      lastDragRepaintTime = now;
    }
    return;
  }

  // Handle box selection
  if (boxSelector->isSelecting()) {
    boxSelector->updateSelection(adjustedX, adjustedY);
    if (shouldRepaint) {
      repaint();
      lastDragRepaintTime = now;
    }
    return;
  }

  // Handle multi-note drag
  if (pitchEditor->isDraggingMultiNotes()) {
    pitchEditor->updateMultiNoteDrag(adjustedY);
    if (shouldRepaint) {
      repaint();
      lastDragRepaintTime = now;
    }
    return;
  }

  // Handle single note drag
  if (isDragging && draggedNote) {
    float deltaY = dragStartY - adjustedY;
    float deltaSemitones = deltaY / pixelsPerSemitone;

    draggedNote->setPitchOffset(deltaSemitones);
    draggedNote->markDirty();
    applyDragBasePreview(deltaSemitones);

    if (shouldRepaint) {
      repaint();
      lastDragRepaintTime = now;
    }
  }
}

void PianoRollComponent::mouseUp(const juce::MouseEvent &e) {
  juce::ignoreUnused(e);

  // Ensure keyboard focus is maintained after mouse operations
  grabKeyboardFocus();

  if (loopDragMode != LoopDragMode::None) {
    constexpr float minDragDistance = 4.0f;
    const bool isCreate = loopDragMode == LoopDragMode::Create;
    loopDragMode = LoopDragMode::None;

    if (!project) {
      repaint();
      return;
    }

    if (!isCreate ||
        std::abs(static_cast<float>(e.x) - loopDragStartX) >= minDragDistance) {
      project->setLoopRange(loopDragStartSeconds, loopDragEndSeconds);
      if (onLoopRangeChanged)
        onLoopRangeChanged(project->getLoopRange());
    }
    repaint();
    return;
  }

  if (editMode == EditMode::Draw && isDrawing) {
    isDrawing = false;
    commitPitchDrawing();
    repaint();
    return;
  }

  if (editMode == EditMode::Stretch && stretchDrag.active) {
    finishStretchDrag();
    repaint();
    return;
  }

  // Handle box selection end
  if (boxSelector->isSelecting()) {
    auto notesInRect = boxSelector->getNotesInRect(project, coordMapper.get());
    for (auto *note : notesInRect) {
      note->setSelected(true);
    }
    boxSelector->endSelection();
    repaint();
    return;
  }

  // Handle multi-note drag end
  if (pitchEditor->isDraggingMultiNotes()) {
    pitchEditor->endMultiNoteDrag();
    repaint();
    return;
  }

  // Handle single note drag end
  if (isDragging && draggedNote) {
    float newOffset = draggedNote->getPitchOffset();

    // Check if there was any meaningful change (threshold: 0.001 semitones)
    constexpr float CHANGE_THRESHOLD = 0.001f;
    bool hasChange = std::abs(newOffset) >= CHANGE_THRESHOLD;

    if (hasChange && project) {
      int startFrame = draggedNote->getStartFrame();
      int endFrame = draggedNote->getEndFrame();
      auto &audioData = project->getAudioData();
      int f0Size = static_cast<int>(audioData.f0.size());

      // Update note's midiNote with final offset (bake pitchOffset into
      // midiNote)
      draggedNote->setMidiNote(originalMidiNote + newOffset);
      draggedNote->setPitchOffset(
          0.0f); // Reset offset since it's baked into midiNote

      // Find adjacent notes to expand dirty range (basePitch smoothing affects
      // neighbors)
      const auto &notes = project->getNotes();
      int expandedStart = startFrame;
      int expandedEnd = endFrame;
      for (const auto &note : notes) {
        if (&note == draggedNote)
          continue;
        // If note is adjacent (within smoothing window ~20 frames), include it
        if (note.getEndFrame() > startFrame - 30 &&
            note.getEndFrame() <= startFrame) {
          expandedStart = std::min(expandedStart, note.getStartFrame());
        }
        if (note.getStartFrame() < endFrame + 30 &&
            note.getStartFrame() >= endFrame) {
          expandedEnd = std::max(expandedEnd, note.getEndFrame());
        }
      }

      // Rebuild base pitch curve and F0 with final note position
      PitchCurveProcessor::rebuildBaseFromNotes(*project);
      PitchCurveProcessor::composeF0InPlace(*project, /*applyUvMask=*/false);

      // Invalidate base pitch cache so it gets regenerated on next paint
      invalidateBasePitchCache();

      // Mark dirty range for synthesis (use expanded range)
      int smoothStart = std::max(0, expandedStart - 60);
      int smoothEnd = std::min(f0Size, expandedEnd + 60);
      project->setF0DirtyRange(smoothStart, smoothEnd);

      // Create undo action
      if (undoManager) {
        std::vector<F0FrameEdit> f0Edits;
        for (int i = startFrame; i < endFrame && i < f0Size; ++i) {
          int localIdx = i - startFrame;
          F0FrameEdit edit;
          edit.idx = i;
          edit.oldF0 = (localIdx < static_cast<int>(originalF0Values.size()))
                           ? originalF0Values[localIdx]
                           : 0.0f;
          edit.newF0 = audioData.f0[static_cast<size_t>(i)];
          f0Edits.push_back(edit);
        }
        // Capture frame range for undo callback
        int capturedExpandedStart = expandedStart;
        int capturedExpandedEnd = expandedEnd;
        int capturedF0Size = f0Size;
        auto action = std::make_unique<NotePitchDragAction>(
            draggedNote, &audioData.f0, originalMidiNote,
            originalMidiNote + newOffset, std::move(f0Edits),
            [this, capturedExpandedStart, capturedExpandedEnd,
             capturedF0Size](Note *n) {
              if (project) {
                PitchCurveProcessor::rebuildBaseFromNotes(*project);
                PitchCurveProcessor::composeF0InPlace(*project,
                                                      /*applyUvMask=*/false);
                // Invalidate base pitch cache
                invalidateBasePitchCache();
                // Set dirty range for synthesis (use expanded range)
                int smoothStart = std::max(0, capturedExpandedStart - 60);
                int smoothEnd =
                    std::min(capturedF0Size, capturedExpandedEnd + 60);
                project->setF0DirtyRange(smoothStart, smoothEnd);
                // Clear note's dirty flag since we're using F0 dirty range
                // instead This prevents getDirtyFrameRange() from expanding the
                // range unnecessarily
                if (n) {
                  n->clearDirty();
                }
              }
            });
        undoManager->addAction(std::move(action));
      }

      if (onPitchEdited)
        onPitchEdited();
      repaint();
      if (onPitchEditFinished)
        onPitchEditFinished();
    } else {
      // No meaningful change: just reset pitchOffset and repaint
      restoreDragBasePreview();
      draggedNote->setPitchOffset(0.0f);
      repaint();
    }
  }

  isDragging = false;
  draggedNote = nullptr;
  dragPreviewStartFrame = -1;
  dragPreviewEndFrame = -1;
  dragPreviewWeights.clear();
  dragBasePitchSnapshot.clear();
  dragF0Snapshot.clear();
}

void PianoRollComponent::mouseMove(const juce::MouseEvent &e) {
  if (project && e.y >= timelineHeight && e.y < headerHeight &&
      e.x >= pianoKeysWidth) {
    const auto &loopRange = project->getLoopRange();
    if (loopRange.endSeconds > loopRange.startSeconds) {
      const float startX =
          static_cast<float>(pianoKeysWidth) + timeToX(loopRange.startSeconds) -
          static_cast<float>(scrollX);
      const float endX =
          static_cast<float>(pianoKeysWidth) + timeToX(loopRange.endSeconds) -
          static_cast<float>(scrollX);

      if (std::abs(static_cast<float>(e.x) - startX) <= loopHandleHitPadding ||
          std::abs(static_cast<float>(e.x) - endX) <= loopHandleHitPadding) {
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
      } else if (static_cast<float>(e.x) > startX &&
                 static_cast<float>(e.x) < endX) {
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
      } else {
        setMouseCursor(juce::MouseCursor::NormalCursor);
      }
    } else {
      setMouseCursor(juce::MouseCursor::NormalCursor);
    }
  } else {
    setMouseCursor(juce::MouseCursor::NormalCursor);
  }

  if (editMode == EditMode::Stretch) {
    if (e.y >= headerHeight && e.x >= pianoKeysWidth) {
      float adjustedX = e.x - pianoKeysWidth + static_cast<float>(scrollX);
      int boundaryIndex =
          findStretchBoundaryIndex(adjustedX, stretchHandleHitPadding);
      hoveredStretchBoundaryIndex = boundaryIndex;
      if (boundaryIndex >= 0) {
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
      } else {
        setMouseCursor(juce::MouseCursor::NormalCursor);
      }
    } else {
      hoveredStretchBoundaryIndex = -1;
    }
    repaint();
  }

  // Split mode guide line
  if (editMode == EditMode::Split && project) {
    float adjustedX = e.x - pianoKeysWidth + static_cast<float>(scrollX);
    float adjustedY = e.y - headerHeight + static_cast<float>(scrollY);

    Note *note = noteSplitter->findNoteAt(adjustedX, adjustedY);
    if (note) {
      splitGuideX = adjustedX;
      splitGuideNote = note;
    } else {
      splitGuideX = -1.0f;
      splitGuideNote = nullptr;
    }
    repaint();
  } else if (splitGuideX >= 0) {
    // Clear guide when leaving split mode
    splitGuideX = -1.0f;
    splitGuideNote = nullptr;
    repaint();
  }
}

void PianoRollComponent::mouseDoubleClick(const juce::MouseEvent &e) {
  if (!project)
    return;

  // Ignore double-clicks outside main area
  if (e.y < headerHeight || e.x < pianoKeysWidth)
    return;

  float adjustedX = e.x - pianoKeysWidth + static_cast<float>(scrollX);
  float adjustedY = e.y - headerHeight + static_cast<float>(scrollY);

  // Check if double-clicking on a note
  Note *note = findNoteAt(adjustedX, adjustedY);

  if (note) {
    auto rebuildAndNotify = [this]() {
      PitchCurveProcessor::rebuildBaseFromNotes(*project);
      PitchCurveProcessor::composeF0InPlace(*project, false);
      if (onPitchEdited)
        onPitchEdited();
      if (onPitchEditFinished)
        onPitchEditFinished();
      repaint();
    };

    if (note->isSelected()) {
      auto selectedNotes = project->getSelectedNotes();
      if (selectedNotes.size() > 1) {
        std::vector<Note *> notesToSnap;
        std::vector<float> oldMidis;
        std::vector<float> oldOffsets;
        std::vector<float> newMidis;

        notesToSnap.reserve(selectedNotes.size());
        oldMidis.reserve(selectedNotes.size());
        oldOffsets.reserve(selectedNotes.size());
        newMidis.reserve(selectedNotes.size());

        for (auto *selected : selectedNotes) {
          if (!selected || selected->isRest())
            continue;

          float oldMidi = selected->getMidiNote();
          float oldOffset = selected->getPitchOffset();
          float adjustedMidi = oldMidi + oldOffset;
          float snappedMidi = std::round(adjustedMidi);

          if (std::abs(snappedMidi - adjustedMidi) <= 0.001f)
            continue;

          notesToSnap.push_back(selected);
          oldMidis.push_back(oldMidi);
          oldOffsets.push_back(oldOffset);
          newMidis.push_back(snappedMidi);
        }

        if (!notesToSnap.empty()) {
          if (undoManager) {
            auto action = std::make_unique<MultiNoteSnapToSemitoneAction>(
                notesToSnap, oldMidis, oldOffsets, newMidis,
                [this, rebuildAndNotify](const std::vector<Note *> &) {
                  rebuildAndNotify();
                });
            undoManager->addAction(std::move(action));
          }

          for (size_t i = 0; i < notesToSnap.size(); ++i) {
            notesToSnap[i]->setMidiNote(newMidis[i]);
            notesToSnap[i]->setPitchOffset(0.0f);
            notesToSnap[i]->markDirty();
          }

          rebuildAndNotify();
        }
        return;
      }
    }

    // Snap single note pitch to nearest standard semitone
    float oldMidi = note->getMidiNote();
    float oldOffset = note->getPitchOffset();
    float adjustedMidi = oldMidi + oldOffset;
    float snappedMidi = std::round(adjustedMidi);

    if (std::abs(snappedMidi - adjustedMidi) > 0.001f) {
      if (undoManager) {
        auto action = std::make_unique<NoteSnapToSemitoneAction>(
            note, oldMidi, oldOffset, snappedMidi,
            [this, rebuildAndNotify](Note *n) { rebuildAndNotify(); });
        undoManager->addAction(std::move(action));
      }

      note->setMidiNote(snappedMidi);
      note->setPitchOffset(0.0f);
      note->markDirty();
      rebuildAndNotify();
    }
  }
}

void PianoRollComponent::mouseWheelMove(const juce::MouseEvent &e,
                                        const juce::MouseWheelDetails &wheel) {
  float scrollMultiplier = wheel.isSmooth ? 200.0f : 80.0f;

  bool isOverPianoKeys = e.x < pianoKeysWidth;
  bool isOverTimeline = e.y < headerHeight;

  // Hover-based zoom (no modifier keys needed)
  if (!e.mods.isCommandDown() && !e.mods.isCtrlDown()) {
    // Over piano keys: vertical zoom
    if (isOverPianoKeys) {
      // Calculate MIDI note at mouse position before zoom
      float mouseY = e.y - headerHeight;
      float midiAtMouse = (mouseY + scrollY) / pixelsPerSemitone;

      float zoomFactor = 1.0f + wheel.deltaY * 0.3f;
      float newPps = pixelsPerSemitone * zoomFactor;
      newPps = juce::jlimit(MIN_PIXELS_PER_SEMITONE, MAX_PIXELS_PER_SEMITONE,
                            newPps);
      pixelsPerSemitone = newPps;
      coordMapper->setPixelsPerSemitone(newPps);

      // Adjust scroll position to keep MIDI note at mouse position fixed
      double newScrollY = midiAtMouse * pixelsPerSemitone - mouseY;
      newScrollY = std::max(0.0, newScrollY);
      scrollY = newScrollY;
      coordMapper->setScrollY(newScrollY);

      updateScrollBars();
      repaint();
      return;
    }

    // Over timeline: horizontal zoom
    if (isOverTimeline) {
      // Calculate time at mouse position before zoom
      float mouseX = e.x - pianoKeysWidth;
      double timeAtMouse = (mouseX + scrollX) / pixelsPerSecond;

      float zoomFactor = 1.0f + wheel.deltaY * 0.3f;
      float newPps = pixelsPerSecond * zoomFactor;
      newPps =
          juce::jlimit(MIN_PIXELS_PER_SECOND, MAX_PIXELS_PER_SECOND, newPps);
      pixelsPerSecond = newPps;
      coordMapper->setPixelsPerSecond(newPps);

      // Adjust scroll position to keep time at mouse position fixed
      double newScrollX = timeAtMouse * pixelsPerSecond - mouseX;
      newScrollX = std::max(0.0, newScrollX);
      scrollX = newScrollX;
      coordMapper->setScrollX(newScrollX);

      updateScrollBars();
      repaint();
      if (onZoomChanged)
        onZoomChanged(pixelsPerSecond);
      return;
    }

    // Normal scrolling in grid area
    float deltaX = wheel.deltaX;
    float deltaY = wheel.deltaY;

    if (e.mods.isShiftDown() && std::abs(deltaX) < 0.001f) {
      deltaX = deltaY;
      deltaY = 0.0f;
    }

    if (std::abs(deltaX) > 0.001f) {
      double newScrollX = scrollX - deltaX * scrollMultiplier;
      newScrollX = std::max(0.0, newScrollX);
      horizontalScrollBar.setCurrentRangeStart(newScrollX);
    }

    if (std::abs(deltaY) > 0.001f) {
      double newScrollY = scrollY - deltaY * scrollMultiplier;
      verticalScrollBar.setCurrentRangeStart(newScrollY);
    }
    return;
  }

  // Key-based zoom in grid area
  if (e.mods.isCommandDown() || e.mods.isCtrlDown()) {
    float zoomFactor = 1.0f + wheel.deltaY * 0.3f;

    if (e.mods.isShiftDown()) {
      // Vertical zoom - center on mouse position
      float mouseY = static_cast<float>(e.y - headerHeight);
      float midiAtMouse = yToMidi(mouseY + static_cast<float>(scrollY));

      float newPps = pixelsPerSemitone * zoomFactor;
      newPps = juce::jlimit(MIN_PIXELS_PER_SEMITONE, MAX_PIXELS_PER_SEMITONE,
                            newPps);

      // Adjust scroll to keep mouse position stable
      float newMouseY = midiToY(midiAtMouse);
      scrollY = std::max(0.0, static_cast<double>(newMouseY - mouseY));
      coordMapper->setScrollY(scrollY);

      pixelsPerSemitone = newPps;
      coordMapper->setPixelsPerSemitone(newPps);
      updateScrollBars();
      repaint();
    } else {
      // Horizontal zoom - center on mouse position
      float mouseX = static_cast<float>(e.x - pianoKeysWidth);
      double timeAtMouse = xToTime(mouseX + static_cast<float>(scrollX));

      float newPps = pixelsPerSecond * zoomFactor;
      newPps =
          juce::jlimit(MIN_PIXELS_PER_SECOND, MAX_PIXELS_PER_SECOND, newPps);

      // Adjust scroll to keep mouse position stable
      float newMouseX = static_cast<float>(timeAtMouse * newPps);
      scrollX = std::max(0.0, static_cast<double>(newMouseX - mouseX));
      coordMapper->setScrollX(scrollX);

      pixelsPerSecond = newPps;
      coordMapper->setPixelsPerSecond(newPps);
      updateScrollBars();
      repaint();

      if (onZoomChanged)
        onZoomChanged(pixelsPerSecond);
    }
  }
}

void PianoRollComponent::mouseMagnify(const juce::MouseEvent &e,
                                      float scaleFactor) {
  // Pinch-to-zoom on trackpad - horizontal zoom, center on mouse position
  float mouseX = static_cast<float>(e.x - pianoKeysWidth);
  double timeAtMouse = xToTime(mouseX + static_cast<float>(scrollX));

  float newPps = pixelsPerSecond * scaleFactor;
  newPps = juce::jlimit(MIN_PIXELS_PER_SECOND, MAX_PIXELS_PER_SECOND, newPps);

  // Adjust scroll to keep mouse position stable
  float newMouseX = static_cast<float>(timeAtMouse * newPps);
  scrollX = std::max(0.0, static_cast<double>(newMouseX - mouseX));
  coordMapper->setScrollX(scrollX);

  pixelsPerSecond = newPps;
  coordMapper->setPixelsPerSecond(newPps);
  updateScrollBars();
  repaint();

  if (onZoomChanged)
    onZoomChanged(pixelsPerSecond);
}

void PianoRollComponent::scrollBarMoved(juce::ScrollBar *scrollBar,
                                        double newRangeStart) {
  if (scrollBar == &horizontalScrollBar) {
    scrollX = newRangeStart;
    coordMapper->setScrollX(newRangeStart);

    // Notify scroll changed for synchronization
    if (onScrollChanged)
      onScrollChanged(scrollX);
  } else if (scrollBar == &verticalScrollBar) {
    scrollY = newRangeStart;
    coordMapper->setScrollY(newRangeStart);
  }
  repaint();
}

void PianoRollComponent::setProject(Project *proj) {
  project = proj;

  // Update modular components
  renderer->setProject(proj);
  scrollZoomController->setProject(proj);
  pitchEditor->setProject(proj);
  noteSplitter->setProject(proj);

  // Clear all caches when project changes to free memory
  invalidateBasePitchCache();
  waveformCache = juce::Image(); // Clear waveform cache
  cachedScrollX = -1.0;
  cachedPixelsPerSecond = -1.0f;
  cachedWidth = 0;
  cachedHeight = 0;

  updateScrollBars();
  repaint();
}

void PianoRollComponent::setUndoManager(PitchUndoManager *manager) {
  undoManager = manager;
  pitchEditor->setUndoManager(manager);
  noteSplitter->setUndoManager(manager);
}

bool PianoRollComponent::keyPressed(const juce::KeyPress &key,
                                    juce::Component *) {
  // Ctrl+Z or Cmd+Z: Undo
  if (key == juce::KeyPress('z', juce::ModifierKeys::ctrlModifier, 0) ||
      key == juce::KeyPress('z', juce::ModifierKeys::commandModifier, 0)) {
    if (onUndo) {
      onUndo();
      return true;
    }
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
    if (onRedo) {
      onRedo();
      return true;
    }
  }

  // Space: Play/Pause
  if (key == juce::KeyPress::spaceKey) {
    if (onPlayPause) {
      onPlayPause();
      return true;
    }
  }

  // D: Toggle draw mode
  if (key == juce::KeyPress('d') || key == juce::KeyPress('D')) {
    if (editMode == EditMode::Draw)
      setEditMode(EditMode::Select);
    else
      setEditMode(EditMode::Draw);
    return true;
  }

  return false;
}

void PianoRollComponent::setCursorTime(double time) {
  if (std::abs(cursorTime - time) < 0.0001)
    return; // Skip if no change

  // Calculate dirty rectangle for cursor position
  // Include timeline area (from 0) and extra width for triangle indicator
  auto getCursorRect = [this](double t) -> juce::Rectangle<int> {
    float x =
        static_cast<float>(t * pixelsPerSecond - scrollX) + pianoKeysWidth;
    constexpr int triangleHalfWidth = 6; // Half of triangle width + margin
    int rectX = static_cast<int>(x) - triangleHalfWidth;
    int rectWidth =
        triangleHalfWidth * 2 + 2; // Full triangle width + cursor line
    // Start from 0 (top of timeline) to include triangle indicator
    return juce::Rectangle<int>(rectX, 0, rectWidth, getHeight());
  };

  // Repaint OLD cursor position (the current cursorTime that's about to change)
  repaint(getCursorRect(cursorTime));

  // Update cursor time
  cursorTime = time;

  // Repaint NEW cursor position
  repaint(getCursorRect(cursorTime));
}

void PianoRollComponent::setPixelsPerSecond(float pps, bool centerOnCursor) {
  float oldPps = pixelsPerSecond;
  float newPps =
      juce::jlimit(MIN_PIXELS_PER_SECOND, MAX_PIXELS_PER_SECOND, pps);

  if (std::abs(oldPps - newPps) < 0.01f)
    return; // No significant change

  if (centerOnCursor) {
    // Calculate cursor position relative to view
    float cursorX = static_cast<float>(cursorTime * oldPps);
    float cursorRelativeX = cursorX - static_cast<float>(scrollX);

    // Calculate new scroll position to keep cursor at same relative position
    float newCursorX = static_cast<float>(cursorTime * newPps);
    scrollX = static_cast<double>(newCursorX - cursorRelativeX);
    scrollX = std::max(0.0, scrollX);
    coordMapper->setScrollX(scrollX);
  }

  pixelsPerSecond = newPps;
  coordMapper->setPixelsPerSecond(newPps);
  updateScrollBars();
  repaint();

  // Don't call onZoomChanged here to avoid infinite recursion
  // The caller is responsible for synchronizing other components
}

void PianoRollComponent::setPixelsPerSemitone(float pps) {
  pixelsPerSemitone =
      juce::jlimit(MIN_PIXELS_PER_SEMITONE, MAX_PIXELS_PER_SEMITONE, pps);
  coordMapper->setPixelsPerSemitone(pixelsPerSemitone);
  updateScrollBars();
  repaint();
}

void PianoRollComponent::setScrollX(double x) {
  if (std::abs(scrollX - x) < 0.01)
    return; // No significant change

  scrollX = x;
  coordMapper->setScrollX(x);
  horizontalScrollBar.setCurrentRangeStart(x);

  // Don't call onScrollChanged here to avoid infinite recursion
  // The caller is responsible for synchronizing other components

  repaint();
}

void PianoRollComponent::centerOnPitchRange(float minMidi, float maxMidi) {
  // Calculate center MIDI note
  float centerMidi = (minMidi + maxMidi) / 2.0f;

  // Calculate Y position for center
  float centerY = midiToY(centerMidi);

  // Get visible height
  auto bounds = getLocalBounds();
  int visibleHeight = bounds.getHeight() - 8; // scrollbar height

  // Calculate scroll position to center the pitch range
  double newScrollY = centerY - visibleHeight / 2.0;

  // Clamp to valid range
  double totalHeight = (MAX_MIDI_NOTE - MIN_MIDI_NOTE) * pixelsPerSemitone;
  newScrollY =
      juce::jlimit(0.0, std::max(0.0, totalHeight - visibleHeight), newScrollY);

  scrollY = newScrollY;
  coordMapper->setScrollY(newScrollY);
  verticalScrollBar.setCurrentRangeStart(newScrollY);
  repaint();
}

void PianoRollComponent::setEditMode(EditMode mode) {
  if (editMode == EditMode::Stretch && mode != EditMode::Stretch &&
      stretchDrag.active) {
    cancelStretchDrag();
  }

  editMode = mode;

  // Clear split guide when leaving split mode
  if (mode != EditMode::Split) {
    splitGuideX = -1.0f;
    splitGuideNote = nullptr;
  }

  // Change cursor based on mode
  if (mode == EditMode::Draw) {
    // Create a custom pen cursor
    // Simple pen icon: 16x16 pixels with pen tip at bottom-left
    juce::Image penImage(juce::Image::ARGB, 16, 16, true);
    juce::Graphics g(penImage);

    // Draw a simple pen shape
    g.setColour(juce::Colours::white);
    // Pen body (diagonal line from top-right to bottom-left)
    g.drawLine(12.0f, 2.0f, 2.0f, 12.0f, 2.0f);
    // Pen tip (small triangle at bottom-left)
    juce::Path tip;
    tip.addTriangle(0.0f, 14.0f, 4.0f, 10.0f, 2.0f, 12.0f);
    g.fillPath(tip);

    // Set hotspot at pen tip (bottom-left corner)
    setMouseCursor(juce::MouseCursor(penImage, 0, 14));
  } else {
    setMouseCursor(juce::MouseCursor::NormalCursor);
  }

  if (mode != EditMode::Stretch) {
    hoveredStretchBoundaryIndex = -1;
  }

  repaint();
}

std::vector<PianoRollComponent::StretchBoundary>
PianoRollComponent::collectStretchBoundaries() const {
  std::vector<StretchBoundary> boundaries;
  if (!project)
    return boundaries;

  std::vector<Note *> ordered;
  ordered.reserve(project->getNotes().size());
  for (auto &note : project->getNotes()) {
    if (!note.isRest())
      ordered.push_back(&note);
  }

  std::sort(ordered.begin(), ordered.end(),
            [](const Note *a, const Note *b) {
              return a->getStartFrame() < b->getStartFrame();
            });

  for (size_t i = 0; i + 1 < ordered.size(); ++i) {
    Note *left = ordered[i];
    Note *right = ordered[i + 1];
    int frame = left->getEndFrame();
    boundaries.push_back({left, right, frame});
  }

  return boundaries;
}

int PianoRollComponent::findStretchBoundaryIndex(float worldX,
                                                  float tolerancePx) const {
  auto boundaries = collectStretchBoundaries();
  int bestIndex = -1;
  float bestDist = tolerancePx;

  for (size_t i = 0; i < boundaries.size(); ++i) {
    float boundaryX = framesToSeconds(boundaries[i].frame) * pixelsPerSecond;
    float dist = std::abs(worldX - boundaryX);
    if (dist <= bestDist) {
      bestIndex = static_cast<int>(i);
      bestDist = dist;
    }
  }

  return bestIndex;
}

void PianoRollComponent::startStretchDrag(const StretchBoundary &boundary) {
  if (!project || !boundary.left || !boundary.right)
    return;

  stretchDrag = {};
  stretchDrag.active = true;
  stretchDrag.boundary = boundary;
  stretchDrag.originalBoundary = boundary.left->getEndFrame();
  stretchDrag.originalLeftStart = boundary.left->getStartFrame();
  stretchDrag.originalLeftEnd = boundary.left->getEndFrame();
  stretchDrag.originalRightStart = boundary.right->getStartFrame();
  stretchDrag.originalRightEnd = boundary.right->getEndFrame();
  stretchDrag.currentBoundary = stretchDrag.originalBoundary;
  stretchDrag.minFrame =
      stretchDrag.originalLeftStart + minStretchNoteFrames;
  stretchDrag.maxFrame =
      stretchDrag.originalRightEnd - minStretchNoteFrames;

  auto &audioData = project->getAudioData();
  const int totalFrames = static_cast<int>(audioData.f0.size());
  if (totalFrames <= 0) {
    stretchDrag.active = false;
    return;
  }

  // Ensure all notes have clip waveforms so dragging never falls back to
  // global waveform slicing.
  if (audioData.waveform.getNumSamples() > 0) {
    const float *src = audioData.waveform.getReadPointer(0);
    const int totalSamples = audioData.waveform.getNumSamples();
    for (auto &note : project->getNotes()) {
      if (note.hasClipWaveform())
        continue;
      int startSample = note.getStartFrame() * HOP_SIZE;
      int endSample = note.getEndFrame() * HOP_SIZE;
      startSample = std::max(0, std::min(startSample, totalSamples));
      endSample = std::max(startSample, std::min(endSample, totalSamples));
      std::vector<float> clip;
      clip.reserve(static_cast<size_t>(endSample - startSample));
      for (int i = startSample; i < endSample; ++i)
        clip.push_back(src[i]);
      note.setClipWaveform(std::move(clip));
    }
  }

  if (audioData.deltaPitch.size() < static_cast<size_t>(totalFrames))
    audioData.deltaPitch.resize(static_cast<size_t>(totalFrames), 0.0f);
  if (audioData.voicedMask.size() < static_cast<size_t>(totalFrames))
    audioData.voicedMask.resize(static_cast<size_t>(totalFrames), true);

  if (stretchDrag.originalBoundary < stretchDrag.minFrame ||
      stretchDrag.maxFrame < stretchDrag.minFrame) {
    stretchDrag.active = false;
    return;
  }

  int leftStart = std::max(0, stretchDrag.originalLeftStart);
  int leftEnd = std::min(stretchDrag.originalLeftEnd, totalFrames);
  stretchDrag.rangeStartFull = leftStart;
  stretchDrag.rangeEndFull =
      std::min(totalFrames, stretchDrag.originalRightEnd);

  if (leftEnd <= leftStart ||
      stretchDrag.rangeEndFull <= stretchDrag.rangeStartFull) {
    stretchDrag.active = false;
    return;
  }

  stretchDrag.leftDelta.assign(
      audioData.deltaPitch.begin() + leftStart,
      audioData.deltaPitch.begin() + leftEnd);
  stretchDrag.leftVoiced.assign(
      audioData.voicedMask.begin() + leftStart,
      audioData.voicedMask.begin() + leftEnd);
  int rightStart = std::max(0, stretchDrag.originalRightStart);
  int rightEnd = std::min(stretchDrag.originalRightEnd, totalFrames);
  if (rightEnd <= rightStart) {
    stretchDrag.active = false;
    return;
  }
  stretchDrag.rightDelta.assign(
      audioData.deltaPitch.begin() + rightStart,
      audioData.deltaPitch.begin() + rightEnd);
  stretchDrag.rightVoiced.assign(
      audioData.voicedMask.begin() + rightStart,
      audioData.voicedMask.begin() + rightEnd);

  if (boundary.left->hasClipWaveform())
    stretchDrag.originalLeftClip = boundary.left->getClipWaveform();
  if (boundary.right->hasClipWaveform())
    stretchDrag.originalRightClip = boundary.right->getClipWaveform();

  stretchDrag.originalDeltaRangeFull.assign(
      audioData.deltaPitch.begin() + stretchDrag.rangeStartFull,
      audioData.deltaPitch.begin() + stretchDrag.rangeEndFull);
  stretchDrag.originalVoicedRangeFull.assign(
      audioData.voicedMask.begin() + stretchDrag.rangeStartFull,
      audioData.voicedMask.begin() + stretchDrag.rangeEndFull);

  if (!audioData.melSpectrogram.empty() &&
      stretchDrag.rangeStartFull <
          static_cast<int>(audioData.melSpectrogram.size())) {
    int melEnd = std::min(stretchDrag.rangeEndFull,
                          static_cast<int>(audioData.melSpectrogram.size()));
    stretchDrag.originalMelRangeFull.assign(
        audioData.melSpectrogram.begin() + stretchDrag.rangeStartFull,
        audioData.melSpectrogram.begin() + melEnd);
  }
}

void PianoRollComponent::updateStretchDrag(int targetFrame) {
  if (!stretchDrag.active || !project || !stretchDrag.boundary.left ||
      !stretchDrag.boundary.right)
    return;

  int previewRangeStart = -1;
  int previewRangeEnd = -1;

  targetFrame =
      juce::jlimit(stretchDrag.minFrame, stretchDrag.maxFrame, targetFrame);
  if (targetFrame == stretchDrag.currentBoundary)
    return;

  const int leftStart = stretchDrag.originalLeftStart;
  const int newLeftLength = targetFrame - leftStart;
  if (newLeftLength < minStretchNoteFrames)
    return;
  const int newRightLength = stretchDrag.originalRightEnd - targetFrame;
  if (newRightLength < minStretchNoteFrames)
    return;

  stretchDrag.currentBoundary = targetFrame;
  stretchDrag.changed = true;

  auto &audioData = project->getAudioData();
  const int totalFrames = static_cast<int>(audioData.deltaPitch.size());
  if (audioData.deltaPitch.size() < static_cast<size_t>(totalFrames))
    audioData.deltaPitch.resize(static_cast<size_t>(totalFrames), 0.0f);
  if (audioData.voicedMask.size() < static_cast<size_t>(totalFrames))
    audioData.voicedMask.resize(static_cast<size_t>(totalFrames), true);

  // Restore original region to avoid cumulative errors during drag.
  if (!stretchDrag.originalDeltaRangeFull.empty() &&
      !stretchDrag.originalVoicedRangeFull.empty()) {
    for (int i = stretchDrag.rangeStartFull;
         i < stretchDrag.rangeEndFull; ++i) {
      int idx = i - stretchDrag.rangeStartFull;
      audioData.deltaPitch[static_cast<size_t>(i)] =
          stretchDrag.originalDeltaRangeFull[static_cast<size_t>(idx)];
      audioData.voicedMask[static_cast<size_t>(i)] =
          stretchDrag.originalVoicedRangeFull[static_cast<size_t>(idx)];
    }
  }
  if (!stretchDrag.originalMelRangeFull.empty() &&
      audioData.melSpectrogram.size() >=
          static_cast<size_t>(stretchDrag.rangeStartFull +
                              stretchDrag.originalMelRangeFull.size())) {
    for (size_t i = 0; i < stretchDrag.originalMelRangeFull.size(); ++i)
      audioData.melSpectrogram[static_cast<size_t>(stretchDrag.rangeStartFull) +
                               i] = stretchDrag.originalMelRangeFull[i];
  }

  auto newLeftDelta =
      CurveResampler::resampleLinear(stretchDrag.leftDelta, newLeftLength);
  auto newLeftVoiced =
      CurveResampler::resampleNearest(stretchDrag.leftVoiced, newLeftLength);

  for (int i = 0; i < newLeftLength; ++i) {
    audioData.deltaPitch[static_cast<size_t>(leftStart + i)] =
        newLeftDelta[static_cast<size_t>(i)];
    audioData.voicedMask[static_cast<size_t>(leftStart + i)] =
        newLeftVoiced[static_cast<size_t>(i)];
  }

  if (!stretchDrag.originalLeftClip.empty()) {
    const int newLeftSamples =
        std::max(0, newLeftLength * HOP_SIZE);
    auto newLeftClip =
        CurveResampler::resampleLinear(stretchDrag.originalLeftClip,
                                       newLeftSamples);
    stretchDrag.boundary.left->setClipWaveform(std::move(newLeftClip));
  }

  auto newRightDelta =
      CurveResampler::resampleLinear(stretchDrag.rightDelta, newRightLength);
  auto newRightVoiced =
      CurveResampler::resampleNearest(stretchDrag.rightVoiced, newRightLength);

  for (int i = 0; i < newRightLength; ++i) {
    audioData.deltaPitch[static_cast<size_t>(targetFrame + i)] =
        newRightDelta[static_cast<size_t>(i)];
    audioData.voicedMask[static_cast<size_t>(targetFrame + i)] =
        newRightVoiced[static_cast<size_t>(i)];
  }

  auto *rightNote = stretchDrag.boundary.right;
  if (rightNote) {
    rightNote->setStartFrame(targetFrame);
    rightNote->setEndFrame(stretchDrag.originalRightEnd);
    rightNote->markDirty();
    if (!stretchDrag.originalRightClip.empty()) {
      const int newRightSamples =
          std::max(0, newRightLength * HOP_SIZE);
      auto newRightClip =
          CurveResampler::resampleLinear(stretchDrag.originalRightClip,
                                         newRightSamples);
      rightNote->setClipWaveform(std::move(newRightClip));
    }
  }

  stretchDrag.boundary.left->setEndFrame(targetFrame);
  stretchDrag.boundary.left->markDirty();

  if (!audioData.melSpectrogram.empty() &&
      stretchDrag.rangeStartFull <
          static_cast<int>(audioData.melSpectrogram.size())) {
    const int melSize = static_cast<int>(audioData.melSpectrogram.size());
    int rangeStart = std::clamp(stretchDrag.rangeStartFull, 0, melSize);
    int rangeEnd =
        std::clamp(stretchDrag.originalRightEnd, 0, melSize);

    std::vector<std::vector<float>> newMel;
    if (rangeEnd > rangeStart) {
      const int leftLen = targetFrame - stretchDrag.originalLeftStart;
      const int rightLen = stretchDrag.originalRightEnd - targetFrame;
      const int leftOffset =
          stretchDrag.originalLeftStart - stretchDrag.rangeStartFull;
      const int rightOffset =
          stretchDrag.originalRightStart - stretchDrag.rangeStartFull;
      std::vector<std::vector<float>> leftMel;
      std::vector<std::vector<float>> rightMel;
      if (leftOffset >= 0 &&
          leftOffset +
                  (stretchDrag.originalLeftEnd -
                   stretchDrag.originalLeftStart) <=
              static_cast<int>(stretchDrag.originalMelRangeFull.size())) {
        leftMel.assign(
            stretchDrag.originalMelRangeFull.begin() + leftOffset,
            stretchDrag.originalMelRangeFull.begin() + leftOffset +
                (stretchDrag.originalLeftEnd - stretchDrag.originalLeftStart));
      }
      if (rightOffset >= 0 &&
          rightOffset +
                  (stretchDrag.originalRightEnd -
                   stretchDrag.originalRightStart) <=
              static_cast<int>(stretchDrag.originalMelRangeFull.size())) {
        rightMel.assign(
            stretchDrag.originalMelRangeFull.begin() + rightOffset,
            stretchDrag.originalMelRangeFull.begin() + rightOffset +
                (stretchDrag.originalRightEnd -
                 stretchDrag.originalRightStart));
      }
      auto newLeftMel = CurveResampler::resampleNearest2D(leftMel, leftLen);
      auto newRightMel =
          CurveResampler::resampleNearest2D(rightMel, rightLen);
      newMel.reserve(static_cast<size_t>(leftLen + rightLen));
      newMel.insert(newMel.end(), newLeftMel.begin(), newLeftMel.end());
      newMel.insert(newMel.end(), newRightMel.begin(), newRightMel.end());
    }

    if (!newMel.empty() &&
        static_cast<int>(newMel.size()) == (rangeEnd - rangeStart)) {
      for (int i = rangeStart; i < rangeEnd; ++i)
        audioData.melSpectrogram[static_cast<size_t>(i)] =
            newMel[static_cast<size_t>(i - rangeStart)];
      previewRangeStart = rangeStart;
      previewRangeEnd = rangeEnd;
    }
  }

  PitchCurveProcessor::rebuildBaseFromNotes(*project);
  PitchCurveProcessor::composeF0InPlace(*project, /*applyUvMask=*/false);
  invalidateBasePitchCache();

  if (onPitchEdited)
    onPitchEdited();

  if (previewRangeStart >= 0 && previewRangeEnd > previewRangeStart &&
      onPitchEditFinished) {
    const int f0Size = static_cast<int>(audioData.f0.size());
    int smoothStart = std::max(0, previewRangeStart - 60);
    int smoothEnd = std::min(f0Size, previewRangeEnd + 60);
    project->setF0DirtyRange(smoothStart, smoothEnd);

    const auto now =
        static_cast<juce::int64>(juce::Time::getMillisecondCounter());
    if (now - lastStretchPreviewTime >= minStretchPreviewInterval) {
      lastStretchPreviewTime = now;
      onPitchEditFinished();
    }
  }
}

void PianoRollComponent::finishStretchDrag() {
  if (!stretchDrag.active || !project) {
    stretchDrag = {};
    return;
  }

  if (!stretchDrag.changed) {
    cancelStretchDrag();
    return;
  }

  auto &audioData = project->getAudioData();
  const int totalFrames = static_cast<int>(audioData.deltaPitch.size());
  const int currentBoundary = stretchDrag.currentBoundary;
  int rangeStart = std::clamp(stretchDrag.rangeStartFull, 0, totalFrames);
  int rangeEnd =
      std::clamp(stretchDrag.originalRightEnd, 0, totalFrames);
  if (rangeEnd <= rangeStart) {
    cancelStretchDrag();
    return;
  }

  std::vector<float> newDelta(
      audioData.deltaPitch.begin() + rangeStart,
      audioData.deltaPitch.begin() + rangeEnd);
  std::vector<bool> newVoiced(
      audioData.voicedMask.begin() + rangeStart,
      audioData.voicedMask.begin() + rangeEnd);
  std::vector<std::vector<float>> newMel;
  if (!audioData.melSpectrogram.empty() &&
      rangeEnd <= static_cast<int>(audioData.melSpectrogram.size())) {
    const int leftLen = currentBoundary - stretchDrag.originalLeftStart;
    const int rightLen = stretchDrag.originalRightEnd - currentBoundary;
    const int leftOffset =
        stretchDrag.originalLeftStart - stretchDrag.rangeStartFull;
    const int rightOffset =
        stretchDrag.originalRightStart - stretchDrag.rangeStartFull;
    std::vector<std::vector<float>> leftMel;
    std::vector<std::vector<float>> rightMel;
    if (leftOffset >= 0 &&
        leftOffset +
                (stretchDrag.originalLeftEnd -
                 stretchDrag.originalLeftStart) <=
            static_cast<int>(stretchDrag.originalMelRangeFull.size())) {
      leftMel.assign(
          stretchDrag.originalMelRangeFull.begin() + leftOffset,
          stretchDrag.originalMelRangeFull.begin() + leftOffset +
              (stretchDrag.originalLeftEnd - stretchDrag.originalLeftStart));
    }
    if (rightOffset >= 0 &&
        rightOffset +
                (stretchDrag.originalRightEnd -
                 stretchDrag.originalRightStart) <=
            static_cast<int>(stretchDrag.originalMelRangeFull.size())) {
      rightMel.assign(
          stretchDrag.originalMelRangeFull.begin() + rightOffset,
          stretchDrag.originalMelRangeFull.begin() + rightOffset +
              (stretchDrag.originalRightEnd - stretchDrag.originalRightStart));
    }
    auto newLeftMel = CurveResampler::resampleNearest2D(leftMel, leftLen);
    auto newRightMel = CurveResampler::resampleNearest2D(rightMel, rightLen);
    newMel.reserve(static_cast<size_t>(leftLen + rightLen));
    newMel.insert(newMel.end(), newLeftMel.begin(), newLeftMel.end());
    newMel.insert(newMel.end(), newRightMel.begin(), newRightMel.end());

    if (!newMel.empty() &&
        static_cast<int>(newMel.size()) == (rangeEnd - rangeStart)) {
      for (int i = rangeStart; i < rangeEnd; ++i)
        audioData.melSpectrogram[static_cast<size_t>(i)] =
            newMel[static_cast<size_t>(i - rangeStart)];
    } else {
      newMel.clear();
    }
  }

  int newLeftStart = stretchDrag.boundary.left->getStartFrame();
  int newLeftEnd = stretchDrag.boundary.left->getEndFrame();
  std::vector<float> newLeftClip = stretchDrag.boundary.left->getClipWaveform();
  std::vector<float> newRightClip;
  if (stretchDrag.boundary.right)
    newRightClip = stretchDrag.boundary.right->getClipWaveform();

  if (undoManager) {
    int capturedRangeStart = rangeStart;
    int capturedRangeEnd = rangeEnd;
    std::vector<float> oldDelta;
    std::vector<bool> oldVoiced;
    std::vector<std::vector<float>> oldMel;
    if (!stretchDrag.originalDeltaRangeFull.empty() &&
        !stretchDrag.originalVoicedRangeFull.empty()) {
      int offset = rangeStart - stretchDrag.rangeStartFull;
      int count = rangeEnd - rangeStart;
      if (offset >= 0 &&
          offset + count <=
              static_cast<int>(stretchDrag.originalDeltaRangeFull.size())) {
        oldDelta.assign(stretchDrag.originalDeltaRangeFull.begin() + offset,
                        stretchDrag.originalDeltaRangeFull.begin() + offset +
                            count);
        oldVoiced.assign(stretchDrag.originalVoicedRangeFull.begin() + offset,
                         stretchDrag.originalVoicedRangeFull.begin() + offset +
                             count);
      }
    }
    if (!stretchDrag.originalMelRangeFull.empty()) {
      int offset = rangeStart - stretchDrag.rangeStartFull;
      int count = rangeEnd - rangeStart;
      if (offset >= 0 &&
          offset + count <=
              static_cast<int>(stretchDrag.originalMelRangeFull.size())) {
        oldMel.assign(stretchDrag.originalMelRangeFull.begin() + offset,
                      stretchDrag.originalMelRangeFull.begin() + offset + count);
      }
    }

    auto action = std::make_unique<NoteTimingStretchAction>(
        stretchDrag.boundary.left,
        stretchDrag.boundary.right,
        &audioData.deltaPitch, &audioData.voicedMask, &audioData.melSpectrogram,
        capturedRangeStart, capturedRangeEnd, stretchDrag.originalLeftStart,
        stretchDrag.originalLeftEnd, stretchDrag.originalRightStart,
        stretchDrag.originalRightEnd, newLeftStart, newLeftEnd,
        currentBoundary, stretchDrag.originalRightEnd,
        stretchDrag.originalLeftClip, newLeftClip,
        stretchDrag.originalRightClip, newRightClip,
        std::move(oldDelta), std::move(newDelta),
        std::move(oldVoiced), std::move(newVoiced),
        std::move(oldMel), std::move(newMel),
        [this](int startFrame, int endFrame) {
          if (!project)
            return;
          PitchCurveProcessor::rebuildBaseFromNotes(*project);
          PitchCurveProcessor::composeF0InPlace(*project,
                                                /*applyUvMask=*/false);
          invalidateBasePitchCache();
          const int f0Size =
              static_cast<int>(project->getAudioData().f0.size());
          int smoothStart = std::max(0, startFrame - 60);
          int smoothEnd = std::min(f0Size, endFrame + 60);
          project->setF0DirtyRange(smoothStart, smoothEnd);
        });
    undoManager->addAction(std::move(action));
  }

  const int f0Size = static_cast<int>(audioData.f0.size());
  int smoothStart = std::max(0, rangeStart - 60);
  int smoothEnd = std::min(f0Size, rangeEnd + 60);
  project->setF0DirtyRange(smoothStart, smoothEnd);

  if (onPitchEditFinished)
    onPitchEditFinished();

  stretchDrag = {};
}

void PianoRollComponent::cancelStretchDrag() {
  if (!stretchDrag.active || !project) {
    stretchDrag = {};
    return;
  }

  auto &audioData = project->getAudioData();
  const int totalFrames = static_cast<int>(audioData.deltaPitch.size());
  int rangeStart = std::clamp(stretchDrag.rangeStartFull, 0, totalFrames);
  int rangeEnd = std::clamp(stretchDrag.rangeEndFull, 0, totalFrames);

  if (rangeEnd > rangeStart &&
      stretchDrag.originalDeltaRangeFull.size() ==
          static_cast<size_t>(rangeEnd - rangeStart)) {
    for (int i = rangeStart; i < rangeEnd; ++i)
      audioData.deltaPitch[static_cast<size_t>(i)] =
          stretchDrag
              .originalDeltaRangeFull[static_cast<size_t>(i - rangeStart)];
  }

  if (rangeEnd > rangeStart &&
      stretchDrag.originalVoicedRangeFull.size() ==
          static_cast<size_t>(rangeEnd - rangeStart)) {
    for (int i = rangeStart; i < rangeEnd; ++i)
      audioData.voicedMask[static_cast<size_t>(i)] =
          stretchDrag
              .originalVoicedRangeFull[static_cast<size_t>(i - rangeStart)];
  }

  if (!stretchDrag.originalMelRangeFull.empty() &&
      rangeStart < rangeEnd &&
      audioData.melSpectrogram.size() >=
          static_cast<size_t>(rangeStart + stretchDrag.originalMelRangeFull.size())) {
    for (size_t i = 0; i < stretchDrag.originalMelRangeFull.size(); ++i)
      audioData.melSpectrogram[static_cast<size_t>(rangeStart) + i] =
          stretchDrag.originalMelRangeFull[i];
  }

  if (stretchDrag.boundary.left) {
    stretchDrag.boundary.left->setStartFrame(stretchDrag.originalLeftStart);
    stretchDrag.boundary.left->setEndFrame(stretchDrag.originalLeftEnd);
    stretchDrag.boundary.left->markDirty();
    if (!stretchDrag.originalLeftClip.empty())
      stretchDrag.boundary.left->setClipWaveform(stretchDrag.originalLeftClip);
  }
  if (stretchDrag.boundary.right) {
    stretchDrag.boundary.right->setStartFrame(stretchDrag.originalRightStart);
    stretchDrag.boundary.right->setEndFrame(stretchDrag.originalRightEnd);
    stretchDrag.boundary.right->markDirty();
    if (!stretchDrag.originalRightClip.empty())
      stretchDrag.boundary.right->setClipWaveform(stretchDrag.originalRightClip);
  }

  PitchCurveProcessor::rebuildBaseFromNotes(*project);
  PitchCurveProcessor::composeF0InPlace(*project, /*applyUvMask=*/false);
  invalidateBasePitchCache();

  if (onPitchEdited)
    onPitchEdited();

  stretchDrag = {};
}

Note *PianoRollComponent::findNoteAt(float x, float y) {
  if (!project)
    return nullptr;

  for (auto &note : project->getNotes()) {
    // Skip rest notes
    if (note.isRest())
      continue;

    float noteX = framesToSeconds(note.getStartFrame()) * pixelsPerSecond;
    float noteW = framesToSeconds(note.getDurationFrames()) * pixelsPerSecond;
    float noteY = midiToY(note.getAdjustedMidiNote());
    float noteH = pixelsPerSemitone;

    if (x >= noteX && x < noteX + noteW && y >= noteY && y < noteY + noteH) {
      return &note;
    }
  }

  return nullptr;
}

void PianoRollComponent::updateScrollBars() {
  if (project) {
    float totalWidth = project->getAudioData().getDuration() * pixelsPerSecond;
    float totalHeight = (MAX_MIDI_NOTE - MIN_MIDI_NOTE) * pixelsPerSemitone;

    int visibleWidth = getWidth() - pianoKeysWidth - 14;
    int visibleHeight = getHeight() - 14;

    horizontalScrollBar.setRangeLimits(0, totalWidth);
    horizontalScrollBar.setCurrentRange(scrollX, visibleWidth);

    verticalScrollBar.setRangeLimits(0, totalHeight);
    verticalScrollBar.setCurrentRange(scrollY, visibleHeight);
  }
}

void PianoRollComponent::updateBasePitchCacheIfNeeded() {
  if (!project) {
    cachedBasePitch.clear();
    cachedNoteCount = 0;
    cachedTotalFrames = 0;
    return;
  }

  const auto &notes = project->getNotes();
  const auto &audioData = project->getAudioData();
  int totalFrames = static_cast<int>(audioData.f0.size());

  // Check if cache is valid
  size_t currentNoteCount = 0;
  for (const auto &note : notes) {
    if (!note.isRest()) {
      currentNoteCount++;
    }
  }

  // Invalidate cache if notes changed or total frames changed or explicitly
  // invalidated For performance, we only check note count and total frames A
  // more precise check would compare note positions/pitches, but that's
  // expensive
  if (cacheInvalidated || cachedNoteCount != currentNoteCount ||
      cachedTotalFrames != totalFrames || cachedBasePitch.empty()) {
    // Only regenerate if we have notes and frames
    if (currentNoteCount > 0 && totalFrames > 0) {
      // Collect all notes
      std::vector<BasePitchCurve::NoteSegment> noteSegments;
      noteSegments.reserve(currentNoteCount);
      for (const auto &note : notes) {
        if (!note.isRest()) {
          noteSegments.push_back(
              {note.getStartFrame(), note.getEndFrame(), note.getMidiNote()});
        }
      }

      if (!noteSegments.empty()) {
        // Generate smoothed base pitch curve (expensive operation, cached)
        // This is only called when notes change, not on every repaint
        cachedBasePitch =
            BasePitchCurve::generateForNotes(noteSegments, totalFrames);
        cachedNoteCount = currentNoteCount;
        cachedTotalFrames = totalFrames;
        cacheInvalidated = false; // Mark cache as valid
      } else {
        cachedBasePitch.clear();
        cachedNoteCount = 0;
        cachedTotalFrames = 0;
        cacheInvalidated = false; // Mark as processed (even if empty)
      }
    } else {
      cachedBasePitch.clear();
      cachedNoteCount = 0;
      cachedTotalFrames = 0;
      cacheInvalidated = false; // Mark as processed (even if empty)
    }
  }
}

void PianoRollComponent::prepareDragBasePreview() {
  if (!project || !draggedNote)
    return;

  auto &audioData = project->getAudioData();
  if (audioData.basePitch.empty() || audioData.f0.empty())
    return;

  auto range = computeBasePitchPreviewRange(
      project->getNotes(), static_cast<int>(audioData.basePitch.size()),
      [this](const Note &note) { return &note == draggedNote; });

  if (range.startFrame < 0 || range.endFrame <= range.startFrame ||
      range.weights.empty()) {
    return;
  }

  dragPreviewStartFrame = range.startFrame;
  dragPreviewEndFrame = range.endFrame;
  dragPreviewWeights = std::move(range.weights);

  const int count = dragPreviewEndFrame - dragPreviewStartFrame;
  dragBasePitchSnapshot.resize(static_cast<size_t>(count));
  dragF0Snapshot.resize(static_cast<size_t>(count));

  for (int i = 0; i < count; ++i) {
    const int frame = dragPreviewStartFrame + i;
    dragBasePitchSnapshot[static_cast<size_t>(i)] =
        audioData.basePitch[static_cast<size_t>(frame)];
    dragF0Snapshot[static_cast<size_t>(i)] =
        audioData.f0[static_cast<size_t>(frame)];
  }

  lastDragPitchOffset = 0.0f;
}

void PianoRollComponent::applyDragBasePreview(float pitchOffsetSemitones) {
  if (std::abs(pitchOffsetSemitones - lastDragPitchOffset) < 0.0001f)
    return;

  lastDragPitchOffset = pitchOffsetSemitones;
  if (!project || dragPreviewStartFrame < 0 ||
      dragPreviewEndFrame <= dragPreviewStartFrame ||
      dragPreviewWeights.empty() || dragBasePitchSnapshot.empty())
    return;

  auto &audioData = project->getAudioData();
  const int count = dragPreviewEndFrame - dragPreviewStartFrame;

  if (audioData.basePitch.size() < static_cast<size_t>(dragPreviewEndFrame))
    return;

  if (audioData.baseF0.size() < audioData.basePitch.size())
    audioData.baseF0.resize(audioData.basePitch.size(), 0.0f);

  for (int i = 0; i < count; ++i) {
    const int frame = dragPreviewStartFrame + i;
    const float baseMidi =
        dragBasePitchSnapshot[static_cast<size_t>(i)] +
        pitchOffsetSemitones * dragPreviewWeights[static_cast<size_t>(i)];
    audioData.basePitch[static_cast<size_t>(frame)] = baseMidi;
    audioData.baseF0[static_cast<size_t>(frame)] = midiToFreq(baseMidi);

    const float deltaMidi =
        (frame < static_cast<int>(audioData.deltaPitch.size()))
            ? audioData.deltaPitch[static_cast<size_t>(frame)]
            : 0.0f;
    if (frame < static_cast<int>(audioData.voicedMask.size()) &&
        !audioData.voicedMask[static_cast<size_t>(frame)]) {
      audioData.f0[static_cast<size_t>(frame)] = 0.0f;
    } else {
      audioData.f0[static_cast<size_t>(frame)] = midiToFreq(baseMidi + deltaMidi);
    }
  }
}

void PianoRollComponent::restoreDragBasePreview() {
  if (!project || dragPreviewStartFrame < 0 ||
      dragPreviewEndFrame <= dragPreviewStartFrame ||
      dragBasePitchSnapshot.empty() || dragF0Snapshot.empty())
    return;

  auto &audioData = project->getAudioData();
  const int count = dragPreviewEndFrame - dragPreviewStartFrame;
  if (audioData.basePitch.size() < static_cast<size_t>(dragPreviewEndFrame))
    return;

  for (int i = 0; i < count; ++i) {
    const int frame = dragPreviewStartFrame + i;
    audioData.basePitch[static_cast<size_t>(frame)] =
        dragBasePitchSnapshot[static_cast<size_t>(i)];
    if (frame < static_cast<int>(audioData.baseF0.size()))
      audioData.baseF0[static_cast<size_t>(frame)] =
          midiToFreq(audioData.basePitch[static_cast<size_t>(frame)]);
    audioData.f0[static_cast<size_t>(frame)] =
        dragF0Snapshot[static_cast<size_t>(i)];
  }
  lastDragPitchOffset = 0.0f;
}

void PianoRollComponent::reapplyBasePitchForNote(Note *note) {
  if (!note || !project)
    return;

  auto &audioData = project->getAudioData();
  int startFrame = note->getStartFrame();
  int endFrame = note->getEndFrame();
  int f0Size = static_cast<int>(audioData.f0.size());

  // Reapply base + delta from dense curves
  for (int i = startFrame; i < endFrame && i < f0Size; ++i) {
    float base = (i < static_cast<int>(audioData.basePitch.size()))
                     ? audioData.basePitch[static_cast<size_t>(i)]
                     : 0.0f;
    float delta = (i < static_cast<int>(audioData.deltaPitch.size()))
                      ? audioData.deltaPitch[static_cast<size_t>(i)]
                      : 0.0f;
    audioData.f0[i] = midiToFreq(base + delta);
  }

  // Always set F0 dirty range for synthesis (needed for undo/redo to trigger
  // resynthesis)
  int smoothStart = std::max(0, startFrame - 60);
  int smoothEnd = std::min(f0Size, endFrame + 60);
  project->setF0DirtyRange(smoothStart, smoothEnd);

  // Trigger repaint
  repaint();
}

void PianoRollComponent::applyPitchDrawing(float x, float y) {
  if (!project)
    return;

  auto &audioData = project->getAudioData();
  if (audioData.f0.empty())
    return;

  // Convert screen coordinates to time and MIDI
  double time = xToTime(x);
  // Compensate for centering offset used in display
  float midi = yToMidi(y - pixelsPerSemitone * 0.5f);
  // Remove global pitch offset so drawing maps to what is shown on screen
  if (project)
    midi -= project->getGlobalPitchOffset();
  int frameIndex = static_cast<int>(secondsToFrames(static_cast<float>(time)));
  int midiCents = static_cast<int>(std::round(midi * 100.0f));
  applyPitchPoint(frameIndex, midiCents);
}

void PianoRollComponent::commitPitchDrawing() {
  if (drawingEdits.empty())
    return;

  // Calculate the dirty frame range from the changes
  int minFrame = std::numeric_limits<int>::max();
  int maxFrame = std::numeric_limits<int>::min();
  for (const auto &e : drawingEdits) {
    minFrame = std::min(minFrame, e.idx);
    maxFrame = std::max(maxFrame, e.idx);
  }

  // Clear deltaPitch for notes in the edited range so they use the drawn F0
  // values
  if (project && minFrame <= maxFrame) {
    auto &notes = project->getNotes();
    for (auto &note : notes) {
      // Check if note overlaps with edited range
      if (note.getEndFrame() > minFrame && note.getStartFrame() < maxFrame) {
        // Clear deltaPitch so the note will use audioData.f0 instead of
        // computed values
        if (note.hasDeltaPitch()) {
          note.setDeltaPitch(std::vector<float>());
        }
      }
    }
  }

  // Set F0 dirty range in project for incremental synthesis
  if (project && minFrame <= maxFrame) {
    project->setF0DirtyRange(minFrame, maxFrame);
  }

  // Create undo action
  if (undoManager && project) {
    auto &audioData = project->getAudioData();
    auto action = std::make_unique<F0EditAction>(
        &audioData.f0, &audioData.deltaPitch, &audioData.voicedMask,
        drawingEdits, [this](int minFrame, int maxFrame) {
          // Callback to trigger resynthesis after undo/redo
          if (project) {
            project->setF0DirtyRange(minFrame, maxFrame);
            if (onPitchEditFinished)
              onPitchEditFinished();
          }
        });
    undoManager->addAction(std::move(action));
  }

  drawingEdits.clear();
  drawingEditIndexByFrame.clear();
  lastDrawFrame = -1;
  lastDrawValueCents = 0;
  activeDrawCurve = nullptr;
  drawCurves.clear();

  // Trigger synthesis
  if (onPitchEditFinished)
    onPitchEditFinished();
}

void PianoRollComponent::cancelDrawing() {
  if (!isDrawing)
    return;

  // Restore original F0 values from drawing edits
  if (project && !drawingEdits.empty()) {
    auto &audioData = project->getAudioData();
    for (const auto &e : drawingEdits) {
      if (e.idx >= 0 && e.idx < static_cast<int>(audioData.f0.size())) {
        audioData.f0[e.idx] = e.oldF0;
      }
      if (e.idx >= 0 && e.idx < static_cast<int>(audioData.deltaPitch.size())) {
        audioData.deltaPitch[e.idx] = e.oldDelta;
      }
      if (e.idx >= 0 && e.idx < static_cast<int>(audioData.voicedMask.size())) {
        audioData.voicedMask[e.idx] = e.oldVoiced;
      }
    }
  }

  // Clear drawing state
  isDrawing = false;
  drawingEdits.clear();
  drawingEditIndexByFrame.clear();
  lastDrawFrame = -1;
  lastDrawValueCents = 0;
  activeDrawCurve = nullptr;
  drawCurves.clear();

  repaint();
}

void PianoRollComponent::applyPitchPoint(int frameIndex, int midiCents) {
  if (!project)
    return;

  auto &audioData = project->getAudioData();
  if (audioData.f0.empty())
    return;

  const int f0Size = static_cast<int>(audioData.f0.size());
  if (audioData.deltaPitch.size() < audioData.f0.size())
    audioData.deltaPitch.resize(audioData.f0.size(), 0.0f);
  if (audioData.basePitch.size() < audioData.f0.size())
    audioData.basePitch.resize(audioData.f0.size(), 0.0f);
  if (frameIndex < 0 || frameIndex >= f0Size)
    return;

  // Only start a new curve if there's no active curve (first point of drawing)
  if (!activeDrawCurve) {
    startNewPitchCurve(frameIndex, midiCents);
    // First point of the new curve: apply and exit
    auto applyFrameFirst = [&](int idx, int cents) {
      const float newFreq = midiToFreq(static_cast<float>(cents) / 100.0f);
      const float oldF0 = audioData.f0[idx];
      const float oldDelta =
          (idx < static_cast<int>(audioData.deltaPitch.size()))
              ? audioData.deltaPitch[idx]
              : 0.0f;
      const bool oldVoiced =
          (idx < static_cast<int>(audioData.voicedMask.size()))
              ? audioData.voicedMask[idx]
              : false;

      float baseMidi = (idx < static_cast<int>(audioData.basePitch.size()))
                           ? audioData.basePitch[static_cast<size_t>(idx)]
                           : 0.0f;
      float newMidi = static_cast<float>(cents) / 100.0f;
      float newDelta = newMidi - baseMidi;

      auto it = drawingEditIndexByFrame.find(idx);
      if (it == drawingEditIndexByFrame.end()) {
        drawingEditIndexByFrame.emplace(idx, drawingEdits.size());
        drawingEdits.push_back(F0FrameEdit{idx, oldF0, newFreq, oldDelta,
                                           newDelta, oldVoiced, true});
        // Clear deltaPitch for any note containing this frame so changes are
        // visible immediately
        auto &notes = project->getNotes();
        for (auto &note : notes) {
          if (note.getStartFrame() <= idx && note.getEndFrame() > idx &&
              note.hasDeltaPitch()) {
            note.setDeltaPitch(std::vector<float>());
            break;
          }
        }
      } else {
        auto &e = drawingEdits[it->second];
        e.newF0 = newFreq;
        e.newDelta = newDelta;
        e.newVoiced = true;
      }

      audioData.f0[idx] = newFreq;
      if (idx < static_cast<int>(audioData.deltaPitch.size())) {
        audioData.deltaPitch[static_cast<size_t>(idx)] = newDelta;
      }
      if (idx < static_cast<int>(audioData.voicedMask.size()))
        audioData.voicedMask[idx] = true;
    };
    applyFrameFirst(frameIndex, midiCents);
    return;
  }

  auto applyFrame = [&](int idx, int cents) {
    if (idx < 0 || idx >= f0Size)
      return;

    const float newFreq = midiToFreq(static_cast<float>(cents) / 100.0f);
    const float oldF0 = audioData.f0[idx];
    const float oldDelta = (idx < static_cast<int>(audioData.deltaPitch.size()))
                               ? audioData.deltaPitch[idx]
                               : 0.0f;
    const bool oldVoiced = (idx < static_cast<int>(audioData.voicedMask.size()))
                               ? audioData.voicedMask[idx]
                               : false;

    float baseMidi = (idx < static_cast<int>(audioData.basePitch.size()))
                         ? audioData.basePitch[static_cast<size_t>(idx)]
                         : 0.0f;
    float newMidi = static_cast<float>(cents) / 100.0f;
    float newDelta = newMidi - baseMidi;

    auto it = drawingEditIndexByFrame.find(idx);
    if (it == drawingEditIndexByFrame.end()) {
      drawingEditIndexByFrame.emplace(idx, drawingEdits.size());
      drawingEdits.push_back(F0FrameEdit{idx, oldF0, newFreq, oldDelta,
                                         newDelta, oldVoiced, true});

      // Clear deltaPitch for any note containing this frame so changes are
      // visible immediately
      auto &notes = project->getNotes();
      for (auto &note : notes) {
        if (note.getStartFrame() <= idx && note.getEndFrame() > idx &&
            note.hasDeltaPitch()) {
          note.setDeltaPitch(std::vector<float>());
          break;
        }
      }
    } else {
      auto &e = drawingEdits[it->second];
      e.newF0 = newFreq;
      e.newDelta = newDelta;
      e.newVoiced = true;
    }

    audioData.f0[idx] = newFreq;
    if (idx < static_cast<int>(audioData.deltaPitch.size())) {
      audioData.deltaPitch[static_cast<size_t>(idx)] = newDelta;
    }
    if (idx < static_cast<int>(audioData.voicedMask.size()))
      audioData.voicedMask[idx] = true;
  };

  auto appendValue = [&](int idx, int cents) {
    if (!activeDrawCurve)
      return;

    const int curveStart = activeDrawCurve->localStart();
    auto &vals = activeDrawCurve->mutableValues();

    // Handle backward drawing: prepend values if idx < curveStart
    if (idx < curveStart) {
      const int prependCount = curveStart - idx;
      std::vector<int> newVals(static_cast<size_t>(prependCount), cents);
      newVals.insert(newVals.end(), vals.begin(), vals.end());
      activeDrawCurve->setValues(std::move(newVals));
      activeDrawCurve->setLocalStart(idx);
      return;
    }

    const int offset = idx - curveStart;
    if (offset < static_cast<int>(vals.size())) {
      vals[static_cast<std::size_t>(offset)] = cents;
      return;
    }

    while (static_cast<int>(vals.size()) < offset) {
      int fill = vals.empty() ? cents : vals.back();
      vals.push_back(fill);
    }
    vals.push_back(cents);
  };

  if (lastDrawFrame < 0) {
    appendValue(frameIndex, midiCents);
    applyFrame(frameIndex, midiCents);
  } else {
    int start = lastDrawFrame;
    int end = frameIndex;
    int startVal = lastDrawValueCents;
    int endVal = midiCents;

    if (start == end) {
      appendValue(frameIndex, midiCents);
      applyFrame(frameIndex, midiCents);
    } else {
      int step = (end > start) ? 1 : -1;
      int length = std::abs(end - start);
      for (int i = 0; i <= length; ++i) {
        int idx = start + i * step;
        float t = length == 0
                      ? 0.0f
                      : static_cast<float>(i) / static_cast<float>(length);
        float v = juce::jmap(t, 0.0f, 1.0f, static_cast<float>(startVal),
                             static_cast<float>(endVal));
        int cents = static_cast<int>(std::round(v));
        appendValue(idx, cents);
        applyFrame(idx, cents);
      }
    }
  }

  lastDrawFrame = frameIndex;
  lastDrawValueCents = midiCents;
}

void PianoRollComponent::startNewPitchCurve(int frameIndex, int midiCents) {
  drawCurves.push_back(std::make_unique<DrawCurve>(frameIndex, 1));
  activeDrawCurve = drawCurves.back().get();
  activeDrawCurve->appendValue(midiCents);
  lastDrawFrame = frameIndex;
  lastDrawValueCents = midiCents;
}

void PianoRollComponent::drawSelectionRect(juce::Graphics &g) {
  if (!boxSelector || !boxSelector->isSelecting())
    return;

  auto rect = boxSelector->getSelectionRect();

  // Draw semi-transparent fill
  g.setColour(juce::Colour::fromRGBA(0x30, 0x80, 0xFF, 0x40));
  g.fillRect(rect);

  // Draw border
  g.setColour(juce::Colour::fromRGBA(0x30, 0x80, 0xFF, 0xC0));
  g.drawRect(rect, 1.0f);
}
