#include "Visualizer.h"

#include "../Parameters.h"
#include "../PluginProcessor.h"

namespace ui
{

namespace
{
    juce::String describePitch (float hz)
    {
        const float midi = tether::hzToMidi (hz);
        const int nearest = juce::roundToInt (midi);
        const int cents = juce::roundToInt ((midi - (float) nearest) * 100.0f);
        return noteName (nearest) + (cents == 0 ? juce::String() : (cents > 0 ? " +" : " ") + juce::String (cents) + juce::String::fromUTF8 ("\xc2\xa2"));
    }
}

Visualizer::Visualizer (TetherAudioProcessor& p) : processor (p)
{
    setOpaque (false);
    startTimerHz (40);
}

Visualizer::~Visualizer()
{
    stopTimer();
}

void Visualizer::timerCallback()
{
    tether::Telemetry t;
    bool received = false;
    float guideDb = -120.0f, outputDb = -120.0f;

    while (processor.getEngine().getTelemetryQueue().pop (t))
    {
        received = true;
        latest = t;
        guideDb = juce::jmax (guideDb, t.guideDb);
        outputDb = juce::jmax (outputDb, t.outputDb);
    }

    Frame frame;

    if (received)
    {
        ticksWithoutData = 0;
        frame = { latest.guideHz, latest.layerHz, latest.outputHz, guideDb, outputDb };
    }
    else if (++ticksWithoutData < 4)
    {
        // Big hops can arrive slower than the display rate: hold the last frame briefly.
        frame = history[(size_t) ((head + historySize - 1) % historySize)];
    }

    history[(size_t) head] = frame;
    head = (head + 1) % historySize;

    // Guide status for the header and the empty-state message.
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (received && latest.guideDb > -70.0f)
        lastGuideSignalMs = now;

    const auto newStatus = ! processor.isGuideRouted() ? GuideStatus::notRouted
                         : (now - lastGuideSignalMs < 1500.0 ? GuideStatus::active : GuideStatus::silent);

    if (newStatus != status)
    {
        status = newStatus;
        if (onStatusChange)
            onStatusChange();
    }

    // Ease the visible range toward whatever is being played.
    float low = 1000.0f, high = -1000.0f;
    for (const auto& f : history)
    {
        for (float hz : { f.guideHz, f.outputHz })
        {
            if (hz > 0.0f)
            {
                const float m = tether::hzToMidi (hz);
                low = juce::jmin (low, m);
                high = juce::jmax (high, m);
            }
        }
    }

    if (low <= high)
    {
        float targetLow = low - 5.0f, targetHigh = high + 5.0f;
        const float missing = 24.0f - (targetHigh - targetLow);
        if (missing > 0.0f)
        {
            targetLow -= missing * 0.5f;
            targetHigh += missing * 0.5f;
        }

        viewLow += 0.08f * (targetLow - viewLow);
        viewHigh += 0.08f * (targetHigh - viewHigh);
    }

    repaint();
}

float Visualizer::midiToY (float midi, juce::Rectangle<float> lane) const noexcept
{
    const float t = (midi - viewLow) / juce::jmax (1.0f, viewHigh - viewLow);
    return lane.getBottom() - t * lane.getHeight();
}

void Visualizer::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    g.setColour (palette::panel);
    g.fillRoundedRectangle (area, 10.0f);
    g.setColour (palette::panelEdge);
    g.drawRoundedRectangle (area.reduced (0.5f), 10.0f, 1.0f);

    auto inner = area.reduced (14.0f, 12.0f);
    auto header = inner.removeFromTop (18.0f);
    inner.removeFromTop (6.0f);
    auto levelLane = inner.removeFromBottom (inner.getHeight() * 0.22f);
    inner.removeFromBottom (8.0f);
    auto pitchLane = inner;
    pitchLane.removeFromLeft (30.0f);
    levelLane.removeFromLeft (30.0f);

