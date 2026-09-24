#pragma once

#include "TetherLookAndFeel.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace ui
{

/** Rotary control bound to a parameter, with its name and live value underneath. */
class Knob final : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& titleText,
          juce::Colour accent, const juce::String& tooltip, bool bipolar = false)
        : title (titleText.toUpperCase())
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        slider.setMouseDragSensitivity (220);
        slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
        slider.getProperties().set ("bipolar", bipolar);
        slider.setTooltip (tooltip);
        slider.onValueChange = [this] { repaint(); };
        addAndMakeVisible (slider);

        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, paramId, slider);
    }

    void resized() override
    {
        auto bounds = getLocalBounds();
        textArea = bounds.removeFromBottom (30);
        const int size = juce::jmin (bounds.getWidth(), bounds.getHeight(), 66);
        slider.setBounds (bounds.withSizeKeepingCentre (size, size));
    }

    void paint (juce::Graphics& g) override
    {
        auto area = textArea;
        g.setColour (palette::textDim);
        g.setFont (uiFont (10.0f, true).withExtraKerningFactor (0.08f));
        g.drawText (title, area.removeFromTop (13), juce::Justification::centred, false);

        g.setColour (isEnabled() ? palette::text : palette::textDim);
        g.setFont (uiFont (12.5f));
        g.drawText (slider.getTextFromValue (slider.getValue()), area, juce::Justification::centred, false);
    }

    juce::Slider slider;

private:
    juce::String title;
    juce::Rectangle<int> textArea;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Knob)
};

} // namespace ui
