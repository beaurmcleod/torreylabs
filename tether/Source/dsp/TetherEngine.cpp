#include "TetherEngine.h"

namespace tether
{

int TetherEngine::fftSizeFor (Resolution r, double rate) noexcept
{
    const int base = r == Resolution::tight ? 1024 : (r == Resolution::normal ? 2048 : 4096);
    const int scale = rate <= 50000.0 ? 1 : (rate <= 100000.0 ? 2 : 4);
    return base * scale;
}

float TetherEngine::lowestPitchFor (Resolution r) noexcept
{
    return r == Resolution::tight ? 90.0f : (r == Resolution::normal ? 45.0f : 25.0f);
}

float TetherEngine::detectorWindowMs (Resolution r) noexcept
{
    // About one period of the lowest note each mode tracks, so level readings
    // don't ripple with the waveform.
    return r == Resolution::tight ? 8.0f : (r == Resolution::normal ? 12.0f : 20.0f);
}

static int detectorLength (Resolution r, double rate) noexcept
{
    return std::max (2, (int) std::lround (TetherEngine::detectorWindowMs (r) * 0.001 * rate));
}

int TetherEngine::latencyFor (Resolution r, double rate) noexcept
{
    return fftSizeFor (r, rate) + detectorLength (r, rate) / 2;
}

void TetherEngine::prepare (double newSampleRate, int numLayerChannels)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
    channels = juce::jlimit (1, maxChannels, numLayerChannels);
    activeChannels = channels;

    const int smallest = fftSizeFor (Resolution::tight, sampleRate);
    const int largest  = fftSizeFor (Resolution::deep, sampleRate);
    const int longestDetector = detectorLength (Resolution::deep, sampleRate);
    const int longestDelay = largest + longestDetector;

    ffts.prepare (log2Int (smallest), log2Int (largest));
    guideTracker.prepare (largest);
    layerTracker.prepare (largest);
    spectral.prepare (largest);

    for (int c = 0; c < maxChannels; ++c)
    {
        layerRing[c].prepare (longestDelay + 1);
        wetRing[c].prepare (longestDetector + 1);
        layerFrame[c].assign ((size_t) largest, 0.0f);
        synthFrame[c].assign ((size_t) largest, 0.0f);
        outAcc[c].assign ((size_t) largest, 0.0f);
        outReady[c].assign ((size_t) largest / 4, 0.0f);
    }

    guideRing.prepare (longestDelay + 1);
    guideFrame.assign ((size_t) largest, 0.0f);
    monoFrame.assign ((size_t) largest, 0.0f);
    guideRms.prepare (longestDetector);
    layerRms.prepare (longestDetector);
    outputRms.prepare (longestDetector);

    prepared = true;
    setResolution (resolution);
}

void TetherEngine::setResolution (Resolution r) noexcept
{
    resolution = r;

    if (! prepared)
        return;

    fftSize = fftSizeFor (r, sampleRate);
    hopSize = fftSize / 4;

    const int detector = detectorLength (r, sampleRate);
    detectorDelay = detector / 2;
    guideRms.setLength (detector);
    layerRms.setLength (detector);
    outputRms.setLength (detector);

    // The guide is followed immediately (unclear big leaps after two repeats);
    // the layer's own pitch must hold ~40 ms (big leaps ~80 ms) before it
    // counts, because a mistake there moves the whole output.
    const double hopSeconds = hopSize / sampleRate;
    guideFollower.setConfirmationFrames (1, 3);
    layerFollower.setConfirmationFrames ((int) std::ceil (0.04 / hopSeconds), (int) std::ceil (0.08 / hopSeconds));

    const auto* fft = ffts.get (fftSize);
    guideTracker.configure (sampleRate, fftSize, lowestPitchFor (r), 2000.0f, fft);
    layerTracker.configure (sampleRate, fftSize, lowestPitchFor (r), 2000.0f, fft);
    spectral.configure (sampleRate, fftSize, fft);
    reset();
}

void TetherEngine::reset() noexcept
{
    for (int c = 0; c < maxChannels; ++c)
    {
        layerRing[c].clear();
        wetRing[c].clear();
        std::fill (outAcc[c].begin(), outAcc[c].end(), 0.0f);
        std::fill (outReady[c].begin(), outReady[c].end(), 0.0f);
    }

    guideRing.clear();
    guideFollower.reset();
    layerFollower.reset();
    spectral.reset();

    hopPos = 0;
    shiftSmoothed = 0.0f;

    guideRms.reset();
    layerRms.reset();
    outputRms.reset();
    guideShapedDb = lastGuideDb = lastLayerDb = lastOutputDb = -120.0f;
    punchFast.reset();
    punchSlow.reset();

    gateGain = 0.0f;
    gateOpen = false;
    gateHold = 0;
    smoothersPrimed = false;
}

