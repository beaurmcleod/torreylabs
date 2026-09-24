#include "TestSignals.h"
#include "dsp/TetherEngine.h"

#include <juce_core/juce_core.h>

using namespace tether;

namespace
{

struct Rendered
{
    std::vector<float> left, right;
    int latency = 0;
};

Rendered render (const std::vector<float>& layerL, const std::vector<float>& layerR,
                 const std::vector<float>* guide, const EngineParams& params,
                 Resolution resolution = Resolution::normal, double sampleRate = 48000.0, int blockSize = 512,
                 Engine engineMode = Engine::natural)
{
    TetherEngine engine;
    engine.prepare (sampleRate, 2);
    engine.setResolution (resolution);
    engine.setEngine (engineMode);

    Rendered r;
    r.latency = engine.getLatencySamples();
    r.left = layerL;
    r.right = layerR;

    const int total = (int) r.left.size();

    for (int start = 0; start < total; start += blockSize)
    {
        const int len = std::min (blockSize, total - start);
        float* layer[2] = { r.left.data() + start, r.right.data() + start };
        const float* guideChannels[1] = { guide != nullptr ? guide->data() + start : nullptr };
        engine.process (layer, 2, guide != nullptr ? guideChannels : nullptr, guide != nullptr ? 1 : 0, len, params);
    }

    return r;
}

float maxAbsDifference (const std::vector<float>& a, const std::vector<float>& b)
{
    float worst = 0.0f;
    for (size_t i = 0; i < std::min (a.size(), b.size()); ++i)
        worst = std::max (worst, std::abs (a[i] - b[i]));
    return worst;
}

/** Guide melody: saw notes with gaps and different levels. */
struct Melody
{
    std::vector<float> signal;
    std::vector<int> noteStarts, noteLengths;
    std::vector<double> noteHz;
};

Melody makeMelody (double sampleRate, const std::vector<double>& hz, const std::vector<float>& amps,
                   double noteSeconds, double gapSeconds)
{
    Melody m;
    const int noteLen = (int) (noteSeconds * sampleRate), gapLen = (int) (gapSeconds * sampleRate);
    const int total = (int) hz.size() * (noteLen + gapLen) + gapLen;
    std::vector<float> freq ((size_t) total, 0.0f), amp ((size_t) total, 0.0f);
    const int fade = (int) (0.005 * sampleRate);

    int pos = gapLen;
    for (size_t n = 0; n < hz.size(); ++n)
    {
        m.noteStarts.push_back (pos);
        m.noteLengths.push_back (noteLen);
        m.noteHz.push_back (hz[n]);

        for (int i = 0; i < noteLen; ++i)
        {
            const float env = (float) std::min ({ 1.0, (double) i / fade, (double) (noteLen - 1 - i) / fade });
            freq[(size_t) (pos + i)] = (float) hz[n];
            amp[(size_t) (pos + i)] = amps[n] * env;
        }

        pos += noteLen + gapLen;
    }

    m.signal.assign ((size_t) total, 0.0f);
    testsig::addSaw (m.signal, sampleRate, freq, amp);
    return m;
}

EngineParams neutralParams()
{
    EngineParams p;
    p.pitchAmount = 0.0f;
    p.levelAmount = 0.0f;
    p.motion = 0.0f;
    p.tone = 0.0f;
    p.punch = 0.0f;
    p.gateDb = -80.0f;
    p.formant = false;
    return p;
}

} // namespace

//==============================================================================
class FFTRoundTripTests final : public juce::UnitTest
{
public:
    FFTRoundTripTests() : juce::UnitTest ("FFT round trip", "Tether") {}

