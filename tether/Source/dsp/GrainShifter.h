#pragma once

#include "DspUtils.h"

#include <cstdint>
#include <cstdio>
#include <vector>

namespace tether
{

/** Pitch-synchronous overlap-add pitch and formant shifter (TD-PSOLA).

    Analysis places a mark on every period of the layer, each one aligned to
    the previous mark by waveform correlation. Synthesis cuts a two-period Hann
    grain around the latest mark before every output period and adds it at the
    output position; resampling the grain moves the formants independently of
    the pitch.

    The output period is set directly from the target frequency, so the output
    pitch is exact whatever the analysis does: analysis quality only affects
    the timbre. Everything runs on absolute sample counters and the output
    lags the input by a fixed `delay`, so the caller can hide the look-ahead
    inside its latency. Real-time safe after prepare(). */
class GrainShifter
{
public:
    static constexpr int maxChannels = 2;

    struct Controls
    {
        float analysisPeriod = 100.0f;  // the layer's own period in samples
        float outputPeriod = 100.0f;    // wanted output period in samples
        float formantRatio = 1.0f;      // 1 keeps the formants; the pitch ratio moves them with the pitch
        bool lockToInput = false;       // no shift wanted: follow the input marks exactly (transparent)
        float grainPeriods = 1.0f;      // grain half-length in periods (1 = classic two-period grains)
    };

    /** The smallest delay that lets marks and grains be complete in time. */
    static int minimumDelay (int maxPeriod, int hopSize) noexcept;

    /** Allocates for the largest values configure() will be given. */
    void prepare (int maxPeriod, int hopSize, int delay);

    /** maxPeriod: the longest layer period that will be asked for. hopSize:
        the block length of push() and render(). delay: how far behind the
        input the output runs (at least minimumDelay). Real-time safe. */
    void configure (int maxPeriod, int hopSize, int delay) noexcept;
    void reset() noexcept;

    /** Appends hopSize samples per channel. */
    void push (const float* const* input, int numChannels) noexcept;

    /** Writes the next hopSize output samples per channel. */
    void render (const Controls& controls, float* const* output, int numChannels) noexcept;

    int getDelay() const noexcept   { return delay; }

   #if TETHER_GRAIN_DEBUG
    void debugDump (FILE* f) const;
   #endif

private:
    struct Mark
    {
        double position = 0.0;  // absolute input sample (fractional)
        double spacing = 0.0;   // distance from the previous mark
    };

    struct Grain
    {
        double centre = 0.0;     // output position
        double source = 0.0;     // input mark it was cut around
        double halfLength = 0.0; // half length in output samples
        double rate = 1.0;       // input samples per output sample (formant ratio)
        float gain = 1.0f;
    };

    void placeMarks (double period, bool lock) noexcept;
    float sampleAt (int channel, double position) const noexcept;
    float monoAt (double position) const noexcept;
    static float interpolate (const float* ring, int mask, double position) noexcept;

    int channels = 2, hop = 0, delay = 0, maxPeriod = 16, minPeriod = 8;
    std::vector<float> ring[maxChannels], mono;
    int ringMask = 0;
    int64_t totalIn = 0;

    std::vector<Mark> marks;
    int markMask = 0;
    int64_t markCount = 0;
    int64_t grainMarkIndex = 0;
    double gridSpacing = 0.0;

    std::vector<Grain> grains;
    int grainMask = 0;
    int64_t grainHead = 0, grainTail = 0;
    double nextCentre = 0.0;
    bool started = false;

    std::vector<float> reference, candidates, referenceCoarse, candidatesCoarse, scores;
};

} // namespace tether
