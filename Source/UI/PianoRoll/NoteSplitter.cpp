#include "NoteSplitter.h"
#include "../../Utils/Constants.h"

Note* NoteSplitter::findNoteAt(float x, float y) {
    if (!project || !coordMapper)
        return nullptr;

    float pixelsPerSecond = coordMapper->getPixelsPerSecond();
    float pixelsPerSemitone = coordMapper->getPixelsPerSemitone();

    for (auto& note : project->getNotes()) {
        if (note.isRest())
            continue;

        float noteX = framesToSeconds(note.getStartFrame()) * pixelsPerSecond;
        float noteW = framesToSeconds(note.getDurationFrames()) * pixelsPerSecond;
        float noteY = coordMapper->midiToY(note.getAdjustedMidiNote());
        float noteH = pixelsPerSemitone;

        if (x >= noteX && x < noteX + noteW && y >= noteY && y < noteY + noteH) {
            return &note;
        }
    }

    return nullptr;
}

bool NoteSplitter::splitNoteAtFrame(Note* note, int splitFrame) {
    if (!note || !project)
        return false;

    int startFrame = note->getStartFrame();
    int endFrame = note->getEndFrame();

    // Ensure split point is within note bounds (with margin)
    if (splitFrame <= startFrame + 5 || splitFrame >= endFrame - 5)
        return false;

    // Store original note data for undo
    Note originalNote = *note;

    // Create the second note (right part)
    Note secondNote;
    secondNote.setStartFrame(splitFrame);
    secondNote.setEndFrame(endFrame);
    secondNote.setMidiNote(note->getMidiNote());
    secondNote.setLyric(note->getLyric());
    secondNote.setPitchOffset(0.0f);

    // Modify the first note (left part)
    note->setEndFrame(splitFrame);

    // Add the second note to project
    project->addNote(secondNote);

    // Create undo action
    if (undoManager) {
        auto action = std::make_unique<NoteSplitAction>(
            project, originalNote, *note, secondNote,
            [this]() {
                if (onNoteSplit)
                    onNoteSplit();
            });
        undoManager->addAction(std::move(action));
    }

    if (onNoteSplit)
        onNoteSplit();

    return true;
}

bool NoteSplitter::splitNoteAtX(Note* note, float x) {
    if (!note || !coordMapper)
        return false;

    // Convert X coordinate to frame
    float pixelsPerSecond = coordMapper->getPixelsPerSecond();
    double time = x / pixelsPerSecond;
    int frame = static_cast<int>(time * SAMPLE_RATE / HOP_SIZE);

    return splitNoteAtFrame(note, frame);
}
