#include "PitchToolHandles.h"
#include <algorithm>
#include <limits>

PitchToolHandles::PitchToolHandles() {
  // Initialize (currently empty, but reserve space)
  handles.reserve(24);  // Typical max handles for multi-note selection
}

void PitchToolHandles::updateHandles(const std::vector<Note*>& selectedNotes,
                                     const CoordinateMapper& mapper,
                                     bool append) {
  if (!append)
    handles.clear();

  if (selectedNotes.empty())
    return;

  // Compute bounding box in musical coordinates
  int minStartFrame = std::numeric_limits<int>::max();
  int maxEndFrame = std::numeric_limits<int>::min();
  float minMidi = std::numeric_limits<float>::max();
  float maxMidi = std::numeric_limits<float>::min();

  for (const auto* note : selectedNotes) {
    if (!note) continue;
    minStartFrame = std::min(minStartFrame, note->getStartFrame());
    maxEndFrame = std::max(maxEndFrame, note->getEndFrame());
    minMidi = std::min(minMidi, note->getAdjustedMidiNote());
    maxMidi = std::max(maxMidi, note->getAdjustedMidiNote());
  }

  // Convert to WORLD coordinates (not screen - mouse events are in world space)
  // Start/End times
  float startSec = mapper.framesToSeconds(minStartFrame);
  float endSec = mapper.framesToSeconds(maxEndFrame);
  
  float leftX = mapper.timeToX(startSec);
  float rightX = mapper.timeToX(endSec);

  // Pitch range
  // Note: High pitch = Low Y value (Top of screen)
  // Low pitch = High Y value (Bottom of screen)
  // We want the visual box.
  // Top Y corresponds to the highest MIDI note.
  float topY = mapper.midiToY(maxMidi);
  
  // Bottom Y corresponds to the lowest MIDI note + 1 semitone height (since note has height)
  // midiToY(minMidi) gives the top of the lowest note.
  // midiToY(minMidi) + pixelsPerSemitone gives the bottom of the lowest note.
  float bottomY = mapper.midiToY(minMidi) + mapper.getPixelsPerSemitone();

  // Ensure valid dimensions
  if (rightX < leftX) std::swap(leftX, rightX);
  if (bottomY < topY) std::swap(topY, bottomY);

  float centerX = (leftX + rightX) * 0.5f;
  float centerY = (topY + bottomY) * 0.5f;
  Note* primaryNote = selectedNotes.size() == 1 ? selectedNotes.front() : nullptr;

  // Add Handles
  // 1. Smooth Left: Left edge, vertically centered
  addHandle(HandleType::SmoothLeft, leftX, centerY, primaryNote);

  // 2. Smooth Right: Right edge, vertically centered
  addHandle(HandleType::SmoothRight, rightX, centerY, primaryNote);

  // 3. Reduce Variance: Top edge, horizontally centered
  addHandle(HandleType::ReduceVariance, centerX, topY, primaryNote);

  // 4. Tilt Left: Top-Left corner
  addHandle(HandleType::TiltLeft, leftX, topY, primaryNote);

  // 5. Tilt Right: Top-Right corner
  addHandle(HandleType::TiltRight, rightX, topY, primaryNote);

  // 6. High-pass filter: Bottom-left corner
  addHandle(HandleType::HighPassLeft, leftX, bottomY, primaryNote);

  // 7. Low-pass filter: Bottom-right corner
  addHandle(HandleType::LowPassRight, rightX, bottomY, primaryNote);
}

