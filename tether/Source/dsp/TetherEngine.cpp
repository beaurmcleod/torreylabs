#include "TetherEngine.h"

namespace tether
{

const char* scaleName (Scale s) noexcept
{
    switch (s)
    {
        case Scale::off:             return "Off";
        case Scale::chromatic:       return "Chromatic";
        case Scale::major:           return "Major";
        case Scale::minor:           return "Minor";
        case Scale::harmonicMinor:   return "Harmonic Minor";
        case Scale::dorian:          return "Dorian";
        case Scale::phrygian:        return "Phrygian";
        case Scale::lydian:          return "Lydian";
        case Scale::mixolydian:      return "Mixolydian";
        case Scale::majorPentatonic: return "Major Pentatonic";
        case Scale::minorPentatonic: return "Minor Pentatonic";
        case Scale::blues:           return "Blues";
        case Scale::wholeTone:       return "Whole Tone";
        case Scale::octaves:         return "Octaves";
        case Scale::fifths:          return "Fifths";
        case Scale::count:           break;
    }

    return "";
}

const char* keyName (int key) noexcept
{
    static const char* const names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return names[((key % 12) + 12) % 12];
}

static unsigned scaleMask (Scale s) noexcept
{
    auto set = [] (std::initializer_list<int> degrees)
    {
        unsigned mask = 0;
        for (int d : degrees)
            mask |= 1u << d;
        return mask;
    };

    switch (s)
    {
        case Scale::chromatic:       return 0xfffu;
        case Scale::major:           return set ({ 0, 2, 4, 5, 7, 9, 11 });
        case Scale::minor:           return set ({ 0, 2, 3, 5, 7, 8, 10 });
        case Scale::harmonicMinor:   return set ({ 0, 2, 3, 5, 7, 8, 11 });
        case Scale::dorian:          return set ({ 0, 2, 3, 5, 7, 9, 10 });
        case Scale::phrygian:        return set ({ 0, 1, 3, 5, 7, 8, 10 });
        case Scale::lydian:          return set ({ 0, 2, 4, 6, 7, 9, 11 });
        case Scale::mixolydian:      return set ({ 0, 2, 4, 5, 7, 9, 10 });
        case Scale::majorPentatonic: return set ({ 0, 2, 4, 7, 9 });
        case Scale::minorPentatonic: return set ({ 0, 3, 5, 7, 10 });
        case Scale::blues:           return set ({ 0, 3, 5, 6, 7, 10 });
        case Scale::wholeTone:       return set ({ 0, 2, 4, 6, 8, 10 });
        case Scale::octaves:         return set ({ 0 });
        case Scale::fifths:          return set ({ 0, 7 });
        case Scale::off:
        case Scale::count:           break;
    }

    return 0;
}

float quantiseToScale (float midi, Scale scale, int key) noexcept
{
    const unsigned mask = scaleMask (scale);

    if (mask == 0 || ! std::isfinite (midi))
        return midi;

    const int centre = (int) std::lround (midi);
    int best = centre;
    float bestDistance = 1.0e9f;

    for (int n = centre - 7; n <= centre + 7; ++n)
    {
        const int degree = (((n - key) % 12) + 12) % 12;

        if ((mask & (1u << degree)) == 0)
            continue;

        const float distance = std::abs ((float) n - midi);

        // Ties go to the lower note.
        if (distance < bestDistance - 1.0e-6f || (std::abs (distance - bestDistance) <= 1.0e-6f && n < best))
        {
            bestDistance = distance;
            best = n;
        }
    }

    return (float) best;
}

//==============================================================================
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

int TetherEngine::maxPeriodFor (Resolution r, double rate) noexcept
{
    return (int) std::ceil (rate / lowestPitchFor (r));
}

int TetherEngine::pitchDelayFor (Resolution r, double rate) noexcept
{
    // Both engines share one delay: the STFT's frame, or the grain engine's
    // look-ahead, whichever is longer.
    const int fft = fftSizeFor (r, rate);
    return std::max (fft, GrainShifter::minimumDelay (maxPeriodFor (r, rate), fft / 4));
}

int TetherEngine::latencyFor (Resolution r, double rate) noexcept
{
    return pitchDelayFor (r, rate) + detectorLength (r, rate) / 2;
}

void TetherEngine::prepare (double newSampleRate, int numLayerChannels)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
    channels = std::clamp (numLayerChannels, 1, maxChannels);
    activeChannels = channels;

    const int smallest = fftSizeFor (Resolution::tight, sampleRate);
    const int largest  = fftSizeFor (Resolution::deep, sampleRate);
    const int longestPeriod = maxPeriodFor (Resolution::deep, sampleRate);
    const int longestPitchDelay = pitchDelayFor (Resolution::deep, sampleRate);
    const int longestDetector = detectorLength (Resolution::deep, sampleRate);
    const int longestDelay = longestPitchDelay + longestDetector;

