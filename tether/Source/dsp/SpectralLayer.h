#pragma once

#include "DspUtils.h"

#include <juce_dsp/juce_dsp.h>

#include <complex>

namespace tether
{

/** Frame-by-frame spectral processing of the layer:

    1. Pitch shift with a peak-locked phase vocoder (Laroche & Dolson, 1999):
       every spectral peak and its region of influence is moved rigidly to the
       target frequency and rotated by an accumulated phase, which keeps
       partials coherent. The same rotation is applied to every channel so the
       stereo image survives.
    2. Optional formant preservation using a cepstrally smoothed envelope.
    3. "Motion" / "Tone": the guide's spectral shape (per ~1/3-octave band) is
       compared with the layer's; its movement over time (and optionally its
       average tone) is imposed on the layer, loudness-neutral.

    The caller owns framing and overlap-add (hop = N / 4, Hann analysis and
    synthesis windows). Everything here is real-time safe after prepare(). */
class SpectralLayer
{
public:
    static constexpr int maxChannels = 2;
    static constexpr int maxBands = 48;

    struct FrameParams
    {
        float ratio = 1.0f;           // frequency ratio applied to the layer
        bool preserveFormants = false;
        float sourceHz = 0.0f;        // layer pitch before shifting (0 = unknown)
        float motion = 0.0f;          // 0..1
        float tone = 0.0f;            // 0..1
        bool guidePresent = false;
    };

    void prepare (int maxFftSize);
    void configure (double sampleRate, int fftSize, const juce::dsp::FFT* fftOfSize);
    void reset() noexcept;

    /** layer: numChannels frames of N raw samples. guide: N raw samples (may be null).
        synthesis: receives N windowed samples per channel, ready to overlap-add. */
    void processFrame (const float* const* layer, int numChannels, const float* guide,
                       const FrameParams& params, float* const* synthesis) noexcept;

    int getNumBands() const noexcept                 { return numBands; }
    float getBandGainDb (int band) const noexcept    { return bandGainDb[band]; }

private:
    using Complex = std::complex<float>;

    void shiftPitch (int numChannels, const FrameParams& params) noexcept;
    void computeEnvelope (float sourceHz) noexcept;
    void applyMotion (int numChannels, const float* guide, const FrameParams& params) noexcept;
    void buildBands();

    const juce::dsp::FFT* fft = nullptr;
    double sampleRate = 48000.0;
    int fftSize = 0, numBins = 0, hopSize = 0;
    float phasePerBinPerHop = 0.0f;

    std::vector<float> window, fftWork, envelope, cepstrum, magnitude, guidePower, binGain;
    std::vector<float> rotationPrev, rotationNext;
    std::vector<int> peaks;
    std::vector<Complex> spectrum[maxChannels], previous[maxChannels], shifted[maxChannels];

    int numBands = 0;
    int bandEdges[maxBands + 1] {};
    float bandCentres[maxBands] {};
    float guideShort[maxBands] {}, guideLong[maxBands] {};
    float layerShort[maxBands] {}, layerLong[maxBands] {};
    float bandGainDb[maxBands] {};
    float guideEnergy[maxBands] {}, layerEnergy[maxBands] {};
    int guideFrames = 0, layerFrames = 0;
};

} // namespace tether
