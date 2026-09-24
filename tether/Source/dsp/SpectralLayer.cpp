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
    guidePower.assign (bins, 0.0f);
    binGain.assign (bins, 1.0f);
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

    buildBands();
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
    std::fill (binGain.begin(), binGain.end(), 1.0f);

    for (int b = 0; b < maxBands; ++b)
    {
        guideShort[b] = guideLong[b] = layerShort[b] = layerLong[b] = 0.0f;
        bandGainDb[b] = guideEnergy[b] = layerEnergy[b] = 0.0f;
    }

    guideFrames = layerFrames = 0;
}

void SpectralLayer::buildBands()
{
    // Roughly third-octave bands from 50 Hz up, each at least two bins wide.
    int count = 1;
    bandEdges[0] = 0;

    const double highest = std::min (16000.0, 0.46 * sampleRate);

    for (double f = 50.0; f < highest && count < maxBands; f *= 1.2599210498948732)
    {
        const int bin = (int) std::lround (f * fftSize / sampleRate);

        if (bin >= bandEdges[count - 1] + 2)
            bandEdges[count++] = bin;
    }

    if (count > 1 && numBins - bandEdges[count - 1] < 2)
        --count;

    bandEdges[count] = numBins;
    numBands = count;

    for (int b = 0; b < numBands; ++b)
        bandCentres[b] = 0.5f * (float) (bandEdges[b] + bandEdges[b + 1] - 1);
}