    void runTest() override
    {
        beginTest ("Forward then inverse real FFT reproduces the input");

        for (int order : { 10, 11, 12, 13 })
        {
            juce::dsp::FFT fft (order);
            const int n = 1 << order;
            auto input = testsig::noise (n, 0.8f, order);
            std::vector<float> work ((size_t) (2 * n), 0.0f);
            std::copy (input.begin(), input.end(), work.begin());

            fft.performRealOnlyForwardTransform (work.data(), true);
            fft.performRealOnlyInverseTransform (work.data());

            float worst = 0.0f;
            for (int i = 0; i < n; ++i)
                worst = std::max (worst, std::abs (work[(size_t) i] - input[(size_t) i]));

            expectLessThan (worst, 1.0e-5f, "size " + juce::String (n));
        }
    }
};

static FFTRoundTripTests fftRoundTripTests;

//==============================================================================
class PitchTrackerTests final : public juce::UnitTest
{
public:
    PitchTrackerTests() : juce::UnitTest ("Pitch tracker", "Tether") {}

    void runTest() override
    {
        constexpr double sr = 48000.0;

        struct Case { Resolution res; std::vector<double> hz; };
        const Case cases[] = {
            { Resolution::tight,  { 100.0, 146.83, 220.0, 440.0, 880.0, 1500.0 } },
            { Resolution::normal, { 49.0, 65.41, 110.0, 261.63, 523.25 } },
            { Resolution::deep,   { 27.5, 30.87, 41.2, 55.0, 110.0 } },
        };

        for (const auto& c : cases)
        {
            const int n = TetherEngine::fftSizeFor (c.res, sr);
            juce::dsp::FFT fft (log2Int (n));
            PitchTracker tracker;
            tracker.prepare (n);
            tracker.configure (sr, n, TetherEngine::lowestPitchFor (c.res), 2000.0f, &fft);

            beginTest ("YIN accuracy, window " + juce::String (n));

            for (double hz : c.hz)
            {
                const auto sine = testsig::sine (sr, hz, 0.5f, n + 1000);
                const auto saw  = testsig::saw (sr, hz, 0.5f, n + 1000);
                const auto sq   = testsig::square (sr, hz, 0.5f, n + 1000);

                for (const auto* signal : { &sine, &saw, &sq })
                {
                    const auto e = tracker.analyse (signal->data() + 500);
                    expect (e.voiced, juce::String (hz) + " Hz should be voiced");
                    expectLessThan (std::abs (testsig::cents (e.hz, hz)), 5.0,
                                    juce::String (hz) + " Hz estimated as " + juce::String (e.hz));
                }
            }

            beginTest ("Noise and silence are unvoiced, window " + juce::String (n));
            {
                const auto noise = testsig::noise (n, 0.5f, 7);
                expect (! tracker.analyse (noise.data()).voiced, "white noise must be unvoiced");

                const std::vector<float> silence ((size_t) n, 0.0f);
                expect (! tracker.analyse (silence.data()).voiced, "silence must be unvoiced");
            }
        }

        beginTest ("Follower needs repeats before trusting an unclear big leap");
        {
            PitchFollower f;
            f.setConfirmationFrames (1, 3);
            auto est = [] (float hz, float aperiodicity)
            {
                PitchEstimate e;
                e.hz = hz;
                e.voiced = true;
                e.aperiodicity = aperiodicity;
                return e;
            };

            f.update (est (110.0f, 0.02f));
            f.update (est (220.0f, 0.1f));
            f.update (est (220.0f, 0.1f));
            expectWithinAbsoluteError (f.getHz(), 110.0f, 0.01f);
            f.update (est (220.0f, 0.1f));
            expectWithinAbsoluteError (f.getHz(), 220.0f, 0.01f);

            f.update (est (233.0f, 0.1f)); // a normal step is taken at once
            expectWithinAbsoluteError (f.getHz(), 233.0f, 0.01f);
            f.update (est (116.5f, 0.01f)); // a clean octave leap too
            expectWithinAbsoluteError (f.getHz(), 116.5f, 0.01f);
        }
    }
};

static PitchTrackerTests pitchTrackerTests;

//==============================================================================
class EngineTests final : public juce::UnitTest
{
public:
    EngineTests() : juce::UnitTest ("Engine", "Tether") {}

