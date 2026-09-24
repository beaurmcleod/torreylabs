#include "TetherLookAndFeel.h"

#include <BinaryData.h>

namespace ui
{

namespace
{
    struct FontCache
    {
        FontCache()
            : medium (juce::Typeface::createSystemTypefaceFor (BinaryData::InterMedium_ttf, BinaryData::InterMedium_ttfSize)),
              heavy (juce::Typeface::createSystemTypefaceFor (BinaryData::InterExtraBold_ttf, BinaryData::InterExtraBold_ttfSize))
        {
        }

        juce::Typeface::Ptr medium, heavy;
    };
}

juce::Font uiFont (float height, bool heavy)
{
    juce::SharedResourcePointer<FontCache> cache;
    const auto typeface = heavy ? cache->heavy : cache->medium;

    if (typeface == nullptr)
        return juce::Font (juce::FontOptions (height, heavy ? juce::Font::bold : juce::Font::plain));

    return juce::Font (juce::FontOptions (typeface).withHeight (height));
}

TetherLookAndFeel::TetherLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, palette::background);
    setColour (juce::Label::textColourId, palette::text);
    setColour (juce::ComboBox::backgroundColourId, palette::panel);
    setColour (juce::ComboBox::outlineColourId, palette::panelEdge);
    setColour (juce::ComboBox::textColourId, palette::text);
    setColour (juce::ComboBox::arrowColourId, palette::textDim);
    setColour (juce::PopupMenu::backgroundColourId, palette::panel);
    setColour (juce::PopupMenu::textColourId, palette::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, palette::track);
    setColour (juce::PopupMenu::highlightedTextColourId, palette::text);
    setColour (juce::TooltipWindow::backgroundColourId, palette::panel.brighter (0.1f));
    setColour (juce::TooltipWindow::textColourId, palette::text);
    setColour (juce::TooltipWindow::outlineColourId, palette::panelEdge);
    setColour (juce::AlertWindow::backgroundColourId, palette::panel);
    setColour (juce::AlertWindow::textColourId, palette::text);
    setColour (juce::AlertWindow::outlineColourId, palette::panelEdge);
    setColour (juce::TextEditor::backgroundColourId, palette::background);
    setColour (juce::TextEditor::textColourId, palette::text);
    setColour (juce::TextEditor::outlineColourId, palette::panelEdge);
    setColour (juce::TextEditor::focusedOutlineColourId, palette::textDim);
    setColour (juce::TextEditor::highlightColourId, palette::track);
    setColour (juce::TextButton::buttonColourId, palette::background);
    setColour (juce::TextButton::textColourOffId, palette::text);
    setColour (juce::TextButton::textColourOnId, palette::text);
}

void TetherLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&,
                                              bool highlighted, bool down)
{
    const auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (down ? palette::track : (highlighted ? palette::panel.brighter (0.08f) : palette::background));
    g.fillRoundedRectangle (bounds, 6.0f);
    g.setColour (highlighted ? palette::textDim : palette::panelEdge);
    g.drawRoundedRectangle (bounds, 6.0f, 1.0f);
}

void TetherLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button, bool, bool)
{
    const bool isName = button.getProperties().getWithDefault ("presetName", false);
    g.setColour (button.isEnabled() ? palette::text : palette::textDim);
    g.setFont (isName ? uiFont (12.5f, true) : uiFont (11.0f, true).withExtraKerningFactor (0.08f));
    g.drawText (button.getButtonText(), button.getLocalBounds().reduced (isName ? 10 : 4, 0),
                isName ? juce::Justification::centredLeft : juce::Justification::centred, true);
}

juce::Typeface::Ptr TetherLookAndFeel::getTypefaceForFont (const juce::Font& font)
{
    if (font.getTypefaceName() == juce::Font::getDefaultSansSerifFontName())
        if (auto typeface = uiFont (font.getHeight(), font.isBold()).getTypefacePtr())
            return typeface;

    return LookAndFeel_V4::getTypefaceForFont (font);
}

void TetherLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                          float startAngle, float endAngle, juce::Slider& slider)
{
    const auto accent = slider.findColour (juce::Slider::rotarySliderFillColourId);
    const auto enabled = slider.isEnabled();
    const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (3.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float arcRadius = radius - 3.0f;
    const float angle = startAngle + sliderPos * (endAngle - startAngle);
    const float thickness = 3.5f;

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
    g.setColour (palette::track);
    g.strokePath (track, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Bipolar knobs (octave, semitones, output) draw their arc from the centre.
    const bool bipolar = slider.getProperties().getWithDefault ("bipolar", false);
    const float from = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;

    if (std::abs (angle - from) > 0.001f)
    {
        juce::Path value;
        value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
        g.setColour (enabled ? accent : accent.withSaturation (0.1f).withAlpha (0.5f));
        g.strokePath (value, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Body
    const float bodyRadius = arcRadius - 7.0f;
    const auto body = juce::Rectangle<float> (bodyRadius * 2.0f, bodyRadius * 2.0f).withCentre (centre);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2b2f3e), body.getX(), body.getY(),
                                             juce::Colour (0xff1a1c26), body.getRight(), body.getBottom(), false));
    g.fillEllipse (body);
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.drawEllipse (body.reduced (0.5f), 1.0f);

    // Pointer
    const auto tip = centre.getPointOnCircumference (bodyRadius - 4.0f, angle);
    const auto base = centre.getPointOnCircumference (bodyRadius * 0.35f, angle);
    g.setColour (enabled ? palette::text : palette::textDim);
    g.drawLine ({ base, tip }, 2.5f);
}

void TetherLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool highlighted, bool)
{
    const auto accent = button.findColour (juce::ToggleButton::tickColourId);
    const auto bounds = button.getLocalBounds().toFloat().reduced (1.0f);
    const bool on = button.getToggleState();
    const float corner = bounds.getHeight() * 0.5f;

    g.setColour (on ? accent.withAlpha (0.18f) : palette::background);
    g.fillRoundedRectangle (bounds, corner);
    g.setColour (on ? accent : (highlighted ? palette::textDim : palette::panelEdge));
    g.drawRoundedRectangle (bounds, corner, 1.2f);

    // Status dot + label
    const auto dot = juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ bounds.getX() + 13.0f, bounds.getCentreY() });
    g.setColour (on ? accent : palette::track);
    g.fillEllipse (dot);

    g.setColour (on ? palette::text : palette::textDim);
    g.setFont (uiFont (11.0f, true));
    g.drawText (button.getButtonText(), bounds.withTrimmedLeft (22.0f).withTrimmedRight (6.0f),
                juce::Justification::centredLeft, false);
}

void TetherLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto bounds = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.5f);
    g.setColour (palette::background);
    g.fillRoundedRectangle (bounds, 6.0f);
    g.setColour (box.hasKeyboardFocus (false) ? palette::textDim : palette::panelEdge);
    g.drawRoundedRectangle (bounds, 6.0f, 1.0f);

    juce::Path arrow;
    const float ax = (float) width - 16.0f, ay = (float) height * 0.5f;
    arrow.startNewSubPath (ax - 4.0f, ay - 2.0f);
    arrow.lineTo (ax, ay + 2.5f);
    arrow.lineTo (ax + 4.0f, ay - 2.0f);
    g.setColour (palette::textDim);
    g.strokePath (arrow, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void TetherLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (8, 1, box.getWidth() - 30, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}

} // namespace ui
