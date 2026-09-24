#include "TestSignals.h"
#include "dsp/TetherEngine.h"

#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>

using namespace tether;

namespace
{

constexpr double sr = 48000.0;

struct Rendered
{
    std::vector<float> left, right;
    int latency = 0;
};

Rendered render (const std::vector<float>& layer, const std::vector<float>* guide, const EngineParams& params,
                 Resolution resolution = Resolution::normal, Engine engine = Engine::natural, int blockSize = 512)
{
    TetherEngine e;
    e.prepare (sr, 2);
    e.setResolution (resolution);
    e.setEngine (engine);

    Rendered r;
    r.latency = e.getLatencySamples();
    r.left = layer;
    r.right = layer;

    const int total = (int) layer.size();
    for (int start = 0; start < total; start += blockSize)
    {
        const int len = std::min (blockSize, total - start);
        float* ch[2] = { r.left.data() + start, r.right.data() + start };
        const float* g[1] = { guide != nullptr ? guide->data() + start : nullptr };
        e.process (ch, 2, guide != nullptr ? g : nullptr, guide != nullptr ? 1 : 0, len, params);
    }

    return r;
}

/** Normalised autocorrelation at a fractional lag: 1 = perfectly periodic. */
double periodicity (const std::vector<float>& x, int start, int length, double period)
{
    const int lag = (int) std::floor (period);
    const double frac = period - lag;
    double xy = 0.0, xx = 0.0, yy = 0.0;

    for (int i = start; i < start + length; ++i)
    {
        const double a = x[(size_t) i];
        const double b = (1.0 - frac) * x[(size_t) (i + lag)] + frac * x[(size_t) (i + lag + 1)];
        xy += a * b; xx += a * a; yy += b * b;
    }

    return xy / std::sqrt (xx * yy + 1.0e-20);
}

/** Power spectrum of a Blackman-Harris windowed frame (sidelobes -92 dB). */
std::vector<double> powerSpectrum (const std::vector<float>& x, int start, int n)
{
    juce::dsp::FFT fft (log2Int (n));
    std::vector<float> work ((size_t) (2 * n), 0.0f);

    for (int i = 0; i < n; ++i)
    {
        const double t = (double) i / n;
        const double w = 0.35875 - 0.48829 * std::cos (testsig::twoPi * t) + 0.14128 * std::cos (2 * testsig::twoPi * t)
                       - 0.01168 * std::cos (3 * testsig::twoPi * t);
        work[(size_t) i] = (float) (x[(size_t) (start + i)] * w);
    }

    fft.performRealOnlyForwardTransform (work.data(), true);

    std::vector<double> power ((size_t) n / 2 + 1);
    for (int k = 0; k <= n / 2; ++k)
        power[(size_t) k] = (double) work[(size_t) (2 * k)] * work[(size_t) (2 * k)] + (double) work[(size_t) (2 * k + 1)] * work[(size_t) (2 * k + 1)];
    return power;
}

/** Energy at multiples of hz versus everything else below 8 kHz, in dB. */
double harmonicToNoiseDb (const std::vector<float>& x, int start, int n, double hz)
{
    const auto power = powerSpectrum (x, start, n);
    const double binHz = sr / n;
    std::vector<bool> harmonic (power.size(), false);

    for (int h = 1; h * hz < 8000.0; ++h)
    {
        const int k = (int) std::lround (h * hz / binHz);
        for (int j = std::max (0, k - 3); j <= std::min ((int) power.size() - 1, k + 3); ++j)
            harmonic[(size_t) j] = true;
    }

    double inside = 0.0, outside = 0.0;
    for (size_t k = 0; k < power.size() && k * binHz < 8000.0; ++k)
        (harmonic[k] ? inside : outside) += power[k];

    return 10.0 * std::log10 (inside / (outside + 1.0e-20));
}

double spectralCentroidHz (const std::vector<float>& x, int start, int n)
{
    const auto power = powerSpectrum (x, start, n);
    const double binHz = sr / n;
    double weighted = 0.0, total = 0.0;

    for (size_t k = 0; k < power.size(); ++k)
    {
        weighted += power[k] * (double) k * binHz;
        total += power[k];
    }

    return weighted / (total + 1.0e-20);
}

} // namespace

