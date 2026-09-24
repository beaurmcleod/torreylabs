#pragma once

#include "dsp/TetherEngine.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace ParamIDs
{
    inline constexpr auto pitchAmount  = "pitchAmount";
    inline constexpr auto glide        = "glide";
    inline constexpr auto octave       = "octave";
    inline constexpr auto semitones    = "semitones";
    inline constexpr auto fine         = "fine";
    inline constexpr auto detectLayer  = "detectLayer";
    inline constexpr auto layerRoot    = "layerRoot";
    inline constexpr auto formant      = "formant";
    inline constexpr auto formantShift = "formantShift";
    inline constexpr auto vibrato      = "vibrato";
    inline constexpr auto scale        = "scale";
    inline constexpr auto key          = "key";
    inline constexpr auto level        = "level";
    inline constexpr auto attack       = "attack";
    inline constexpr auto release      = "release";
    inline constexpr auto punch        = "punch";
    inline constexpr auto gate         = "gate";
    inline constexpr auto motion       = "motion";
    inline constexpr auto tone         = "tone";
    inline constexpr auto resolution   = "resolution";
    inline constexpr auto engine       = "engine";
    inline constexpr auto mix          = "mix";
    inline constexpr auto output       = "output";
    inline constexpr auto listen       = "listen";
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

/** Note name in the convention used by Ableton Live and Logic (MIDI 60 = C3). */
juce::String noteName (int midiNote);

/** A complete setting of the plugin in plain units. */
struct PluginSettings
{
    tether::EngineParams params;
    tether::Resolution resolution = tether::Resolution::normal;
    tether::Engine engine = tether::Engine::natural;
};

/** Sets every parameter from a PluginSettings (message thread). */
void applySettings (juce::AudioProcessorValueTreeState& state, const PluginSettings& settings);

/** Caches the parameters' atomics so the audio thread can read them lock-free. */
class ParameterReader
{
public:
    explicit ParameterReader (juce::AudioProcessorValueTreeState& state);

    tether::EngineParams read() const noexcept;
    tether::Resolution resolution() const noexcept;
    tether::Engine engine() const noexcept;

private:
    std::atomic<float>* get (juce::AudioProcessorValueTreeState& state, const char* id);

    std::atomic<float> *pitchAmount, *glide, *octave, *semitones, *fine, *detectLayer, *layerRoot, *formant,
                       *formantShift, *vibrato, *scale, *key, *level, *attack, *release, *punch, *gate,
                       *motion, *tone, *resolutionChoice, *engineChoice, *mix, *output, *listen;
};
