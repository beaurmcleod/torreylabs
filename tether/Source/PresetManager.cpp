#include "PresetManager.h"

namespace
{
    constexpr auto presetNameProperty = "presetName";
    constexpr auto presetFactoryProperty = "presetIsFactory";
    constexpr auto fileExtension = ".tetherpreset";
}

PresetManager::PresetManager (juce::AudioProcessorValueTreeState& s)
    : state (s),
      userDirectory (juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                         .getChildFile ("Low End Candy").getChildFile ("Tether").getChildFile ("Presets"))
{
}

void PresetManager::setUserDirectory (const juce::File& folder)
{
    userDirectory = folder;
}

juce::File PresetManager::getUserDirectory() const
{
    return userDirectory;
}

int PresetManager::getNumFactoryPresets() const
{
    return (int) factoryPresets().size();
}

const FactoryPreset& PresetManager::getFactoryPreset (int index) const
{
    const auto& list = factoryPresets();
    return list[(size_t) juce::jlimit (0, (int) list.size() - 1, index)];
}

juce::String PresetManager::sanitiseName (const juce::String& name)
{
    return juce::File::createLegalFileName (name.trim()).substring (0, 60);
}

juce::File PresetManager::fileFor (const juce::String& name) const
{
    return userDirectory.getChildFile (sanitiseName (name) + fileExtension);
}

juce::StringArray PresetManager::getUserPresetNames() const
{
    juce::StringArray names;

    for (const auto& file : userDirectory.findChildFiles (juce::File::findFiles, false, "*" + juce::String (fileExtension)))
        names.add (file.getFileNameWithoutExtension());

    names.sortNatural();
    return names;
}

void PresetManager::setCurrent (const juce::String& name, bool factory)
{
    state.state.setProperty (presetNameProperty, name, nullptr);
    state.state.setProperty (presetFactoryProperty, factory, nullptr);

    if (onPresetChanged)
        onPresetChanged();
}

juce::String PresetManager::getCurrentName() const
{
    return state.state.getProperty (presetNameProperty, "Init").toString();
}

bool PresetManager::isCurrentFactory() const
{
    return (bool) state.state.getProperty (presetFactoryProperty, true);
}

void PresetManager::loadFactoryPreset (int index)
{
    const auto& preset = getFactoryPreset (index);
    applySettings (state, preset.settings);
    setCurrent (preset.name, true);
}

bool PresetManager::loadUserPreset (const juce::String& name)
{
    const auto xml = juce::XmlDocument::parse (fileFor (name));

    if (xml == nullptr || ! xml->hasTagName ("TetherPreset"))
        return false;

    for (const auto* child : xml->getChildWithTagNameIterator ("PARAM"))
    {
        if (auto* param = state.getParameter (child->getStringAttribute ("id")))
        {
            const float plain = (float) child->getDoubleAttribute ("value", param->convertFrom0to1 (param->getDefaultValue()));
            param->setValueNotifyingHost (param->convertTo0to1 (plain));
        }
    }

    setCurrent (name, false);
    return true;
}

bool PresetManager::saveUserPreset (const juce::String& rawName)
{
    const auto name = sanitiseName (rawName);

    if (name.isEmpty() || ! userDirectory.createDirectory())
        return false;

    juce::XmlElement xml ("TetherPreset");
    xml.setAttribute ("name", name);
    xml.setAttribute ("version", 1);

    for (auto* param : state.processor.getParameters())
    {
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param))
        {
            auto* element = xml.createNewChildElement ("PARAM");
            element->setAttribute ("id", ranged->paramID);
            element->setAttribute ("value", (double) ranged->convertFrom0to1 (ranged->getValue()));
        }
    }

    if (! xml.writeTo (fileFor (name)))
        return false;

    setCurrent (name, false);
    return true;
}

bool PresetManager::deleteUserPreset (const juce::String& name)
{
    return fileFor (name).deleteFile();
}

void PresetManager::loadNext (int direction)
{
    const int numFactory = getNumFactoryPresets();
    const auto users = getUserPresetNames();
    const int total = numFactory + users.size();

    if (total == 0)
        return;

    int current = 0;
    const auto name = getCurrentName();

    if (isCurrentFactory())
    {
        for (int i = 0; i < numFactory; ++i)
            if (name == getFactoryPreset (i).name)
                current = i;
    }
    else
    {
        const int index = users.indexOf (name);
        current = index >= 0 ? numFactory + index : 0;
    }

    const int next = ((current + (direction >= 0 ? 1 : -1)) % total + total) % total;

    if (next < numFactory)
        loadFactoryPreset (next);
    else
        loadUserPreset (users[next - numFactory]);
}
