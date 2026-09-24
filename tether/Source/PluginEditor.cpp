#include "PluginEditor.h"

using namespace ui;

namespace
{
    void placeRow (juce::Rectangle<int> row, std::initializer_list<juce::Component*> items, int cellWidth)
    {
        auto area = row.withSizeKeepingCentre (cellWidth * (int) items.size(), row.getHeight());
        for (auto* item : items)
            item->setBounds (area.removeFromLeft (cellWidth));
    }
}

TetherEditor::TetherEditor (TetherAudioProcessor& p)
    : AudioProcessorEditor (&p),
      processor (p),
      visualizer (p),
      motionMeter (p),
      pitchAmount (p.getState(), ParamIDs::pitchAmount, "Amount", palette::pitch,
                   "How far the layer's pitch moves onto the guide's pitch."),
      glide (p.getState(), ParamIDs::glide, "Glide", palette::pitch,
             "Smooths pitch changes: 0 = instant, higher = slides between notes."),
      octave (p.getState(), ParamIDs::octave, "Octave", palette::pitch,
              "Put the layer octaves above or below the guide.", true),
      semitones (p.getState(), ParamIDs::semitones, "Interval", palette::pitch,
                 "Extra offset in semitones, e.g. +7 for a fifth above the guide.", true),
      root (p.getState(), ParamIDs::layerRoot, "Root", palette::pitch,
            "The layer's own note. Used when Detect is off, or when no pitch can be detected (noise, FX)."),
      level (p.getState(), ParamIDs::level, "Level", palette::level,
             "How closely the layer's volume follows the guide's (100% = identical loudness curve)."),
      attack (p.getState(), ParamIDs::attack, "Attack", palette::level,
              "How fast the layer follows the guide getting louder."),
      release (p.getState(), ParamIDs::release, "Release", palette::level,
               "How fast the layer follows the guide getting quieter."),
      punch (p.getState(), ParamIDs::punch, "Punch", palette::level,
             "Re-applies the guide's transients to the layer."),
      gate (p.getState(), ParamIDs::gate, "Gate", palette::level,
            "Mutes the layer whenever the guide drops below this level."),
      motion (p.getState(), ParamIDs::motion, "Motion", palette::motion,
              "Copies the guide's tonal movement (filter sweeps, vowels, brightness) onto the layer."),
      tone (p.getState(), ParamIDs::tone, "Tone", palette::motion,
            "Pulls the layer's overall tone toward the guide's."),
      mix (p.getState(), ParamIDs::mix, "Mix", palette::output,
           "Blend between the processed and the untouched layer."),
      output (p.getState(), ParamIDs::output, "Output", palette::output, "Output level.", true)
{
    setLookAndFeel (&lookAndFeel);

    addAndMakeVisible (visualizer);
    addAndMakeVisible (motionMeter);

    for (auto* knob : { &pitchAmount, &glide, &octave, &semitones, &root, &level, &attack, &release,
                        &punch, &gate, &motion, &tone, &mix, &output })
        addAndMakeVisible (knob);

    detectButton.setColour (juce::ToggleButton::tickColourId, palette::pitch);
    detectButton.setTooltip ("Detect the layer's own pitch automatically. Turn off to use the Root note instead.");
    formantButton.setColour (juce::ToggleButton::tickColourId, palette::pitch);
    formantButton.setTooltip ("Keep the layer's tonal character when it's shifted (no chipmunk / monster effect).");
    addAndMakeVisible (detectButton);
    addAndMakeVisible (formantButton);

    detectAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.getState(), ParamIDs::detectLayer, detectButton);
    formantAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.getState(), ParamIDs::formant, formantButton);

    resolutionBox.addItemList ({ "Tight", "Normal", "Deep" }, 1);
    resolutionBox.setTooltip ("Tight: lowest latency, notes above ~90 Hz.\n"
                              "Normal: notes down to ~45 Hz.\n"
                              "Deep: sub bass down to ~25 Hz, highest latency.");
    addAndMakeVisible (resolutionBox);
    resolutionAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.getState(), ParamIDs::resolution, resolutionBox);

    visualizer.onStatusChange = [this] { repaint (headerArea); };

    setSize (defaultWidth, defaultHeight);
    timerCallback();
    startTimerHz (8);
}

TetherEditor::~TetherEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void TetherEditor::timerCallback()
{
    const double ms = processor.getLatencyMs();
    const auto text = ms > 0.0 ? juce::String (juce::roundToInt (ms)) + " ms" : juce::String ("--");

    if (text != latencyText)
    {
        latencyText = text;
        repaint (headerArea);
    }

    // The root note only matters when detection is off (or finds nothing).
    root.setAlpha (detectButton.getToggleState() ? 0.55f : 1.0f);
}

void TetherEditor::paint (juce::Graphics& g)
{
    g.fillAll (palette::background);

    // A little candy glow behind the header.
    g.setGradientFill (juce::ColourGradient (palette::pitch.withAlpha (0.10f), 0.0f, 0.0f,
                                             palette::background.withAlpha (0.0f), 420.0f, 220.0f, true));
    g.fillRect (getLocalBounds());

    paintHeader (g);
    paintPanel (g, pitchPanel, "PITCH", palette::pitch);
    paintPanel (g, levelPanel, "LEVEL + ARTICULATION", palette::level);
    paintPanel (g, motionPanel, "MOTION", palette::motion);
    paintPanel (g, outputPanel, "OUTPUT", palette::output);
}