//==============================================================================
void TetherEngine::process (float* const* layer, int numLayerChannels,
                            const float* const* guide, int numGuideChannels,
                            int numSamples, const EngineParams& p) noexcept
{
    if (! prepared || numSamples <= 0 || layer == nullptr)
        return;

    activeChannels = juce::jmin (numLayerChannels, channels);

    if (activeChannels <= 0)
        return;

    const int numCh = activeChannels;
    const bool guideConnected = guide != nullptr && numGuideChannels > 0;
    const float guideScale = guideConnected ? 1.0f / (float) numGuideChannels : 0.0f;

    // Level: the guide's level (dB) is shaped by attack/release, then the
    // layer's own measured level is divided out and replaced by it.
    const float attackCoeff  = onePoleCoeff (std::max (p.attackMs, 0.05f) * 0.001f, sampleRate);
    const float releaseCoeff = onePoleCoeff (std::max (p.releaseMs, 1.0f) * 0.001f, sampleRate);
    const float levelAmount = juce::jlimit (0.0f, 1.0f, p.levelAmount);
    constexpr float floorDb = -120.0f;
    constexpr float layerFloorDb = -72.0f;  // don't chase an almost silent layer
    constexpr float maxBoostDb = 24.0f;

    // Gate: opens above the threshold, closes 6 dB lower after a hold of one
    // detector window, fading out over the release time.
    const bool gateEnabled = p.gateDb > -79.9f;
    const float gateCloseDb = p.gateDb - 6.0f;
    const int gateHoldSamples = guideRms.getLength();
    const float gateOpenStep  = 1.0f / (0.001f * (float) sampleRate);
    const float gateCloseStep = 1.0f / (std::max (p.releaseMs, 5.0f) * 0.001f * (float) sampleRate);

    // Punch: ratio of a fast and a slow envelope of the guide.
    const float punchAmount = juce::jlimit (0.0f, 1.0f, p.punch);
    constexpr float punchKnee = 1.21f;  // energy ratio (+0.8 dB) below which ripple is ignored
    punchFast.setTimes (1.0f, 25.0f, sampleRate);
    punchSlow.setTimes (40.0f, 250.0f, sampleRate);

    const float mixTarget = juce::jlimit (0.0f, 1.0f, p.mix);
    const float outTarget = dbToGain (p.outputDb);
    const float smoothCoeff = onePoleCoeff (0.02f, sampleRate);

    if (! smoothersPrimed)
    {
        mixSmoothed = mixTarget;
        outGainSmoothed = outTarget;
        smoothersPrimed = true;
    }

    const int wetDelay = detectorDelay, dryDelay = fftSize + detectorDelay;

    for (int i = 0; i < numSamples; ++i)
    {
        float g = 0.0f;

        if (guideConnected)
        {
            for (int gc = 0; gc < numGuideChannels; ++gc)
                g += guide[gc][i];

            g *= guideScale;
        }

        guideRing.push (g);

        float wet[maxChannels], dry[maxChannels];
        float wetEnergy = 0.0f;

        for (int c = 0; c < numCh; ++c)
        {
            layerRing[c].push (layer[c][i]);
            dry[c] = layerRing[c].delayed (dryDelay);

            const float fresh = outReady[c][(size_t) hopPos];
            wetEnergy += fresh * fresh;
            wetRing[c].push (fresh);
            wet[c] = wetRing[c].delayed (wetDelay);
        }

        // Both detector windows are centred on the sample being output.
        const float guideAhead = guideRing.delayed (fftSize);
        const float guideAligned = guideRing.delayed (dryDelay);
        const float guideDb = std::max (floorDb, energyToDb (guideRms.process (guideAhead * guideAhead)));
        const float layerDb = std::max (floorDb, energyToDb (layerRms.process (wetEnergy / (float) numCh)));

        guideShapedDb += (guideDb > guideShapedDb ? attackCoeff : releaseCoeff) * (guideDb - guideShapedDb);

        const float alignedEnergy = guideAligned * guideAligned;
        const float fastEnv = punchFast.process (alignedEnergy);
        const float slowEnv = punchSlow.process (alignedEnergy);

        float gainDb = 0.0f;

        if (guideConnected)
        {
            if (levelAmount > 0.0f)
                gainDb = std::min (maxBoostDb, levelAmount * (guideShapedDb - std::max (layerDb, layerFloorDb)));

            if (gateEnabled)
            {
                if (guideDb > p.gateDb)
                {
                    gateOpen = true;
                    gateHold = gateHoldSamples;
                }
                else if (gateOpen && guideDb < gateCloseDb)
                {
                    if (gateHold > 0)
                        --gateHold;
                    else
                        gateOpen = false;
                }

                gateGain = gateOpen ? std::min (1.0f, gateGain + gateOpenStep)
                                    : std::max (0.0f, gateGain - gateCloseStep);
            }
            else
            {
                gateGain = 1.0f;
            }

            if (punchAmount > 0.0f)
            {
                const float transient = fastEnv / std::max (slowEnv, 1.0e-10f);

                if (transient > punchKnee)
                    gainDb += punchAmount * 5.0f * std::log10 (std::min (transient / punchKnee, 16.0f));
            }
        }

        const float gain = guideConnected ? dbToGain (gainDb) * gateGain : 1.0f;

        mixSmoothed += smoothCoeff * (mixTarget - mixSmoothed);
        outGainSmoothed += smoothCoeff * (outTarget - outGainSmoothed);

        const float wetGain = gain * mixSmoothed * outGainSmoothed;
        const float dryGain = (1.0f - mixSmoothed) * outGainSmoothed;
        float outEnergy = 0.0f;

        for (int c = 0; c < numCh; ++c)
        {
            const float y = wet[c] * wetGain + dry[c] * dryGain;
            layer[c][i] = y;
            outEnergy += y * y;
        }

        lastOutputDb = std::max (floorDb, energyToDb (outputRms.process (outEnergy / (float) numCh)));
        lastGuideDb = guideDb;
        lastLayerDb = layerDb;

        if (++hopPos == hopSize)
        {
            hopPos = 0;
            processFrame (p, guideConnected);
        }
    }
}

