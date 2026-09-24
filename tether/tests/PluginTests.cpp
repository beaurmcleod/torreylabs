#include "TestSignals.h"
#include "PluginProcessor.h"

#include <juce_audio_formats/juce_audio_formats.h>

namespace
{

void processSeconds (TetherAudioProcessor& processor, const std::vector<float>& layer, const std::vector<float>& guide,
                     int blockSize, std::vector<float>* outputLeft = nullptr, std::function<void()> betweenBlocks = {})
{
    juce::AudioBuffer<float> buffer (4, blockSize);
    juce::MidiBuffer midi;
    const int total = (int) layer.size();

    for (int start = 0; start + blockSize <= total; start += blockSize)
    {
        for (int i = 0; i < blockSize; ++i)
        {
            const float l = layer[(size_t) (start + i)], g = guide[(size_t) (start + i)];
            buffer.setSample (0, i, l);
            buffer.setSample (1, i, l);
            buffer.setSample (2, i, g);
            buffer.setSample (3, i, g);
        }

        processor.processBlock (buffer, midi);

        if (outputLeft != nullptr)
            outputLeft->insert (outputLeft->end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + blockSize);

        if (betweenBlocks)
            betweenBlocks();
    }
}

void setParam (TetherAudioProcessor& p, const char* id, float plainValue)
{
    auto* param = p.getState().getParameter (id);
    param->setValueNotifyingHost (param->convertTo0to1 (plainValue));
}

} // namespace

class PluginTests final : public juce::UnitTest
{
public:
    PluginTests() : juce::UnitTest ("Plugin", "Tether") {}

