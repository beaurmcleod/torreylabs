// Offline renderer for Tether's DSP engine.
//
//   tether_render <layer.wav> <guide.wav> <out.wav> [options]
//   tether_render --demo <folder>
//
// Output is latency-compensated, so it lines up sample-for-sample with the inputs.

#include "dsp/TetherEngine.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <iostream>

using namespace tether;

namespace
{

constexpr double twoPi = 6.283185307179586;

void printUsage()
{
    std::cout <<
        "Tether offline renderer\n\n"
        "  tether_render <layer.wav> <guide.wav> <out.wav> [options]\n"
        "  tether_render --demo <folder>\n\n"
        "Options (defaults match the plugin):\n"
        "  --pitch 0-100      pitch amount (%)          --glide ms\n"
        "  --octave n         -3..3                     --semitones n   -12..12\n"
        "  --fine cents       -100..100                 --vibrato 0-200 guide vibrato passed on (%)\n"
        "  --root midi        layer root note           --no-detect     use --root instead of detecting\n"
        "  --no-formant       formants follow the pitch --formant-shift st   -12..12\n"
        "  --scale name       off|chromatic|major|minor|... (see README)   --key C..B\n"
        "  --level 0-100      level follow (%)          --attack ms     --release ms\n"
        "  --punch 0-100      transient transfer (%)    --gate dB       (-80 = off)\n"
        "  --motion 0-100     spectral movement (%)     --tone 0-100    tone match (%)\n"
        "  --mix 0-100        wet/dry (%)               --output dB\n"
        "  --resolution tight|normal|deep            --engine natural|spectral\n"
        "  --listen           output the guide instead of the layer\n";
}

struct Audio
{
    std::vector<std::vector<float>> channels;
    double sampleRate = 48000.0;
    int length() const { return channels.empty() ? 0 : (int) channels[0].size(); }
};

bool readAudio (const juce::File& file, Audio& audio)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

    if (reader == nullptr)
        return false;

    const int length = (int) reader->lengthInSamples;
    const int numChannels = juce::jmin (2, (int) reader->numChannels);
    juce::AudioBuffer<float> buffer (numChannels, length);
    reader->read (&buffer, 0, length, 0, true, numChannels > 1);

    audio.sampleRate = reader->sampleRate;
    audio.channels.assign ((size_t) numChannels, std::vector<float> ((size_t) length));
    for (int c = 0; c < numChannels; ++c)
        std::copy (buffer.getReadPointer (c), buffer.getReadPointer (c) + length, audio.channels[(size_t) c].begin());

    return true;
}

bool writeAudio (const juce::File& file, const Audio& audio)
{
    file.deleteFile();
    auto stream = std::make_unique<juce::FileOutputStream> (file);

    if (! stream->openedOk())
        return false;

    juce::WavAudioFormat wav;
    const auto options = juce::AudioFormatWriterOptions{}
                             .withSampleRate (audio.sampleRate)
                             .withNumChannels ((int) audio.channels.size())
                             .withBitsPerSample (24);
    std::unique_ptr<juce::OutputStream> out (std::move (stream));
    auto writer = wav.createWriterFor (out, options);

    if (writer == nullptr)
        return false;

    std::vector<const float*> pointers;
    for (const auto& c : audio.channels)
        pointers.push_back (c.data());

    return writer->writeFromFloatArrays (pointers.data(), (int) pointers.size(), audio.length());
}

/** Runs the engine over whole files and removes the latency. */
Audio render (const Audio& layer, const Audio& guide, const EngineParams& params, Resolution resolution,
              Engine engineMode = Engine::natural)
{
    TetherEngine engine;
    engine.prepare (layer.sampleRate, 2);
    engine.setResolution (resolution);
    engine.setEngine (engineMode);

    const int latency = engine.getLatencySamples();
    const int length = layer.length();
    const int numLayer = (int) layer.channels.size();
    const int numGuide = (int) guide.channels.size();
    const int total = length + latency;

    std::vector<std::vector<float>> work ((size_t) numLayer, std::vector<float> ((size_t) total, 0.0f));
    std::vector<std::vector<float>> guideWork ((size_t) numGuide, std::vector<float> ((size_t) total, 0.0f));

    for (int c = 0; c < numLayer; ++c)
        std::copy (layer.channels[(size_t) c].begin(), layer.channels[(size_t) c].end(), work[(size_t) c].begin());

    for (int c = 0; c < numGuide; ++c)
        std::copy_n (guide.channels[(size_t) c].begin(), juce::jmin (length, guide.length()), guideWork[(size_t) c].begin());

    constexpr int block = 512;
    for (int start = 0; start < total; start += block)
    {
        const int n = juce::jmin (block, total - start);
        float* layerPtrs[2] = {};
        const float* guidePtrs[2] = {};

        for (int c = 0; c < numLayer; ++c)
            layerPtrs[c] = work[(size_t) c].data() + start;
        for (int c = 0; c < numGuide; ++c)
            guidePtrs[c] = guideWork[(size_t) c].data() + start;

        engine.process (layerPtrs, numLayer, guidePtrs, numGuide, n, params);
    }

    Audio out;
    out.sampleRate = layer.sampleRate;
    for (const auto& c : work)
        out.channels.emplace_back (c.begin() + latency, c.end());

    return out;
}