    ffts.prepare (log2Int (smallest), log2Int (largest));
    guideTracker.prepare (largest);
    layerTracker.prepare (largest);
    spectral.prepare (largest);
    grains.prepare (longestPeriod, largest / 4, longestPitchDelay);
    motionAnalyser.prepare (largest);

    for (int c = 0; c < maxChannels; ++c)
    {
        layerRing[c].prepare (longestDelay + largest + 1);
        wetRing[c].prepare (longestDelay + 1);
        layerFrame[c].assign ((size_t) largest, 0.0f);
        synthFrame[c].assign ((size_t) largest, 0.0f);
        outAcc[c].assign ((size_t) largest, 0.0f);
        outReady[c].assign ((size_t) largest / 4, 0.0f);
    }

    guideRing.prepare (longestDelay + largest + 1);
    wetMono.prepare (largest + 1);
    guideFrame.assign ((size_t) largest, 0.0f);
    monoFrame.assign ((size_t) largest, 0.0f);
    wetFrame.assign ((size_t) largest, 0.0f);
    guideRms.prepare (longestDetector + longestPeriod);
    layerRms.prepare (longestDetector + longestPeriod);
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
    maxPeriod = maxPeriodFor (r, sampleRate);
    pitchDelay = pitchDelayFor (r, sampleRate);

    detectorLength_ = detectorLength (r, sampleRate);
    detectorDelay = detectorLength_ / 2;
    guideRms.setLength (detectorLength_);
    layerRms.setLength (detectorLength_);
    outputRms.setLength (detectorLength_);

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
    grains.configure (maxPeriod, hopSize, pitchDelay);
    motionAnalyser.configure (sampleRate, fftSize, fft);
    motionEq.configure (sampleRate, motionAnalyser);
    setEngine (engineMode);
}

