#include "PitchTracker.h"

#include <complex>

namespace tether
{

void PitchTracker::prepare (int maxWindowSize)
{
    const auto n = (size_t) nextPowerOfTwo (maxWindowSize);
    full.assign (2 * n, 0.0f);
    head.assign (2 * n, 0.0f);
    diff.assign (n + 2, 0.0f);
    cmnd.assign (n + 2, 1.0f);
    boxSums.assign (n + 1, {});
    boxEnergies.assign (n + 1, 0.0);
}

void PitchTracker::configure (double newSampleRate, int windowSize, float minHz, float maxHz,
                              const juce::dsp::FFT* fftOfWindowSize)
{
    jassert (fftOfWindowSize != nullptr && fftOfWindowSize->getSize() == windowSize);
    jassert ((size_t) windowSize * 2 <= full.size());

    fft = fftOfWindowSize;
    sampleRate = newSampleRate;
    size = windowSize;
    lowestHz = minHz;
    highestHz = maxHz;

    // Leave at least a third of the window for the integration period.
    tauMax = std::min ((int) std::ceil (sampleRate / minHz) + 2, (size * 2) / 3);
    tauMin = std::max (2, (int) std::floor (sampleRate / maxHz));
    // The +1 keeps tauMax + 1 (used for interpolation) inside the window.
    integration = size - tauMax - 1;
}

PitchEstimate PitchTracker::analyse (const float* x) noexcept
{
    PitchEstimate result;

    if (fft == nullptr)
        return result;

    const int n = size, w = integration;

    double sumSquares = 0.0;
    for (int i = 0; i < n; ++i)
        sumSquares += (double) x[i] * x[i];

    result.levelDb = energyToDb ((float) (sumSquares / n));

    if (result.levelDb < silenceDb)
        return result;

    // A window that is much louder at one end than the other is catching a note
    // starting or stopping; its estimate is less trustworthy.
    {
        const int quarter = n / 4;
        double first = 1.0e-12, last = 1.0e-12;
        for (int i = 0; i < quarter; ++i)
        {
            first += (double) x[i] * x[i];
            last += (double) x[n - 1 - i] * x[n - 1 - i];
        }
        result.stable = std::max (first, last) < 20.0 * std::min (first, last);
    }

    // Cross-correlation r(tau) = sum_{j<w} x[j] x[j + tau] via FFT. No
    // wrap-around can occur because j + tau never exceeds n - 1.
    std::copy (x, x + n, full.begin());
    std::fill (full.begin() + n, full.begin() + 2 * n, 0.0f);
    std::copy (x, x + w, head.begin());
    std::fill (head.begin() + w, head.begin() + 2 * n, 0.0f);

    fft->performRealOnlyForwardTransform (full.data(), true);
    fft->performRealOnlyForwardTransform (head.data(), true);

    for (int k = 0; k <= n / 2; ++k)
    {
        const float hr = head[(size_t) (2 * k)], hi = head[(size_t) (2 * k + 1)];
        const float xr = full[(size_t) (2 * k)], xi = full[(size_t) (2 * k + 1)];
        full[(size_t) (2 * k)]     = hr * xr + hi * xi;
        full[(size_t) (2 * k + 1)] = hr * xi - hi * xr;
    }

    fft->performRealOnlyInverseTransform (full.data());

    // Difference function d(tau) and its cumulative-mean-normalised form d'(tau).
    double e0 = 0.0;
    for (int j = 0; j < w; ++j)
        e0 += (double) x[j] * x[j];

    double eTau = e0, running = 0.0;
    diff[0] = 0.0f;
    cmnd[0] = 1.0f;

    for (int tau = 1; tau <= tauMax + 1; ++tau)
    {
        eTau += (double) x[tau + w - 1] * x[tau + w - 1] - (double) x[tau - 1] * x[tau - 1];
        const double d = std::max (0.0, e0 + eTau - 2.0 * (double) full[(size_t) tau]);
        running += d;
        diff[(size_t) tau] = (float) d;
        cmnd[(size_t) tau] = running > 0.0 ? (float) (d * tau / running) : 1.0f;
    }

    // First dip under the threshold, walked down to its local minimum.
    int best = -1;

    for (int tau = tauMin; tau <= tauMax; ++tau)
    {
        if (cmnd[(size_t) tau] < threshold)
        {
            while (tau + 1 <= tauMax && cmnd[(size_t) (tau + 1)] < cmnd[(size_t) tau])
                ++tau;

            best = tau;
            break;
        }
    }

    if (best < 0)
    {
        best = tauMin;

        for (int tau = tauMin + 1; tau <= tauMax; ++tau)
            if (cmnd[(size_t) tau] < cmnd[(size_t) best])
                best = tau;
    }

    // Octave-error guard: if a fraction of the chosen lag (half, a third, a
    // quarter) is nearly as periodic, the shorter period is the real one.
    for (int divisor = 4; divisor >= 2; --divisor)
    {
        const int centre = juce::roundToInt ((float) best / (float) divisor);

        if (centre - 2 < tauMin)
            continue;

        int candidate = centre;
        for (int tau = centre - 2; tau <= centre + 2; ++tau)
            if (cmnd[(size_t) tau] < cmnd[(size_t) candidate])
                candidate = tau;

        if (cmnd[(size_t) candidate] < std::min (0.25f, cmnd[(size_t) best] + 0.12f))
        {
            best = candidate;
            break;
        }
    }

    // Parabolic interpolation on the raw difference function.
    float lag = (float) best;
    {
        const float a = diff[(size_t) (best - 1)], b = diff[(size_t) best], c = diff[(size_t) (best + 1)];
        const float denom = a - 2.0f * b + c;

        if (denom > 1.0e-12f)
            lag += juce::jlimit (-0.5f, 0.5f, 0.5f * (a - c) / denom);
    }

    result.aperiodicity = cmnd[(size_t) best];
    result.hz = (float) (sampleRate / lag);

    // Refine with the fundamental partial itself when it is strong enough.
    const double peak = std::sqrt (2.0 * sumSquares / n);
    const auto fundamental = measurePartial (x, result.hz);

    if (fundamental.valid && fundamental.amplitude >= 0.15 * peak)
        result.hz = fundamental.hz;

    result.voiced = result.aperiodicity < voicingLimit
                    && result.hz >= lowestHz * 0.97f
                    && result.hz <= highestHz * 1.03f;
    return result;
}

PitchTracker::Partial PitchTracker::measurePartial (const float* x, float hz) noexcept
{
    Partial partial;
    const int period = (int) std::lround (sampleRate / hz);

    if (period < 4 || period * 2 > size)
        return partial;

    // Sliding one-period sum of x[n] * e^(-i w n): every harmonic of hz cancels
    // over a whole period, leaving the fundamental as a slowly rotating phasor.
    const double w = 2.0 * juce::MathConstants<double>::pi * hz / sampleRate;
    const std::complex<double> step (std::cos (w), -std::sin (w));
    std::complex<double> osc (1.0, 0.0), trailing (1.0, 0.0), sum (0.0, 0.0);
    double energy = 0.0;

    for (int n = 0; n < period; ++n)
    {
        sum += (double) x[n] * osc;
        energy += (double) x[n] * x[n];
        osc *= step;
    }

    const int stride = std::max (1, period / 8);
    const int outputs = size - period;
    int count = 0;
    boxSums[(size_t) count] = sum;
    boxEnergies[(size_t) count++] = energy;

    for (int k = 1; k <= outputs; ++k)
    {
        const double entering = x[k + period - 1], leaving = x[k - 1];
        sum += entering * osc - leaving * trailing;
        energy += entering * entering - leaving * leaving;
        osc *= step;
        trailing *= step;

        if (k % stride == 0)
        {
            boxSums[(size_t) count] = sum;
            boxEnergies[(size_t) count++] = energy;
        }

        if ((k & 255) == 0)
        {
            osc /= std::abs (osc);
            trailing /= std::abs (trailing);
        }
    }

    // A box that is much quieter than the boxes a period before or after it is
    // straddling a note starting or stopping: it no longer cancels the other
    // harmonics, so its phase is meaningless. Smooth level changes are fine.
    const int periodInSteps = std::max (1, period / stride);

    auto valid = [&] (int i)
    {
        double reference = 0.0;
        if (i - periodInSteps >= 0)     reference = std::max (reference, boxEnergies[(size_t) (i - periodInSteps)]);
        if (i + periodInSteps < count)  reference = std::max (reference, boxEnergies[(size_t) (i + periodInSteps)]);
        return boxEnergies[(size_t) i] > 1.0e-12 && boxEnergies[(size_t) i] >= 0.5 * reference;
    };

    std::complex<double> rotation (0.0, 0.0);
    double magnitudes = 0.0;
    int steps = 0, used = 0;
    bool previousValid = false;

    for (int i = 0; i < count; ++i)
    {
        const bool isValid = valid (i);

        if (isValid)
        {
            magnitudes += std::abs (boxSums[(size_t) i]);
            ++used;

            if (previousValid)
            {
                rotation += boxSums[(size_t) i] * std::conj (boxSums[(size_t) (i - 1)]);
                ++steps;
            }
        }

        previousValid = isValid;
    }

    if (steps < 4 || std::abs (rotation) <= 0.0)
        return partial;

    const double measured = hz + std::arg (rotation) / (2.0 * juce::MathConstants<double>::pi * stride) * sampleRate;

    // The one-period average only passes about +-f/2: trust small corrections only.
    if (! (std::abs (12.0 * std::log2 (measured / hz)) < 3.0))
        return partial;

    partial.hz = (float) measured;
    partial.amplitude = 2.0 * magnitudes / ((double) period * used);
    partial.valid = true;
    return partial;
}

//==============================================================================
void PitchFollower::reset() noexcept
{
    hz = pendingHz = 0.0f;
    has = voicedNow = false;
    pendingCount = 0;
    framesSinceVoiced = 1 << 20;
}

void PitchFollower::update (const PitchEstimate& estimate) noexcept
{
    // Onset / cut-off windows may only confirm the current pitch, never move it.
    if (estimate.voiced && ! estimate.stable && has && std::abs (std::log2 (estimate.hz / hz)) < 0.5f / 12.0f)
    {
        voicedNow = true;
        framesSinceVoiced = 0;
        return;
    }

    if (! estimate.voiced || ! estimate.stable)
    {
        voicedNow = false;
        pendingCount = 0;
        framesSinceVoiced = std::min (framesSinceVoiced + 1, 1 << 20);
        return;
    }

    const bool freshStart = ! has || framesSinceVoiced > framesBeforeFreshStart;

    // Anything that isn't a small move (vibrato, bends, drift) has to repeat
    // before it's believed: a new phrase needs `jumpFrames` consistent frames
    // from clearly periodic windows, big leaps need `largeJumpFrames`.
    auto confirm = [this] (float candidateHz, int required)
    {
        if (pendingCount > 0 && std::abs (std::log2 (candidateHz / pendingHz)) < 0.5f / 12.0f)
            ++pendingCount;
        else
        {
            pendingHz = candidateHz;
            pendingCount = 1;
        }

        if (pendingCount < required)
            return false;

        pendingCount = 0;
        return true;
    };

    if (freshStart)
    {
        if (estimate.aperiodicity > 0.25f || ! confirm (estimate.hz, jumpFrames))
        {
            voicedNow = false;
            return;
        }

        hz = estimate.hz;
        has = true;
        voicedNow = true;
        framesSinceVoiced = 0;
        return;
    }

    framesSinceVoiced = 0;
    voicedNow = true;

    const float semitones = 12.0f * std::log2 (estimate.hz / hz);

    if (std::abs (semitones) < 1.0f)
    {
        hz = estimate.hz;
        pendingCount = 0;
        return;
    }

    const bool largeLeap = std::abs (semitones) > 9.0f && estimate.aperiodicity >= 0.05f;

    if (confirm (estimate.hz, largeLeap ? largeJumpFrames : jumpFrames))
        hz = estimate.hz;
}

} // namespace tether