    void runTest() override
    {
        testPassThrough();
        testPitchMatching();
        testMelodyFollowing();
        testOnsetStability();
        testControls();
        testBlockSizeInvariance();
        testRobustness();
    }

private:
    void testPassThrough()
    {
        for (double sr : { 44100.0, 48000.0, 96000.0 })
        {
            for (auto res : { Resolution::tight, Resolution::normal, Resolution::deep })
            {
                beginTest ("No guide = pure delay by the reported latency, "
                           + juce::String (sr) + " Hz, resolution " + juce::String ((int) res));

                const int n = (int) sr;
                const auto left = testsig::noise (n, 0.5f, 1), right = testsig::noise (n, 0.5f, 2);
                const auto r = render (left, right, nullptr, EngineParams {}, res, sr);

                expectEquals (r.latency, TetherEngine::latencyFor (res, sr));

                float worst = 0.0f;
                for (int i = r.latency; i < n; ++i)
                {
                    worst = std::max (worst, std::abs (r.left[(size_t) i] - left[(size_t) (i - r.latency)]));
                    worst = std::max (worst, std::abs (r.right[(size_t) i] - right[(size_t) (i - r.latency)]));
                }

                expectLessThan (worst, 1.0e-4f);
            }
        }

        beginTest ("Neutral settings with a guide are transparent, and dry/wet stay phase aligned");
        {
            const int n = 48000;
            const auto layer = testsig::saw (48000.0, 110.0, 0.3f, n);
            const auto guide = testsig::saw (48000.0, 164.81, 0.3f, n);

            auto p = neutralParams();
            for (float mix : { 1.0f, 0.5f, 0.0f })
            {
                p.mix = mix;
                const auto r = render (layer, layer, &guide, p);

                float worst = 0.0f;
                for (int i = r.latency; i < n; ++i)
                    worst = std::max (worst, std::abs (r.left[(size_t) i] - layer[(size_t) (i - r.latency)]));

                expectLessThan (worst, 1.0e-4f, "mix " + juce::String (mix));
            }
        }
    }

    void expectPitch (const std::vector<float>& out, int start, double expectedHz, double toleranceCents, const juce::String& what)
    {
        const double hz = testsig::referencePitch (out, start, 8192, 48000.0, 20.0, 2000.0);
        expectLessThan (std::abs (testsig::cents (hz, expectedHz)), toleranceCents,
                        what + ": expected " + juce::String (expectedHz, 2) + " Hz, got " + juce::String (hz, 2));
    }