//==============================================================================
class QualityTests final : public juce::UnitTest
{
public:
    QualityTests() : juce::UnitTest ("Quality", "Tether") {}

    void runTest() override
    {
        testShiftQuality();
        testSpectralEngine();
        testFineAndFormant();
        testQuantise();
        testVibrato();
        testListen();
    }

private:
    void testShiftQuality()
    {
        const int n = (int) (2.0 * sr), at = (int) (1.2 * sr);

        struct Case { const char* name; double layerHz, guideHz; bool formant; Resolution res; };
        const Case cases[] = {
            { "up a fifth",              110.0, 164.81, true,  Resolution::normal },
            { "up a fifth, formants follow", 110.0, 164.81, false, Resolution::normal },
            { "down a fifth",            220.0, 146.83, true,  Resolution::normal },
            { "down a fifth, formants follow", 220.0, 146.83, false, Resolution::normal },
            { "up an octave",            196.0, 392.0,  true,  Resolution::tight },
            { "down an octave",          220.0, 110.0,  true,  Resolution::normal },
            { "sub bass",                55.0,  41.2,   true,  Resolution::deep },
            { "a semitone up",           220.0, 233.08, true,  Resolution::normal },
        };

        for (const auto& c : cases)
        {
            beginTest (juce::String ("Natural engine: ") + c.name + " lands on pitch, stays periodic and harmonic");

            const auto layer = testsig::saw (sr, c.layerHz, 0.25f, n);
            const auto guide = testsig::saw (sr, c.guideHz, 0.3f, n);
            EngineParams p;
            p.formant = c.formant;
            const auto r = render (layer, &guide, p, c.res);

            const double hz = testsig::referencePitch (r.left, at, 8192, sr, 20.0, 2000.0);
            expectLessThan (std::abs (testsig::cents (hz, c.guideHz)), 2.0, "pitch " + juce::String (hz, 3) + " Hz");
            expectGreaterThan (periodicity (r.left, at, 16384, sr / c.guideHz), 0.995, "periodicity");
            expectGreaterThan (harmonicToNoiseDb (r.left, at, 32768, c.guideHz), 30.0, "harmonic content");

            const double outDb = testsig::rmsDb (r.left, at, 16384);
            const double guideDb = testsig::rmsDb (guide, at - r.latency, 16384);
            expectLessThan (std::abs (outDb - guideDb), 0.3, "level " + juce::String (outDb - guideDb, 2) + " dB off");
        }

        beginTest ("Natural engine: an unshifted layer passes through bit-exactly while its pitch is being tracked");
        {
            const auto layer = testsig::saw (sr, 110.0, 0.25f, n);
            const auto guide = testsig::saw (sr, 164.81, 0.3f, n);
            EngineParams p;
            p.pitchAmount = 0.0f;
            p.levelAmount = 0.0f;
            p.motion = 0.0f;
            p.gateDb = -80.0f;
            const auto r = render (layer, &guide, p);

            float worst = 0.0f;
            for (int i = r.latency; i < n; ++i)
                worst = std::max (worst, std::abs (r.left[(size_t) i] - layer[(size_t) (i - r.latency)]));

            expectLessThan (worst, 1.0e-5f);
        }
    }

    void testSpectralEngine()
    {
        const int n = (int) (2.0 * sr), at = (int) (1.2 * sr);

        for (double guideHz : { 164.81, 73.42 })
        {
            beginTest ("Spectral engine moves a 110 Hz layer to " + juce::String (guideHz) + " Hz");

            const auto layer = testsig::saw (sr, 110.0, 0.25f, n);
            const auto guide = testsig::saw (sr, guideHz, 0.3f, n);
            const auto r = render (layer, &guide, EngineParams {}, Resolution::normal, Engine::spectral);

            const double hz = testsig::referencePitch (r.left, at, 8192, sr, 20.0, 2000.0);
            expectLessThan (std::abs (testsig::cents (hz, guideHz)), 10.0, "pitch " + juce::String (hz, 2) + " Hz");
            expectLessThan (std::abs (testsig::rmsDb (r.left, at, 16384) - testsig::rmsDb (guide, at - r.latency, 16384)), 0.5, "level");
        }

        beginTest ("Both engines report the same latency and the spectral engine stays block-size invariant");
        {
            TetherEngine a, b;
            a.prepare (sr, 2);
            b.prepare (sr, 2);
            a.setEngine (Engine::natural);
            b.setEngine (Engine::spectral);
            expectEquals (a.getLatencySamples(), b.getLatencySamples());

            const auto layer = testsig::saw (sr, 146.83, 0.2f, n);
            const auto guide = testsig::saw (sr, 110.0, 0.3f, n);
            const auto reference = render (layer, &guide, EngineParams {}, Resolution::normal, Engine::spectral, 512);
            const auto other = render (layer, &guide, EngineParams {}, Resolution::normal, Engine::spectral, 333);

            float worst = 0.0f;
            for (size_t i = 0; i < reference.left.size(); ++i)
                worst = std::max (worst, std::abs (reference.left[i] - other.left[i]));

            expectLessThan (worst, 1.0e-6f);
        }
    }

