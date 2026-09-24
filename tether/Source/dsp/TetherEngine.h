#pragma once

#include "DspUtils.h"
#include "FFTBank.h"
#include "PitchTracker.h"
#include "SpectralLayer.h"

#include <array>

namespace tether
{

/** Analysis/processing resolution. Bigger FFTs resolve lower notes but add latency. */
enum class Resolution : int
{
    tight  = 0, // 1024 @ 48 kHz, tracks down to ~90 Hz
    normal = 1, // 2048 @ 48 kHz, tracks down to ~45 Hz
    deep   = 2  // 4096 @ 48 kHz, tracks down to ~25 Hz (sub bass)
};

/** Everything the engine needs from the parameters, in plain units.
    The default values here are also the plugin's parameter defaults. */
struct EngineParams
{
    // Pitch
    float pitchAmount = 1.0f;   // 0..1, 1 = land exactly on the guide's pitch
    float glideMs     = 20.0f;  // smoothing of pitch changes
    int   octave      = 0;      // -3..3
    int   semitones   = 0;      // -12..12
    bool  layerAuto   = true;   // detect the layer's own pitch (falls back to layerRoot)
    float layerRoot   = 48.0f;  // MIDI note of the layer when not detected (C3)
    bool  formant     = true;   // keep the layer's formants when shifting

    // Level & articulation
    float levelAmount = 1.0f;   // 0..1, 1 = the layer's loudness follows the guide exactly
    float attackMs    = 3.0f;
    float releaseMs   = 80.0f;
    float punch       = 0.0f;   // 0..1, re-applies the guide's transients
    float gateDb      = -60.0f; // guide level below which the layer is muted; <= -80 = off

    // Motion
    float motion      = 0.5f;   // 0..1, guide's spectral movement imposed on the layer
    float tone        = 0.0f;   // 0..1, pull the layer's average tone toward the guide

    // Output
    float mix         = 1.0f;   // 0..1
    float outputDb    = 0.0f;
};

/** Snapshot pushed to the editor once per hop. */
struct Telemetry
{
    float guideHz = 0.0f;       // 0 = unvoiced
    float layerHz = 0.0f;       // 0 = unvoiced
    float outputHz = 0.0f;      // where the layer is being moved to (0 = unknown)
    float shiftSemitones = 0.0f;
    float guideDb = -120.0f, layerDb = -120.0f, outputDb = -120.0f;
    float gate = 1.0f;
    bool guideConnected = false;
};

/** The whole effect: a latency-compensated STFT layer processor plus a
    sample-accurate level/gate/punch stage driven by the sidechain guide. */
class TetherEngine
{
public:
    static constexpr int maxChannels = 2;

    void prepare (double sampleRate, int numLayerChannels);
    void setResolution (Resolution r) noexcept;   // real-time safe once prepared
    Resolution getResolution() const noexcept     { return resolution; }
    int getLatencySamples() const noexcept        { return fftSize + detectorDelay; }
    void reset() noexcept;

    static int fftSizeFor (Resolution r, double sampleRate) noexcept;
    static float lowestPitchFor (Resolution r) noexcept;
    static float detectorWindowMs (Resolution r) noexcept;
    static int latencyFor (Resolution r, double sampleRate) noexcept;

    /** Processes the layer in place. guide may have 0 channels (no sidechain):
        the layer is then passed through (delayed by the latency). */
    void process (float* const* layer, int numLayerChannels,
                  const float* const* guide, int numGuideChannels,
                  int numSamples, const EngineParams& params) noexcept;

    SpscQueue<Telemetry, 512>& getTelemetryQueue() noexcept   { return telemetry; }
    const SpectralLayer& getSpectralLayer() const noexcept     { return spectral; }

    /** Motion EQ per band, safe to read from the UI thread. */
    int getDisplayBandCount() const noexcept          { return displayBandCount.load (std::memory_order_relaxed); }
    float getDisplayBandGainDb (int band) const noexcept
    {
        return displayBandGains[(size_t) juce::jlimit (0, SpectralLayer::maxBands - 1, band)].load (std::memory_order_relaxed);
    }

private:
    void processFrame (const EngineParams& params, bool guideConnected) noexcept;

    double sampleRate = 48000.0;
    int channels = 2, activeChannels = 2;
    Resolution resolution = Resolution::normal;
    int fftSize = 2048, hopSize = 512, hopPos = 0;
    bool prepared = false;

    FFTBank ffts;
    PitchTracker guideTracker, layerTracker;
    PitchFollower guideFollower, layerFollower;
    SpectralLayer spectral;

    SampleRing layerRing[maxChannels], guideRing;
    std::vector<float> layerFrame[maxChannels], synthFrame[maxChannels], outAcc[maxChannels], outReady[maxChannels];
    std::vector<float> guideFrame, monoFrame;

    // Per-hop control state
    float shiftSmoothed = 0.0f;

    // Per-sample stage. Level detectors are centred on the output sample, which
    // costs half a detector window of extra latency (detectorDelay).
    int detectorDelay = 0;
    SampleRing wetRing[maxChannels];
    MovingAverage guideRms, layerRms, outputRms;
    float guideShapedDb = -120.0f, lastGuideDb = -120.0f, lastLayerDb = -120.0f;
    EnvelopeFollower punchFast, punchSlow;
    float lastOutputDb = -120.0f;
    float gateGain = 0.0f;
    bool gateOpen = false;
    int gateHold = 0;
    float mixSmoothed = 1.0f, outGainSmoothed = 1.0f;
    bool smoothersPrimed = false;

    SpscQueue<Telemetry, 512> telemetry;
    std::array<std::atomic<float>, SpectralLayer::maxBands> displayBandGains {};
    std::atomic<int> displayBandCount { 0 };
};

} // namespace tether
