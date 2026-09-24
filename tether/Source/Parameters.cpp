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

std::unique_ptr<juce::AudioParameterFloat> percent (const char* id, const juce::String& name, float defaultFraction)
{
    return std::make_unique<juce::AudioParameterFloat> (
        pid (id), name, juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), defaultFraction * 100.0f,
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
    outputGroup->addChild (percent (ParamIDs::mix, "Mix", defaults.mix));
    outputGroup->addChild (std::make_unique<juce::AudioParameterFloat> (
        pid (ParamIDs::output), "Output", juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f), defaults.outputDb,
        FloatAttributes().withLabel ("dB").withStringFromValueFunction ([] (float v, int)
        {
            return (v > 0.05f ? "+" : "") + juce::String (v, 1) + " dB";
        })));

    layout.add (std::move (pitch), std::move (levelGroup), std::move (motionGroup), std::move (outputGroup));
    return layout;
}

//==============================================================================
ParameterReader::ParameterReader (juce::AudioProcessorValueTreeState& state)
    : pitchAmount (get (state, ParamIDs::pitchAmount)),
      glide (get (state, ParamIDs::glide)),
      octave (get (state, ParamIDs::octave)),
      semitones (get (state, ParamIDs::semitones)),
      detectLayer (get (state, ParamIDs::detectLayer)),
      layerRoot (get (state, ParamIDs::layerRoot)),
      formant (get (state, ParamIDs::formant)),
      level (get (state, ParamIDs::level)),
      attack (get (state, ParamIDs::attack)),
      release (get (state, ParamIDs::release)),
      punch (get (state, ParamIDs::punch)),
      gate (get (state, ParamIDs::gate)),
      motion (get (state, ParamIDs::motion)),
      tone (get (state, ParamIDs::tone)),
      resolutionChoice (get (state, ParamIDs::resolution)),
      mix (get (state, ParamIDs::mix)),
      output (get (state, ParamIDs::output))
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
    p.pitchAmount = pitchAmount->load() * 0.01f;
    p.glideMs     = glide->load();
    p.octave      = juce::roundToInt (octave->load());
    p.semitones   = juce::roundToInt (semitones->load());
    p.layerAuto   = detectLayer->load() > 0.5f;
    p.layerRoot   = layerRoot->load();
    p.formant     = formant->load() > 0.5f;
    p.levelAmount = level->load() * 0.01f;
    p.attackMs    = attack->load();
    p.releaseMs   = release->load();
    p.punch       = punch->load() * 0.01f;
    p.gateDb      = gate->load();
    p.motion      = motion->load() * 0.01f;
    p.tone        = tone->load() * 0.01f;
    p.mix         = mix->load() * 0.01f;
    p.outputDb    = output->load();
    return p;
}

tether::Resolution ParameterReader::resolution() const noexcept
{
    return (tether::Resolution) juce::jlimit (0, 2, juce::roundToInt (resolutionChoice->load()));
}