    void testFineAndFormant()
    {
        const int n = (int) (2.0 * sr), at = (int) (1.2 * sr);
        const auto layer = testsig::saw (sr, 110.0, 0.25f, n);
        const auto guide = testsig::saw (sr, 164.81, 0.3f, n);

        beginTest ("Fine tune offsets the target in cents");
        {
            for (float cents : { 50.0f, -35.0f })
            {
                EngineParams p;
                p.fineCents = cents;
                const auto r = render (layer, &guide, p);
                const double hz = testsig::referencePitch (r.left, at, 8192, sr, 20.0, 2000.0);
                expectWithinAbsoluteError (testsig::cents (hz, 164.81), (double) cents, 2.5, juce::String (cents) + " cents");
            }
        }

        beginTest ("Formant shift moves the spectral balance without changing the pitch");
        {
            const auto same = testsig::saw (sr, 110.0, 0.3f, n);
            double centroids[3] = {};
            const float shifts[3] = { -12.0f, 0.0f, 12.0f };

            for (int i = 0; i < 3; ++i)
            {
                EngineParams p;
                p.formantShift = shifts[i];
                p.motion = 0.0f;
                const auto r = render (layer, &same, p);
                centroids[i] = spectralCentroidHz (r.left, at, 16384);
                const double hz = testsig::referencePitch (r.left, at, 8192, sr, 20.0, 2000.0);
                expectLessThan (std::abs (testsig::cents (hz, 110.0)), 3.0, "pitch with formant shift " + juce::String (shifts[i]));
            }

            expectGreaterThan (centroids[2], centroids[1] * 1.3, "formants up brighten");
            expectLessThan (centroids[0], centroids[1] * 0.8, "formants down darken");
        }
    }