//==============================================================================
// Demo material: everything is synthesised, so no samples are needed.

struct Voice
{
    double phase = 0.0;

    // Band-limited sawtooth via additive synthesis (slow but clean).
    float saw (double hz, double sampleRate)
    {
        phase += twoPi * hz / sampleRate;
        if (phase > twoPi * 64.0)
            phase = std::fmod (phase, twoPi);

        const int harmonics = juce::jmax (1, (int) (0.45 * sampleRate / hz));
        double v = 0.0;
        for (int h = 1; h <= harmonics; ++h)
            v += std::sin (phase * h) / h;
        return (float) (v * 0.55);
    }
};

struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

    void bandpass (double hz, double q, double sampleRate)
    {
        const double w = twoPi * hz / sampleRate, alpha = std::sin (w) / (2.0 * q), a0 = 1.0 + alpha;
        b0 = alpha / a0; b1 = 0.0; b2 = -alpha / a0;
        a1 = -2.0 * std::cos (w) / a0; a2 = (1.0 - alpha) / a0;
    }

    void lowpass (double hz, double q, double sampleRate)
    {
        const double w = twoPi * juce::jlimit (20.0, 0.45 * sampleRate, hz) / sampleRate;
        const double alpha = std::sin (w) / (2.0 * q), cw = std::cos (w), a0 = 1.0 + alpha;
        b0 = (1.0 - cw) * 0.5 / a0; b1 = (1.0 - cw) / a0; b2 = b0;
        a1 = -2.0 * cw / a0; a2 = (1.0 - alpha) / a0;
    }

    float process (float x)
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return (float) y;
    }
};

struct Note { double startSec, lengthSec, hz; int vowel; float velocity; };

/** A sung-sounding guide: glottal saw -> vowel formants, with vibrato and phrasing. */
Audio makeVocalGuide (double sr, double seconds)
{
    static const double formants[5][3] = { { 800, 1150, 2900 }, { 400, 1600, 2700 }, { 300, 2300, 3000 },
                                           { 450, 800, 2830 }, { 325, 700, 2530 } };
    const double beat = 60.0 / 92.0;
    const std::vector<Note> notes = {
        { 0.0 * beat, 1.5 * beat, 220.00, 0, 0.8f }, { 1.5 * beat, 0.5 * beat, 246.94, 1, 0.6f },
        { 2.0 * beat, 1.0 * beat, 261.63, 3, 0.9f }, { 3.0 * beat, 1.0 * beat, 329.63, 0, 1.0f },
        { 4.5 * beat, 1.0 * beat, 293.66, 2, 0.7f }, { 5.5 * beat, 1.5 * beat, 261.63, 1, 0.8f },
        { 7.0 * beat, 1.0 * beat, 246.94, 4, 0.6f }, { 8.0 * beat, 3.0 * beat, 220.00, 0, 0.9f },
        { 11.5 * beat, 0.5 * beat, 196.00, 3, 0.7f }, { 12.0 * beat, 2.5 * beat, 220.00, 0, 1.0f },
    };

    const int n = (int) (seconds * sr);
    Audio a;
    a.sampleRate = sr;
    a.channels.assign (1, std::vector<float> ((size_t) n, 0.0f));
    Voice voice;
    Biquad f[3];
    juce::Random random (1);

    for (int i = 0; i < n; ++i)
    {
        const double t = i / sr;
        const Note* note = nullptr;
        for (const auto& nt : notes)
            if (t >= nt.startSec && t < nt.startSec + nt.lengthSec)
                note = &nt;

        if (note == nullptr)
        {
            for (auto& filter : f)
                filter.process (0.0f);
            continue;
        }

        const double local = t - note->startSec, remaining = note->startSec + note->lengthSec - t;
        const double vibratoDepth = 0.35 * juce::jlimit (0.0, 1.0, (local - 0.25) / 0.4);
        const double hz = note->hz * std::pow (2.0, vibratoDepth / 12.0 * std::sin (twoPi * 5.3 * local))
                                   * (1.0 + 0.002 * (random.nextFloat() - 0.5f));
        const double env = juce::jmin (1.0, local / 0.04) * juce::jmin (1.0, remaining / 0.07)
                         * (0.85 + 0.15 * std::sin (juce::jmin (local, 1.5) * 2.0));

        for (int k = 0; k < 3; ++k)
            f[k].bandpass (formants[note->vowel][k], 9.0 + 3.0 * k, sr);

        const float source = voice.saw (hz, sr);
        const float sung = f[0].process (source) + 0.6f * f[1].process (source) + 0.3f * f[2].process (source);
        a.channels[0][(size_t) i] = (float) (0.9 * env * note->velocity) * sung;
    }

    return a;
}

