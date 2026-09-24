#pragma once

#include "DspUtils.h"

#include <complex>

#include <juce_dsp/juce_dsp.h>

namespace tether
{

struct PitchEstimate
{
    float hz = 0.0f;            // fundamental estimate (meaningful when voiced)
    float aperiodicity = 1.0f;  // YIN d' at the chosen lag: 0 = perfectly periodic
    float levelDb = -120.0f;    // RMS level of the analysis window
    bool voiced = false;
    bool stable = true;         // false when the window straddles an onset or a cut-off
};

/** Monophonic f0 estimator: YIN (de Cheveigne & Kawahara, 2002) with the
    difference function computed through an FFT cross-correlation. */
class PitchTracker
{
public:
    void prepare (int maxWindowSize);

    /** windowSize must be a power of two and match the FFT's size. */
    void configure (double sampleRate, int windowSize, float minHz, float maxHz, const juce::dsp::FFT* fftOfWindowSize);

    /** Analyses `windowSize` samples. Real-time safe. */
    PitchEstimate analyse (const float* window) noexcept;

    struct Partial
    {
        float hz = 0.0f;          // measured frequency of the partial
        double amplitude = 0.0;   // its peak amplitude
        bool valid = false;
    };

    /** Measures the partial nearest `hz` (within about +-3 semitones): the
        window is shifted down by hz and averaged over exactly one period, which
        nulls every other harmonic; the phase drift that is left gives the
        frequency. The fundamental is far less disturbed by moving resonances
        (filter sweeps, formants) than the waveform as a whole, so this is used
        to refine YIN's estimate. */
    Partial measurePartial (const float* window, float hz) noexcept;

    float threshold    = 0.15f;  // YIN absolute threshold
    float voicingLimit = 0.35f;  // highest aperiodicity still treated as pitched
    float silenceDb    = -65.0f; // windows quieter than this are unvoiced

private:
    const juce::dsp::FFT* fft = nullptr;
    double sampleRate = 48000.0;
    int size = 0, tauMin = 2, tauMax = 2, integration = 0;
    float lowestHz = 30.0f, highestHz = 2000.0f;
    std::vector<float> full, head, diff, cmnd;
    std::vector<std::complex<double>> boxSums;
    std::vector<double> boxEnergies;
};

/** Turns frame-by-frame estimates into a stable pitch: holds the last good
    value through unvoiced frames and waits one frame before trusting a
    sudden octave jump (the classic YIN failure mode) in the middle of a note. */
class PitchFollower
{
public:
    void reset() noexcept;
    void update (const PitchEstimate& estimate) noexcept;

    bool hasPitch() const noexcept   { return has; }
    bool isVoiced() const noexcept   { return voicedNow; }
    float getHz() const noexcept     { return hz; }

    /** How many consistent frames a jump of more than a semitone needs before
        it's believed. Leaps of more than 9 semitones (octave errors look like
        this) use the second value unless the estimate is very clean. */
    void setConfirmationFrames (int jumps, int largeJumps) noexcept
    {
        jumpFrames = std::max (1, jumps);
        largeJumpFrames = std::max (1, largeJumps);
    }

    int framesBeforeFreshStart = 12; // after this many unvoiced frames a new phrase starts fresh

private:
    float hz = 0.0f, pendingHz = 0.0f;
    bool has = false, voicedNow = false;
    int pendingCount = 0, framesSinceVoiced = 1 << 20;
    int jumpFrames = 1, largeJumpFrames = 3;
};

} // namespace tether
