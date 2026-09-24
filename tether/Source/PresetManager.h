#pragma once

#include "FactoryPresets.h"

#include <juce_audio_processors/juce_audio_processors.h>

/** Loads factory presets and saves / loads user presets (XML files in the
    user's documents folder). The current preset's name lives in the plugin
    state, so it survives a session reload. Message thread only. */
class PresetManager
{
public:
    explicit PresetManager (juce::AudioProcessorValueTreeState& state);

    /** Where user presets are kept; pass a folder to override (tests). */
    void setUserDirectory (const juce::File& folder);
    juce::File getUserDirectory() const;

    int getNumFactoryPresets() const;
    const FactoryPreset& getFactoryPreset (int index) const;
    juce::StringArray getUserPresetNames() const;   // scans the folder, sorted

    void loadFactoryPreset (int index);
    bool loadUserPreset (const juce::String& name);
    bool saveUserPreset (const juce::String& name);
    bool deleteUserPreset (const juce::String& name);

    /** Steps through factory then user presets; wraps around. */
    void loadNext (int direction);

    juce::String getCurrentName() const;
    bool isCurrentFactory() const;

    /** Called whenever a preset is loaded or saved. */
    std::function<void()> onPresetChanged;

    static juce::String sanitiseName (const juce::String& name);

private:
    juce::File fileFor (const juce::String& name) const;
    void setCurrent (const juce::String& name, bool factory);

    juce::AudioProcessorValueTreeState& state;
    juce::File userDirectory;
};