/** A static detuned-saw pad on C3, the kind of thing you'd want to "sing" along. */
Audio makePadLayer (double sr, double seconds)
{
    const int n = (int) (seconds * sr);
    Audio a;
    a.sampleRate = sr;
    a.channels.assign (2, std::vector<float> ((size_t) n, 0.0f));
    const double detune[2][3] = { { -9.0, 3.0, 14.0 }, { -13.0, -2.0, 8.0 } };

    for (int c = 0; c < 2; ++c)
    {
        Voice voices[3];
        voices[1].phase = 1.3 * (c + 1);
        voices[2].phase = 2.1 * (c + 1);
        Biquad lowpass;
        lowpass.lowpass (2800.0, 0.7, sr);

        for (int i = 0; i < n; ++i)
        {
            float v = 0.0f;
            for (int k = 0; k < 3; ++k)
                v += voices[k].saw (130.81 * std::pow (2.0, detune[c][k] / 1200.0), sr);

            a.channels[(size_t) c][(size_t) i] = 0.12f * lowpass.process (v);
        }
    }

    return a;
}

/** A wobbling bass line (resonant low-pass on a saw, LFO in eighth notes). */
Audio makeWobbleBassGuide (double sr, double seconds)
{
    const double beat = 60.0 / 140.0;
    const std::vector<Note> notes = {
        { 0.0 * beat, 1.75 * beat, 41.20, 0, 1.0f }, { 2.0 * beat, 1.75 * beat, 49.00, 0, 0.9f },
        { 4.0 * beat, 0.75 * beat, 55.00, 0, 1.0f }, { 5.0 * beat, 2.75 * beat, 43.65, 0, 0.9f },
        { 8.0 * beat, 1.75 * beat, 41.20, 0, 1.0f }, { 10.0 * beat, 1.75 * beat, 65.41, 0, 0.8f },
        { 12.0 * beat, 3.5 * beat, 55.00, 0, 1.0f },
    };

    const int n = (int) (seconds * sr);
    Audio a;
    a.sampleRate = sr;
    a.channels.assign (1, std::vector<float> ((size_t) n, 0.0f));
    Voice voice;
    Biquad filter;

    for (int i = 0; i < n; ++i)
    {
        const double t = i / sr;
        const Note* note = nullptr;
        for (const auto& nt : notes)
            if (t >= nt.startSec && t < nt.startSec + nt.lengthSec)
                note = &nt;

        const double lfo = 0.5 - 0.5 * std::cos (twoPi * t / (beat * 0.5));
        filter.lowpass (90.0 + 1400.0 * lfo * lfo, 4.0, sr);

        if (note == nullptr)
        {
            filter.process (0.0f);
            continue;
        }

        const double local = t - note->startSec, remaining = note->startSec + note->lengthSec - t;
        const double env = juce::jmin (1.0, local / 0.005) * juce::jmin (1.0, remaining / 0.02);
        a.channels[0][(size_t) i] = (float) (0.55 * env * note->velocity) * filter.process (voice.saw (note->hz, sr));
    }

    return a;
}