void TetherEngine::setEngine (Engine e) noexcept
{
    engineMode = e;

    if (! prepared)
        return;

    // The grain engine's output block sits pitchDelay behind the input; its
    // guide analysis frame is centred on that block. The spectral engine
    // works on the newest frame and is delayed afterwards.
    guideAnalysisDelay = e == Engine::natural ? pitchDelay - fftSize / 2 - hopSize / 2 : 0;
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
    wetMono.clear();
    guideFollower.reset();
    layerFollower.reset();
    spectral.reset();
    grains.reset();
    motionAnalyser.reset();
    motionEq.reset();

    hopPos = 0;
    shiftSmoothed = 0.0f;
    slowMidi[0] = slowMidi[1] = lagMidi[0] = lagMidi[1] = 0.0f;
    hasSlow = false;

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

    activeChannels = std::min (numLayerChannels, channels);

    if (activeChannels <= 0)
        return;

    const int numCh = activeChannels;
    const bool guideConnected = guide != nullptr && numGuideChannels > 0;
    const float guideScale = guideConnected ? 1.0f / (float) numGuideChannels : 0.0f;

    // Level: the guide's level (dB) is shaped by attack/release, then the
    // layer's own measured level is divided out and replaced by it.
    const float attackCoeff  = onePoleCoeff (std::max (p.attackMs, 0.05f) * 0.001f, sampleRate);
    const float releaseCoeff = onePoleCoeff (std::max (p.releaseMs, 1.0f) * 0.001f, sampleRate);
    const float levelAmount = std::clamp (p.levelAmount, 0.0f, 1.0f);
    constexpr float floorDb = -120.0f;
    constexpr float layerFloorDb = -72.0f;  // don't chase an almost silent layer
    constexpr float maxBoostDb = 24.0f;

    // Gate: opens above the threshold, closes 6 dB lower after a hold of one
    // detector window, fading out over the release time.
    const bool gateEnabled = p.gateDb > -79.9f;
    const float gateCloseDb = p.gateDb - 6.0f;
    const int gateHoldSamples = detectorLength_;
    const float gateOpenStep  = 1.0f / (0.001f * (float) sampleRate);
    const float gateCloseStep = 1.0f / (std::max (p.releaseMs, 5.0f) * 0.001f * (float) sampleRate);

    // Punch: ratio of a fast and a slow envelope of the guide.
    const float punchAmount = std::clamp (p.punch, 0.0f, 1.0f);
    constexpr float punchKnee = 1.21f;  // energy ratio (+0.8 dB) below which ripple is ignored
    punchFast.setTimes (1.0f, 25.0f, sampleRate);
    punchSlow.setTimes (40.0f, 250.0f, sampleRate);

    const float mixTarget = std::clamp (p.mix, 0.0f, 1.0f);
    const float outTarget = dbToGain (p.outputDb);
    const float listenTarget = (p.listen && guideConnected) ? 1.0f : 0.0f;
    const float smoothCoeff = onePoleCoeff (0.02f, sampleRate);

    if (! smoothersPrimed)
    {
        mixSmoothed = mixTarget;
        outGainSmoothed = outTarget;
        listenSmoothed = listenTarget;
        smoothersPrimed = true;
    }

    const int dryDelay = pitchDelay + detectorDelay;
    const int wetDelay = detectorDelay + (engineMode == Engine::spectral ? pitchDelay - fftSize : 0);

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
        const bool eqActive = ! motionEq.isBypassed();

        for (int c = 0; c < numCh; ++c)
        {
            layerRing[c].push (layer[c][i]);
            dry[c] = layerRing[c].delayed (dryDelay);

            float fresh = outReady[c][(size_t) hopPos];
            if (eqActive)
                fresh = motionEq.process (c, fresh);

            wetEnergy += fresh * fresh;
            wetRing[c].push (fresh);
            wet[c] = wetRing[c].delayed (wetDelay);
        }

        if (eqActive)
            motionEq.advance();

        // Both detector windows are centred on the sample being output.
        const float guideAhead = guideRing.delayed (pitchDelay);
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
        listenSmoothed += smoothCoeff * (listenTarget - listenSmoothed);

        const float wetGain = gain * mixSmoothed * outGainSmoothed;
        const float dryGain = (1.0f - mixSmoothed) * outGainSmoothed;
        const float listenGain = listenSmoothed * outGainSmoothed;
        const float layerGain = 1.0f - listenSmoothed;
        float outEnergy = 0.0f;

        for (int c = 0; c < numCh; ++c)
        {
            const float y = (wet[c] * wetGain + dry[c] * dryGain) * layerGain + guideAligned * listenGain;
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
    const double frameRate = sampleRate / hopSize;

    for (int c = 0; c < numCh; ++c)
        layerRing[c].copyLatest (layerFrame[c].data(), n);

    guideRing.copyLatest (guideFrame.data(), n, guideAnalysisDelay);

    const float monoScale = 1.0f / (float) numCh;
    for (int i = 0; i < n; ++i)
    {
        float sum = 0.0f;
        for (int c = 0; c < numCh; ++c)
            sum += layerFrame[c][(size_t) i];

        monoFrame[(size_t) i] = sum * monoScale;
    }

    PitchEstimate guideEstimate;
    if (guideConnected)
        guideEstimate = guideTracker.analyse (guideFrame.data());

    const PitchEstimate layerEstimate = layerTracker.analyse (monoFrame.data());
    guideFollower.update (guideEstimate);
    layerFollower.update (layerEstimate);

    const bool guidePresent = guideConnected && guideEstimate.levelDb > -75.0f;

    // Level windows cover a whole number of periods (about the base length), so
    // a low note's waveform doesn't ripple the readings: the guide's window
    // follows the guide, the layer's follows the pitch the layer is moved to.
    auto wholePeriods = [this] (float hz)
    {
        const float period = (float) sampleRate / std::max (hz, 1.0f);
        return (int) std::lround (std::max (1.0f, std::round ((float) detectorLength_ / period)) * period);
    };

    if (const int window = guideFollower.isVoiced() ? wholePeriods (guideFollower.getHz()) : detectorLength_; window != guideRms.getLength())
        guideRms.setLength (window);
    const float layerHz = (p.layerAuto && layerFollower.hasPitch()) ? layerFollower.getHz()
                                                                      : midiToHz (p.layerRoot);
    const float layerMidi = hzToMidi (layerHz);
    float targetShift = 0.0f;
    const bool following = guideConnected && guideFollower.hasPitch() && p.pitchAmount > 0.0f;

    if (following)
    {
        // The guide's pitch is split into the note and its vibrato / bends:
        // the note can be quantised, the vibrato scaled. A leap of more than
        // 1.5 semitones is a new note and restarts the smoothing; anything
        // slower is smoothed out of the note (two poles, ~100 ms each), and
        // the lag that leaves behind on slides is recovered by a slower
        // smoother so a slide still lands where the guide is.
        const float guideMidi = hzToMidi (guideFollower.getHz());

        if (! hasSlow || std::abs (guideMidi - slowMidi[1]) > 1.5f)
        {
            slowMidi[0] = slowMidi[1] = guideMidi;
            lagMidi[0] = lagMidi[1] = 0.0f;
            hasSlow = true;
        }
        else
        {
            const float noteCoeff = onePoleCoeff (0.1f, frameRate), lagCoeff = onePoleCoeff (0.12f, frameRate);
            slowMidi[0] += noteCoeff * (guideMidi - slowMidi[0]);
            slowMidi[1] += noteCoeff * (slowMidi[0] - slowMidi[1]);
            lagMidi[0] += lagCoeff * ((guideMidi - slowMidi[1]) - lagMidi[0]);
            lagMidi[1] += lagCoeff * (lagMidi[0] - lagMidi[1]);
        }

        const float noteMidi = slowMidi[1] + lagMidi[1];
        const float vibratoMidi = guideMidi - noteMidi;
        float note = noteMidi + 12.0f * (float) p.octave + (float) p.semitones;

        if (p.scale != Scale::off)
            note = quantiseToScale (note, p.scale, p.key);

        const float targetMidi = note + std::clamp (p.vibrato, 0.0f, 2.0f) * vibratoMidi + p.fineCents / 100.0f;
        targetShift = std::clamp (targetMidi - layerMidi, -48.0f, 48.0f) * std::clamp (p.pitchAmount, 0.0f, 1.0f);
    }

    shiftSmoothed += onePoleCoeff (p.glideMs * 0.001f, frameRate) * (targetShift - shiftSmoothed);

    // Settle exactly so an untouched layer takes the bit-transparent path.
    if (! following && std::abs (shiftSmoothed) < 1.0e-4f)
        shiftSmoothed = 0.0f;

    const float ratio = std::exp2 (shiftSmoothed / 12.0f);
    const float formantRatio = std::exp2 (p.formantShift / 12.0f) * (p.formant ? 1.0f : ratio);

    if (const int window = wholePeriods (layerHz * ratio); window != layerRms.getLength())
        layerRms.setLength (window);
    const bool identity = std::abs (shiftSmoothed) < 1.0e-9f && std::abs (formantRatio - 1.0f) < 1.0e-6f;

    if (engineMode == Engine::natural)
    {
        const float* newest[maxChannels] = { layerFrame[0].data() + n - hopSize, layerFrame[1].data() + n - hopSize };
        grains.push (newest, numCh);

        GrainShifter::Controls controls;
        controls.analysisPeriod = (float) sampleRate / layerHz;
        controls.outputPeriod = controls.analysisPeriod / ratio;
        controls.formantRatio = identity ? 1.0f : formantRatio;
        controls.lockToInput = identity;

        float* outPtrs[maxChannels] = { outReady[0].data(), outReady[1].data() };
        grains.render (controls, outPtrs, numCh);
    }
    else
    {
        SpectralLayer::FrameParams frame;
        frame.ratio = ratio;
        frame.formantRatio = identity ? 1.0f : formantRatio;
        frame.sourceHz = layerHz;

        const float* layerPtrs[maxChannels] = { layerFrame[0].data(), layerFrame[1].data() };
        float* synthPtrs[maxChannels] = { synthFrame[0].data(), synthFrame[1].data() };
        spectral.processFrame (layerPtrs, numCh, frame, synthPtrs);

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
    }

    // Motion / tone: compare the guide with the shifted layer, then set the EQ.
    for (int i = 0; i < hopSize; ++i)
    {
        float sum = 0.0f;
        for (int c = 0; c < numCh; ++c)
            sum += outReady[c][(size_t) i];
        wetMono.push (sum * monoScale);
    }

    wetMono.copyLatest (wetFrame.data(), n);

    MotionAnalyser::Params motionParams;
    motionParams.motion = guideConnected ? std::clamp (p.motion, 0.0f, 1.0f) : 0.0f;
    motionParams.tone = guideConnected ? std::clamp (p.tone, 0.0f, 1.0f) : 0.0f;
    motionParams.guidePresent = guidePresent;
    motionAnalyser.analyse (guideConnected ? guideFrame.data() : nullptr, wetFrame.data(), motionParams);
    motionEq.setTargets (motionAnalyser, hopSize);

    const int bands = motionAnalyser.getNumBands();
    for (int b = 0; b < bands; ++b)
        displayBandGains[(size_t) b].store (motionAnalyser.getBandGainDb (b), std::memory_order_relaxed);
    displayBandCount.store (bands, std::memory_order_relaxed);

    Telemetry t;
    t.guideConnected = guideConnected;
    t.guideHz = guideFollower.isVoiced() ? guideFollower.getHz() : 0.0f;
    t.layerHz = layerFollower.isVoiced() ? layerFollower.getHz() : 0.0f;
    t.outputHz = (layerFollower.isVoiced() || ! p.layerAuto) ? layerHz * ratio : 0.0f;
    t.shiftSemitones = shiftSmoothed;
    t.guideDb = lastGuideDb;
    t.layerDb = lastLayerDb;
    t.outputDb = lastOutputDb;
    t.gate = gateGain;
    telemetry.push (t);
}

} // namespace tether