    // Legend
    {
        auto legend = header;
        const std::pair<const char*, juce::Colour> items[] = {
            { "GUIDE", palette::pitch }, { "LAYER IN", palette::level.withAlpha (0.7f) }, { "LAYER OUT", palette::output }
        };

        g.setFont (uiFont (10.0f, true).withExtraKerningFactor (0.08f));
        for (const auto& [name, colour] : items)
        {
            g.setColour (colour);
            g.fillRoundedRectangle (legend.removeFromLeft (14.0f).withSizeKeepingCentre (14.0f, 3.0f), 1.5f);
            legend.removeFromLeft (6.0f);
            g.setColour (palette::textDim);
            const float w = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), name) + 4.0f;
            g.drawText (name, legend.removeFromLeft (w), juce::Justification::centredLeft, false);
            legend.removeFromLeft (16.0f);
        }

        // Live readout on the right.
        juce::String readout;
        if (latest.guideHz > 0.0f)
            readout << "GUIDE " << describePitch (latest.guideHz);
        if (latest.outputHz > 0.0f)
            readout << "   LAYER " << describePitch (latest.outputHz);
        if (std::abs (latest.shiftSemitones) > 0.05f)
            readout << "   SHIFT " << (latest.shiftSemitones > 0 ? "+" : "") << juce::String (latest.shiftSemitones, 1) << " st";

        g.setColour (palette::text);
        g.setFont (uiFont (11.0f, true));
        g.drawText (readout, header, juce::Justification::centredRight, false);
    }

    // Note grid
    g.setFont (uiFont (9.5f, true));
    for (int m = (int) std::ceil (viewLow); m <= (int) std::floor (viewHigh); ++m)
    {
        const float y = midiToY ((float) m, pitchLane);
        if (m % 12 == 0)
        {
            g.setColour (palette::panelEdge);
            g.drawHorizontalLine (juce::roundToInt (y), pitchLane.getX(), pitchLane.getRight());
            g.setColour (palette::textDim);
            g.drawText (noteName (m), juce::Rectangle<float> (pitchLane.getX() - 32.0f, y - 7.0f, 28.0f, 14.0f),
                        juce::Justification::centredRight, false);
        }
        else if (viewHigh - viewLow < 30.0f)
        {
            g.setColour (palette::panelEdge.withAlpha (0.35f));
            g.drawHorizontalLine (juce::roundToInt (y), pitchLane.getX(), pitchLane.getRight());
        }
    }

    auto xFor = [] (int index, juce::Rectangle<float> lane)
    {
        return lane.getX() + lane.getWidth() * (float) index / (float) (historySize - 1);
    };

    auto drawTrace = [&] (float Frame::* member, juce::Colour colour, float thickness)
    {
        juce::Path path;
        bool drawing = false;

        for (int i = 0; i < historySize; ++i)
        {
            const float hz = history[(size_t) ((head + i) % historySize)].*member;

            if (hz <= 0.0f)
            {
                drawing = false;
                continue;
            }

            const juce::Point<float> p { xFor (i, pitchLane), midiToY (tether::hzToMidi (hz), pitchLane) };
            if (drawing)
                path.lineTo (p);
            else
                path.startNewSubPath (p);
            drawing = true;
        }

        g.setColour (colour);
        g.strokePath (path, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    };

    {
        juce::Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (pitchLane.toNearestInt());
        drawTrace (&Frame::layerHz, palette::level.withAlpha (0.6f), 1.5f);
        drawTrace (&Frame::guideHz, palette::pitch.withAlpha (0.6f), 6.0f);  // wide, so the layer on top of it stays visible
        drawTrace (&Frame::outputHz, palette::output, 2.0f);
    }

    // Level lane: guide as a filled area, output as a line (-60..0 dB).
    {
        g.setColour (palette::background.withAlpha (0.6f));
        g.fillRoundedRectangle (levelLane, 4.0f);

        auto dbToY = [&] (float db)
        {
            return levelLane.getBottom() - juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f) * levelLane.getHeight();
        };

        juce::Path guideArea, outputLine;
        guideArea.startNewSubPath (levelLane.getX(), levelLane.getBottom());

        for (int i = 0; i < historySize; ++i)
        {
            const auto& f = history[(size_t) ((head + i) % historySize)];
            const float x = xFor (i, levelLane);
            guideArea.lineTo (x, dbToY (f.guideDb));

            if (i == 0)
                outputLine.startNewSubPath (x, dbToY (f.outputDb));
            else
                outputLine.lineTo (x, dbToY (f.outputDb));
        }

        guideArea.lineTo (levelLane.getRight(), levelLane.getBottom());
        guideArea.closeSubPath();

        g.setColour (palette::pitch.withAlpha (0.28f));
        g.fillPath (guideArea);
        g.setColour (palette::output.withAlpha (0.9f));
        g.strokePath (outputLine, juce::PathStrokeType (1.5f));

        g.setColour (palette::textDim);
        g.setFont (uiFont (9.5f, true));
        g.drawText ("LEVEL", juce::Rectangle<float> (levelLane.getX() - 32.0f, levelLane.getY(), 28.0f, 14.0f),
                    juce::Justification::centredRight, false);
    }

    // Empty states
    if (status != GuideStatus::active)
    {
        const bool routed = status == GuideStatus::silent;
        auto message = pitchLane.withSizeKeepingCentre (pitchLane.getWidth(), 44.0f);
        g.setColour (palette::text);
        g.setFont (uiFont (15.0f, true));
        g.drawText (routed ? "Waiting for the guide to play..." : "Send your guide sound to Tether's sidechain",
                    message.removeFromTop (22.0f), juce::Justification::centred, false);
        g.setColour (palette::textDim);
        g.setFont (uiFont (12.0f));
        g.drawText (routed ? "The layer is silenced while the guide is silent (Level / Gate)."
                           : "Until then the layer passes through untouched.",
                    message, juce::Justification::centred, false);
    }
}

