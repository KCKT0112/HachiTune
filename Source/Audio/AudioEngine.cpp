#include "AudioEngine.h"

AudioEngine::AudioEngine()
{
}

AudioEngine::~AudioEngine()
{
    shutdownAudio();
}

void AudioEngine::initializeAudio()
{
    // Initialize audio device
    auto result = deviceManager.initialiseWithDefaultDevices(0, 2);  // No input, stereo output
    
    if (result.isNotEmpty())
    {
        DBG("Audio device initialization error: " + result);
    }
    else
    {
        DBG("Audio device initialized successfully");
        auto* device = deviceManager.getCurrentAudioDevice();
        if (device)
        {
            DBG("Device name: " + device->getName());
            DBG("Sample rate: " + juce::String(device->getCurrentSampleRate()));
            DBG("Buffer size: " + juce::String(device->getCurrentBufferSizeSamples()));
        }
    }
    
    deviceManager.addAudioCallback(&audioSourcePlayer);
    audioSourcePlayer.setSource(this);
}

void AudioEngine::shutdownAudio()
{
    audioSourcePlayer.setSource(nullptr);
    deviceManager.removeAudioCallback(&audioSourcePlayer);
    deviceManager.closeAudioDevice();
}

void AudioEngine::prepareToPlay(int samplesPerBlockExpected, double sampleRate)
{
    currentSampleRate = sampleRate;
    playbackRatio = static_cast<double>(waveformSampleRate) / sampleRate;
    interpolator.reset();
    fractionalPosition = 0.0;
    
    DBG("AudioEngine::prepareToPlay - Device sample rate: " + juce::String(sampleRate) + 
        " Hz, Waveform sample rate: " + juce::String(waveformSampleRate) + 
        " Hz, Playback ratio: " + juce::String(playbackRatio));
}

void AudioEngine::releaseResources()
{
}

void AudioEngine::getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill)
{
    if (!playing || currentWaveform.getNumSamples() == 0)
    {
        bufferToFill.clearActiveBufferRegion();
        return;
    }
    
    auto* outputBuffer = bufferToFill.buffer;
    auto numOutputSamples = bufferToFill.numSamples;
    auto startSample = bufferToFill.startSample;
    
    int64_t pos = currentPosition.load();
    int64_t waveformLength = currentWaveform.getNumSamples();
    
    if (pos >= waveformLength)
    {
        bufferToFill.clearActiveBufferRegion();
        playing = false;
        
        // Schedule callback on message thread
        if (finishCallback)
        {
            juce::MessageManager::callAsync([this]() {
                if (finishCallback)
                    finishCallback();
            });
        }
        return;
    }
    
    // Use interpolator for sample rate conversion
    const float* inputData = currentWaveform.getReadPointer(0);
    float* outputData = outputBuffer->getWritePointer(0, startSample);
    
    // Calculate how many input samples we need
    int inputSamplesAvailable = static_cast<int>(waveformLength - pos);
    
    // Process with interpolation
    int samplesUsed = interpolator.process(
        playbackRatio,
        inputData + pos,
        outputData,
        numOutputSamples,
        inputSamplesAvailable,
        0  // No wrap
    );
    
    // Update position
    int64_t newPos = pos + samplesUsed;
    currentPosition.store(newPos);
    
    // Copy to other channels (if stereo output)
    for (int ch = 1; ch < outputBuffer->getNumChannels(); ++ch)
    {
        outputBuffer->copyFrom(ch, startSample, outputBuffer->getReadPointer(0, startSample), numOutputSamples);
    }
    
    // Check if we've reached the end
    if (newPos >= waveformLength)
    {
        playing = false;
        if (finishCallback)
        {
            juce::MessageManager::callAsync([this]() {
                if (finishCallback)
                    finishCallback();
            });
        }
    }
    
    // Update position callback
    if (positionCallback)
    {
        double posSeconds = static_cast<double>(currentPosition.load()) / waveformSampleRate;
        juce::MessageManager::callAsync([this, posSeconds]() {
            if (positionCallback)
                positionCallback(posSeconds);
        });
    }
}

void AudioEngine::changeListenerCallback(juce::ChangeBroadcaster* source)
{
}

void AudioEngine::loadWaveform(const juce::AudioBuffer<float>& buffer, int sampleRate)
{
    // CRITICAL: Validate this pointer before accessing any member variables
    // This helps catch cases where the object has been destroyed
    if (this == nullptr) {
        DBG("AudioEngine::loadWaveform - ERROR: this pointer is null!");
        jassertfalse; // This should never happen in valid code
        return;
    }
    
    DBG("AudioEngine::loadWaveform called - this=" << juce::String::toHexString(reinterpret_cast<uintptr_t>(this)));
    
    stop();
    currentWaveform = buffer;
    waveformSampleRate = sampleRate;
    currentPosition.store(0);
    
    // Update playback ratio for sample rate conversion
    if (currentSampleRate > 0)
        playbackRatio = static_cast<double>(waveformSampleRate) / currentSampleRate;
    else
        playbackRatio = 1.0;
    
    interpolator.reset();
    fractionalPosition = 0.0;
    
    DBG("Loaded waveform: " + juce::String(buffer.getNumSamples()) + " samples at " + 
        juce::String(sampleRate) + " Hz, playback ratio: " + juce::String(playbackRatio));
}

void AudioEngine::play()
{
    if (currentWaveform.getNumSamples() == 0)
    {
        DBG("Cannot play: no waveform loaded");
        return;
    }
    
    DBG("Starting playback from position: " + juce::String(currentPosition.load()));
    playing = true;
}

void AudioEngine::pause()
{
    playing = false;
}

void AudioEngine::stop()
{
    // CRITICAL: Validate this pointer before accessing any member variables
    // This helps catch cases where the object has been destroyed
    if (this == nullptr) {
        DBG("AudioEngine::stop - ERROR: this pointer is null!");
        jassertfalse; // This should never happen in valid code
        return;
    }
    
    DBG("AudioEngine::stop called - this=" << juce::String::toHexString(reinterpret_cast<uintptr_t>(this)));
    
    playing = false;
    currentPosition.store(0);
    interpolator.reset();
    fractionalPosition = 0.0;
}

void AudioEngine::seek(double timeSeconds)
{
    int64_t newPos = static_cast<int64_t>(timeSeconds * waveformSampleRate);
    newPos = juce::jlimit<int64_t>(0, currentWaveform.getNumSamples(), newPos);
    currentPosition.store(newPos);
    interpolator.reset();
    fractionalPosition = 0.0;
}

double AudioEngine::getPosition() const
{
    return static_cast<double>(currentPosition.load()) / waveformSampleRate;
}

double AudioEngine::getDuration() const
{
    if (currentWaveform.getNumSamples() == 0)
        return 0.0;
    return static_cast<double>(currentWaveform.getNumSamples()) / waveformSampleRate;
}
