#pragma once

#include "DspUtils.h"

#include <juce_dsp/juce_dsp.h>

#include <complex>

namespace tether
{

/** Frame-by-frame spectral pitch shifting of the layer ("Spectral" engine):
    a peak-locked phase vocoder (Laroche & Dolson, 1999). Every spectral peak
    and its region of influence is moved rigidly to the target frequency and
    rotated by an accumulated phase, which keeps partials coherent. The same
    rotation is applied to every channel so the stereo image survives. The
    spectral envelope (cepstrally smoothed) can be kept, moved with the pitch
    or shifted on its own.

    The caller owns framing and overlap-add (hop = N / 4, Hann analysis and
    synthesis windows). Everything here is real-time safe after prepare(). */
class SpectralLayer
{
public:
    static constexpr int maxChannels = 2;

    struct FrameParams
    {
        float ratio = 1.0f;          // frequency ratio applied to the layer
        float formantRatio = 1.0f;   // where the spectral envelope goes: 1 = stays, ratio = follows the pitch
        float sourceHz = 0.0f;       // layer pitch before shifting (0 = unknown)
    };

    void prepare (int maxFftSize);
    void configure (double sampleRate, int fftSize, const juce::dsp::FFT* fftOfSize);
    void reset() noexcept;

    /** layer: numChannels frames of N raw samples. synthesis: receives N
        windowed samples per channel, ready to overlap-add. */
    void processFrame (const float* const* layer, int numChannels, const FrameParams& params,
                       float* const* synthesis) noexcept;

private:
    using Complex = std::complex<float>;

    void shiftPitch (int numChannels, const FrameParams& params) noexcept;
    void computeEnvelope (float sourceHz) noexcept;

    const juce::dsp::FFT* fft = nullptr;
    double sampleRate = 48000.0;
    int fftSize = 0, numBins = 0, hopSize = 0;
    float phasePerBinPerHop = 0.0f;

    std::vector<float> window, fftWork, envelope, cepstrum, magnitude;
    std::vector<float> rotationPrev, rotationNext;
    std::vector<int> peaks;
    std::vector<Complex> spectrum[maxChannels], previous[maxChannels], shifted[maxChannels];
};

} // namespace tether