/** A static, gritty FM texture on A1 (110 Hz) to glue on top of the bass. */
Audio makeGrowlLayer (double sr, double seconds)
{
    const int n = (int) (seconds * sr);
    Audio a;
    a.sampleRate = sr;
    a.channels.assign (2, std::vector<float> ((size_t) n, 0.0f));

    for (int c = 0; c < 2; ++c)
    {
        double carrier = 0.3 * c, modulator = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const double hz = 110.0 * (1.0 + 0.003 * (c == 0 ? 1 : -1));
            modulator += twoPi * hz * 2.0 / sr;
            carrier += twoPi * hz / sr + 0.0;
            const double fm = std::sin (carrier + 2.6 * std::sin (modulator));
            a.channels[(size_t) c][(size_t) i] = (float) (0.3 * std::tanh (2.5 * fm));
        }
    }

    return a;
}

Audio mixTogether (const Audio& a, const Audio& b, float gainA, float gainB)
{
    Audio out;
    out.sampleRate = a.sampleRate;
    const int n = juce::jmin (a.length(), b.length());
    out.channels.assign (2, std::vector<float> ((size_t) n, 0.0f));

    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < n; ++i)
            out.channels[(size_t) c][(size_t) i] = gainA * a.channels[(size_t) juce::jmin (c, (int) a.channels.size() - 1)][(size_t) i]
                                                 + gainB * b.channels[(size_t) juce::jmin (c, (int) b.channels.size() - 1)][(size_t) i];
    return out;
}

bool writeOrComplain (const juce::File& file, const Audio& audio)
{
    if (writeAudio (file, audio))
    {
        std::cout << "  wrote " << file.getFullPathName() << "\n";
        return true;
    }

    std::cerr << "Couldn't write " << file.getFullPathName() << "\n";
    return false;
}

int runDemo (const juce::File& folder)
{
    constexpr double sr = 48000.0;
    folder.createDirectory();
    bool ok = true;

    std::cout << "Demo 1: vocal guide -> static pad layer\n";
    {
        const auto guide = makeVocalGuide (sr, 10.0);
        const auto pad = makePadLayer (sr, 10.0);
        EngineParams p; // plugin defaults
        const auto tethered = render (pad, guide, p, Resolution::normal);
        ok = writeOrComplain (folder.getChildFile ("01_guide_vocal.wav"), guide) && ok;
        ok = writeOrComplain (folder.getChildFile ("01_layer_pad_before.wav"), pad) && ok;
        ok = writeOrComplain (folder.getChildFile ("01_layer_pad_tethered.wav"), tethered) && ok;
        ok = writeOrComplain (folder.getChildFile ("01_mix_guide_plus_tethered.wav"), mixTogether (guide, tethered, 0.8f, 0.8f)) && ok;
    }

    std::cout << "Demo 3: vocal guide -> pad harmony, a diatonic fifth above in A minor\n";
    {
        const auto guide = makeVocalGuide (sr, 10.0);
        const auto pad = makePadLayer (sr, 10.0);
        EngineParams p;
        p.semitones = 7;
        p.scale = Scale::minor;
        p.key = 9;             // A minor: the demo vocal's key
        p.vibrato = 0.6f;
        p.glideMs = 60.0f;
        p.attackMs = 15.0f;
        p.releaseMs = 250.0f;
        p.motion = 0.4f;
        p.tone = 0.3f;
        const auto tethered = render (pad, guide, p, Resolution::normal);
        ok = writeOrComplain (folder.getChildFile ("03_layer_pad_harmony_fifth.wav"), tethered) && ok;
        ok = writeOrComplain (folder.getChildFile ("03_mix_guide_plus_harmony.wav"), mixTogether (guide, tethered, 0.8f, 0.7f)) && ok;
    }

    std::cout << "Demo 2: wobble bass guide -> static FM growl, one octave up\n";
    {
        const auto guide = makeWobbleBassGuide (sr, 7.0);
        const auto growl = makeGrowlLayer (sr, 7.0);
        EngineParams p;
        p.layerAuto = false;
        p.layerRoot = 45.0f; // the growl sits on 110 Hz
        p.octave = 1;
        p.motion = 1.0f;
        p.formant = false;
        const auto tethered = render (growl, guide, p, Resolution::deep);
        ok = writeOrComplain (folder.getChildFile ("02_guide_wobble_bass.wav"), guide) && ok;
        ok = writeOrComplain (folder.getChildFile ("02_layer_growl_before.wav"), growl) && ok;
        ok = writeOrComplain (folder.getChildFile ("02_layer_growl_tethered.wav"), tethered) && ok;
        ok = writeOrComplain (folder.getChildFile ("02_mix_guide_plus_tethered.wav"), mixTogether (guide, tethered, 0.9f, 0.7f)) && ok;
    }

    return ok ? 0 : 1;
}

