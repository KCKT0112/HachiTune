#pragma once

#include "../JuceHeader.h"

/**
 * Simple file logger for debugging.
 * Logs to %APPDATA%/HachiTune/debug.log
 */
class AppLogger
{
public:
    static void log(const juce::String& message)
    {
        auto logFile = getLogFile();
        auto timestamp = juce::Time::getCurrentTime().toString(true, true, true, true);
        logFile.appendText("[" + timestamp + "] " + message + "\n");
        DBG(message);
    }

    static void clear()
    {
        auto logFile = getLogFile();
        logFile.deleteFile();
    }

    static juce::File getLogFile()
    {
        auto dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                       .getChildFile("HachiTune");
        dir.createDirectory();
        return dir.getChildFile("debug.log");
    }
};

#define LOG(msg) AppLogger::log(msg)
