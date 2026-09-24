#pragma once

#include "dsp/TetherEngine.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace ParamIDs
{
    inline constexpr auto pitchAmount = "pitchAmount";
    inline constexpr auto glide       = "glide";
    inline constexpr auto octave      = "octave";
    inline constexpr auto semitones   = "semitones";
    inline constexpr auto detectLayer = "detectLayer";
    inline constexpr auto layerRoot   = "layerRoot";
    inline constexpr auto formant     = "formant";
    inline constexpr auto level       = "level";
    inline constexpr auto attack      = "attack";
    inline constexpr auto release     = "release";
    inline constexpr auto punch       = "punch";
    inline constexpr auto gate        = "gate";
    inline constexpr auto motion      = "motion";
    inline constexpr auto tone        = "tone";
    inline constexpr auto resolution  = "resolution";
    inline constexpr auto mix         = "mix";
    inline constexpr auto output      = "output";
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

/** Note name in the convention used by Ableton Live and Logic (MIDI 60 = C3). */
juce::String noteName (int midiNote);

/** Caches the parameters' atomics so the audio thread can read them lock-free. */
class ParameterReader
{
public:
    explicit ParameterReader (juce::AudioProcessorValueTreeState& state);

    tether::EngineParams read() const noexcept;
    tether::Resolution resolution() const noexcept;

private:
    std::atomic<float>* get (juce::AudioProcessorValueTreeState& state, const char* id);

    std::atomic<float> *pitchAmount, *glide, *octave, *semitones, *detectLayer, *layerRoot, *formant,
                       *level, *attack, *release, *punch, *gate, *motion, *tone, *resolutionChoice,
                       *mix, *output;
};
