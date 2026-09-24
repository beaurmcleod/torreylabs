#pragma once

#include "Parameters.h"
#include "dsp/TetherEngine.h"

#include <juce_audio_processors/juce_audio_processors.h>

/** Tether: the main input is the layer, the sidechain ("Guide") is the sound
    whose pitch, level and movement the layer should follow. */
class TetherAudioProcessor final : public juce::AudioProcessor
{
public:
    TetherAudioProcessor();
    ~TetherAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    using AudioProcessor::processBlockBypassed;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                        { return true; }

    const juce::String getName() const override            { return "Tether"; }
    bool acceptsMidi() const override                      { return false; }
    bool producesMidi() const override                     { return false; }
    bool isMidiEffect() const override                     { return false; }
    double getTailLengthSeconds() const override           { return 0.3; }

    int getNumPrograms() override                          { return 1; }
    int getCurrentProgram() override                       { return 0; }
    void setCurrentProgram (int) override                  {}
    const juce::String getProgramName (int) override       { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getState() noexcept     { return state; }
    tether::TetherEngine& getEngine() noexcept                  { return engine; }

    /** True while the host is feeding the Guide (sidechain) bus. */
    bool isGuideRouted() const noexcept                         { return guideRouted.load (std::memory_order_relaxed); }

    double getLatencyMs() const noexcept
    {
        const double rate = preparedRate.load (std::memory_order_relaxed);
        return rate > 0.0 ? 1000.0 * getLatencySamples() / rate : 0.0;
    }

private:
    static BusesProperties makeBusesProperties();
    void run (juce::AudioBuffer<float>&, bool bypassed);

    juce::AudioProcessorValueTreeState state;
    ParameterReader params;
    tether::TetherEngine engine;
    std::atomic<bool> guideRouted { false };
    std::atomic<double> preparedRate { 0.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TetherAudioProcessor)
};
