#include "Parameters.h"

namespace
{

using Layout = juce::AudioProcessorValueTreeState::ParameterLayout;
using FloatAttributes = juce::AudioParameterFloatAttributes;

juce::ParameterID pid (const char* id)   { return { id, 1 }; }

juce::NormalisableRange<float> skewedRange (float min, float max, float centre)
{
    juce::NormalisableRange<float> range (min, max, 0.01f);
    range.setSkewForCentre (centre);
    return range;
}

std::unique_ptr<juce::AudioParameterFloat> percent (const char* id, const juce::String& name, float defaultFraction, float maxPercent = 100.0f)
{
    return std::make_unique<juce::AudioParameterFloat> (
        pid (id), name, juce::NormalisableRange<float> (0.0f, maxPercent, 0.1f), defaultFraction * 100.0f,
        FloatAttributes().withLabel ("%").withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v)) + "%"; }));
}

std::unique_ptr<juce::AudioParameterFloat> milliseconds (const char* id, const juce::String& name,
                                                         float min, float max, float centre, float defaultValue)
{
    return std::make_unique<juce::AudioParameterFloat> (
        pid (id), name, skewedRange (min, max, centre), defaultValue,
        FloatAttributes().withLabel ("ms").withStringFromValueFunction ([] (float v, int)
        {
            if (v >= 1000.0f)  return juce::String (v / 1000.0f, 2) + " s";
            if (v < 10.0f)     return juce::String (v, 1) + " ms";
            return juce::String (juce::roundToInt (v)) + " ms";
        }));
}

juce::String signedText (float v, int decimals, const juce::String& unit)
{
    const auto text = decimals > 0 ? juce::String (v, decimals) : juce::String (juce::roundToInt (v));
    return (v > 0.0001f ? "+" : "") + text + unit;
}

juce::StringArray scaleNames()
{
    juce::StringArray names;
    for (int i = 0; i < (int) tether::Scale::count; ++i)
        names.add (tether::scaleName ((tether::Scale) i));
    return names;
}

juce::StringArray keyNames()
{
    juce::StringArray names;
    for (int i = 0; i < 12; ++i)
        names.add (tether::keyName (i));
    return names;
}

void setPlain (juce::AudioProcessorValueTreeState& state, const char* id, float plain)
{
    if (auto* param = state.getParameter (id))
        param->setValueNotifyingHost (param->convertTo0to1 (plain));
}

} // namespace