float parseFloat (const juce::StringArray& args, int& i)
{
    return ++i < args.size() ? args[i].getFloatValue() : 0.0f;
}

} // namespace

int main (int argc, char** argv)
{
    juce::StringArray args;
    for (int i = 1; i < argc; ++i)
        args.add (juce::String::fromUTF8 (argv[i]));

    if (args.size() == 2 && args[0] == "--demo")
        return runDemo (juce::File::getCurrentWorkingDirectory().getChildFile (args[1]));

    if (args.size() < 3 || args.contains ("--help"))
    {
        printUsage();
        return args.contains ("--help") ? 0 : 1;
    }

    EngineParams p;
    Resolution resolution = Resolution::normal;
    Engine engineMode = Engine::natural;

    for (int i = 3; i < args.size(); ++i)
    {
        const auto& a = args[i];
        if      (a == "--pitch")      p.pitchAmount = parseFloat (args, i) * 0.01f;
        else if (a == "--glide")      p.glideMs = parseFloat (args, i);
        else if (a == "--octave")     p.octave = (int) parseFloat (args, i);
        else if (a == "--semitones")  p.semitones = (int) parseFloat (args, i);
        else if (a == "--fine")       p.fineCents = parseFloat (args, i);
        else if (a == "--vibrato")    p.vibrato = parseFloat (args, i) * 0.01f;
        else if (a == "--root")       p.layerRoot = parseFloat (args, i);
        else if (a == "--no-detect")  p.layerAuto = false;
        else if (a == "--no-formant") p.formant = false;
        else if (a == "--formant-shift") p.formantShift = parseFloat (args, i);
        else if (a == "--listen")     p.listen = true;
        else if (a == "--scale")
        {
            const auto name = ++i < args.size() ? args[i].toLowerCase().replace ("-", " ").replace ("_", " ") : juce::String();
            p.scale = Scale::off;
            for (int k = 0; k < (int) Scale::count; ++k)
                if (juce::String (scaleName ((Scale) k)).toLowerCase() == name)
                    p.scale = (Scale) k;
        }
        else if (a == "--key")
        {
            const auto name = ++i < args.size() ? args[i].toUpperCase() : juce::String();
            for (int k = 0; k < 12; ++k)
                if (juce::String (keyName (k)) == name)
                    p.key = k;
        }
        else if (a == "--engine")
        {
            const auto e = ++i < args.size() ? args[i].toLowerCase() : juce::String();
            engineMode = e == "spectral" ? Engine::spectral : Engine::natural;
        }
        else if (a == "--level")      p.levelAmount = parseFloat (args, i) * 0.01f;
        else if (a == "--attack")     p.attackMs = parseFloat (args, i);
        else if (a == "--release")    p.releaseMs = parseFloat (args, i);
        else if (a == "--punch")      p.punch = parseFloat (args, i) * 0.01f;
        else if (a == "--gate")       p.gateDb = parseFloat (args, i);
        else if (a == "--motion")     p.motion = parseFloat (args, i) * 0.01f;
        else if (a == "--tone")       p.tone = parseFloat (args, i) * 0.01f;
        else if (a == "--mix")        p.mix = parseFloat (args, i) * 0.01f;
        else if (a == "--output")     p.outputDb = parseFloat (args, i);
        else if (a == "--resolution")
        {
            const auto r = ++i < args.size() ? args[i].toLowerCase() : juce::String();
            resolution = r == "tight" ? Resolution::tight : (r == "deep" ? Resolution::deep : Resolution::normal);
        }
        else
        {
            std::cerr << "Unknown option " << a << "\n";
            return 1;
        }
    }

    const auto cwd = juce::File::getCurrentWorkingDirectory();
    Audio layer, guide;

    if (! readAudio (cwd.getChildFile (args[0]), layer) || ! readAudio (cwd.getChildFile (args[1]), guide))
    {
        std::cerr << "Couldn't read the input files\n";
        return 1;
    }

    if (std::abs (layer.sampleRate - guide.sampleRate) > 0.5)
    {
        std::cerr << "Layer and guide must have the same sample rate\n";
        return 1;
    }

    return writeOrComplain (cwd.getChildFile (args[2]), render (layer, guide, p, resolution, engineMode)) ? 0 : 1;
}
