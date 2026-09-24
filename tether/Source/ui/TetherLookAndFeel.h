#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace ui
{

namespace palette
{
    const juce::Colour background { 0xff0c0d12 };
    const juce::Colour panel      { 0xff15171f };
    const juce::Colour panelEdge  { 0xff252836 };
    const juce::Colour track      { 0xff2a2e3d };
    const juce::Colour text       { 0xffeceef5 };
    const juce::Colour textDim    { 0xff858ba1 };
    const juce::Colour pitch      { 0xffff4fa3 }; // candy pink
    const juce::Colour level      { 0xff36d6ff }; // ice cyan
    const juce::Colour motion     { 0xffb18cff }; // grape
    const juce::Colour output     { 0xffffd84d }; // lemon
    const juce::Colour ok         { 0xff5cf2b0 };
    const juce::Colour warn       { 0xffffa94d };
}

/** Embedded Inter (SIL OFL) so the UI looks identical on every platform. */
juce::Font uiFont (float height, bool heavy = false);

class TetherLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    TetherLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;

    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;

    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;

    juce::Font getComboBoxFont (juce::ComboBox&) override        { return uiFont (13.0f, true); }
    juce::Font getPopupMenuFont() override                       { return uiFont (14.0f); }
    juce::Font getLabelFont (juce::Label&) override              { return uiFont (12.0f); }
    juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;
};

} // namespace ui