//==============================================================================
MotionMeter::MotionMeter (TetherAudioProcessor& p) : processor (p)
{
    startTimerHz (30);
}

MotionMeter::~MotionMeter()
{
    stopTimer();
}

void MotionMeter::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    g.setColour (palette::background.withAlpha (0.6f));
    g.fillRoundedRectangle (area, 6.0f);

    auto plot = area.reduced (8.0f, 8.0f);
    auto label = plot.removeFromBottom (12.0f);
    g.setColour (palette::textDim);
    g.setFont (uiFont (9.0f, true).withExtraKerningFactor (0.08f));
    g.drawText ("LOW", label, juce::Justification::centredLeft, false);
    g.drawText ("MOTION EQ", label, juce::Justification::centred, false);
    g.drawText ("HIGH", label, juce::Justification::centredRight, false);

    const auto& engine = processor.getEngine();
    const int bands = engine.getDisplayBandCount();
    const float mid = plot.getCentreY();

    g.setColour (palette::panelEdge);
    g.drawHorizontalLine (juce::roundToInt (mid), plot.getX(), plot.getRight());

    if (bands <= 0)
        return;

    const float barWidth = plot.getWidth() / (float) bands;
    constexpr float rangeDb = 18.0f;

    for (int b = 0; b < bands; ++b)
    {
        const float db = juce::jlimit (-rangeDb, rangeDb, engine.getDisplayBandGainDb (b));
        const float h = (db / rangeDb) * plot.getHeight() * 0.5f;
        auto bar = juce::Rectangle<float> (plot.getX() + b * barWidth + 1.0f, h > 0 ? mid - h : mid, barWidth - 2.0f, std::abs (h));
        g.setColour (palette::motion.withAlpha (0.35f + 0.65f * std::abs (db) / rangeDb));
        g.fillRoundedRectangle (bar, 1.5f);
    }
}

} // namespace ui