//==============================================================================
void TetherEngine::processFrame (const EngineParams& p, bool guideConnected) noexcept
{
    const int n = fftSize, numCh = activeChannels;

    for (int c = 0; c < numCh; ++c)
        layerRing[c].copyLatest (layerFrame[c].data(), n);

    guideRing.copyLatest (guideFrame.data(), n);

    const float monoScale = 1.0f / (float) numCh;
    for (int i = 0; i < n; ++i)
    {
        float sum = 0.0f;
        for (int c = 0; c < numCh; ++c)
            sum += layerFrame[c][(size_t) i];

        monoFrame[(size_t) i] = sum * monoScale;
    }

    // Pitch analysis looks at the same window the STFT is about to process.
    PitchEstimate guideEstimate;
    if (guideConnected)
        guideEstimate = guideTracker.analyse (guideFrame.data());

    const PitchEstimate layerEstimate = layerTracker.analyse (monoFrame.data());
    guideFollower.update (guideEstimate);
    layerFollower.update (layerEstimate);

    const bool guidePresent = guideConnected && guideEstimate.levelDb > -75.0f;
    const float layerHz = (p.layerAuto && layerFollower.hasPitch()) ? layerFollower.getHz()
                                                                      : midiToHz (p.layerRoot);
    float targetShift = 0.0f;
    const bool following = guideConnected && guideFollower.hasPitch() && p.pitchAmount > 0.0f;

    if (following)
    {
        const float targetHz = guideFollower.getHz() * std::exp2 ((float) p.octave + (float) p.semitones / 12.0f);
        targetShift = juce::jlimit (-48.0f, 48.0f, 12.0f * std::log2 (targetHz / layerHz))
                    * juce::jlimit (0.0f, 1.0f, p.pitchAmount);
    }

    shiftSmoothed += onePoleCoeff (p.glideMs * 0.001f, sampleRate / hopSize) * (targetShift - shiftSmoothed);

    // Settle exactly so an untouched layer takes the bit-transparent path.
    if (! following && std::abs (shiftSmoothed) < 1.0e-4f)
        shiftSmoothed = 0.0f;

    SpectralLayer::FrameParams frame;
    frame.ratio = std::exp2 (shiftSmoothed / 12.0f);
    frame.preserveFormants = p.formant;
    frame.sourceHz = layerHz;
    frame.motion = guideConnected ? juce::jlimit (0.0f, 1.0f, p.motion) : 0.0f;
    frame.tone = guideConnected ? juce::jlimit (0.0f, 1.0f, p.tone) : 0.0f;
    frame.guidePresent = guidePresent;

    const float* layerPtrs[maxChannels] = { layerFrame[0].data(), layerFrame[1].data() };
    float* synthPtrs[maxChannels] = { synthFrame[0].data(), synthFrame[1].data() };
    spectral.processFrame (layerPtrs, numCh, guideFrame.data(), frame, synthPtrs);

    for (int c = 0; c < numCh; ++c)
    {
        float* acc = outAcc[c].data();
        const float* synth = synthFrame[c].data();

        for (int i = 0; i < n; ++i)
            acc[i] += synth[i];

        std::copy (acc, acc + hopSize, outReady[c].data());
        std::copy (acc + hopSize, acc + n, acc);
        std::fill (acc + n - hopSize, acc + n, 0.0f);
    }

    const int bands = spectral.getNumBands();
    for (int b = 0; b < bands; ++b)
        displayBandGains[(size_t) b].store (spectral.getBandGainDb (b), std::memory_order_relaxed);
    displayBandCount.store (bands, std::memory_order_relaxed);

    Telemetry t;
    t.guideConnected = guideConnected;
    t.guideHz = guideFollower.isVoiced() ? guideFollower.getHz() : 0.0f;
    t.layerHz = layerFollower.isVoiced() ? layerFollower.getHz() : 0.0f;
    t.outputHz = (layerFollower.isVoiced() || ! p.layerAuto) ? layerHz * frame.ratio : 0.0f;
    t.shiftSemitones = shiftSmoothed;
    t.guideDb = lastGuideDb;
    t.layerDb = lastLayerDb;
    t.outputDb = lastOutputDb;
    t.gate = gateGain;
    telemetry.push (t);
}

} // namespace tether
