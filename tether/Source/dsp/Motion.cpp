#include "Motion.h"

namespace tether
{

void MotionAnalyser::prepare (int maxFftSize)
{
    const auto n = (size_t) nextPowerOfTwo (maxFftSize);
    window.assign (n, 0.0f);
    work.assign (2 * n, 0.0f);
    guidePower.assign (n / 2 + 1, 0.0f);
    layerPower.assign (n / 2 + 1, 0.0f);
}

void MotionAnalyser::configure (double newSampleRate, int newFftSize, const juce::dsp::FFT* fftOfSize)
{
    jassert (fftOfSize != nullptr && fftOfSize->getSize() == newFftSize);
    jassert ((size_t) newFftSize <= window.size());

    fft = fftOfSize;
    sampleRate = newSampleRate;
    fftSize = newFftSize;
    numBins = fftSize / 2 + 1;
    hopSize = fftSize / 4;

    for (int n = 0; n < fftSize; ++n)
        window[(size_t) n] = 0.5f - 0.5f * std::cos (kTwoPi * (float) n / (float) fftSize);

    buildBands();
    reset();
}

void MotionAnalyser::reset() noexcept
{
    std::fill (guidePower.begin(), guidePower.end(), 0.0f);
    std::fill (layerPower.begin(), layerPower.end(), 0.0f);

    for (int b = 0; b < maxBands; ++b)
    {
        guideShort[b] = guideLong[b] = layerShort[b] = layerLong[b] = 0.0f;
        bandGainDb[b] = guideEnergy[b] = layerEnergy[b] = 0.0f;
    }

    normDb = 0.0f;
    active = false;
    guideFrames = layerFrames = 0;
}

void MotionAnalyser::buildBands()
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
}

void MotionAnalyser::powerSpectrum (const float* input, float* power) noexcept
{
    const int n = fftSize;
    float* w = work.data();

    for (int i = 0; i < n; ++i)
        w[i] = input[i] * window[(size_t) i];

    std::fill (w + n, w + 2 * n, 0.0f);
    fft->performRealOnlyForwardTransform (w, true);

    for (int k = 0; k < numBins; ++k)
        power[k] = w[2 * k] * w[2 * k] + w[2 * k + 1] * w[2 * k + 1];
}

void MotionAnalyser::analyse (const float* guide, const float* layer, const Params& params) noexcept
{
    if (fft == nullptr || numBands == 0)
        return;

    const int n = fftSize;
    const double frameRate = sampleRate / hopSize;

    // Converts a one-sided sum of |X|^2 of a Hann-windowed frame into mean-square.
    const float toMeanSquare = 1.0f / (0.1875f * (float) n * (float) n);
    constexpr float silence = 1.0e-9f; // -90 dBFS

    if (guide != nullptr && params.guidePresent)
        powerSpectrum (guide, guidePower.data());
    else
        std::fill (guidePower.begin(), guidePower.end(), 0.0f);

    powerSpectrum (layer, layerPower.data());

    float totalGuide = 0.0f, totalLayer = 0.0f;

    for (int b = 0; b < numBands; ++b)
    {
        float g = 0.0f, l = 0.0f;

        for (int k = bandEdges[b]; k < bandEdges[b + 1]; ++k)
        {
            g += guidePower[(size_t) k];
            l += layerPower[(size_t) k];
        }

        guideEnergy[b] = g;
        layerEnergy[b] = l;
        totalGuide += g;
        totalLayer += l;
    }

    const bool guideActive = totalGuide * toMeanSquare > silence;
    const bool layerActive = totalLayer * toMeanSquare > silence;

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

            const float target = std::clamp (params.motion * movement + params.tone * toneOffset, -24.0f, 18.0f);
            bandGainDb[b] += gainCoeff * (target - bandGainDb[b]);
        }
        else
        {
            bandGainDb[b] -= relaxCoeff * bandGainDb[b];
        }

        if (std::abs (bandGainDb[b]) < 1.0e-4f)
            bandGainDb[b] = 0.0f;
        else
            anyGain = true;
    }

    active = anyGain;
    normDb = 0.0f;

    if (! anyGain)
        return;

    // Keep the overall layer energy unchanged: loudness is the level stage's job.
    double weighted = 0.0, plain = 0.0;
    for (int b = 0; b < numBands; ++b)
    {
        weighted += (double) layerEnergy[b] * std::pow (10.0, bandGainDb[b] / 10.0);
        plain += layerEnergy[b];
    }

    if (plain > 0.0 && weighted > 0.0)
        normDb = (float) (10.0 * std::log10 (weighted / plain));
}

//==============================================================================
MotionEq::Coeffs MotionEq::peaking (const Band& band, double A) noexcept
{
    // RBJ peaking EQ; the bandwidth is measured at half the gain (in dB).
    // Stable for any A > 0, so A can be interpolated freely.
    const double a0 = 1.0 + band.alpha / A;
    const double norm = 1.0 / a0;

    Coeffs c;
    c.b0 = (float) ((1.0 + band.alpha * A) * norm);
    c.b1 = (float) (-2.0 * band.cosW * norm);
    c.b2 = (float) ((1.0 - band.alpha * A) * norm);
    c.a1 = c.b1;
    c.a2 = (float) ((1.0 - band.alpha / A) * norm);
    return c;
}

