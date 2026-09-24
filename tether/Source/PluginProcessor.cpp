#include "PluginProcessor.h"
#include "PluginEditor.h"

juce::AudioProcessor::BusesProperties TetherAudioProcessor::makeBusesProperties()
{
    return BusesProperties()
        .withInput ("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
        .withInput ("Guide", juce::AudioChannelSet::stereo(), true);
}

TetherAudioProcessor::TetherAudioProcessor()
    : AudioProcessor (makeBusesProperties()),
      state (*this, nullptr, "TetherState", createParameterLayout()),
      params (state)
{
}

void TetherAudioProcessor::prepareToPlay (double sampleRate, int)
{
    // Always prepared for stereo; mono layouts simply use the first channel.
    engine.prepare (sampleRate, 2);
    engine.setResolution (params.resolution());
    engine.setEngine (params.engine());
    setLatencySamples (engine.getLatencySamples());
    preparedRate.store (sampleRate, std::memory_order_relaxed);
}

void TetherAudioProcessor::reset()
{
    engine.reset();
}

bool TetherAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto mainOut = layouts.getMainOutputChannelSet();

    if (mainOut != juce::AudioChannelSet::mono() && mainOut != juce::AudioChannelSet::stereo())
        return false;

    if (layouts.getMainInputChannelSet() != mainOut)
        return false;

    if (layouts.inputBuses.size() > 1)
    {
        const auto guide = layouts.getChannelSet (true, 1);

        if (! guide.isDisabled() && guide != juce::AudioChannelSet::mono() && guide != juce::AudioChannelSet::stereo())
            return false;
    }

    return true;
}

void TetherAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    run (buffer, false);
}

void TetherAudioProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // Bypass still delays the layer by the reported latency (so it stays in
    // time), and keeps the analysis running so switching back is seamless.
    run (buffer, true);
}

void TetherAudioProcessor::run (juce::AudioBuffer<float>& buffer, bool bypassed)
{
    juce::ScopedNoDenormals noDenormals;

    // Resolution and engine are not automatable; switching them restarts the
    // pitch stage, and a new resolution re-reports the latency.
    if (const auto wanted = params.resolution(); wanted != engine.getResolution())
    {
        engine.setResolution (wanted);
        setLatencySamples (engine.getLatencySamples());
    }

    if (const auto wanted = params.engine(); wanted != engine.getEngine())
        engine.setEngine (wanted);

    auto layer = getBusBuffer (buffer, true, 0);

    const float* guidePointers[2] = { nullptr, nullptr };
    int numGuideChannels = 0;

    if (getBusCount (true) > 1)
    {
        auto guide = getBusBuffer (buffer, true, 1);
        numGuideChannels = juce::jmin (2, guide.getNumChannels());

        for (int c = 0; c < numGuideChannels; ++c)
            guidePointers[c] = guide.getReadPointer (c);
    }

    guideRouted.store (numGuideChannels > 0, std::memory_order_relaxed);

    auto engineParams = params.read();

    if (bypassed)
    {
        engineParams.mix = 0.0f;
        engineParams.outputDb = 0.0f;
    }

    engine.process (layer.getArrayOfWritePointers(), layer.getNumChannels(),
                    numGuideChannels > 0 ? guidePointers : nullptr, numGuideChannels,
                    buffer.getNumSamples(), engineParams);
}

juce::AudioProcessorEditor* TetherAudioProcessor::createEditor()
{
    return new TetherEditor (*this);
}

void TetherAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = state.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void TetherAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (state.state.getType()))
            state.replaceState (juce::ValueTree::fromXml (*xml));
}

//==============================================================================
#if ! TETHER_TESTS
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TetherAudioProcessor();
}
#endif