void TetherEditor::paintHeader (juce::Graphics& g) const
{
    auto area = headerArea.toFloat().reduced (20.0f, 10.0f);

    auto titleArea = area.removeFromLeft (360.0f);
    g.setColour (palette::text);
    g.setFont (uiFont (27.0f, true).withExtraKerningFactor (0.14f));
    g.drawText ("TETHER", titleArea.removeFromTop (30.0f), juce::Justification::bottomLeft, false);
    g.setColour (palette::textDim);
    g.setFont (uiFont (10.0f, true).withExtraKerningFactor (0.12f));
    g.drawText ("LOW END CANDY  /  PERFORMANCE LAYER", titleArea, juce::Justification::topLeft, false);

    // Latency, left of the resolution selector.
    const auto box = resolutionBox.getBounds().toFloat();
    auto latency = juce::Rectangle<float> (box.getX() - 104.0f, box.getY(), 92.0f, box.getHeight());
    g.setFont (uiFont (9.5f, true).withExtraKerningFactor (0.08f));
    g.setColour (palette::textDim);
    g.drawText ("LATENCY", latency.removeFromTop (12.0f), juce::Justification::centredRight, false);
    g.setColour (palette::text);
    g.setFont (uiFont (13.0f, true));
    g.drawText (latencyText, latency, juce::Justification::centredRight, false);

    // Guide status pill.
    const auto status = visualizer.getGuideStatus();
    const auto colour = status == GuideStatus::active ? palette::ok
                      : status == GuideStatus::silent ? palette::warn : palette::pitch;
    const juce::String label = status == GuideStatus::active ? "GUIDE LOCKED"
                             : status == GuideStatus::silent ? "GUIDE SILENT" : "NO GUIDE ROUTED";

    auto pill = juce::Rectangle<float> (box.getX() - 104.0f - 16.0f - 164.0f, box.getY(), 164.0f, box.getHeight());
    g.setColour (colour.withAlpha (0.12f));
    g.fillRoundedRectangle (pill, pill.getHeight() * 0.5f);
    g.setColour (colour.withAlpha (0.8f));
    g.drawRoundedRectangle (pill.reduced (0.5f), pill.getHeight() * 0.5f, 1.0f);
    g.setColour (colour);
    g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ pill.getX() + 16.0f, pill.getCentreY() }));
    g.setColour (palette::text);
    g.setFont (uiFont (11.0f, true).withExtraKerningFactor (0.08f));
    g.drawText (label, pill.withTrimmedLeft (28.0f), juce::Justification::centredLeft, false);
}

void TetherEditor::paintPanel (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title, juce::Colour accent) const
{
    const auto bounds = area.toFloat();
    g.setColour (palette::panel);
    g.fillRoundedRectangle (bounds, 10.0f);
    g.setColour (palette::panelEdge);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 10.0f, 1.0f);

    auto titleRow = bounds.reduced (14.0f, 12.0f).removeFromTop (14.0f);
    g.setColour (accent);
    g.fillEllipse (titleRow.removeFromLeft (7.0f).withSizeKeepingCentre (7.0f, 7.0f));
    titleRow.removeFromLeft (8.0f);
    g.setColour (palette::text);
    g.setFont (uiFont (10.5f, true).withExtraKerningFactor (0.12f));
    g.drawText (title, titleRow, juce::Justification::centredLeft, false);
}

void TetherEditor::resized()
{
    const int w = getWidth(), h = getHeight();
    constexpr int margin = 16, gap = 12, cell = 84;

    headerArea = { 0, 0, w, 64 };
    resolutionBox.setBounds (w - margin - 112, 18, 112, 28);
    visualizer.setBounds (margin, 70, w - 2 * margin, 216);

    const int top = visualizer.getBottom() + gap;
    const int panelHeight = h - top - margin;
    pitchPanel  = { margin, top, 272, panelHeight };
    levelPanel  = { pitchPanel.getRight() + gap, top, 272, panelHeight };
    motionPanel = { levelPanel.getRight() + gap, top, 192, panelHeight };
    outputPanel = { motionPanel.getRight() + gap, top, w - margin - (motionPanel.getRight() + gap), panelHeight };

    auto rows = [] (juce::Rectangle<int> panel)
    {
        auto content = panel.reduced (10, 10).withTrimmedTop (22);
        auto first = content.removeFromTop (content.getHeight() / 2);
        return std::pair { first, content };
    };

    {
        auto [first, second] = rows (pitchPanel);
        placeRow (first, { &pitchAmount, &glide, &octave }, cell);

        auto area = second.withSizeKeepingCentre (cell * 3, second.getHeight());
        semitones.setBounds (area.removeFromLeft (cell));
        root.setBounds (area.removeFromLeft (cell));
        auto toggles = area.withSizeKeepingCentre (area.getWidth() - 4, 62);
        detectButton.setBounds (toggles.removeFromTop (28));
        toggles.removeFromTop (6);
        formantButton.setBounds (toggles.removeFromTop (28));
    }
    {
        auto [first, second] = rows (levelPanel);
        placeRow (first, { &level, &attack, &release }, cell);
        placeRow (second, { &punch, &gate }, cell);
    }
    {
        auto [first, second] = rows (motionPanel);
        placeRow (first, { &motion, &tone }, cell);
        motionMeter.setBounds (second.reduced (4, 10));
    }
    {
        auto [first, second] = rows (outputPanel);
        placeRow (first, { &mix }, cell);
        placeRow (second, { &output }, cell);
    }
}