    void testPitchMatching()
    {
        constexpr double sr = 48000.0;
        const int n = (int) (2.0 * sr);

        struct Case { double layerHz, guideHz; Resolution res; bool formant; };
        const Case cases[] = {
            { 110.0, 164.81, Resolution::normal, true },  // up a fifth
            { 110.0, 164.81, Resolution::normal, false },
            { 220.0, 146.83, Resolution::normal, true },  // down a fifth
            { 196.0, 392.0,  Resolution::tight,  true },  // up an octave
            { 55.0,  41.2,   Resolution::deep,   true },  // sub bass
            { 82.41, 98.0,   Resolution::deep,   false },
        };

        for (const auto& c : cases)
        {
            beginTest ("Layer at " + juce::String (c.layerHz) + " Hz lands on guide at " + juce::String (c.guideHz)
                       + " Hz (formant " + (c.formant ? "on" : "off") + ", resolution " + juce::String ((int) c.res) + ")");

            const auto layer = testsig::saw (sr, c.layerHz, 0.25f, n);
            const auto guide = testsig::saw (sr, c.guideHz, 0.3f, n);
            EngineParams p;
            p.formant = c.formant;
            const auto r = render (layer, layer, &guide, p, c.res);

            expectPitch (r.left, (int) (1.2 * sr), c.guideHz, 2.0, "output pitch");

            const double outDb = testsig::rmsDb (r.left, (int) (1.2 * sr), 16384);
            const double guideDb = testsig::rmsDb (guide, (int) (1.2 * sr) - r.latency, 16384);
            expectLessThan (std::abs (outDb - guideDb), 0.3, "level match: out " + juce::String (outDb, 2)
                                                             + " dB vs guide " + juce::String (guideDb, 2) + " dB");
        }

        beginTest ("Octave and semitone offsets move the target");
        {
            const auto layer = testsig::saw (sr, 110.0, 0.25f, n);
            const auto guide = testsig::saw (sr, 330.0, 0.3f, n);
            EngineParams p;
            p.octave = -1;
            expectPitch (render (layer, layer, &guide, p).left, (int) (1.2 * sr), 165.0, 2.0, "octave -1");

            p.octave = 0;
            p.semitones = -7;
            expectPitch (render (layer, layer, &guide, p).left, (int) (1.2 * sr), 330.0 * std::pow (2.0, -7.0 / 12.0), 2.0, "-7 st");
        }

        beginTest ("Pitch amount 0 leaves the layer's pitch alone");
        {
            const auto layer = testsig::saw (sr, 110.0, 0.25f, n);
            const auto guide = testsig::saw (sr, 164.81, 0.3f, n);
            EngineParams p;
            p.pitchAmount = 0.0f;
            expectPitch (render (layer, layer, &guide, p).left, (int) (1.2 * sr), 110.0, 5.0, "unshifted");
        }

        beginTest ("Fixed root note is used when detection is off");
        {
            const auto layer = testsig::saw (sr, 110.0, 0.25f, n);
            const auto guide = testsig::saw (sr, 164.81, 0.3f, n);
            EngineParams p;
            p.layerAuto = false;
            p.layerRoot = 45.0f; // A2 = 110 Hz, the layer's true pitch
            expectPitch (render (layer, layer, &guide, p).left, (int) (1.2 * sr), 164.81, 2.0, "root A2");
        }
    }

    void testMelodyFollowing()
    {
        constexpr double sr = 48000.0;
        const auto melody = makeMelody (sr, { 110.0, 130.81, 164.81, 98.0 }, { 0.5f, 0.2f, 0.35f, 0.12f }, 0.5, 0.15);
        const int n = (int) melody.signal.size();
        const auto layer = testsig::saw (sr, 146.83, 0.2f, n); // a sustained D3 pad

        for (auto res : { Resolution::tight, Resolution::normal, Resolution::deep })
        {
            if (res == Resolution::tight)
                continue; // 98 Hz is too close to tight mode's floor for a fair test

            beginTest ("Layer follows a melody's notes, levels and gaps, resolution " + juce::String ((int) res));

            const auto r = render (layer, layer, &melody.signal, EngineParams {}, res);

            for (size_t k = 0; k < melody.noteStarts.size(); ++k)
            {
                const int start = melody.noteStarts[k] + melody.noteLengths[k] / 2 - 4096;
                expectPitch (r.left, start + r.latency, melody.noteHz[k], 3.0, "note " + juce::String ((int) k));

                const double outDb = testsig::rmsDb (r.left, start + r.latency, 8192);
                const double guideDb = testsig::rmsDb (melody.signal, start, 8192);
                expectLessThan (std::abs (outDb - guideDb), 0.75, "note " + juce::String ((int) k) + " level: out "
                                + juce::String (outDb, 2) + " dB, guide " + juce::String (guideDb, 2) + " dB");
            }

            // Gaps: the layer must be silent where the guide is.
            for (size_t k = 1; k < melody.noteStarts.size(); ++k)
            {
                const int gapStart = melody.noteStarts[k - 1] + melody.noteLengths[k - 1];
                const int gapLen = melody.noteStarts[k] - gapStart;
                const double gapDb = testsig::rmsDb (r.left, gapStart + r.latency + gapLen / 2 - 1200, 2400);
                expectLessThan (gapDb, -60.0, "gap " + juce::String ((int) k));
            }
        }
    }

