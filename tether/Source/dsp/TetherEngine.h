#pragma once

#include "DspUtils.h"
#include "FFTBank.h"
#include "GrainShifter.h"
#include "Motion.h"
#include "PitchTracker.h"
#include "SpectralLayer.h"

#include <array>

namespace tether
{

/** Analysis/processing resolution. Bigger windows resolve lower notes but add latency. */
enum class Resolution : int
{
    tight  = 0, // tracks down to ~90 Hz
    normal = 1, // tracks down to ~45 Hz
    deep   = 2  // tracks down to ~25 Hz (sub bass)
};

/** How the layer's pitch is moved. */
enum class Engine : int
{
    natural  = 0, // pitch-synchronous grains: cleanest for single-note layers (default)
    spectral = 1  // phase vocoder: for chords, pads and noisy layers
};

/** Musical scales for pitch quantisation, as pitch-class sets. */
enum class Scale : int
{
    off = 0, chromatic, major, minor, harmonicMinor, dorian, phrygian, lydian, mixolydian,
    majorPentatonic, minorPentatonic, blues, wholeTone, octaves, fifths,
    count
};

const char* scaleName (Scale s) noexcept;
const char* keyName (int key) noexcept;     // 0 = C

/** Everything the engine needs from the parameters, in plain units.
    The default values here are also the plugin's parameter defaults. */
struct EngineParams
{
    // Pitch
    float pitchAmount = 1.0f;   // 0..1, 1 = land exactly on the guide's pitch
    float glideMs     = 20.0f;  // smoothing of pitch changes
    int   octave      = 0;      // -3..3
    int   semitones   = 0;      // -12..12
    float fineCents   = 0.0f;   // -100..100
    bool  layerAuto   = true;   // detect the layer's own pitch (falls back to layerRoot)
    float layerRoot   = 48.0f;  // MIDI note of the layer when not detected (C3)
    bool  formant     = true;   // keep the layer's formants when shifting
    float formantShift = 0.0f;  // extra formant shift in semitones, -12..12
    float grainPeriods = 1.0f;  // (internal) grain half-length in periods for the Natural engine
    float vibrato     = 1.0f;   // 0..2, how much of the guide's vibrato / bends is passed on
    Scale scale       = Scale::off;
    int   key         = 0;      // 0 = C .. 11 = B

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
    bool  listen      = false;  // monitor the guide instead of the layer
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

/** Quantises a MIDI note (fractional) to the nearest note of the scale. */
float quantiseToScale (float midi, Scale scale, int key) noexcept;

/** The whole effect: pitch tracking of guide and layer, a pitch/formant
    shifter (grain or spectral engine), a motion EQ and a sample-accurate
    level/gate/punch stage driven by the sidechain guide. */
class TetherEngine
{
public:
    static constexpr int maxChannels = 2;

    void prepare (double sampleRate, int numLayerChannels);
    void setResolution (Resolution r) noexcept;   // real-time safe once prepared
    void setEngine (Engine e) noexcept;           // real-time safe once prepared; restarts the pitch stage
    Resolution getResolution() const noexcept     { return resolution; }
    Engine getEngine() const noexcept             { return engineMode; }
    int getLatencySamples() const noexcept        { return pitchDelay + detectorDelay; }
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

    /** Motion EQ per band, safe to read from the UI thread. */
    int getDisplayBandCount() const noexcept          { return displayBandCount.load (std::memory_order_relaxed); }
    float getDisplayBandGainDb (int band) const noexcept
    {
        return displayBandGains[(size_t) std::clamp (band, 0, MotionAnalyser::maxBands - 1)].load (std::memory_order_relaxed);
    }

private:
    static int maxPeriodFor (Resolution r, double sampleRate) noexcept;
    static int pitchDelayFor (Resolution r, double sampleRate) noexcept;
    void processFrame (const EngineParams& params, bool guideConnected) noexcept;

    double sampleRate = 48000.0;
    int channels = 2, activeChannels = 2;
    Resolution resolution = Resolution::normal;
    Engine engineMode = Engine::natural;
    int fftSize = 2048, hopSize = 512, hopPos = 0, maxPeriod = 1067, pitchDelay = 0, guideAnalysisDelay = 0;
    bool prepared = false;

    FFTBank ffts;
    PitchTracker guideTracker, layerTracker;
    PitchFollower guideFollower, layerFollower;
    SpectralLayer spectral;
    GrainShifter grains;
    MotionAnalyser motionAnalyser;
    MotionEq motionEq;

    SampleRing layerRing[maxChannels], guideRing, wetMono;
    std::vector<float> layerFrame[maxChannels], synthFrame[maxChannels], outAcc[maxChannels], outReady[maxChannels];
    std::vector<float> guideFrame, monoFrame, wetFrame;

    // Per-hop control state. The guide's pitch is split into its note and its
    // vibrato: two-pole smoothers, the second recovering the lag of the first.
    float shiftSmoothed = 0.0f;
    float slowMidi[2] {}, lagMidi[2] {};
    bool hasSlow = false;

    // Per-sample stage. Level detectors are centred on the output sample, which
    // costs half a detector window of extra latency (detectorDelay).
    int detectorDelay = 0, detectorLength_ = 2;
    SampleRing wetRing[maxChannels];
    MovingAverage guideRms, layerRms, outputRms;
    float guideShapedDb = -120.0f, lastGuideDb = -120.0f, lastLayerDb = -120.0f;
    EnvelopeFollower punchFast, punchSlow;
    float lastOutputDb = -120.0f;
    float gateGain = 0.0f;
    bool gateOpen = false;
    int gateHold = 0;
    float mixSmoothed = 1.0f, outGainSmoothed = 1.0f, listenSmoothed = 0.0f;
    bool smoothersPrimed = false;

    SpscQueue<Telemetry, 512> telemetry;
    std::array<std::atomic<float>, MotionAnalyser::maxBands> displayBandGains {};
    std::atomic<int> displayBandCount { 0 };
};

} // namespace tether