//==============================================================================
void SpectralLayer::processFrame (const float* const* layer, int numChannels, const float* guide,
                                  const FrameParams& params, float* const* synthesis) noexcept
{
    const int channels = juce::jlimit (1, maxChannels, numChannels);
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

    if (std::abs (params.ratio - 1.0f) > 1.0e-5f)
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

    applyMotion (channels, guide, params);

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

    if (params.preserveFormants)
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

            if (params.preserveFormants)
                factor *= juce::jlimit (1.0f / 16.0f, 16.0f, envelope[(size_t) dest] / envelope[(size_t) k]);

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
    const int cutoff = std::min (juce::jlimit (8, upper, (int) (0.6f * period)), n / 2 - 1);

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

//==============================================================================
void SpectralLayer::applyMotion (int channels, const float* guide, const FrameParams& params) noexcept
{
    const int n = fftSize, lastBin = numBins - 1;
    const double frameRate = sampleRate / hopSize;

    // Converts a one-sided sum of |X|^2 of a Hann-windowed frame into mean-square.
    const float toMeanSquare = 1.0f / (0.1875f * (float) n * (float) n);
    constexpr float silence = 1.0e-9f; // -90 dBFS

    float totalGuide = 0.0f, totalLayer = 0.0f;

    if (guide != nullptr && params.guidePresent)
    {
        float* work = fftWork.data();

        for (int i = 0; i < n; ++i)
            work[i] = guide[i] * window[(size_t) i];

        std::fill (work + n, work + 2 * n, 0.0f);
        fft->performRealOnlyForwardTransform (work, true);

        for (int k = 0; k <= lastBin; ++k)
            guidePower[(size_t) k] = work[2 * k] * work[2 * k] + work[2 * k + 1] * work[2 * k + 1];
    }
    else
    {
        std::fill (guidePower.begin(), guidePower.end(), 0.0f);
    }

    for (int b = 0; b < numBands; ++b)
    {
        float g = 0.0f, l = 0.0f;

        for (int k = bandEdges[b]; k < bandEdges[b + 1]; ++k)
        {
            g += guidePower[(size_t) k];

            for (int c = 0; c < channels; ++c)
                l += std::norm (shifted[c][(size_t) k]);
        }

        guideEnergy[b] = g;
        layerEnergy[b] = l;
        totalGuide += g;
        totalLayer += l;
    }

    const bool guideActive = totalGuide * toMeanSquare > silence;
    const bool layerActive = totalLayer * toMeanSquare / (float) channels > silence;

    if (guideActive) guideFrames = std::min (guideFrames + 1, 1 << 30);
    if (layerActive) layerFrames = std::min (layerFrames + 1, 1 << 30);

    const float shortCoeff = onePoleCoeff (0.02f, frameRate);
    const float longCoeff  = onePoleCoeff (4.0f, frameRate);
    const float gainCoeff  = onePoleCoeff (0.03f, frameRate);
    const float relaxCoeff = onePoleCoeff (0.15f, frameRate);

    // Plain running mean until the exponential average has warmed up.
    const float guideLongCoeff = std::max (longCoeff, 1.0f / (float) std::max (1, guideFrames));
    const float layerLongCoeff = std::max (longCoeff, 1.0f / (float) std::max (1, layerFrames));

    // Band shapes are relative to the frame's total energy, floored at -50 dB
    // so empty bands read as "quiet", not as minus infinity.
    constexpr float shapeFloor = 1.0e-5f;
    bool anyGain = false;

    for (int b = 0; b < numBands; ++b)
    {
        if (guideActive)
        {
            const float shape = 10.0f * std::log10 (guideEnergy[b] / totalGuide + shapeFloor);
            guideShort[b] = guideFrames == 1 ? shape : guideShort[b] + shortCoeff * (shape - guideShort[b]);
            guideLong[b] += guideLongCoeff * (guideShort[b] - guideLong[b]);
        }

        if (layerActive)
        {
            const float shape = 10.0f * std::log10 (layerEnergy[b] / totalLayer + shapeFloor);
            layerShort[b] = layerFrames == 1 ? shape : layerShort[b] + shortCoeff * (shape - layerShort[b]);
            layerLong[b] += layerLongCoeff * (layerShort[b] - layerLong[b]);
        }

        if (guideActive && layerActive)
        {
            // Replace the layer's own spectral movement with the guide's...
            const float movement = (guideShort[b] - guideLong[b]) - (layerShort[b] - layerLong[b]);
            // ...and optionally pull its average tone towards the guide's.
            const float toneOffset = guideLong[b] - layerLong[b];

            const float target = juce::jlimit (-24.0f, 18.0f, params.motion * movement + params.tone * toneOffset);
            bandGainDb[b] += gainCoeff * (target - bandGainDb[b]);
        }
        else
        {
            bandGainDb[b] -= relaxCoeff * bandGainDb[b];
        }

        anyGain = anyGain || std::abs (bandGainDb[b]) > 0.01f;
    }

    if (! anyGain || numBands == 0)
        return;

    // Keep the overall layer energy unchanged: loudness is the level stage's job.
    double weighted = 0.0, plain = 0.0;
    for (int b = 0; b < numBands; ++b)
    {
        weighted += (double) layerEnergy[b] * std::pow (10.0, bandGainDb[b] / 10.0);
        plain += layerEnergy[b];
    }

    const float normDb = (plain > 0.0 && weighted > 0.0) ? (float) (10.0 * std::log10 (weighted / plain)) : 0.0f;

    int band = 0;
    for (int k = 0; k <= lastBin; ++k)
    {
        while (band + 1 < numBands && (float) k >= bandCentres[band + 1])
            ++band;

        float db;

        if ((float) k <= bandCentres[0])
            db = bandGainDb[0];
        else if (band + 1 >= numBands)
            db = bandGainDb[numBands - 1];
        else
        {
            const float t = ((float) k - bandCentres[band]) / (bandCentres[band + 1] - bandCentres[band]);
            db = bandGainDb[band] + t * (bandGainDb[band + 1] - bandGainDb[band]);
        }

        binGain[(size_t) k] = dbToGain (db - normDb);
    }

    for (int c = 0; c < channels; ++c)
        for (int k = 0; k <= lastBin; ++k)
            shifted[c][(size_t) k] *= binGain[(size_t) k];
}

} // namespace tether