    void testOnsetStability()
    {
        // Notes with vibrato and pauses; every voiced guide reading must sit on
        // the note being played (no octave errors at onsets).
        constexpr double sr = 48000.0;
        const int n = (int) (6.0 * sr), noteLen = (int) (0.75 * sr);
        const double notes[] = { 110.0, 130.81, 146.83, 164.81, 146.83, 130.81, 98.0, 110.0 };
        std::vector<float> guide ((size_t) n, 0.0f), freq ((size_t) n, 0.0f), amp ((size_t) n, 0.0f);

        for (int i = 0; i < n; ++i)
        {
            const double t = (i % noteLen) / sr;
            const double vibrato = std::pow (2.0, 0.25 / 12.0 * std::sin (testsig::twoPi * 5.5 * t) * std::min (1.0, t / 0.3));
            freq[(size_t) i] = (float) (notes[(i / noteLen) % 8] * vibrato);
            amp[(size_t) i] = t < 0.6 ? (float) (0.35 * std::min (1.0, t / 0.01)) : 0.0f;
        }

        testsig::addSaw (guide, sr, freq, amp);
        const auto layer = testsig::saw (sr, 98.0, 0.2f, n);

        for (auto res : { Resolution::tight, Resolution::normal, Resolution::deep })
        {
            beginTest ("Guide pitch has no octave errors at note onsets, resolution " + juce::String ((int) res));

            TetherEngine engine;
            engine.prepare (sr, 2);
            engine.setResolution (res);
            const int fft = TetherEngine::fftSizeFor (res, sr), hop = fft / 4;

            std::vector<float> left = layer, right = layer;
            int frame = 0, voicedFrames = 0, offFrames = 0, octaveErrors = 0;
            double worstCents = 0.0;

            for (int start = 0; start + 512 <= n; start += 512)
            {
                float* ch[2] = { left.data() + start, right.data() + start };
                const float* g[1] = { guide.data() + start };
                engine.process (ch, 2, g, 1, 512, EngineParams {});

                Telemetry t;
                while (engine.getTelemetryQueue().pop (t))
                {
                    ++frame;
                    if (t.guideHz <= 0.0f)
                        continue;

                    const int centre = frame * hop - fft / 2;
                    const double cents = testsig::cents (t.guideHz, notes[(centre / noteLen) % 8]);
                    ++voicedFrames;
                    worstCents = std::max (worstCents, std::abs (cents));
                    offFrames += std::abs (cents) > 50.0 ? 1 : 0;
                    octaveErrors += std::abs (cents) > 300.0 ? 1 : 0;
                }
            }

            expectGreaterThan (voicedFrames, frame / 2, "most frames should be voiced");
            expectEquals (octaveErrors, 0, "worst reading " + juce::String (worstCents, 0) + " cents off");
            expectLessThan (offFrames * 50, voicedFrames, juce::String (offFrames) + " of " + juce::String (voicedFrames)
                                                          + " frames more than 50 cents off");
        }
    }