    void runTest() override
    {
        beginTest ("Bus layouts: layer mono/stereo in = out, guide mono/stereo/off");
        {
            TetherAudioProcessor p;
            const auto mono = juce::AudioChannelSet::mono(), stereo = juce::AudioChannelSet::stereo();
            const auto off = juce::AudioChannelSet::disabled();

            auto layout = [] (juce::AudioChannelSet in, juce::AudioChannelSet out, juce::AudioChannelSet guide)
            {
                juce::AudioProcessor::BusesLayout l;
                l.inputBuses.add (in);
                l.inputBuses.add (guide);
                l.outputBuses.add (out);
                return l;
            };

            expect (p.checkBusesLayoutSupported (layout (stereo, stereo, stereo)));
            expect (p.checkBusesLayoutSupported (layout (stereo, stereo, mono)));
            expect (p.checkBusesLayoutSupported (layout (stereo, stereo, off)));
            expect (p.checkBusesLayoutSupported (layout (mono, mono, stereo)));
            expect (! p.checkBusesLayoutSupported (layout (mono, stereo, stereo)));
            expect (! p.checkBusesLayoutSupported (layout (juce::AudioChannelSet::create5point1(), juce::AudioChannelSet::create5point1(), stereo)));
        }

        beginTest ("Reported latency follows the Resolution parameter");
        {
            TetherAudioProcessor p;
            p.prepareToPlay (48000.0, 512);
            expectEquals (p.getLatencySamples(), tether::TetherEngine::latencyFor (tether::Resolution::normal, 48000.0));

            setParam (p, ParamIDs::resolution, 2.0f);
            std::vector<float> silence (4096, 0.0f);
            processSeconds (p, silence, silence, 512);
            expectEquals (p.getLatencySamples(), tether::TetherEngine::latencyFor (tether::Resolution::deep, 48000.0));
        }

        beginTest ("Guide on the sidechain bus drives the layer");
        {
            TetherAudioProcessor p;
            p.prepareToPlay (48000.0, 256);
            const int n = 96000;
            const auto layer = testsig::saw (48000.0, 110.0, 0.25f, n);
            const auto guide = testsig::saw (48000.0, 164.81, 0.3f, n);
            std::vector<float> out;
            processSeconds (p, layer, guide, 256, &out);

            const double hz = testsig::referencePitch (out, 60000, 8192, 48000.0);
            expectLessThan (std::abs (testsig::cents (hz, 164.81)), 10.0, "got " + juce::String (hz) + " Hz");
            expect (p.isGuideRouted());
        }

        beginTest ("Host bypass keeps the layer delayed by the reported latency");
        {
            TetherAudioProcessor p;
            p.prepareToPlay (48000.0, 512);
            const int latency = p.getLatencySamples();
            const auto input = testsig::noise (48000, 0.5f, 5);
            const auto guide = testsig::saw (48000.0, 220.0, 0.3f, 48000);

            juce::AudioBuffer<float> buffer (4, 512);
            juce::MidiBuffer midi;
            std::vector<float> out;

            for (int start = 0; start + 512 <= (int) input.size(); start += 512)
            {
                for (int i = 0; i < 512; ++i)
                {
                    buffer.setSample (0, i, input[(size_t) (start + i)]);
                    buffer.setSample (1, i, input[(size_t) (start + i)]);
                    buffer.setSample (2, i, guide[(size_t) (start + i)]);
                    buffer.setSample (3, i, guide[(size_t) (start + i)]);
                }

                p.processBlockBypassed (buffer, midi);
                out.insert (out.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + 512);
            }

            float worst = 0.0f;
            for (int i = latency; i < (int) out.size(); ++i)
                worst = std::max (worst, std::abs (out[(size_t) i] - input[(size_t) (i - latency)]));

            expectLessThan (worst, 1.0e-6f);
        }

        beginTest ("State survives a save / load round trip");
        {
            TetherAudioProcessor a;
            setParam (a, ParamIDs::octave, -2.0f);
            setParam (a, ParamIDs::motion, 80.0f);
            setParam (a, ParamIDs::detectLayer, 0.0f);
            setParam (a, ParamIDs::layerRoot, 43.0f);

            juce::MemoryBlock blob;
            a.getStateInformation (blob);

            TetherAudioProcessor b;
            b.setStateInformation (blob.getData(), (int) blob.getSize());
            auto value = [&] (const char* id) { return b.getState().getRawParameterValue (id)->load(); };

            expectWithinAbsoluteError (value (ParamIDs::octave), -2.0f, 0.001f);
            expectWithinAbsoluteError (value (ParamIDs::motion), 80.0f, 0.01f);
            expectWithinAbsoluteError (value (ParamIDs::detectLayer), 0.0f, 0.001f);
            expectWithinAbsoluteError (value (ParamIDs::layerRoot), 43.0f, 0.001f);
        }

        beginTest ("Parameter text");
        {
            TetherAudioProcessor p;
            auto text = [&] (const char* id, float plain)
            {
                auto* param = p.getState().getParameter (id);
                return param->getText (param->convertTo0to1 (plain), 32);
            };

            expectEquals (text (ParamIDs::layerRoot, 60.0f), juce::String ("C3"));
            expectEquals (text (ParamIDs::gate, -80.0f), juce::String ("Off"));
            expectEquals (text (ParamIDs::semitones, 7.0f), juce::String ("+7 st"));
            expectEquals (text (ParamIDs::octave, -1.0f), juce::String ("-1 oct"));
        }

        beginTest ("Editor opens, animates and closes cleanly");
        {
            TetherAudioProcessor p;
            p.prepareToPlay (48000.0, 512);
            std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorAndMakeActive());
            expect (editor != nullptr);
            expectEquals (editor->getWidth(), 900);

            // Play a short melody so the visualizer has something to show.
            constexpr double sr = 48000.0;
            const int n = (int) (6.0 * sr);
            std::vector<float> guide ((size_t) n, 0.0f), freq ((size_t) n, 0.0f), amp ((size_t) n, 0.0f);
            const double notes[] = { 110.0, 130.81, 146.83, 164.81, 146.83, 130.81, 98.0, 110.0 };
            for (int i = 0; i < n; ++i)
            {
                const int note = (i / (int) (0.75 * sr)) % 8;
                const double t = (i % (int) (0.75 * sr)) / sr;
                const double vibrato = std::pow (2.0, 0.25 / 12.0 * std::sin (2.0 * juce::MathConstants<double>::pi * 5.5 * t) * juce::jmin (1.0, t / 0.3));
                freq[(size_t) i] = (float) (notes[note] * vibrato);
                amp[(size_t) i] = t < 0.6 ? (float) (0.35 * juce::jmin (1.0, t / 0.01)) : 0.0f;
            }
            testsig::addSaw (guide, sr, freq, amp);
            const auto layer = testsig::saw (sr, 98.0, 0.2f, n);

            int blocks = 0;
            processSeconds (p, layer, guide, 512, nullptr, [&]
            {
                // Let the UI timers run roughly in step with the audio.
                if (++blocks % 2 == 0)
                    juce::MessageManager::getInstance()->runDispatchLoopUntil (15);
            });

            if (auto path = juce::SystemStats::getEnvironmentVariable ("TETHER_SNAPSHOT", {}); path.isNotEmpty())
            {
                const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);
                juce::File file (path);
                file.deleteFile();
                juce::FileOutputStream stream (file);
                juce::PNGImageFormat png;
                expect (stream.openedOk() && png.writeImageToStream (image, stream), "snapshot written");
                logMessage ("Snapshot written to " + file.getFullPathName());
            }

            editor.reset();
        }
    }
};

static PluginTests pluginTests;
