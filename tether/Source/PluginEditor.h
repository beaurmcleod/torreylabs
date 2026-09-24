#pragma once

#include "PluginProcessor.h"
#include "ui/Knob.h"
#include "ui/PresetBar.h"
#include "ui/TetherLookAndFeel.h"
#include "ui/Visualizer.h"

class TetherEditor final : public juce::AudioProcessorEditor,
                           private juce::Timer
{
public:
    explicit TetherEditor (TetherAudioProcessor&);
    ~TetherEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int defaultWidth = 1040, defaultHeight = 600;

private:
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    void timerCallback() override;
    void paintPanel (juce::Graphics&, juce::Rectangle<int> area, const juce::String& title, juce::Colour accent) const;
    void paintHeader (juce::Graphics&) const;

    TetherAudioProcessor& processor;
    ui::TetherLookAndFeel lookAndFeel;

    ui::Visualizer visualizer;
    ui::MotionMeter motionMeter;
    ui::PresetBar presetBar;

    ui::Knob pitchAmount, glide, octave, semitones, fine, root, formantShift, vibrato;
    ui::Knob level, attack, release, punch, gate;
    ui::Knob motion, tone;
    ui::Knob mix, output;

    juce::ToggleButton detectButton { "DETECT" }, formantButton { "PRESERVE" }, listenButton { "LISTEN" };
    juce::ComboBox resolutionBox, engineBox, scaleBox, keyBox;
    std::unique_ptr<ButtonAttachment> detectAttachment, formantAttachment, listenAttachment;
    std::unique_ptr<ComboAttachment> resolutionAttachment, engineAttachment, scaleAttachment, keyAttachment;

    juce::Rectangle<int> headerArea, pitchPanel, levelPanel, motionPanel, outputPanel;
    juce::String latencyText;
    juce::TooltipWindow tooltips { this, 700 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TetherEditor)
};