juce::String noteName (int midiNote)
{
    return juce::MidiMessage::getMidiNoteName (midiNote, true, true, 3);
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    const tether::EngineParams defaults;
    Layout layout;

    auto pitch = std::make_unique<juce::AudioProcessorParameterGroup> ("pitchGroup", "Pitch", "|");
    pitch->addChild (percent (ParamIDs::pitchAmount, "Pitch Amount", defaults.pitchAmount));
    pitch->addChild (milliseconds (ParamIDs::glide, "Glide", 0.0f, 500.0f, 40.0f, defaults.glideMs));
    pitch->addChild (std::make_unique<juce::AudioParameterInt> (
        pid (ParamIDs::octave), "Octave", -3, 3, defaults.octave,
        juce::AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int)
        {
            return v == 0 ? juce::String ("0 oct") : (v > 0 ? "+" : "") + juce::String (v) + " oct";
        })));
    pitch->addChild (std::make_unique<juce::AudioParameterInt> (
        pid (ParamIDs::semitones), "Semitones", -12, 12, defaults.semitones,
        juce::AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int)
        {
            return v == 0 ? juce::String ("0 st") : (v > 0 ? "+" : "") + juce::String (v) + " st";
        })));
    pitch->addChild (std::make_unique<juce::AudioParameterFloat> (
        pid (ParamIDs::fine), "Fine", juce::NormalisableRange<float> (-100.0f, 100.0f, 1.0f), defaults.fineCents,
        FloatAttributes().withLabel ("ct").withStringFromValueFunction ([] (float v, int) { return signedText (v, 0, " ct"); })));
    pitch->addChild (std::make_unique<juce::AudioParameterBool> (pid (ParamIDs::detectLayer), "Detect Layer Pitch", defaults.layerAuto));
    pitch->addChild (std::make_unique<juce::AudioParameterInt> (
        pid (ParamIDs::layerRoot), "Layer Root", 12, 96, juce::roundToInt (defaults.layerRoot),
        juce::AudioParameterIntAttributes()
            .withStringFromValueFunction ([] (int v, int) { return noteName (v); })
            .withValueFromStringFunction ([] (const juce::String& text)
            {
                for (int n = 12; n <= 96; ++n)
                    if (noteName (n).equalsIgnoreCase (text.trim()))
                        return n;
                return text.getIntValue();
            })));
    pitch->addChild (std::make_unique<juce::AudioParameterBool> (pid (ParamIDs::formant), "Preserve Formants", defaults.formant));
    pitch->addChild (std::make_unique<juce::AudioParameterFloat> (
        pid (ParamIDs::formantShift), "Formant Shift", juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), defaults.formantShift,
        FloatAttributes().withLabel ("st").withStringFromValueFunction ([] (float v, int) { return signedText (v, 1, " st"); })));
    pitch->addChild (percent (ParamIDs::vibrato, "Vibrato", defaults.vibrato, 200.0f));
    pitch->addChild (std::make_unique<juce::AudioParameterChoice> (pid (ParamIDs::scale), "Scale", scaleNames(), (int) defaults.scale));
    pitch->addChild (std::make_unique<juce::AudioParameterChoice> (pid (ParamIDs::key), "Key", keyNames(), defaults.key));

    auto levelGroup = std::make_unique<juce::AudioProcessorParameterGroup> ("levelGroup", "Level", "|");
    levelGroup->addChild (percent (ParamIDs::level, "Level Follow", defaults.levelAmount));
    levelGroup->addChild (milliseconds (ParamIDs::attack, "Attack", 0.1f, 100.0f, 10.0f, defaults.attackMs));
    levelGroup->addChild (milliseconds (ParamIDs::release, "Release", 5.0f, 1000.0f, 120.0f, defaults.releaseMs));
    levelGroup->addChild (percent (ParamIDs::punch, "Punch", defaults.punch));
    levelGroup->addChild (std::make_unique<juce::AudioParameterFloat> (
        pid (ParamIDs::gate), "Gate", juce::NormalisableRange<float> (-80.0f, 0.0f, 0.1f), defaults.gateDb,
        FloatAttributes().withLabel ("dB").withStringFromValueFunction ([] (float v, int)
        {
            return v <= -79.95f ? juce::String ("Off") : juce::String (juce::roundToInt (v)) + " dB";
        })));

    auto motionGroup = std::make_unique<juce::AudioProcessorParameterGroup> ("motionGroup", "Motion", "|");
    motionGroup->addChild (percent (ParamIDs::motion, "Motion", defaults.motion));
    motionGroup->addChild (percent (ParamIDs::tone, "Tone Match", defaults.tone));

    auto outputGroup = std::make_unique<juce::AudioProcessorParameterGroup> ("outputGroup", "Output", "|");
    outputGroup->addChild (std::make_unique<juce::AudioParameterChoice> (
        pid (ParamIDs::resolution), "Resolution", juce::StringArray { "Tight", "Normal", "Deep" }, 1,
        juce::AudioParameterChoiceAttributes().withAutomatable (false)));
    outputGroup->addChild (std::make_unique<juce::AudioParameterChoice> (
        pid (ParamIDs::engine), "Engine", juce::StringArray { "Natural", "Spectral" }, 0,
        juce::AudioParameterChoiceAttributes().withAutomatable (false)));
    outputGroup->addChild (percent (ParamIDs::mix, "Mix", defaults.mix));
    outputGroup->addChild (std::make_unique<juce::AudioParameterFloat> (
        pid (ParamIDs::output), "Output", juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f), defaults.outputDb,
        FloatAttributes().withLabel ("dB").withStringFromValueFunction ([] (float v, int) { return signedText (v, 1, " dB"); })));
    outputGroup->addChild (std::make_unique<juce::AudioParameterBool> (pid (ParamIDs::listen), "Listen To Guide", defaults.listen));

    layout.add (std::move (pitch), std::move (levelGroup), std::move (motionGroup), std::move (outputGroup));
    return layout;
}