void PitchToolHandles::draw(juce::Graphics& g) const {
  for (int i = 0; i < static_cast<int>(handles.size()); ++i) {
    const auto& handle = handles[i];
    bool isHovered = (i == hoveredHandleIndex);

    auto drawBounds = handle.bounds;
    if (isHovered)
      drawBounds = drawBounds.expanded(1.5f);

    g.setColour(juce::Colours::black.withAlpha(isHovered ? 0.26f : 0.18f));
    g.fillEllipse(drawBounds.translated(0.0f, 1.5f));

    g.setColour(handle.color.withMultipliedSaturation(isHovered ? 1.1f : 1.0f));
    g.fillEllipse(drawBounds);

    g.setColour(juce::Colours::white.withAlpha(isHovered ? 0.95f : 0.9f));
    g.drawEllipse(drawBounds, 1.2f);

    const auto iconBounds = drawBounds.reduced(drawBounds.getWidth() * 0.26f);
    g.setColour(juce::Colours::white.withAlpha(0.95f));

    switch (handle.type) {
      case HandleType::TiltLeft:
      {
        g.drawLine(iconBounds.getRight(), iconBounds.getY(),
                   iconBounds.getX(), iconBounds.getBottom(), 1.8f);
        g.drawLine(iconBounds.getX(), iconBounds.getBottom(),
                   iconBounds.getX() + 2.4f, iconBounds.getBottom() - 3.0f, 1.8f);
        g.drawLine(iconBounds.getX(), iconBounds.getBottom(),
                   iconBounds.getX() + 3.2f, iconBounds.getBottom() + 0.2f, 1.8f);
        break;
      }
      case HandleType::TiltRight:
      {
        g.drawLine(iconBounds.getX(), iconBounds.getY(),
                   iconBounds.getRight(), iconBounds.getBottom(), 1.8f);
        g.drawLine(iconBounds.getRight(), iconBounds.getBottom(),
                   iconBounds.getRight() - 2.4f, iconBounds.getBottom() - 3.0f, 1.8f);
        g.drawLine(iconBounds.getRight(), iconBounds.getBottom(),
                   iconBounds.getRight() - 3.2f, iconBounds.getBottom() + 0.2f, 1.8f);
        break;
      }
      case HandleType::ReduceVariance:
      {
        const float cx = iconBounds.getCentreX();
        const float cy = iconBounds.getCentreY();
        g.drawLine(iconBounds.getX(), cy, iconBounds.getRight(), cy, 1.8f);
        g.drawLine(cx, iconBounds.getY() + 1.0f, cx, iconBounds.getBottom() - 1.0f,
                   1.8f);
        break;
      }
      case HandleType::SmoothLeft:
      case HandleType::SmoothRight:
      {
        juce::Path arc;
        if (handle.type == HandleType::SmoothLeft)
          arc.startNewSubPath(iconBounds.getRight(), iconBounds.getY() + 1.0f);
        else
          arc.startNewSubPath(iconBounds.getX(), iconBounds.getY() + 1.0f);

        const float controlX = handle.type == HandleType::SmoothLeft
                                   ? iconBounds.getX()
                                   : iconBounds.getRight();
        const float endX = handle.type == HandleType::SmoothLeft
                               ? iconBounds.getX()
                               : iconBounds.getRight();
        arc.quadraticTo(controlX, iconBounds.getCentreY(),
                        endX, iconBounds.getBottom() - 1.0f);
        g.strokePath(arc, juce::PathStrokeType(1.8f));
        break;
      }
      case HandleType::HighPassLeft:
      {
        const float lowY = iconBounds.getBottom() - 1.0f;
        const float highY = iconBounds.getY() + 1.0f;
        const float stepX = iconBounds.getX() + iconBounds.getWidth() * 0.42f;
        g.drawLine(iconBounds.getX(), lowY, stepX, lowY, 1.8f);
        g.drawLine(stepX, lowY, stepX, highY, 1.8f);
        g.drawLine(stepX, highY, iconBounds.getRight(), highY, 1.8f);
        break;
      }
      case HandleType::LowPassRight:
      {
        const float highY = iconBounds.getY() + 1.0f;
        const float lowY = iconBounds.getBottom() - 1.0f;
        const float stepX = iconBounds.getX() + iconBounds.getWidth() * 0.58f;
        g.drawLine(iconBounds.getX(), highY, stepX, highY, 1.8f);
        g.drawLine(stepX, highY, stepX, lowY, 1.8f);
        g.drawLine(stepX, lowY, iconBounds.getRight(), lowY, 1.8f);
        break;
      }
      case HandleType::None:
      default:
        break;
    }
  }
}

int PitchToolHandles::hitTest(float worldX, float worldY, float tolerance) const {
  int bestIndex = -1;
  float bestDistance = std::numeric_limits<float>::max();

  for (int i = 0; i < static_cast<int>(handles.size()); ++i) {
    auto center = handles[i].bounds.getCentre();
    float distance = center.getDistanceFrom(juce::Point<float>(worldX, worldY));

    if (distance <= std::max(tolerance, handleSize * 0.9f)) {
      if (distance < bestDistance - 0.001f ||
          (std::abs(distance - bestDistance) < 0.001f && i > bestIndex)) {
        bestDistance = distance;
        bestIndex = i;
      }
    }
  }

  return bestIndex;
}

void PitchToolHandles::addHandle(HandleType type, float worldX, float worldY, Note* note) {
  Handle h;
  h.type = type;
  h.note = note;
  h.color = getColorForType(type);
  
  // Center the handle bounds on the coordinate (now in world space)
  float halfSize = handleSize * 0.5f;
  h.bounds = juce::Rectangle<float>(worldX - halfSize, worldY - halfSize, 
                                   handleSize, handleSize);
  
  handles.push_back(h);
}

juce::Colour PitchToolHandles::getColorForType(HandleType type) const {
  switch (type) {
    case HandleType::TiltLeft:
    case HandleType::TiltRight:
      return juce::Colours::orange;
      
    case HandleType::ReduceVariance:
      return juce::Colours::mediumpurple; // "Reduce" implies constraint -> purple/magenta
      
    case HandleType::SmoothLeft:
    case HandleType::SmoothRight:
      return juce::Colours::cyan; // "Smooth" implies liquid/soft -> cyan/blue

    case HandleType::HighPassLeft:
      return juce::Colours::yellowgreen;

    case HandleType::LowPassRight:
      return juce::Colours::deepskyblue;
      
    default:
      return juce::Colours::white;
  }
}