double MotionEq::responseDb (const Coeffs& c, double hz, double sampleRate) noexcept
{
    const double w = kTwoPi * hz / sampleRate;
    const std::complex<double> z1 = std::polar (1.0, -w), z2 = z1 * z1;
    const auto num = (double) c.b0 + (double) c.b1 * z1 + (double) c.b2 * z2;
    const auto den = 1.0 + (double) c.a1 * z1 + (double) c.a2 * z2;
    return 20.0 * std::log10 (std::abs (num / den) + 1.0e-12);
}

void MotionEq::configure (double newSampleRate, const MotionAnalyser& analyser)
{
    sampleRate = newSampleRate;
    numBands = analyser.getNumBands();
    const double top = 0.47 * sampleRate;

    for (int b = 0; b < numBands; ++b)
    {
        const double lo = std::max (20.0, (double) analyser.getBandLowHz (b));
        const double hi = std::min (top, std::max (lo * 1.05, (double) analyser.getBandHighHz (b)));
        Band& band = bands[b];
        band.centreHz = std::sqrt (lo * hi);
        band.q = std::clamp (band.centreHz / (hi - lo), 0.5, 12.0);
        const double w0 = kTwoPi * band.centreHz / sampleRate;
        band.cosW = std::cos (w0);
        band.alpha = std::sin (w0) / (2.0 * band.q);
    }

    // How much each band's filter spills into its neighbours, per dB of gain,
    // so the cascade can be solved to hit the wanted gains at the centres.
    for (int k = 0; k < numBands; ++k)
    {
        const auto probe = peaking (bands[k], std::pow (10.0, 6.0 / 40.0));
        for (int b = 0; b < numBands; ++b)
            coupling[b][k] = (float) (responseDb (probe, bands[b].centreHz, sampleRate) / 6.0);
    }

    reset();
}

void MotionEq::reset() noexcept
{
    for (auto& band : bands)
    {
        band.targetDb = 0.0f;
        band.currentA = 1.0;
        band.stepA = 0.0;
        band.current = Coeffs {};
        band.active = false;
        for (int c = 0; c < maxChannels; ++c)
            band.z1[c] = band.z2[c] = 0.0f;
    }

    std::fill (std::begin (filterDb), std::end (filterDb), 0.0f);
    rampRemaining = 0;
    bypassed = true;
}

void MotionEq::setTargets (const MotionAnalyser& analyser, int rampSamples) noexcept
{
    const int ramp = std::max (1, rampSamples);
    const bool wanted = analyser.isActive();
    const float norm = analyser.getNormalisationDb();

    // Solve coupling * filterDb = wanted gains (under-relaxed Gauss-Seidel,
    // warm-started from the previous hop).
    for (int sweep = 0; sweep < 3; ++sweep)
    {
        for (int b = 0; b < numBands; ++b)
        {
            float sum = wanted ? analyser.getBandGainDb (b) - norm : 0.0f;
            for (int k = 0; k < numBands; ++k)
                if (k != b)
                    sum -= coupling[b][k] * filterDb[k];

            const float solved = std::clamp (sum / std::max (0.25f, coupling[b][b]), -30.0f, 30.0f);
            filterDb[b] += 0.6f * (solved - filterDb[b]);
        }
    }

    bool anyActive = false;

    for (int b = 0; b < numBands; ++b)
    {
        Band& band = bands[b];
        const bool wasFlat = std::abs (band.targetDb) < 1.0e-9f;
        band.targetDb = wanted ? filterDb[b] : 0.0f;

        if (std::abs (band.targetDb) < 0.005f)
            band.targetDb = 0.0f;

        const bool flatNow = std::abs (band.targetDb) < 1.0e-9f;

        if (! band.active)
        {
            if (flatNow)
                continue;

            band.active = true;
            band.currentA = 1.0;
            band.current = Coeffs {};
        }

        const double targetA = flatNow ? 1.0 : std::pow (10.0, (double) band.targetDb / 40.0);
        band.stepA = (targetA - band.currentA) / (double) ramp;

        // A band that has settled at 0 dB with an empty state can drop out.
        if (flatNow && wasFlat)
        {
            float energy = 0.0f;
            for (int c = 0; c < maxChannels; ++c)
                energy += std::abs (band.z1[c]) + std::abs (band.z2[c]);

            if (energy < 1.0e-7f)
            {
                band.active = false;
                band.currentA = 1.0;
                band.stepA = 0.0;
                band.current = Coeffs {};
                for (int c = 0; c < maxChannels; ++c)
                    band.z1[c] = band.z2[c] = 0.0f;
                continue;
            }
        }

        anyActive = true;
    }

    rampRemaining = ramp;
    bypassed = ! anyActive;
}

float MotionEq::process (int channel, float x) noexcept
{
    for (int b = 0; b < numBands; ++b)
    {
        Band& band = bands[b];

        if (! band.active)
            continue;

        const Coeffs& c = band.current;
        const float y = c.b0 * x + band.z1[channel];
        band.z1[channel] = c.b1 * x - c.a1 * y + band.z2[channel];
        band.z2[channel] = c.b2 * x - c.a2 * y;
        x = y;
    }

    return x;
}

void MotionEq::advance() noexcept
{
    if (rampRemaining <= 0)
        return;

    --rampRemaining;

    for (int b = 0; b < numBands; ++b)
    {
        Band& band = bands[b];

        if (! band.active)
            continue;

        band.currentA = std::max (1.0e-3, band.currentA + band.stepA);
        band.current = peaking (band, band.currentA);
    }
}

} // namespace tether