    void testQuantise()
    {
        beginTest ("quantiseToScale picks the nearest scale note (ties go down)");
        {
            expectWithinAbsoluteError (quantiseToScale (60.4f, Scale::major, 0), 60.0f, 1.0e-4f);
            expectWithinAbsoluteError (quantiseToScale (61.0f, Scale::major, 0), 60.0f, 1.0e-4f);   // C# is not in C major
            expectWithinAbsoluteError (quantiseToScale (61.6f, Scale::major, 0), 62.0f, 1.0e-4f);
            expectWithinAbsoluteError (quantiseToScale (66.0f, Scale::major, 0), 65.0f, 1.0e-4f);   // F# -> F (tie, lower)
            expectWithinAbsoluteError (quantiseToScale (60.0f, Scale::major, 2), 59.0f, 1.0e-4f);   // C in D major -> B
            expectWithinAbsoluteError (quantiseToScale (63.0f, Scale::octaves, 0), 60.0f, 1.0e-4f);
            expectWithinAbsoluteError (quantiseToScale (65.0f, Scale::fifths, 0), 67.0f, 1.0e-4f);
            expectWithinAbsoluteError (quantiseToScale (61.3f, Scale::off, 0), 61.3f, 1.0e-4f);
            expectWithinAbsoluteError (quantiseToScale (61.3f, Scale::chromatic, 0), 61.0f, 1.0e-4f);
        }

        beginTest ("A sliding guide is snapped to the scale, vibrato riding on top");
        {
            // Guide slides an octave over three seconds; the layer must sit on
            // C major notes (within a few cents) wherever we look.
            const int n = (int) (3.0 * sr);
            std::vector<float> guide ((size_t) n, 0.0f), hz ((size_t) n), amp ((size_t) n, 0.3f);
            for (int i = 0; i < n; ++i)
                hz[(size_t) i] = (float) (110.0 * std::pow (2.0, (double) i / n));
            testsig::addSaw (guide, sr, hz, amp);

            const auto layer = testsig::saw (sr, 146.83, 0.25f, n);
            EngineParams p;
            p.scale = Scale::major;
            p.key = 0;
            p.glideMs = 5.0f;
            const auto r = render (layer, &guide, p);

            // Short frames every 512 samples; the few that straddle a step
            // between notes (5 ms glide) are allowed.
            int offNotes = 0, frames = 0;
            for (int at = (int) (0.6 * sr); at < (int) (2.8 * sr); at += 512)
            {
                const double measured = testsig::referencePitch (r.left, at, 2048, sr, 60.0, 2000.0);
                expectGreaterThan (measured, 60.0, "pitch reading");
                const float midi = hzToMidi ((float) measured);
                const float snapped = quantiseToScale (midi, Scale::major, 0);
                ++frames;
                if (std::abs (midi - snapped) > 0.1f)
                    ++offNotes;
            }

            expectLessThan (offNotes * 100, frames * 15, juce::String (offNotes) + " of " + juce::String (frames)
                                                          + " frames more than 10 cents off a C major note");

            // Without a scale the same slide is followed continuously.
            p.scale = Scale::off;
            const auto plain = render (layer, &guide, p);
            double worstFollow = 0.0;
            for (double t = 0.8; t < 2.6; t += 0.3)
            {
                const int at = (int) (t * sr);
                const double measured = testsig::referencePitch (plain.left, at + plain.latency, 2048, sr, 60.0, 2000.0);
                worstFollow = std::max (worstFollow, std::abs (testsig::cents (measured, hz[(size_t) at])));
            }
            expectLessThan (worstFollow, 25.0, "slide followed within " + juce::String (worstFollow, 1) + " cents");
        }
    }

    void testVibrato()
    {
        beginTest ("Vibrato amount scales the guide's vibrato: 0 % removes it, 200 % doubles it");
        {
            const int n = (int) (2.5 * sr);
            std::vector<float> guide ((size_t) n, 0.0f), hz ((size_t) n), amp ((size_t) n, 0.3f);
            for (int i = 0; i < n; ++i)
                hz[(size_t) i] = (float) (220.0 * std::pow (2.0, 0.3 / 12.0 * std::sin (testsig::twoPi * 5.5 * i / sr)));
            testsig::addSaw (guide, sr, hz, amp);
            const auto layer = testsig::saw (sr, 146.83, 0.25f, n);

            auto swing = [&] (float vibrato)
            {
                EngineParams p;
                p.vibrato = vibrato;
                p.glideMs = 0.0f;
                const auto r = render (layer, &guide, p);

                double lowest = 1.0e9, highest = -1.0e9;
                for (int at = (int) (1.0 * sr); at < (int) (2.0 * sr); at += 512)
                {
                    const double c = testsig::cents (testsig::referencePitch (r.left, at, 2048, sr, 100.0, 2000.0), 220.0);
                    lowest = std::min (lowest, c);
                    highest = std::max (highest, c);
                }
                return highest - lowest;
            };

            const double none = swing (0.0f), normal = swing (1.0f), doubled = swing (2.0f);
            expectLessThan (none, 12.0, "vibrato 0 % swing " + juce::String (none, 1) + " cents");
            expectWithinAbsoluteError (normal, 60.0, 12.0, "vibrato 100 % swing " + juce::String (normal, 1) + " cents");
            expectGreaterThan (doubled, 100.0, "vibrato 200 % swing " + juce::String (doubled, 1) + " cents");
        }
    }

    void testListen()
    {
        beginTest ("Listen outputs the guide, aligned with the layer's latency");
        {
            const int n = 48000;
            const auto layer = testsig::saw (sr, 110.0, 0.25f, n);
            const auto guide = testsig::noise (n, 0.4f, 9);
            EngineParams p;
            p.listen = true;
            const auto r = render (layer, &guide, p);

            float worst = 0.0f;
            for (int i = r.latency; i < n; ++i)
                worst = std::max (worst, std::abs (r.left[(size_t) i] - guide[(size_t) (i - r.latency)]));

            expectLessThan (worst, 1.0e-5f);
        }
    }
};

static QualityTests qualityTests;
