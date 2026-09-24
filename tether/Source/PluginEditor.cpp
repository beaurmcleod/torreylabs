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

    juce::StringArray namesOf (int count, const char* (*name) (int))
    {
        juce::StringArray names;
        for (int i = 0; i < count; ++i)
            names.add (name (i));
        return names;
    }
}

TetherEditor::TetherEditor (TetherAudioProcessor& p)
    : AudioProcessorEditor (&p),
      processor (p),
      visualizer (p),
      motionMeter (p),
      presetBar (p.getPresets()),
      pitchAmount (p.getState(), ParamIDs::pitchAmount, "Amount", palette::pitch,
                   "How far the layer's pitch moves onto the guide's pitch."),
      glide (p.getState(), ParamIDs::glide, "Glide", palette::pitch,
             "Smooths pitch changes: 0 = instant, higher = slides between notes."),
      octave (p.getState(), ParamIDs::octave, "Octave", palette::pitch,
              "Put the layer octaves above or below the guide.", true),
      semitones (p.getState(), ParamIDs::semitones, "Interval", palette::pitch,
                 "Extra offset in semitones, e.g. +7 for a fifth above the guide.", true),
      fine (p.getState(), ParamIDs::fine, "Fine", palette::pitch,
            "Fine tuning in cents. A few cents of detune thickens a doubled layer.", true),
      root (p.getState(), ParamIDs::layerRoot, "Root", palette::pitch,
            "The layer's own note. Used when Detect is off, or when no pitch can be detected (noise, FX)."),
      formantShift (p.getState(), ParamIDs::formantShift, "Formant", palette::pitch,
                    "Moves the layer's formants (its resonant character) up or down, in semitones: "
                    "down sounds bigger, up sounds smaller.", true),
      vibrato (p.getState(), ParamIDs::vibrato, "Vibrato", palette::pitch,
               "How much of the guide's vibrato and bends the layer copies: 0% plays plain notes, "
               "100% copies them exactly, 200% exaggerates them."),
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
    addAndMakeVisible (presetBar);

    for (auto* knob : { &pitchAmount, &glide, &octave, &semitones, &fine, &root, &formantShift, &vibrato,
                        &level, &attack, &release, &punch, &gate, &motion, &tone, &mix, &output })
        addAndMakeVisible (knob);

    detectButton.setColour (juce::ToggleButton::tickColourId, palette::pitch);
    detectButton.setTooltip ("Detect the layer's own pitch automatically. Turn off to use the Root note instead.");
    formantButton.setColour (juce::ToggleButton::tickColourId, palette::pitch);
    formantButton.setTooltip ("Keep the layer's formants (its tonal character) in place while the pitch moves. "
                              "Off: the formants move with the pitch, like a sampler.");
    listenButton.setColour (juce::ToggleButton::tickColourId, palette::output);
    listenButton.setTooltip ("Monitor the guide coming into the sidechain instead of the layer.");

    for (auto* button : { &detectButton, &formantButton, &listenButton })
        addAndMakeVisible (button);

    detectAttachment = std::make_unique<ButtonAttachment> (p.getState(), ParamIDs::detectLayer, detectButton);
    formantAttachment = std::make_unique<ButtonAttachment> (p.getState(), ParamIDs::formant, formantButton);
    listenAttachment = std::make_unique<ButtonAttachment> (p.getState(), ParamIDs::listen, listenButton);

    resolutionBox.addItemList ({ "Tight", "Normal", "Deep" }, 1);
    resolutionBox.setTooltip ("Tight: lowest latency, notes above ~90 Hz.\n"
                              "Normal: notes down to ~45 Hz.\n"
                              "Deep: sub bass down to ~25 Hz, highest latency.");
    engineBox.addItemList ({ "Natural", "Spectral" }, 1);
    engineBox.setTooltip ("Natural: pitch-synchronous grains, the cleanest sound for single-note layers.\n"
                          "Spectral: phase vocoder, for chords, pads and noisy layers.");
    scaleBox.addItemList (namesOf ((int) tether::Scale::count, [] (int i) { return tether::scaleName ((tether::Scale) i); }), 1);
    scaleBox.setTooltip ("Snap the layer's notes to a scale. Vibrato and bends still ride on top.");
    keyBox.addItemList (namesOf (12, tether::keyName), 1);
    keyBox.setTooltip ("Root note of the scale.");

    for (auto* box : { &resolutionBox, &engineBox, &scaleBox, &keyBox })
        addAndMakeVisible (box);

    resolutionAttachment = std::make_unique<ComboAttachment> (p.getState(), ParamIDs::resolution, resolutionBox);
    engineAttachment = std::make_unique<ComboAttachment> (p.getState(), ParamIDs::engine, engineBox);
    scaleAttachment = std::make_unique<ComboAttachment> (p.getState(), ParamIDs::scale, scaleBox);
    keyAttachment = std::make_unique<ComboAttachment> (p.getState(), ParamIDs::key, keyBox);

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

    // The root note only matters when detection is off (or finds nothing);
    // the key only with a scale that has one.
    root.setAlpha (detectButton.getToggleState() ? 0.55f : 1.0f);
    keyBox.setAlpha (scaleBox.getSelectedItemIndex() >= (int) tether::Scale::major ? 1.0f : 0.45f);
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

    auto titleArea = area.removeFromLeft (236.0f);
    g.setColour (palette::text);
    g.setFont (uiFont (27.0f, true).withExtraKerningFactor (0.14f));
    g.drawText ("TETHER", titleArea.removeFromTop (30.0f), juce::Justification::bottomLeft, false);
    g.setColour (palette::textDim);
    g.setFont (uiFont (10.0f, true).withExtraKerningFactor (0.12f));
    g.drawText ("LOW END CANDY  /  PERFORMANCE LAYER", titleArea, juce::Justification::topLeft, false);

    auto labelAbove = [&] (juce::Rectangle<int> box, const juce::String& text)
    {
        g.setFont (uiFont (9.0f, true).withExtraKerningFactor (0.1f));
        g.setColour (palette::textDim);
        g.drawText (text, box.toFloat().withY ((float) box.getY() - 12.0f).withHeight (11.0f), juce::Justification::centredLeft, false);
    };

    labelAbove (resolutionBox.getBounds(), "RESOLUTION");
    labelAbove (engineBox.getBounds(), "ENGINE");
    labelAbove (presetBar.getBounds().withTrimmedLeft (30), "PRESET");

    // Latency, left of the engine selector.
    const auto box = engineBox.getBounds().toFloat();
    auto latency = juce::Rectangle<float> (box.getX() - 76.0f, box.getY(), 64.0f, box.getHeight());
    g.setFont (uiFont (9.0f, true).withExtraKerningFactor (0.1f));
    g.setColour (palette::textDim);
    g.drawText ("LATENCY", latency.withY (latency.getY() - 12.0f).withHeight (11.0f), juce::Justification::centredRight, false);
    g.setColour (palette::text);
    g.setFont (uiFont (13.0f, true));
    g.drawText (latencyText, latency, juce::Justification::centredRight, false);

    // Guide status pill.
    const auto status = visualizer.getGuideStatus();
    const auto colour = status == GuideStatus::active ? palette::ok
                      : status == GuideStatus::silent ? palette::warn : palette::pitch;
    const juce::String label = status == GuideStatus::active ? "GUIDE LOCKED"
                             : status == GuideStatus::silent ? "GUIDE SILENT" : "NO GUIDE ROUTED";

    auto pill = juce::Rectangle<float> (latency.getX() - 12.0f - 148.0f, box.getY(), 148.0f, box.getHeight());
    g.setColour (colour.withAlpha (0.12f));
    g.fillRoundedRectangle (pill, pill.getHeight() * 0.5f);
    g.setColour (colour.withAlpha (0.8f));
    g.drawRoundedRectangle (pill.reduced (0.5f), pill.getHeight() * 0.5f, 1.0f);
    g.setColour (colour);
    g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ pill.getX() + 15.0f, pill.getCentreY() }));
    g.setColour (palette::text);
    g.setFont (uiFont (10.5f, true).withExtraKerningFactor (0.06f));
    g.drawText (label, pill.withTrimmedLeft (26.0f), juce::Justification::centredLeft, false);
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
    resolutionBox.setBounds (w - margin - 96, 22, 96, 28);
    engineBox.setBounds (resolutionBox.getX() - 8 - 100, 22, 100, 28);
    presetBar.setBounds (256, 22, 296, 28);
    visualizer.setBounds (margin, 70, w - 2 * margin, 200);

    const int top = visualizer.getBottom() + gap;
    const int panelHeight = h - top - margin;
    pitchPanel  = { margin, top, 356, panelHeight };
    levelPanel  = { pitchPanel.getRight() + gap, top, 272, panelHeight };
    motionPanel = { levelPanel.getRight() + gap, top, 192, panelHeight };
    outputPanel = { motionPanel.getRight() + gap, top, w - margin - (motionPanel.getRight() + gap), panelHeight };

    // Every panel: title, two rows of knobs and a short row of switches.
    struct Rows { juce::Rectangle<int> first, second, third; };
    auto rows = [] (juce::Rectangle<int> panel)
    {
        auto content = panel.reduced (10, 10).withTrimmedTop (22);
        Rows r;
        r.third = content.removeFromBottom (34);
        r.first = content.removeFromTop (content.getHeight() / 2);
        r.second = content;
        return r;
    };

    {
        const auto r = rows (pitchPanel);
        placeRow (r.first, { &pitchAmount, &glide, &octave, &semitones }, cell);
        placeRow (r.second, { &fine, &root, &formantShift, &vibrato }, cell);

        auto switches = r.third.withSizeKeepingCentre (cell * 4, 28);
        detectButton.setBounds (switches.removeFromLeft (82));
        switches.removeFromLeft (6);
        formantButton.setBounds (switches.removeFromLeft (92));
        switches.removeFromLeft (6);
        keyBox.setBounds (switches.removeFromRight (56));
        switches.removeFromRight (6);
        scaleBox.setBounds (switches);
    }
    {
        const auto r = rows (levelPanel);
        placeRow (r.first, { &level, &attack, &release }, cell);
        placeRow (r.second, { &punch, &gate }, cell);
    }
    {
        const auto r = rows (motionPanel);
        placeRow (r.first, { &motion, &tone }, cell);
        motionMeter.setBounds (r.second.getUnion (r.third).reduced (4, 6));
    }
    {
        const auto r = rows (outputPanel);
        placeRow (r.first, { &mix }, cell);
        placeRow (r.second, { &output }, cell);
        listenButton.setBounds (r.third.withSizeKeepingCentre (86, 28));
    }
}
