#pragma once

#include "../JuceHeader.h"
#include "../Models/Note.h"
#include "../Models/Project.h"
#include <vector>
#include <memory>
#include <functional>
#include <limits>

/**
 * Base class for undoable actions.
 */
class UndoableAction
{
public:
    virtual ~UndoableAction() = default;
    virtual void undo() = 0;
    virtual void redo() = 0;
    virtual juce::String getName() const = 0;
};

/**
 * Action for changing a note's pitch offset.
 */
class PitchOffsetAction : public UndoableAction
{
public:
    PitchOffsetAction(Note* note, float oldOffset, float newOffset)
        : note(note), oldOffset(oldOffset), newOffset(newOffset) {}
    
    void undo() override { if (note) note->setPitchOffset(oldOffset); }
    void redo() override { if (note) note->setPitchOffset(newOffset); }
    juce::String getName() const override { return "Change Pitch Offset"; }
    
private:
    Note* note;
    float oldOffset;
    float newOffset;
};

/**
 * Action for changing multiple F0 values (hand-drawing).
 */
struct F0FrameEdit
{
    int idx = -1;
    float oldF0 = 0.0f;
    float newF0 = 0.0f;
    float oldDelta = 0.0f;
    float newDelta = 0.0f;
    bool oldVoiced = false;
    bool newVoiced = false;
};

class F0EditAction : public UndoableAction
{
public:
    F0EditAction(std::vector<float>* f0Array,
                 std::vector<float>* deltaPitchArray,
                 std::vector<bool>* voicedMask,
                 std::vector<F0FrameEdit> edits,
                 std::function<void(int, int)> onF0Changed = nullptr)
        : f0Array(f0Array), deltaPitchArray(deltaPitchArray), voicedMask(voicedMask), edits(std::move(edits)), onF0Changed(onF0Changed) {}

    void undo() override
    {
        if (!f0Array) return;
        int minIdx = std::numeric_limits<int>::max();
        int maxIdx = std::numeric_limits<int>::min();
        for (const auto& e : edits)
        {
            if (e.idx >= 0 && e.idx < static_cast<int>(f0Array->size())) {
                (*f0Array)[e.idx] = e.oldF0;
                minIdx = std::min(minIdx, e.idx);
                maxIdx = std::max(maxIdx, e.idx);
            }
            if (deltaPitchArray && e.idx >= 0 && e.idx < static_cast<int>(deltaPitchArray->size()))
                (*deltaPitchArray)[e.idx] = e.oldDelta;
            if (voicedMask && e.idx >= 0 && e.idx < static_cast<int>(voicedMask->size()))
                (*voicedMask)[e.idx] = e.oldVoiced;
        }
        if (onF0Changed && minIdx <= maxIdx)
            onF0Changed(minIdx, maxIdx);
    }

    void redo() override
    {
        if (!f0Array) return;
        int minIdx = std::numeric_limits<int>::max();
        int maxIdx = std::numeric_limits<int>::min();
        for (const auto& e : edits)
        {
            if (e.idx >= 0 && e.idx < static_cast<int>(f0Array->size())) {
                (*f0Array)[e.idx] = e.newF0;
                minIdx = std::min(minIdx, e.idx);
                maxIdx = std::max(maxIdx, e.idx);
            }
            if (deltaPitchArray && e.idx >= 0 && e.idx < static_cast<int>(deltaPitchArray->size()))
                (*deltaPitchArray)[e.idx] = e.newDelta;
            if (voicedMask && e.idx >= 0 && e.idx < static_cast<int>(voicedMask->size()))
                (*voicedMask)[e.idx] = e.newVoiced;
        }
        if (onF0Changed && minIdx <= maxIdx)
            onF0Changed(minIdx, maxIdx);
    }

    juce::String getName() const override { return "Edit Pitch Curve"; }

private:
    std::vector<float>* f0Array;
    std::vector<float>* deltaPitchArray;
    std::vector<bool>* voicedMask;
    std::vector<F0FrameEdit> edits;
    std::function<void(int, int)> onF0Changed;  // Callback with (minFrame, maxFrame) to trigger resynthesis
};

/**
 * Action for dragging a note to change pitch (MIDI note + F0 values).
 */
class NotePitchDragAction : public UndoableAction
{
public:
    NotePitchDragAction(Note* note, std::vector<float>* f0Array,
                        float oldMidi, float newMidi,
                        std::vector<F0FrameEdit> f0Edits,
                        std::function<void(Note*)> onNoteChanged = nullptr)
        : note(note), f0Array(f0Array), oldMidi(oldMidi), newMidi(newMidi),
          f0Edits(std::move(f0Edits)), onNoteChanged(onNoteChanged) {}

    void undo() override
    {
        if (note) {
            note->setMidiNote(oldMidi);
            note->markDirty();
        }
        if (f0Array) {
            for (const auto& e : f0Edits) {
                if (e.idx >= 0 && e.idx < static_cast<int>(f0Array->size()))
                    (*f0Array)[e.idx] = e.oldF0;
            }
        }
        // Notify that note changed, so base pitch can be recalculated
        if (onNoteChanged && note) {
            onNoteChanged(note);
        }
    }

    void redo() override
    {
        if (note) {
            note->setMidiNote(newMidi);
            note->markDirty();
        }
        if (f0Array) {
            for (const auto& e : f0Edits) {
                if (e.idx >= 0 && e.idx < static_cast<int>(f0Array->size()))
                    (*f0Array)[e.idx] = e.newF0;
            }
        }
        // Notify that note changed, so base pitch can be recalculated
        if (onNoteChanged && note) {
            onNoteChanged(note);
        }
    }

