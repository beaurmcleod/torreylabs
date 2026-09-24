#include "FactoryPresets.h"

namespace
{

using tether::Engine;
using tether::Resolution;
using tether::Scale;

FactoryPreset make (const char* name, const char* category, std::function<void (PluginSettings&)> edit)
{
    FactoryPreset preset { name, category, {} };
    edit (preset.settings);
    return preset;
}

std::vector<FactoryPreset> build()
{
    std::vector<FactoryPreset> list;

    list.push_back (make ("Init", "Basics", [] (PluginSettings&) {}));

    list.push_back (make ("Pitch Only", "Basics", [] (PluginSettings& s)
    {
        s.params.levelAmount = 0.0f;
        s.params.gateDb = -80.0f;
        s.params.motion = 0.0f;
    }));

    list.push_back (make ("Level + Motion Only", "Basics", [] (PluginSettings& s)
    {
        s.params.pitchAmount = 0.0f;
        s.params.motion = 0.8f;
        s.params.tone = 0.3f;
    }));

    list.push_back (make ("Vocal Double", "Vocals", [] (PluginSettings& s)
    {
        s.params.glideMs = 30.0f;
        s.params.vibrato = 1.0f;
        s.params.levelAmount = 1.0f;
        s.params.attackMs = 5.0f;
        s.params.releaseMs = 120.0f;
        s.params.gateDb = -50.0f;
        s.params.motion = 0.6f;
        s.params.tone = 0.2f;
    }));

    list.push_back (make ("Vocal Octave Down", "Vocals", [] (PluginSettings& s)
    {
        s.params.octave = -1;
        s.params.glideMs = 40.0f;
        s.params.gateDb = -50.0f;
        s.params.motion = 0.5f;
        s.params.tone = 0.3f;
        s.params.formantShift = -2.0f;
    }));

    list.push_back (make ("Choir Fifth", "Vocals", [] (PluginSettings& s)
    {
        s.params.semitones = 7;
        s.params.scale = Scale::major;
        s.params.glideMs = 60.0f;
        s.params.vibrato = 0.7f;
        s.params.attackMs = 15.0f;
        s.params.releaseMs = 250.0f;
        s.params.gateDb = -55.0f;
        s.params.motion = 0.4f;
        s.params.tone = 0.3f;
    }));

    list.push_back (make ("Harmony Third", "Vocals", [] (PluginSettings& s)
    {
        s.params.semitones = 4;
        s.params.scale = Scale::major;
        s.params.glideMs = 50.0f;
        s.params.vibrato = 0.8f;
        s.params.attackMs = 10.0f;
        s.params.releaseMs = 200.0f;
        s.params.gateDb = -55.0f;
        s.params.motion = 0.4f;
    }));

    list.push_back (make ("Robot Lock", "Vocals", [] (PluginSettings& s)
    {
        s.params.scale = Scale::chromatic;
        s.params.vibrato = 0.0f;
        s.params.glideMs = 0.0f;
        s.params.gateDb = -50.0f;
        s.params.motion = 0.7f;
        s.params.tone = 0.5f;
    }));

    list.push_back (make ("Sub Follow", "Bass", [] (PluginSettings& s)
    {
        s.resolution = Resolution::deep;
        s.params.octave = -1;
        s.params.layerAuto = false;
        s.params.layerRoot = 36.0f;    // C2
        s.params.formant = false;
        s.params.vibrato = 0.5f;
        s.params.glideMs = 40.0f;
        s.params.attackMs = 5.0f;
        s.params.releaseMs = 120.0f;
        s.params.gateDb = -45.0f;
        s.params.motion = 0.0f;
        s.params.tone = 0.0f;
    }));

    list.push_back (make ("808 Lock", "Bass", [] (PluginSettings& s)
    {
        s.resolution = Resolution::deep;
        s.params.scale = Scale::chromatic;
        s.params.vibrato = 0.0f;
        s.params.glideMs = 20.0f;
        s.params.layerAuto = false;
        s.params.layerRoot = 36.0f;
        s.params.formant = false;
        s.params.attackMs = 1.0f;
        s.params.releaseMs = 150.0f;
        s.params.punch = 0.6f;
        s.params.gateDb = -40.0f;
        s.params.motion = 0.2f;
    }));

    list.push_back (make ("Growl Rider", "Bass", [] (PluginSettings& s)
    {
        s.resolution = Resolution::deep;
        s.params.glideMs = 80.0f;
        s.params.layerAuto = false;
        s.params.layerRoot = 45.0f;    // A2
        s.params.formant = false;
        s.params.attackMs = 3.0f;
        s.params.releaseMs = 80.0f;
        s.params.punch = 0.4f;
        s.params.gateDb = -45.0f;
        s.params.motion = 1.0f;
        s.params.tone = 0.2f;
    }));

    list.push_back (make ("Pad Follow", "Pads + Textures", [] (PluginSettings& s)
    {
        s.params.glideMs = 150.0f;
        s.params.vibrato = 0.5f;
        s.params.levelAmount = 0.7f;
        s.params.attackMs = 20.0f;
        s.params.releaseMs = 300.0f;
        s.params.gateDb = -80.0f;
        s.params.motion = 0.4f;
        s.params.tone = 0.3f;
    }));

    list.push_back (make ("Spectral Pad", "Pads + Textures", [] (PluginSettings& s)
    {
        s.engine = Engine::spectral;
        s.params.glideMs = 120.0f;
        s.params.vibrato = 0.6f;
        s.params.levelAmount = 0.8f;
        s.params.attackMs = 20.0f;
        s.params.releaseMs = 300.0f;
        s.params.gateDb = -80.0f;
        s.params.motion = 0.5f;
        s.params.tone = 0.3f;
    }));

    list.push_back (make ("Octave Up Shimmer", "Pads + Textures", [] (PluginSettings& s)
    {
        s.params.octave = 1;
        s.params.glideMs = 80.0f;
        s.params.levelAmount = 0.8f;
        s.params.attackMs = 30.0f;
        s.params.releaseMs = 400.0f;
        s.params.gateDb = -60.0f;
        s.params.motion = 0.3f;
        s.params.tone = 0.5f;
        s.params.mix = 0.6f;
    }));

    list.push_back (make ("Whisper Layer", "Pads + Textures", [] (PluginSettings& s)
    {
        s.params.pitchAmount = 0.0f;
        s.params.levelAmount = 1.0f;
        s.params.attackMs = 2.0f;
        s.params.releaseMs = 60.0f;
        s.params.gateDb = -50.0f;
        s.params.motion = 1.0f;
        s.params.tone = 0.6f;
    }));

    list.push_back (make ("Drone Glue", "Pads + Textures", [] (PluginSettings& s)
    {
        s.params.pitchAmount = 0.0f;
        s.params.levelAmount = 0.6f;
        s.params.attackMs = 30.0f;
        s.params.releaseMs = 500.0f;
        s.params.gateDb = -80.0f;
        s.params.motion = 0.6f;
        s.params.tone = 0.4f;
        s.params.mix = 0.5f;
    }));

    list.push_back (make ("Tight Sync", "Rhythmic", [] (PluginSettings& s)
    {
        s.resolution = Resolution::tight;
        s.params.glideMs = 5.0f;
        s.params.attackMs = 1.0f;
        s.params.releaseMs = 40.0f;
        s.params.punch = 0.3f;
        s.params.gateDb = -40.0f;
        s.params.motion = 0.5f;
    }));

    list.push_back (make ("Pluck Chaser", "Rhythmic", [] (PluginSettings& s)
    {
        s.resolution = Resolution::tight;
        s.params.glideMs = 0.0f;
        s.params.scale = Scale::chromatic;
        s.params.vibrato = 0.0f;
        s.params.attackMs = 0.5f;
        s.params.releaseMs = 60.0f;
        s.params.punch = 0.8f;
        s.params.gateDb = -35.0f;
        s.params.motion = 0.3f;
    }));

    return list;
}

} // namespace

const std::vector<FactoryPreset>& factoryPresets()
{
    static const std::vector<FactoryPreset> list = build();
    return list;
}
