#pragma once

#include <juce_core/juce_core.h>

#include <cmath>
#include <vector>

/** Signal generators and measurements shared by the tests and the demo renderer. */
namespace testsig
{

constexpr double twoPi = 6.283185307179586;

/** Band-limited sawtooth (additive) with an optional per-sample frequency curve. */
inline void addSaw (std::vector<float>& out, double sampleRate, const std::vector<float>& hz,
                    const std::vector<float>& amp, double phase0 = 0.0)
{
    double phase = phase0;

    for (size_t i = 0; i < out.size(); ++i)
    {
        const double f = hz[i];
        if (f > 0.0 && std::abs (amp[i]) > 1.0e-9f)
        {
            double v = 0.0;
            const int harmonics = (int) std::floor (0.45 * sampleRate / f);
            for (int h = 1; h <= harmonics; ++h)
                v += std::sin (phase * h) / h;

            out[i] += (float) (amp[i] * v * (2.0 / juce::MathConstants<double>::pi) * 0.5);
        }

        phase += twoPi * f / sampleRate;
        if (phase > twoPi * 1000.0)
            phase = std::fmod (phase, twoPi);
    }
}

inline std::vector<float> saw (double sampleRate, double hz, float amp, int numSamples)
{
    std::vector<float> out ((size_t) numSamples, 0.0f);
    addSaw (out, sampleRate, std::vector<float> ((size_t) numSamples, (float) hz),
            std::vector<float> ((size_t) numSamples, amp));
    return out;
}

inline std::vector<float> sine (double sampleRate, double hz, float amp, int numSamples, double phase0 = 0.0)
{
    std::vector<float> out ((size_t) numSamples);
    for (int i = 0; i < numSamples; ++i)
        out[(size_t) i] = amp * (float) std::sin (phase0 + twoPi * hz * i / sampleRate);
    return out;
}

inline std::vector<float> square (double sampleRate, double hz, float amp, int numSamples)
{
    std::vector<float> out ((size_t) numSamples, 0.0f);
    const int harmonics = (int) std::floor (0.45 * sampleRate / hz);
    for (int i = 0; i < numSamples; ++i)
    {
        double v = 0.0;
        for (int h = 1; h <= harmonics; h += 2)
            v += std::sin (twoPi * hz * h * i / sampleRate) / h;
        out[(size_t) i] = amp * (float) (v * 4.0 / juce::MathConstants<double>::pi * 0.7);
    }
    return out;
}

inline std::vector<float> noise (int numSamples, float amp, int seed)
{
    juce::Random random (seed);
    std::vector<float> out ((size_t) numSamples);
    for (auto& s : out)
        s = amp * (2.0f * random.nextFloat() - 1.0f);
    return out;
}

inline double rms (const std::vector<float>& x, int start, int length)
{
    jassert (start >= 0 && start + length <= (int) x.size());
    start = std::max (0, start);
    length = std::max (0, std::min (length, (int) x.size() - start));

    double sum = 0.0;
    for (int i = start; i < start + length; ++i)
        sum += (double) x[(size_t) i] * x[(size_t) i];
    return std::sqrt (sum / std::max (1, length));
}

inline double rmsDb (const std::vector<float>& x, int start, int length)
{
    return 20.0 * std::log10 (std::max (rms (x, start, length), 1.0e-9));
}

/** Independent reference pitch estimate: direct normalised autocorrelation
    (not the plugin's FFT-based YIN) with parabolic interpolation. */
inline double referencePitch (const std::vector<float>& x, int start, int length, double sampleRate,
                              double minHz = 20.0, double maxHz = 2000.0)
{
    jassert (start >= 0 && start + length <= (int) x.size());
    length = std::min (length, (int) x.size() - start);

    // Lags up to maxLag + 1 are read (for interpolation), so leave room for them.
    const int maxLag = (int) (sampleRate / minHz), minLag = (int) (sampleRate / maxHz);
    const int n = length - maxLag - 1;
    if (n <= 0 || start < 0)
        return 0.0;

    std::vector<double> nac ((size_t) maxLag + 2, 0.0);
    for (int lag = minLag; lag <= maxLag + 1; ++lag)
    {
        double xy = 0.0, xx = 0.0, yy = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const double a = x[(size_t) (start + i)], b = x[(size_t) (start + i + lag)];
            xy += a * b; xx += a * a; yy += b * b;
        }
        nac[(size_t) lag] = xy / std::sqrt (xx * yy + 1.0e-20);
    }

    // Smallest lag whose correlation is within 3 % of the best one (avoids sub-octaves).
    double best = -1.0;
    for (int lag = minLag + 1; lag <= maxLag; ++lag)
        best = std::max (best, nac[(size_t) lag]);

    int chosen = -1;
    for (int lag = minLag + 1; lag <= maxLag; ++lag)
    {
        if (nac[(size_t) lag] >= best * 0.97 && nac[(size_t) lag] >= nac[(size_t) (lag - 1)] && nac[(size_t) lag] >= nac[(size_t) (lag + 1)])
        {
            chosen = lag;
            break;
        }
    }

    if (chosen < 0)
        return 0.0;

    const double a = nac[(size_t) (chosen - 1)], b = nac[(size_t) chosen], c = nac[(size_t) (chosen + 1)];
    const double denom = a - 2.0 * b + c;
    const double offset = std::abs (denom) > 1.0e-12 ? 0.5 * (a - c) / denom : 0.0;
    return sampleRate / (chosen + offset);
}

inline double cents (double hz, double reference)
{
    return 1200.0 * std::log2 (hz / reference);
}

} // namespace testsig
