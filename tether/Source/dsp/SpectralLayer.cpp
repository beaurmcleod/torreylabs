#include "SpectralLayer.h"

namespace tether
{

void SpectralLayer::prepare (int maxFftSize)
{
    const auto n = (size_t) nextPowerOfTwo (maxFftSize);
    const auto bins = n / 2 + 1;

    window.assign (n, 0.0f);
    fftWork.assign (2 * n, 0.0f);
    cepstrum.assign (2 * n, 0.0f);
    envelope.assign (bins, 1.0f);
    magnitude.assign (bins, 0.0f);
    rotationPrev.assign (bins, 0.0f);
    rotationNext.assign (bins, 0.0f);
    peaks.assign (bins, 0);

    for (int c = 0; c < maxChannels; ++c)
    {
        spectrum[c].assign (bins, {});
        previous[c].assign (bins, {});
        shifted[c].assign (bins, {});
    }
}

void SpectralLayer::configure (double newSampleRate, int newFftSize, const juce::dsp::FFT* fftOfSize)
{
    jassert (fftOfSize != nullptr && fftOfSize->getSize() == newFftSize);
    jassert ((size_t) newFftSize <= window.size());

    fft = fftOfSize;
    sampleRate = newSampleRate;
    fftSize = newFftSize;
    numBins = fftSize / 2 + 1;
    hopSize = fftSize / 4;
    phasePerBinPerHop = kTwoPi * (float) hopSize / (float) fftSize;

    for (int n = 0; n < fftSize; ++n)
        window[(size_t) n] = 0.5f - 0.5f * std::cos (kTwoPi * (float) n / (float) fftSize);

    reset();
}

void SpectralLayer::reset() noexcept
{
    for (int c = 0; c < maxChannels; ++c)
    {
        std::fill (spectrum[c].begin(), spectrum[c].end(), Complex {});
        std::fill (previous[c].begin(), previous[c].end(), Complex {});
        std::fill (shifted[c].begin(), shifted[c].end(), Complex {});
    }

    std::fill (rotationPrev.begin(), rotationPrev.end(), 0.0f);
    std::fill (rotationNext.begin(), rotationNext.end(), 0.0f);
    std::fill (envelope.begin(), envelope.end(), 1.0f);
}

//==============================================================================
void SpectralLayer::processFrame (const float* const* layer, int numChannels, const FrameParams& params,
                                  float* const* synthesis) noexcept
{
    const int channels = std::clamp (numChannels, 1, maxChannels);
    const int n = fftSize, lastBin = numBins - 1;
    float* work = fftWork.data();

    for (int c = 0; c < channels; ++c)
    {
        for (int i = 0; i < n; ++i)
            work[i] = layer[c][i] * window[(size_t) i];

        std::fill (work + n, work + 2 * n, 0.0f);
        fft->performRealOnlyForwardTransform (work, true);

        for (int k = 0; k <= lastBin; ++k)
            spectrum[c][(size_t) k] = { work[2 * k], work[2 * k + 1] };
    }

    for (int k = 0; k <= lastBin; ++k)
    {
        float power = 0.0f;
        for (int c = 0; c < channels; ++c)
            power += std::norm (spectrum[c][(size_t) k]);

        magnitude[(size_t) k] = std::sqrt (power);
    }

    if (std::abs (params.ratio - 1.0f) > 1.0e-5f || std::abs (params.formantRatio - 1.0f) > 1.0e-5f)
    {
        shiftPitch (channels, params);
    }
    else
    {
        for (int c = 0; c < channels; ++c)
            std::copy (spectrum[c].begin(), spectrum[c].begin() + numBins, shifted[c].begin());

        std::fill (rotationPrev.begin(), rotationPrev.end(), 0.0f);
    }

    for (int c = 0; c < channels; ++c)
        std::swap (previous[c], spectrum[c]);

    // Hann analysis x Hann synthesis windows sum to 1.5 at 75 % overlap.
    constexpr float olaScale = 1.0f / 1.5f;

    for (int c = 0; c < channels; ++c)
    {
        for (int k = 0; k <= lastBin; ++k)
        {
            work[2 * k]     = shifted[c][(size_t) k].real();
            work[2 * k + 1] = shifted[c][(size_t) k].imag();
        }

        work[1] = 0.0f;
        work[2 * lastBin + 1] = 0.0f;

        fft->performRealOnlyInverseTransform (work);

        for (int i = 0; i < n; ++i)
            synthesis[c][i] = work[i] * window[(size_t) i] * olaScale;
    }
}

//==============================================================================
void SpectralLayer::shiftPitch (int channels, const FrameParams& params) noexcept
{
    const int lastBin = numBins - 1;

    for (int c = 0; c < channels; ++c)
        std::fill (shifted[c].begin(), shifted[c].begin() + numBins, Complex {});

    float maxMagnitude = 0.0f;
    for (int k = 0; k <= lastBin; ++k)
        maxMagnitude = std::max (maxMagnitude, magnitude[(size_t) k]);

    if (maxMagnitude < 1.0e-9f)
    {
        std::fill (rotationPrev.begin(), rotationPrev.end(), 0.0f);
        return;
    }

    // Peaks: larger than two neighbours on each side and within 100 dB of the maximum.
    const float floor = maxMagnitude * 1.0e-5f;
    int numPeaks = 0;

    for (int k = 2; k <= lastBin - 2; ++k)
    {
        const float m = magnitude[(size_t) k];

        if (m > floor
            && m > magnitude[(size_t) (k - 1)] && m > magnitude[(size_t) (k - 2)]
            && m >= magnitude[(size_t) (k + 1)] && m >= magnitude[(size_t) (k + 2)])
            peaks[(size_t) numPeaks++] = k;
    }

    if (numPeaks == 0)
    {
        int loudest = 0;
        for (int k = 1; k <= lastBin; ++k)
            if (magnitude[(size_t) k] > magnitude[(size_t) loudest])
                loudest = k;

        peaks[(size_t) numPeaks++] = loudest;
    }

    // A partial moved from bin k to k * ratio keeps its amplitude when the
    // envelope follows the pitch; otherwise it takes the envelope's value at
    // its new place, read through the wanted formant ratio.
    const float formant = std::clamp (params.formantRatio, 1.0f / 16.0f, 16.0f);
    const bool useEnvelope = std::abs (formant - params.ratio) > 1.0e-4f;

    if (useEnvelope)
        computeEnvelope (params.sourceHz);

    int regionStart = 0;

    for (int i = 0; i < numPeaks; ++i)
    {
        const int peak = peaks[(size_t) i];
        int regionEnd = lastBin;

        if (i + 1 < numPeaks)
        {
            regionEnd = peak + 1;
            for (int k = peak + 2; k < peaks[(size_t) (i + 1)]; ++k)
                if (magnitude[(size_t) k] < magnitude[(size_t) regionEnd])
                    regionEnd = k;
        }

        // True frequency of the peak (in bins) from the phase advance of its loudest channel.
        int loudestChannel = 0;
        for (int c = 1; c < channels; ++c)
            if (std::norm (spectrum[c][(size_t) peak]) > std::norm (spectrum[loudestChannel][(size_t) peak]))
                loudestChannel = c;

        const auto current = spectrum[loudestChannel][(size_t) peak];
        const auto before  = previous[loudestChannel][(size_t) peak];
        const double expected = std::fmod ((double) phasePerBinPerHop * peak, 2.0 * juce::MathConstants<double>::pi);
        const float deviation = princarg (std::arg (current * std::conj (before)) - (float) expected);
        const float trueBin = (float) peak + deviation / phasePerBinPerHop;

        // Move the whole region by the rounded shift; the phase rotation
        // accumulates the exact (fractional) shift so the partial lands on target.
        const float delta = (params.ratio - 1.0f) * trueBin;
        const int binShift = (int) std::lround (delta);
        const float theta = princarg (rotationPrev[(size_t) peak] + phasePerBinPerHop * delta);
        const Complex rotation = std::polar (1.0f, theta);

        for (int k = regionStart; k <= regionEnd; ++k)
        {
            rotationNext[(size_t) k] = theta;
            const int dest = k + binShift;

            if (dest < 0 || dest > lastBin)
                continue;

            auto factor = rotation;

            if (useEnvelope)
            {
                const int at = std::clamp ((int) std::lround ((float) dest / formant), 0, lastBin);
                factor *= std::clamp (envelope[(size_t) at] / envelope[(size_t) k], 1.0f / 16.0f, 16.0f);
            }

            for (int c = 0; c < channels; ++c)
                shifted[c][(size_t) dest] += spectrum[c][(size_t) k] * factor;
        }

        regionStart = regionEnd + 1;
    }

    std::swap (rotationPrev, rotationNext);
}

void SpectralLayer::computeEnvelope (float sourceHz) noexcept
{
    // Cepstral smoothing: the lifter keeps quefrencies below ~60 % of the pitch
    // period, so the envelope follows formants but not individual harmonics.
    const int n = fftSize, lastBin = numBins - 1;
    const float period = (float) sampleRate / (sourceHz > 20.0f ? sourceHz : 150.0f);
    const int upper = std::max (8, (int) (sampleRate / 350.0));
    const int cutoff = std::min (std::clamp ((int) (0.6f * period), 8, upper), n / 2 - 1);

    float* c = cepstrum.data();

    for (int k = 0; k <= lastBin; ++k)
    {
        c[2 * k] = std::log (magnitude[(size_t) k] + 1.0e-7f);
        c[2 * k + 1] = 0.0f;
    }

    fft->performRealOnlyInverseTransform (c);

    std::fill (c + cutoff + 1, c + n - cutoff, 0.0f);
    c[cutoff] *= 0.5f;
    c[n - cutoff] *= 0.5f;
    std::fill (c + n, c + 2 * n, 0.0f);

    fft->performRealOnlyForwardTransform (c, true);

    for (int k = 0; k <= lastBin; ++k)
        envelope[(size_t) k] = std::exp (c[2 * k]);
}

} // namespace tether