    void testControls()
    {
        constexpr double sr = 48000.0;
        const int n = (int) (2.0 * sr);
        const auto layer = testsig::saw (sr, 110.0, 0.25f, n);

        beginTest ("Level 0 keeps the layer's own loudness");
        {
            const auto guide = testsig::saw (sr, 110.0, 0.05f, n);
            auto p = EngineParams {};
            p.levelAmount = 0.0f;
            p.gateDb = -80.0f;
            p.motion = 0.0f;
            const auto r = render (layer, layer, &guide, p);
            const double inDb = testsig::rmsDb (layer, (int) sr - r.latency, 8192);
            const double outDb = testsig::rmsDb (r.left, (int) sr, 8192);
            expectLessThan (std::abs (outDb - inDb), 0.5);
        }

        beginTest ("Gate mutes the layer while the guide is below threshold");
        {
            const auto guide = testsig::saw (sr, 110.0, 0.001f, n); // about -63 dBFS
            auto p = EngineParams {};
            p.levelAmount = 0.0f;
            p.gateDb = -40.0f;
            const auto r = render (layer, layer, &guide, p);
            expectLessThan (testsig::rmsDb (r.left, (int) sr, 8192), -80.0);
        }

        beginTest ("Punch boosts the layer on the guide's attacks only");
        {
            // Guide: sustained tone with a sharp attack at 0.75 s.
            std::vector<float> guide ((size_t) n, 0.0f);
            const auto tone = testsig::saw (sr, 110.0, 0.3f, n);
            const int onset = (int) (0.75 * sr);
            for (int i = onset; i < n; ++i)
                guide[(size_t) i] = tone[(size_t) i];

            auto p = EngineParams {};
            p.levelAmount = 0.0f;
            p.gateDb = -80.0f;
            p.motion = 0.0f;
            p.pitchAmount = 0.0f;
            const auto plain = render (layer, layer, &guide, p);
            p.punch = 1.0f;
            const auto punchy = render (layer, layer, &guide, p);

            const int attack = onset + plain.latency + (int) (0.004 * sr);
            const double attackBoost = testsig::rmsDb (punchy.left, attack, 480) - testsig::rmsDb (plain.left, attack, 480);
            const int sustain = onset + plain.latency + (int) (0.5 * sr);
            const double sustainBoost = testsig::rmsDb (punchy.left, sustain, 4800) - testsig::rmsDb (plain.left, sustain, 4800);

            expectGreaterThan (attackBoost, 4.0, "attack boost " + juce::String (attackBoost, 2) + " dB");
            expectLessThan (std::abs (sustainBoost), 0.5, "sustain change " + juce::String (sustainBoost, 2) + " dB");
        }

        beginTest ("Motion transfers the guide's brightness changes");
        {
            // Guide alternates between a dull (sine) and a bright (saw) tone every 250 ms.
            std::vector<float> guide ((size_t) n);
            const auto dull = testsig::sine (sr, 110.0, 0.3f, n);
            const auto bright = testsig::saw (sr, 110.0, 0.3f, n);
            const int seg = (int) (0.25 * sr);
            for (int i = 0; i < n; ++i)
                guide[(size_t) i] = ((i / seg) % 2 == 0) ? dull[(size_t) i] : bright[(size_t) i];

            auto brightness = [&] (const std::vector<float>& x, int start, int len)
            {
                expect (start > 0 && start + len <= (int) x.size(), "measurement window must lie inside the signal");
                len = juce::jmin (len, (int) x.size() - start);

                // Energy above 1 kHz relative to total, via first difference as a crude high-pass.
                double hi = 0.0, all = 0.0;
                for (int i = start + 1; i < start + len; ++i)
                {
                    const double d = x[(size_t) i] - x[(size_t) (i - 1)];
                    hi += d * d;
                    all += (double) x[(size_t) i] * x[(size_t) i];
                }
                return 10.0 * std::log10 (hi / (all + 1.0e-20) + 1.0e-20);
            };

            auto p = EngineParams {};
            p.pitchAmount = 0.0f;
            p.motion = 1.0f;
            const auto moved = render (layer, layer, &guide, p);
            p.motion = 0.0f;
            const auto still = render (layer, layer, &guide, p);

            const int dullSeg = 4 * seg + seg / 2, brightSeg = 5 * seg + seg / 2;
            const double movedSwing = brightness (moved.left, brightSeg + moved.latency, 4096)
                                    - brightness (moved.left, dullSeg + moved.latency, 4096);
            const double stillSwing = brightness (still.left, brightSeg + still.latency, 4096)
                                    - brightness (still.left, dullSeg + still.latency, 4096);

            expectGreaterThan (movedSwing, stillSwing + 6.0,
                               "brightness swing with motion " + juce::String (movedSwing, 2)
                               + " dB vs without " + juce::String (stillSwing, 2) + " dB");
        }
    }

