#pragma once

#include "DspUtils.h"

#include <juce_dsp/juce_dsp.h>

namespace tether
{

/** "Motion" / "Tone": compares the guide's spectral shape (per ~1/3-octave
    band) with the layer's, once per hop, and works out a per-band gain that
    imposes the guide's movement over time (and optionally its average tone)
    on the layer, loudness-neutral. MotionEq applies the result. */
class MotionAnalyser
{
public:
    static constexpr int maxBands = 48;

    struct Params
    {
        float motion = 0.0f;   // 0..1
        float tone = 0.0f;     // 0..1
        bool guidePresent = false;
    };

    void prepare (int maxFftSize);
    void configure (double sampleRate, int fftSize, const juce::dsp::FFT* fftOfSize);
    void reset() noexcept;

    /** guide and layer: fftSize raw samples each (guide may be null). The
        layer is the signal the EQ will be applied to. */
    void analyse (const float* guide, const float* layer, const Params& params) noexcept;

    int getNumBands() const noexcept                 { return numBands; }
    float getBandGainDb (int band) const noexcept    { return bandGainDb[band]; }
    float getNormalisationDb() const noexcept        { return normDb; }
    bool isActive() const noexcept                   { return active; }
    float getBandLowHz (int band) const noexcept     { return (float) (bandEdges[band] * sampleRate / fftSize); }
    float getBandHighHz (int band) const noexcept    { return (float) (bandEdges[band + 1] * sampleRate / fftSize); }

private:
    void buildBands();
    void powerSpectrum (const float* input, float* power) noexcept;

    const juce::dsp::FFT* fft = nullptr;
    double sampleRate = 48000.0;
    int fftSize = 0, numBins = 0, hopSize = 0;

    std::vector<float> window, work, guidePower, layerPower;

    int numBands = 0;
    int bandEdges[maxBands + 1] {};
    float guideShort[maxBands] {}, guideLong[maxBands] {};
    float layerShort[maxBands] {}, layerLong[maxBands] {};
    float bandGainDb[maxBands] {};
    float guideEnergy[maxBands] {}, layerEnergy[maxBands] {};
    float normDb = 0.0f;
    bool active = false;
    int guideFrames = 0, layerFrames = 0;
};

/** Cascade of peaking filters, one per band, that realises the analyser's
    gains in the time domain. Coefficients glide between updates so gain
    changes never click; with every gain at 0 dB it is bit-transparent. */
class MotionEq
{
public:
    static constexpr int maxBands = MotionAnalyser::maxBands;
    static constexpr int maxChannels = 2;

    void configure (double sampleRate, const MotionAnalyser& bands);
    void reset() noexcept;

    /** New targets (dB per band, from the analyser); reached after rampSamples. */
    void setTargets (const MotionAnalyser& analyser, int rampSamples) noexcept;

    /** Processes one sample of one channel. Call advance() once per sample
        after all channels. */
    float process (int channel, float x) noexcept;
    void advance() noexcept;

    bool isBypassed() const noexcept   { return bypassed; }

private:
    struct Coeffs { float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f; };

    struct Band
    {
        double centreHz = 1000.0, q = 4.0, cosW = 0.0, alpha = 0.0;
        float targetDb = 0.0f;
        double currentA = 1.0, stepA = 0.0;   // 10^(dB/40), interpolated per sample
        Coeffs current;
        bool active = false;
        float z1[maxChannels] {}, z2[maxChannels] {};
    };

    static Coeffs peaking (const Band& band, double A) noexcept;
    static double responseDb (const Coeffs& c, double hz, double sampleRate) noexcept;

    double sampleRate = 48000.0;
    int numBands = 0, rampRemaining = 0;
    Band bands[maxBands];
    float coupling[maxBands][maxBands] {};   // dB response of band k's filter at band b's centre, per dB of gain
    float filterDb[maxBands] {};
    bool bypassed = true;
};

} // namespace tether