    juce::String getName() const override { return "Drag Note Pitch"; }

private:
    Note* note;
    std::vector<float>* f0Array;
    float oldMidi;
    float newMidi;
    std::vector<F0FrameEdit> f0Edits;
    std::function<void(Note*)> onNoteChanged;  // Callback when note MIDI changes
};

/**
 * Action for dragging multiple notes to change pitch.
 */
class MultiNotePitchDragAction : public UndoableAction
{
public:
    MultiNotePitchDragAction(std::vector<Note*> notes, std::vector<float>* f0Array,
                             std::vector<float> oldMidis, float pitchDelta,
                             std::vector<F0FrameEdit> f0Edits,
                             std::function<void(const std::vector<Note*>&)> onNotesChanged = nullptr)
        : notes(std::move(notes)), f0Array(f0Array), oldMidis(std::move(oldMidis)),
          pitchDelta(pitchDelta), f0Edits(std::move(f0Edits)), onNotesChanged(onNotesChanged) {}

    void undo() override
    {
        for (size_t i = 0; i < notes.size() && i < oldMidis.size(); ++i) {
            if (notes[i]) {
                notes[i]->setMidiNote(oldMidis[i]);
                notes[i]->markDirty();
            }
        }
        if (f0Array) {
            for (const auto& e : f0Edits) {
                if (e.idx >= 0 && e.idx < static_cast<int>(f0Array->size()))
                    (*f0Array)[e.idx] = e.oldF0;
            }
        }
        if (onNotesChanged)
            onNotesChanged(notes);
    }

    void redo() override
    {
        for (size_t i = 0; i < notes.size() && i < oldMidis.size(); ++i) {
            if (notes[i]) {
                notes[i]->setMidiNote(oldMidis[i] + pitchDelta);
                notes[i]->markDirty();
            }
        }
        if (f0Array) {
            for (const auto& e : f0Edits) {
                if (e.idx >= 0 && e.idx < static_cast<int>(f0Array->size()))
                    (*f0Array)[e.idx] = e.newF0;
            }
        }
        if (onNotesChanged)
            onNotesChanged(notes);
    }

    juce::String getName() const override { return "Drag Multiple Notes"; }

private:
    std::vector<Note*> notes;
    std::vector<float>* f0Array;
    std::vector<float> oldMidis;
    float pitchDelta;
    std::vector<F0FrameEdit> f0Edits;
    std::function<void(const std::vector<Note*>&)> onNotesChanged;
};

/**
 * Action for splitting a note into two.
 */
class NoteSplitAction : public UndoableAction
{
public:
    NoteSplitAction(Project* proj, const Note& original, const Note& firstPart, const Note& secondPart,
                    std::function<void()> onChanged = nullptr)
        : project(proj), originalNote(original), firstNote(firstPart), secondNote(secondPart),
          onChanged(onChanged) {}

    void undo() override
    {
        if (!project) return;
        // Remove the second note and restore original
        project->removeNoteByStartFrame(secondNote.getStartFrame());
        // Find and restore the first note to original state
        for (auto& note : project->getNotes()) {
            if (note.getStartFrame() == firstNote.getStartFrame()) {
                note = originalNote;
                break;
            }
        }
        if (onChanged) onChanged();
    }

    void redo() override
    {
        if (!project) return;
        // Split again: modify first note and add second
        for (auto& note : project->getNotes()) {
            if (note.getStartFrame() == originalNote.getStartFrame()) {
                note = firstNote;
                break;
            }
        }
        project->addNote(secondNote);
        if (onChanged) onChanged();
    }

    juce::String getName() const override { return "Split Note"; }

private:
    Project* project;
    Note originalNote;
    Note firstNote;
    Note secondNote;
    std::function<void()> onChanged;
};

/**
 * Simple undo manager for the pitch editor.
 */
class PitchUndoManager
{
public:
    PitchUndoManager(size_t maxHistory = 100) : maxHistory(maxHistory) {}
    
    void addAction(std::unique_ptr<UndoableAction> action)
    {
        // Clear redo stack when new action is added
        redoStack.clear();
        
        undoStack.push_back(std::move(action));
        
        // Limit history size
        while (undoStack.size() > maxHistory)
        {
            undoStack.erase(undoStack.begin());
        }
        
        if (onHistoryChanged)
            onHistoryChanged();
    }
    
    bool canUndo() const { return !undoStack.empty(); }
    bool canRedo() const { return !redoStack.empty(); }
    
    void undo()
    {
        if (undoStack.empty()) return;
        
        auto action = std::move(undoStack.back());
        undoStack.pop_back();
        
        action->undo();
        redoStack.push_back(std::move(action));
        
        if (onHistoryChanged)
            onHistoryChanged();
    }
    
    void redo()
    {
        if (redoStack.empty()) return;
        
        auto action = std::move(redoStack.back());
        redoStack.pop_back();
        
        action->redo();
        undoStack.push_back(std::move(action));
        
        if (onHistoryChanged)
            onHistoryChanged();
    }
    
    void clear()
    {
        undoStack.clear();
        redoStack.clear();
        
        if (onHistoryChanged)
            onHistoryChanged();
    }
    
    juce::String getUndoName() const
    {
        return undoStack.empty() ? "" : undoStack.back()->getName();
    }
    
    juce::String getRedoName() const
    {
        return redoStack.empty() ? "" : redoStack.back()->getName();
    }
    
    std::function<void()> onHistoryChanged;
    
private:
    std::vector<std::unique_ptr<UndoableAction>> undoStack;
    std::vector<std::unique_ptr<UndoableAction>> redoStack;
    size_t maxHistory;
};
