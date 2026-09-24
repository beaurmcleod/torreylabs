#pragma once

#include "TetherLookAndFeel.h"
#include "dsp/TetherEngine.h"

#include <array>

class TetherAudioProcessor;

namespace ui
{

enum class GuideStatus
{
    notRouted,  // the host isn't feeding the sidechain bus
    silent,     // routed, but nothing is playing on it
    active
};

/** Scrolling view of the guide's pitch, the layer's own pitch and where the
    layer is being moved to, plus the guide/output levels underneath. */
class Visualizer final : public juce::Component,
                         private juce::Timer
{
public:
    explicit Visualizer (TetherAudioProcessor&);
    ~Visualizer() override;

    void paint (juce::Graphics&) override;

    GuideStatus getGuideStatus() const noexcept    { return status; }
    std::function<void()> onStatusChange;

private:
    struct Frame
    {
        float guideHz = 0.0f, layerHz = 0.0f, outputHz = 0.0f;
        float guideDb = -120.0f, outputDb = -120.0f;
    };

    void timerCallback() override;
    float midiToY (float midi, juce::Rectangle<float> lane) const noexcept;

    static constexpr int historySize = 240; // 6 s at 40 frames per second

    TetherAudioProcessor& processor;
    std::array<Frame, historySize> history {};
    int head = 0, ticksWithoutData = 1000;
    tether::Telemetry latest;
    float viewLow = 36.0f, viewHigh = 72.0f; // visible MIDI range, eased toward the content
    GuideStatus status = GuideStatus::notRouted;
    double lastGuideSignalMs = -1.0e9;
};

/** Bar graph of the per-band gains the Motion/Tone stage is applying. */
class MotionMeter final : public juce::Component,
                          private juce::Timer
{
public:
    explicit MotionMeter (TetherAudioProcessor&);
    ~MotionMeter() override;

    void paint (juce::Graphics&) override;

private:
    void timerCallback() override   { repaint(); }

    TetherAudioProcessor& processor;
};

} // namespace ui