    void testBlockSizeInvariance()
    {
        beginTest ("Output does not depend on the host block size");

        constexpr double sr = 48000.0;
        const auto melody = makeMelody (sr, { 110.0, 164.81 }, { 0.4f, 0.2f }, 0.3, 0.1);
        const int n = (int) melody.signal.size();
        const auto layerL = testsig::saw (sr, 146.83, 0.2f, n);
        const auto layerR = testsig::noise (n, 0.05f, 3);

        EngineParams p;
        p.punch = 0.5f;
        p.tone = 0.5f;
        const auto reference = render (layerL, layerR, &melody.signal, p, Resolution::normal, sr, 512);

        for (int block : { 1, 7, 64, 441, 4096 })
        {
            const auto r = render (layerL, layerR, &melody.signal, p, Resolution::normal, sr, block);
            expectLessThan (maxAbsDifference (r.left, reference.left), 1.0e-6f, "block " + juce::String (block));
            expectLessThan (maxAbsDifference (r.right, reference.right), 1.0e-6f, "block " + juce::String (block));
        }

        beginTest ("Identical channels stay identical");
        const auto mono = render (layerL, layerL, &melody.signal, p);
        expectLessThan (maxAbsDifference (mono.left, mono.right), 1.0e-7f);
    }

    void testRobustness()
    {
        beginTest ("Extreme inputs and settings never produce NaN, Inf or runaway gain");

        const int n = 48000;

        std::vector<std::vector<float>> inputs;
        inputs.push_back (std::vector<float> ((size_t) n, 0.0f));
        inputs.push_back (std::vector<float> ((size_t) n, 0.5f));
        inputs.push_back (testsig::noise (n, 1.0f, 11));
        {
            std::vector<float> clicks ((size_t) n, 0.0f);
            for (int i = 0; i < n; i += 4000)
                clicks[(size_t) i] = 1.0f;
            inputs.push_back (clicks);
        }

        EngineParams extremes[3];
        extremes[1].pitchAmount = 1.0f; extremes[1].glideMs = 0.0f; extremes[1].octave = 3; extremes[1].semitones = 12;
        extremes[1].fineCents = 100.0f; extremes[1].formantShift = 12.0f; extremes[1].vibrato = 2.0f; extremes[1].scale = Scale::fifths;
        extremes[1].attackMs = 0.1f; extremes[1].releaseMs = 5.0f; extremes[1].punch = 1.0f; extremes[1].gateDb = 0.0f;
        extremes[1].motion = 1.0f; extremes[1].tone = 1.0f; extremes[1].outputDb = 24.0f;
        extremes[2].octave = -3; extremes[2].semitones = -12; extremes[2].glideMs = 500.0f; extremes[2].attackMs = 100.0f;
        extremes[2].fineCents = -100.0f; extremes[2].formantShift = -12.0f; extremes[2].formant = false; extremes[2].vibrato = 0.0f;
        extremes[2].layerAuto = false; extremes[2].layerRoot = 12.0f;
        extremes[2].releaseMs = 1000.0f; extremes[2].gateDb = -80.0f; extremes[2].mix = 0.0f; extremes[2].outputDb = -24.0f;

        for (const auto& layer : inputs)
        {
            for (const auto& guide : inputs)
            {
                for (const auto& p : extremes)
                {
                    for (auto res : { Resolution::tight, Resolution::deep })
                    {
                        const auto r = render (layer, layer, &guide, p, res, 48000.0, 512, (&p - extremes) == 2 ? Engine::spectral : Engine::natural);
                        bool finite = true;
                        float peak = 0.0f;
                        for (float v : r.left)
                        {
                            finite = finite && std::isfinite (v);
                            peak = std::max (peak, std::abs (v));
                        }

                        expect (finite, "non-finite output");
                        expectLessThan (peak, 100.0f, "runaway gain");
                    }
                }
            }
        }
    }
};

static EngineTests engineTests;