void applySettings (juce::AudioProcessorValueTreeState& state, const PluginSettings& s)
{
    const auto& p = s.params;
    setPlain (state, ParamIDs::pitchAmount, p.pitchAmount * 100.0f);
    setPlain (state, ParamIDs::glide, p.glideMs);
    setPlain (state, ParamIDs::octave, (float) p.octave);
    setPlain (state, ParamIDs::semitones, (float) p.semitones);
    setPlain (state, ParamIDs::fine, p.fineCents);
    setPlain (state, ParamIDs::detectLayer, p.layerAuto ? 1.0f : 0.0f);
    setPlain (state, ParamIDs::layerRoot, p.layerRoot);
    setPlain (state, ParamIDs::formant, p.formant ? 1.0f : 0.0f);
    setPlain (state, ParamIDs::formantShift, p.formantShift);
    setPlain (state, ParamIDs::vibrato, p.vibrato * 100.0f);
    setPlain (state, ParamIDs::scale, (float) p.scale);
    setPlain (state, ParamIDs::key, (float) p.key);
    setPlain (state, ParamIDs::level, p.levelAmount * 100.0f);
    setPlain (state, ParamIDs::attack, p.attackMs);
    setPlain (state, ParamIDs::release, p.releaseMs);
    setPlain (state, ParamIDs::punch, p.punch * 100.0f);
    setPlain (state, ParamIDs::gate, p.gateDb);
    setPlain (state, ParamIDs::motion, p.motion * 100.0f);
    setPlain (state, ParamIDs::tone, p.tone * 100.0f);
    setPlain (state, ParamIDs::resolution, (float) s.resolution);
    setPlain (state, ParamIDs::engine, (float) s.engine);
    setPlain (state, ParamIDs::mix, p.mix * 100.0f);
    setPlain (state, ParamIDs::output, p.outputDb);
    setPlain (state, ParamIDs::listen, p.listen ? 1.0f : 0.0f);
}

//==============================================================================
ParameterReader::ParameterReader (juce::AudioProcessorValueTreeState& state)
    : pitchAmount (get (state, ParamIDs::pitchAmount)),
      glide (get (state, ParamIDs::glide)),
      octave (get (state, ParamIDs::octave)),
      semitones (get (state, ParamIDs::semitones)),
      fine (get (state, ParamIDs::fine)),
      detectLayer (get (state, ParamIDs::detectLayer)),
      layerRoot (get (state, ParamIDs::layerRoot)),
      formant (get (state, ParamIDs::formant)),
      formantShift (get (state, ParamIDs::formantShift)),
      vibrato (get (state, ParamIDs::vibrato)),
      scale (get (state, ParamIDs::scale)),
      key (get (state, ParamIDs::key)),
      level (get (state, ParamIDs::level)),
      attack (get (state, ParamIDs::attack)),
      release (get (state, ParamIDs::release)),
      punch (get (state, ParamIDs::punch)),
      gate (get (state, ParamIDs::gate)),
      motion (get (state, ParamIDs::motion)),
      tone (get (state, ParamIDs::tone)),
      resolutionChoice (get (state, ParamIDs::resolution)),
      engineChoice (get (state, ParamIDs::engine)),
      mix (get (state, ParamIDs::mix)),
      output (get (state, ParamIDs::output)),
      listen (get (state, ParamIDs::listen))
{
}

std::atomic<float>* ParameterReader::get (juce::AudioProcessorValueTreeState& state, const char* id)
{
    auto* value = state.getRawParameterValue (id);
    jassert (value != nullptr);
    return value;
}

tether::EngineParams ParameterReader::read() const noexcept
{
    tether::EngineParams p;
    p.pitchAmount  = pitchAmount->load() * 0.01f;
    p.glideMs      = glide->load();
    p.octave       = juce::roundToInt (octave->load());
    p.semitones    = juce::roundToInt (semitones->load());
    p.fineCents    = fine->load();
    p.layerAuto    = detectLayer->load() > 0.5f;
    p.layerRoot    = layerRoot->load();
    p.formant      = formant->load() > 0.5f;
    p.formantShift = formantShift->load();
    p.vibrato      = vibrato->load() * 0.01f;
    p.scale        = (tether::Scale) juce::jlimit (0, (int) tether::Scale::count - 1, juce::roundToInt (scale->load()));
    p.key          = juce::jlimit (0, 11, juce::roundToInt (key->load()));
    p.levelAmount  = level->load() * 0.01f;
    p.attackMs     = attack->load();
    p.releaseMs    = release->load();
    p.punch        = punch->load() * 0.01f;
    p.gateDb       = gate->load();
    p.motion       = motion->load() * 0.01f;
    p.tone         = tone->load() * 0.01f;
    p.mix          = mix->load() * 0.01f;
    p.outputDb     = output->load();
    p.listen       = listen->load() > 0.5f;
    return p;
}

tether::Resolution ParameterReader::resolution() const noexcept
{
    return (tether::Resolution) juce::jlimit (0, 2, juce::roundToInt (resolutionChoice->load()));
}

tether::Engine ParameterReader::engine() const noexcept
{
    return (tether::Engine) juce::jlimit (0, 1, juce::roundToInt (engineChoice->load()));
}
